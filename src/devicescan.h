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
 * @file devicescan.h
 * @brief The devices adb lists: the device scan.
 *
 * DeviceScan runs `adb devices -l` in the background and keeps the devices
 * of the last scan.  Requests made while a scan runs are folded into one
 * more scan after it; restart() abandons a running scan, whose result is
 * stale; a scan that hangs is given up after 10 s; the same failure is
 * logged once; scanningChanged() says when a scan begins and when the last
 * one ends.
 *
 * It runs adb through a port, AdbDevicesRunner.  The plugin's adapter,
 * QProcessAdbDevices, starts a QProcess; the tests' adapter completes a
 * run when the test says so (tests/fakeadbdevices.h).
 *
 * The dialog (devicewidget.h) and the sidebar (sidebarwidget.h) show its
 * devices and whether it scans, and call refresh().
 *
 * OWNERSHIP
 * ─────────
 * Created by logsquirl_plugin_init(), deleted by logsquirl_plugin_shutdown()
 * after the views.  Its runner is its child.
 */

#pragma once

#include "adbprocess.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

namespace logcat {

/**
 * The port through which DeviceScan runs `adb devices -l`.
 *
 * A run reports exactly once, with listed() or failed(), unless it is
 * abandoned first.  The report may come from inside start() (a failure to
 * start, as on Windows), or later from the event loop.
 */
class AdbDevicesRunner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    /// Run `<adb> devices -l`.  Called only when no run is running.
    virtual void start( const QString& adb ) = 0;

    /// End the running run, if any, without a report.
    virtual void abandon() = 0;

Q_SIGNALS:
    /// adb exited with 0; @p output is what it wrote to stdout.
    void listed( const QByteArray& output );

    /// The run failed; @p message says why, for the log.  Empty if there is
    /// nothing to log (adb was killed).
    void failed( const QString& message );
};

/// The AdbDevicesRunner of the plugin: runs adb in a QProcess.
class QProcessAdbDevices : public AdbDevicesRunner {
    Q_OBJECT

public:
    explicit QProcessAdbDevices( QObject* parent = nullptr );

    /// Kills a run that still runs, and reaps it, without a report.
    ~QProcessAdbDevices() override;

    QProcessAdbDevices( const QProcessAdbDevices& ) = delete;
    QProcessAdbDevices& operator=( const QProcessAdbDevices& ) = delete;

    void start( const QString& adb ) override;
    void abandon() override;

private:
    QProcess* process_ = nullptr;
};

class DeviceScan : public QObject {
    Q_OBJECT

public:
    /// How long a scan may take before it is given up.
    static constexpr int kTimeoutMs = 10000;

    /// Scans with @p runner (which becomes its child), and with a
    /// QProcessAdbDevices if it is null.  Starts the first scan.
    explicit DeviceScan( AdbDevicesRunner* runner = nullptr, QObject* parent = nullptr );

    /// Abandons a scan that still runs.
    ~DeviceScan() override;

    DeviceScan( const DeviceScan& ) = delete;
    DeviceScan& operator=( const DeviceScan& ) = delete;

    /// Serials of the devices found by the last scan that can run logcat
    /// (state "device").
    const QStringList& devices() const
    {
        return devices_;
    }

    /// Every device found by the last scan, in any state (also
    /// unauthorized or offline), with its model, in adb's order.
    const QList<Device>& attachedDevices() const
    {
        return attached_;
    }

    /// Whether a scan runs.
    bool isScanning() const
    {
        return scanning_;
    }

public Q_SLOTS:
    /**
     * Scan again.  devicesChanged() is emitted when the scan has ended.
     * Requests made while a scan runs are folded into one more scan after
     * it: the running scan may have started before whatever prompted the
     * request.
     */
    void refresh();

    /**
     * Abandon a running scan and start a new one, e.g. because the ADB
     * path has changed and the running scan's result would be stale.
     */
    void restart();

Q_SIGNALS:
    /// A scan has ended and devices() is updated.
    void devicesChanged();

    /// A scan started, or the last one ended: see isScanning().
    void scanningChanged();

private:
    /** Take the output of a scan that listed the devices. */
    void onListed( const QByteArray& output );

    /** End a scan that failed with @p message. */
    void onFailed( const QString& message );

    /** Give up on a scan that hangs. */
    void onTimeout();

    /** Log @p message, unless it is empty, or the failure logged last with
     *  no successful scan since. */
    void logFailure( const QString& message );

    /** Store the result of a scan and announce it. */
    void setDevices( const QList<Device>& devices );

    /** A scan has ended: run the rescan requested during it, if any. */
    void onScanEnded();

    AdbDevicesRunner* runner_ = nullptr; ///< Runs `adb devices -l`.
    QTimer* timeout_ = nullptr;          ///< Gives up on a hanging scan.
    QList<Device> attached_;             ///< Result of the last scan.
    QStringList devices_;                ///< The ready devices of attached_.
    bool scanning_ = false;              ///< A scan runs.
    bool scanned_ = false;               ///< A scan has listed devices: they were logged.
    bool rescanPending_ = false;         ///< Scan again when the running scan ends.
    QString scanError_;                  ///< The failure logged last; empty after a success.
};

} // namespace logcat
