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
 * Output is written to a temporary file (and optionally a user-chosen file)
 * so that LogSquirl can open it with follow/tail mode via the host API.
 *
 * THREAD MODEL
 * ────────────
 * AdbProcess objects live on the main thread.  QProcess signals
 * (readyReadStandardOutput, finished) are handled on the main thread's
 * event loop.  File I/O is synchronous but fast (line-buffered writes).
 *
 * USAGE
 * ─────
 *   auto* proc = new AdbProcess( "SERIAL123", "/optional/save.log", parent );
 *   if ( proc->start() )              // launches `adb -s SERIAL123 logcat`
 *       qDebug() << proc->tempFilePath(); // LogSquirl opens this file
 *   proc->stop();                     // ends adb, waits for exit
 */

#pragma once

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace logcat {

/**
 * Manages a single `adb logcat` process for one device.
 *
 * Captures stdout line-by-line and writes each line to:
 *   - The user-configured log directory file, when savePath is non-empty.
 *   - A temporary file inside a QTemporaryDir, when savePath is empty,
 *     named after the process (see tempdirs.h).
 *
 * When a savePath is provided the temporary directory is not used,
 * avoiding orphaned temp files that fill up disk space.
 *
 * The file path returned by tempFilePath() is what the host opens
 * via open_file().
 */
class AdbProcess : public QObject {
    Q_OBJECT

public:
    /**
     * Construct a logcat session for one device.
     *
     * @param serial    ADB device serial (from `adb devices`).
     * @param savePath  Optional path to a .log file for persistent saving.
     *                  Pass an empty string to disable saving.
     * @param parent    QObject parent for memory management.
     */
    explicit AdbProcess( const QString& serial, const QString& savePath = {},
                         QObject* parent = nullptr );

    /** Stops the process without emitting any signal. */
    ~AdbProcess() override;

    // ── Static helpers ───────────────────────────────────────────────

    /**
     * Auto-detect the ADB executable path.
     *
     * Search order:
     *   1. User override from plugin config (logcat.ini → adb/path)
     *   2. ANDROID_HOME/platform-tools/adb
     *   3. ANDROID_SDK_ROOT/platform-tools/adb  (legacy env var)
     *   4. System PATH lookup
     *
     * @return Absolute path to `adb`, or empty string if not found.
     */
    static QString findAdb();

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
     * @param output  Raw stdout bytes from `adb devices`.
     * @return List of serials with status "device".
     */
    static QStringList parseDeviceList( const QByteArray& output );

    /**
     * Remove every complete line from the front of @p buffer and return
     * the lines, without their "\n" or "\r\n" terminator.  An incomplete
     * last line stays in the buffer until more data arrives.
     *
     * Extracted as a static helper so unit tests can exercise the line
     * splitting without running a real ADB process.
     *
     * @param buffer  Bytes read from adb so far; complete lines are removed.
     * @return The complete lines, in order.
     */
    static QList<QByteArray> takeLines( QByteArray& buffer );

    /**
     * Return a path for a new log file in @p dir, named
     * `<yyyy-MM-dd_HHmmss>_<serial>.log`.  Characters of the serial that
     * are not valid in file names (the ':' of a wireless device's
     * "192.168.1.5:5555") are replaced by '_'.  If that file exists, a
     * number is appended (`…_2.log`, `…_3.log`, …), so that two captures
     * within the same second never share a file.
     *
     * @param dir        Directory the file will be created in.
     * @param serial     ADB device serial.
     * @param timestamp  Time the capture starts.
     * @return Absolute path of a file that does not exist yet.
     */
    static QString generateLogPath( const QString& dir, const QString& serial,
                                    const QDateTime& timestamp = QDateTime::currentDateTime() );

    /**
     * Return the plugin's config directory from the host API.
     * Falls back to a temp path if the plugin is not initialised.
     */
    static QString configDir();

    /// How long start() waits for adb to launch before giving up.
    static constexpr int kStartTimeoutMs = 5000;

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
     * seconds.  No-op if not running.
     */
    void stop();

    /**
     * Prevent the temporary log file from being deleted when this
     * object is destroyed.  Call before deleteLater() so that the
     * LogSquirl tab can keep displaying the captured output.
     *
     * @return The temporary directory left on disk, or empty when the
     *         session writes to a save path (nothing to preserve).
     */
    QString preserveTempFile();

    /**
     * Rotate the log file: close the current temp file and open a new
     * one in the same temp directory.  The old file is preserved so the
     * existing LogSquirl tab keeps its content.  New logcat output is
     * redirected to the new file.
     *
     * If the new file cannot be created, errorOccurred() is emitted and
     * the capture continues in the old file; if that cannot be reopened
     * either, the session is stopped (finished() is emitted).
     *
     * @return Absolute path to the new temp file, or empty on failure.
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
     * Absolute path to the log file (save path or temp file).
     *
     * Returns the user-configured save path when one was provided,
     * otherwise the temporary file path.  This is the file that will
     * be opened in LogSquirl via host->open_file().  It is only valid
     * after start() is called.
     */
    QString tempFilePath() const;

    /** Whether the session writes directly to a user-specified save path. */
    bool isUsingSavePath() const
    {
        return usingSavePath_;
    }

    /** Total number of lines captured so far. */
    qint64 lineCount() const
    {
        return lineCount_;
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
     * End adb: SIGTERM on Unix, kill on Windows, then kill after a second.
     * Waits at most two seconds; the exit is not reported as an error.
     */
    void endProcess();

    /** Write one line to the log file, terminated by "\n". */
    void writeLine( const QByteArray& line );

    /** Write out a buffered partial line, e.g. before the file is closed. */
    void flushPartialLine();

    /** Close the log file after a failed start; remove it if start() created it. */
    void discardLogFile();

    QString serial_;
    QString savePath_;

    QProcess process_;
    QTemporaryDir tempDir_;
    QFile tempFile_;
    QByteArray readBuffer_;   ///< Accumulates partial lines from stdout.
    QByteArray stderrBuffer_; ///< Accumulates partial lines from stderr.
    QString lastStderrLine_;  ///< Last line adb wrote to stderr.
    qint64 lineCount_ = 0;
    int rotationCount_ = 0;       ///< Incremented on each rotateLog() call.
    bool usingSavePath_ = false;  ///< True when writing directly to the log directory.
    bool createdLogFile_ = false; ///< True when start() created the log file.
    bool stopping_ = false;       ///< True while stop() ends the process.
};

} // namespace logcat
