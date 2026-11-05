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
 * @file scopedenv.h
 * @brief Sets an environment variable for the length of a test.
 */

#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace logcat_test {

/** Sets an environment variable for its lifetime, then restores it. */
class ScopedEnv {
public:
    ScopedEnv( const char* name, const QByteArray& value )
        : name_( name )
        , saved_( qgetenv( name ) )
    {
        qputenv( name, value );
    }

    ~ScopedEnv()
    {
        if ( saved_.isNull() ) {
            qunsetenv( name_ );
        }
        else {
            qputenv( name_, saved_ );
        }
    }

    ScopedEnv( const ScopedEnv& ) = delete;
    ScopedEnv& operator=( const ScopedEnv& ) = delete;

private:
    const char* name_;
    QByteArray saved_;
};

} // namespace logcat_test
