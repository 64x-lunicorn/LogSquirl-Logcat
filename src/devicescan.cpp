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
 * @file devicescan.cpp
 * @brief Implementation of DeviceScan, and of its QProcess adapter.
 */

#include "devicescan.h"
#include "plugin.h"
#include "settings.h"

#include <QProcess>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>

namespace logcat {

// ── QProcessAdbDevices ──────────────────────────────────────────────────

QProcessAdbDevices::QProcessAdbDevices( QObject* parent )
    : AdbDevicesRunner( parent )
    , process_( new QProcess( this ) )
{
    // Each handler runs through guarded(): an exception must not unwind
    // through Qt's event loop into the host.
    connect( process_, &QProcess::finished, this,
             [ this ]( int exitCode, QProcess::ExitStatus exitStatus ) {
                 guarded( "reading the device list", [ this, exitCode, exitStatus ] {
                     if ( exitStatus == QProcess::CrashExit ) {
                         // Killed: nothing to log
                         Q_EMIT failed( {} );
                     }
                     else if ( exitCode != 0 ) {
                         Q_EMIT failed(
                             "adb devices failed: "
                             + QString::fromUtf8( process_->readAllStandardError() ).trimmed() );
                     }
                     else {
                         Q_EMIT listed( process_->readAllStandardOutput() );
                     }
                 } );
             } );
    connect( process_, &QProcess::errorOccurred, this, [ this ]( QProcess::ProcessError error ) {
        guarded( "reporting a device scan error", [ this, error ] {
            // No finished() follows a failed start
            if ( error == QProcess::FailedToStart ) {
                Q_EMIT failed( "adb devices could not be started: " + process_->errorString() );
            }
        } );
    } );
}

QProcessAdbDevices::~QProcessAdbDevices()
{
    // A run that is still running is killed and reaped here rather than
    // left to ~QProcess, which only warns and waits for it.
    process_->disconnect( this );
    abandon();
}

void QProcessAdbDevices::start( const QString& adb )
{
    process_->setProgram( adb );
    // -l: also the model of each device
    process_->setArguments( { "devices", "-l" } );
    process_->start();
}

void QProcessAdbDevices::abandon()
{
    if ( process_->state() != QProcess::NotRunning ) {
        // Its result is not wanted, not even as a failure.
        const QSignalBlocker blocker( process_ );
        process_->kill();
        process_->waitForFinished( 1000 );
    }
}

// ── DeviceScan ──────────────────────────────────────────────────────────

DeviceScan::DeviceScan( const Settings* settings, AdbDevicesRunner* runner, QObject* parent )
    : QObject( parent )
    , settings_( settings )
    , runner_( runner ? runner : new QProcessAdbDevices )
{
    runner_->setParent( this );

    // Each handler runs through guarded(): an exception must not unwind
    // through Qt's event loop into the host.
    connect( runner_, &AdbDevicesRunner::listed, this, [ this ]( const QByteArray& output ) {
        guarded( "reading the device list", [ this, &output ] { onListed( output ); } );
    } );
    connect( runner_, &AdbDevicesRunner::failed, this, [ this ]( const QString& message ) {
        guarded( "reporting a device scan error", [ this, &message ] { onFailed( message ); } );
    } );

    // The first scan may need to start the ADB server, which can take
    // several seconds; give up after 10.
    timeout_ = new QTimer( this );
    timeout_->setSingleShot( true );
    timeout_->setInterval( kTimeoutMs );
    connect( timeout_, &QTimer::timeout, this, [ this ]() {
        guarded( "ending a device scan that timed out", [ this ] { onTimeout(); } );
    } );

    // A scan with the old adb is stale.
    connect( settings_, &Settings::adbPathChanged, this,
             [ this ]() { guarded( "scanning with the new adb", [ this ] { restart(); } ); } );

    refresh();
}

DeviceScan::~DeviceScan()
{
    // The runner must not report into a half-destroyed object.
    runner_->disconnect( this );
    runner_->abandon();
}

void DeviceScan::refresh()
{
    if ( scanning_ ) {
        rescanPending_ = true;
        return;
    }

    const auto adb = settings_->findAdb();
    if ( adb.isEmpty() ) {
        logFailure( "adb not found — cannot discover devices." );
        setDevices( {} );
        // A scan that ended just before may have left a view showing "Scanning…"
        Q_EMIT scanningChanged();
        return;
    }

    scanning_ = true;
    // Arm the timeout first: a runner may report a failure from inside
    // start(), and that stops the timeout.
    timeout_->start();
    runner_->start( adb );
    Q_EMIT scanningChanged();
}

void DeviceScan::restart()
{
    if ( scanning_ ) {
        // Its result is not wanted, not even as an empty list.
        timeout_->stop();
        runner_->abandon();
        scanning_ = false;
    }
    rescanPending_ = false;
    refresh();
}

void DeviceScan::onListed( const QByteArray& output )
{
    if ( !scanning_ ) {
        return; // abandoned
    }
    timeout_->stop();
    scanning_ = false;

    const auto found = AdbProcess::parseDevices( output );
    // The sidebar rescans every few seconds: only news is logged, and the
    // devices after a failure.
    if ( !scanned_ || found != attached_ || !scanError_.isEmpty() ) {
        const auto ready = std::count_if( found.cbegin(), found.cend(),
                                          []( const Device& d ) { return d.isReady(); } );
        scanned_ = true;
        scanError_.clear();
        hostLog( LOGSQUIRL_LOG_INFO,
                 QString( "Discovered %1 device(s), %2 ready." ).arg( found.size() ).arg( ready ) );
    }

    setDevices( found );
    onScanEnded();
}

void DeviceScan::onFailed( const QString& message )
{
    if ( !scanning_ ) {
        return; // abandoned
    }
    timeout_->stop();
    scanning_ = false;

    logFailure( message );
    setDevices( {} );
    onScanEnded();
}

void DeviceScan::onTimeout()
{
    if ( !scanning_ ) {
        return;
    }
    runner_->abandon();
    onFailed( "adb devices timed out." );
}

void DeviceScan::logFailure( const QString& message )
{
    // The sidebar rescans every few seconds: the same failure is logged once.
    if ( !message.isEmpty() && message != scanError_ ) {
        scanError_ = message;
        hostLog( LOGSQUIRL_LOG_WARNING, message );
    }
}

void DeviceScan::onScanEnded()
{
    if ( rescanPending_ ) {
        rescanPending_ = false;
        refresh();
    }
    else {
        Q_EMIT scanningChanged();
    }
}

void DeviceScan::setDevices( const QList<Device>& devices )
{
    attached_ = devices;
    devices_.clear();
    for ( const auto& device : devices ) {
        if ( device.isReady() ) {
            devices_.append( device.serial );
        }
    }
    Q_EMIT devicesChanged();
}

} // namespace logcat
