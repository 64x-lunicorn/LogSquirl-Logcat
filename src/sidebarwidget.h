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
 * A running tile shows its session's details (sessionDetails()): running
 * time and lines per second, lines and bytes, the current file's size and
 * the rotations so far, and when it rotates next — on request, as logcat
 * has no size limit.  A session's lines emit no signal, so the counters are
 * polled every kCounterPollMs (pollCounters()), the lines per second
 * measured between two polls, as LogSquirl-NATS's tiles do.  A stopped tile
 * shows only the device.
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
 * unregisters and deletes it, before the sessions and the store it shows.
 */

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>

class QDateTime;
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
struct SessionCounters;

/**
 * The title of a device's tile: "<model> · <serial>", e.g.
 * "Pixel 8 · 3A1B…".  A serial longer than 8 characters is cut after its
 * first 4 (the summary shows it whole); without a model, the serial alone,
 * whole.
 */
QString deviceTitle( const QString& serial, const QString& model );

/// @p count with its thousands separated by commas, e.g. "12,345".
QString formatCount( qint64 count );

/// @p bytes in bytes below 1 KB ("512 B"), else to a tenth of the largest
/// unit of 1024 it reaches, up to TB: "1.0 KB", "1.2 MB".
QString formatSize( qint64 bytes );

/// @p seconds as "m:ss", or "h:mm:ss" from an hour on: "0:05", "1:02:03".
/// Less than 0 is "0:00".
QString formatDuration( qint64 seconds );

/// How many per second @p count in @p elapsedMs milliseconds is, rounded
/// down; 0 for a count below 0 or no time.
qint64 ratePerSecond( qint64 count, qint64 elapsedMs );

/**
 * The details of a running session for its tile, at @p now:
 *
 *   running 1:02:03 · 120 lines/s     time since start, lines per second
 *   12,345 lines · 1.2 MB             since start, over all its files
 *   file 512.0 KB · 2 rotations       the current file, rotations so far
 *   rotates on request                when it rotates next
 *
 * @p linesPerSecond is the rate the caller measured.  "rotates on request":
 * a logcat session rotates only on ↻ Rotate; a session that rotated at a
 * size (SessionCounters::nextRotationBytes) would say "rotates at 10.0 MB".
 */
QStringList sessionDetails( const SessionCounters& counters, qint64 linesPerSecond,
                            const QDateTime& now );

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    /// How often the devices are rescanned, in milliseconds.
    static constexpr int kScanIntervalMs = 3000;

    /// How often a running tile's counters are polled, in milliseconds.
    static constexpr int kCounterPollMs = 1000;

    /// Shows the devices of @p store and @p sessions, and the sessions of
    /// @p sessions, which must both outlive it.
    SidebarWidget( DeviceStore* store, Sessions* sessions, QWidget* parent = nullptr );

    /// Show the devices and sessions as they are now.
    void reload();

    /**
     * Read the counters of the running sessions, and show them on their
     * tiles, with the lines per second since the poll before (0 at the
     * first poll of a session).  The timer "counterPoll" calls it every
     * kCounterPollMs; tests call it themselves.
     */
    void pollCounters();

private:
    /// The lines of a running session at the poll before, to measure its
    /// rate by.
    struct Sample {
        qint64 lines = 0;    ///< Its lines at that poll.
        qint64 rate = 0;     ///< Lines per second between that poll and the one before.
        QElapsedTimer since; ///< Started at that poll.
    };

    /// Show on @p tile the details of its session, if it runs, else none.
    void updateDetails( DeviceTile* tile );

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
    QTimer* pollTimer_;
    QHash<QString, DeviceTile*> tileBySerial_;
    /// The running sessions' samples, by serial.
    QHash<QString, Sample> samples_;
};

} // namespace logcat
