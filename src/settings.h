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
 * @file settings.h
 * @brief The plugin's settings in logcat.ini: the adb it runs, and the log
 *        directory.
 *
 * Settings owns logcat.ini in the plugin's configuration directory, apart
 * from the devices seen before, which DeviceStore keeps there
 * (devicestore.h):
 *
 *   [adb]
 *   path=/opt/homebrew/bin/adb
 *   [logcat]
 *   logDir=/Users/me/logs
 *
 * findAdb() finds the adb that the device scan and the sessions run: the
 * ADB path the user set, else the SDK's (ANDROID_HOME, then
 * ANDROID_SDK_ROOT), else the PATH's, else one in a well-known location.
 * Setting the ADB path emits adbPathChanged(), on which the device scan
 * starts again (devicescan.h): both dialogs only set it.
 *
 * Every call reads the file, so two Settings on one file agree.  Without a
 * configuration directory (no host to say where) nothing is read or saved:
 * one in the temporary directory would let any local user set the adb that
 * LogSquirl runs.
 *
 * OWNERSHIP
 * ─────────
 * Created first by logsquirl_plugin_init() and deleted last by
 * logsquirl_plugin_shutdown(), after everything that reads it.
 */

#pragma once

#include <QObject>
#include <QString>

namespace logcat {

class Settings : public QObject {
    Q_OBJECT

public:
    /// The settings in logcat.ini in @p configDir, which need not exist yet;
    /// empty, there are none to read or save.
    explicit Settings( const QString& configDir, QObject* parent = nullptr );

    /// The plugin's configuration directory, as the host says; empty while
    /// the plugin is not initialised.
    static QString hostConfigDir();

    /// The configuration directory; empty without one.
    QString configDir() const
    {
        return configDir_;
    }

    /// logcat.ini in configDir(), which DeviceStore shares; empty without a
    /// configuration directory.
    QString settingsFile() const;

    /// The ADB path the user set; empty for auto-detection.
    QString adbPathOverride() const;

    /**
     * Save @p path (trimmed) as the ADB path; empty for auto-detection.  A
     * new path is logged and emits adbPathChanged(); the same path again
     * changes nothing.  False, and a warning in the log, if a new path
     * cannot be saved: there is no configuration directory.
     */
    bool setAdbPathOverride( const QString& path );

    /**
     * The adb to run: the ADB path the user set, if it exists; else
     * ANDROID_HOME/platform-tools/adb, ANDROID_SDK_ROOT/platform-tools/adb,
     * the PATH's adb, or one in a well-known location (Homebrew, the SDK's
     * default directory).  Empty if there is none.
     */
    QString findAdb() const;

    /// The log directory as the user set it; empty for the default.
    QString customLogDir() const;

    /// Set the log directory (trimmed); empty for the default.
    void setLogDir( const QString& dir );

    /// Where a device without a save path writes: the log directory the
    /// user set, else logs/ in the configuration directory.  Empty without
    /// either.
    QString logDir() const;

Q_SIGNALS:
    /// setAdbPathOverride() saved a new ADB path.
    void adbPathChanged();

private:
    QString configDir_;
};

} // namespace logcat
