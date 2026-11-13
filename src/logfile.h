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
 * @file logfile.h
 * @brief The file a logcat Session writes: opening, writing, Rotate, its temp dir.
 *
 * A Session writes either to a save path the user chose, which is appended
 * to, or to a new file in a temporary directory of its own (tempdirs.h).
 * LogFile opens only a full path that is a regular file, notices a write
 * that fails, rotates to a new file, and keeps or releases the temporary
 * directory.  It knows nothing of adb: AdbProcess hands it the lines.
 *
 * USAGE
 * ─────
 *   LogFile file( "SERIAL123", "/optional/save.log" );
 *   QString error;
 *   if ( !file.open( error ) ) … // error says why, for the user
 *   file.writeLine( "a line" ); file.flush();
 *   file.rotate( error );       // the capture goes on in a new file
 *   file.close();
 *   file.keepTempDir();         // the tabs keep their files
 */

#pragma once

#include <QDateTime>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <memory>

namespace logcat {

class LogFile {
public:
    /**
     * @param serial    ADB device serial: it names the files.
     * @param savePath  The file to append to, as the user typed it (a
     *                  leading ~ is the home directory); empty for a new
     *                  file in a temporary directory.
     * @param tempRoot  Where the temporary directory is created (tempdirs.h);
     *                  empty for tempRoot().
     */
    explicit LogFile( const QString& serial, const QString& savePath = {},
                      const QString& tempRoot = {} );

    /// Closes the file; removes the temporary directory unless it was kept.
    ~LogFile();

    LogFile( const LogFile& ) = delete;
    LogFile& operator=( const LogFile& ) = delete;

    /**
     * The path a user typed, made absolute: a leading `~` is the home
     * directory, and the path is cleaned.  Empty for a path that is still
     * relative, as `open_file()` takes absolute paths only and LogSquirl's
     * working directory is nothing the user chose (`/` when started from
     * the Finder or the Dock).
     */
    static QString absoluteUserPath( const QString& path );

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

    /// Why the save path cannot be opened before anything is tried (it is
    /// not a full path), for the user; empty when it may be.
    QString pathError() const;

    /**
     * Open the file: the save path, appended to (its directory is created
     * if need be), or a new file `logcat_<serial>.log` in the temporary
     * directory.  Only a regular file is opened: a FIFO would block the UI
     * thread, a symlink lead elsewhere.  A file it creates is readable by
     * the user only.
     *
     * @param error  Why it failed, for the user.
     * @return Whether the file is open.
     */
    bool open( QString& error );

    /// Close the file; it stays on disk.
    void close();

    /// Close the file after a failed start, and remove it if open() created it.
    void discard();

    /// Whether a file is open.
    bool isOpen() const
    {
        return file_.isOpen();
    }

    /**
     * Write @p line and a newline.  False if it cannot be written (disk
     * full, volume gone): failed() is set then, and every write after it
     * fails as well, rather than leave a gap in the capture.
     */
    bool writeLine( const QByteArray& line );

    /// Flush what was written; false, setting failed(), if it cannot be.
    bool flush();

    /// Whether a write has failed since open().
    bool failed() const
    {
        return !failure_.isEmpty();
    }

    /// Why the write failed, naming the file, for the user; empty if none has.
    const QString& failure() const
    {
        return failure_;
    }

    /// What became of a rotate().
    enum class Rotation {
        Rotated,     ///< The capture goes on in the new file.
        KeptOldFile, ///< No new file; the capture goes on in the old one.
        Closed,      ///< Neither file could be opened: no file is open.
    };

    /**
     * Close the file and open a new one: a generated name (generateLogPath())
     * next to the save path, or `logcat_<serial>_<n>.log` in the temporary
     * directory.  The old file stays on disk, for the tab that shows it.
     * If the new file cannot be created, the old one is opened again.
     *
     * @param error  Unless Rotated, what happened, for the user.
     */
    Rotation rotate( QString& error );

    /**
     * Keep the temporary directory when this is destroyed, so that the
     * LogSquirl tabs keep showing its files (until LogSquirl quits, see
     * tempdirs.h).
     *
     * @return The directory kept, or empty when the Session writes to a
     *         save path (nothing to keep).
     */
    QString keepTempDir();

    /// The file open (or last open): the save path or a temporary file;
    /// empty before open() and after a failed one.
    const QString& path() const
    {
        return path_;
    }

    /// Whether the Session writes to the user's save path.
    bool isSavePath() const
    {
        return !savePath_.isEmpty();
    }

    /// Size of the open file, with what it held before open().
    qint64 size() const
    {
        return file_.size();
    }

private:
    /// Open @p path with @p mode, refusing anything that is not a regular
    /// file.  False, with the reason in @p error.
    bool openPath( const QString& path, QIODevice::OpenMode mode, QString& error );

    /// The capture cannot go on in this file: remember why, once.
    void fail();

    QString serial_;
    QString savePath_;
    QString tempRoot_;
    /// Created by the first open() without a save path.
    std::unique_ptr<QTemporaryDir> tempDir_;
    QFile file_;
    /// The file open in file_, which an open by descriptor leaves without
    /// a name.
    QString path_;
    QString failure_;
    int rotations_ = 0;    ///< Successful rotate()s: they name the temporary files.
    bool created_ = false; ///< Whether open() created the file.
};

} // namespace logcat
