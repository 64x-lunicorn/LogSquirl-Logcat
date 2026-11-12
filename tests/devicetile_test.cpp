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
 * @file devicetile_test.cpp
 * @brief BDD tests for the tile of a device (issue #48).
 *
 * The tile is a plain widget that knows nothing of adb, so it is tested on
 * its own, through its named children, as the sidebar sets it.
 */

#include <catch2/catch.hpp>

#include "devicetile.h"

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QPalette>
#include <QPushButton>

#include <utility>

using logcat::DeviceTile;
using logcat::TileState;

namespace {

QString stateOf( const DeviceTile& tile )
{
    return tile.findChild<QLabel*>( "state" )->property( "state" ).toString();
}

QPushButton* button( const DeviceTile& tile, const char* name )
{
    return tile.findChild<QPushButton*>( name );
}

QWidget* child( const DeviceTile& tile, const char* name )
{
    return tile.findChild<QWidget*>( name );
}

} // namespace

SCENARIO( "a device tile shows whether its device can run logcat", "[tile]" )
{
    GIVEN( "a new tile" )
    {
        DeviceTile tile( "3A1B2C3D4E5F" );
        tile.show();

        THEN( "it stands for its serial, and is disconnected: Start disabled, Forget shown" )
        {
            REQUIRE( tile.serial() == "3A1B2C3D4E5F" );
            REQUIRE( stateOf( tile ) == "disconnected" );
            REQUIRE( tile.property( "disconnected" ).toBool() );
            REQUIRE( button( tile, "startStop" )->text().contains( "Start" ) );
            REQUIRE_FALSE( button( tile, "startStop" )->isEnabled() );
            REQUIRE( button( tile, "forget" )->isVisible() );
            REQUIRE_FALSE( button( tile, "rotate" )->isVisible() );
            REQUIRE_FALSE( child( tile, "details" )->isVisible() );
        }

        WHEN( "its device is connected" )
        {
            tile.setState( TileState::Connected );

            THEN( "it can be started, not forgotten, and is no longer greyed" )
            {
                REQUIRE( stateOf( tile ) == "connected" );
                REQUIRE( button( tile, "startStop" )->text().contains( "Start" ) );
                REQUIRE( button( tile, "startStop" )->isEnabled() );
                REQUIRE_FALSE( button( tile, "forget" )->isVisible() );
                REQUIRE( button( tile, "savePath" )->isVisible() );
                REQUIRE_FALSE( tile.property( "disconnected" ).toBool() );
            }
        }

        WHEN( "it runs" )
        {
            tile.setState( TileState::Running );

            THEN( "it can be stopped and rotated, and its save path waits" )
            {
                REQUIRE( stateOf( tile ) == "running" );
                REQUIRE( button( tile, "startStop" )->text().contains( "Stop" ) );
                REQUIRE( button( tile, "startStop" )->isEnabled() );
                REQUIRE( button( tile, "rotate" )->isVisible() );
                REQUIRE_FALSE( button( tile, "savePath" )->isVisible() );
                REQUIRE_FALSE( button( tile, "forget" )->isVisible() );
                REQUIRE( tile.property( "running" ).toBool() );
            }
        }

        WHEN( "its device is unauthorized, offline, or in another state" )
        {
            THEN( "the dot says so; it cannot be started, nor forgotten while attached" )
            {
                const std::pair<TileState, QString> states[] = {
                    { TileState::Unauthorized, "unauthorized" },
                    { TileState::Offline, "offline" },
                    { TileState::Unavailable, "unavailable" },
                };
                for ( const auto& [ state, name ] : states ) {
                    tile.setState( state );
                    REQUIRE( stateOf( tile ) == name );
                    REQUIRE_FALSE( button( tile, "startStop" )->isEnabled() );
                    REQUIRE_FALSE( button( tile, "forget" )->isVisible() );
                    REQUIRE_FALSE( tile.property( "disconnected" ).toBool() );
                }
            }
        }

        WHEN( "it has details" )
        {
            tile.setDetails( { "12,345 lines" } );

            THEN( "they are shown below a line; without any, both are hidden again" )
            {
                REQUIRE( child( tile, "details" )->isVisible() );
                REQUIRE( child( tile, "separator" )->isVisible() );
                tile.setDetails( {} );
                REQUIRE_FALSE( child( tile, "details" )->isVisible() );
                REQUIRE_FALSE( child( tile, "separator" )->isVisible() );
            }
        }
    }
}

SCENARIO( "a device tile says which button was clicked", "[tile]" )
{
    GIVEN( "a tile of a connected device" )
    {
        DeviceTile tile( "emulator-5554" );
        tile.setState( TileState::Connected );
        QStringList clicked;
        QObject::connect( &tile, &DeviceTile::startClicked, [ & ] { clicked << "start"; } );
        QObject::connect( &tile, &DeviceTile::stopClicked, [ & ] { clicked << "stop"; } );
        QObject::connect( &tile, &DeviceTile::rotateClicked, [ & ] { clicked << "rotate"; } );
        QObject::connect( &tile, &DeviceTile::savePathClicked, [ & ] { clicked << "savePath"; } );
        QObject::connect( &tile, &DeviceTile::forgetClicked, [ & ] { clicked << "forget"; } );

        WHEN( "Start, Save to…, then, running, Rotate and Stop, then, unplugged, Forget" )
        {
            button( tile, "startStop" )->click();
            button( tile, "savePath" )->click();
            tile.setState( TileState::Running );
            button( tile, "rotate" )->click();
            button( tile, "startStop" )->click();
            tile.setState( TileState::Disconnected );
            button( tile, "forget" )->click();

            THEN( "each is said, in order" )
            {
                REQUIRE( clicked
                         == QStringList{ "start", "savePath", "rotate", "stop", "forget" } );
            }
        }
    }
}

SCENARIO( "a device tile shows its title and summary as plain text", "[tile]" )
{
    GIVEN( "a tile whose title and summary are markup, as any USB gadget can name itself" )
    {
        DeviceTile tile( "<b>x</b>" );
        tile.setTitle( "<b>x</b>" );
        tile.setSummary( { "<i>y</i>" } );

        THEN( "both are shown as their literal text" )
        {
            auto* title = tile.findChild<QLabel*>( "title" );
            auto* summary = tile.findChild<QLabel*>( "summary" );
            REQUIRE( title->text() == "<b>x</b>" );
            REQUIRE( title->textFormat() == Qt::PlainText );
            REQUIRE( summary->text() == "<i>y</i>" );
            REQUIRE( summary->textFormat() == Qt::PlainText );
        }
    }
}

SCENARIO( "a device tile takes its colours from the palette around it", "[tile]" )
{
    GIVEN( "a tile in a dark sidebar" )
    {
        QWidget sidebar;
        QPalette dark;
        dark.setColor( QPalette::Window, QColor( 30, 30, 30 ) );
        dark.setColor( QPalette::WindowText, QColor( 230, 230, 230 ) );
        sidebar.setPalette( dark );
        auto* tile = new DeviceTile( "emulator-5554", &sidebar );

        THEN( "its card is dark too" )
        {
            REQUIRE( tile->styleSheet().contains( "background: "
                                                  + QColor( 30, 30, 30 ).lighter( 125 ).name() ) );
        }

        WHEN( "the sidebar turns light" )
        {
            QPalette light;
            light.setColor( QPalette::Window, QColor( 236, 236, 236 ) );
            light.setColor( QPalette::WindowText, QColor( 20, 20, 20 ) );
            sidebar.setPalette( light );

            THEN( "the card follows" )
            {
                REQUIRE( tile->styleSheet().contains(
                    "background: " + QColor( 236, 236, 236 ).darker( 103 ).name() ) );
            }
        }
    }
}
