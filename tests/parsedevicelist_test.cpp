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

    GIVEN( "the long format of `adb devices -l`, columns separated by spaces" )
    {
        const QByteArray output
            = "List of devices attached\n"
              "3A1B2C3D4E5F           device usb:1-1 product:shiba model:Pixel_8 device:shiba "
              "transport_id:1\n"
              "NOAUTH456              unauthorized usb:1-2 transport_id:3\n"
              "\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDeviceList( output );

            THEN( "only the device in state 'device' is listed" )
            {
                REQUIRE( devices == QStringList{ "3A1B2C3D4E5F" } );
            }
        }
    }
}

SCENARIO( "parseDevices reads every device's serial, state and model", "[parsedevicelist]" )
{
    GIVEN( "the output of `adb devices -l` with devices in every state" )
    {
        const QByteArray output
            = "List of devices attached\n"
              "3A1B2C3D4E5F           device usb:1-1 product:shiba model:Pixel_8 device:shiba "
              "transport_id:1\n"
              "emulator-5554          device product:sdk_gphone64 model:sdk_gphone64_arm64 "
              "device:emu64a transport_id:2\n"
              "NOAUTH456              unauthorized usb:1-2 transport_id:3\n"
              "OFFLINE123             offline transport_id:4\n"
              "0123456789ABCDEF       no permissions (missing udev rules? user is in the plugdev "
              "group); see [http://developer.android.com/tools/device.html] usb:3-1\n"
              "\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDevices( output );

            THEN( "every device is listed, in order, with its state" )
            {
                REQUIRE( devices.size() == 5 );
                REQUIRE( devices[ 0 ].serial == "3A1B2C3D4E5F" );
                REQUIRE( devices[ 0 ].state == "device" );
                REQUIRE( devices[ 1 ].serial == "emulator-5554" );
                REQUIRE( devices[ 2 ].serial == "NOAUTH456" );
                REQUIRE( devices[ 2 ].state == "unauthorized" );
                REQUIRE( devices[ 3 ].state == "offline" );
                REQUIRE( devices[ 4 ].serial == "0123456789ABCDEF" );
                REQUIRE( devices[ 4 ].state == "no permissions" );
            }

            THEN( "a model is read with spaces for adb's underscores" )
            {
                REQUIRE( devices[ 0 ].model == "Pixel 8" );
                REQUIRE( devices[ 1 ].model == "sdk gphone64 arm64" );
            }

            THEN( "a device adb names no model has none" )
            {
                REQUIRE( devices[ 2 ].model.isEmpty() );
                REQUIRE( devices[ 3 ].model.isEmpty() );
            }

            THEN( "only a device in state 'device' is ready for logcat" )
            {
                REQUIRE( devices[ 0 ].isReady() );
                REQUIRE( devices[ 1 ].isReady() );
                REQUIRE_FALSE( devices[ 2 ].isReady() );
                REQUIRE_FALSE( devices[ 3 ].isReady() );
                REQUIRE_FALSE( devices[ 4 ].isReady() );
            }
        }
    }

    GIVEN( "the short format of `adb devices`, columns separated by a tab" )
    {
        const QByteArray output = "List of devices attached\r\n"
                                  "emulator-5554\tdevice\r\n"
                                  "\r\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDevices( output );

            THEN( "the device is listed without a model" )
            {
                REQUIRE( devices.size() == 1 );
                REQUIRE( devices[ 0 ].serial == "emulator-5554" );
                REQUIRE( devices[ 0 ].state == "device" );
                REQUIRE( devices[ 0 ].model.isEmpty() );
            }
        }
    }

    GIVEN( "lines adb writes besides the list, e.g. when it starts its server" )
    {
        const QByteArray output = "* daemon not running; starting now at tcp:5037\n"
                                  "* daemon started successfully\n"
                                  "List of devices attached\n"
                                  "emulator-5554\tdevice\n";

        WHEN( "parsing the output" )
        {
            const auto devices = AdbProcess::parseDevices( output );

            THEN( "only the device is listed" )
            {
                REQUIRE( devices.size() == 1 );
                REQUIRE( devices[ 0 ].serial == "emulator-5554" );
            }
        }
    }
}
