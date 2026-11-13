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
 * @file tilestate.h
 * @brief What a device's tile shows it is: the word the device roster
 * (deviceroster.h) decides and the tile (devicetile.h) shows, so that
 * neither depends on the other.
 */

#pragma once

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

} // namespace logcat
