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
 * @file fakeadbdevices.h
 * @brief A runner of `adb devices -l` that the test completes, for tests.
 *
 * FakeAdbDevices is the tests' adapter of DeviceScan's port
 * (devicescan.h): a run starts when DeviceScan asks, and ends when the
 * test calls list() or fail(), with the output or failure the test
 * chooses.  Nothing runs, and nothing waits.
 */

#pragma once

#include "devicescan.h"

#include <QStringList>

namespace logcat_test {

// No Q_OBJECT: it adds no signals or slots.
class FakeAdbDevices : public logcat::AdbDevicesRunner {
public:
    using logcat::AdbDevicesRunner::AdbDevicesRunner;

    /// What `adb devices -l` lists for one ready device, @p serial.
    static QByteArray listing( const QString& serial )
    {
        return "List of devices attached\n" + serial.toUtf8() + "  device model:Pixel_8\n\n";
    }

    void start( const QString& adb ) override
    {
        adbs << adb;
        running_ = true;
        if ( !failOnStart.isEmpty() ) {
            // As QProcess on Windows: the failure is reported inside start().
            fail( failOnStart );
        }
    }

    void abandon() override
    {
        if ( running_ ) {
            running_ = false;
            ++abandoned;
        }
    }

    /// Whether a run has started and not yet ended.
    bool isRunning() const
    {
        return running_;
    }

    /// End the running run: adb listed @p output.
    void list( const QByteArray& output )
    {
        REQUIRE( running_ );
        running_ = false;
        Q_EMIT listed( output );
    }

    /// End the running run: adb listed one ready device, @p serial.
    void listOne( const QString& serial )
    {
        list( listing( serial ) );
    }

    /// End the running run: it failed with @p message.
    void fail( const QString& message )
    {
        REQUIRE( running_ );
        running_ = false;
        Q_EMIT failed( message );
    }

    QStringList adbs;    ///< The adb of each run started, in order.
    int abandoned = 0;   ///< Runs abandoned while they ran.
    QString failOnStart; ///< If set, each run fails inside start() with it.

private:
    bool running_ = false;
};

} // namespace logcat_test
