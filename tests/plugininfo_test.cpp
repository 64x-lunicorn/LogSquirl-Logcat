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
 * @file plugininfo_test.cpp
 * @brief BDD tests verifying the plugin's C ABI metadata.
 *
 * Ensures logsquirl_plugin_get_info() returns a well-formed
 * LogSquirlPluginInfo struct with the expected field values.
 */

#include <catch2/catch.hpp>

#include "logsquirl_plugin_api.h"

#include <cstring>

// The symbol is exported from plugin.cpp (linked into the test binary).
extern "C" const LogSquirlPluginInfo* logsquirl_plugin_get_info( void );

SCENARIO( "logsquirl_plugin_get_info returns valid metadata", "[plugininfo]" )
{
    GIVEN( "the exported get_info function" )
    {
        const auto* info = logsquirl_plugin_get_info();

        THEN( "the returned pointer is not null" )
        {
            REQUIRE( info != nullptr );
        }

        WHEN( "checking the plugin id" )
        {
            THEN( "it matches the expected reverse-domain identifier" )
            {
                REQUIRE( std::strcmp( info->id, "io.github.logsquirl.logcat" ) == 0 );
            }
        }

        WHEN( "checking the display name" )
        {
            THEN( "it is 'Android Logcat'" )
            {
                REQUIRE( std::strcmp( info->name, "Android Logcat" ) == 0 );
            }
        }

        WHEN( "checking the version string" )
        {
            THEN( "it is a non-empty SemVer string" )
            {
                REQUIRE( info->version != nullptr );
                REQUIRE( std::strlen( info->version ) > 0 );
            }
        }

        WHEN( "checking the plugin type" )
        {
            THEN( "it is LOGSQUIRL_PLUGIN_UI" )
            {
                REQUIRE( info->type == LOGSQUIRL_PLUGIN_UI );
            }
        }

        WHEN( "checking the API version" )
        {
            THEN( "it matches the current LOGSQUIRL_PLUGIN_API_VERSION" )
            {
                REQUIRE( info->api_version == LOGSQUIRL_PLUGIN_API_VERSION );
            }
        }

        WHEN( "checking the license field" )
        {
            THEN( "it is GPL-3.0-or-later" )
            {
                REQUIRE( std::strcmp( info->license, "GPL-3.0-or-later" ) == 0 );
            }
        }

        WHEN( "checking optional descriptive fields" )
        {
            THEN( "description is non-null and non-empty" )
            {
                REQUIRE( info->description != nullptr );
                REQUIRE( std::strlen( info->description ) > 0 );
            }

            THEN( "author is non-null and non-empty" )
            {
                REQUIRE( info->author != nullptr );
                REQUIRE( std::strlen( info->author ) > 0 );
            }
        }
    }
}
