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
 * One tile (devicetile.h) per row of the device roster (deviceroster.h):
 * each device adb lists, in any state, and each device seen before
 * (devicestore.h), which stays while it is unplugged — greyed out, Start
 * disabled, with Forget — until it is forgotten; also a device that runs a
 * session adb no longer lists.  The roster decides what each tile shows —
 * its title, state, summary, details and rate; the tab only gathers what
 * it needs, and adds, updates, reorders and removes the tiles to match.
 *
 * Start runs logcat for the device (sessions.h), followed in a new tab,
 * writing to the device's own save path or else to a new file in the log
 * directory; Stop ends it, its tab keeping the file; ↻ Rotate goes on in a
 * new file and tab.  Several devices run at once.  Save to… on a tile sets
 * the device's own save path; Settings… opens the settings dialog
 * (settingsdialog.h): the log directory and the ADB path.
 *
 * While the tab shows, the devices are rescanned every kScanIntervalMs, so
 * that a device plugged in or unplugged shows by itself; they are scanned
 * once right as it is shown, and not while it is hidden.  A scan or a
 * session that starts, stops or rotates updates the tiles at once.
 *
 * A running tile shows its session's details (sessionDetails()).  A
 * session's lines emit no signal, so the counters are polled every
 * kCounterPollMs (pollCounters()) while the tab shows, and right as it is
 * shown, the lines per second measured between two polls, as
 * LogSquirl-NATS's tiles do.  A stopped tile shows only the device.
 *
 * Children named for tests: "settings", "stopAll", "empty" (the hint shown
 * without a tile), "deviceScan" (the rescan timer), "counterPoll" (the
 * counters' timer), the tiles ("tile"), and
 * while it is open the dialog of a tile's Save to… ("savePathDialog", with
 * the line edit "savePath").
 *
 * OWNERSHIP
 * ─────────
 * Created by logsquirl_plugin_init() and registered with
 * register_sidebar_tab; the host parents it.  logsquirl_plugin_shutdown()
 * unregisters and deletes it, before the scan, the sessions and the store
 * it shows.
 */

#pragma once

#include "deviceroster.h"

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QWidget>

#include <optional>

class QHideEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QTimer;
class QVBoxLayout;

namespace logcat {

class DeviceScan;
class DeviceStore;
class DeviceTile;
class Sessions;

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    /// How often the devices are rescanned, in milliseconds.
    static constexpr int kScanIntervalMs = 3000;

    /// How often a running tile's counters are polled, in milliseconds.
    static constexpr int kCounterPollMs = 1000;

    /// Shows the devices of @p store and @p scan, and the sessions of
    /// @p sessions, which must all outlive it.
    SidebarWidget( DeviceStore* store, DeviceScan* scan, Sessions* sessions,
                   QWidget* parent = nullptr );

    /// Show the devices and sessions as they are now.
    void reload();

    /**
     * Read the counters of the running sessions, and show them on their
     * tiles, with the lines per second since the poll before (0 at the
     * first poll of a session).  The timer "counterPoll" calls it every
     * kCounterPollMs; tests call it themselves.
     */
    void pollCounters();

protected:
    /// Scan the devices and poll the counters at once, then start the
    /// timers that do it again.
    void showEvent( QShowEvent* event ) override;

    /// Stop the timers: a hidden tab scans and polls nothing.
    void hideEvent( QHideEvent* event ) override;

private:
    /// Show the rows of the device roster (deviceroster.h) as they are now
    /// on the tiles; @p pollAtMs (on clock_) also polls the counters.
    void showRoster( std::optional<qint64> pollAtMs );

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

    DeviceStore* store_;
    DeviceScan* scan_;
    Sessions* sessions_;
    QPushButton* stopAll_;
    QLabel* empty_;
    QVBoxLayout* tiles_;
    QTimer* scanTimer_;
    QTimer* pollTimer_;
    QHash<QString, DeviceTile*> tileBySerial_;
    /// The running sessions' samples, by serial, as the roster last returned.
    QHash<QString, CounterSample> samples_;
    /// The monotonic clock the polls are timed on; started at construction.
    QElapsedTimer clock_;
};

} // namespace logcat
