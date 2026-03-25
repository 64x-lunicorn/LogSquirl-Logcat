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
 * @file parsedevicelist_test.cpp
 * @brief BDD tests for AdbProcess::parseDeviceList().
 *
 * Validates that the `adb devices` output parser correctly extracts
 * device serials for all known output formats, including edge cases
 * like empty output, header-only, and mixed device states.
 */

#include <catch2/catch.hpp>

#include "adbprocess.h"

using logcat::AdbProcess;

SCENARIO( "parseDeviceList extracts serials from adb output", "[parsedevicelist]" )
{
    GIVEN( "typical adb devices output with one physical device" )
    {
        const QByteArray output = "List of devices attached\n"
                                  "R5CR10XXXXX\tdevice\n"
                                  "\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDeviceList( output );

            THEN( "exactly one serial is returned" )
            {
                REQUIRE( devices.size() == 1 );
                REQUIRE( devices[ 0 ] == "R5CR10XXXXX" );
            }
        }
    }

    GIVEN( "output with one emulator" )
    {
        const QByteArray output = "List of devices attached\n"
                                  "emulator-5554\tdevice\n"
                                  "\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDeviceList( output );

            THEN( "the emulator serial is returned" )
            {
                REQUIRE( devices.size() == 1 );
                REQUIRE( devices[ 0 ] == "emulator-5554" );
            }
        }
    }

    GIVEN( "output with multiple devices in different states" )
    {
        const QByteArray output = "List of devices attached\n"
                                  "emulator-5554\tdevice\n"
                                  "R5CR10XXXXX\tdevice\n"
                                  "OFFLINE123\toffline\n"
                                  "NOAUTH456\tunauthorized\n"
                                  "\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDeviceList( output );

            THEN( "only 'device' entries are included" )
            {
                REQUIRE( devices.size() == 2 );
                REQUIRE( devices.contains( "emulator-5554" ) );
                REQUIRE( devices.contains( "R5CR10XXXXX" ) );
            }

            THEN( "offline and unauthorized are excluded" )
            {
                REQUIRE_FALSE( devices.contains( "OFFLINE123" ) );
                REQUIRE_FALSE( devices.contains( "NOAUTH456" ) );
            }
        }
    }

    GIVEN( "completely empty output" )
    {
        const QByteArray output;

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDeviceList( output );

            THEN( "the result is empty" )
            {
                REQUIRE( devices.isEmpty() );
            }
        }
    }

    GIVEN( "header-only output with no devices attached" )
    {
        const QByteArray output = "List of devices attached\n"
                                  "\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDeviceList( output );

            THEN( "the result is empty" )
            {
                REQUIRE( devices.isEmpty() );
            }
        }
    }

    GIVEN( "output with \\r\\n line endings (Windows)" )
    {
        const QByteArray output = "List of devices attached\r\n"
                                  "emulator-5554\tdevice\r\n"
                                  "\r\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDeviceList( output );

            THEN( "the serial is returned without trailing \\r" )
            {
                REQUIRE( devices.size() == 1 );
                REQUIRE( devices[ 0 ] == "emulator-5554" );
            }
        }
    }

    GIVEN( "output with extra whitespace around serial and status" )
    {
        const QByteArray output = "List of devices attached\n"
                                  "  R5CR10XXXXX  \t  device  \n"
                                  "\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDeviceList( output );

            THEN( "the serial is trimmed correctly" )
            {
                REQUIRE( devices.size() == 1 );
                REQUIRE( devices[ 0 ] == "R5CR10XXXXX" );
            }
        }
    }
}
