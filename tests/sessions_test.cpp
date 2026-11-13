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
 * @file sessions_test.cpp
 * @brief BDD tests for Sessions: the logcat session of each device.
 *
 * Sessions owns the running AdbProcess sessions for both the dialog and
 * the sidebar.  These tests drive it through its public API with a
 * FakeHost and a stand-in for adb (see fakeadb.h).  The device scan has
 * its own tests (devicescan_test.cpp).
 */

#include <catch2/catch.hpp>

#include "fakeadb.h"
#include "readonlydir.h"
#include "scopedenv.h"
#include "sessions.h"
#include "tempdirs.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>

using logcat::Sessions;
using logcat_test::FakeHost;
using logcat_test::waitFor;

namespace {

/** Counts the signals of a Sessions, per serial. */
struct SignalLog {
    explicit SignalLog( const Sessions& sessions )
    {
        QObject::connect( &sessions, &Sessions::sessionStarted,
                          [ this ]( const QString& serial ) { started << serial; } );
        QObject::connect( &sessions, &Sessions::sessionStopped,
                          [ this ]( const QString& serial ) { stopped << serial; } );
        QObject::connect( &sessions, &Sessions::sessionChanged,
                          [ this ]( const QString& serial ) { changed << serial; } );
        QObject::connect( &sessions, &Sessions::sessionEnded,
                          [ this ]( const QString& serial ) { ended << serial; } );
    }

    QStringList started;
    QStringList stopped;
    QStringList changed;
    QStringList ended; ///< sessionEnded(): ended on their own
};

} // namespace

SCENARIO( "a session is only accepted when adb starts", "[sessions]" )
{
    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        Sessions sessions( &host.settings() );
        QTemporaryDir logDir;

        WHEN( "starting a session with a save path" )
        {
            const auto savePath = logDir.filePath( "capture.log" );
            const auto started = sessions.startSession( "emulator-5554", savePath );

            THEN( "the session is rejected" )
            {
                REQUIRE_FALSE( started );
                REQUIRE( sessions.activeSessionCount() == 0 );
                REQUIRE_FALSE( sessions.isSessionActive( "emulator-5554" ) );
            }

            THEN( "no tab is opened and no empty file is left behind" )
            {
                REQUIRE( host.openedFiles.isEmpty() );
                REQUIRE( QDir( logDir.path() ).isEmpty() );
            }

            THEN( "the user is told once why" )
            {
                REQUIRE( host.notifications.size() == 1 );
            }
        }
    }

#ifdef Q_OS_UNIX
    GIVEN( "a working adb" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions( &host.settings() );

        WHEN( "starting a session" )
        {
            const auto started = sessions.startSession( "emulator-5554" );

            THEN( "the session is accepted and its file opened in a tab" )
            {
                REQUIRE( started );
                REQUIRE( sessions.isSessionActive( "emulator-5554" ) );
                REQUIRE( host.openedFiles.size() == 1 );
            }

            sessions.stopAll();
        }
    }
#endif
}

#ifdef Q_OS_UNIX
SCENARIO( "two sessions never write to the same file", "[sessions]" )
{
    GIVEN( "a session writing to a save path" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions( &host.settings() );
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        REQUIRE( sessions.startSession( "emulator-5554", savePath ) );
        host.notifications.clear();

        WHEN( "starting a second device with the same save path" )
        {
            const auto started = sessions.startSession( "emulator-5556", savePath );

            THEN( "the second session is refused, and the user told why" )
            {
                REQUIRE_FALSE( started );
                REQUIRE_FALSE( sessions.isSessionActive( "emulator-5556" ) );
                REQUIRE( host.notifications.size() == 1 );
            }
        }

        sessions.stopAll();
    }

    GIVEN( "a session writing to a save path in the home directory, given with ~" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        QTemporaryDir home;
        const logcat_test::ScopedEnv homeEnv( "HOME", QFile::encodeName( home.path() ) );
        Sessions sessions( &host.settings() );
        REQUIRE( sessions.startSession( "emulator-5554", "~/capture.log" ) );

        WHEN( "starting a second device with the file's full path" )
        {
            const auto started
                = sessions.startSession( "emulator-5556", home.filePath( "capture.log" ) );

            THEN( "it is refused" )
            {
                REQUIRE_FALSE( started );
            }
        }

        WHEN( "starting a second device with ~ again" )
        {
            const auto started = sessions.startSession( "emulator-5556", "~/capture.log" );

            THEN( "it is refused" )
            {
                REQUIRE_FALSE( started );
            }
        }

        sessions.stopAll();
    }

    GIVEN( "a session whose save file has been deleted while it runs" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions( &host.settings() );
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        REQUIRE( sessions.startSession( "emulator-5554", savePath ) );
        REQUIRE( QFile::remove( savePath ) );

        WHEN( "starting a second device with a different, new save path" )
        {
            const auto started
                = sessions.startSession( "emulator-5556", logDir.filePath( "other.log" ) );

            THEN( "it is accepted: two missing files are not the same file" )
            {
                REQUIRE( started );
                REQUIRE( sessions.isSessionActive( "emulator-5556" ) );
            }
        }

        WHEN( "starting a second device with the same save path, spelled differently" )
        {
            const auto started
                = sessions.startSession( "emulator-5556", logDir.path() + "/sub/../capture.log" );

            THEN( "it is still refused" )
            {
                REQUIRE_FALSE( started );
            }
        }

        sessions.stopAll();
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "stopping a session is not reported as an error", "[sessions]" )
{
    GIVEN( "a running session" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions( &host.settings() );
        REQUIRE( sessions.startSession( "emulator-5554" ) );
        host.notifications.clear();

        WHEN( "the user stops it" )
        {
            sessions.stopSession( "emulator-5554" );

            THEN( "the only notification says it stopped" )
            {
                REQUIRE( host.notifications.size() == 1 );
                REQUIRE( host.notifications.first().startsWith( "Logcat stopped" ) );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "a failed rotation is reported once", "[sessions]" )
{
    GIVEN( "a session whose log directory no longer accepts new files" )
    {
        if ( !logcat_test::ReadOnlyDir::isEnforced() ) {
            WARN( "File permissions are not enforced (running as root?); skipped." );
            return;
        }

        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions( &host.settings() );
        QTemporaryDir logDir;
        REQUIRE( sessions.startSession( "emulator-5554", logDir.filePath( "capture.log" ) ) );
        host.notifications.clear();
        host.openedFiles.clear();

        WHEN( "rotating the session" )
        {
            {
                const logcat_test::ReadOnlyDir readOnly( logDir.path() );
                sessions.rotateSession( "emulator-5554" );
            }

            THEN( "the user is told once, no tab is opened, and the session goes on" )
            {
                REQUIRE( host.notifications.size() == 1 );
                REQUIRE( host.openedFiles.isEmpty() );
                REQUIRE( sessions.isSessionActive( "emulator-5554" ) );
            }
        }

        sessions.stopAll();
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "stopAll keeps temporary log files for their tabs", "[sessions]" )
{
    GIVEN( "a session writing to a temporary file" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        auto* sessions = new Sessions( &host.settings() );
        REQUIRE( sessions->startSession( "emulator-5554" ) );
        REQUIRE( host.openedFiles.size() == 1 );
        const auto tempFile = host.openedFiles.first();
        // A rescan for the stopped session would be asked for (sessionEnded()).
        int rescansAsked = 0;
        QObject::connect( sessions, &Sessions::sessionEnded,
                          [ &rescansAsked ]() { ++rescansAsked; } );

        WHEN( "the user stops all sessions: stopAll(), then Sessions is deleted" )
        {
            sessions->stopAll();
            QObject::disconnect( sessions, &Sessions::sessionEnded, nullptr, nullptr );
            delete sessions;

            THEN( "the temporary file is kept for its tab" )
            {
                REQUIRE( QFileInfo::exists( tempFile ) );
            }

            THEN( "the devices are not scanned again for the stopped session" )
            {
                REQUIRE( rescansAsked == 0 );
            }
        }
    }

    GIVEN( "a session writing to a temporary file that has been rotated" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        auto* sessions = new Sessions( &host.settings() );
        REQUIRE( sessions->startSession( "emulator-5554" ) );
        sessions->rotateSession( "emulator-5554" );
        REQUIRE( host.openedFiles.size() == 2 );
        REQUIRE( QFileInfo( host.openedFiles.last() ).absolutePath()
                 == QFileInfo( host.openedFiles.first() ).absolutePath() );

        WHEN( "the user stops all sessions: stopAll(), then Sessions is deleted" )
        {
            sessions->stopAll();
            delete sessions;

            THEN( "the files of both tabs are kept" )
            {
                REQUIRE( QFileInfo::exists( host.openedFiles.first() ) );
                REQUIRE( QFileInfo::exists( host.openedFiles.last() ) );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "a session's files are kept for their tabs until LogSquirl quits", "[sessions]" )
{
    const QString serial = "emulator-5554";

    // The session has ended, and the event loop has deleted it: what it
    // left on disk is what its tabs show.
    const auto deleteEnded
        = []() { QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete ); };

    GIVEN( "a session writing to a temporary file" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions( &host.settings() );
        REQUIRE( sessions.startSession( serial ) );
        const auto file = sessions.filePath( serial );

        WHEN( "the user stops it" )
        {
            sessions.stopSession( serial );
            deleteEnded();

            THEN( "its file is kept" )
            {
                REQUIRE( QFileInfo::exists( file ) );
            }

            AND_WHEN( "LogSquirl quits" )
            {
                logcat::removeOwnTempDirs( host.tempRoot() );

                THEN( "its file is removed" )
                {
                    REQUIRE_FALSE( QFileInfo::exists( file ) );
                }
            }
        }

        WHEN( "the user stops all sessions" )
        {
            sessions.stopAll();
            deleteEnded();

            THEN( "its file is kept" )
            {
                REQUIRE( QFileInfo::exists( file ) );
            }
        }

        WHEN( "it is rotated, then stopped" )
        {
            sessions.rotateSession( serial );
            const auto rotated = sessions.filePath( serial );
            REQUIRE( rotated != file );
            sessions.stopSession( serial );
            deleteEnded();

            THEN( "the files of both tabs are kept" )
            {
                REQUIRE( QFileInfo::exists( file ) );
                REQUIRE( QFileInfo::exists( rotated ) );
            }

            AND_WHEN( "LogSquirl quits" )
            {
                logcat::removeOwnTempDirs( host.tempRoot() );

                THEN( "both files are removed" )
                {
                    REQUIRE_FALSE( QFileInfo::exists( file ) );
                    REQUIRE_FALSE( QFileInfo::exists( rotated ) );
                }
            }
        }

        sessions.stopAll();
    }

    GIVEN( "a session whose adb exits after one line" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "printf 'only\\n'\n" );
        Sessions sessions( &host.settings() );
        const SignalLog log( sessions );
        REQUIRE( sessions.startSession( serial ) );
        const auto file = sessions.filePath( serial );

        WHEN( "adb has exited" )
        {
            REQUIRE( waitFor( [ &log ]() { return !log.ended.isEmpty(); } ) );
            deleteEnded();

            THEN( "its file is kept, with what adb wrote" )
            {
                QFile kept( file );
                REQUIRE( kept.open( QIODevice::ReadOnly ) );
                REQUIRE( kept.readAll() == "only\n" );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "stopping all sessions waits for them together", "[sessions]" )
{
    GIVEN( "several sessions whose adb ignores SIGTERM" )
    {
        FakeHost host;
        // sleep inherits the ignored SIGTERM, so only kill ends it.
        logcat_test::installFakeAdb( host, "trap '' TERM\nprintf 'first\\n'\nexec sleep 30\n" );
        auto* sessions = new Sessions( &host.settings() );
        constexpr int kSessions = 6;
        QStringList serials;
        for ( int i = 0; i < kSessions; ++i ) {
            serials << QString( "emulator-%1" ).arg( 5554 + 2 * i );
            REQUIRE( sessions->startSession( serials.last() ) );
        }
        // Each adb has set its trap once it has written its first line.
        REQUIRE( waitFor( [ sessions, &serials ]() {
            return std::all_of( serials.cbegin(), serials.cend(),
                                [ sessions ]( const auto& serial ) {
                                    return sessions->counters( serial ).lines > 0;
                                } );
        } ) );

        WHEN( "all of them are stopped and Sessions deleted, as at shutdown" )
        {
            QElapsedTimer timer;
            timer.start();
            sessions->stopAll();
            delete sessions;
            const auto elapsedMs = timer.elapsed();

            THEN( "they are waited for together, not one after the other" )
            {
                // One after the other, each would take a second before it
                // is killed: six seconds in all.
                REQUIRE( elapsedMs < 4000 );
            }

            THEN( "each of them ends when it is killed" )
            {
                REQUIRE( host.logs.filter( "did not exit" ).isEmpty() );
                REQUIRE( host.logs.filter( "Stopped logcat" ).size() == kSessions );
            }
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "a session's counters can be read while it runs", "[sessions]" )
{
    const QString serial = "emulator-5554";

    GIVEN( "a session writing to a temporary file, whose adb prints two lines" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host ); // "first\r\nsecond\n"
        Sessions sessions( &host.settings() );

        THEN( "before it starts, its counters are all zero" )
        {
            const auto counters = sessions.counters( serial );
            REQUIRE( counters.lines == 0 );
            REQUIRE( counters.bytes == 0 );
            REQUIRE( counters.fileBytes == 0 );
            REQUIRE( counters.rotations == 0 );
            REQUIRE( counters.nextRotationBytes == 0 );
            REQUIRE( counters.startTime.isNull() );
            REQUIRE( sessions.filePath( serial ).isEmpty() );
        }

        WHEN( "it has captured the lines" )
        {
            const auto before = QDateTime::currentDateTime();
            REQUIRE( sessions.startSession( serial ) );
            const auto after = QDateTime::currentDateTime();
            REQUIRE( waitFor( [ & ]() { return sessions.counters( serial ).lines == 2; } ) );
            const auto counters = sessions.counters( serial );

            THEN( "its counters say how much it wrote, to which file, since when" )
            {
                // "first\n" and "second\n": adb's "\r\n" is written as "\n"
                REQUIRE( counters.bytes == 13 );
                REQUIRE( counters.fileBytes == 13 );
                REQUIRE( counters.rotations == 0 );
                REQUIRE( counters.nextRotationBytes == 0 );
                REQUIRE( counters.startTime >= before );
                REQUIRE( counters.startTime <= after );
                REQUIRE( sessions.filePath( serial ) == host.openedFiles.first() );
                REQUIRE( QFileInfo( sessions.filePath( serial ) ).size() == counters.fileBytes );
            }

            AND_WHEN( "it is stopped" )
            {
                sessions.stopSession( serial );

                THEN( "it has no counters any more" )
                {
                    REQUIRE( sessions.counters( serial ).lines == 0 );
                    REQUIRE( sessions.counters( serial ).startTime.isNull() );
                    REQUIRE( sessions.filePath( serial ).isEmpty() );
                }
            }

            sessions.stopAll();
        }
    }

    GIVEN( "a session appending to a save path that holds an earlier capture" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions( &host.settings() );
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        {
            QFile file( savePath );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
            file.write( "earlier\n" );
        }
        REQUIRE( sessions.startSession( serial, savePath ) );
        REQUIRE( waitFor( [ & ]() { return sessions.counters( serial ).lines == 2; } ) );

        THEN( "the file size counts the earlier capture, the bytes only this one" )
        {
            const auto counters = sessions.counters( serial );
            REQUIRE( counters.bytes == 13 );
            REQUIRE( counters.fileBytes == 8 + 13 );
            REQUIRE( QFileInfo( savePath ).size() == counters.fileBytes );
        }

        sessions.stopAll();
    }

    GIVEN( "a session that is rotated between two lines" )
    {
        FakeHost host;
        const auto trigger = host.configDir() + "/go";
        logcat_test::installFakeAdb( host, logcat_test::scriptWaitingFor( trigger ) );
        Sessions sessions( &host.settings() );
        REQUIRE( sessions.startSession( serial ) );
        REQUIRE( waitFor( [ & ]() { return sessions.counters( serial ).lines == 1; } ) );

        WHEN( "the next line arrives in the new file" )
        {
            sessions.rotateSession( serial );
            {
                QFile file( trigger );
                REQUIRE( file.open( QIODevice::WriteOnly ) );
            }
            REQUIRE( waitFor( [ & ]() { return sessions.counters( serial ).lines == 2; } ) );
            const auto counters = sessions.counters( serial );

            THEN( "lines and bytes count both files, the file size only the new one" )
            {
                REQUIRE( counters.rotations == 1 );
                REQUIRE( counters.bytes == 12 ); // "first\n", "after\n"
                REQUIRE( counters.fileBytes == 6 );
                REQUIRE( host.openedFiles.size() == 2 );
                REQUIRE( sessions.filePath( serial ) == host.openedFiles.last() );
                REQUIRE( QFileInfo( host.openedFiles.last() ).size() == counters.fileBytes );
            }

            AND_WHEN( "it is stopped" )
            {
                host.notifications.clear();
                sessions.stopSession( serial );

                THEN( "the notification counts the lines of both files" )
                {
                    REQUIRE( host.notifications.size() == 1 );
                    REQUIRE( host.notifications.first().toStdString()
                             == "Logcat stopped for emulator-5554 (2 lines)" );
                }
            }
        }

        sessions.stopAll();
    }
}
#endif

SCENARIO( "Sessions says when a session starts, stops or changes", "[sessions]" )
{
    const QString serial = "emulator-5554";

    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        Sessions sessions( &host.settings() );
        const SignalLog log( sessions );

        WHEN( "a session fails to start" )
        {
            REQUIRE_FALSE( sessions.startSession( serial ) );

            THEN( "nothing is announced" )
            {
                REQUIRE( log.started.isEmpty() );
                REQUIRE( log.stopped.isEmpty() );
                REQUIRE( log.changed.isEmpty() );
            }
        }
    }

#ifdef Q_OS_UNIX
    GIVEN( "a working adb" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        Sessions sessions( &host.settings() );
        const SignalLog log( sessions );
        bool runningWhenAnnounced = false;
        QObject::connect( &sessions, &Sessions::sessionStarted, [ & ]( const QString& started ) {
            runningWhenAnnounced
                = sessions.isSessionActive( started ) && host.openedFiles.size() == 1;
        } );

        WHEN( "a session starts" )
        {
            REQUIRE( sessions.startSession( serial ) );

            THEN( "its start is announced once it runs, with its tab open" )
            {
                REQUIRE( log.started == QStringList{ serial } );
                REQUIRE( runningWhenAnnounced );
                REQUIRE( log.stopped.isEmpty() );
            }

            AND_WHEN( "it is rotated" )
            {
                sessions.rotateSession( serial );

                THEN( "the change is announced" )
                {
                    REQUIRE( log.changed == QStringList{ serial } );
                }
            }

            AND_WHEN( "it is stopped" )
            {
                sessions.stopSession( serial );

                THEN( "its stop is announced, not as an end on its own" )
                {
                    REQUIRE( log.stopped == QStringList{ serial } );
                    REQUIRE( log.ended.isEmpty() );
                }
            }

            AND_WHEN( "all sessions are stopped" )
            {
                REQUIRE( sessions.startSession( "emulator-5556" ) );
                sessions.stopAll();

                THEN( "the stop of each is announced" )
                {
                    REQUIRE( log.stopped.size() == 2 );
                    REQUIRE( log.stopped.contains( serial ) );
                    REQUIRE( log.stopped.contains( "emulator-5556" ) );
                }
            }

            sessions.stopAll();
        }
    }

    GIVEN( "an adb that exits after one line" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "printf 'only\\n'\n" );
        Sessions sessions( &host.settings() );
        const SignalLog log( sessions );

        WHEN( "its session runs until adb exits" )
        {
            REQUIRE( sessions.startSession( serial ) );

            THEN( "its stop is announced, and it no longer runs" )
            {
                REQUIRE( waitFor( [ &log ]() { return !log.stopped.isEmpty(); } ) );
                REQUIRE( log.stopped == QStringList{ serial } );
                REQUIRE_FALSE( sessions.isSessionActive( serial ) );
            }

            THEN( "that it ended on its own is announced, so that the devices are rescanned" )
            {
                REQUIRE( waitFor( [ &log ]() { return !log.ended.isEmpty(); } ) );
                REQUIRE( log.ended == QStringList{ serial } );
            }
        }
    }
#endif
}
