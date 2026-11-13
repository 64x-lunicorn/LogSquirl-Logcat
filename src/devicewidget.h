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
 * It is a view of the plugin's DeviceScan (devicescan.h), the devices, and
 * of its Sessions (sessions.h), the logcat sessions.  It provides:
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
 * OWNERSHIP
 * ─────────
 * The dialog is created by logsquirl_plugin_init() and deleted by
 * logsquirl_plugin_shutdown(), before the scan and the Sessions it shows.
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace logcat {

class DeviceScan;
class Sessions;
class Settings;

/**
 * Non-modal dialog for managing ADB logcat sessions.
 *
 * When the user clicks "Start", the Sessions start a session for the
 * selected device, whose file opens in a LogSquirl tab.
 */
class DeviceWidget : public QDialog {
    Q_OBJECT

public:
    /**
     * @param settings  The ADB path the dialog shows and sets.  Must
     *                  outlive the dialog.
     * @param scan      The devices the dialog shows.  Must outlive the
     *                  dialog.
     * @param sessions  The sessions the dialog shows.  Must outlive the
     *                  dialog.
     * @param parent    QWidget parent.
     */
    DeviceWidget( Settings* settings, DeviceScan* scan, Sessions* sessions,
                  QWidget* parent = nullptr );

    /**
     * Ask for the ADB executable path (Configure button, and Plugins →
     * Configure) and save it in the settings, which has the devices
     * rescanned with it (Settings::adbPathChanged()).
     *
     * @param parent  Parent of the input dialog.
     */
    void configureAdbPath( QWidget* parent );

private Q_SLOTS:
    /** Start logcat for the currently selected device. */
    void startCapture();

    /** Stop logcat for the currently selected device. */
    void stopCapture();

    /** Stop all active sessions. */
    void stopAllCaptures();

    /** Let the user browse for a save file path. */
    void browseSavePath();

private:
    /** Update UI state (button enable/disable, status label, ADB path). */
    void updateUiState();

    /** Refill the device combo box from the devices, marking active sessions. */
    void updateDeviceCombo();

    /** Show on the Refresh button whether a scan is running. */
    void updateRefreshButton();

    /** Return the serial of the currently selected device, or empty string. */
    QString currentSerial() const;

    Settings* settings_ = nullptr;
    DeviceScan* scan_ = nullptr;
    Sessions* sessions_ = nullptr;

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
};

} // namespace logcat
