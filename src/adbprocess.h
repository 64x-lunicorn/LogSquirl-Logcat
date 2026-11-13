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
 * @file adbprocess.h
 * @brief ADB device discovery and per-device logcat process management.
 *
 * Each AdbProcess instance wraps a single `adb -s <serial> logcat` process.
 * Its output is written, line by line, to the Session's log file (logfile.h):
 * a temporary file or a user-chosen one, which LogSquirl opens with
 * follow/tail mode via the host API.
 *
 * THREAD MODEL
 * ────────────
 * AdbProcess objects live on the main thread.  QProcess signals
 * (readyReadStandardOutput, finished) are handled on the main thread's
 * event loop.  File I/O is synchronous but fast (line-buffered writes).
 *
 * USAGE
 * ─────
 *   auto* proc = new AdbProcess( settings.findAdb(), "SERIAL123", "/optional/save.log", parent );
 *   if ( proc->start() )              // launches `adb -s SERIAL123 logcat`
 *       qDebug() << proc->logFilePath(); // LogSquirl opens this file
 *   proc->stop();                     // ends adb, waits for exit
 */

#pragma once

#include "logfile.h"

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <limits>

namespace logcat {

/// What a logcat session has captured since start(), as AdbProcess::counters()
/// reads it.
struct SessionCounters {
    qint64 lines = 0;     ///< Lines written since start(), over all of its files.
    qint64 bytes = 0;     ///< Bytes written since start(), newlines included, over all files.
    qint64 fileBytes = 0; ///< Size of the current file, with what a save path held before.
    int rotations = 0;    ///< How often the log was rotated since start().
    /// Size of the current file at which it rotates next; 0: only when
    /// asked to (rotateLog()), as logcat has no size limit.
    qint64 nextRotationBytes = 0;
    QDateTime startTime; ///< When start() launched adb; null before.
};

/// A device as `adb devices -l` lists it (AdbProcess::parseDevices()).
struct Device {
    QString serial; ///< E.g. "emulator-5554", "3A1B2C3D4E5F" or "192.168.1.5:5555".
    /// As adb says it: "device" (connected, and USB debugging allowed),
    /// "unauthorized", "offline", "no permissions", "recovery", …
    QString state;
    /// E.g. "Pixel 8"; empty when adb names none (an unauthorized or offline
    /// device, or the short format of `adb devices`).
    QString model;

    /// Whether logcat can run on it: its state is "device".
    bool isReady() const
    {
        return state == QLatin1String( "device" );
    }

    bool operator==( const Device& other ) const
    {
        return serial == other.serial && state == other.state && model == other.model;
    }
    bool operator!=( const Device& other ) const
    {
        return !( *this == other );
    }
};

/**
 * Manages a single `adb logcat` process for one device.
 *
 * Captures stdout line-by-line and writes each line to the Session's
 * LogFile: the save path, when savePath is non-empty, or else a temporary
 * file in a directory named after the process (see tempdirs.h).
 *
 * The file path returned by logFilePath() is what the host opens
 * via open_file().
 */
class AdbProcess : public QObject {
    Q_OBJECT

public:
    /**
     * Construct a logcat session for one device.
     *
     * @param adb       The adb executable to run (Settings::findAdb()); empty
     *                  if there is none, which start() reports.
     * @param serial    ADB device serial (from `adb devices`).
     * @param savePath  Optional path to a .log file for persistent saving.
     *                  Pass an empty string to disable saving.
     * @param parent    QObject parent for memory management.
     */
    AdbProcess( const QString& adb, const QString& serial, const QString& savePath = {},
                QObject* parent = nullptr );

    /** Stops the process without emitting any signal. */
    ~AdbProcess() override;

    // ── Static helpers ───────────────────────────────────────────────

    /**
     * Parse the raw output of `adb devices` into a list of device serials.
     *
     * Each entry is the first column of the output, e.g. "emulator-5554"
     * or "R5CR10XXXXX".  Only devices with status "device" (not "offline"
     * or "unauthorized") are included.
     *
     * Extracted as a static helper so unit tests can exercise the parsing
     * logic without running a real ADB process.
     *
     * @param output  Raw stdout bytes from `adb devices` or `adb devices -l`.
     * @return List of serials with status "device".
     */
    static QStringList parseDeviceList( const QByteArray& output );

    /**
     * Parse the raw output of `adb devices -l` (or of `adb devices`) into
     * every device it lists, in any state, in its order.  The columns may
     * be separated by a tab (short format) or by spaces (long format); a
     * model is read from the long format's `model:` field, with spaces for
     * the underscores adb writes.
     *
     * @param output  Raw stdout bytes from `adb devices -l`.
     * @return The devices, in the order adb lists them.
     */
    static QList<Device> parseDevices( const QByteArray& output );

    /**
     * Remove every complete line from the front of @p buffer and return
     * the lines, without their "\n" or "\r\n" terminator.  An incomplete
     * last line stays in the buffer until more data arrives - unless it is
     * longer than @p maxLineBytes: then it is taken in lines of
     * @p maxLineBytes, so that the buffer never keeps more than that.
     *
     * Extracted as a static helper so unit tests can exercise the line
     * splitting without running a real ADB process.
     *
     * @param buffer        Bytes read from adb so far; complete lines are removed.
     * @param maxLineBytes  Most bytes of an incomplete line left in @p buffer.
     * @return The complete lines, in order.
     */
    static QList<QByteArray>
    takeLines( QByteArray& buffer, qsizetype maxLineBytes = std::numeric_limits<qsizetype>::max() );

    /// How long start() waits for adb to launch before giving up.
    static constexpr int kStartTimeoutMs = 5000;

    /// Most bytes of a line without a newline kept in memory: past it,
    /// adb's output is written out as a line, its stderr cut off.
    static constexpr qsizetype kMaxLineBytes = 1024 * 1024;

    /// Most characters of adb's stderr that go into one log message or
    /// error; a longer line is cut short with "…".
    static constexpr qsizetype kMaxMessageChars = 500;

    // ── Instance methods ─────────────────────────────────────────────

    /**
     * Open the log file and launch the logcat process.  No-op if already
     * running.
     *
     * A save path is appended to, so an earlier capture in that file is
     * never overwritten.
     *
     * On failure the reason has been emitted through errorOccurred(), and
     * a log file that start() created is removed again.
     *
     * @return true if adb is running, false if the session did not start.
     */
    bool start();

    /**
     * Stop the logcat process: SIGTERM on Unix, kill on Windows (where
     * adb, a console program, ignores terminate()).  Waits at most two
     * seconds.  No-op if not running, or if a stop has given up on adb
     * already.
     */
    void stop();

    /**
     * End the session, however it ends (Stop, Stop All, adb exiting on its
     * own): its signals are cut first, so that stopping it calls back
     * no-one; then adb is stopped as stop() does; its files are kept for
     * their tabs (LogFile::keepTempDir(): the plugin removes the temporary
     * ones when LogSquirl quits, tempdirs.h); and it is deleted later.
     *
     * Nothing but its counters() and logFilePath() may be read afterwards,
     * and only until control returns to the event loop.
     */
    void end();

    /**
     * End each of @p sessions as end() does, together: every adb is told
     * to end before any is waited for, and all are waited for against one
     * shared deadline, so that ending many takes no longer than ending
     * one - at most two seconds.
     */
    static void endAll( const QList<AdbProcess*>& sessions );

    /**
     * Rotate the log file (LogFile::rotate()): close the current file and
     * open a new one next to it.  The old file stays on disk, and end()
     * keeps it, so the existing LogSquirl tab keeps its content.  New
     * logcat output is redirected to the new file.
     *
     * If the new file cannot be created, errorOccurred() is emitted and
     * the capture continues in the old file; if that cannot be reopened
     * either, the session is stopped (finished() is emitted).
     *
     * @return Absolute path to the new file, or empty on failure.
     */
    QString rotateLog();

    /** Whether the underlying QProcess is currently running. */
    bool isRunning() const;

    /** The device serial this session is attached to. */
    const QString& serial() const
    {
        return serial_;
    }

    /**
     * Absolute path to the file the session writes now: the save path
     * (as expanded, ~ for the home directory) or the temporary file, or
     * after a Rotate the file it moved to.  This is the file that will be
     * opened in LogSquirl via host->open_file().  Empty before start(),
     * and after a start() that failed.
     */
    QString logFilePath() const
    {
        return logFile_.path();
    }

    /** What the session has captured since start(), read live: counters().lines
     *  counts the lines of the whole session, over every file. */
    SessionCounters counters() const
    {
        return counters_;
    }

Q_SIGNALS:
    /** Emitted when the logcat process has started successfully. */
    void started();

    /** Emitted when the logcat process exits (normally or on error). */
    void finished( int exitCode );

    /**
     * Emitted when an error occurs (ADB not found, process crash, adb
     * exiting with an error, …).  Not emitted for the exit that stop()
     * causes.
     */
    void errorOccurred( const QString& message );

private Q_SLOTS:
    /** Handle new data available on stdout. */
    void onReadyRead();

    /** Forward adb's stderr to the host log. */
    void onReadyReadStandardError();

    /** Handle process exit. */
    void onFinished( int exitCode, QProcess::ExitStatus exitStatus );

    /** Handle process error. */
    void onErrorOccurred( QProcess::ProcessError error );

private:
    /**
     * Stop the logcat processes of @p sessions together, as stop() does
     * one, against one shared deadline (see endAll()).
     */
    static void stopAll( const QList<AdbProcess*>& sessions );

    /**
     * End the adb of every one of @p sessions: SIGTERM on Unix, kill on
     * Windows, then kill those left after a second.  Waits at most two
     * seconds for all of them together; the exits are not reported as
     * errors.
     */
    static void endProcesses( const QList<AdbProcess*>& sessions );

    /// How long endProcesses() waits after it signals the processes, each time.
    static constexpr int kStopStepMs = 1000;

    /// Write @p line and a newline to the log file and count it; false if
    /// it cannot be written (see failWrite()).
    bool writeLine( const QByteArray& line );
    /// Flush the log file; false if it cannot be written (see failWrite()).
    bool flushLogFile();
    /// The log file cannot be written (disk full, volume gone): say so once
    /// and stop the session, rather than drop every line from here on.
    void failWrite();

    /** Write out a buffered partial line, e.g. before the file is closed. */
    void flushPartialLine();

    QString adb_; ///< The adb executable; empty if there is none.
    QString serial_;

    /// adb.  A child of this object - unless it outlives it: see ~AdbProcess().
    QProcess* process_;
    LogFile logFile_;         ///< Where adb's output goes.
    QByteArray readBuffer_;   ///< Accumulates partial lines from stdout.
    QByteArray stderrBuffer_; ///< Accumulates partial lines from stderr, cut at kMaxLineBytes.
    QString lastStderrLine_;  ///< Last line adb wrote to stderr.
    /// Since start(), over every file of the session; counters_.rotations
    /// counts rotateLog().
    SessionCounters counters_;
    bool stopping_ = false; ///< True while stop() ends the process.
};

} // namespace logcat
