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
 * @file devicetile.h
 * @brief One device as a tile (card) in the sidebar.
 *
 *   ╭───────────────────────────────────────╮
 *   │ ● Pixel 8 · 3A1B…          [▶ Start]  │  state dot, title, Start / Stop
 *   │ 3A1B2C3D4E5F · connected              │  summary: the device, its
 *   │ Saves to the log directory            │  state and where it saves
 *   │ ───────────────────────────────────── │  separator, with the details
 *   │ running 1:02:03 · 120 lines/s         │  (muted), hidden without any:
 *   │ 12,345 lines · 1.2 MB                 │  the running session's
 *   │ file 512.0 KB · 2 rotations           │  counters
 *   │ rotates on request                    │
 *   │              [↻ Rotate] [Save to…]    │  Rotate while it runs; Forget
 *   ╰───────────────────────────────────────╯  only while it is unplugged
 *
 * The card of LogSquirl-NATS's ConnectionTile, standing for a device rather
 * than a saved configuration: a rounded 1 px frame on a background set off
 * the sidebar's, the frame in the accent colour while the device runs
 * logcat, and the texts greyed while it is unplugged.  Its colours are
 * taken from the palette, so a light and a dark theme both work, and follow
 * a change of the palette.  The dot is green (running), grey (connected),
 * amber (unauthorized, offline or another state adb names) or hollow
 * (disconnected); the summary says the same in words, for whoever cannot
 * tell the colours apart.
 *
 * Only a connected device can be started: Start is disabled otherwise.
 *
 * A plain widget that knows nothing of adb: the sidebar (sidebarwidget.h)
 * sets its texts and state and acts on its signals.  The details are where
 * a running session's counters go (setDetails()).
 *
 * Its children are named for tests: "state" (its property "state" is
 * disconnected, connected, running, unauthorized, offline or unavailable),
 * "title", "summary", "separator", "details", "startStop", "rotate",
 * "savePath", "forget".  The tile's properties "running" and
 * "disconnected" style its frame and its texts.
 */

#pragma once

#include <QFrame>
#include <QPointer>
#include <QString>
#include <QStringList>

class QEvent;
class QLabel;
class QPushButton;

namespace logcat {

/// What a tile's device is, and does.
enum class TileState {
    Disconnected, ///< Seen before, not attached now.
    Connected,    ///< Attached and ready (adb's "device"), not running.
    Running,      ///< Running logcat.
    Unauthorized, ///< Attached, USB debugging not allowed (yet) on the device.
    Offline,      ///< Attached, not answering adb.
    Unavailable,  ///< Attached in another state ("no permissions", "recovery", …).
};

class DeviceTile : public QFrame {
    Q_OBJECT

public:
    /// A tile for the device @p serial, disconnected, titled with its serial.
    explicit DeviceTile( const QString& serial, QWidget* parent = nullptr );

    /// The serial of the device it shows.
    QString serial() const
    {
        return serial_;
    }

    /// What it is called, e.g. "Pixel 8 · 3A1B…".
    void setTitle( const QString& title );

    /// What the device is, one line each.
    void setSummary( const QStringList& lines );

    /// What it does, one line each, below a line; none hides them.
    void setDetails( const QStringList& lines );

    /// What the device is and does: which buttons it has (see the file
    /// comment), and its dot.
    void setState( TileState state );

    /// The state set last.
    TileState state() const
    {
        return state_;
    }

protected:
    void changeEvent( QEvent* event ) override;
    bool eventFilter( QObject* watched, QEvent* event ) override;

Q_SIGNALS:
    /// Start was clicked, the device being connected.
    void startClicked();
    /// Stop was clicked, the device running.
    void stopClicked();
    /// ↻ Rotate was clicked, the device running.
    void rotateClicked();
    /// Save to… was clicked.
    void savePathClicked();
    /// Forget was clicked, the device being disconnected.
    void forgetClicked();

private:
    /// Style the card from the palette around it, if that changed.
    void applyStyle();

    QString serial_;
    TileState state_ = TileState::Disconnected;
    /// The palette colours the stylesheet was made for.
    QString styledFor_;
    /// The parent whose palette changes it follows.
    QPointer<QWidget> watched_;
    QLabel* dot_;
    QLabel* title_;
    QLabel* summary_;
    QFrame* separator_;
    QLabel* details_;
    QPushButton* startStop_;
    QPushButton* rotate_;
    QPushButton* savePath_;
    QPushButton* forget_;
};

} // namespace logcat
