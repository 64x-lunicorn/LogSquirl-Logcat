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
 * @brief BDD tests for the dialog, a view of the device scan and Sessions.
 *
 * The dialog is driven through its widgets and through the scan and the
 * Sessions it shows, with a FakeHost, a scan that the test completes
 * (fakeadbdevices.h) and a stand-in for adb (see fakeadb.h).
 */

#include <catch2/catch.hpp>

#include "devicescan.h"
#include "devicewidget.h"
#include "fakeadb.h"
#include "fakeadbdevices.h"
#include "sessions.h"

#include <QFile>
#include <QLabel>
#include <QPushButton>

#include <array>

using logcat::DeviceScan;
using logcat::DeviceWidget;
using logcat::Sessions;
using logcat_test::FakeAdbDevices;
using logcat_test::FakeHost;

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
        logcat_test::installBrokenAdb( host ); // only its path is used
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( &host.settings(), runner );
        Sessions sessions( &host.settings() );
        const DeviceWidget widget( &host.settings(), &scan, &sessions );
        auto* refresh = widget.findChild<QPushButton*>( "refresh" );
        REQUIRE( refresh != nullptr );

        THEN( "the Refresh button shows that it is scanning until the scan ends" )
        {
            REQUIRE_FALSE( refresh->isEnabled() );
            runner->listOne( "emulator-5554" );
            REQUIRE( refresh->isEnabled() );
        }
    }

    GIVEN( "a dialog whose device scan hangs" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( &host.settings(), runner );
        Sessions sessions( &host.settings() );
        const DeviceWidget widget( &host.settings(), &scan, &sessions );

        WHEN( "the ADB path changes to one where no adb can be found, which restarts the scan" )
        {
            const NoAdbAnywhere noAdb;
            auto* refresh = widget.findChild<QPushButton*>( "refresh" );
            REQUIRE( refresh != nullptr );
            REQUIRE_FALSE( refresh->isEnabled() );
            logcat_test::useAdb( host, host.configDir() + "/missing-adb" );
            if ( !host.settings().findAdb().isEmpty() ) {
                WARN( "An adb is installed in a well-known location; skipped." );
                return;
            }

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
SCENARIO( "an adb path is shown as plain text", "[devicewidget]" )
{
    // The sidebar's tiles show a serial as plain text (sidebarwidget_test.cpp).
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
            DeviceScan scan( &host.settings(), new FakeAdbDevices );
            Sessions sessions( &host.settings() );
            const DeviceWidget widget( &host.settings(), &scan, &sessions );

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
