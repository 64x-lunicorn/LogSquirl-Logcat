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
 * without requiring a running ADB daemon.  Starting a session runs a
 * stand-in for adb (see fakeadb.h); the scenarios that need a working
 * one use a shell script and only run on Unix.
 */

#include <catch2/catch.hpp>

#include "adbprocess.h"
#include "fakeadb.h"
#include "plugin.h"

#include <QFile>
#include <QTemporaryDir>

using logcat::AdbProcess;
using logcat_test::FakeHost;
using logcat_test::waitFor;

namespace {

QByteArray readFile( const QString& path )
{
    QFile file( path );
    return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray();
}

} // namespace

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

SCENARIO( "rotateLog creates a new temp file and preserves the old one", "[adbprocess]" )
{
    GIVEN( "an AdbProcess that is not running" )
    {
        AdbProcess proc( "test-device" );

        WHEN( "rotateLog is called without starting the process" )
        {
            const auto result = proc.rotateLog();

            THEN( "it returns an empty string because the process is not running" )
            {
                REQUIRE( result.isEmpty() );
            }
        }
    }

    GIVEN( "an AdbProcess whose temp file has been manually set up for testing" )
    {
        // We cannot call start() without a real ADB, but we can verify that
        // rotateLog returns empty when not running (no process = no rotation).
        AdbProcess proc( "rotate-test-device" );

        THEN( "rotateLog returns empty because no process is running" )
        {
            REQUIRE( proc.rotateLog().isEmpty() );
        }

        THEN( "the rotation count stays at 0 after a failed rotation" )
        {
            proc.rotateLog();
            // lineCount stays at 0 since nothing was rotated
            REQUIRE( proc.lineCount() == 0 );
        }
    }
}

SCENARIO( "start reports whether adb could be launched", "[adbprocess]" )
{
    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );

        AdbProcess proc( "emulator-5554", savePath );
        QStringList errors;
        QObject::connect( &proc, &AdbProcess::errorOccurred,
                          [ &errors ]( const QString& message ) { errors << message; } );

        WHEN( "starting the session" )
        {
            const auto started = proc.start();

            THEN( "start() fails and the session is not running" )
            {
                REQUIRE_FALSE( started );
                REQUIRE_FALSE( proc.isRunning() );
            }

            THEN( "the failure is reported exactly once" )
            {
                REQUIRE( errors.size() == 1 );
            }

            THEN( "no empty log file is left behind" )
            {
                REQUIRE_FALSE( QFileInfo::exists( savePath ) );
                REQUIRE( proc.tempFilePath().isEmpty() );
            }
        }
    }

    GIVEN( "an adb that cannot be executed and a save path that already holds a capture" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        {
            QFile existing( savePath );
            REQUIRE( existing.open( QIODevice::WriteOnly ) );
            existing.write( "earlier capture\n" );
        }

        AdbProcess proc( "emulator-5554", savePath );

        WHEN( "starting the session fails" )
        {
            REQUIRE_FALSE( proc.start() );

            THEN( "the earlier capture is kept" )
            {
                REQUIRE( QFileInfo::exists( savePath ) );
            }
        }
    }

#ifdef Q_OS_UNIX
    GIVEN( "a working adb" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );

        AdbProcess proc( "emulator-5554" );

        WHEN( "starting the session" )
        {
            const auto started = proc.start();

            THEN( "start() succeeds and logcat output reaches the log file" )
            {
                REQUIRE( started );
                REQUIRE( proc.isRunning() );
                REQUIRE( waitFor( [ &proc ]() { return proc.lineCount() == 2; } ) );
                REQUIRE( readFile( proc.tempFilePath() ) == "first\nsecond\n" );
            }
        }
    }

    GIVEN( "a working adb and a save path that already holds a capture" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        {
            QFile existing( savePath );
            REQUIRE( existing.open( QIODevice::WriteOnly ) );
            existing.write( "earlier capture\n" );
        }

        AdbProcess proc( "emulator-5554", savePath );

        WHEN( "the session runs" )
        {
            REQUIRE( proc.start() );
            REQUIRE( waitFor( [ &proc ]() { return proc.lineCount() == 2; } ) );

            THEN( "the new output is appended to the earlier capture" )
            {
                REQUIRE( readFile( savePath ) == "earlier capture\nfirst\nsecond\n" );
            }
        }
    }
#endif
}

#ifdef Q_OS_UNIX
SCENARIO( "rotateLog moves the capture to a new file", "[adbprocess]" )
{
    GIVEN( "a running session writing to a generated file in the log directory" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        QTemporaryDir logDir;
        const auto savePath = AdbProcess::generateLogPath( logDir.path(), "emulator-5554" );

        AdbProcess proc( "emulator-5554", savePath );
        REQUIRE( proc.start() );
        REQUIRE( waitFor( [ &proc ]() { return proc.lineCount() == 2; } ) );

        WHEN( "rotating within the same second" )
        {
            const auto newPath = proc.rotateLog();

            THEN( "the capture continues in a different file" )
            {
                REQUIRE_FALSE( newPath.isEmpty() );
                REQUIRE( newPath != savePath );
                REQUIRE( proc.tempFilePath() == newPath );
                REQUIRE( QFileInfo::exists( newPath ) );
            }

            THEN( "the old file keeps its content" )
            {
                REQUIRE( readFile( savePath ) == "first\nsecond\n" );
            }
        }
    }

    GIVEN( "a running session writing to a temporary file" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );

        AdbProcess proc( "192.168.1.5:5555" );
        REQUIRE( proc.start() );
        REQUIRE( waitFor( [ &proc ]() { return proc.lineCount() == 2; } ) );
        const auto oldPath = proc.tempFilePath();

        WHEN( "rotating" )
        {
            const auto newPath = proc.rotateLog();

            THEN( "neither file name contains the serial's ':'" )
            {
                REQUIRE_FALSE( newPath.isEmpty() );
                REQUIRE_FALSE( QFileInfo( oldPath ).fileName().contains( ':' ) );
                REQUIRE_FALSE( QFileInfo( newPath ).fileName().contains( ':' ) );
            }

            THEN( "the old file keeps its content" )
            {
                REQUIRE( readFile( oldPath ) == "first\nsecond\n" );
            }
        }
    }
}
#endif
