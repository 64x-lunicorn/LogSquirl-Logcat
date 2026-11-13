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

#include <QString>

#include <cstddef>
#include <exception>
#include <utility>

namespace logcat {
class AdbDevicesRunner;
class DeviceScan;
class DeviceStore;
class DeviceWidget;
class Sessions;
class Settings;
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
    Settings* settings = nullptr;           ///< logcat.ini: the adb, and the log directory.
    DeviceScan* scan = nullptr;             ///< The devices adb lists.
    Sessions* sessions = nullptr;           ///< The logcat session of each device.
    DeviceStore* store = nullptr;           ///< The devices seen before.
    DeviceWidget* dialog = nullptr;         ///< Logcat session dialog, a view of scan and sessions.
    SidebarWidget* sidebarWidget = nullptr; ///< Sidebar tab, a view of scan, sessions and store.
    bool sidebarTabRegistered = false;      ///< The host holds sidebarWidget as a tab.
    /// The size of the host's table (init_ex()): a function added after
    /// LogSquirl 26.10 may be used only if LOGSQUIRL_HOST_API_HAS( hostApiSize,
    /// member ) says the running host has it.  Never copy *api.
    std::size_t hostApiSize = 0;
    bool initialised = false; ///< True between init() and shutdown().
    bool quitting = false; ///< LogSquirl is quitting (aboutToQuit), not just unloading the plugin.
    QString tempRoot;      ///< Where temporary directories go; empty: the system's (for tests).
    /// For tests: the runner that the next init() gives its DeviceScan, which
    /// takes it (and init() clears this); null: adb runs in a QProcess.
    AdbDevicesRunner* devicesRunner = nullptr;
};

/// Singleton plugin state.  Defined in plugin.cpp.
extern PluginState g_state;

/// Convenience: log via host API.  No-op if the plugin is not initialised.
/// The host decodes the message as UTF-8.
void hostLog( int level, const QString& message );

/// Convenience: show a host notification.  No-op if the plugin is not
/// initialised.  The host decodes the message as UTF-8.
void hostNotify( const QString& message );

/// Log through the host that @p what failed, for @p reason.  Never throws.
void logFailure( const char* what, const char* reason ) noexcept;

/// Run @p work for the host, logging instead of throwing: the host is C,
/// and an exception escaping into it would terminate LogSquirl.  Every
/// entry point, host callback and Qt signal handler runs its work through
/// it.  @p what names the work in the log ("<what> failed: <reason>").
/// Returns false if it threw; a handler that must return a value returns
/// a default then.
template <typename Work>
bool guarded( const char* what, Work&& work ) noexcept
{
    try {
        std::forward<Work>( work )();
        return true;
    } catch ( const std::exception& e ) {
        logFailure( what, e.what() );
    } catch ( ... ) {
        logFailure( what, "unknown exception" );
    }
    return false;
}

} // namespace logcat
