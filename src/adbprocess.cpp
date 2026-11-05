/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of logsquirl-logcat.
 *
 * logsquirl-logcat is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl-logcat is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl-logcat.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file adbprocess.cpp
 * @brief Implementation of AdbProcess — ADB device discovery and logcat streaming.
 *
 * HOW IT WORKS
 * ────────────
 *   1. findAdb() locates the `adb` executable by checking:
 *      - User config override (logcat.ini)
 *      - ANDROID_HOME / ANDROID_SDK_ROOT environment variables
 *      - System PATH
 *
 *   2. DeviceWidget runs `adb devices` in the background, and
 *      parseDeviceList() extracts the serials of devices in the "device"
 *      state from its tabular output.
 *
 *   3. start() launches `adb -s <serial> logcat` as a child process.
 *      Stdout is read incrementally (readyReadStandardOutput signal) and
 *      each complete line is written to a temporary file.
 *
 *   4. The host opens the temporary file with follow/tail mode, so lines
 *      appear in real-time as logcat produces output.
 *
 *   5. stop() ends the child process.
 */

#include "adbprocess.h"
#include "plugin.h"
#include "tempdirs.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace logcat {

namespace {

/// Make a device serial usable as part of a file name on every platform.
QString safeFileName( const QString& serial )
{
    auto name = serial;
    name.replace( QRegularExpression( "[^a-zA-Z0-9._-]" ), "_" );
    return name;
}

/// @p text, cut short at AdbProcess::kMaxMessageChars characters.
QString messageText( const QString& text )
{
    if ( text.size() <= AdbProcess::kMaxMessageChars ) {
        return text;
    }
    auto length = AdbProcess::kMaxMessageChars;
    // Never split a surrogate pair.
    if ( text.at( length - 1 ).isHighSurrogate() ) {
        --length;
    }
    return text.left( length ) + QStringLiteral( "…" );
}

} // namespace

// ── Construction / destruction ──────────────────────────────────────────

AdbProcess::AdbProcess( const QString& serial, const QString& savePath, QObject* parent )
    : QObject( parent )
    , serial_( serial )
    , savePath_( savePath )
    , process_( new QProcess( this ) )
    , tempDir_( tempDirTemplate( tempRoot() ) )
{
    // Connect QProcess signals to our slots
    connect( process_, &QProcess::readyReadStandardOutput, this, &AdbProcess::onReadyRead );
    connect( process_, &QProcess::readyReadStandardError, this,
             &AdbProcess::onReadyReadStandardError );
    connect( process_, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
             &AdbProcess::onFinished );
    connect( process_, &QProcess::errorOccurred, this, &AdbProcess::onErrorOccurred );
}

AdbProcess::~AdbProcess()
{
    // The owner may be half-destroyed already (QObject deletes its children
    // after the owner's own destructor has run), so stopping must not emit
    // finished() into it.
    blockSignals( true );
    stop();

    // An adb that stop() could not reap would deliver its last output and
    // finished() to this object's slots, after the members they use are
    // gone.
    process_->disconnect( this );

    // ~QProcess would kill it and wait once more, for up to 30 s, on the
    // UI thread.  It is left to end on its own instead and deleted when it
    // does.  The connection names Qt's own signal and slot, so that no
    // code of this library is called if it has been unloaded by then.
    if ( process_->state() != QProcess::NotRunning ) {
        process_->setParent( nullptr );
        QObject::connect( process_, SIGNAL( finished( int, QProcess::ExitStatus ) ), process_,
                          SLOT( deleteLater() ) );
    }
}

// ── Static: ADB discovery ───────────────────────────────────────────────

QString AdbProcess::configDir()
{
    if ( g_state.api && g_state.handle ) {
        return QString::fromUtf8( g_state.api->get_config_dir( g_state.handle ) );
    }
    // No fallback: one in the temporary directory, /tmp on Linux, would
    // let any local user set the adb that LogSquirl runs.
    return {};
}

QString AdbProcess::findAdb()
{
    // 1. Check user override from plugin config
    const auto cfgDir = configDir();
    if ( !cfgDir.isEmpty() ) {
        QSettings settings( cfgDir + "/logcat.ini", QSettings::IniFormat );
        const auto override = settings.value( "adb/path" ).toString();
        if ( !override.isEmpty() && QFileInfo::exists( override ) ) {
            return override;
        }
    }

    const auto env = QProcessEnvironment::systemEnvironment();

    // The SDK's adb, as an executable is named on this platform: adb.exe
    // on Windows.
    const auto sdkAdb = []( const QString& sdk ) {
        return sdk.isEmpty() ? QString()
                             : QStandardPaths::findExecutable(
                                   "adb", { QDir( sdk ).filePath( "platform-tools" ) } );
    };

    // 2. Check ANDROID_HOME/platform-tools/adb
    const auto androidHome = env.value( "ANDROID_HOME" );
    if ( const auto candidate = sdkAdb( androidHome ); !candidate.isEmpty() ) {
        return candidate;
    }

    // 3. Check ANDROID_SDK_ROOT/platform-tools/adb (legacy)
    const auto sdkRoot = env.value( "ANDROID_SDK_ROOT" );
    if ( sdkRoot != androidHome ) {
        if ( const auto candidate = sdkAdb( sdkRoot ); !candidate.isEmpty() ) {
            return candidate;
        }
    }

    // 4. Fall back to system PATH
    const auto pathAdb = QStandardPaths::findExecutable( "adb" );
    if ( !pathAdb.isEmpty() ) {
        return pathAdb;
    }

    // 5. Check well-known platform-specific locations.
    //    On macOS, GUI apps launched from Finder/Dock have a minimal PATH
    //    that may not include Homebrew or user-installed SDK paths.
    const QStringList wellKnownPaths = {
#ifdef Q_OS_MACOS
        "/opt/homebrew/bin/adb",
        "/usr/local/bin/adb",
        QDir::homePath() + "/Library/Android/sdk/platform-tools/adb",
#endif
#ifdef Q_OS_LINUX
        "/usr/bin/adb",
        "/usr/local/bin/adb",
        QDir::homePath() + "/Android/Sdk/platform-tools/adb",
#endif
    };
    for ( const auto& candidate : wellKnownPaths ) {
        if ( QFileInfo::exists( candidate ) ) {
            return candidate;
        }
    }

    return {};
}

QStringList AdbProcess::parseDeviceList( const QByteArray& output )
{
    // Parse output.  Format:
    //   List of devices attached
    //   SERIAL1\tdevice
    //   SERIAL2\tunauthorized
    //   <blank line>
    QStringList devices;
    const auto lines = output.split( '\n' );

    for ( const auto& line : lines ) {
        const auto trimmed = line.trimmed();
        if ( trimmed.isEmpty() || trimmed.startsWith( "List of" ) ) {
            continue;
        }

        const auto parts = trimmed.split( '\t' );
        if ( parts.size() >= 2 && parts[ 1 ].trimmed() == "device" ) {
            devices.append( QString::fromUtf8( parts[ 0 ].trimmed() ) );
        }
    }

    return devices;
}

QList<QByteArray> AdbProcess::takeLines( QByteArray& buffer, qsizetype maxLineBytes )
{
    QList<QByteArray> lines;
    qsizetype start = 0;
    for ( auto end = buffer.indexOf( '\n' ); end >= 0; end = buffer.indexOf( '\n', start ) ) {
        auto line = buffer.mid( start, end - start );
        // adb on Windows, and some devices, end lines with "\r\n"
        if ( line.endsWith( '\r' ) ) {
            line.chop( 1 );
        }
        lines.append( line );
        start = end + 1;
    }
    // Output that never ends a line (a misbehaving adb, or another program
    // configured as adb) must not grow the buffer without limit.
    while ( buffer.size() - start > maxLineBytes ) {
        lines.append( buffer.mid( start, maxLineBytes ) );
        start += maxLineBytes;
    }
    buffer.remove( 0, start );
    return lines;
}

QString AdbProcess::absoluteUserPath( const QString& path )
{
    auto expanded = path.trimmed();
    if ( expanded == QStringLiteral( "~" ) ) {
        expanded = QDir::homePath();
    }
    else if ( expanded.startsWith( QStringLiteral( "~/" ) )
              || expanded.startsWith( QStringLiteral( "~\\" ) ) ) {
        expanded = QDir::homePath() + expanded.mid( 1 );
    }
    if ( expanded.isEmpty() || QDir::isRelativePath( expanded ) ) {
        return {};
    }
    return QDir::cleanPath( expanded );
}

QString AdbProcess::generateLogPath( const QString& dir, const QString& serial,
                                     const QDateTime& timestamp )
{
    const QDir logDir( dir );
    const auto stem = timestamp.toString( "yyyy-MM-dd_HHmmss" ) + "_" + safeFileName( serial );

    auto path = logDir.filePath( stem + ".log" );
    for ( int n = 2; QFileInfo::exists( path ); ++n ) {
        path = logDir.filePath( QString( "%1_%2.log" ).arg( stem ).arg( n ) );
    }
    return path;
}

// ── Instance: start / stop ──────────────────────────────────────────────

bool AdbProcess::start()
{
    if ( isRunning() ) {
        return true;
    }

    // The path the user typed is checked first: it is theirs to fix,
    // whether or not adb is found.
    if ( !savePath_.isEmpty() && absoluteUserPath( savePath_ ).isEmpty() ) {
        Q_EMIT errorOccurred(
            QString( "The log file %1 is not a full path; give one such as %2." )
                .arg( savePath_, QDir( QDir::homePath() ).filePath( "logcat.log" ) ) );
        return false;
    }

    const auto adb = findAdb();
    if ( adb.isEmpty() ) {
        Q_EMIT errorOccurred( "ADB executable not found.\n\n"
                              "Set the path via Plugins → Configure, or ensure "
                              "ANDROID_HOME is set." );
        return false;
    }

    // When a save path is configured, write directly to the log directory
    // instead of creating a temporary file.  This avoids accumulating
    // orphaned temp files and ensures the user's log directory is used.
    //
    // Nothing is ever truncated: a save path is appended to, so that Stop
    // and Start with the same path keep the earlier capture, and the temp
    // file must be new.
    QString path;
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    if ( !savePath_.isEmpty() ) {
        path = absoluteUserPath( savePath_ );
        QDir().mkpath( QFileInfo( path ).absolutePath() );
        mode |= QIODevice::Append;
    }
    else {
        if ( !tempDir_.isValid() ) {
            Q_EMIT errorOccurred( "Failed to create temporary directory." );
            return false;
        }
        path = tempDir_.filePath( "logcat_" + safeFileName( serial_ ) + ".log" );
        mode |= QIODevice::NewOnly;
    }

    createdLogFile_ = !QFileInfo::exists( path );
    QString openError;
    if ( !openLogFile( path, mode, openError ) ) {
        Q_EMIT errorOccurred( "Failed to open log file " + path + ": " + openError );
        tempFile_.setFileName( {} );
        logPath_.clear();
        return false;
    }
    usingSavePath_ = !savePath_.isEmpty();

    writeFailed_ = false;
    lineCount_ = 0;
    readBuffer_.clear();
    stderrBuffer_.clear();
    lastStderrLine_.clear();

    process_->setProgram( adb );
    process_->setArguments( { "-s", serial_, "logcat" } );
    process_->start();

    // Wait for the launch itself (not for any output) so that a failure is
    // reported to the caller here.  Otherwise QProcess reports it later,
    // as an error without a finished() signal, and the session would stay
    // registered with nothing running.  onErrorOccurred() has already
    // emitted the reason - except for a timeout, which QProcess only sets
    // as error() without emitting errorOccurred().  The wait is short:
    // it blocks the GUI.
    if ( !process_->waitForStarted( kStartTimeoutMs ) ) {
        if ( process_->state() != QProcess::NotRunning ) {
            onErrorOccurred( QProcess::Timedout );
            endProcesses( { this } );
        }
        discardLogFile();
        return false;
    }

    hostLog( LOGSQUIRL_LOG_INFO, QString( "Started logcat for device %1" ).arg( serial_ ) );
    Q_EMIT started();
    return true;
}

void AdbProcess::stop()
{
    stopAll( { this } );
}

void AdbProcess::stopAll( const QList<AdbProcess*>& sessions )
{
    // A session that a stop has given up on is not waited for again.
    QList<AdbProcess*> ending;
    for ( auto* session : sessions ) {
        if ( session && session->isRunning() && !session->stopping_ ) {
            ending.append( session );
        }
    }
    if ( ending.isEmpty() ) {
        return;
    }

    endProcesses( ending );

    for ( auto* session : ending ) {
        session->tempFile_.close();
        hostLog( LOGSQUIRL_LOG_INFO, QString( "Stopped logcat for device %1 (%2 lines captured)" )
                                         .arg( session->serial_ )
                                         .arg( session->lineCount_ ) );
    }
}

void AdbProcess::endProcesses( const QList<AdbProcess*>& sessions )
{
    // Wait for every session's adb to exit, against one deadline for all:
    // one that has exited while another was waited for is then only
    // collected.
    const auto waitForExit = [ &sessions ]() {
        const QDeadlineTimer deadline( kStopStepMs );
        for ( auto* session : sessions ) {
            if ( session->isRunning() ) {
                session->process_->waitForFinished(
                    static_cast<int>( std::max<qint64>( 0, deadline.remainingTime() ) ) );
            }
        }
    };

    // adb exits from the signal, which QProcess reports as a crash.  That
    // is expected here and must not reach the user as an error.  Every
    // session is told to end before any is waited for.
    for ( auto* session : sessions ) {
        session->stopping_ = true;
#ifdef Q_OS_WIN
        // terminate() only posts WM_CLOSE, which a console program like
        // adb never receives; waiting for it would just block the GUI.
        session->process_->kill();
#else
        session->process_->terminate();
#endif
    }
    waitForExit();

    bool killed = false;
    for ( auto* session : sessions ) {
        if ( session->isRunning() ) {
            session->process_->kill();
            killed = true;
        }
    }
    if ( killed ) {
        waitForExit();
    }

    for ( auto* session : sessions ) {
        if ( session->isRunning() ) {
            hostLog( LOGSQUIRL_LOG_WARNING,
                     QString( "adb for %1 did not exit when stopped." ).arg( session->serial_ ) );
        }
    }
    // onFinished() clears stopping_.  If adb has not exited yet, it stays
    // set, so that the exit, when it comes, is not reported as a crash,
    // and a later stop does not wait for it again.
}

QString AdbProcess::preserveTempFile()
{
    // When writing directly to the log directory, the temp dir is unused
    // and can be auto-removed safely.
    if ( usingSavePath_ ) {
        return {};
    }
    tempDir_.setAutoRemove( false );
    return tempDir_.path();
}

QString AdbProcess::rotateLog()
{
    if ( !isRunning() ) {
        return {};
    }

    // Flush any pending partial line to the old file before rotating
    flushPartialLine();

    // Generate the rotated file path.  When using the log directory,
    // create a new timestamped file there; otherwise use the temp dir.
    // Either way the file must be new: opening an existing one would
    // truncate an earlier capture.
    QString newPath;
    if ( usingSavePath_ ) {
        newPath = generateLogPath( QFileInfo( savePath_ ).absolutePath(), serial_ );
    }
    else {
        newPath = tempDir_.filePath( QString( "logcat_%1_%2.log" )
                                         .arg( safeFileName( serial_ ) )
                                         .arg( rotationCount_ + 1 ) );
    }

    // Close the old file (it stays on disk for the old tab)
    const auto oldPath = logPath_;
    tempFile_.close();

    QString reason;
    if ( !openLogFile( newPath, QIODevice::WriteOnly | QIODevice::NewOnly, reason ) ) {

        // Keep capturing into the old file: a session left running with a
        // closed file would silently drop everything from here on.
        QString reopenError;
        if ( openLogFile( oldPath, QIODevice::WriteOnly | QIODevice::Append, reopenError ) ) {
            Q_EMIT errorOccurred( QString( "Could not rotate the log to %1 (%2); "
                                           "still writing to %3." )
                                      .arg( newPath, reason, oldPath ) );
        }
        else {
            Q_EMIT errorOccurred( QString( "Could not rotate the log to %1 (%2), nor reopen "
                                           "%3 (%4); logcat stopped." )
                                      .arg( newPath, reason, oldPath, reopenError ) );
            stop();
        }
        return {};
    }

    ++rotationCount_;
    lineCount_ = 0;

    hostLog( LOGSQUIRL_LOG_INFO, QString( "Rotated logcat log for %1 (rotation #%2)" )
                                     .arg( serial_ )
                                     .arg( rotationCount_ ) );

    return newPath;
}

bool AdbProcess::isRunning() const
{
    return process_->state() != QProcess::NotRunning;
}

QString AdbProcess::tempFilePath() const
{
    return logPath_;
}

// ── Private helpers ─────────────────────────────────────────────────────

bool AdbProcess::writeLine( const QByteArray& line )
{
    if ( writeFailed_ ) {
        return false;
    }
    if ( tempFile_.write( line ) != line.size() || tempFile_.write( "\n", 1 ) != 1 ) {
        failWrite();
        return false;
    }
    ++lineCount_;
    return true;
}

bool AdbProcess::flushLogFile()
{
    if ( writeFailed_ ) {
        return false;
    }
    if ( !tempFile_.flush() ) {
        failWrite();
        return false;
    }
    return true;
}

void AdbProcess::failWrite()
{
    if ( writeFailed_ ) {
        return;
    }
    writeFailed_ = true;
    Q_EMIT errorOccurred( QString( "Could not write the log file %1 (%2); logcat stopped." )
                              .arg( logPath_, tempFile_.errorString() ) );
    // Not from inside the slot that is reading adb's output.
    QMetaObject::invokeMethod( this, &AdbProcess::stop, Qt::QueuedConnection );
}

bool AdbProcess::openLogFile( const QString& path, QIODevice::OpenMode mode, QString& error )
{
    tempFile_.setFileName( path );
#ifdef Q_OS_UNIX
    // One open that cannot block on a FIFO nor follow a symlink, then a
    // check of what was opened: a check before the open could be raced.
    int flags = O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    flags |= mode.testFlag( QIODevice::NewOnly ) ? O_EXCL : 0;
    flags |= mode.testFlag( QIODevice::Append ) ? O_APPEND : 0;
    const int fd = ::open( QFile::encodeName( path ).constData(), flags, S_IRUSR | S_IWUSR );
    if ( fd < 0 ) {
        // A FIFO without a reader (ENXIO) and a symlink (ELOOP) are what
        // the flags refuse; anything else is the system's own reason.
        if ( errno == ENXIO ) {
            error = QStringLiteral( "not a regular file" );
        }
        else if ( errno == ELOOP ) {
            error = QStringLiteral( "a symbolic link, not a regular file" );
        }
        else {
            error = QString::fromLocal8Bit( std::strerror( errno ) );
        }
        return false;
    }
    struct stat info{};
    if ( ::fstat( fd, &info ) != 0 || !S_ISREG( info.st_mode ) ) {
        ::close( fd );
        error = QStringLiteral( "not a regular file" );
        return false;
    }
    // A regular file never blocks; the flag only kept the open from waiting.
    ::fcntl( fd, F_SETFL, ::fcntl( fd, F_GETFL ) & ~O_NONBLOCK );
    if ( !tempFile_.open( fd, mode & ~QIODevice::NewOnly, QFileDevice::AutoCloseHandle ) ) {
        ::close( fd );
        error = tempFile_.errorString();
        return false;
    }
    logPath_ = path;
    return true;
#else
    const QFileInfo info( path );
    if ( info.isSymLink() || ( info.exists() && !info.isFile() ) ) {
        error = QStringLiteral( "not a regular file" );
        return false;
    }
    if ( !tempFile_.open( mode ) ) {
        error = tempFile_.errorString();
        return false;
    }
    if ( mode.testFlag( QIODevice::NewOnly ) || !info.exists() ) {
        tempFile_.setPermissions( QFileDevice::ReadOwner | QFileDevice::WriteOwner );
    }
    logPath_ = path;
    return true;
#endif
}

void AdbProcess::discardLogFile()
{
    tempFile_.close();
    if ( createdLogFile_ && !logPath_.isEmpty() ) {
        QFile::remove( logPath_ );
    }
    tempFile_.setFileName( {} );
    logPath_.clear();
}

void AdbProcess::flushPartialLine()
{
    if ( readBuffer_.isEmpty() ) {
        return;
    }

    if ( readBuffer_.endsWith( '\r' ) ) {
        readBuffer_.chop( 1 );
    }
    if ( writeLine( readBuffer_ ) ) {
        flushLogFile();
    }
    readBuffer_.clear();
}

// ── Private slots ───────────────────────────────────────────────────────

void AdbProcess::onReadyRead()
{
    readBuffer_.append( process_->readAllStandardOutput() );

    const auto lines = takeLines( readBuffer_, kMaxLineBytes );
    for ( const auto& line : lines ) {
        if ( !writeLine( line ) ) {
            return;
        }
    }
    flushLogFile();
}

void AdbProcess::onReadyReadStandardError()
{
    // adb reports problems (device offline, unauthorised, server restarts)
    // on stderr.  Pass them on to the host log; the last one explains an
    // unexpected exit in onFinished().
    stderrBuffer_.append( process_->readAllStandardError() );
    for ( const auto& line : takeLines( stderrBuffer_ ) ) {
        const auto text = messageText( QString::fromUtf8( line ).trimmed() );
        if ( !text.isEmpty() ) {
            lastStderrLine_ = text;
            hostLog( LOGSQUIRL_LOG_WARNING, QString( "adb (%1): %2" ).arg( serial_, text ) );
        }
    }
    // Only the start of a line reaches a message: the rest of one that
    // never ends is dropped rather than kept.
    if ( stderrBuffer_.size() > kMaxLineBytes ) {
        stderrBuffer_.truncate( kMaxLineBytes );
    }
}

void AdbProcess::onFinished( int exitCode, QProcess::ExitStatus exitStatus )
{
    // Flush any remaining partial line in the buffer
    flushPartialLine();

    tempFile_.close();

    // Collect what adb wrote to stderr just before it exited, including a
    // last line without a terminator
    onReadyReadStandardError();
    if ( !stderrBuffer_.isEmpty() ) {
        stderrBuffer_.append( '\n' );
        onReadyReadStandardError();
    }

    if ( !stopping_ && ( exitStatus == QProcess::CrashExit || exitCode != 0 ) ) {
        auto message
            = exitStatus == QProcess::CrashExit
                  ? QString( "ADB process for %1 crashed." ).arg( serial_ )
                  : QString( "adb for %1 exited with code %2." ).arg( serial_ ).arg( exitCode );
        if ( !lastStderrLine_.isEmpty() ) {
            message += " " + lastStderrLine_;
        }
        Q_EMIT errorOccurred( message );
    }

    stopping_ = false;
    Q_EMIT finished( exitCode );
}

void AdbProcess::onErrorOccurred( QProcess::ProcessError error )
{
    // A crash is followed by finished(); onFinished() reports it together
    // with what adb last wrote to stderr.  While stop() ends the process,
    // the "crash" is expected.
    if ( error == QProcess::Crashed || stopping_ ) {
        return;
    }

    QString message;
    switch ( error ) {
    case QProcess::FailedToStart:
        message = "ADB process failed to start.  Check that the ADB path is correct.";
        break;
    case QProcess::Timedout:
        // Only start() reports this: stop() sets stopping_ while it waits.
        message = QString( "adb did not start within %1 s." ).arg( kStartTimeoutMs / 1000 );
        break;
    default:
        message = QString( "ADB process error (%1)." ).arg( static_cast<int>( error ) );
        break;
    }

    // The receiver logs and shows the message; logging it here as well
    // would report every error twice.
    Q_EMIT errorOccurred( message );
}

} // namespace logcat
