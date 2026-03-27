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
 * @file newsessionwidget.h
 * @brief Toolbar button that rotates the log for an active logcat session.
 *
 * When clicked, a dropdown menu lists all active device sessions.
 * Selecting one rotates its log: the old temp file is preserved (tab stays
 * open), a new temp file is created, and a new LogSquirl tab opens in
 * follow mode.
 *
 * Registered in the host's "Plugins" toolbar via register_status_widget().
 */

#pragma once

#include <QMenu>
#include <QToolButton>
#include <QWidget>

namespace logcat {

/**
 * Toolbar widget with a "New Session" button and a dynamic dropdown
 * listing active logcat sessions.
 */
class NewSessionWidget : public QWidget {
    Q_OBJECT

  public:
    explicit NewSessionWidget( QWidget* parent = nullptr );

  private Q_SLOTS:
    /** Rebuild the dropdown menu with current active sessions. */
    void updateMenu();

  private:
    QToolButton* button_ = nullptr;
    QMenu* menu_ = nullptr;
};

} // namespace logcat
