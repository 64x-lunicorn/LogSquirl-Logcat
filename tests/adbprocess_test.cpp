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
 * @file adbprocess_test.cpp
 * @brief BDD tests for AdbProcess instance behaviour.
 *
 * Tests basic construction, property accessors, and configDir fallback
 * without requiring a running ADB daemon.
 */

#include <catch2/catch.hpp>

#include "adbprocess.h"
#include "plugin.h"

#include <QTemporaryDir>

using logcat::AdbProcess;

SCENARIO( "AdbProcess construction and properties", "[adbprocess]" )
{
    GIVEN( "a freshly constructed AdbProcess" )
    {
        AdbProcess proc( "emulator-5554" );

        THEN( "the serial matches the constructor argument" )
        {
            REQUIRE( proc.serial() == "emulator-5554" );
        }

        THEN( "it is not running initially" )
        {
            REQUIRE_FALSE( proc.isRunning() );
        }

        THEN( "line count starts at zero" )
        {
            REQUIRE( proc.lineCount() == 0 );
        }
    }
}

SCENARIO( "configDir falls back to temp when plugin is not initialised", "[adbprocess]" )
{
    GIVEN( "the plugin is not initialised (g_state.api is null)" )
    {
        // Ensure clean state — no host API available.
        logcat::g_state.api = nullptr;
        logcat::g_state.handle = nullptr;
        logcat::g_state.initialised = false;

        WHEN( "calling configDir()" )
        {
            const auto dir = AdbProcess::configDir();

            THEN( "a non-empty temporary path is returned" )
            {
                REQUIRE_FALSE( dir.isEmpty() );
            }
        }
    }
}

SCENARIO( "findAdb returns a path or empty string", "[adbprocess]" )
{
    GIVEN( "no special environment setup" )
    {
        WHEN( "calling findAdb()" )
        {
            const auto adbPath = AdbProcess::findAdb();

            THEN( "the result is either empty or points to an existing file" )
            {
                if ( !adbPath.isEmpty() ) {
                    REQUIRE( QFileInfo::exists( adbPath ) );
                }
                // If ADB is not installed, empty is the correct result — no assertion needed.
                SUCCEED();
            }
        }
    }
}
