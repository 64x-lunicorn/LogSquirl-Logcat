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
 * @file hostapi_test.cpp
 * @brief BDD tests for the hostLog() / hostNotify() convenience wrappers.
 *
 * The host decodes every string it receives with QString::fromUtf8(), so
 * the wrappers must hand it UTF-8 regardless of the local 8-bit codec.
 */

#include <catch2/catch.hpp>

#include "fakehost.h"
#include "plugin.h"

using logcat_test::FakeHost;

SCENARIO( "messages reach the host as UTF-8", "[hostapi]" )
{
    GIVEN( "a host that decodes every message as UTF-8" )
    {
        FakeHost host;
        const auto text = QString::fromUtf8( "Gerät \xE2\x86\x92 192.168.1.5:5555 \xE2\x9C\x93" );

        WHEN( "the plugin logs a message with non-ASCII characters" )
        {
            logcat::hostLog( LOGSQUIRL_LOG_INFO, text );

            THEN( "the host receives it unchanged" )
            {
                REQUIRE( host.logs == QStringList{ text } );
            }
        }

        WHEN( "the plugin shows a notification with non-ASCII characters" )
        {
            logcat::hostNotify( text );

            THEN( "the host receives it unchanged" )
            {
                REQUIRE( host.notifications == QStringList{ text } );
            }
        }
    }

    GIVEN( "no host (the plugin is not initialised)" )
    {
        WHEN( "the plugin logs and notifies" )
        {
            logcat::hostLog( LOGSQUIRL_LOG_INFO, "ignored" );
            logcat::hostNotify( "ignored" );

            THEN( "nothing happens" )
            {
                SUCCEED();
            }
        }
    }
}
