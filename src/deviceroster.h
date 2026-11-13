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
 * @file deviceroster.h
 * @brief What the sidebar's tiles show: one row per device, from the devices
 * seen before, those attached now and the running sessions.
 *
 *   known (logcat.ini) ─┐
 *   attached (scan) ────┼─▶ deviceRoster() ─▶ rows: serial, title, state,
 *   running sessions ───┤                          summary, details, lines/s
 *   previous samples ───┘                     samples for the next call
 *
 * One row per device: each device seen before (KnownDevice), in the order
 * the store keeps them; then each device adb lists, in any state; then each
 * device that runs a session adb no longer lists.  A row says what its tile
 * shows: its title (deviceTitle()), its TileState, its summary — the serial
 * and the state in words, and where it saves or writes — and, while its
 * session runs, the session's details (sessionDetails()) with the lines per
 * second.
 *
 * The lines per second are measured between two polls of the counters: a
 * call that polls (pollAtMs given) samples each running session's lines and
 * measures the rate since the sample before; a call that does not keeps the
 * rates of the last poll.  The first poll of a session measures 0.  A
 * session that stopped loses its sample: started again, it counts anew.
 *
 * A pure function of plain values: no widget, timer or process.  The
 * sidebar (sidebarwidget.h) gathers the values, keeps the samples between
 * calls, and maps the rows onto its tiles.
 */

#pragma once

#include "adbprocess.h"  // Device, SessionCounters (the plain values only)
#include "devicestore.h" // KnownDevice (the plain value only)
#include "tilestate.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace logcat {

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
 * @p linesPerSecond is the measured rate.  "rotates on request": a logcat
 * session rotates only on ↻ Rotate; a session that rotated at a size
 * (SessionCounters::nextRotationBytes) would say "rotates at 10.0 MB".
 */
QStringList sessionDetails( const SessionCounters& counters, qint64 linesPerSecond,
                            const QDateTime& now );

/// A running session, as the roster needs it.
struct RunningSession {
    QString serial;
    QString filePath;         ///< The file it writes now.
    SessionCounters counters; ///< What it has captured so far.
};

/// A running session's lines at a poll, to measure its rate by.
struct CounterSample {
    qint64 lines = 0; ///< Its lines at that poll.
    qint64 atMs = 0;  ///< When that poll was, on the caller's monotonic clock.
    qint64 rate = 0;  ///< Lines per second between that poll and the one before.
};

/// What one tile shows.
struct TileRow {
    QString serial;
    QString title;
    TileState state = TileState::Disconnected;
    QStringList summary;       ///< The device, its state, where it saves or writes.
    QStringList details;       ///< The running session's details; empty: not running.
    qint64 linesPerSecond = 0; ///< The running session's rate; 0: not running.
};

/// What deviceRoster() returns.
struct Roster {
    QList<TileRow> rows;                   ///< One per tile, in the order shown.
    QHash<QString, CounterSample> samples; ///< By serial; pass to the next call.
};

/**
 * The tiles of the devices @p known (seen before), @p attached (adb lists
 * them) and @p running (their sessions run), at @p now.
 *
 * @p previous are the samples the call before returned.  @p pollAtMs, the
 * time of a poll on the caller's monotonic clock, samples the running
 * sessions' lines anew and measures their rates; without it the rates are
 * those of @p previous.
 */
Roster deviceRoster( const QList<KnownDevice>& known, const QList<Device>& attached,
                     const QList<RunningSession>& running,
                     const QHash<QString, CounterSample>& previous, const QDateTime& now,
                     std::optional<qint64> pollAtMs = std::nullopt );

} // namespace logcat
