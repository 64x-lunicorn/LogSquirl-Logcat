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
#include "scopedenv.h"
#include "tempdirs.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using logcat::AdbProcess;
using logcat_test::FakeHost;
using logcat_test::ReadOnlyDir;
using logcat_test::ScopedEnv;
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

/**
 * Create an Android SDK in @p sdk with an adb in platform-tools, named as
 * the SDK names it on this platform, and return its path.
 */
QString installSdkAdb( const QString& sdk )
{
#ifdef Q_OS_WIN
    const auto path = sdk + "/platform-tools/adb.exe";
#else
    const auto path = sdk + "/platform-tools/adb";
#endif
    REQUIRE( QDir().mkpath( sdk + "/platform-tools" ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "#!/bin/sh\n" );
    file.setPermissions( file.permissions() | QFileDevice::ExeOwner );
    return path;
}

bool isSameFile( const QString& a, const QString& b )
{
    return !a.isEmpty() && QFileInfo( a ).canonicalFilePath() == QFileInfo( b ).canonicalFilePath();
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

        THEN( "no line has been counted" )
        {
            REQUIRE( proc.counters().lines == 0 );
        }
    }
}

SCENARIO( "no adb override is read while the plugin is not initialised", "[adbprocess]" )
{
    GIVEN( "the plugin is not initialised, and a logcat.ini in the temporary directory" )
    {
        // Ensure clean state — no host API available.
        logcat::g_state.api = nullptr;
        logcat::g_state.handle = nullptr;
        logcat::g_state.initialised = false;

        // A temporary directory that every local user can write to, such
        // as /tmp, with an override another user planted.
        QTemporaryDir sharedTemp;
        const ScopedEnv tmpdir( "TMPDIR", QFile::encodeName( sharedTemp.path() ) );
        const ScopedEnv tmp( "TMP", QFile::encodeName( sharedTemp.path() ) );
        const ScopedEnv temp( "TEMP", QFile::encodeName( sharedTemp.path() ) );
        const auto planted = sharedTemp.filePath( "planted-adb" );
        {
            QFile file( planted );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
        }
        {
            QSettings settings( sharedTemp.filePath( "logcat.ini" ), QSettings::IniFormat );
            settings.setValue( "adb/path", planted );
        }

        THEN( "configDir() is empty" )
        {
            REQUIRE( AdbProcess::configDir().isEmpty() );
        }

        THEN( "findAdb() does not use the planted override" )
        {
            REQUIRE_FALSE( isSameFile( AdbProcess::findAdb(), planted ) );
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

SCENARIO( "findAdb finds the adb of the Android SDK", "[adbprocess]" )
{
    GIVEN( "an SDK with adb in platform-tools, and no override" )
    {
        FakeHost host;
        QTemporaryDir sdk;
        QTemporaryDir emptySdk;
        const auto adb = installSdkAdb( sdk.path() );

        WHEN( "ANDROID_HOME names the SDK" )
        {
            const ScopedEnv home( "ANDROID_HOME", QFile::encodeName( sdk.path() ) );
            const ScopedEnv root( "ANDROID_SDK_ROOT", QFile::encodeName( emptySdk.path() ) );

            THEN( "its adb is found" )
            {
                REQUIRE( isSameFile( AdbProcess::findAdb(), adb ) );
            }
        }

        WHEN( "ANDROID_SDK_ROOT names the SDK, and ANDROID_HOME one without adb" )
        {
            const ScopedEnv home( "ANDROID_HOME", QFile::encodeName( emptySdk.path() ) );
            const ScopedEnv root( "ANDROID_SDK_ROOT", QFile::encodeName( sdk.path() ) );

            THEN( "its adb is found" )
            {
                REQUIRE( isSameFile( AdbProcess::findAdb(), adb ) );
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
                REQUIRE( proc.counters().lines == 0 );
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
                REQUIRE( proc.logFilePath().isEmpty() );
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
                REQUIRE( waitFor( [ &proc ]() { return proc.counters().lines == 2; } ) );
                REQUIRE( readFile( proc.logFilePath() ) == "first\nsecond\n" );
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
        const auto savePath = logcat::LogFile::generateLogPath( logDir.path(), "emulator-5554" );

        AdbProcess proc( "emulator-5554", savePath );
        REQUIRE( proc.start() );
        REQUIRE( waitFor( [ &proc ]() { return proc.counters().lines == 2; } ) );

        WHEN( "rotating within the same second" )
        {
            const auto newPath = proc.rotateLog();

            THEN( "the capture continues in a different file" )
            {
                REQUIRE_FALSE( newPath.isEmpty() );
                REQUIRE( newPath != savePath );
                REQUIRE( proc.logFilePath() == newPath );
                REQUIRE( QFileInfo::exists( newPath ) );
            }

            THEN( "the old file keeps its content" )
            {
                REQUIRE( readFile( savePath ) == "first\nsecond\n" );
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
        REQUIRE( waitFor( [ &proc ]() { return proc.counters().lines == 1; } ) );

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
                REQUIRE( proc.logFilePath() == savePath );
                REQUIRE( waitFor( [ &proc ]() { return proc.counters().lines == 2; } ) );
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
SCENARIO( "the stop log line counts the lines of every file after a Rotate", "[adbprocess]" )
{
    GIVEN( "a session that wrote a line, rotated, then wrote another" )
    {
        FakeHost host;
        const auto trigger = host.configDir() + "/go";
        logcat_test::installFakeAdb( host, logcat_test::scriptWaitingFor( trigger ) );

        AdbProcess proc( "emulator-5554" );
        REQUIRE( proc.start() );
        REQUIRE( waitFor( [ &proc ]() { return proc.counters().lines == 1; } ) );
        REQUIRE_FALSE( proc.rotateLog().isEmpty() );
        touch( trigger );
        REQUIRE( waitFor( [ &proc ]() { return proc.counters().lines == 2; } ) );

        WHEN( "it is stopped" )
        {
            proc.stop();

            THEN( "the host log counts both lines, not only the last file's" )
            {
                REQUIRE( host.logs
                             .filter( "Stopped logcat for device emulator-5554 (2 lines "
                                      "captured)" )
                             .size()
                         == 1 );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "end() ends a session and keeps its file for its tab", "[adbprocess]" )
{
    GIVEN( "a running session writing to a temporary file, with listeners" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        int signalCount = 0;
        QPointer<AdbProcess> proc = new AdbProcess( "emulator-5554" );
        QObject::connect( proc, &AdbProcess::finished, [ &signalCount ]( int ) { ++signalCount; } );
        QObject::connect( proc, &AdbProcess::errorOccurred,
                          [ &signalCount ]( const QString& ) { ++signalCount; } );
        REQUIRE( proc->start() );
        const auto file = proc->logFilePath();

        WHEN( "it is ended, and the event loop has run" )
        {
            proc->end();
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

            THEN( "adb is stopped, without a signal to the listeners" )
            {
                REQUIRE( host.logs.filter( "Stopped logcat for device emulator-5554" ).size()
                         == 1 );
                REQUIRE( signalCount == 0 );
            }

            THEN( "the session is deleted, and its file kept" )
            {
                REQUIRE( proc.isNull() );
                REQUIRE( QFileInfo::exists( file ) );
            }

            AND_WHEN( "LogSquirl quits" )
            {
                logcat::removeOwnTempDirs( host.tempRoot() );

                THEN( "the file is removed" )
                {
                    REQUIRE_FALSE( QFileInfo::exists( file ) );
                }
            }
        }
    }

    GIVEN( "a session whose adb has exited on its own" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "printf 'only\\n'\n" );
        QPointer<AdbProcess> proc = new AdbProcess( "emulator-5554" );
        bool finished = false;
        QObject::connect( proc, &AdbProcess::finished, [ &finished ]( int ) { finished = true; } );
        REQUIRE( proc->start() );
        const auto file = proc->logFilePath();
        REQUIRE( waitFor( [ &finished ]() { return finished; } ) );

        WHEN( "it is ended, and the event loop has run" )
        {
            proc->end();
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

            THEN( "the session is deleted, and its file kept with what adb wrote" )
            {
                REQUIRE( proc.isNull() );
                REQUIRE( readFile( file ) == "only\n" );
            }
        }
    }

    GIVEN( "a running session that has been rotated" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        QPointer<AdbProcess> proc = new AdbProcess( "emulator-5554" );
        REQUIRE( proc->start() );
        const auto first = proc->logFilePath();
        const auto second = proc->rotateLog();
        REQUIRE_FALSE( second.isEmpty() );

        WHEN( "it is ended, and the event loop has run" )
        {
            proc->end();
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

            THEN( "the files of both tabs are kept" )
            {
                REQUIRE( proc.isNull() );
                REQUIRE( QFileInfo::exists( first ) );
                REQUIRE( QFileInfo::exists( second ) );
            }
        }
    }

    GIVEN( "several sessions whose adb ignores SIGTERM" )
    {
        FakeHost host;
        // sleep inherits the ignored SIGTERM, so only kill ends it.
        logcat_test::installFakeAdb( host, "trap '' TERM\nprintf 'first\\n'\nexec sleep 30\n" );
        constexpr int kSessions = 4;
        QList<AdbProcess*> sessions;
        for ( int i = 0; i < kSessions; ++i ) {
            sessions << new AdbProcess( QString( "emulator-%1" ).arg( 5554 + 2 * i ) );
            REQUIRE( sessions.last()->start() );
        }
        // Each adb has set its trap once it has written its first line.
        REQUIRE( waitFor( [ &sessions ]() {
            return std::all_of( sessions.cbegin(), sessions.cend(),
                                []( const auto* proc ) { return proc->counters().lines > 0; } );
        } ) );
        QStringList files;
        for ( const auto* proc : sessions ) {
            files << proc->logFilePath();
        }

        WHEN( "they are ended together" )
        {
            QElapsedTimer timer;
            timer.start();
            AdbProcess::endAll( sessions );
            const auto elapsedMs = timer.elapsed();
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

            THEN( "they are waited for once, not one after the other" )
            {
                // One after the other, each would take a second before it
                // is killed: four seconds in all.
                REQUIRE( elapsedMs < 3000 );
                REQUIRE( host.logs.filter( "did not exit" ).isEmpty() );
                REQUIRE( host.logs.filter( "Stopped logcat" ).size() == kSessions );
            }

            THEN( "every file is kept" )
            {
                for ( const auto& file : files ) {
                    REQUIRE( QFileInfo::exists( file ) );
                }
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

#ifdef Q_OS_UNIX
SCENARIO( "adb output without a newline is capped in memory", "[adbprocess]" )
{
    // A script that writes @p bytes x's without a newline to @p stream.
    const auto xs = []( qsizetype bytes, const char* stream ) {
        return "head -c " + QByteArray::number( bytes ) + " /dev/zero | tr '\\0' x" + stream + "\n";
    };

    GIVEN( "an adb that writes one byte more than the cap without a newline, then waits" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host,
                                     xs( AdbProcess::kMaxLineBytes + 1, "" ) + "exec sleep 30\n" );
        AdbProcess proc( "emulator-5554" );

        WHEN( "the session runs" )
        {
            REQUIRE( proc.start() );

            THEN( "the cap's worth is written out as a line while adb still runs" )
            {
                const auto path = proc.logFilePath();
                REQUIRE( waitFor( [ &path ]() {
                    return readFile( path ).size() == AdbProcess::kMaxLineBytes + 1;
                } ) );
                REQUIRE( readFile( path ) == QByteArray( AdbProcess::kMaxLineBytes, 'x' ) + '\n' );
                REQUIRE( proc.isRunning() );
                REQUIRE( proc.counters().lines == 1 );
            }
        }
    }

    GIVEN( "an adb that writes twice the cap to stderr without a newline, then fails" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host,
                                     xs( 2 * AdbProcess::kMaxLineBytes, " >&2" ) + "exit 1\n" );
        QStringList errors;
        int finishedCount = 0;
        AdbProcess proc( "emulator-5554" );
        QObject::connect( &proc, &AdbProcess::errorOccurred,
                          [ &errors ]( const QString& message ) { errors << message; } );
        QObject::connect( &proc, &AdbProcess::finished,
                          [ &finishedCount ]( int ) { ++finishedCount; } );

        WHEN( "the session ends" )
        {
            REQUIRE( proc.start() );
            REQUIRE( waitFor( [ &finishedCount ]() { return finishedCount == 1; } ) );

            THEN( "the error shows the start of what adb said, cut short" )
            {
                REQUIRE( errors.size() == 1 );
                REQUIRE( errors.first().contains( "xxxx…" ) );
                REQUIRE( errors.first().size() < AdbProcess::kMaxMessageChars + 100 );
            }

            THEN( "the host log gets it cut short as well" )
            {
                const auto lines = host.logs.filter( "xxxx" );
                REQUIRE_FALSE( lines.isEmpty() );
                for ( const auto& line : lines ) {
                    REQUIRE( line.size() < AdbProcess::kMaxMessageChars + 100 );
                }
            }
        }
    }
}
#endif

SCENARIO( "a session with a relative save path does not start", "[adbprocess]" )
{
    GIVEN( "a session with a relative save path" )
    {
        FakeHost host;
        AdbProcess proc( "emulator-5554", "relative-capture.log" );
        QStringList errors;
        QObject::connect( &proc, &AdbProcess::errorOccurred,
                          [ &errors ]( const QString& message ) { errors << message; } );

        WHEN( "it is started" )
        {
            const auto started = proc.start();

            THEN( "it does not start, says why, and creates nothing in the working directory" )
            {
                REQUIRE_FALSE( started );
                REQUIRE( errors.size() == 1 );
                REQUIRE( errors.first().contains( "not a full path" ) );
                REQUIRE_FALSE( QFileInfo::exists( "relative-capture.log" ) );
                REQUIRE( proc.logFilePath().isEmpty() );
            }
        }
    }
}

#ifdef Q_OS_UNIX
SCENARIO( "a log file that cannot be written stops the session and says why", "[adbprocess]" )
{
    GIVEN( "a session whose log file may grow by a few bytes only" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        AdbProcess proc( "emulator-5554", savePath );
        QStringList errors;
        QObject::connect( &proc, &AdbProcess::errorOccurred,
                          [ &errors ]( const QString& message ) { errors << message; } );

        // A file size limit makes every write past it fail with EFBIG, as a
        // full disk fails it; the signal it would send is ignored.
        struct rlimit before{};
        REQUIRE( ::getrlimit( RLIMIT_FSIZE, &before ) == 0 );
        const auto oldHandler = std::signal( SIGXFSZ, SIG_IGN );
        struct rlimit tiny = before;
        tiny.rlim_cur = 8;
        REQUIRE( ::setrlimit( RLIMIT_FSIZE, &tiny ) == 0 );

        WHEN( "adb writes more than that" )
        {
            REQUIRE( proc.start() );
            const bool stopped = waitFor( [ &proc ]() { return !proc.isRunning(); } );
            ::setrlimit( RLIMIT_FSIZE, &before );

            THEN( "the session stops, and the failure is reported once, naming the file" )
            {
                REQUIRE( stopped );
                REQUIRE( errors.size() == 1 );
                REQUIRE( errors.first().contains( "Could not write the log file" ) );
                REQUIRE( errors.first().contains( savePath ) );
            }
        }

        ::setrlimit( RLIMIT_FSIZE, &before );
        std::signal( SIGXFSZ, oldHandler );
    }
}
#endif
