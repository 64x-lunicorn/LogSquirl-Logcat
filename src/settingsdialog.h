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
 * @file settingsdialog.h
 * @brief The settings of the sidebar tab: the log directory and the ADB path.
 *
 *   ┌ Logcat Settings ───────────────────────────────┐
 *   │ Log directory  [~/logs                 ] […]   │
 *   │ ADB            [(auto: /opt/…/adb)     ] […]   │
 *   │                              [Cancel] [OK]     │
 *   └────────────────────────────────────────────────┘
 *
 * Opened by the tab's Settings… button (sidebarwidget.h).  OK saves the log
 * directory in the device store (devicestore.h) and the ADB path in
 * logcat.ini (adbprocess.h), and rescans the devices with a new ADB path
 * right away.  A device's own save path is set on its tile instead.
 *
 * Children named for tests: "logDir", "adbPath" (line edits), and their
 * browse buttons "browseLogDir", "browseAdb".
 */

#pragma once

#include <QDialog>

class QLineEdit;

namespace logcat {

class DeviceStore;
class Sessions;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    /// Shows the settings of @p store and of adb; @p sessions rescans
    /// with a new ADB path.  Both must outlive it.
    SettingsDialog( DeviceStore* store, Sessions* sessions, QWidget* parent = nullptr );

    /// Save the settings, rescan with a new ADB path, and close.
    void accept() override;

private:
    DeviceStore* store_;
    Sessions* sessions_;
    QLineEdit* logDir_;
    QLineEdit* adbPath_;
};

} // namespace logcat
