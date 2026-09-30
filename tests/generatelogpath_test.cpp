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
 * @file generatelogpath_test.cpp
 * @brief BDD tests for AdbProcess::generateLogPath().
 *
 * Log file names only have one-second resolution, so two captures in the
 * same second (a quick rotation, or Stop and Start) must still get
 * distinct files; and the device serial must be usable in a file name
 * on every platform.
 */

#include <catch2/catch.hpp>

#include "adbprocess.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using logcat::AdbProcess;

namespace {

void touch( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
}

} // namespace

SCENARIO( "generateLogPath names a new log file", "[generatelogpath]" )
{
    const QDateTime timestamp( QDate( 2026, 9, 30 ), QTime( 14, 5, 9 ) );
    QTemporaryDir dir;

    GIVEN( "an empty log directory" )
    {
        WHEN( "generating a path for a device" )
        {
            const auto path = AdbProcess::generateLogPath( dir.path(), "emulator-5554", timestamp );

            THEN( "it is <date>_<time>_<serial>.log inside the directory" )
            {
                REQUIRE( path == dir.filePath( "2026-09-30_140509_emulator-5554.log" ) );
            }
        }

        WHEN( "generating a path for a wireless device" )
        {
            const auto path
                = AdbProcess::generateLogPath( dir.path(), "192.168.1.5:5555", timestamp );

            THEN( "characters that are invalid in file names are replaced" )
            {
                REQUIRE( path == dir.filePath( "2026-09-30_140509_192.168.1.5_5555.log" ) );
            }
        }
    }

    GIVEN( "a log file with that name already exists" )
    {
        const auto taken = dir.filePath( "2026-09-30_140509_emulator-5554.log" );
        touch( taken );

        WHEN( "generating a path for the same device and second" )
        {
            const auto path = AdbProcess::generateLogPath( dir.path(), "emulator-5554", timestamp );

            THEN( "a numbered name that does not exist yet is returned" )
            {
                REQUIRE( path == dir.filePath( "2026-09-30_140509_emulator-5554_2.log" ) );
                REQUIRE_FALSE( QFileInfo::exists( path ) );
            }
        }

        AND_GIVEN( "the first numbered name is taken as well" )
        {
            touch( dir.filePath( "2026-09-30_140509_emulator-5554_2.log" ) );

            THEN( "the next number is used" )
            {
                REQUIRE( AdbProcess::generateLogPath( dir.path(), "emulator-5554", timestamp )
                         == dir.filePath( "2026-09-30_140509_emulator-5554_3.log" ) );
            }
        }
    }
}
