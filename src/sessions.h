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
 * @file sessions.h
 * @brief The devices adb lists, and the logcat session of each.
 *
 * Sessions scans for devices with `adb devices -l` in the background, starts
 * an AdbProcess for a device (adbprocess.h), opens its file in a followed
 * tab, opens the next file in a new tab when the session is rotated, and
 * says through its signals when a session starts, stops or changes.  A
 * device runs one session at a time; several devices run side by side,
 * each with its own adb, file and tab.
 *
 * The dialog (devicewidget.h) and the sidebar (sidebarwidget.h) are views
 * of one Sessions: they show its devices and sessions and call it to
 * start, stop and rotate them.
 *
 * Starting, stopping and errors are host notifications; stopAll() (the
 * dialog's and the sidebar's Stop All, and shutdown) notifies nothing.  A
 * session's temporary file is kept for its tab when the session ends; the
 * plugin removes the temporary files when LogSquirl quits (tempdirs.h).
 *
 * COUNTERS
 * ────────
 * counters() reads a running session's SessionCounters live: lines and
 * bytes grow with every line adb writes, and no signal is emitted for
 * them, so a view that shows them polls (sessionChanged() is for a new
 * file).
 *
 * OWNERSHIP
 * ─────────
 * Created by logsquirl_plugin_init(), deleted by logsquirl_plugin_shutdown()
 * after stopAll() and after the views.  The running AdbProcesses and the
 * device scan are its children.
 */

#pragma once

#include "adbprocess.h"

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QTimer;

namespace logcat {

class Sessions : public QObject {
    Q_OBJECT

public:
    /// Starts the first device scan, in the background.
    explicit Sessions( QObject* parent = nullptr );

    /// Kills a device scan that still runs.  Sessions that still run are
    /// ended without signals and without keeping their temporary files:
    /// call stopAll() first.
    ~Sessions() override;

    Sessions( const Sessions& ) = delete;
    Sessions& operator=( const Sessions& ) = delete;

    // ── Devices ──────────────────────────────────────────────────────

    /// Serials of the devices found by the most recent scan that can run
    /// logcat (state "device").
    const QStringList& devices() const
    {
        return devices_;
    }

    /// Every device found by the most recent scan, in any state (also
    /// unauthorized or offline), with its model, in adb's order.
    const QList<Device>& attachedDevices() const
    {
        return attached_;
    }

    /// Whether a device scan runs.
    bool isScanning() const;

    // ── Sessions ─────────────────────────────────────────────────────

    /**
     * Start logcat capture for the device @p serial and open its file in a
     * followed tab.
     *
     * @param serial    Device serial to capture.
     * @param savePath  Optional path to a .log file for persistent saving,
     *                  appended to.  Refused if another running session
     *                  writes to it.
     * @return true if the session started; false if it did not (the user
     *         has been told why), or if the device runs a session already.
     */
    bool startSession( const QString& serial, const QString& savePath = {} );

    /// Stop the session of @p serial, keep its file for its tab, and say so
    /// in a host notification.  No-op if it does not run.
    void stopSession( const QString& serial );

    /**
     * Stop every session, together, keeping their files for their tabs.
     * Notifies nothing.
     *
     * Their temporary log files are kept also when the plugin is disabled
     * or updated at runtime; the plugin removes them when LogSquirl quits.
     */
    void stopAll();

    /**
     * Rotate the log of the session of @p serial: its file is kept for its
     * tab, the capture goes on in a new file, and a new tab follows that.
     * No-op if it does not run; a new file that cannot be created is a
     * host notification, and the capture goes on in the old one.
     */
    void rotateSession( const QString& serial );

    /// Whether the session of @p serial runs.
    bool isSessionActive( const QString& serial ) const;

    /// Number of running sessions.
    int activeSessionCount() const;

    /// Serials of the running sessions, sorted.
    QStringList activeSerials() const;

    /// Lines in the current file of the session of @p serial (they start
    /// from 0 on each rotation); 0 if it does not run.
    qint64 sessionLineCount( const QString& serial ) const;

    /// What the session of @p serial has captured so far, read live; all
    /// zero (and a null start time) if it does not run.
    SessionCounters counters( const QString& serial ) const;

    /// The file the session of @p serial writes; empty if it does not run.
    QString filePath( const QString& serial ) const;

public Q_SLOTS:
    /**
     * Re-scan for ADB devices.  `adb devices` runs in the background
     * (starting the ADB server can take seconds); devicesChanged() is
     * emitted when it has finished.  Requests made while a scan is
     * running are folded into one more scan after it: the running scan
     * may have started before whatever prompted the request.
     */
    void refreshDevices();

    /**
     * Abandon a running device scan and start a new one, e.g. because the
     * ADB path has changed and the running scan's result would be stale.
     */
    void restartDeviceScan();

Q_SIGNALS:
    /// A device scan has finished and devices() is updated.
    void devicesChanged();

    /// A device scan started, or the last one ended: see isScanning().
    void scanningChanged();

    /// The session of @p serial started; its file is open in a tab.
    void sessionStarted( const QString& serial );

    /// The session of @p serial stopped: by stopSession() or stopAll(), or
    /// on its own (adb exited, the file could not be written).
    void sessionStopped( const QString& serial );

    /// The session of @p serial goes on in a new file (a rotation).
    void sessionChanged( const QString& serial );

private Q_SLOTS:
    /** Take the result of a finished `adb devices` scan. */
    void onScanFinished( int exitCode, QProcess::ExitStatus exitStatus );

private:
    /** A session ended on its own (adb exited, or a write failed). */
    void onSessionFinished( const QString& serial );

    /** A session reported an error. */
    void onSessionError( const QString& serial, const QString& message );

    /** Store the result of a scan and announce it. */
    void setDevices( const QList<Device>& devices );

    /** Run the rescan requested during the scan that has just ended. */
    void onScanEnded();

    /**
     * Remove the session for @p serial from the running sessions and cut
     * its signals to this object.  The caller stops and deletes it.
     *
     * @return The session, or nullptr if there is none for @p serial.
     */
    AdbProcess* takeSession( const QString& serial );

    /** Whether a running session writes to the file at @p path. */
    bool isFileInUse( const QString& path ) const;

    // ── Device discovery ─────────────────────────────────────────────
    QProcess* scanProcess_ = nullptr; ///< Runs `adb devices`.
    QTimer* scanTimeout_ = nullptr;   ///< Gives up on a hanging scan.
    QList<Device> attached_;          ///< Result of the last scan.
    QStringList devices_;             ///< The ready devices of attached_.
    bool scanned_ = false;            ///< A scan has finished: its devices were logged.
    bool rescanPending_ = false;      ///< Scan again when the running scan ends.

    // ── Running sessions (serial → AdbProcess*) ─────────────────────
    QMap<QString, AdbProcess*> sessions_;
};

} // namespace logcat
