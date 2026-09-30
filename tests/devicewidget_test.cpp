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
 * @brief BDD tests for the session bookkeeping in DeviceWidget.
 *
 * DeviceWidget owns the running AdbProcess sessions for both the dialog
 * and the sidebar.  These tests drive it through its public API with a
 * FakeHost and a stand-in for adb (see fakeadb.h).
 */

#include <catch2/catch.hpp>

#include "devicewidget.h"
#include "fakeadb.h"

#include <QDir>
#include <QTemporaryDir>

using logcat::DeviceWidget;
using logcat_test::FakeHost;
using logcat_test::waitFor;

SCENARIO( "a session is only accepted when adb starts", "[devicewidget]" )
{
    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        DeviceWidget widget;
        QTemporaryDir logDir;

        WHEN( "starting a session with a save path" )
        {
            const auto savePath = logDir.filePath( "capture.log" );
            const auto started = widget.startSession( "emulator-5554", savePath );

            THEN( "the session is rejected" )
            {
                REQUIRE_FALSE( started );
                REQUIRE( widget.activeSessionCount() == 0 );
                REQUIRE_FALSE( widget.isSessionActive( "emulator-5554" ) );
            }

            THEN( "no tab is opened and no empty file is left behind" )
            {
                REQUIRE( host.openedFiles.isEmpty() );
                REQUIRE( QDir( logDir.path() ).isEmpty() );
            }

            THEN( "the user is told once why" )
            {
                REQUIRE( host.notifications.size() == 1 );
            }
        }
    }

#ifdef Q_OS_UNIX
    GIVEN( "a working adb" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        DeviceWidget widget;

        WHEN( "starting a session" )
        {
            const auto started = widget.startSession( "emulator-5554" );

            THEN( "the session is accepted and its file opened in a tab" )
            {
                REQUIRE( started );
                REQUIRE( widget.isSessionActive( "emulator-5554" ) );
                REQUIRE( host.openedFiles.size() == 1 );
            }

            widget.stopAll();
        }
    }
#endif
}

#ifdef Q_OS_UNIX
SCENARIO( "two sessions never write to the same file", "[devicewidget]" )
{
    GIVEN( "a session writing to a save path" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        DeviceWidget widget;
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        REQUIRE( widget.startSession( "emulator-5554", savePath ) );
        host.notifications.clear();

        WHEN( "starting a second device with the same save path" )
        {
            const auto started = widget.startSession( "emulator-5556", savePath );

            THEN( "the second session is refused, and the user told why" )
            {
                REQUIRE_FALSE( started );
                REQUIRE_FALSE( widget.isSessionActive( "emulator-5556" ) );
                REQUIRE( host.notifications.size() == 1 );
            }
        }

        widget.stopAll();
    }
}
#endif
