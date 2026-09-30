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
 * @file fakehost.h
 * @brief In-process stand-in for the LogSquirl host API, for tests.
 *
 * A FakeHost installs itself into g_state for its lifetime: the plugin
 * gets a private, empty config directory (so tests never read or write
 * a real logcat.ini), and every log message, notification and
 * open_file() request is recorded for the test to inspect.
 */

#pragma once

#include "plugin.h"

#include <catch2/catch.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <functional>

namespace logcat_test {

class FakeHost {
public:
    FakeHost()
        : configDirUtf8_( configDir_.path().toUtf8() )
    {
        api_.api_version = LOGSQUIRL_PLUGIN_API_VERSION;
        api_.log_message = []( void* handle, int, const char* message ) {
            self( handle )->logs << QString::fromUtf8( message );
        };
        api_.get_config_dir
            = []( void* handle ) { return self( handle )->configDirUtf8_.constData(); };
        api_.show_notification = []( void* handle, const char* message ) {
            self( handle )->notifications << QString::fromUtf8( message );
        };
        api_.open_file = []( void* handle, const char* filePath, int ) {
            self( handle )->openedFiles << QString::fromUtf8( filePath );
        };

        logcat::g_state.api = &api_;
        logcat::g_state.handle = this;
    }

    ~FakeHost()
    {
        logcat::g_state.api = nullptr;
        logcat::g_state.handle = nullptr;
    }

    FakeHost( const FakeHost& ) = delete;
    FakeHost& operator=( const FakeHost& ) = delete;

    /** The plugin's config directory (empty until a test writes to it). */
    QString configDir() const
    {
        return configDir_.path();
    }

    QStringList logs;
    QStringList notifications;
    QStringList openedFiles;

private:
    static FakeHost* self( void* handle )
    {
        return static_cast<FakeHost*>( handle );
    }

    QTemporaryDir configDir_;
    QByteArray configDirUtf8_;
    LogSquirlHostApi api_{};
};

/** Run the event loop until @p condition holds or @p timeoutMs passes. */
inline bool waitFor( const std::function<bool()>& condition, int timeoutMs = 5000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !condition() ) {
        if ( timer.elapsed() > timeoutMs ) {
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep( 5 );
    }
    return true;
}

} // namespace logcat_test

// Let Catch print Qt strings in failure messages.
namespace Catch {
template <>
struct StringMaker<QString> {
    static std::string convert( const QString& value )
    {
        return '"' + value.toStdString() + '"';
    }
};

template <>
struct StringMaker<QByteArray> {
    static std::string convert( const QByteArray& value )
    {
        return '"' + value.toStdString() + '"';
    }
};
} // namespace Catch
