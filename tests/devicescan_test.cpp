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
 * @file devicescan_test.cpp
 * @brief BDD tests for DeviceScan: the devices adb lists.
 *
 * The scan runs `adb devices -l` through FakeAdbDevices
 * (fakeadbdevices.h), and each test ends each run when it chooses, with
 * the output or failure it chooses: nothing runs and nothing waits.  The
 * last scenario checks the plugin's adapter, which runs a stand-in for adb
 * in a QProcess (fakeadb.h).
 */

#include <catch2/catch.hpp>

#include "devicescan.h"
#include "fakeadb.h"
#include "fakeadbdevices.h"

#include <QElapsedTimer>
#include <QTimer>

#include <array>

using logcat::DeviceScan;
using logcat_test::FakeAdbDevices;
using logcat_test::FakeHost;
using logcat_test::waitFor;

namespace {

/** Counts the signals of a DeviceScan. */
struct ScanLog {
    explicit ScanLog( const DeviceScan& scan )
    {
        QObject::connect( &scan, &DeviceScan::devicesChanged, [ this ]() { ++changes; } );
        QObject::connect( &scan, &DeviceScan::scanningChanged,
                          [ this, &scan ]() { scanning << scan.isScanning(); } );
    }

    int changes = 0;      ///< devicesChanged()
    QList<bool> scanning; ///< isScanning() at each scanningChanged()
};

/** The timer that gives up on a hanging scan. */
QTimer* timeoutOf( const DeviceScan& scan )
{
    const auto timers = scan.findChildren<QTimer*>( QString(), Qt::FindDirectChildrenOnly );
    return timers.size() == 1 ? timers.first() : nullptr;
}

/** Let the timeout of @p scan expire, as it would after 10 s. */
void expireTimeout( const DeviceScan& scan )
{
    auto* timer = timeoutOf( scan );
    REQUIRE( timer != nullptr );
    REQUIRE( timer->isActive() );
    timer->stop();
    REQUIRE( QMetaObject::invokeMethod( timer, "timeout" ) );
}

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

#ifdef Q_OS_UNIX
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
#endif

} // namespace

SCENARIO( "the device scan knows every attached device, its state and its model", "[devicescan]" )
{
    GIVEN( "a scan, which starts scanning when it is created" )
    {
        FakeHost host;
        const auto adb = logcat_test::installBrokenAdb( host ); // only its path is used
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );
        const ScanLog log( scan );

        THEN( "it scans with the configured adb, and knows no device yet" )
        {
            REQUIRE( scan.isScanning() );
            REQUIRE( runner->adbs == QStringList{ adb } );
            REQUIRE( scan.devices().isEmpty() );
            REQUIRE( scan.attachedDevices().isEmpty() );
        }

        WHEN( "adb lists a ready, an unauthorized and an offline device" )
        {
            runner->list( "List of devices attached\n"
                          "3A1B2C3D4E5F  device usb:1-1 product:shiba model:Pixel_8 device:shiba\n"
                          "NOAUTH456     unauthorized usb:1-2 transport_id:3\n"
                          "OFFLINE123    offline transport_id:4\n\n" );

            THEN( "every device is attached, with its state, and the ready one with its model" )
            {
                const auto& attached = scan.attachedDevices();
                REQUIRE( attached.size() == 3 );
                REQUIRE( attached[ 0 ].serial == "3A1B2C3D4E5F" );
                REQUIRE( attached[ 0 ].model == "Pixel 8" );
                REQUIRE( attached[ 1 ].state == "unauthorized" );
                REQUIRE( attached[ 2 ].state == "offline" );
            }

            THEN( "only the ready device can run logcat" )
            {
                REQUIRE( scan.devices() == QStringList{ "3A1B2C3D4E5F" } );
            }

            THEN( "the change is announced once, and the scan is over" )
            {
                REQUIRE( log.changes == 1 );
                REQUIRE_FALSE( scan.isScanning() );
            }

            AND_WHEN( "the next scan fails" )
            {
                scan.refresh();
                runner->fail( "adb devices failed: protocol fault" );

                THEN( "no device is attached any more" )
                {
                    REQUIRE( scan.attachedDevices().isEmpty() );
                    REQUIRE( scan.devices().isEmpty() );
                    REQUIRE( log.changes == 2 );
                }
            }
        }
    }
}

SCENARIO( "refreshes requested during a scan are folded into one more scan", "[devicescan]" )
{
    GIVEN( "a scan whose first run is running" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );
        const ScanLog log( scan );

        WHEN( "two refreshes are requested while it runs" )
        {
            scan.refresh();
            scan.refresh();

            THEN( "no second run starts while the first runs" )
            {
                REQUIRE( runner->adbs.size() == 1 );
            }

            AND_WHEN( "the first run ends" )
            {
                runner->listOne( "emulator-5554" );

                THEN( "its devices are announced, and one more run starts" )
                {
                    REQUIRE( log.changes == 1 );
                    REQUIRE( scan.devices() == QStringList{ "emulator-5554" } );
                    REQUIRE( runner->adbs.size() == 2 );
                    REQUIRE( scan.isScanning() );
                }

                AND_WHEN( "that run ends too, with the same devices" )
                {
                    runner->listOne( "emulator-5554" );

                    THEN( "no third run starts" )
                    {
                        REQUIRE( log.changes == 2 );
                        REQUIRE( runner->adbs.size() == 2 );
                        REQUIRE_FALSE( scan.isScanning() );
                    }

                    THEN( "the devices are logged once, as the second scan finds the same" )
                    {
                        REQUIRE( host.logs.filter( "Discovered" ).size() == 1 );
                        REQUIRE( host.logs.contains( "Discovered 1 device(s), 1 ready." ) );
                    }
                }
            }
        }
    }
}

SCENARIO( "a restart abandons the running scan, whose result is stale", "[devicescan]" )
{
    GIVEN( "a scan whose run hangs, with a refresh requested during it" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );
        scan.refresh();
        const ScanLog log( scan );

        WHEN( "the ADB path changes, and the scan is restarted" )
        {
            const auto otherAdb = host.configDir() + "/other-adb";
            {
                QFile file( otherAdb );
                REQUIRE( file.open( QIODevice::WriteOnly ) );
            }
            logcat_test::useAdb( host, otherAdb );
            scan.restart();

            THEN( "the hanging run is abandoned, unreported, and the new adb runs" )
            {
                REQUIRE( runner->abandoned == 1 );
                REQUIRE( log.changes == 0 );
                REQUIRE( runner->adbs.size() == 2 );
                REQUIRE( runner->adbs.last() == otherAdb );
                REQUIRE( scan.isScanning() );
            }

            AND_WHEN( "the new run ends" )
            {
                runner->listOne( "emulator-5556" );

                THEN( "its devices arrive, and the refresh of the stale scan is not run" )
                {
                    REQUIRE( scan.devices() == QStringList{ "emulator-5556" } );
                    REQUIRE( log.changes == 1 );
                    REQUIRE( runner->adbs.size() == 2 );
                    REQUIRE_FALSE( scan.isScanning() );
                }
            }
        }

#ifdef Q_OS_UNIX
        WHEN( "the ADB path changes to one where no adb can be found, and the scan is restarted" )
        {
            const NoAdbAnywhere noAdb;
            logcat_test::useAdb( host, host.configDir() + "/missing-adb" );
            if ( !logcat::AdbProcess::findAdb().isEmpty() ) {
                WARN( "An adb is installed in a well-known location; skipped." );
                return;
            }
            scan.restart();

            THEN( "no scan runs any more, and that is announced" )
            {
                REQUIRE( runner->abandoned == 1 );
                REQUIRE_FALSE( scan.isScanning() );
                REQUIRE( log.scanning == QList<bool>{ false } );
                REQUIRE( scan.devices().isEmpty() );
                REQUIRE( host.logs.filter( "adb not found" ).size() == 1 );
            }
        }
#endif
    }

    GIVEN( "a scan that is not running" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );
        runner->listOne( "emulator-5554" );

        WHEN( "it is restarted" )
        {
            scan.restart();

            THEN( "nothing is abandoned, and a scan starts" )
            {
                REQUIRE( runner->abandoned == 0 );
                REQUIRE( runner->adbs.size() == 2 );
                REQUIRE( scan.isScanning() );
            }
        }
    }
}

SCENARIO( "a scan that hangs is given up after 10 s", "[devicescan]" )
{
    GIVEN( "a scan whose run hangs" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );
        runner->listOne( "emulator-5554" );
        scan.refresh();
        const ScanLog log( scan );

        THEN( "a timeout of 10 s runs for it" )
        {
            auto* timer = timeoutOf( scan );
            REQUIRE( timer != nullptr );
            REQUIRE( timer->isActive() );
            REQUIRE( timer->isSingleShot() );
            REQUIRE( timer->interval() == 10000 );
        }

        WHEN( "the timeout expires" )
        {
            expireTimeout( scan );

            THEN( "the run is abandoned, and that is logged" )
            {
                REQUIRE( runner->abandoned == 1 );
                REQUIRE( host.logs.filter( "adb devices timed out." ).size() == 1 );
            }

            THEN( "no device is attached, and the scan is over" )
            {
                REQUIRE( scan.devices().isEmpty() );
                REQUIRE( scan.attachedDevices().isEmpty() );
                REQUIRE( log.changes == 1 );
                REQUIRE_FALSE( scan.isScanning() );
                REQUIRE( log.scanning == QList<bool>{ false } );
            }
        }

        WHEN( "the run ends in time" )
        {
            runner->listOne( "emulator-5554" );

            THEN( "the timeout is stopped" )
            {
                REQUIRE_FALSE( timeoutOf( scan )->isActive() );
            }
        }
    }

    GIVEN( "a scan whose runs fail inside start(), as QProcess does on Windows" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        runner->failOnStart = "adb devices could not be started: not executable";
        DeviceScan scan( runner );

        THEN( "no timeout is pending, and no scan runs" )
        {
            REQUIRE_FALSE( timeoutOf( scan )->isActive() );
            REQUIRE_FALSE( scan.isScanning() );
            REQUIRE( host.logs.filter( "could not be started" ).size() == 1 );
        }
    }
}

SCENARIO( "a device scan that keeps failing is logged once", "[devicescan]" )
{
    // The sidebar rescans every few seconds: the same failure each time must
    // not fill the log.
    GIVEN( "a scan whose runs fail" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );
        runner->fail( "adb devices failed: daemon not running" );
        const auto scanFailing = [ &scan, runner ]( const QString& message, int times ) {
            for ( int i = 0; i < times; ++i ) {
                scan.refresh();
                runner->fail( message );
            }
        };

        WHEN( "the devices are scanned again and again" )
        {
            scanFailing( "adb devices failed: daemon not running", 3 );

            THEN( "the failure is logged once" )
            {
                REQUIRE( host.logs.filter( "adb devices failed: daemon not running" ).size() == 1 );
            }
        }

        WHEN( "the failure changes" )
        {
            scanFailing( "adb devices failed: protocol fault", 2 );

            THEN( "the new failure is logged, once" )
            {
                REQUIRE( host.logs.filter( "adb devices failed" ).size() == 2 );
                REQUIRE( host.logs.filter( "adb devices failed: protocol fault" ).size() == 1 );
            }
        }

        WHEN( "a scan succeeds, and the next fail the same way" )
        {
            scan.refresh();
            runner->listOne( "emulator-5554" );
            scanFailing( "adb devices failed: daemon not running", 2 );

            THEN( "the recovery is logged, and the failure again, once" )
            {
                INFO( host.logs.join( '\n' ).toStdString() );
                REQUIRE( host.logs.filter( "Discovered 1 device(s)" ).size() == 1 );
                REQUIRE( host.logs.filter( "adb devices failed: daemon not running" ).size() == 2 );
            }
        }

        WHEN( "a run fails with nothing to log, as when adb is killed" )
        {
            const auto logged = host.logs.size();
            scanFailing( {}, 1 );

            THEN( "nothing is logged, and no device is attached" )
            {
                REQUIRE( host.logs.size() == logged );
                REQUIRE( scan.devices().isEmpty() );
                REQUIRE_FALSE( scan.isScanning() );
            }
        }
    }

    GIVEN( "a scan whose runs succeed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );
        runner->listOne( "emulator-5554" );

        WHEN( "the devices change" )
        {
            scan.refresh();
            runner->list( "List of devices attached\n\n" );

            THEN( "the new devices are logged" )
            {
                REQUIRE( host.logs.filter( "Discovered" ).size() == 2 );
                REQUIRE( host.logs.contains( "Discovered 0 device(s), 0 ready." ) );
            }
        }
    }

#ifdef Q_OS_UNIX
    GIVEN( "no adb anywhere" )
    {
        FakeHost host;
        const NoAdbAnywhere noAdb;
        logcat_test::useAdb( host, host.configDir() + "/missing-adb" );
        if ( !logcat::AdbProcess::findAdb().isEmpty() ) {
            WARN( "An adb is installed in a well-known location; skipped." );
            return;
        }
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );

        WHEN( "the devices are scanned again and again" )
        {
            scan.refresh();
            scan.refresh();
            scan.refresh();

            THEN( "that adb is not found is logged once, and adb never runs" )
            {
                REQUIRE( host.logs.filter( "adb not found" ).size() == 1 );
                REQUIRE( runner->adbs.isEmpty() );
                REQUIRE_FALSE( scan.isScanning() );
            }
        }
    }
#endif
}

SCENARIO( "the scan says when \"Scanning…\" begins and ends", "[devicescan]" )
{
    GIVEN( "a scan that is not running" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );
        auto* runner = new FakeAdbDevices;
        DeviceScan scan( runner );
        runner->listOne( "emulator-5554" );
        const ScanLog log( scan );

        WHEN( "a refresh starts a scan" )
        {
            scan.refresh();

            THEN( "its beginning is announced" )
            {
                REQUIRE( log.scanning == QList<bool>{ true } );
            }

            AND_WHEN( "it ends" )
            {
                runner->listOne( "emulator-5554" );

                THEN( "its end is announced, after the devices" )
                {
                    REQUIRE( log.scanning == QList<bool>{ true, false } );
                    REQUIRE( log.changes == 1 );
                }
            }

            AND_WHEN( "it fails" )
            {
                runner->fail( "adb devices failed: protocol fault" );

                THEN( "its end is announced" )
                {
                    REQUIRE( log.scanning == QList<bool>{ true, false } );
                }
            }

            AND_WHEN( "it ends with a refresh requested during it" )
            {
                scan.refresh();
                runner->listOne( "emulator-5554" );

                THEN( "scanning goes on into the next scan, and only that is announced" )
                {
                    REQUIRE( log.scanning == QList<bool>{ true, true } );
                    REQUIRE( scan.isScanning() );
                }
            }
        }
    }
}

SCENARIO( "the QProcess adapter runs adb devices -l", "[devicescan]" )
{
#ifdef Q_OS_UNIX
    GIVEN( "an adb that lists a device in the long format only when asked for it, as adb does" )
    {
        FakeHost host;
        logcat_test::installFakeAdb(
            host, "exec sleep 30\n",
            "if [ \"$2\" = -l ]; then\n"
            "printf 'List of devices attached\\n"
            "3A1B2C3D4E5F  device usb:1-1 product:shiba model:Pixel_8 device:shiba\\n"
            "NOAUTH456     unauthorized usb:1-2 transport_id:3\\n\\n'\n"
            "else\n"
            "printf 'List of devices attached\\n3A1B2C3D4E5F\\tdevice\\n\\n'\n"
            "fi\n" );

        WHEN( "the scan has run it" )
        {
            QElapsedTimer timer;
            timer.start();
            const DeviceScan scan;
            const auto constructionMs = timer.elapsed();
            REQUIRE( waitFor( [ &scan ]() { return !scan.isScanning(); } ) );

            THEN( "it ran in the background, and its devices and models were read" )
            {
                REQUIRE( constructionMs < 500 );
                REQUIRE( scan.attachedDevices().size() == 2 );
                REQUIRE( scan.attachedDevices()[ 0 ].model == "Pixel 8" );
                REQUIRE( scan.devices() == QStringList{ "3A1B2C3D4E5F" } );
            }
        }
    }

    GIVEN( "an adb whose device scan fails with a message" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "exec sleep 30\n",
                                     "printf 'daemon not running\\n' >&2\nexit 1\n" );

        WHEN( "the scan has run it" )
        {
            const DeviceScan scan;
            REQUIRE( waitFor( [ &scan ]() { return !scan.isScanning(); } ) );

            THEN( "adb's message is logged" )
            {
                REQUIRE( host.logs.contains( "adb devices failed: daemon not running" ) );
            }
        }
    }

    GIVEN( "an adb whose device scan hangs" )
    {
        FakeHost host;
        logcat_test::installFakeAdb( host, "exec sleep 30\n", "exec sleep 30\n" );

        WHEN( "the scan is destroyed while it runs" )
        {
            const MessageCapture capture;
            QElapsedTimer timer;
            timer.start();
            {
                const DeviceScan scan;
                logcat_test::processEventsFor( 200 );
                REQUIRE( scan.isScanning() );
            }
            const auto elapsedMs = timer.elapsed();

            THEN( "adb is killed, not waited for, and not left running" )
            {
                REQUIRE( elapsedMs < 3000 );
                REQUIRE( MessageCapture::messages().filter( "Destroyed while process" ).isEmpty() );
            }
        }
    }
#endif

    GIVEN( "an adb that cannot be executed" )
    {
        FakeHost host;
        logcat_test::installBrokenAdb( host );

        WHEN( "the scan has tried to run it" )
        {
            const DeviceScan scan;
            REQUIRE( waitFor( [ &scan ]() { return !scan.isScanning(); } ) );

            THEN( "the failure is logged" )
            {
                REQUIRE( host.logs.filter( "adb devices could not be started" ).size() == 1 );
            }
        }

        WHEN( "the scan is destroyed right after it started" )
        {
            const MessageCapture capture;
            {
                const DeviceScan scan;
            }

            THEN( "no running process is left to Qt to destroy" )
            {
                REQUIRE( MessageCapture::messages().filter( "Destroyed while process" ).isEmpty() );
            }
        }
    }
}
