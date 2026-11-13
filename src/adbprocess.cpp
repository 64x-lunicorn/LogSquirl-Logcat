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
 *   2. Sessions (sessions.h) runs `adb devices` in the background, and
 *      parseDevices() reads every device of its tabular output (`adb
 *      devices -l`: serial, state, model); parseDeviceList() keeps the
 *      serials of those in the "device" state.
 *
 *   3. start() launches `adb -s <serial> logcat` as a child process.
 *      Stdout is read incrementally (readyReadStandardOutput signal) and
 *      each complete line is written to the log file (logfile.h).
 *
 *   4. The host opens the log file with follow/tail mode, so lines
 *      appear in real-time as logcat produces output.
 *
 *   5. stop() ends the child process.
 */

#include "adbprocess.h"
#include "plugin.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace logcat {

namespace {

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
    , process_( new QProcess( this ) )
    , logFile_( serial, savePath )
{
    // Connect QProcess signals to our slots, each through guarded(): an
    // exception must not unwind through Qt's event loop into the host.
    connect( process_, &QProcess::readyReadStandardOutput, this,
             [ this ]() { guarded( "capturing logcat output", [ this ] { onReadyRead(); } ); } );
    connect( process_, &QProcess::readyReadStandardError, this, [ this ]() {
        guarded( "reading what adb reported", [ this ] { onReadyReadStandardError(); } );
    } );
    connect( process_, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
             [ this ]( int exitCode, QProcess::ExitStatus exitStatus ) {
                 guarded( "ending a logcat capture",
                          [ this, exitCode, exitStatus ] { onFinished( exitCode, exitStatus ); } );
             } );
    connect( process_, &QProcess::errorOccurred, this, [ this ]( QProcess::ProcessError error ) {
        guarded( "reporting an adb error", [ this, error ] { onErrorOccurred( error ); } );
    } );
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

QString AdbProcess::settingsFile()
{
    const auto cfgDir = configDir();
    return cfgDir.isEmpty() ? QString() : cfgDir + "/logcat.ini";
}

QString AdbProcess::adbPathOverride()
{
    const auto file = settingsFile();
    if ( file.isEmpty() ) {
        return {};
    }
    const QSettings settings( file, QSettings::IniFormat );
    return settings.value( "adb/path" ).toString();
}

bool AdbProcess::setAdbPathOverride( const QString& path )
{
    const auto file = settingsFile();
    if ( file.isEmpty() ) {
        return false;
    }
    QSettings settings( file, QSettings::IniFormat );
    settings.setValue( "adb/path", path );
    return true;
}

QString AdbProcess::findAdb()
{
    // 1. Check user override from plugin config
    if ( const auto override = adbPathOverride();
         !override.isEmpty() && QFileInfo::exists( override ) ) {
        return override;
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
    QStringList serials;
    const auto devices = parseDevices( output );
    for ( const auto& device : devices ) {
        if ( device.isReady() ) {
            serials.append( device.serial );
        }
    }
    return serials;
}

QList<Device> AdbProcess::parseDevices( const QByteArray& output )
{
    // Parse output.  Format, short (`adb devices`) or long (`-l`):
    //   List of devices attached
    //   SERIAL1\tdevice
    //   SERIAL2        device usb:1-1 product:shiba model:Pixel_8 device:shiba
    //   SERIAL3        unauthorized usb:1-2 transport_id:3
    //   SERIAL4        no permissions (…); see [http://…] usb:3-1
    //   <blank line>
    QList<Device> devices;
    const auto lines = output.split( '\n' );

    for ( const auto& line : lines ) {
        const auto trimmed = line.trimmed();
        // The header, and what adb says when it starts its server ("* daemon …")
        if ( trimmed.isEmpty() || trimmed.startsWith( "List of" ) || trimmed.startsWith( '*' ) ) {
            continue;
        }

        const auto fields = trimmed.simplified().split( ' ' );
        if ( fields.size() < 2 ) {
            continue;
        }

        Device device;
        device.serial = QString::fromUtf8( fields[ 0 ] );
        device.state = QString::fromUtf8( fields[ 1 ] );
        if ( device.state == QLatin1String( "no" ) && fields.size() > 2
             && fields[ 2 ] == "permissions" ) {
            device.state = QStringLiteral( "no permissions" );
        }
        for ( const auto& field : fields.mid( 2 ) ) {
            if ( field.startsWith( "model:" ) ) {
                device.model = QString::fromUtf8( field.mid( 6 ) ).replace( '_', ' ' );
            }
        }
        devices.append( device );
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

// ── Instance: start / stop ──────────────────────────────────────────────

bool AdbProcess::start()
{
    if ( isRunning() ) {
        return true;
    }

    // The path the user typed is checked first: it is theirs to fix,
    // whether or not adb is found.
    if ( const auto pathError = logFile_.pathError(); !pathError.isEmpty() ) {
        Q_EMIT errorOccurred( pathError );
        return false;
    }

    const auto adb = findAdb();
    if ( adb.isEmpty() ) {
        Q_EMIT errorOccurred( "ADB executable not found.\n\n"
                              "Set the path via Plugins → Configure, or ensure "
                              "ANDROID_HOME is set." );
        return false;
    }

    QString openError;
    if ( !logFile_.open( openError ) ) {
        Q_EMIT errorOccurred( openError );
        return false;
    }

    counters_ = {};
    // A save path is appended to: what it holds already is in the file.
    counters_.fileBytes = logFile_.size();
    readBuffer_.clear();
    stderrBuffer_.clear();
    lastStderrLine_.clear();

    process_->setProgram( adb );
    process_->setArguments( { "-s", serial_, "logcat" } );
    counters_.startTime = QDateTime::currentDateTime();
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
        logFile_.discard();
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
        session->logFile_.close();
        hostLog( LOGSQUIRL_LOG_INFO, QString( "Stopped logcat for device %1 (%2 lines captured)" )
                                         .arg( session->serial_ )
                                         .arg( session->counters_.lines ) );
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

void AdbProcess::end()
{
    endAll( { this } );
}

void AdbProcess::endAll( const QList<AdbProcess*>& sessions )
{
    // Cut first: stopping emits finished(), and an owner that acted on it
    // would end the session a second time.
    for ( auto* session : sessions ) {
        session->blockSignals( true );
    }
    stopAll( sessions );
    for ( auto* session : sessions ) {
        session->logFile_.keepTempDir();
        session->deleteLater();
    }
}

QString AdbProcess::rotateLog()
{
    if ( !isRunning() ) {
        return {};
    }

    // Flush any pending partial line to the old file before rotating
    flushPartialLine();

    QString error;
    switch ( logFile_.rotate( error ) ) {
    case LogFile::Rotation::Rotated:
        break;
    case LogFile::Rotation::KeptOldFile:
        Q_EMIT errorOccurred( error );
        return {};
    case LogFile::Rotation::Closed:
        Q_EMIT errorOccurred( error + "; logcat stopped." );
        stop();
        return {};
    }

    ++counters_.rotations;
    counters_.fileBytes = 0;

    hostLog( LOGSQUIRL_LOG_INFO, QString( "Rotated logcat log for %1 (rotation #%2)" )
                                     .arg( serial_ )
                                     .arg( counters_.rotations ) );

    return logFile_.path();
}

bool AdbProcess::isRunning() const
{
    return process_->state() != QProcess::NotRunning;
}

// ── Private helpers ─────────────────────────────────────────────────────

bool AdbProcess::writeLine( const QByteArray& line )
{
    // A failure is reported once; the session stops after it.
    if ( logFile_.failed() ) {
        return false;
    }
    if ( !logFile_.writeLine( line ) ) {
        failWrite();
        return false;
    }
    const auto written = line.size() + 1;
    ++counters_.lines;
    counters_.bytes += written;
    counters_.fileBytes += written;
    return true;
}

bool AdbProcess::flushLogFile()
{
    if ( logFile_.failed() ) {
        return false;
    }
    if ( !logFile_.flush() ) {
        failWrite();
        return false;
    }
    return true;
}

void AdbProcess::failWrite()
{
    // No file open (a Rotate that could open none) is not a write failure:
    // the session is stopping already.
    if ( !logFile_.failed() ) {
        return;
    }
    Q_EMIT errorOccurred( logFile_.failure() + "; logcat stopped." );
    // Not from inside the slot that is reading adb's output.
    QMetaObject::invokeMethod(
        this,
        [ this ]() { guarded( "stopping a capture whose log file failed", [ this ] { stop(); } ); },
        Qt::QueuedConnection );
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

    logFile_.close();

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
