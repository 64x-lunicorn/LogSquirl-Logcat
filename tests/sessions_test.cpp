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
 * @brief BDD tests for Sessions: the devices, and the logcat session of each.
 *
 * Sessions owns the device scan and the running AdbProcess sessions for
 * both the dialog and the sidebar.  These tests drive it through its
 * public API with a FakeHost and a stand-in for adb (see fakeadb.h).
 */

#include <catch2/catch.hpp>

#include "fakeadb.h"
#include "readonlydir.h"
#include "scopedenv.h"
#include "sessions.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <array>

using logcat::Sessions;
using logcat_test::FakeHost;
using logcat_test::waitFor;

namespace {

/** Collects the Qt messages printed during its lifetime. */
class MessageCapture {
public:
    MessageCapture()
    {
        messages().clear();
        previous_ = qInstallMessageHandler(
            []( QtMsgType, const QMessageLogContext&, const QString& message ) {
                messages().append( message );
            } );
    }

    ~MessageCapture()
    {
        qInstallMessageHandler( previous_ );
    }

    MessageCapture( const MessageCapture& ) = delete;
    MessageCapture& operator=( const MessageCapture& ) = delete;

    static QStringList& messages()
    {
        static QStringList list;
        return list;
    }

private:
    QtMessageHandler previous_ = nullptr;
};

/** The timer that gives up on a hanging `adb devices`. */
QTimer* scanTimeoutOf( const Sessions& sessions )
{
    const auto timers = sessions.findChildren<QTimer*>( QString(), Qt::FindDirectChildrenOnly );
    for ( auto* timer : timers ) {
        if ( timer->isSingleShot() && timer->interval() == 10000 ) {
            return timer;
        }
    }
    return nullptr;
}

/**
 * Hides adb from the PATH and the SDK environment variables while it
 * exists, so that findAdb() only finds one in a well-known location.
 */
class NoAdbAnywhere {
public:
    NoAdbAnywhere()
    {
        for ( const auto* name : kNames ) {
            saved_.append( qgetenv( name ) );
            qunsetenv( name );
        }
        qputenv( "PATH", "/nonexistent" );
    }

    ~NoAdbAnywhere()
    {
        for ( size_t i = 0; i < kNames.size(); ++i ) {
            if ( saved_[ static_cast<qsizetype>( i ) ].isNull() ) {
                qunsetenv( kNames[ i ] );
            }
            else {
                qputenv( kNames[ i ], saved_[ static_cast<qsizetype>( i ) ] );
            }
        }
    }

    NoAdbAnywhere( const NoAdbAnywhere& ) = delete;
    NoAdbAnywhere& operator=( const NoAdbAnywhere& ) = delete;

private:
    static constexpr std::array<const char*, 3> kNames
        = { "PATH", "ANDROID_HOME", "ANDROID_SDK_ROOT" };
    QList<QByteArray> saved_;
};

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
    }

    QStringList started;
    QStringList stopped;
    QStringList changed;
};

} // namespace

SCENARIO( "a device scan that cannot start does not time out later", "[sessions]" )
{
    GIVEN( "sessions whose first scan has failed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        Sessions sessions;
        auto* scanTimeout = scanTimeoutOf( sessions );
        REQUIRE( scanTimeout != nullptr );
        REQUIRE( waitFor( [ scanTimeout ]() { return !scanTimeout->isActive(); } ) );

        WHEN( "the next scan fails inside QProcess::start(), as it does on Windows" )
        {
            auto* scan = sessions.findChild<QProcess*>( QString(), Qt::FindDirectChildrenOnly );
            REQUIRE( scan != nullptr );
            QObject::connect( scan, &QProcess::stateChanged,
                              [ scan ]( QProcess::ProcessState state ) {
                                  if ( state == QProcess::Starting ) {
                                      Q_EMIT scan->errorOccurred( QProcess::FailedToStart );
                                  }
                              } );
            sessions.refreshDevices();

            THEN( "no timeout is pending for it" )
            {
                REQUIRE_FALSE( scanTimeout->isActive() );
            }

            waitFor( [ scan ]() { return scan->state() == QProcess::NotRunning; } );
        }
    }
}

SCENARIO( "destroying Sessions during a device scan ends the scan", "[sessions]" )
{
    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );

        WHEN( "Sessions is destroyed right after it started scanning" )
        {
            const MessageCapture capture;
            {
                Sessions sessions;
            }

            THEN( "no running process is left to Qt to destroy" )
            {
                REQUIRE( MessageCapture::messages().filter( "Destroyed while process" ).isEmpty() );
            }
        }
    }

#ifdef Q_OS_UNIX
    GIVEN( "an adb whose device scan hangs" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "exec sleep 30\n", "exec sleep 30\n" );

        WHEN( "Sessions is destroyed while the scan runs" )
        {
            const MessageCapture capture;
            QElapsedTimer timer;
            timer.start();
            {
                Sessions sessions;
                logcat_test::processEventsFor( 200 );
            }
            const auto elapsedMs = timer.elapsed();

            THEN( "the scan is killed, not waited for, and not left running" )
            {
                REQUIRE( elapsedMs < 3000 );
                REQUIRE( MessageCapture::messages().filter( "Destroyed while process" ).isEmpty() );
            }
        }
    }
#endif
}

SCENARIO( "a session is only accepted when adb starts", "[sessions]" )
{
    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        Sessions sessions;
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
        Sessions sessions;

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
        Sessions sessions;
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
        Sessions sessions;
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
        Sessions sessions;
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
        Sessions sessions;
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
        Sessions sessions;
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
        auto* sessions = new Sessions;
        REQUIRE( sessions->startSession( "emulator-5554" ) );
        REQUIRE( host.openedFiles.size() == 1 );
        const auto tempFile = host.openedFiles.first();
        // A scan started by stopAll() would be announced (scanningChanged()).
        REQUIRE( waitFor( [ sessions ]() { return !sessions->isScanning(); } ) );
        int scansDuringStop = 0;
        QObject::connect( sessions, &Sessions::scanningChanged,
                          [ &scansDuringStop ]() { ++scansDuringStop; } );

        WHEN( "the user stops all sessions: stopAll(), then Sessions is deleted" )
        {
            sessions->stopAll();
            QObject::disconnect( sessions, &Sessions::scanningChanged, nullptr, nullptr );
            delete sessions;

            THEN( "the temporary file is kept for its tab" )
            {
                REQUIRE( QFileInfo::exists( tempFile ) );
            }

            THEN( "the devices are not scanned again for the stopped session" )
            {
                REQUIRE( scansDuringStop == 0 );
            }
        }
    }

    GIVEN( "a session writing to a temporary file that has been rotated" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        auto* sessions = new Sessions;
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
SCENARIO( "stopping all sessions waits for them together", "[sessions]" )
{
    GIVEN( "several sessions whose adb ignores SIGTERM" )
    {
        FakeHost host;
        // sleep inherits the ignored SIGTERM, so only kill ends it.
        logcat_test::installFakeAdb( host, "trap '' TERM\nprintf 'first\\n'\nexec sleep 30\n" );
        auto* sessions = new Sessions;
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
                                    return sessions->sessionLineCount( serial ) > 0;
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
SCENARIO( "devices are discovered in the background", "[sessions]" )
{
    GIVEN( "an adb whose device scan takes a while" )
    {
        FakeHost host;
        logcat_test::installFakeAdb(
            host, "exec sleep 30\n",
            "sleep 1\nprintf 'List of devices attached\\nemulator-5554\\tdevice\\n\\n'\n" );

        WHEN( "Sessions is created, which scans for devices" )
        {
            QElapsedTimer timer;
            timer.start();
            Sessions sessions;
            const auto constructionMs = timer.elapsed();

            int changes = 0;
            QObject::connect( &sessions, &Sessions::devicesChanged, [ &changes ]() { ++changes; } );

            THEN( "creating it does not wait for the scan" )
            {
                REQUIRE( constructionMs < 500 );
                REQUIRE( sessions.devices().isEmpty() );
            }

            THEN( "the devices arrive when the scan completes" )
            {
                REQUIRE( waitFor( [ &changes ]() { return changes == 1; } ) );
                REQUIRE( sessions.devices() == QStringList{ "emulator-5554" } );
            }

            AND_WHEN( "more refreshes are requested while the scan runs" )
            {
                sessions.refreshDevices();
                sessions.refreshDevices();

                THEN( "they are folded into one more scan after the running one" )
                {
                    REQUIRE( waitFor( [ &changes ]() { return changes == 2; }, 8000 ) );
                    logcat_test::processEventsFor( 1500 ); // time for a third scan
                    REQUIRE( changes == 2 );
                }

                THEN( "the devices are logged once, as the second scan finds the same" )
                {
                    REQUIRE( waitFor( [ &changes ]() { return changes == 2; }, 8000 ) );
                    REQUIRE( host.logs.filter( "Discovered" ).size() == 1 );
                }
            }
        }
    }

    GIVEN( "sessions whose device scan hangs" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "exec sleep 30\n", "exec sleep 30\n" );
        Sessions sessions;
        int changes = 0;
        QObject::connect( &sessions, &Sessions::devicesChanged, [ &changes ]() { ++changes; } );

        WHEN( "the ADB path changes to a working adb" )
        {
            const auto workingAdb = host.configDir() + "/other-adb";
            {
                QFile file( workingAdb );
                REQUIRE( file.open( QIODevice::WriteOnly ) );
                file.write( "#!/bin/sh\n"
                            "printf 'List of devices attached\\nemulator-5556\\tdevice\\n\\n'\n" );
                file.setPermissions( file.permissions() | QFileDevice::ExeOwner );
            }
            logcat_test::useAdb( host, workingAdb );
            sessions.restartDeviceScan();

            THEN( "the hanging scan is abandoned and the new adb's devices arrive" )
            {
                REQUIRE( waitFor( [ &changes ]() { return changes == 1; }, 3000 ) );
                REQUIRE( sessions.devices() == QStringList{ "emulator-5556" } );
            }
        }

        WHEN( "the ADB path changes to one where no adb can be found" )
        {
            const NoAdbAnywhere noAdb;
            logcat_test::useAdb( host, host.configDir() + "/missing-adb" );
            if ( !logcat::AdbProcess::findAdb().isEmpty() ) {
                WARN( "An adb is installed in a well-known location; skipped." );
                return;
            }
            REQUIRE( sessions.isScanning() );
            int scanningChanges = 0;
            QObject::connect( &sessions, &Sessions::scanningChanged,
                              [ &scanningChanges ]() { ++scanningChanges; } );
            sessions.restartDeviceScan();

            THEN( "no scan runs any more, and that is announced" )
            {
                REQUIRE_FALSE( sessions.isScanning() );
                REQUIRE( scanningChanges >= 1 );
                REQUIRE( sessions.devices().isEmpty() );
            }
        }
    }
}
#endif

SCENARIO( "a device scan that keeps failing is logged once", "[sessions]" )
{
    // The sidebar rescans every few seconds: the same failure each time must
    // not fill the log.
    const auto scanTimes = []( Sessions& sessions, int times ) {
        for ( int i = 0; i < times; ++i ) {
            int changes = 0;
            const auto connection = QObject::connect( &sessions, &Sessions::devicesChanged,
                                                      [ &changes ]() { ++changes; } );
            sessions.refreshDevices();
            REQUIRE( waitFor(
                [ &changes, &sessions ]() { return changes > 0 && !sessions.isScanning(); } ) );
            QObject::disconnect( connection );
        }
    };

    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        Sessions sessions;
        REQUIRE( waitFor( [ &host ]() { return !host.logs.isEmpty(); } ) );

        WHEN( "the devices are scanned again and again" )
        {
            scanTimes( sessions, 3 );

            THEN( "the failure is logged once" )
            {
                REQUIRE( host.logs.filter( "could not be started" ).size() == 1 );
            }
        }
    }

#ifdef Q_OS_UNIX
    GIVEN( "an adb whose device scan fails with the message in a file, if any" )
    {
        FakeHost host;
        const auto messageFile = host.tempRoot() + "/message";
        const auto setMessage = [ &messageFile ]( const QByteArray& message ) {
            QFile file( messageFile );
            REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
            file.write( message );
        };
        setMessage( "daemon not running" );
        const auto quoted = "'" + messageFile.toUtf8() + "'";
        logcat_test::installFakeAdb(
            host, "exec sleep 30\n",
            "if [ -s " + quoted + " ]; then cat " + quoted
                + " >&2; exit 1; fi\n"
                  "printf 'List of devices attached\\nemulator-5554\\tdevice\\n\\n'\n" );
        Sessions sessions;
        scanTimes( sessions, 1 );

        WHEN( "the devices are scanned again and again" )
        {
            scanTimes( sessions, 3 );

            THEN( "the failure is logged once" )
            {
                INFO( host.logs.join( '\n' ).toStdString() );
                REQUIRE( host.logs.filter( "adb devices failed: daemon not running" ).size() == 1 );
            }
        }

        WHEN( "the failure changes" )
        {
            setMessage( "protocol fault" );
            scanTimes( sessions, 2 );

            THEN( "the new failure is logged, once" )
            {
                REQUIRE( host.logs.filter( "adb devices failed" ).size() == 2 );
                REQUIRE( host.logs.filter( "adb devices failed: protocol fault" ).size() == 1 );
            }
        }

        WHEN( "a scan succeeds, and the next fails the same way" )
        {
            setMessage( {} );
            scanTimes( sessions, 1 );
            setMessage( "daemon not running" );
            scanTimes( sessions, 2 );

            THEN( "the recovery is logged, and the failure again, once" )
            {
                INFO( host.logs.join( '\n' ).toStdString() );
                REQUIRE( host.logs.filter( "Discovered 1 device(s)" ).size() == 1 );
                REQUIRE( host.logs.filter( "adb devices failed: daemon not running" ).size() == 2 );
            }
        }
    }

    GIVEN( "no adb anywhere" )
    {
        FakeHost host;
        const NoAdbAnywhere noAdb;
        logcat_test::useAdb( host, host.configDir() + "/missing-adb" );
        if ( !logcat::AdbProcess::findAdb().isEmpty() ) {
            WARN( "An adb is installed in a well-known location; skipped." );
            return;
        }
        Sessions sessions;

        WHEN( "the devices are scanned again and again" )
        {
            scanTimes( sessions, 3 );

            THEN( "that adb is not found is logged once" )
            {
                REQUIRE( host.logs.filter( "adb not found" ).size() == 1 );
            }
        }
    }
#endif
}

#ifdef Q_OS_UNIX
SCENARIO( "Sessions knows every attached device, its state and its model", "[sessions]" )
{
    GIVEN( "an adb that lists a ready, an unauthorized and an offline device with -l" )
    {
        FakeHost host;
        // The long format only when asked for it, as adb does
        logcat_test::installFakeAdb(
            host, "exec sleep 30\n",
            "if [ \"$2\" = -l ]; then\n"
            "printf 'List of devices attached\\n"
            "3A1B2C3D4E5F  device usb:1-1 product:shiba model:Pixel_8 device:shiba\\n"
            "NOAUTH456     unauthorized usb:1-2 transport_id:3\\n"
            "OFFLINE123    offline transport_id:4\\n\\n'\n"
            "else\n"
            "printf 'List of devices attached\\n3A1B2C3D4E5F\\tdevice\\n\\n'\n"
            "fi\n" );

        WHEN( "Sessions has scanned" )
        {
            Sessions sessions;
            REQUIRE( waitFor( [ &sessions ]() { return !sessions.devices().isEmpty(); } ) );

            THEN( "every device is attached, with its state, and the ready one with its model" )
            {
                const auto& attached = sessions.attachedDevices();
                REQUIRE( attached.size() == 3 );
                REQUIRE( attached[ 0 ].serial == "3A1B2C3D4E5F" );
                REQUIRE( attached[ 0 ].model == "Pixel 8" );
                REQUIRE( attached[ 1 ].state == "unauthorized" );
                REQUIRE( attached[ 2 ].state == "offline" );
            }

            THEN( "only the ready device can run logcat" )
            {
                REQUIRE( sessions.devices() == QStringList{ "3A1B2C3D4E5F" } );
            }

            AND_WHEN( "a scan fails" )
            {
                logcat_test::installBrokenAdb( host );
                int changes = 0;
                QObject::connect( &sessions, &Sessions::devicesChanged,
                                  [ &changes ]() { ++changes; } );
                sessions.refreshDevices();
                REQUIRE( waitFor( [ &changes ]() { return changes == 1; } ) );

                THEN( "no device is attached any more" )
                {
                    REQUIRE( sessions.attachedDevices().isEmpty() );
                    REQUIRE( sessions.devices().isEmpty() );
                }
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
        Sessions sessions;

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
        Sessions sessions;
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
        Sessions sessions;
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
                REQUIRE( sessions.sessionLineCount( serial ) == 1 );
                REQUIRE( host.openedFiles.size() == 2 );
                REQUIRE( sessions.filePath( serial ) == host.openedFiles.last() );
                REQUIRE( QFileInfo( host.openedFiles.last() ).size() == counters.fileBytes );
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
        Sessions sessions;
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
        Sessions sessions;
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

                THEN( "its stop is announced" )
                {
                    REQUIRE( log.stopped == QStringList{ serial } );
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
        Sessions sessions;
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
        }
    }
#endif
}
