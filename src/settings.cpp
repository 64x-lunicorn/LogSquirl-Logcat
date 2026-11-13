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
 * @file settings.cpp
 * @brief Implementation of Settings.
 */

#include "settings.h"

#include "plugin.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace logcat {

namespace {

constexpr auto kAdbPath = "adb/path";
constexpr auto kLogDir = "logcat/logDir";

} // namespace

Settings::Settings( const QString& configDir, QObject* parent )
    : QObject( parent )
    , configDir_( configDir )
{
}

QString Settings::hostConfigDir()
{
    if ( g_state.api && g_state.handle ) {
        return QString::fromUtf8( g_state.api->get_config_dir( g_state.handle ) );
    }
    // No fallback: one in the temporary directory, /tmp on Linux, would
    // let any local user set the adb that LogSquirl runs.
    return {};
}

QString Settings::settingsFile() const
{
    return configDir_.isEmpty() ? QString() : configDir_ + "/logcat.ini";
}

QString Settings::adbPathOverride() const
{
    const auto file = settingsFile();
    if ( file.isEmpty() ) {
        return {};
    }
    const QSettings settings( file, QSettings::IniFormat );
    return settings.value( kAdbPath ).toString();
}

bool Settings::setAdbPathOverride( const QString& path )
{
    const auto trimmed = path.trimmed();
    if ( trimmed == adbPathOverride() ) {
        return true; // nothing to save
    }
    const auto file = settingsFile();
    if ( file.isEmpty() ) {
        hostLog( LOGSQUIRL_LOG_WARNING, "The ADB path cannot be configured: no config directory." );
        return false;
    }
    {
        QSettings settings( file, QSettings::IniFormat );
        settings.setValue( kAdbPath, trimmed );
    }
    hostLog( LOGSQUIRL_LOG_INFO, trimmed.isEmpty()
                                     ? "ADB path override cleared — using auto-detection."
                                     : "ADB path set to: " + trimmed );
    Q_EMIT adbPathChanged();
    return true;
}

QString Settings::findAdb() const
{
    // 1. The path the user set
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

    // 2. ANDROID_HOME/platform-tools/adb
    const auto androidHome = env.value( "ANDROID_HOME" );
    if ( const auto candidate = sdkAdb( androidHome ); !candidate.isEmpty() ) {
        return candidate;
    }

    // 3. ANDROID_SDK_ROOT/platform-tools/adb (legacy)
    const auto sdkRoot = env.value( "ANDROID_SDK_ROOT" );
    if ( sdkRoot != androidHome ) {
        if ( const auto candidate = sdkAdb( sdkRoot ); !candidate.isEmpty() ) {
            return candidate;
        }
    }

    // 4. The PATH
    if ( const auto pathAdb = QStandardPaths::findExecutable( "adb" ); !pathAdb.isEmpty() ) {
        return pathAdb;
    }

    // 5. Well-known platform-specific locations: on macOS, GUI apps
    //    launched from Finder/Dock have a minimal PATH that may not include
    //    Homebrew or user-installed SDK paths.
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

QString Settings::customLogDir() const
{
    const auto file = settingsFile();
    if ( file.isEmpty() ) {
        return {};
    }
    const QSettings settings( file, QSettings::IniFormat );
    return settings.value( kLogDir ).toString().trimmed();
}

void Settings::setLogDir( const QString& dir )
{
    const auto file = settingsFile();
    if ( file.isEmpty() ) {
        return;
    }
    QSettings settings( file, QSettings::IniFormat );
    settings.setValue( kLogDir, dir.trimmed() );
}

QString Settings::logDir() const
{
    if ( const auto dir = customLogDir(); !dir.isEmpty() ) {
        return dir;
    }
    // Default to logs/ beside the settings, so that captures are kept.
    return configDir_.isEmpty() ? QString() : configDir_ + "/logs";
}

} // namespace logcat
