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
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

namespace logcat {

// ── Construction / destruction ──────────────────────────────────────────

AdbProcess::AdbProcess( const QString& serial, const QString& savePath, QObject* parent )
    : QObject( parent )
    , serial_( serial )
    , savePath_( savePath )
{
    // Connect QProcess signals to our slots
    connect( &process_, &QProcess::readyReadStandardOutput,
             this, &AdbProcess::onReadyRead );
    connect( &process_, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ),
             this, &AdbProcess::onFinished );
    connect( &process_, &QProcess::errorOccurred,
             this, &AdbProcess::onErrorOccurred );
}

AdbProcess::~AdbProcess()
{
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
        hostLog( LOGSQUIRL_LOG_WARNING,
                 qPrintable( "adb devices failed: " + proc.readAllStandardError() ) );
        return {};
    }

    const auto output = proc.readAllStandardOutput();
    const auto devices = parseDeviceList( output );

    hostLog( LOGSQUIRL_LOG_INFO,
             qPrintable( QString( "Discovered %1 device(s)." ).arg( devices.size() ) ) );
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

// ── Instance: start / stop ──────────────────────────────────────────────

void AdbProcess::start()
{
    if ( isRunning() ) {
        return;
    }

    const auto adb = findAdb();
    if ( adb.isEmpty() ) {
        Q_EMIT errorOccurred( "ADB executable not found.\n\n"
                              "Set the path via Plugins → Configure, or ensure "
                              "ANDROID_HOME is set." );
        return;
    }

    if ( !tempDir_.isValid() ) {
        Q_EMIT errorOccurred( "Failed to create temporary directory." );
        return;
    }

    // Open the temporary file for writing
    const auto tempPath = tempDir_.path() + "/logcat_" + serial_ + ".log";
    tempFile_.setFileName( tempPath );
    if ( !tempFile_.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        Q_EMIT errorOccurred( "Failed to open temp file: " + tempFile_.errorString() );
        return;
    }

    // Optionally open the user-specified save file
    if ( !savePath_.isEmpty() ) {
        saveFile_.setFileName( savePath_ );
        if ( !saveFile_.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
            hostLog( LOGSQUIRL_LOG_WARNING,
                     qPrintable( "Cannot open save file: " + saveFile_.errorString() ) );
            // Continue without saving — the temp file still works.
        }
    }

    lineCount_ = 0;
    readBuffer_.clear();

    process_.setProgram( adb );
    process_.setArguments( { "-s", serial_, "logcat" } );
    process_.start();

    hostLog( LOGSQUIRL_LOG_INFO,
             qPrintable( QString( "Started logcat for device %1" ).arg( serial_ ) ) );
}

void AdbProcess::stop()
{
    if ( !isRunning() ) {
        return;
    }

    process_.terminate();
    if ( !process_.waitForFinished( 3000 ) ) {
        process_.kill();
        process_.waitForFinished( 1000 );
    }

    tempFile_.close();
    if ( saveFile_.isOpen() ) {
        saveFile_.close();
    }

    hostLog( LOGSQUIRL_LOG_INFO,
             qPrintable( QString( "Stopped logcat for device %1 (%2 lines captured)" )
                             .arg( serial_ )
                             .arg( lineCount_ ) ) );
}

void AdbProcess::preserveTempFile()
{
    tempDir_.setAutoRemove( false );
}

bool AdbProcess::isRunning() const
{
    return process_.state() != QProcess::NotRunning;
}

QString AdbProcess::tempFilePath() const
{
    return tempFile_.fileName();
}

// ── Private slots ───────────────────────────────────────────────────────

void AdbProcess::onReadyRead()
{
    // Append new data to the read buffer
    readBuffer_.append( process_.readAllStandardOutput() );

    // Process complete lines (split on '\n')
    int start = 0;
    for ( int i = 0; i < readBuffer_.size(); ++i ) {
        if ( readBuffer_[ i ] == '\n' ) {
            const auto lineData = readBuffer_.mid( start, i - start );
            start = i + 1;

            // Write to temp file (with newline)
            tempFile_.write( lineData );
            tempFile_.write( "\n", 1 );
            tempFile_.flush();

            // Write to save file if open
            if ( saveFile_.isOpen() ) {
                saveFile_.write( lineData );
                saveFile_.write( "\n", 1 );
                saveFile_.flush();
            }

            ++lineCount_;
        }
    }

    // Keep any incomplete trailing line in the buffer
    if ( start > 0 ) {
        readBuffer_.remove( 0, start );
    }
}

void AdbProcess::onFinished( int exitCode, QProcess::ExitStatus exitStatus )
{
    // Flush any remaining partial line in the buffer
    if ( !readBuffer_.isEmpty() ) {
        tempFile_.write( readBuffer_ );
        tempFile_.write( "\n", 1 );
        tempFile_.flush();

        if ( saveFile_.isOpen() ) {
            saveFile_.write( readBuffer_ );
            saveFile_.write( "\n", 1 );
            saveFile_.flush();
        }

        ++lineCount_;
        readBuffer_.clear();
    }

    tempFile_.close();
    if ( saveFile_.isOpen() ) {
        saveFile_.close();
    }

    if ( exitStatus == QProcess::CrashExit ) {
        hostLog( LOGSQUIRL_LOG_WARNING,
                 qPrintable( QString( "Logcat process for %1 crashed." ).arg( serial_ ) ) );
    }

    Q_EMIT finished( exitCode );
}

void AdbProcess::onErrorOccurred( QProcess::ProcessError error )
{
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

    hostLog( LOGSQUIRL_LOG_ERROR, qPrintable( message ) );
    Q_EMIT errorOccurred( message );
}

} // namespace logcat
