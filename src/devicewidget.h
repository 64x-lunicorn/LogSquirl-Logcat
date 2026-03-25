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
 *   - A "Refresh" button to re-scan for devices
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
#include <QPushButton>

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
    ~DeviceWidget() override = default;

    /** Stop all active logcat sessions.  Called during plugin shutdown. */
    void stopAll();

    /** Number of currently running logcat sessions. */
    int activeSessionCount() const;

  private Q_SLOTS:
    /** Re-scan for ADB devices and update the combo box. */
    void refreshDevices();

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

    /** Return the serial of the currently selected device, or empty string. */
    QString currentSerial() const;

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

    // ── Active sessions (serial → AdbProcess*) ──────────────────────
    QMap<QString, AdbProcess*> sessions_;
};

} // namespace logcat
