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
#include "readonlydir.h"

#include <QFile>
#include <QTemporaryDir>

using logcat::AdbProcess;
using logcat_test::FakeHost;
using logcat_test::ReadOnlyDir;
using logcat_test::waitFor;

namespace {

#ifdef Q_OS_UNIX
QByteArray readFile( const QString& path )
{
    QFile file( path );
    return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray();
}

void touch( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
}
#endif
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

SCENARIO( "rotateLog does nothing while no session runs", "[adbprocess]" )
{
    GIVEN( "an AdbProcess that has not been started" )
    {
        AdbProcess proc( "test-device" );

        WHEN( "rotateLog is called" )
        {
            const auto result = proc.rotateLog();

            THEN( "it returns an empty path and the line count stays at 0" )
            {
                REQUIRE( result.isEmpty() );
                REQUIRE( proc.lineCount() == 0 );
            }
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

        QStringList errors;
        AdbProcess proc( "emulator-5554", savePath );
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

    GIVEN( "a running session whose log directory no longer accepts new files" )
    {
        if ( !ReadOnlyDir::isEnforced() ) {
            WARN( "File permissions are not enforced (running as root?); skipped." );
            return;
        }

        FakeHost host;
        const auto trigger = host.configDir() + "/go";
        logcat_test::installFakeAdb( host, logcat_test::scriptWaitingFor( trigger ) );
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );

        QStringList errors;
        AdbProcess proc( "emulator-5554", savePath );
        QObject::connect( &proc, &AdbProcess::errorOccurred,
                          [ &errors ]( const QString& message ) { errors << message; } );
        REQUIRE( proc.start() );
        REQUIRE( waitFor( [ &proc ]() { return proc.lineCount() == 1; } ) );

        WHEN( "rotating fails" )
        {
            QString newPath;
            {
                ReadOnlyDir readOnly( logDir.path() );
                newPath = proc.rotateLog();
            }
            touch( trigger );

            THEN( "the failure is reported once and the session keeps running" )
            {
                REQUIRE( newPath.isEmpty() );
                REQUIRE( errors.size() == 1 );
                REQUIRE( proc.isRunning() );
            }

            THEN( "later output still reaches the old file" )
            {
                REQUIRE( proc.tempFilePath() == savePath );
                REQUIRE( waitFor( [ &proc ]() { return proc.lineCount() == 2; } ) );
                REQUIRE( readFile( savePath ) == "first\nafter\n" );
            }
        }

        AND_WHEN( "rotating fails and the old file cannot be reopened either" )
        {
            int finishedCount = 0;
            QObject::connect( &proc, &AdbProcess::finished,
                              [ &finishedCount ]( int ) { ++finishedCount; } );
            QString newPath;
            {
                QFile::setPermissions( savePath, QFileDevice::ReadOwner );
                ReadOnlyDir readOnly( logDir.path() );
                newPath = proc.rotateLog();
            }

            THEN( "the session stops instead of running without a file" )
            {
                REQUIRE( newPath.isEmpty() );
                REQUIRE( errors.size() == 1 );
                REQUIRE_FALSE( proc.isRunning() );
                REQUIRE( finishedCount == 1 );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "destroying a running session does not call back its owner", "[adbprocess]" )
{
    GIVEN( "a running session with listeners" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        int signalCount = 0;
        auto* proc = new AdbProcess( "emulator-5554" );
        QObject::connect( proc, &AdbProcess::finished, [ &signalCount ]( int ) { ++signalCount; } );
        QObject::connect( proc, &AdbProcess::errorOccurred,
                          [ &signalCount ]( const QString& ) { ++signalCount; } );
        REQUIRE( proc->start() );

        WHEN( "it is destroyed, as when its owner's children are deleted" )
        {
            delete proc;

            THEN( "no signal is emitted into the (possibly half-destroyed) owner" )
            {
                REQUIRE( signalCount == 0 );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "adb's stderr reaches the user", "[adbprocess]" )
{
    GIVEN( "an adb that warns on stderr while it runs" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "echo 'adb: warning: slow device' >&2\n"
                                           "printf 'first\\n'\n"
                                           "exec sleep 30\n" );
        QStringList errors;
        AdbProcess proc( "emulator-5554" );
        QObject::connect( &proc, &AdbProcess::errorOccurred,
                          [ &errors ]( const QString& message ) { errors << message; } );

        WHEN( "the session runs" )
        {
            REQUIRE( proc.start() );

            THEN( "the warning is forwarded to the host log, not shown as an error" )
            {
                REQUIRE( waitFor( [ &host ]() {
                    return !host.logs.filter( "adb: warning: slow device" ).isEmpty();
                } ) );
                REQUIRE( errors.isEmpty() );
            }
        }
    }

    GIVEN( "an adb that fails with a message on stderr" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "echo \"error: device 'emulator-5554' not found\" >&2\n"
                                           "exit 1\n" );
        QStringList errors;
        int finishedCount = 0;
        AdbProcess proc( "emulator-5554" );
        QObject::connect( &proc, &AdbProcess::errorOccurred,
                          [ &errors ]( const QString& message ) { errors << message; } );
        QObject::connect( &proc, &AdbProcess::finished,
                          [ &finishedCount ]( int ) { ++finishedCount; } );

        WHEN( "the session runs" )
        {
            REQUIRE( proc.start() );
            REQUIRE( waitFor( [ &finishedCount ]() { return finishedCount == 1; } ) );

            THEN( "one error tells the user what adb said" )
            {
                REQUIRE( errors.size() == 1 );
                REQUIRE( errors.first().contains( "device 'emulator-5554' not found" ) );
            }
        }
    }
}
#endif
