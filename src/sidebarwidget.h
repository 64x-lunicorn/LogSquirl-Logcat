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
 * @brief Sidebar panel for ADB logcat session management.
 *
 * This widget is registered as a tab in the LogSquirl sidebar via the
 * host API's register_sidebar_tab().  It provides:
 *
 *   - Device combo box with refresh button
 *   - Start / Stop capture buttons
 *   - Active sessions list showing device serial, line count, and
 *     per-session Rotate / Stop buttons
 *   - A "Stop All" button
 *
 * The panel delegates session management to the DeviceWidget (which also
 * remains available as a standalone dialog via the Plugins menu).  Both
 * the sidebar and the dialog share the same underlying sessions_ map
 * inside DeviceWidget.
 */

#pragma once

#include "adbprocess.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace logcat {

class DeviceWidget;

/**
 * Sidebar panel widget for logcat session control.
 *
 * Embeds device selection and an active-sessions list.  All actual
 * session logic is delegated to DeviceWidget so that the dialog
 * and the sidebar stay in sync.
 */
class SidebarWidget : public QWidget {
    Q_OBJECT

  public:
    /**
     * Construct the sidebar panel.
     *
     * @param deviceWidget  Shared DeviceWidget that owns the sessions.
     *                      Must outlive this widget.
     * @param parent        QWidget parent.
     */
    explicit SidebarWidget( DeviceWidget* deviceWidget,
                            QWidget* parent = nullptr );

  private Q_SLOTS:
    /** Re-scan for ADB devices and update the combo box. */
    void refreshDevices();

    /** Start logcat for the currently selected device. */
    void startCapture();

    /** Stop logcat for the currently selected device. */
    void stopSelectedCapture();

    /** Stop all active sessions. */
    void stopAllCaptures();

    /** Periodic refresh of the active-sessions list (line counts). */
    void refreshSessionList();

  private:
    /** Build or rebuild the sessions list widget contents. */
    void rebuildSessionList();

    /** Return the serial of the currently selected combo box item. */
    QString currentSerial() const;

    /** Update button enabled/disabled states. */
    void updateUiState();

    /** Generate a save path for a new session based on log directory and device serial. */
    QString generateSavePath( const QString& serial ) const;

    /** Load the persisted log directory from plugin config. */
    void loadLogDir();

    /** Persist the log directory to plugin config. */
    void saveLogDir();

    // ── Shared state ─────────────────────────────────────────────────
    DeviceWidget* deviceWidget_ = nullptr;

    // ── UI elements ──────────────────────────────────────────────────
    QComboBox* deviceCombo_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* stopAllButton_ = nullptr;
    QLineEdit* logDirEdit_ = nullptr;
    QPushButton* logDirBrowseButton_ = nullptr;
    QListWidget* sessionList_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    // ── Periodic refresh ─────────────────────────────────────────────
    QTimer* refreshTimer_ = nullptr;
};

} // namespace logcat
