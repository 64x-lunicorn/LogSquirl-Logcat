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
 * @file plugin.cpp
 * @brief C ABI entry points for the LogSquirl logcat plugin.
 *
 * This file implements the four exported symbols that every LogSquirl
 * plugin must provide:
 *
 *   - logsquirl_plugin_get_info()   → static metadata
 *   - logsquirl_plugin_init_ex()    → init() for LogSquirl 26.11 and later,
 *                                     with the size of the host's table
 *   - logsquirl_plugin_init()       → store host API, create UI widget
 *   - logsquirl_plugin_shutdown()   → tear down sessions, destroy widget
 *   - logsquirl_plugin_configure()  → open ADB path configuration dialog
 *
 * PLUGIN LIFECYCLE
 * ────────────────
 *   1. Host calls get_info() to read metadata.
 *   2. Host calls init_ex(api, handle, api_size) — or init(api, handle),
 *      which is init_ex() with the base table's size, if it is older than
 *      LogSquirl 26.11 — we store the pointers and the size, add the
 *      Plugins menu entry, create the Sessions and the DeviceWidget over
 *      them, the DeviceStore, and a SidebarWidget over both, and register
 *      the latter as a sidebar tab.
 *   3. User interacts with the widgets (start, stop, settings, …).
 *   4. Host calls shutdown() — we unregister + delete the sidebar, stop all
 *      logcat sessions, delete the DeviceWidget, the Sessions and the
 *      DeviceStore, and clear state.
 *
 * No exception may leave an entry point, a host callback or a Qt signal
 * handler: the host is C, and an exception escaping into it terminates
 * LogSquirl.  Their work runs through guarded() (plugin.h), which logs the
 * failure instead.
 *
 * @see logsquirl_plugin_api.h for the full host API reference.
 */

#include "plugin.h"

#include "adbprocess.h"
#include "devicestore.h"
#include "devicewidget.h"
#include "sessions.h"
#include "sidebarwidget.h"
#include "tempdirs.h"

#include <QApplication>
#include <QWindow>

// ── Global state ─────────────────────────────────────────────────────────

namespace logcat {
PluginState g_state;

void hostLog( int level, const QString& message )
{
    if ( g_state.api && g_state.handle ) {
        g_state.api->log_message( g_state.handle, level, message.toUtf8().constData() );
    }
}

void hostNotify( const QString& message )
{
    if ( g_state.api && g_state.handle ) {
        g_state.api->show_notification( g_state.handle, message.toUtf8().constData() );
    }
}

void logFailure( const char* what, const char* reason ) noexcept
{
    try {
        hostLog( LOGSQUIRL_LOG_ERROR, QStringLiteral( "Logcat plugin: %1 failed: %2" )
                                          .arg( what, QString::fromUtf8( reason ) ) );
    } catch ( ... ) {
        // Nothing left to report it with.
    }
}
} // namespace logcat

using logcat::guarded;

// ── Plugin metadata ──────────────────────────────────────────────────────

/// Static plugin info.  All strings have static storage duration.
static const LogSquirlPluginInfo kPluginInfo = {
    /* id          */ "io.github.logsquirl.logcat",
    /* name        */ "Android Logcat",
    /* version     */ LOGSQUIRL_PLUGIN_VERSION,
    /* description */ "Stream Android logcat from ADB devices into LogSquirl tabs",
    /* author      */ "LogSquirl Contributors",
    /* license     */ "GPL-3.0-or-later",
    /* type        */ LOGSQUIRL_PLUGIN_UI,
    /* api_version */ LOGSQUIRL_PLUGIN_API_VERSION,
};

// ── Menu action callback ─────────────────────────────────────────────────

/**
 * Called when the user clicks "Android Logcat" in the Plugins menu.
 * Creates (if needed) and shows the logcat session dialog.
 */
static void showLogcatDialog( void* /* userData */ )
{
    guarded( "showing the Android Logcat dialog", [] {
        if ( !logcat::g_state.sessions ) {
            return; // not initialised
        }
        if ( !logcat::g_state.dialog ) {
            logcat::g_state.dialog = new logcat::DeviceWidget( logcat::g_state.sessions );
        }
        auto* dialog = logcat::g_state.dialog;

        // The dialog is created parentless in init() and deleted in shutdown(),
        // so it must not become a child of a main window that may be destroyed
        // first.  A transient parent keeps it on top of the window whose menu
        // opened it, without handing over ownership.
        auto* window = QApplication::activeWindow();
        if ( window && window != dialog ) {
            dialog->winId(); // creates the native window, and so windowHandle()
            dialog->windowHandle()->setTransientParent( window->windowHandle() );
        }

        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    } );
}

// ── Exported C entry points ──────────────────────────────────────────────

extern "C" {

LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void );

/**
 * Return static plugin metadata.
 *
 * The host calls this before init() to read the plugin's identity, type,
 * and API version.  The returned pointer is valid for the lifetime of the
 * shared library.
 */
LOGSQUIRL_PLUGIN_EXPORT const LogSquirlPluginInfo* logsquirl_plugin_get_info( void )
{
    return &kPluginInfo;
}

/**
 * Initialise the plugin.
 *
 * @param api     Host API function table — valid until shutdown().
 * @param handle  Opaque handle — pass back to every host API call.
 * @param api_size  The size of the table @p api points to, in the host's
 *                  build: it tells which later functions the host has.
 * @return 0 on success, non-zero on failure.
 *
 * Stores the host API and handle, creates the Sessions and a DeviceWidget
 * over them, and the DeviceStore (the devices seen before and the log
 * directory), and registers a sidebar tab over the store and the Sessions.
 * A failure is logged and undone (shutdown()),
 * and init returns 1.
 */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init_ex( const LogSquirlHostApi* api, void* handle,
                                                      size_t api_size )
{
    if ( !api || !handle ) {
        return 1;
    }

    logcat::g_state.hostApiSize = api_size;

    logcat::g_state.api = api;
    logcat::g_state.handle = handle;
    logcat::g_state.initialised = true;
    logcat::g_state.quitting = false;

    const bool ok = guarded( "initialisation", [ api, handle ] {
        api->log_message( handle, LOGSQUIRL_LOG_INFO, "Logcat plugin initialising…" );

        // Files of LogSquirl processes that ended without removing them,
        // e.g. after a crash: no tab can show them any more.
        logcat::removeStaleTempDirs( logcat::tempRoot() );

        // Add "Android Logcat" to the Plugins menu.  When clicked it opens a
        // non-modal dialog for device selection and session management.
        api->register_menu_action( handle, "Plugins", "Android Logcat\u2026", &showLogcatDialog,
                                   nullptr );

        // The devices and their sessions, which the dialog and the sidebar
        // show.  Creating it starts the first device scan.
        logcat::g_state.sessions = new logcat::Sessions();
        logcat::g_state.dialog = new logcat::DeviceWidget( logcat::g_state.sessions );

        // The host shuts the plugin down both when LogSquirl quits (after
        // aboutToQuit) and when the plugin is disabled or updated at runtime,
        // with the tabs left open; only in the first case may the temporary
        // files go.
        if ( auto* app = QCoreApplication::instance() ) {
            QObject::connect( app, &QCoreApplication::aboutToQuit, logcat::g_state.sessions, []() {
                guarded( "preparing to quit", [] { logcat::g_state.quitting = true; } );
            } );
        }

        // The devices seen before, remembered in logcat.ini, and the log
        // directory; none without a config directory.
        logcat::g_state.store = new logcat::DeviceStore( logcat::AdbProcess::settingsFile() );

        // Register a sidebar tab with a tile per device
        logcat::g_state.sidebarWidget
            = new logcat::SidebarWidget( logcat::g_state.store, logcat::g_state.sessions );
        api->register_sidebar_tab( handle, "Logcat",
                                   static_cast<void*>( logcat::g_state.sidebarWidget ) );
        logcat::g_state.sidebarTabRegistered = true;

        api->log_message( handle, LOGSQUIRL_LOG_INFO, "Logcat plugin ready." );
    } );

    if ( !ok ) {
        // Undo what was set up, in reverse: shutdown() unregisters the tab
        // only if the host took it, then deletes the widgets.
        logsquirl_plugin_shutdown();
        return 1;
    }
    return 0;
}

/**
 * Initialise the plugin for a host older than init_ex(): its table ends
 * where the functions LogSquirl 26.11 added begin.  The host refuses a
 * plugin without this entry point.
 */
LOGSQUIRL_PLUGIN_EXPORT int logsquirl_plugin_init( const LogSquirlHostApi* api, void* handle )
{
    return logsquirl_plugin_init_ex( api, handle, LOGSQUIRL_HOST_API_BASE_SIZE );
}

/**
 * Shut down the plugin — stop all sessions and release resources.
 *
 * The host calls this before unloading the shared library.  After this
 * function returns, no host API calls may be made.
 */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_shutdown( void )
{
    auto& st = logcat::g_state;
    // Each step on its own, so that one failing does not skip the others.
    guarded( "shutdown",
             [] { logcat::hostLog( LOGSQUIRL_LOG_INFO, "Logcat plugin shutting down…" ); } );

    // The host must let go of the tab before its widget is deleted.
    if ( st.sidebarTabRegistered ) {
        st.sidebarTabRegistered = false;
        guarded( "unregistering the sidebar tab", [] {
            auto& st = logcat::g_state;
            if ( st.api && st.handle ) {
                st.api->unregister_sidebar_tab( st.handle, static_cast<void*>( st.sidebarWidget ) );
            }
        } );
    }
    if ( st.sidebarWidget ) {
        guarded( "shutdown", [] { delete logcat::g_state.sidebarWidget; } );
        st.sidebarWidget = nullptr;
    }

    if ( st.sessions ) {
        guarded( "stopping the logcat sessions", [] { logcat::g_state.sessions->stopAll(); } );
    }
    if ( st.dialog ) {
        guarded( "shutdown", [] { delete logcat::g_state.dialog; } );
        st.dialog = nullptr;
    }
    if ( st.sessions ) {
        guarded( "shutdown", [] { delete logcat::g_state.sessions; } );
        st.sessions = nullptr;
    }
    if ( st.store ) {
        guarded( "shutdown", [] { delete logcat::g_state.store; } );
        st.store = nullptr;
    }

    // The tabs close with LogSquirl: remove the files of every instance of
    // the plugin in this process, also those of instances before a runtime
    // disable or update, which only the directory names remember.  Save
    // paths and the log directory are never touched.
    if ( st.quitting ) {
        guarded( "removing temporary files",
                 [] { logcat::removeOwnTempDirs( logcat::tempRoot() ); } );
    }

    logcat::g_state.api = nullptr;
    logcat::g_state.handle = nullptr;
    logcat::g_state.hostApiSize = 0;
    logcat::g_state.initialised = false;
}

/**
 * Open a configuration dialog for the ADB path.
 *
 * @param parent_widget  Cast of a QWidget* the plugin can use as dialog parent.
 *
 * Reads the current ADB path from the plugin's config directory, lets the
 * user pick a new one, persists the choice, and rescans the devices with it.
 */
LOGSQUIRL_PLUGIN_EXPORT void logsquirl_plugin_configure( void* parent_widget )
{
    // The same as the dialog's Configure button, so that a new path is
    // used for a device scan right away, in the dialog and the sidebar.
    guarded( "configuring", [ parent_widget ] {
        if ( logcat::g_state.dialog ) {
            logcat::g_state.dialog->configureAdbPath( static_cast<QWidget*>( parent_widget ) );
        }
    } );
}

} // extern "C"
