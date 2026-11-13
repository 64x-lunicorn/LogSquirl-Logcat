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
 * @file devicestore.h
 * @brief The devices seen before, and where the logs of each go.
 *
 * Each device the sidebar has seen attached, in any state, is remembered
 * with its model and its own save path, if it has one: the sidebar shows
 * a tile for it also while it is unplugged, until it is forgotten
 * (sidebarwidget.h).  A device without a save path writes a new file in
 * the log directory (settings.h) at each Start.
 *
 * SETTINGS FILE
 * ─────────────
 * logcat.ini in the plugin's configuration directory (get_config_dir), the
 * file whose other settings, the ADB path and the log directory, are
 * Settings' (settings.h):
 *
 *   [devices]
 *   1\serial=3A1B2C3D4E5F
 *   1\model=Pixel 8
 *   1\savePath=
 *   size=1
 *
 * The devices are the array "devices", in the order they were first seen.
 * The store reads the file on each call, so two stores on one file agree
 * (as one after a restart does with the one before).  Without a file (no
 * host to say where) it remembers nothing.
 *
 * OWNERSHIP
 * ─────────
 * Created by logsquirl_plugin_init() and deleted by
 * logsquirl_plugin_shutdown(), after the sidebar that shows it.
 */

#pragma once

#include "adbprocess.h"

#include <QList>
#include <QObject>
#include <QString>

#include <optional>

namespace logcat {

/// A device seen before.
struct KnownDevice {
    QString serial;
    QString model;    ///< The last model adb named for it; empty if it never did.
    QString savePath; ///< The file its logs are appended to; empty: the log directory.
};

class DeviceStore : public QObject {
    Q_OBJECT

public:
    /// The store of @p settingsFile, which need not exist yet; empty, the
    /// store remembers nothing.
    explicit DeviceStore( const QString& settingsFile, QObject* parent = nullptr );

    /// The file the store reads and writes.
    QString settingsFile() const
    {
        return settingsFile_;
    }

    /// The devices seen before, in the order they were first seen.
    QList<KnownDevice> list() const;

    /// The device @p serial, if it was seen before.
    std::optional<KnownDevice> find( const QString& serial ) const;

    /// Remember the devices of @p attached that are new, after the others,
    /// and the model of those whose model adb names now.  A device that is
    /// not attached is kept.
    void remember( const QList<Device>& attached );

    /// Forget the device @p serial (with its save path).
    void forget( const QString& serial );

    /// Make @p path the file the device @p serial appends its logs to; empty
    /// for a new file in the log directory at each Start.  Only for a
    /// device seen before.
    void setSavePath( const QString& serial, const QString& path );

    /**
     * The file a Start of the device @p serial writes to: its save path,
     * or else a new file `<yyyy-MM-dd_HHmmss>_<serial>.log` in @p logDir
     * (Settings::logDir()), which is created.  A relative log directory is
     * left as it is (AdbProcess::start() refuses it with a message).
     * Empty, for a temporary file, without either.
     */
    QString savePathFor( const QString& serial, const QString& logDir ) const;

Q_SIGNALS:
    /// A device was remembered, changed or forgotten.
    void changed();

private:
    /// Write @p devices, replacing those saved.
    void write( const QList<KnownDevice>& devices );

    QString settingsFile_;
};

} // namespace logcat
