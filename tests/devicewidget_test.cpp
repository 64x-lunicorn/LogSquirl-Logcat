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
 * @file devicewidget_test.cpp
 * @brief BDD tests for the session bookkeeping in DeviceWidget.
 *
 * DeviceWidget owns the running AdbProcess sessions for both the dialog
 * and the sidebar.  These tests drive it through its public API with a
 * FakeHost and a stand-in for adb (see fakeadb.h).
 */

#include <catch2/catch.hpp>

#include "devicewidget.h"
#include "fakeadb.h"
#include "readonlydir.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

using logcat::DeviceWidget;
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
QTimer* scanTimeoutOf( const DeviceWidget& widget )
{
    const auto timers = widget.findChildren<QTimer*>( QString(), Qt::FindDirectChildrenOnly );
    for ( auto* timer : timers ) {
        if ( timer->isSingleShot() && timer->interval() == 10000 ) {
            return timer;
        }
    }
    return nullptr;
}

} // namespace

SCENARIO( "a device scan that cannot start does not time out later", "[devicewidget]" )
{
    GIVEN( "a widget whose first scan has failed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        DeviceWidget widget;
        auto* scanTimeout = scanTimeoutOf( widget );
        REQUIRE( scanTimeout != nullptr );
        REQUIRE( waitFor( [ scanTimeout ]() { return !scanTimeout->isActive(); } ) );

        WHEN( "the next scan fails inside QProcess::start(), as it does on Windows" )
        {
            auto* scan = widget.findChild<QProcess*>( QString(), Qt::FindDirectChildrenOnly );
            REQUIRE( scan != nullptr );
            QObject::connect( scan, &QProcess::stateChanged,
                              [ scan ]( QProcess::ProcessState state ) {
                                  if ( state == QProcess::Starting ) {
                                      Q_EMIT scan->errorOccurred( QProcess::FailedToStart );
                                  }
                              } );
            widget.refreshDevices();

            THEN( "no timeout is pending for it" )
            {
                REQUIRE_FALSE( scanTimeout->isActive() );
            }

            waitFor( [ scan ]() { return scan->state() == QProcess::NotRunning; } );
        }
    }
}

SCENARIO( "destroying the widget during a device scan ends the scan", "[devicewidget]" )
{
    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );

        WHEN( "the widget is destroyed right after it started scanning" )
        {
            const MessageCapture capture;
            {
                DeviceWidget widget;
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

        WHEN( "the widget is destroyed while the scan runs" )
        {
            const MessageCapture capture;
            QElapsedTimer timer;
            timer.start();
            {
                DeviceWidget widget;
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

SCENARIO( "a session is only accepted when adb starts", "[devicewidget]" )
{
    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        DeviceWidget widget;
        QTemporaryDir logDir;

        WHEN( "starting a session with a save path" )
        {
            const auto savePath = logDir.filePath( "capture.log" );
            const auto started = widget.startSession( "emulator-5554", savePath );

            THEN( "the session is rejected" )
            {
                REQUIRE_FALSE( started );
                REQUIRE( widget.activeSessionCount() == 0 );
                REQUIRE_FALSE( widget.isSessionActive( "emulator-5554" ) );
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
        DeviceWidget widget;

        WHEN( "starting a session" )
        {
            const auto started = widget.startSession( "emulator-5554" );

            THEN( "the session is accepted and its file opened in a tab" )
            {
                REQUIRE( started );
                REQUIRE( widget.isSessionActive( "emulator-5554" ) );
                REQUIRE( host.openedFiles.size() == 1 );
            }

            widget.stopAll();
        }
    }
#endif
}

#ifdef Q_OS_UNIX
SCENARIO( "two sessions never write to the same file", "[devicewidget]" )
{
    GIVEN( "a session writing to a save path" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        DeviceWidget widget;
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        REQUIRE( widget.startSession( "emulator-5554", savePath ) );
        host.notifications.clear();

        WHEN( "starting a second device with the same save path" )
        {
            const auto started = widget.startSession( "emulator-5556", savePath );

            THEN( "the second session is refused, and the user told why" )
            {
                REQUIRE_FALSE( started );
                REQUIRE_FALSE( widget.isSessionActive( "emulator-5556" ) );
                REQUIRE( host.notifications.size() == 1 );
            }
        }

        widget.stopAll();
    }

    GIVEN( "a session whose save file has been deleted while it runs" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        DeviceWidget widget;
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        REQUIRE( widget.startSession( "emulator-5554", savePath ) );
        REQUIRE( QFile::remove( savePath ) );

        WHEN( "starting a second device with a different, new save path" )
        {
            const auto started
                = widget.startSession( "emulator-5556", logDir.filePath( "other.log" ) );

            THEN( "it is accepted: two missing files are not the same file" )
            {
                REQUIRE( started );
                REQUIRE( widget.isSessionActive( "emulator-5556" ) );
            }
        }

        WHEN( "starting a second device with the same save path, spelled differently" )
        {
            const auto started
                = widget.startSession( "emulator-5556", logDir.path() + "/sub/../capture.log" );

            THEN( "it is still refused" )
            {
                REQUIRE_FALSE( started );
            }
        }

        widget.stopAll();
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "stopping a session is not reported as an error", "[devicewidget]" )
{
    GIVEN( "a running session" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        DeviceWidget widget;
        REQUIRE( widget.startSession( "emulator-5554" ) );
        host.notifications.clear();

        WHEN( "the user stops it" )
        {
            widget.stopSession( "emulator-5554" );

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
SCENARIO( "a failed rotation is reported once", "[devicewidget]" )
{
    GIVEN( "a session whose log directory no longer accepts new files" )
    {
        if ( !logcat_test::ReadOnlyDir::isEnforced() ) {
            WARN( "File permissions are not enforced (running as root?); skipped." );
            return;
        }

        FakeHost host;
        logcat_test::installFakeAdb( host );
        DeviceWidget widget;
        QTemporaryDir logDir;
        REQUIRE( widget.startSession( "emulator-5554", logDir.filePath( "capture.log" ) ) );
        host.notifications.clear();
        host.openedFiles.clear();

        WHEN( "rotating the session" )
        {
            {
                const logcat_test::ReadOnlyDir readOnly( logDir.path() );
                widget.rotateSession( "emulator-5554" );
            }

            THEN( "the user is told once, no tab is opened, and the session goes on" )
            {
                REQUIRE( host.notifications.size() == 1 );
                REQUIRE( host.openedFiles.isEmpty() );
                REQUIRE( widget.isSessionActive( "emulator-5554" ) );
            }
        }

        widget.stopAll();
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "stopAll decides whether temporary log files survive", "[devicewidget]" )
{
    GIVEN( "a session writing to a temporary file" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        auto* widget = new DeviceWidget;
        REQUIRE( widget->startSession( "emulator-5554" ) );
        REQUIRE( host.openedFiles.size() == 1 );
        const auto tempFile = host.openedFiles.first();
        const auto scansBefore = host.logs.filter( "Discovered" ).size();

        WHEN( "the plugin shuts down: stopAll( true ), then the widget is deleted" )
        {
            widget->stopAll( true );
            const auto scansDuringStop = host.logs.filter( "Discovered" ).size() - scansBefore;
            delete widget;

            THEN( "the temporary file is removed" )
            {
                REQUIRE_FALSE( QFileInfo::exists( tempFile ) );
            }

            THEN( "the devices are not scanned again for the stopped session" )
            {
                REQUIRE( scansDuringStop == 0 );
            }
        }

        WHEN( "the user stops all sessions: stopAll(), then the widget is deleted" )
        {
            widget->stopAll();
            delete widget;

            THEN( "the temporary file is kept for its tab" )
            {
                REQUIRE( QFileInfo::exists( tempFile ) );
            }

            QDir( QFileInfo( tempFile ).absolutePath() ).removeRecursively();
        }
    }

    GIVEN( "a session writing to a temporary file that has been rotated" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host );
        auto* widget = new DeviceWidget;
        REQUIRE( widget->startSession( "emulator-5554" ) );
        widget->rotateSession( "emulator-5554" );
        REQUIRE( host.openedFiles.size() == 2 );
        const auto tempDir = QFileInfo( host.openedFiles.first() ).absolutePath();
        REQUIRE( QFileInfo( host.openedFiles.last() ).absolutePath() == tempDir );

        WHEN( "the plugin shuts down: stopAll( true ), then the widget is deleted" )
        {
            widget->stopAll( true );
            delete widget;

            THEN( "the temporary directory is removed with the files of both tabs" )
            {
                REQUIRE_FALSE( QFileInfo::exists( tempDir ) );
            }
        }

        WHEN( "the user stops all sessions: stopAll(), then the widget is deleted" )
        {
            widget->stopAll();
            delete widget;

            THEN( "the files of both tabs are kept" )
            {
                REQUIRE( QFileInfo::exists( host.openedFiles.first() ) );
                REQUIRE( QFileInfo::exists( host.openedFiles.last() ) );
            }

            QDir( tempDir ).removeRecursively();
        }
    }
}
#endif

#ifdef Q_OS_UNIX
SCENARIO( "devices are discovered in the background", "[devicewidget]" )
{
    GIVEN( "an adb whose device scan takes a while" )
    {
        FakeHost host;
        logcat_test::installFakeAdb(
            host, "exec sleep 30\n",
            "sleep 1\nprintf 'List of devices attached\\nemulator-5554\\tdevice\\n\\n'\n" );

        WHEN( "the widget is created, which scans for devices" )
        {
            QElapsedTimer timer;
            timer.start();
            DeviceWidget widget;
            const auto constructionMs = timer.elapsed();

            int changes = 0;
            QObject::connect( &widget, &DeviceWidget::devicesChanged,
                              [ &changes ]() { ++changes; } );

            THEN( "creating it does not wait for the scan" )
            {
                REQUIRE( constructionMs < 500 );
                REQUIRE( widget.devices().isEmpty() );
            }

            THEN( "the devices arrive when the scan completes" )
            {
                REQUIRE( waitFor( [ &changes ]() { return changes == 1; } ) );
                REQUIRE( widget.devices() == QStringList{ "emulator-5554" } );
            }

            AND_WHEN( "more refreshes are requested while the scan runs" )
            {
                widget.refreshDevices();
                widget.refreshDevices();

                THEN( "they are folded into the running scan" )
                {
                    REQUIRE( waitFor( [ &changes ]() { return changes == 1; } ) );
                    logcat_test::processEventsFor( 1500 ); // time for a second scan
                    REQUIRE( changes == 1 );
                    REQUIRE( host.logs.filter( "Discovered" ).size() == 1 );
                }
            }
        }
    }
}
#endif
