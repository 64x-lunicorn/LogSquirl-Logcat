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
 * @file fakeadb.h
 * @brief Stand-ins for the adb executable, for tests.
 *
 * The install helpers write a file into the FakeHost's config directory
 * and point the logcat.ini "adb/path" override at it, so findAdb()
 * returns it instead of a real adb.
 */

#pragma once

#include "fakehost.h"

#include <QFile>
#include <QSettings>

namespace logcat_test {

/** Make @p path the adb that the plugin under test runs. */
inline void useAdb( const FakeHost& host, const QString& path )
{
    QSettings settings( host.configDir() + "/logcat.ini", QSettings::IniFormat );
    settings.setValue( "adb/path", path );
}

/**
 * Install an adb that exists but cannot be executed on any platform (a
 * text file), so that launching it fails with QProcess::FailedToStart.
 */
inline QString installBrokenAdb( const FakeHost& host )
{
    const auto path = host.configDir() + "/not-adb.txt";
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        return {};
    }
    file.write( "this is not an executable\n" );
    useAdb( host, path );
    return path;
}

#ifdef Q_OS_UNIX
/**
 * Install a shell script as adb.  `adb devices` lists one device,
 * emulator-5554; `adb -s <serial> logcat` runs @p logcatScript.  The
 * default script prints two lines, then waits to be stopped.
 */
inline QString installFakeAdb( const FakeHost& host,
                               const QByteArray& logcatScript
                               = "printf 'first\\r\\nsecond\\n'\nexec sleep 30\n" )
{
    const auto path = host.configDir() + "/adb";
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        return {};
    }
    file.write( "#!/bin/sh\n"
                "if [ \"$1\" = devices ]; then\n"
                "    printf 'List of devices attached\\nemulator-5554\\tdevice\\n\\n'\n"
                "    exit 0\n"
                "fi\n" );
    file.write( logcatScript );
    file.setPermissions( file.permissions() | QFileDevice::ExeOwner );
    file.close();
    useAdb( host, path );
    return path;
}

/**
 * A logcat script that prints "first", waits until the file @p trigger
 * exists, prints "after", then waits to be stopped.  Lets a test change
 * something between two lines of output.
 */
inline QByteArray scriptWaitingFor( const QString& trigger )
{
    return "printf 'first\\n'\n"
           "while [ ! -e '"
           + trigger.toUtf8()
           + "' ]; do sleep 0.02; done\n"
             "printf 'after\\n'\n"
             "exec sleep 30\n";
}
#endif

} // namespace logcat_test
