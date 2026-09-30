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
 *   2. discoverDevices() runs `adb devices` and parses the tabular output
 *      to extract device serials that are in the "device" state.
 *
 *   3. start() launches `adb -s <serial> logcat` as a child process.
 *      Stdout is read incrementally (readyReadStandardOutput signal) and
 *      each complete line is written to a temporary file.
 *
 *   4. The host opens the temporary file with follow/tail mode, so lines
 *      appear in real-time as logcat produces output.
 *
 *   5. stop() terminates the child process gracefully.
 */

#include "adbprocess.h"
#include "plugin.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace logcat {

namespace {

/// Make a device serial usable as part of a file name on every platform.
QString safeFileName( const QString& serial )
{
    auto name = serial;
    name.replace( QRegularExpression( "[^a-zA-Z0-9._-]" ), "_" );
    return name;
}

} // namespace

// ── Construction / destruction ──────────────────────────────────────────

AdbProcess::AdbProcess( const QString& serial, const QString& savePath, QObject* parent )
    : QObject( parent )
    , serial_( serial )
    , savePath_( savePath )
{
    // Connect QProcess signals to our slots
    connect( &process_, &QProcess::readyReadStandardOutput, this, &AdbProcess::onReadyRead );
    connect( &process_, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
             &AdbProcess::onFinished );
    connect( &process_, &QProcess::errorOccurred, this, &AdbProcess::onErrorOccurred );
}

AdbProcess::~AdbProcess()
{
    // The owner may be half-destroyed already (QObject deletes its children
    // after the owner's own destructor has run), so stopping must not emit
    // finished() into it.
    blockSignals( true );
    stop();
}

// ── Static: ADB discovery ───────────────────────────────────────────────

QString AdbProcess::configDir()
{
    if ( g_state.api && g_state.handle ) {
        return QString::fromUtf8( g_state.api->get_config_dir( g_state.handle ) );
    }
    // Fallback for when plugin is not yet initialised (shouldn't happen in practice)
    return QStandardPaths::writableLocation( QStandardPaths::TempLocation );
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

    // 2. Check ANDROID_HOME/platform-tools/adb
    const auto androidHome = env.value( "ANDROID_HOME" );
    if ( !androidHome.isEmpty() ) {
        const auto candidate = QDir( androidHome ).filePath( "platform-tools/adb" );
        if ( QFileInfo::exists( candidate ) ) {
            return candidate;
        }
    }

    // 3. Check ANDROID_SDK_ROOT/platform-tools/adb (legacy)
    const auto sdkRoot = env.value( "ANDROID_SDK_ROOT" );
    if ( !sdkRoot.isEmpty() && sdkRoot != androidHome ) {
        const auto candidate = QDir( sdkRoot ).filePath( "platform-tools/adb" );
        if ( QFileInfo::exists( candidate ) ) {
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

QStringList AdbProcess::discoverDevices()
{
    const auto adb = findAdb();
    if ( adb.isEmpty() ) {
        hostLog( LOGSQUIRL_LOG_WARNING, "adb not found — cannot discover devices." );
        return {};
    }

    QProcess proc;
    proc.setProgram( adb );
    proc.setArguments( { "devices" } );
    proc.start();

    // Allow up to 10 s — the first invocation may need to start the ADB
    // server, which can take several seconds.
    if ( !proc.waitForFinished( 10000 ) ) {
        hostLog( LOGSQUIRL_LOG_WARNING, "adb devices timed out." );
        return {};
    }

    if ( proc.exitCode() != 0 ) {
        hostLog( LOGSQUIRL_LOG_WARNING, "adb devices failed: " + proc.readAllStandardError() );
        return {};
    }

    const auto output = proc.readAllStandardOutput();
    const auto devices = parseDeviceList( output );

    hostLog( LOGSQUIRL_LOG_INFO, QString( "Discovered %1 device(s)." ).arg( devices.size() ) );
    return devices;
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

QList<QByteArray> AdbProcess::takeLines( QByteArray& buffer )
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
    buffer.remove( 0, start );
    return lines;
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
        QDir().mkpath( QFileInfo( savePath_ ).absolutePath() );
        path = savePath_;
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
    tempFile_.setFileName( path );
    if ( !tempFile_.open( mode ) ) {
        Q_EMIT errorOccurred( "Failed to open log file: " + tempFile_.errorString() );
        tempFile_.setFileName( {} );
        return false;
    }
    usingSavePath_ = !savePath_.isEmpty();

    lineCount_ = 0;
    readBuffer_.clear();

    process_.setProgram( adb );
    process_.setArguments( { "-s", serial_, "logcat" } );
    process_.start();

    // Wait for the launch itself (not for any output) so that a failure is
    // reported to the caller here.  Otherwise QProcess reports it later,
    // as an error without a finished() signal, and the session would stay
    // registered with nothing running.  onErrorOccurred() has already
    // emitted the reason.
    if ( !process_.waitForStarted() ) {
        discardLogFile();
        return false;
    }

    hostLog( LOGSQUIRL_LOG_INFO, QString( "Started logcat for device %1" ).arg( serial_ ) );
    Q_EMIT started();
    return true;
}

void AdbProcess::stop()
{
    if ( !isRunning() ) {
        return;
    }

    // adb exits from the signal, which QProcess reports as a crash.  That
    // is expected here and must not reach the user as an error.
    stopping_ = true;
    process_.terminate();
    if ( !process_.waitForFinished( 3000 ) ) {
        process_.kill();
        process_.waitForFinished( 1000 );
    }
    stopping_ = false;

    tempFile_.close();

    hostLog( LOGSQUIRL_LOG_INFO, QString( "Stopped logcat for device %1 (%2 lines captured)" )
                                     .arg( serial_ )
                                     .arg( lineCount_ ) );
}

void AdbProcess::preserveTempFile()
{
    // When writing directly to the log directory, the temp dir is unused
    // and can be auto-removed safely.
    if ( !usingSavePath_ ) {
        tempDir_.setAutoRemove( false );
    }
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
    const auto oldPath = tempFile_.fileName();
    tempFile_.close();

    tempFile_.setFileName( newPath );
    if ( !tempFile_.open( QIODevice::WriteOnly | QIODevice::NewOnly ) ) {
        const auto reason = tempFile_.errorString();

        // Keep capturing into the old file: a session left running with a
        // closed file would silently drop everything from here on.
        tempFile_.setFileName( oldPath );
        if ( tempFile_.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
            Q_EMIT errorOccurred( QString( "Could not rotate the log to %1 (%2); "
                                           "still writing to %3." )
                                      .arg( newPath, reason, oldPath ) );
        }
        else {
            Q_EMIT errorOccurred( QString( "Could not rotate the log to %1 (%2), nor reopen "
                                           "%3 (%4); logcat stopped." )
                                      .arg( newPath, reason, oldPath, tempFile_.errorString() ) );
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
    return process_.state() != QProcess::NotRunning;
}

QString AdbProcess::tempFilePath() const
{
    return tempFile_.fileName();
}

// ── Private helpers ─────────────────────────────────────────────────────

void AdbProcess::writeLine( const QByteArray& line )
{
    tempFile_.write( line );
    tempFile_.write( "\n", 1 );
    ++lineCount_;
}

void AdbProcess::discardLogFile()
{
    tempFile_.close();
    if ( createdLogFile_ ) {
        tempFile_.remove();
    }
    tempFile_.setFileName( {} );
}

void AdbProcess::flushPartialLine()
{
    if ( readBuffer_.isEmpty() ) {
        return;
    }

    if ( readBuffer_.endsWith( '\r' ) ) {
        readBuffer_.chop( 1 );
    }
    writeLine( readBuffer_ );
    tempFile_.flush();
    readBuffer_.clear();
}

// ── Private slots ───────────────────────────────────────────────────────

void AdbProcess::onReadyRead()
{
    readBuffer_.append( process_.readAllStandardOutput() );

    const auto lines = takeLines( readBuffer_ );
    for ( const auto& line : lines ) {
        writeLine( line );
    }
    tempFile_.flush();
}

void AdbProcess::onFinished( int exitCode, QProcess::ExitStatus exitStatus )
{
    // Flush any remaining partial line in the buffer
    flushPartialLine();

    tempFile_.close();

    if ( exitStatus == QProcess::CrashExit && !stopping_ ) {
        hostLog( LOGSQUIRL_LOG_WARNING,
                 QString( "Logcat process for %1 crashed." ).arg( serial_ ) );
    }

    Q_EMIT finished( exitCode );
}

void AdbProcess::onErrorOccurred( QProcess::ProcessError error )
{
    if ( stopping_ ) {
        return;
    }

    QString message;
    switch ( error ) {
    case QProcess::FailedToStart:
        message = "ADB process failed to start.  Check that the ADB path is correct.";
        break;
    case QProcess::Crashed:
        message = QString( "ADB process for %1 crashed." ).arg( serial_ );
        break;
    case QProcess::Timedout:
        message = "ADB process timed out.";
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
