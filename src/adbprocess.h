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
 *   proc->start();                    // launches `adb -s SERIAL123 logcat`
 *   qDebug() << proc->tempFilePath(); // LogSquirl opens this file
 *   proc->stop();                     // sends SIGTERM, waits for exit
 */

#pragma once

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
 *   1. A temporary file inside a QTemporaryDir (always).
 *   2. A user-selected log file (optional, if savePath is non-empty).
 *
 * The temporary file path is what the host opens via open_file().
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
    explicit AdbProcess( const QString& serial,
                         const QString& savePath = {},
                         QObject* parent = nullptr );
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
     * Run `adb devices` and return a list of attached device serials.
     *
     * Each entry is the first column from `adb devices` output, e.g.
     * "emulator-5554" or "R5CR10XXXXX".  Only devices with status
     * "device" (not "offline" or "unauthorized") are included.
     *
     * @return List of device serial strings, possibly empty.
     */
    static QStringList discoverDevices();

    /**
     * Parse the raw output of `adb devices` into a list of device serials.
     *
     * Extracted as a static helper so unit tests can exercise the parsing
     * logic without running a real ADB process.
     *
     * @param output  Raw stdout bytes from `adb devices`.
     * @return List of serials with status "device".
     */
    static QStringList parseDeviceList( const QByteArray& output );

    /**
     * Return the plugin's config directory from the host API.
     * Falls back to a temp path if the plugin is not initialised.
     */
    static QString configDir();

    // ── Instance methods ─────────────────────────────────────────────

    /** Start the logcat process.  No-op if already running. */
    void start();

    /** Stop the logcat process (SIGTERM).  No-op if not running. */
    void stop();

    /**
     * Prevent the temporary log file from being deleted when this
     * object is destroyed.  Call before deleteLater() so that the
     * LogSquirl tab can keep displaying the captured output.
     */
    void preserveTempFile();

    /**
     * Rotate the log file: close the current temp file and open a new
     * one in the same temp directory.  The old file is preserved so the
     * existing LogSquirl tab keeps its content.  New logcat output is
     * redirected to the new file.
     *
     * @return Absolute path to the new temp file, or empty on failure.
     */
    QString rotateLog();

    /** Whether the underlying QProcess is currently running. */
    bool isRunning() const;

    /** The device serial this session is attached to. */
    const QString& serial() const { return serial_; }

    /**
     * Absolute path to the temporary log file.
     *
     * This is the file that will be opened in LogSquirl via
     * host->open_file().  It is only valid after start() is called.
     */
    QString tempFilePath() const;

    /** Total number of lines captured so far. */
    qint64 lineCount() const { return lineCount_; }

  Q_SIGNALS:
    /** Emitted when the logcat process has started successfully. */
    void started();

    /** Emitted when the logcat process exits (normally or on error). */
    void finished( int exitCode );

    /** Emitted when an error occurs (ADB not found, process crash, …). */
    void errorOccurred( const QString& message );

  private Q_SLOTS:
    /** Handle new data available on stdout. */
    void onReadyRead();

    /** Handle process exit. */
    void onFinished( int exitCode, QProcess::ExitStatus exitStatus );

    /** Handle process error. */
    void onErrorOccurred( QProcess::ProcessError error );

  private:
    QString serial_;
    QString savePath_;

    QProcess process_;
    QTemporaryDir tempDir_;
    QFile tempFile_;
    QFile saveFile_;
    QByteArray readBuffer_;  ///< Accumulates partial lines from stdout.
    qint64 lineCount_ = 0;
    int rotationCount_ = 0;  ///< Incremented on each rotateLog() call.
};

} // namespace logcat
