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
 * @file deviceroster_test.cpp
 * @brief Table tests for the device roster (issue #58): what each tile of
 * the sidebar shows, from the devices seen before, those attached and the
 * running sessions, without a widget, a scan or an adb.
 */

#include <catch2/catch.hpp>

#include "deviceroster.h"

#include <QDate>
#include <QTime>

using logcat::CounterSample;
using logcat::Device;
using logcat::deviceRoster;
using logcat::KnownDevice;
using logcat::Roster;
using logcat::RunningSession;
using logcat::TileRow;
using logcat::TileState;

namespace {

const QDateTime kStart( QDate( 2026, 11, 12 ), QTime( 10, 0, 0 ) );

/// A session for @p serial writing @p file, started at kStart, with @p lines.
RunningSession session( const QString& serial, const QString& file, qint64 lines = 0 )
{
    RunningSession running;
    running.serial = serial;
    running.filePath = file;
    running.counters.lines = lines;
    running.counters.bytes = 1258291;
    running.counters.fileBytes = 524288;
    running.counters.rotations = 2;
    running.counters.startTime = kStart;
    return running;
}

/// The serials of @p roster's rows, in order.
QStringList serialsOf( const Roster& roster )
{
    QStringList serials;
    for ( const auto& row : roster.rows ) {
        serials << row.serial;
    }
    return serials;
}

/// The one row of @p roster.
TileRow onlyRow( const Roster& roster )
{
    REQUIRE( roster.rows.size() == 1 );
    return roster.rows.front();
}

} // namespace

SCENARIO( "a tile is titled by the model and the serial of its device", "[deviceroster]" )
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

SCENARIO( "a running tile's counts, sizes, rates and times are written to be read at a glance",
          "[deviceroster]" )
{
    using logcat::formatCount;
    using logcat::formatDuration;
    using logcat::formatSize;
    using logcat::ratePerSecond;

    THEN( "a count has its thousands separated" )
    {
        REQUIRE( formatCount( 0 ) == "0" );
        REQUIRE( formatCount( 999 ) == "999" );
        REQUIRE( formatCount( 12345 ) == "12,345" );
        REQUIRE( formatCount( 1234567 ) == "1,234,567" );
    }

    THEN( "a size is in bytes below a kilobyte, else in the largest unit, to a tenth" )
    {
        REQUIRE( formatSize( 0 ) == "0 B" );
        REQUIRE( formatSize( 1023 ) == "1023 B" );
        REQUIRE( formatSize( 1024 ) == "1.0 KB" );
        REQUIRE( formatSize( 1536 ) == "1.5 KB" );
        REQUIRE( formatSize( 1258291 ) == "1.2 MB" );
        REQUIRE( formatSize( 5LL * 1024 * 1024 * 1024 ) == "5.0 GB" );
        REQUIRE( formatSize( 3LL * 1024 * 1024 * 1024 * 1024 ) == "3.0 TB" );
    }

    THEN( "a duration is minutes and seconds, with hours once it has any" )
    {
        REQUIRE( formatDuration( 0 ) == "0:00" );
        REQUIRE( formatDuration( 5 ) == "0:05" );
        REQUIRE( formatDuration( 125 ) == "2:05" );
        REQUIRE( formatDuration( 3723 ) == "1:02:03" );
        REQUIRE( formatDuration( 100 * 3600 ) == "100:00:00" );
        REQUIRE( formatDuration( -3 ) == "0:00" );
    }

    THEN( "a rate is how many per second over the time it was counted in" )
    {
        REQUIRE( ratePerSecond( 50, 1000 ) == 50 );
        REQUIRE( ratePerSecond( 50, 500 ) == 100 );
        REQUIRE( ratePerSecond( 3, 2000 ) == 1 );
        REQUIRE( ratePerSecond( 0, 1000 ) == 0 );
        REQUIRE( ratePerSecond( 50, 0 ) == 0 );
        REQUIRE( ratePerSecond( -5, 1000 ) == 0 );
    }
}

SCENARIO( "a running session's details say what it captured, how fast and for how long",
          "[deviceroster]" )
{
    using logcat::sessionDetails;

    GIVEN( "the counters of a session that has run an hour" )
    {
        logcat::SessionCounters counters;
        counters.lines = 12345;
        counters.bytes = 1258291;
        counters.fileBytes = 524288;
        counters.rotations = 2;
        counters.startTime = kStart;
        const auto now = counters.startTime.addSecs( 3723 );

        THEN( "they show its time and rate, lines and bytes, file, rotations and next rotation" )
        {
            REQUIRE( sessionDetails( counters, 120, now )
                     == QStringList{ "running 1:02:03 · 120 lines/s", "12,345 lines · 1.2 MB",
                                     "file 512.0 KB · 2 rotations", "rotates on request" } );
        }

        THEN( "one rotation is one, not ones" )
        {
            counters.rotations = 1;
            REQUIRE( sessionDetails( counters, 0, now )[ 2 ] == "file 512.0 KB · 1 rotation" );
        }

        THEN( "a session that rotated at a size would say at which" )
        {
            counters.nextRotationBytes = 10 * 1024 * 1024;
            REQUIRE( sessionDetails( counters, 0, now )[ 3 ] == "rotates at 10.0 MB" );
        }
    }
}

SCENARIO( "each device has one row, saying what its tile shows", "[deviceroster]" )
{
    const auto now = kStart.addSecs( 65 );

    // One device in each situation, and what its tile shows.
    struct Case {
        const char* what;
        QList<KnownDevice> known;
        QList<Device> attached;
        QList<RunningSession> running;
        QString title;
        TileState state;
        QStringList summary;
        bool hasDetails;
    };
    const QList<Case> cases{
        { "a known device that is unplugged",
          { { "3A1B2C3D4E5F", "Pixel 8", {} } },
          {},
          {},
          "Pixel 8 · 3A1B…",
          TileState::Disconnected,
          { "3A1B2C3D4E5F · disconnected", "Saves to the log directory" },
          false },
        { "a known unplugged device with its own save path",
          { { "3A1B2C3D4E5F", "Pixel 8", "/logs/pixel.log" } },
          {},
          {},
          "Pixel 8 · 3A1B…",
          TileState::Disconnected,
          { "3A1B2C3D4E5F · disconnected", "Saves to /logs/pixel.log" },
          false },
        { "a device that is attached but not known",
          {},
          { { "emulator-5554", "device", "sdk_gphone64" } },
          {},
          "sdk_gphone64 · emul…",
          TileState::Connected,
          { "emulator-5554 · connected", "Saves to the log directory" },
          false },
        { "an attached device without a model",
          {},
          { { "emulator-5554", "device", {} } },
          {},
          "emulator-5554",
          TileState::Connected,
          { "emulator-5554 · connected", "Saves to the log directory" },
          false },
        { "an unauthorized device",
          {},
          { { "NOAUTH456", "unauthorized", {} } },
          {},
          "NOAUTH456",
          TileState::Unauthorized,
          { "NOAUTH456 · unauthorized: allow USB debugging on the device",
            "Saves to the log directory" },
          false },
        { "an offline device",
          {},
          { { "OFFLINE123", "offline", {} } },
          {},
          "OFFLINE123",
          TileState::Offline,
          { "OFFLINE123 · offline", "Saves to the log directory" },
          false },
        { "a device in another state adb names",
          {},
          { { "R5CR", "no permissions", {} } },
          {},
          "R5CR",
          TileState::Unavailable,
          { "R5CR · no permissions", "Saves to the log directory" },
          false },
        { "a known, attached device that runs a session",
          { { "3A1B2C3D4E5F", "Pixel 7", "/logs/pixel.log" } },
          { { "3A1B2C3D4E5F", "device", "Pixel 8" } },
          { session( "3A1B2C3D4E5F", "/logs/pixel.log" ) },
          "Pixel 8 · 3A1B…",
          TileState::Running,
          { "3A1B2C3D4E5F · running", "Writes /logs/pixel.log" },
          true },
        { "a known device whose model adb no longer names keeps the one it named",
          { { "3A1B2C3D4E5F", "Pixel 8", {} } },
          { { "3A1B2C3D4E5F", "unauthorized", {} } },
          {},
          "Pixel 8 · 3A1B…",
          TileState::Unauthorized,
          { "3A1B2C3D4E5F · unauthorized: allow USB debugging on the device",
            "Saves to the log directory" },
          false },
        { "a device that runs a session adb no longer lists",
          {},
          {},
          { session( "emulator-5554", "/logs/a.log" ) },
          "emulator-5554",
          TileState::Running,
          { "emulator-5554 · running", "Writes /logs/a.log" },
          true },
    };

    for ( const auto& c : cases ) {
        WHEN( c.what )
        {
            const auto row = onlyRow( deviceRoster( c.known, c.attached, c.running, {}, now ) );

            THEN( "its row has its title, state, summary and details" )
            {
                REQUIRE( row.title == c.title );
                REQUIRE( row.state == c.state );
                REQUIRE( row.summary == c.summary );
                REQUIRE( row.details.isEmpty() == !c.hasDetails );
                REQUIRE( row.linesPerSecond == 0 );
            }
        }
    }
}

SCENARIO( "the rows come in the order the tiles show", "[deviceroster]" )
{
    GIVEN( "known, attached and running devices, some in more than one" )
    {
        const QList<KnownDevice> known{ { "K2", {}, {} }, { "K1", {}, {} } };
        const QList<Device> attached{ { "A1", "device", {} }, { "K1", "device", {} } };
        const QList<RunningSession> running{ session( "R1", "/r1.log" ), session( "A1", "/a1.log" ),
                                             session( "K2", "/k2.log" ) };

        THEN( "the known devices come first, then those attached, then those that only run, "
              "each once" )
        {
            REQUIRE( serialsOf( deviceRoster( known, attached, running, {}, kStart ) )
                     == QStringList{ "K2", "K1", "A1", "R1" } );
        }
    }

    GIVEN( "no device at all" )
    {
        THEN( "there is no row" )
        {
            REQUIRE( deviceRoster( {}, {}, {}, {}, kStart ).rows.isEmpty() );
        }
    }
}

SCENARIO( "a running session's row shows its details and its rate between two polls",
          "[deviceroster]" )
{
    GIVEN( "a device that runs a session of 2 lines, 1:05 after it started" )
    {
        const QList<Device> attached{ { "emulator-5554", "device", {} } };
        const auto now = kStart.addSecs( 65 );
        auto running = QList<RunningSession>{ session( "emulator-5554", "/logs/a.log", 2 ) };

        WHEN( "it is first polled" )
        {
            const auto first = deviceRoster( {}, attached, running, {}, now, 1000 );

            THEN( "it shows its details, at 0 lines/s, and is sampled" )
            {
                const auto row = onlyRow( first );
                REQUIRE( row.linesPerSecond == 0 );
                REQUIRE( row.details
                         == QStringList{ "running 1:05 · 0 lines/s", "2 lines · 1.2 MB",
                                         "file 512.0 KB · 2 rotations", "rotates on request" } );
                REQUIRE( first.samples.value( "emulator-5554" ).lines == 2 );
                REQUIRE( first.samples.value( "emulator-5554" ).atMs == 1000 );
            }

            AND_WHEN( "a hundred lines more arrive in the half second to the next poll" )
            {
                running[ 0 ].counters.lines = 102;
                const auto second = deviceRoster( {}, attached, running, first.samples, now, 1500 );

                THEN( "it shows them, at 200 lines/s" )
                {
                    const auto row = onlyRow( second );
                    REQUIRE( row.linesPerSecond == 200 );
                    REQUIRE( row.details[ 0 ] == "running 1:05 · 200 lines/s" );
                    REQUIRE( row.details[ 1 ] == "102 lines · 1.2 MB" );
                }

                AND_WHEN( "the tiles are shown again before the next poll" )
                {
                    running[ 0 ].counters.lines = 150;
                    const auto between = deviceRoster( {}, attached, running, second.samples, now );

                    THEN( "the rate stays that of the last poll, the counts are the newest" )
                    {
                        const auto row = onlyRow( between );
                        REQUIRE( row.linesPerSecond == 200 );
                        REQUIRE( row.details[ 1 ] == "150 lines · 1.2 MB" );
                        REQUIRE( between.samples.value( "emulator-5554" ).lines == 102 );
                    }
                }

                AND_WHEN( "nothing more arrives until the next poll" )
                {
                    const auto third
                        = deviceRoster( {}, attached, running, second.samples, now, 2500 );

                    THEN( "the rate is back to 0" )
                    {
                        REQUIRE( onlyRow( third ).linesPerSecond == 0 );
                    }
                }
            }

            AND_WHEN( "the session stops" )
            {
                const auto stopped = deviceRoster( {}, attached, {}, first.samples, now, 1500 );

                THEN( "its tile shows only the device, and its sample is gone" )
                {
                    const auto row = onlyRow( stopped );
                    REQUIRE( row.state == TileState::Connected );
                    REQUIRE( row.details.isEmpty() );
                    REQUIRE( row.linesPerSecond == 0 );
                    REQUIRE( stopped.samples.isEmpty() );
                }
            }
        }

        WHEN( "it is shown, not polled, before its first poll" )
        {
            const auto shown = deviceRoster( {}, attached, running, {}, now );

            THEN( "it shows its details at 0 lines/s, and is not sampled yet" )
            {
                REQUIRE( onlyRow( shown ).details[ 0 ] == "running 1:05 · 0 lines/s" );
                REQUIRE( shown.samples.isEmpty() );
            }
        }
    }
}
