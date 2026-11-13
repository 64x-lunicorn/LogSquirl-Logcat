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
 * @file devicestore_test.cpp
 * @brief BDD tests for DeviceStore: the devices seen before, and where logs go.
 *
 * Driven on temporary settings files; through the sidebar in
 * sidebarwidget_test.cpp.
 */

#include <catch2/catch.hpp>

#include "devicestore.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

using logcat::Device;
using logcat::DeviceStore;

namespace {

QStringList serialsOf( const DeviceStore& store )
{
    QStringList serials;
    const auto known = store.list();
    for ( const auto& device : known ) {
        serials << device.serial;
    }
    return serials;
}

} // namespace

SCENARIO( "the store remembers the devices it is shown", "[devicestore]" )
{
    GIVEN( "a store on a new settings file" )
    {
        QTemporaryDir dir;
        const auto file = dir.filePath( "logcat.ini" );
        DeviceStore store( file );
        int changes = 0;
        QObject::connect( &store, &DeviceStore::changed, [ &changes ]() { ++changes; } );

        THEN( "it knows no device" )
        {
            REQUIRE( store.list().isEmpty() );
        }

        WHEN( "it is shown a ready device with a model and an unauthorized one without" )
        {
            store.remember(
                { { "3A1B2C3D4E5F", "device", "Pixel 8" }, { "NOAUTH456", "unauthorized", {} } } );

            THEN( "it knows both, in that order, and says so once" )
            {
                REQUIRE( serialsOf( store ) == QStringList{ "3A1B2C3D4E5F", "NOAUTH456" } );
                REQUIRE( store.find( "3A1B2C3D4E5F" )->model == "Pixel 8" );
                REQUIRE( changes == 1 );
            }

            THEN( "another store on the same file knows them too, as after a restart" )
            {
                const DeviceStore restarted( file );
                REQUIRE( serialsOf( restarted ) == QStringList{ "3A1B2C3D4E5F", "NOAUTH456" } );
                REQUIRE( restarted.find( "3A1B2C3D4E5F" )->model == "Pixel 8" );
            }

            AND_WHEN( "it is shown the same devices again" )
            {
                store.remember( { { "3A1B2C3D4E5F", "device", "Pixel 8" },
                                  { "NOAUTH456", "unauthorized", {} } } );

                THEN( "nothing changed" )
                {
                    REQUIRE( changes == 1 );
                }
            }

            AND_WHEN( "the unauthorized device is allowed and names its model, and the other is "
                      "unplugged" )
            {
                store.remember( { { "NOAUTH456", "device", "Galaxy S23" } } );

                THEN( "its model is learnt, and the unplugged one is still known" )
                {
                    REQUIRE( serialsOf( store ) == QStringList{ "3A1B2C3D4E5F", "NOAUTH456" } );
                    REQUIRE( store.find( "NOAUTH456" )->model == "Galaxy S23" );
                    REQUIRE( changes == 2 );
                }
            }

            AND_WHEN( "the device with a model goes offline, which names none" )
            {
                store.remember( { { "3A1B2C3D4E5F", "offline", {} } } );

                THEN( "its model is kept" )
                {
                    REQUIRE( store.find( "3A1B2C3D4E5F" )->model == "Pixel 8" );
                    REQUIRE( changes == 1 );
                }
            }

            AND_WHEN( "one is forgotten" )
            {
                store.forget( "3A1B2C3D4E5F" );

                THEN( "only the other is known, also after a restart" )
                {
                    REQUIRE( serialsOf( store ) == QStringList{ "NOAUTH456" } );
                    REQUIRE( serialsOf( DeviceStore( file ) ) == QStringList{ "NOAUTH456" } );
                    REQUIRE( changes == 2 );
                    REQUIRE_FALSE( store.find( "3A1B2C3D4E5F" ) );
                }
            }
        }
    }

    GIVEN( "a store without a settings file, as without a host" )
    {
        DeviceStore store( {} );

        WHEN( "it is shown a device" )
        {
            store.remember( { { "emulator-5554", "device", {} } } );

            THEN( "it remembers nothing" )
            {
                REQUIRE( store.list().isEmpty() );
            }
        }
    }
}

SCENARIO( "the store says where a device's log goes", "[devicestore]" )
{
    GIVEN( "a store that knows a device" )
    {
        QTemporaryDir dir;
        const auto file = dir.filePath( "logcat.ini" );
        DeviceStore store( file );
        store.remember( { { "emulator-5554", "device", {} } } );

        THEN( "without a log directory, a new file goes to logs/ beside the settings" )
        {
            const auto path = store.savePathFor( "emulator-5554" );
            REQUIRE( QFileInfo( path ).absolutePath() == QDir( dir.path() ).filePath( "logs" ) );
            REQUIRE( QFileInfo( path ).fileName().endsWith( "_emulator-5554.log" ) );
            REQUIRE( QFileInfo( QFileInfo( path ).absolutePath() ).isDir() );
        }

        WHEN( "a log directory is set" )
        {
            const auto logDir = dir.filePath( "mine" );
            store.setLogDir( logDir );

            THEN( "a new file goes there, and the directory is remembered" )
            {
                REQUIRE( QFileInfo( store.savePathFor( "emulator-5554" ) ).absolutePath()
                         == logDir );
                REQUIRE( DeviceStore( file ).logDir() == logDir );
            }

            AND_WHEN( "the device has a save path of its own" )
            {
                const auto own = dir.filePath( "phone.log" );
                store.setSavePath( "emulator-5554", own );

                THEN( "its log goes there, also after a restart" )
                {
                    REQUIRE( store.savePathFor( "emulator-5554" ) == own );
                    REQUIRE( DeviceStore( file ).find( "emulator-5554" )->savePath == own );
                }

                AND_WHEN( "the save path is cleared" )
                {
                    store.setSavePath( "emulator-5554", {} );

                    THEN( "its log goes to the log directory again" )
                    {
                        REQUIRE( QFileInfo( store.savePathFor( "emulator-5554" ) ).absolutePath()
                                 == logDir );
                    }
                }
            }
        }
    }
}
