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
 * @file settings_test.cpp
 * @brief BDD tests for Settings: finding adb, the ADB path and the log
 *        directory, in a logcat.ini in a temporary directory.
 *
 * The SDKs and the PATH are temporary directories, named by ScopedEnv.  The
 * dialogs that set the ADB path are driven over a scan that the test
 * completes (fakeadbdevices.h).
 */

#include <catch2/catch.hpp>

#include "devicescan.h"
#include "devicewidget.h"
#include "fakeadb.h"
#include "fakeadbdevices.h"
#include "plugin.h"
#include "scopedenv.h"
#include "sessions.h"
#include "settings.h"
#include "settingsdialog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTimer>

using logcat::DeviceScan;
using logcat::DeviceWidget;
using logcat::Sessions;
using logcat::Settings;
using logcat::SettingsDialog;
using logcat_test::FakeAdbDevices;
using logcat_test::FakeHost;
using logcat_test::ScopedEnv;

namespace {

/// Create an adb in @p dir, named as an executable is on this platform,
/// and return its path.
QString installAdbIn( const QString& dir )
{
#ifdef Q_OS_WIN
    const auto path = dir + "/adb.exe";
#else
    const auto path = dir + "/adb";
#endif
    REQUIRE( QDir().mkpath( dir ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "#!/bin/sh\n" );
    file.setPermissions( file.permissions() | QFileDevice::ExeOwner );
    return path;
}

bool isSameFile( const QString& a, const QString& b )
{
    return !a.isEmpty() && QFileInfo( a ).canonicalFilePath() == QFileInfo( b ).canonicalFilePath();
}

} // namespace

SCENARIO( "findAdb looks for adb in the override, the SDK, then the PATH", "[settings]" )
{
    GIVEN( "an override, two SDKs and a PATH, each with an adb" )
    {
        QTemporaryDir config;
        QTemporaryDir root;
        Settings settings( config.path() );
        const auto override = installAdbIn( root.filePath( "mine" ) );
        const auto homeAdb = installAdbIn( root.filePath( "home/platform-tools" ) );
        const auto sdkRootAdb = installAdbIn( root.filePath( "sdkroot/platform-tools" ) );
        const auto pathAdb = installAdbIn( root.filePath( "bin" ) );
        REQUIRE( QDir().mkpath( root.filePath( "empty" ) ) );

        const ScopedEnv home( "ANDROID_HOME", QFile::encodeName( root.filePath( "home" ) ) );
        const ScopedEnv sdkRoot( "ANDROID_SDK_ROOT",
                                 QFile::encodeName( root.filePath( "sdkroot" ) ) );
        const ScopedEnv path( "PATH", QFile::encodeName( root.filePath( "bin" ) ) );
        REQUIRE( settings.setAdbPathOverride( override ) );

        THEN( "the override is found first" )
        {
            REQUIRE( isSameFile( settings.findAdb(), override ) );
        }

        WHEN( "the override names no file" )
        {
            REQUIRE( settings.setAdbPathOverride( root.filePath( "missing-adb" ) ) );

            THEN( "ANDROID_HOME's adb is found" )
            {
                REQUIRE( isSameFile( settings.findAdb(), homeAdb ) );
            }
        }

        WHEN( "there is no override" )
        {
            REQUIRE( settings.setAdbPathOverride( {} ) );

            THEN( "ANDROID_HOME's adb is found" )
            {
                REQUIRE( isSameFile( settings.findAdb(), homeAdb ) );
            }

            AND_WHEN( "ANDROID_HOME names an SDK without adb" )
            {
                const ScopedEnv emptyHome( "ANDROID_HOME",
                                           QFile::encodeName( root.filePath( "empty" ) ) );

                THEN( "ANDROID_SDK_ROOT's adb is found" )
                {
                    REQUIRE( isSameFile( settings.findAdb(), sdkRootAdb ) );
                }

                AND_WHEN( "ANDROID_SDK_ROOT does too" )
                {
                    const ScopedEnv emptyRoot( "ANDROID_SDK_ROOT",
                                               QFile::encodeName( root.filePath( "empty" ) ) );

                    THEN( "the PATH's adb is found" )
                    {
                        REQUIRE( isSameFile( settings.findAdb(), pathAdb ) );
                    }
                }
            }
        }
    }
}

SCENARIO( "the ADB path override is set and cleared", "[settings]" )
{
    GIVEN( "settings in a new configuration directory" )
    {
        QTemporaryDir config;
        Settings settings( config.path() );
        int changes = 0;
        QObject::connect( &settings, &Settings::adbPathChanged, [ &changes ]() { ++changes; } );

        THEN( "there is no override, and the file is logcat.ini there" )
        {
            REQUIRE( settings.adbPathOverride().isEmpty() );
            REQUIRE( settings.settingsFile() == QDir( config.path() ).filePath( "logcat.ini" ) );
        }

        WHEN( "an ADB path is set, with blanks around it" )
        {
            const auto adb = config.filePath( "adb" );
            REQUIRE( settings.setAdbPathOverride( "  " + adb + " " ) );

            THEN( "it is saved, trimmed, also for settings read after a restart, and says so once" )
            {
                REQUIRE( settings.adbPathOverride() == adb );
                REQUIRE( Settings( config.path() ).adbPathOverride() == adb );
                REQUIRE( changes == 1 );
            }

            AND_WHEN( "the same path is set again" )
            {
                REQUIRE( settings.setAdbPathOverride( adb ) );

                THEN( "nothing changed" )
                {
                    REQUIRE( changes == 1 );
                }
            }

            AND_WHEN( "it is cleared" )
            {
                REQUIRE( settings.setAdbPathOverride( {} ) );

                THEN( "there is no override again, and that is a change" )
                {
                    REQUIRE( settings.adbPathOverride().isEmpty() );
                    REQUIRE( Settings( config.path() ).adbPathOverride().isEmpty() );
                    REQUIRE( changes == 2 );
                }
            }
        }
    }
}

SCENARIO( "the log directory is logs/ in the configuration directory unless it is set",
          "[settings]" )
{
    GIVEN( "settings in a new configuration directory" )
    {
        QTemporaryDir config;
        Settings settings( config.path() );

        THEN( "the log directory is logs/ there, and none is set" )
        {
            REQUIRE( settings.logDir() == QDir( config.path() ).filePath( "logs" ) );
            REQUIRE( settings.customLogDir().isEmpty() );
        }

        WHEN( "a log directory is set" )
        {
            const auto mine = config.filePath( "mine" );
            settings.setLogDir( mine + " " );

            THEN( "it is the log directory, also for settings read after a restart" )
            {
                REQUIRE( settings.logDir() == mine );
                REQUIRE( settings.customLogDir() == mine );
                REQUIRE( Settings( config.path() ).logDir() == mine );
            }

            AND_WHEN( "it is cleared" )
            {
                settings.setLogDir( {} );

                THEN( "the default is the log directory again" )
                {
                    REQUIRE( settings.logDir() == QDir( config.path() ).filePath( "logs" ) );
                }
            }
        }
    }
}

SCENARIO( "a new ADB path from either dialog restarts the device scan", "[settings]" )
{
    GIVEN( "a device scan that hangs, the old dialog and the settings dialog" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host ); // only its path is used
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( &host.settings(), runner );
        Sessions sessions( &host.settings() );
        DeviceWidget widget( &host.settings(), &scan, &sessions );
        REQUIRE( runner->isRunning() );
        const auto otherAdb = host.configDir() + "/other-adb";
        {
            QFile file( otherAdb );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
        }

        WHEN( "another adb is entered in the old dialog's Configure" )
        {
            QTimer::singleShot( 0, [ &otherAdb ]() {
                if ( auto* dialog
                     = qobject_cast<QInputDialog*>( QApplication::activeModalWidget() ) ) {
                    dialog->setTextValue( otherAdb );
                    dialog->accept();
                }
            } );
            widget.configureAdbPath( nullptr );

            THEN( "it is saved, the hanging scan is abandoned, and the other adb runs" )
            {
                REQUIRE( host.settings().adbPathOverride() == otherAdb );
                REQUIRE( runner->abandoned == 1 );
                REQUIRE( runner->adbs.last() == otherAdb );
            }
        }

        WHEN( "another adb is entered in the settings dialog, and OK clicked" )
        {
            SettingsDialog dialog( &host.settings() );
            dialog.findChild<QLineEdit*>( "adbPath" )->setText( otherAdb );
            dialog.accept();

            THEN( "it is saved, the hanging scan is abandoned, and the other adb runs" )
            {
                REQUIRE( host.settings().adbPathOverride() == otherAdb );
                REQUIRE( runner->abandoned == 1 );
                REQUIRE( runner->adbs.last() == otherAdb );
            }
        }

        WHEN( "the settings dialog is OK'd with the ADB path as it was" )
        {
            SettingsDialog dialog( &host.settings() );
            dialog.accept();

            THEN( "the scan runs on" )
            {
                REQUIRE( runner->abandoned == 0 );
                REQUIRE( runner->adbs.size() == 1 );
            }
        }
    }
}

SCENARIO( "without a configuration directory no setting is read or saved", "[settings]" )
{
    GIVEN( "the plugin is not initialised, and a logcat.ini in the temporary directory" )
    {
        // Ensure clean state — no host API available.
        logcat::g_state.api = nullptr;
        logcat::g_state.handle = nullptr;
        logcat::g_state.initialised = false;

        // A temporary directory that every local user can write to, such
        // as /tmp, with an override another user planted.
        QTemporaryDir sharedTemp;
        const ScopedEnv tmpdir( "TMPDIR", QFile::encodeName( sharedTemp.path() ) );
        const ScopedEnv tmp( "TMP", QFile::encodeName( sharedTemp.path() ) );
        const ScopedEnv temp( "TEMP", QFile::encodeName( sharedTemp.path() ) );
        const auto planted = sharedTemp.filePath( "planted-adb" );
        {
            QFile file( planted );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
        }
        REQUIRE( Settings( sharedTemp.path() ).setAdbPathOverride( planted ) );

        Settings settings( Settings::hostConfigDir() );

        THEN( "there is no configuration directory, and no settings file" )
        {
            REQUIRE( Settings::hostConfigDir().isEmpty() );
            REQUIRE( settings.settingsFile().isEmpty() );
        }

        THEN( "findAdb() does not use the planted override" )
        {
            REQUIRE_FALSE( isSameFile( settings.findAdb(), planted ) );
        }

        THEN( "an ADB path cannot be saved, and there is no log directory" )
        {
            int changes = 0;
            QObject::connect( &settings, &Settings::adbPathChanged, [ &changes ]() { ++changes; } );
            REQUIRE_FALSE( settings.setAdbPathOverride( planted ) );
            REQUIRE( changes == 0 );
            settings.setLogDir( sharedTemp.path() );
            REQUIRE( settings.logDir().isEmpty() );
        }
    }
}
