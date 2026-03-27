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
 * @file plugin.h
 * @brief Global plugin state shared across all translation units.
 *
 * This header centralises the host API pointer, the opaque handle, and
 * the collection of active logcat sessions.  It is the single source
 * of truth for "is the plugin initialised?" and "what is running?".
 *
 * DESIGN NOTE — why globals and not a class?
 * ------------------------------------------
 * The LogSquirl plugin ABI is pure C.  The host calls free functions
 * (logsquirl_plugin_init, logsquirl_plugin_shutdown, …) with no object
 * context.  A small, clearly scoped set of file-level globals is the
 * idiomatic way to bridge between the C entry points and the C++/Qt
 * implementation behind them.
 */

#pragma once

#include "logsquirl_plugin_api.h"

namespace logcat {
class DeviceWidget;
class SidebarWidget;
} // namespace logcat

namespace logcat {

/**
 * Global plugin state.
 *
 * Populated by logsquirl_plugin_init(), cleared by logsquirl_plugin_shutdown().
 * All fields are only accessed from the main (GUI) thread, so no locking
 * is necessary.
 */
struct PluginState {
    const LogSquirlHostApi* api = nullptr;  ///< Host API function table.
    void* handle = nullptr;                 ///< Opaque plugin instance handle.
    DeviceWidget* dialog = nullptr;         ///< Logcat session dialog.
    SidebarWidget* sidebarWidget = nullptr;  ///< Sidebar panel for session control.
    bool initialised = false;               ///< True between init() and shutdown().
};

/// Singleton plugin state.  Defined in plugin.cpp.
extern PluginState g_state;

/// Convenience: log via host API.  No-op if the plugin is not initialised.
void hostLog( int level, const char* message );

} // namespace logcat
