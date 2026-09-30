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
 * @file readonlydir.h
 * @brief Makes a directory refuse new files, for tests of failing writes.
 */

#pragma once

#include <QFile>
#include <QTemporaryDir>

namespace logcat_test {

/** Makes a directory read-only for its lifetime, so no file can be created in it. */
class ReadOnlyDir {
public:
    explicit ReadOnlyDir( const QString& path )
        : path_( path )
        , permissions_( QFile::permissions( path ) )
    {
        QFile::setPermissions( path_, QFileDevice::ReadOwner | QFileDevice::ExeOwner );
    }

    ~ReadOnlyDir()
    {
        QFile::setPermissions( path_, permissions_ );
    }

    ReadOnlyDir( const ReadOnlyDir& ) = delete;
    ReadOnlyDir& operator=( const ReadOnlyDir& ) = delete;

    /**
     * Whether a read-only directory really refuses new files here.  It
     * does not for root (tests in a container), nor on Windows.
     */
    static bool isEnforced()
    {
        QTemporaryDir dir;
        const ReadOnlyDir readOnly( dir.path() );
        QFile probe( dir.filePath( "probe" ) );
        return !probe.open( QIODevice::WriteOnly );
    }

private:
    QString path_;
    QFileDevice::Permissions permissions_;
};

} // namespace logcat_test
