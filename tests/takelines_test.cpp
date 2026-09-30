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
 * @file takelines_test.cpp
 * @brief BDD tests for AdbProcess::takeLines().
 *
 * takeLines() turns the bytes read from adb's stdout into complete lines.
 * adb on Windows (and some devices) terminates lines with "\r\n"; the
 * "\r" must not end up in the log file.
 */

#include <catch2/catch.hpp>

#include "adbprocess.h"

using logcat::AdbProcess;

using Lines = QList<QByteArray>;

SCENARIO( "takeLines splits complete lines off the read buffer", "[takelines]" )
{
    GIVEN( "a buffer with LF-terminated lines and a partial last line" )
    {
        QByteArray buffer = "first\nsecond\nthird";

        WHEN( "taking lines" )
        {
            const auto lines = AdbProcess::takeLines( buffer );

            THEN( "the complete lines are returned without their terminator" )
            {
                REQUIRE( lines == Lines{ "first", "second" } );
            }

            THEN( "the partial line stays in the buffer" )
            {
                REQUIRE( buffer == "third" );
            }
        }
    }

    GIVEN( "a buffer with CRLF-terminated lines" )
    {
        QByteArray buffer = "first\r\nsecond\r\n";

        WHEN( "taking lines" )
        {
            const auto lines = AdbProcess::takeLines( buffer );

            THEN( "the trailing carriage returns are stripped" )
            {
                REQUIRE( lines == Lines{ "first", "second" } );
                REQUIRE( buffer.isEmpty() );
            }
        }
    }

    GIVEN( "a CRLF terminator split across two reads" )
    {
        QByteArray buffer = "first\r";

        WHEN( "taking lines before and after the LF arrives" )
        {
            const auto before = AdbProcess::takeLines( buffer );
            buffer.append( "\nsecond" );
            const auto after = AdbProcess::takeLines( buffer );

            THEN( "the line is only taken once it is complete, without the CR" )
            {
                REQUIRE( before.isEmpty() );
                REQUIRE( after == Lines{ "first" } );
                REQUIRE( buffer == "second" );
            }
        }
    }

    GIVEN( "a buffer with empty lines" )
    {
        QByteArray buffer = "\n\r\n";

        WHEN( "taking lines" )
        {
            const auto lines = AdbProcess::takeLines( buffer );

            THEN( "each empty line is kept" )
            {
                REQUIRE( lines == Lines{ "", "" } );
            }
        }
    }

    GIVEN( "a carriage return in the middle of a line" )
    {
        QByteArray buffer = "progress\rdone\n";

        WHEN( "taking lines" )
        {
            const auto lines = AdbProcess::takeLines( buffer );

            THEN( "only a CR right before the LF is stripped" )
            {
                REQUIRE( lines == Lines{ "progress\rdone" } );
            }
        }
    }

    GIVEN( "a buffer without any line terminator" )
    {
        QByteArray buffer = "partial";

        WHEN( "taking lines" )
        {
            const auto lines = AdbProcess::takeLines( buffer );

            THEN( "nothing is taken" )
            {
                REQUIRE( lines.isEmpty() );
                REQUIRE( buffer == "partial" );
            }
        }
    }
}
