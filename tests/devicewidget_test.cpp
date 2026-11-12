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
 * @file devicewidget_test.cpp
 * @brief BDD tests for the dialog and the sidebar, the views of Sessions.
 *
 * The views are driven through their widgets and through the Sessions
 * they show, with a FakeHost and a stand-in for adb (see fakeadb.h).
 */

#include <catch2/catch.hpp>

#include "devicewidget.h"
#include "fakeadb.h"
#include "sessions.h"
#include "sidebarwidget.h"

#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>

#include <array>

using logcat::DeviceWidget;
using logcat::Sessions;
using logcat::SidebarWidget;
using logcat_test::FakeHost;
using logcat_test::waitFor;

namespace {

/**
 * Hides adb from the PATH and the SDK environment variables while it
 * exists, so that findAdb() only finds one in a well-known location.
 */
class NoAdbAnywhere {
public:
    NoAdbAnywhere()
    {
        for ( const auto* name : kNames ) {
            saved_.append( qgetenv( name ) );
            qunsetenv( name );
        }
        qputenv( "PATH", "/nonexistent" );
    }

    ~NoAdbAnywhere()
    {
        for ( size_t i = 0; i < kNames.size(); ++i ) {
            if ( saved_[ static_cast<qsizetype>( i ) ].isNull() ) {
                qunsetenv( kNames[ i ] );
            }
            else {
                qputenv( kNames[ i ], saved_[ static_cast<qsizetype>( i ) ] );
            }
        }
    }

    NoAdbAnywhere( const NoAdbAnywhere& ) = delete;
    NoAdbAnywhere& operator=( const NoAdbAnywhere& ) = delete;

private:
    static constexpr std::array<const char*, 3> kNames
        = { "PATH", "ANDROID_HOME", "ANDROID_SDK_ROOT" };
    QList<QByteArray> saved_;
};

/** The label among @p widget's children that shows @p text, or nullptr. */
QLabel* labelShowing( const QWidget& widget, const QString& text )
{
    const auto labels = widget.findChildren<QLabel*>();
    for ( auto* label : labels ) {
        if ( label->text() == text ) {
            return label;
        }
    }
    return nullptr;
}

} // namespace

#ifdef Q_OS_UNIX
SCENARIO( "the dialog's Refresh button shows a running device scan", "[devicewidget]" )
{
    GIVEN( "a dialog whose device scan is running" )
    {
        FakeHost host;
        logcat_test::installFakeAdb(
            host, "exec sleep 30\n",
            "sleep 1\nprintf 'List of devices attached\\nemulator-5554\\tdevice\\n\\n'\n" );
        Sessions sessions;
        const DeviceWidget widget( &sessions );
        auto* refresh = widget.findChild<QPushButton*>( "refresh" );
        REQUIRE( refresh != nullptr );

        THEN( "the Refresh button shows that it is scanning until the scan ends" )
        {
            REQUIRE_FALSE( refresh->isEnabled() );
            REQUIRE( waitFor( [ &sessions ]() { return !sessions.devices().isEmpty(); } ) );
            REQUIRE( refresh->isEnabled() );
        }
    }

    GIVEN( "a dialog whose device scan hangs" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "exec sleep 30\n", "exec sleep 30\n" );
        Sessions sessions;
        const DeviceWidget widget( &sessions );

        WHEN( "the ADB path changes to one where no adb can be found" )
        {
            const NoAdbAnywhere noAdb;
            logcat_test::useAdb( host, host.configDir() + "/missing-adb" );
            if ( !logcat::AdbProcess::findAdb().isEmpty() ) {
                WARN( "An adb is installed in a well-known location; skipped." );
                return;
            }
            auto* refresh = widget.findChild<QPushButton*>( "refresh" );
            REQUIRE( refresh != nullptr );
            REQUIRE_FALSE( refresh->isEnabled() );
            sessions.restartDeviceScan();

            THEN( "the Refresh button is usable again" )
            {
                REQUIRE( refresh->isEnabled() );
                REQUIRE_FALSE( refresh->text().contains( "Scanning" ) );
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
        Sessions sessions;
        const SidebarWidget sidebar( &sessions );
        REQUIRE( waitFor( [ &sessions ]() { return !sessions.devices().isEmpty(); } ) );
        auto* list = sidebar.findChild<QListWidget*>();
        auto* combo = sidebar.findChild<QComboBox*>();
        REQUIRE( list != nullptr );
        REQUIRE( combo != nullptr );
        REQUIRE( list->count() == 0 );

        WHEN( "a session is started elsewhere, e.g. in the dialog" )
        {
            REQUIRE( sessions.startSession( serial ) );

            THEN( "the sidebar lists it and marks its device right away" )
            {
                REQUIRE( list->count() == 1 );
                REQUIRE( labelShowing( sidebar, serial ) != nullptr );
                REQUIRE( combo->itemText( combo->findData( serial ) ) == serial + " ●" );
            }

            AND_WHEN( "it is stopped elsewhere" )
            {
                sessions.stopSession( serial );

                THEN( "the sidebar no longer lists it, right away" )
                {
                    REQUIRE( list->count() == 0 );
                    REQUIRE( combo->itemText( combo->findData( serial ) ) == serial );
                }
            }

            sessions.stopAll();
        }
    }

    GIVEN( "a sidebar over a session whose adb exits after one line" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "printf 'only\\n'\n" );
        Sessions sessions;
        const SidebarWidget sidebar( &sessions );
        auto* list = sidebar.findChild<QListWidget*>();
        REQUIRE( list != nullptr );
        bool listedWhenStopped = true;
        QObject::connect( &sessions, &Sessions::sessionStopped, [ list, &listedWhenStopped ]() {
            listedWhenStopped = list->count() > 0;
        } );
        int stops = 0;
        QObject::connect( &sessions, &Sessions::sessionStopped, [ &stops ]() { ++stops; } );
        REQUIRE( sessions.startSession( serial ) );
        REQUIRE( list->count() == 1 );

        WHEN( "adb has exited" )
        {
            REQUIRE( waitFor( [ &stops ]() { return stops == 1; } ) );

            THEN( "the sidebar dropped the session when it stopped" )
            {
                REQUIRE_FALSE( listedWhenStopped );
                REQUIRE( list->count() == 0 );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "a serial and an adb path are shown as plain text", "[devicewidget]" )
{
    // Any USB gadget can name itself; a label would render markup in its
    // serial as rich text, and load what an <img> in it points to.
    GIVEN( "a session for a device whose serial is markup" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions;
        const SidebarWidget sidebar( &sessions );
        const QString serial = "<b>emulator-5554</b>";
        REQUIRE( Qt::mightBeRichText( serial ) );
        REQUIRE( sessions.startSession( serial ) );

        THEN( "the sidebar's session list shows the serial as its literal text" )
        {
            QLabel* label = nullptr;
            REQUIRE( waitFor( [ & ]() {
                label = labelShowing( sidebar, serial );
                return label != nullptr;
            } ) );
            REQUIRE( label->textFormat() == Qt::PlainText );
        }

        sessions.stopAll();
    }

    GIVEN( "an adb path that is markup" )
    {
        FakeHost host;
        const auto adb = host.configDir() + "/<b>adb";
        {
            QFile file( adb );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
        }
        logcat_test::useAdb( host, adb );
        REQUIRE( Qt::mightBeRichText( adb.mid( adb.lastIndexOf( '/' ) + 1 ) ) );

        WHEN( "the dialog shows it" )
        {
            Sessions sessions;
            const DeviceWidget widget( &sessions );

            THEN( "it is shown as its literal text" )
            {
                auto* label = labelShowing( widget, adb );
                REQUIRE( label != nullptr );
                REQUIRE( label->textFormat() == Qt::PlainText );
            }
        }
    }
}
#endif
