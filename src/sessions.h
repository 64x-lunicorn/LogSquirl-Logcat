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
 * @brief The logcat session of each device.
 *
 * Sessions starts an AdbProcess for a device (adbprocess.h), opens its file
 * in a followed tab, opens the next file in a new tab when the session is
 * rotated, and says through its signals when a session starts, stops or
 * changes.  A device runs one session at a time; several devices run side
 * by side, each with its own adb, file and tab.  The devices themselves
 * come from the device scan (devicescan.h).
 *
 * The dialog (devicewidget.h) and the sidebar (sidebarwidget.h) are views
 * of one Sessions: they show its sessions and call it to start, stop and
 * rotate them.
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
 * after stopAll() and after the views.  The running AdbProcesses are its
 * children.
 */

#pragma once

#include "adbprocess.h"

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

namespace logcat {

class Sessions : public QObject {
    Q_OBJECT

public:
    explicit Sessions( QObject* parent = nullptr );

    /// Sessions that still run are ended without signals and without
    /// keeping their temporary files: call stopAll() first.
    ~Sessions() override = default;

    Sessions( const Sessions& ) = delete;
    Sessions& operator=( const Sessions& ) = delete;

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

    /// What the session of @p serial has captured so far, read live; all
    /// zero (and a null start time) if it does not run.
    SessionCounters counters( const QString& serial ) const;

    /// The file the session of @p serial writes; empty if it does not run.
    QString filePath( const QString& serial ) const;

Q_SIGNALS:
    /// The session of @p serial started; its file is open in a tab.
    void sessionStarted( const QString& serial );

    /// The session of @p serial stopped: by stopSession() or stopAll(), or
    /// on its own (adb exited, the file could not be written).
    void sessionStopped( const QString& serial );

    /// The session of @p serial goes on in a new file (a rotation).
    void sessionChanged( const QString& serial );

    /// The session of @p serial ended on its own (adb exited, the file
    /// could not be written), after sessionStopped(): adb exits when its
    /// device goes away, so the devices are worth scanning again.
    void sessionEnded( const QString& serial );

private:
    /** A session ended on its own (adb exited, or a write failed). */
    void onSessionFinished( const QString& serial );

    /** A session reported an error. */
    void onSessionError( const QString& serial, const QString& message );

    /** Whether a running session writes to the file at @p path. */
    bool isFileInUse( const QString& path ) const;

    // ── Running sessions (serial → AdbProcess*) ─────────────────────
    QMap<QString, AdbProcess*> sessions_;
};

} // namespace logcat
