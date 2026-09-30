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
 * @file plugin_test.cpp
 * @brief BDD tests for the plugin's lifecycle through its C entry points.
 *
 * Drives logsquirl_plugin_init() / logsquirl_plugin_shutdown() against a
 * FakeHost, and clicks the menu entry the plugin registers.
 */

#include <catch2/catch.hpp>

#include "devicewidget.h"
#include "fakeadb.h"
#include "plugin.h"
#include "sidebarwidget.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QTimer>
#include <QWidget>
#include <QWindow>

using logcat_test::FakeHost;
using logcat_test::waitFor;

extern "C" int logsquirl_plugin_init( const LogSquirlHostApi* api, void* handle );
extern "C" void logsquirl_plugin_shutdown( void );
extern "C" void logsquirl_plugin_configure( void* parent_widget );

SCENARIO( "the Android Logcat dialog belongs to LogSquirl's window", "[plugin]" )
{
    GIVEN( "an initialised plugin and an active main window" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host ); // no real adb scan
        REQUIRE( logsquirl_plugin_init( logcat::g_state.api, logcat::g_state.handle ) == 0 );
        REQUIRE( host.menuActions.size() == 1 );

        QWidget mainWindow;
        mainWindow.show();
        mainWindow.activateWindow();
        REQUIRE(
            waitFor( [ &mainWindow ]() { return QApplication::activeWindow() == &mainWindow; } ) );

        WHEN( "the user opens the dialog from the Plugins menu" )
        {
            host.menuActions.first().trigger();
            auto* dialog = logcat::g_state.dialog;

            THEN( "it is shown on top of the main window" )
            {
                REQUIRE( dialog->isVisible() );
                REQUIRE( dialog->windowHandle()->transientParent() == mainWindow.windowHandle() );
            }

            THEN( "an open dialog does not keep LogSquirl running when the main window closes" )
            {
                REQUIRE_FALSE( dialog->testAttribute( Qt::WA_QuitOnClose ) );
            }

            THEN( "the plugin, not the window, owns it" )
            {
                REQUIRE( dialog->parent() == nullptr );
            }
        }

        logsquirl_plugin_shutdown();
        REQUIRE( logcat::g_state.dialog == nullptr );
    }
}

#ifdef Q_OS_UNIX
SCENARIO( "a new ADB path from Plugins → Configure is used right away", "[plugin]" )
{
    GIVEN( "an initialised plugin whose device scan hangs" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "exec sleep 30\n", "exec sleep 30\n" );
        REQUIRE( logsquirl_plugin_init( logcat::g_state.api, logcat::g_state.handle ) == 0 );

        WHEN( "the user sets the path of a working adb in Plugins → Configure" )
        {
            const auto workingAdb = host.configDir() + "/other-adb";
            {
                QFile file( workingAdb );
                REQUIRE( file.open( QIODevice::WriteOnly ) );
                file.write( "#!/bin/sh\n"
                            "printf 'List of devices attached\\nemulator-5556\\tdevice\\n\\n'\n" );
                file.setPermissions( file.permissions() | QFileDevice::ExeOwner );
            }
            QTimer::singleShot( 0, [ &workingAdb ]() {
                if ( auto* dialog
                     = qobject_cast<QInputDialog*>( QApplication::activeModalWidget() ) ) {
                    dialog->setTextValue( workingAdb );
                    dialog->accept();
                }
            } );
            logsquirl_plugin_configure( nullptr );

            THEN( "the devices of the new adb reach the dialog and the sidebar" )
            {
                REQUIRE( waitFor(
                    []() {
                        return logcat::g_state.dialog->devices() == QStringList{ "emulator-5556" };
                    },
                    3000 ) );
                auto* sidebarCombo = logcat::g_state.sidebarWidget->findChild<QComboBox*>();
                REQUIRE( sidebarCombo );
                REQUIRE( sidebarCombo->findData( "emulator-5556" ) >= 0 );
            }
        }

        logsquirl_plugin_shutdown();
    }
}
#endif

#ifdef Q_OS_UNIX
namespace {

/** Directory of the temporary file a session opened in a tab. */
QString dirOf( const QString& file )
{
    return QFileInfo( file ).absolutePath();
}

/** Start a session, stop it, and start another, all in temp-file mode. */
void startStopAndStartAgain()
{
    auto* widget = logcat::g_state.dialog;
    REQUIRE( widget->startSession( "emulator-5554" ) );
    widget->stopSession( "emulator-5554" );
    REQUIRE( widget->startSession( "emulator-5556" ) );
}

} // namespace

SCENARIO( "temporary files are removed only when LogSquirl quits", "[plugin]" )
{
    GIVEN( "an initialised plugin with a stopped and a running temp-file session" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        const auto* api = logcat::g_state.api;
        auto* handle = logcat::g_state.handle;
        REQUIRE( logsquirl_plugin_init( api, handle ) == 0 );
        startStopAndStartAgain();
        REQUIRE( host.openedFiles.size() == 2 );
        const auto stoppedDir = dirOf( host.openedFiles.first() );
        const auto runningDir = dirOf( host.openedFiles.last() );

        WHEN( "LogSquirl quits, which shuts the plugin down" )
        {
            QMetaObject::invokeMethod( QCoreApplication::instance(), "aboutToQuit" );
            logsquirl_plugin_shutdown();

            THEN( "the temporary directories of both sessions are removed" )
            {
                REQUIRE_FALSE( QFileInfo::exists( stoppedDir ) );
                REQUIRE_FALSE( QFileInfo::exists( runningDir ) );
            }

            AND_WHEN( "the plugin is loaded again and later disabled" )
            {
                host.openedFiles.clear();
                REQUIRE( logsquirl_plugin_init( api, handle ) == 0 );
                REQUIRE( logcat::g_state.dialog->startSession( "emulator-5554" ) );
                const auto newDir = dirOf( host.openedFiles.first() );
                logsquirl_plugin_shutdown();

                THEN( "the earlier quit does not make it remove the new tab's file" )
                {
                    REQUIRE( QFileInfo::exists( newDir ) );
                }

                QDir( newDir ).removeRecursively();
            }
        }

        WHEN( "the plugin is disabled or updated while LogSquirl keeps running" )
        {
            logsquirl_plugin_shutdown();

            THEN( "the files of both sessions are kept for their open tabs" )
            {
                REQUIRE( QFileInfo::exists( host.openedFiles.first() ) );
                REQUIRE( QFileInfo::exists( host.openedFiles.last() ) );
            }

            QDir( stoppedDir ).removeRecursively();
            QDir( runningDir ).removeRecursively();
        }
    }
}
#endif
