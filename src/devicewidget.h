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
 * @file devicewidget.h
 * @brief Non-modal QDialog for ADB device selection and logcat session control.
 *
 * The DeviceWidget is opened from the Plugins → Android Logcat menu entry.
 * It provides:
 *
 *   - A combo box listing discovered ADB devices
 *   - A "Refresh" button to re-scan for devices (in the background)
 *   - A "Start" button to begin logcat capture for the selected device
 *   - A "Stop" button to end the active session for the selected device
 *   - A "Stop All" button (shown when multiple sessions are active)
 *   - A "Save to file" checkbox with a file path selector
 *   - An ADB path display with a "Configure" button
 *   - A status label showing active session count
 *
 * Multiple devices can be captured simultaneously — each gets its own
 * AdbProcess, temp file, and LogSquirl tab.
 *
 * OWNERSHIP
 * ─────────
 * The dialog is created by logsquirl_plugin_init() and deleted by
 * logsquirl_plugin_shutdown().  Active AdbProcess instances are children
 * of the DeviceWidget and are cleaned up automatically.
 */

#pragma once

#include "adbprocess.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QProcess>
#include <QPushButton>
#include <QTimer>

namespace logcat {

/**
 * Non-modal dialog for managing ADB logcat sessions.
 *
 * When the user clicks "Start", an AdbProcess is created for the selected
 * device and the resulting temp file is opened in LogSquirl via the host API.
 */
class DeviceWidget : public QDialog {
    Q_OBJECT

public:
    explicit DeviceWidget( QWidget* parent = nullptr );
    ~DeviceWidget() override;

    /** Stop all active logcat sessions.
     *  @param cleanupTempFiles  If true, temporary log files are removed,
     *         including those of earlier rotations and of sessions that
     *         ended before (used during plugin shutdown).  If false, they are preserved
     *         so that already-open tabs can still display the data.
     */
    void stopAll( bool cleanupTempFiles = false );

    /** Number of currently running logcat sessions. */
    int activeSessionCount() const;

    /** Return the serials of all currently active sessions. */
    QStringList activeSerials() const;

    /**
     * Rotate the log for an active session: the old temp file is preserved
     * (tab stays open), a new temp file is created, and a new tab opens.
     *
     * @param serial  Device serial of the session to rotate.
     */
    void rotateSession( const QString& serial );

    /**
     * Start logcat capture for a specific device serial.
     *
     * @param serial    Device serial to capture.
     * @param savePath  Optional path to a .log file for persistent saving.
     *                  Refused if another active session writes to it.
     * @return true if the session started successfully, false otherwise.
     */
    bool startSession( const QString& serial, const QString& savePath = {} );

    /**
     * Stop logcat capture for a specific device serial.
     *
     * @param serial  Device serial of the session to stop.
     */
    void stopSession( const QString& serial );

    /**
     * Return the current line count for an active session.
     *
     * @param serial  Device serial to query.
     * @return Line count, or 0 if the session is not active.
     */
    qint64 sessionLineCount( const QString& serial ) const;

    /**
     * Check whether a session is currently running for the given serial.
     */
    bool isSessionActive( const QString& serial ) const;

    /** Serials of the devices found by the most recent scan. */
    const QStringList& devices() const
    {
        return devices_;
    }

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
    /** Emitted when a device scan has finished and devices() is updated. */
    void devicesChanged();

private Q_SLOTS:
    /** Take the result of a finished `adb devices` scan. */
    void onScanFinished( int exitCode, QProcess::ExitStatus exitStatus );

    /** Start logcat for the currently selected device. */
    void startCapture();

    /** Stop logcat for the currently selected device. */
    void stopCapture();

    /** Stop all active sessions. */
    void stopAllCaptures();

    /** Let the user browse for a save file path. */
    void browseSavePath();

    /** Open a dialog to configure the ADB executable path. */
    void configureAdbPath();

    /** Handle a logcat session ending (cleanup bookkeeping). */
    void onSessionFinished( const QString& serial );

    /** Handle a logcat session error. */
    void onSessionError( const QString& serial, const QString& message );

private:
    /** Update UI state (button enable/disable, status label, ADB path). */
    void updateUiState();

    /** Refill the device combo box from devices(), marking active sessions. */
    void updateDeviceCombo();

    /** Store the result of a scan and announce it. */
    void setDevices( const QStringList& devices );

    /** Run the rescan requested during the scan that has just ended. */
    void onScanEnded();

    /** Show on the Refresh button whether a scan is running. */
    void updateRefreshButton();

    /** Return the serial of the currently selected device, or empty string. */
    QString currentSerial() const;

    /**
     * Remove the session for @p serial from the active sessions and cut its
     * signals to this widget.  The caller stops and deletes it.
     *
     * @return The session, or nullptr if there is none for @p serial.
     */
    AdbProcess* takeSession( const QString& serial );

    /**
     * Keep the temporary files of a session that has ended for its tabs,
     * and remember them, so that stopAll( true ) removes them at shutdown.
     */
    void keepTempFiles( AdbProcess* proc );

    /** Whether an active session writes to the file at @p path. */
    bool isFileInUse( const QString& path ) const;

    // ── UI elements ──────────────────────────────────────────────────
    QComboBox* deviceCombo_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* stopAllButton_ = nullptr;
    QCheckBox* saveCheckBox_ = nullptr;
    QLineEdit* savePathEdit_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QLabel* adbPathLabel_ = nullptr;
    QPushButton* adbConfigButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    // ── Device discovery ─────────────────────────────────────────────
    QProcess* scanProcess_ = nullptr; ///< Runs `adb devices`.
    QTimer* scanTimeout_ = nullptr;   ///< Gives up on a hanging scan.
    QStringList devices_;             ///< Result of the last scan.
    bool rescanPending_ = false;      ///< Scan again when the running scan ends.

    // ── Active sessions (serial → AdbProcess*) ──────────────────────
    QMap<QString, AdbProcess*> sessions_;

    /// Temporary directories of ended sessions, kept for their tabs until shutdown.
    QStringList endedTempDirs_;
};

} // namespace logcat
