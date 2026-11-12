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
 * @file sidebarwidget.h
 * @brief The plugin's tab "Logcat" in LogSquirl's sidebar: one tile per device.
 *
 *   [Settings…]                    [■ Stop all]
 *   ┌ tile: Pixel 8 · 3A1B… ────────────────┐   connected or running
 *   └───────────────────────────────────────┘
 *   ┌ tile: Galaxy S23 · R5CR… ─────────────┐   unplugged: greyed, Forget
 *   └───────────────────────────────────────┘
 *
 * One tile (devicetile.h) per device: each device adb lists, in any state,
 * and each device seen before (devicestore.h), which stays while it is
 * unplugged — greyed out, Start disabled, with Forget — until it is
 * forgotten; also a device that runs a session adb no longer lists.
 * Devices seen before come first, in the order they were first seen.  A
 * tile is titled by the device's model and serial (deviceTitle()) and says
 * whether the device is connected, running, unauthorized, offline or
 * disconnected, and where it saves.
 *
 * Start runs logcat for the device (sessions.h), followed in a new tab,
 * writing to the device's own save path or else to a new file in the log
 * directory; Stop ends it, its tab keeping the file; ↻ Rotate goes on in a
 * new file and tab.  Several devices run at once.  Save to… on a tile sets
 * the device's own save path; Settings… opens the settings dialog
 * (settingsdialog.h): the log directory and the ADB path.
 *
 * The devices are rescanned every kScanIntervalMs, so that a device
 * plugged in or unplugged shows by itself; a scan or a session that starts,
 * stops or rotates updates the tiles at once.
 *
 * Children named for tests: "settings", "stopAll", "empty" (the hint shown
 * without a tile), "deviceScan" (the rescan timer), the tiles ("tile"), and
 * while it is open the dialog of a tile's Save to… ("savePathDialog", with
 * the line edit "savePath").
 *
 * OWNERSHIP
 * ─────────
 * Created by logsquirl_plugin_init() and registered with
 * register_sidebar_tab; the host parents it.  logsquirl_plugin_shutdown()
 * unregisters and deletes it, before the sessions and the store it shows.
 */

#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace logcat {

class DeviceStore;
class DeviceTile;
class Sessions;
struct Device;
struct KnownDevice;

/**
 * The title of a device's tile: "<model> · <serial>", e.g.
 * "Pixel 8 · 3A1B…".  A serial longer than 8 characters is cut after its
 * first 4 (the summary shows it whole); without a model, the serial alone,
 * whole.
 */
QString deviceTitle( const QString& serial, const QString& model );

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    /// How often the devices are rescanned, in milliseconds.
    static constexpr int kScanIntervalMs = 3000;

    /// Shows the devices of @p store and @p sessions, and the sessions of
    /// @p sessions, which must both outlive it.
    SidebarWidget( DeviceStore* store, Sessions* sessions, QWidget* parent = nullptr );

    /// Show the devices and sessions as they are now.
    void reload();

private:
    /// Start logcat for @p serial, if it is ready.
    void start( const QString& serial );

    /// Open the dialog that sets the save path of @p serial.
    void editSavePath( const QString& serial );

    /// Open the settings dialog.
    void openSettings();

    /// Remember the attached devices, and show them.
    void onDevicesChanged();

    /// The tile of @p serial, created (and connected) if it has none.
    DeviceTile* tileFor( const QString& serial );

    /// Show on @p tile what its device is and does; @p attached is how adb
    /// lists it (nullptr: not), @p known how the store remembers it.
    void updateTile( DeviceTile* tile, const Device* attached, const KnownDevice* known );

    DeviceStore* store_;
    Sessions* sessions_;
    QPushButton* stopAll_;
    QLabel* empty_;
    QVBoxLayout* tiles_;
    QTimer* scanTimer_;
    QHash<QString, DeviceTile*> tileBySerial_;
};

} // namespace logcat
