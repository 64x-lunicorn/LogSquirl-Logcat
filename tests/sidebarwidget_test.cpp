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
 * @file sidebarwidget_test.cpp
 * @brief BDD tests for the sidebar tab: one tile per device (issue #48).
 *
 * The sidebar is driven through its widgets (the tiles' named children, the
 * Settings dialog) over a real Sessions and DeviceStore, with a FakeHost
 * and a stand-in for adb whose device list a test changes: plugging and
 * unplugging a device is writing a line to that list (see attachedFile()).
 */

#include <catch2/catch.hpp>

#include "devicestore.h"
#include "devicetile.h"
#include "fakeadb.h"
#include "sessions.h"
#include "settingsdialog.h"
#include "sidebarwidget.h"

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <memory>

using logcat::DeviceStore;
using logcat::DeviceTile;
using logcat::Sessions;
using logcat::SettingsDialog;
using logcat::SidebarWidget;
using logcat::TileState;
using logcat_test::FakeHost;
using logcat_test::waitFor;

namespace {

/// The file the fake adb lists as its devices, after its header.
QString attachedFile( const FakeHost& host )
{
    return host.configDir() + "/attached.txt";
}

/// Attach the devices of @p lines (`adb devices -l` lines), and no other.
void attach( const FakeHost& host, const QByteArray& lines )
{
    QFile file( attachedFile( host ) );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( lines );
}

#ifdef Q_OS_UNIX
/// Install an adb that lists the devices attach() attached, none at first.
void installAdbWithAttachedDevices( const FakeHost& host,
                                    const QByteArray& logcatScript
                                    = "printf 'first\\nsecond\\n'\nexec sleep 30\n" )
{
    attach( host, {} );
    logcat_test::installFakeAdb( host, logcatScript,
                                 "printf 'List of devices attached\\n'\ncat '"
                                     + attachedFile( host ).toUtf8() + "'\n" );
}
#endif

/// The tile of @p serial in @p sidebar, or nullptr.
DeviceTile* tileOf( const SidebarWidget& sidebar, const QString& serial )
{
    const auto tiles = sidebar.findChildren<DeviceTile*>();
    for ( auto* tile : tiles ) {
        if ( tile->serial() == serial ) {
            return tile;
        }
    }
    return nullptr;
}

QPushButton* button( const QWidget* widget, const char* name )
{
    return widget->findChild<QPushButton*>( name );
}

QString textOf( const QWidget* widget, const char* name )
{
    return widget->findChild<QLabel*>( name )->text();
}

/// Rescan, and wait until the scan has ended.
void rescan( Sessions& sessions )
{
    int changes = 0;
    const auto connection
        = QObject::connect( &sessions, &Sessions::devicesChanged, [ &changes ]() { ++changes; } );
    sessions.refreshDevices();
    REQUIRE(
        waitFor( [ &changes, &sessions ]() { return changes > 0 && !sessions.isScanning(); } ) );
    QObject::disconnect( connection );
}

} // namespace

SCENARIO( "a tile is titled by the model and the serial of its device", "[sidebarwidget]" )
{
    using logcat::deviceTitle;

    THEN( "a long serial is cut after its first four characters" )
    {
        REQUIRE( deviceTitle( "3A1B2C3D4E5F", "Pixel 8" ) == "Pixel 8 · 3A1B…" );
    }

    THEN( "a short serial is shown whole" )
    {
        REQUIRE( deviceTitle( "ABC123", "Pixel 8" ) == "Pixel 8 · ABC123" );
    }

    THEN( "without a model, the serial is the title, whole" )
    {
        REQUIRE( deviceTitle( "emulator-5554", {} ) == "emulator-5554" );
    }
}

#ifdef Q_OS_UNIX
SCENARIO( "the sidebar has a tile for each device, plugged in or seen before", "[sidebarwidget]" )
{
    GIVEN( "a sidebar and an adb without devices" )
    {
        FakeHost host;
        installAdbWithAttachedDevices( host );
        const auto settingsFile = host.configDir() + "/logcat.ini";
        auto store = std::make_unique<DeviceStore>( settingsFile );
        auto sessions = std::make_unique<Sessions>();
        auto sidebar = std::make_unique<SidebarWidget>( store.get(), sessions.get() );
        REQUIRE( waitFor( [ &sessions ]() { return !sessions->isScanning(); } ) );

        THEN( "it has no tile, and says how to get one" )
        {
            REQUIRE( sidebar->findChildren<DeviceTile*>().isEmpty() );
            REQUIRE( sidebar->findChild<QLabel*>( "empty" )->isVisibleTo( sidebar.get() ) );
        }

        WHEN( "a device is plugged in" )
        {
            attach( host, "3A1B2C3D4E5F  device usb:1-1 product:shiba model:Pixel_8\n" );

            THEN( "a tile for it appears by itself, titled by model and serial, ready to start" )
            {
                DeviceTile* tile = nullptr;
                REQUIRE( waitFor(
                    [ & ]() {
                        tile = tileOf( *sidebar, "3A1B2C3D4E5F" );
                        return tile != nullptr;
                    },
                    SidebarWidget::kScanIntervalMs + 3000 ) );
                REQUIRE( textOf( tile, "title" ) == "Pixel 8 · 3A1B…" );
                REQUIRE( textOf( tile, "summary" ).contains( "3A1B2C3D4E5F · connected" ) );
                REQUIRE( tile->state() == TileState::Connected );
                REQUIRE( button( tile, "startStop" )->isEnabled() );
                REQUIRE_FALSE(
                    sidebar->findChild<QLabel*>( "empty" )->isVisibleTo( sidebar.get() ) );
            }

            AND_WHEN( "it is unplugged" )
            {
                rescan( *sessions );
                REQUIRE( tileOf( *sidebar, "3A1B2C3D4E5F" ) );
                attach( host, {} );
                rescan( *sessions );
                auto* tile = tileOf( *sidebar, "3A1B2C3D4E5F" );

                THEN( "its tile stays, greyed out, Start disabled, with Forget" )
                {
                    REQUIRE( tile );
                    REQUIRE( tile->state() == TileState::Disconnected );
                    REQUIRE( tile->property( "disconnected" ).toBool() );
                    REQUIRE( textOf( tile, "title" ) == "Pixel 8 · 3A1B…" );
                    REQUIRE( textOf( tile, "summary" ).contains( "disconnected" ) );
                    REQUIRE_FALSE( button( tile, "startStop" )->isEnabled() );
                    REQUIRE( button( tile, "forget" )->isVisibleTo( tile ) );
                }

                AND_WHEN( "LogSquirl is restarted" )
                {
                    sidebar.reset();
                    sessions.reset();
                    store.reset();
                    store = std::make_unique<DeviceStore>( settingsFile );
                    sessions = std::make_unique<Sessions>();
                    sidebar = std::make_unique<SidebarWidget>( store.get(), sessions.get() );
                    REQUIRE( waitFor( [ &sessions ]() { return !sessions->isScanning(); } ) );

                    THEN( "the device it remembers has its tile, disconnected" )
                    {
                        auto* again = tileOf( *sidebar, "3A1B2C3D4E5F" );
                        REQUIRE( again );
                        REQUIRE( again->state() == TileState::Disconnected );
                        REQUIRE( textOf( again, "title" ) == "Pixel 8 · 3A1B…" );
                    }
                }

                AND_WHEN( "Forget is clicked" )
                {
                    button( tile, "forget" )->click();
                    logcat_test::processEventsFor( 50 );

                    THEN( "its tile is gone, also after a restart" )
                    {
                        REQUIRE( tileOf( *sidebar, "3A1B2C3D4E5F" ) == nullptr );
                        REQUIRE( DeviceStore( settingsFile ).list().isEmpty() );
                    }
                }
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "Start and Stop on a tile run logcat for its device", "[sidebarwidget]" )
{
    GIVEN( "a sidebar with two connected devices" )
    {
        FakeHost host;
        installAdbWithAttachedDevices( host );
        attach( host, "emulator-5554  device model:sdk_gphone64\n"
                      "emulator-5556  device model:sdk_gphone64\n" );
        DeviceStore store( host.configDir() + "/logcat.ini" );
        Sessions sessions;
        const SidebarWidget sidebar( &store, &sessions );
        REQUIRE( waitFor( [ &sessions ]() { return sessions.devices().size() == 2; } ) );
        auto* first = tileOf( sidebar, "emulator-5554" );
        auto* second = tileOf( sidebar, "emulator-5556" );
        REQUIRE( first );
        REQUIRE( second );

        WHEN( "Start is clicked on both" )
        {
            button( first, "startStop" )->click();
            button( second, "startStop" )->click();

            THEN( "both run, each followed in a tab of its own, in the log directory" )
            {
                REQUIRE( sessions.isSessionActive( "emulator-5554" ) );
                REQUIRE( sessions.isSessionActive( "emulator-5556" ) );
                REQUIRE( host.openedFiles.size() == 2 );
                REQUIRE( host.openedFiles[ 0 ].endsWith( "_emulator-5554.log" ) );
                REQUIRE( host.openedFiles[ 1 ].endsWith( "_emulator-5556.log" ) );
                REQUIRE( QFileInfo( host.openedFiles[ 0 ] ).absolutePath()
                         == QDir( host.configDir() ).filePath( "logs" ) );
                REQUIRE( first->state() == TileState::Running );
                REQUIRE( second->state() == TileState::Running );
                REQUIRE( button( first, "startStop" )->text().contains( "Stop" ) );
            }

            AND_WHEN( "Stop is clicked on one" )
            {
                button( first, "startStop" )->click();

                THEN( "it stops, its tab keeps its file, and the other runs on" )
                {
                    REQUIRE_FALSE( sessions.isSessionActive( "emulator-5554" ) );
                    REQUIRE( sessions.isSessionActive( "emulator-5556" ) );
                    REQUIRE( first->state() == TileState::Connected );
                    REQUIRE( QFileInfo::exists( host.openedFiles[ 0 ] ) );
                }
            }

            AND_WHEN( "Rotate is clicked on one" )
            {
                button( first, "rotate" )->click();

                THEN( "it goes on in a new file, followed in a new tab" )
                {
                    REQUIRE( host.openedFiles.size() == 3 );
                    REQUIRE( sessions.isSessionActive( "emulator-5554" ) );
                }
            }

            AND_WHEN( "Stop all is clicked" )
            {
                button( &sidebar, "stopAll" )->click();

                THEN( "both stop" )
                {
                    REQUIRE( sessions.activeSessionCount() == 0 );
                    REQUIRE( first->state() == TileState::Connected );
                    REQUIRE( second->state() == TileState::Connected );
                }
            }

            sessions.stopAll();
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "unauthorized and offline devices cannot be started", "[sidebarwidget]" )
{
    GIVEN( "a sidebar with an unauthorized and an offline device" )
    {
        FakeHost host;
        installAdbWithAttachedDevices( host );
        attach( host, "NOAUTH456   unauthorized usb:1-2 transport_id:3\n"
                      "OFFLINE123  offline transport_id:4\n" );
        DeviceStore store( host.configDir() + "/logcat.ini" );
        Sessions sessions;
        const SidebarWidget sidebar( &store, &sessions );
        REQUIRE( waitFor( [ &sessions ]() { return sessions.attachedDevices().size() == 2; } ) );
        auto* unauthorized = tileOf( sidebar, "NOAUTH456" );
        auto* offline = tileOf( sidebar, "OFFLINE123" );
        REQUIRE( unauthorized );
        REQUIRE( offline );

        THEN( "each tile says so, and Start is disabled" )
        {
            REQUIRE( unauthorized->state() == TileState::Unauthorized );
            REQUIRE( textOf( unauthorized, "summary" ).contains( "unauthorized" ) );
            REQUIRE_FALSE( button( unauthorized, "startStop" )->isEnabled() );
            REQUIRE( offline->state() == TileState::Offline );
            REQUIRE( textOf( offline, "summary" ).contains( "offline" ) );
            REQUIRE_FALSE( button( offline, "startStop" )->isEnabled() );
        }

        WHEN( "Start is clicked anyway" )
        {
            button( unauthorized, "startStop" )->click();
            button( offline, "startStop" )->click();

            THEN( "nothing starts" )
            {
                REQUIRE( sessions.activeSessionCount() == 0 );
                REQUIRE( host.openedFiles.isEmpty() );
            }
        }

        WHEN( "the unauthorized device is allowed" )
        {
            attach( host, "NOAUTH456   device usb:1-2 model:Galaxy_S23\n"
                          "OFFLINE123  offline transport_id:4\n" );
            rescan( sessions );

            THEN( "its tile can be started, and learns its model" )
            {
                REQUIRE( unauthorized->state() == TileState::Connected );
                REQUIRE( button( unauthorized, "startStop" )->isEnabled() );
                REQUIRE( textOf( unauthorized, "title" ) == "Galaxy S23 · NOAU…" );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "a device's own save path is set from its tile", "[sidebarwidget]" )
{
    GIVEN( "a sidebar with a connected device" )
    {
        FakeHost host;
        installAdbWithAttachedDevices( host );
        attach( host, "emulator-5554  device\n" );
        DeviceStore store( host.configDir() + "/logcat.ini" );
        Sessions sessions;
        const SidebarWidget sidebar( &store, &sessions );
        REQUIRE( waitFor( [ &sessions ]() { return !sessions.devices().isEmpty(); } ) );
        auto* tile = tileOf( sidebar, "emulator-5554" );
        REQUIRE( tile );
        REQUIRE( textOf( tile, "summary" ).contains( "log directory" ) );

        WHEN( "Save to… is clicked, and a file is entered" )
        {
            button( tile, "savePath" )->click();
            auto* dialog = sidebar.findChild<QDialog*>( "savePathDialog" );
            REQUIRE( dialog );
            REQUIRE( dialog->isVisible() );
            const auto path = host.tempRoot() + "/phone.log";
            dialog->findChild<QLineEdit*>( "savePath" )->setText( path );
            dialog->accept();

            THEN( "the tile says where it saves, and Start appends to that file" )
            {
                REQUIRE( store.find( "emulator-5554" )->savePath == path );
                REQUIRE( textOf( tile, "summary" ).contains( path ) );
                button( tile, "startStop" )->click();
                REQUIRE( host.openedFiles == QStringList{ path } );
                sessions.stopAll();
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "the settings dialog opened from the tab sets the log directory and adb",
          "[sidebarwidget]" )
{
    GIVEN( "a sidebar whose adb lists no device" )
    {
        FakeHost host;
        installAdbWithAttachedDevices( host );
        DeviceStore store( host.configDir() + "/logcat.ini" );
        Sessions sessions;
        const SidebarWidget sidebar( &store, &sessions );
        REQUIRE( waitFor( [ &sessions ]() { return !sessions.isScanning(); } ) );

        WHEN( "Settings… is clicked" )
        {
            button( &sidebar, "settings" )->click();
            auto* dialog = sidebar.findChild<SettingsDialog*>();

            THEN( "the settings dialog opens" )
            {
                REQUIRE( dialog );
                REQUIRE( dialog->isVisible() );
            }

            AND_WHEN( "a log directory and another adb are entered and OK clicked" )
            {
                REQUIRE( dialog );
                const auto otherAdb = host.configDir() + "/other-adb";
                {
                    QFile file( otherAdb );
                    REQUIRE( file.open( QIODevice::WriteOnly ) );
                    file.write( "#!/bin/sh\n"
                                "if [ \"$1\" = devices ]; then\n"
                                "printf 'List of devices attached\\nemulator-5556\\tdevice\\n\\n'\n"
                                "exit 0\nfi\n"
                                "printf 'line\\n'\nexec sleep 30\n" );
                    file.setPermissions( file.permissions() | QFileDevice::ExeOwner );
                }
                const auto logDir = host.tempRoot() + "/mine";
                dialog->findChild<QLineEdit*>( "logDir" )->setText( logDir );
                dialog->findChild<QLineEdit*>( "adbPath" )->setText( otherAdb );
                dialog->accept();

                THEN( "both are saved, the other adb's devices arrive, and a Start writes to "
                      "the log directory" )
                {
                    REQUIRE( store.logDir() == logDir );
                    REQUIRE( logcat::AdbProcess::adbPathOverride() == otherAdb );
                    DeviceTile* tile = nullptr;
                    REQUIRE( waitFor( [ & ]() {
                        tile = tileOf( sidebar, "emulator-5556" );
                        return tile && tile->state() == TileState::Connected;
                    } ) );
                    button( tile, "startStop" )->click();
                    REQUIRE( host.openedFiles.size() == 1 );
                    REQUIRE( QFileInfo( host.openedFiles[ 0 ] ).absolutePath() == logDir );
                    sessions.stopAll();
                }
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "the sidebar follows the sessions as they start and stop", "[sidebarwidget]" )
{
    const QString serial = "emulator-5554";

    GIVEN( "a sidebar over sessions with a working adb" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        DeviceStore store( host.configDir() + "/logcat.ini" );
        Sessions sessions;
        const SidebarWidget sidebar( &store, &sessions );
        REQUIRE( waitFor( [ &sessions ]() { return !sessions.devices().isEmpty(); } ) );
        auto* tile = tileOf( sidebar, serial );
        REQUIRE( tile );

        WHEN( "a session is started elsewhere, e.g. in the dialog" )
        {
            REQUIRE( sessions.startSession( serial ) );

            THEN( "its tile runs, right away" )
            {
                REQUIRE( tile->state() == TileState::Running );
            }

            AND_WHEN( "it is stopped elsewhere" )
            {
                sessions.stopSession( serial );

                THEN( "its tile is connected again, right away" )
                {
                    REQUIRE( tile->state() == TileState::Connected );
                }
            }

            sessions.stopAll();
        }
    }

    GIVEN( "a sidebar over a session whose adb exits after one line" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "printf 'only\\n'\n" );
        DeviceStore store( host.configDir() + "/logcat.ini" );
        Sessions sessions;
        const SidebarWidget sidebar( &store, &sessions );
        bool runningWhenStopped = true;
        QObject::connect( &sessions, &Sessions::sessionStopped,
                          [ &sidebar, &serial, &runningWhenStopped ]() {
                              auto* tile = tileOf( sidebar, serial );
                              runningWhenStopped = tile && tile->state() == TileState::Running;
                          } );
        int stops = 0;
        QObject::connect( &sessions, &Sessions::sessionStopped, [ &stops ]() { ++stops; } );
        REQUIRE( sessions.startSession( serial ) );
        REQUIRE( tileOf( sidebar, serial )->state() == TileState::Running );

        WHEN( "adb has exited" )
        {
            REQUIRE( waitFor( [ &stops ]() { return stops == 1; } ) );

            THEN( "the tile stopped running when the session stopped" )
            {
                REQUIRE_FALSE( runningWhenStopped );
            }
        }
    }

    GIVEN( "a session for a device whose serial is markup" )
    {
        // Any USB gadget can name itself; a label would render markup in its
        // serial as rich text, and load what an <img> in it points to.
        FakeHost host;
        logcat_test::installFakeAdb( host );
        DeviceStore store( host.configDir() + "/logcat.ini" );
        Sessions sessions;
        const SidebarWidget sidebar( &store, &sessions );
        const QString markup = "<b>emulator-5554</b>";
        REQUIRE( Qt::mightBeRichText( markup ) );
        REQUIRE( sessions.startSession( markup ) );

        THEN( "its tile shows the serial as its literal text" )
        {
            auto* tile = tileOf( sidebar, markup );
            REQUIRE( tile );
            auto* title = tile->findChild<QLabel*>( "title" );
            REQUIRE( title->text() == markup );
            REQUIRE( title->textFormat() == Qt::PlainText );
        }

        sessions.stopAll();
    }
}
#endif
