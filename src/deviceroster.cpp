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
 * @file deviceroster.cpp
 * @brief Implementation of the device roster: the rows of the sidebar's tiles.
 */

#include "deviceroster.h"

#include <QLocale>

#include <algorithm>
#include <iterator>

namespace logcat {

namespace {

/// The longest serial a title shows whole.
constexpr qsizetype kTitleSerialChars = 8;

/// What the tile of a device in adb's @p state is.
TileState tileState( const QString& state )
{
    if ( state == QLatin1String( "device" ) ) {
        return TileState::Connected;
    }
    if ( state == QLatin1String( "unauthorized" ) ) {
        return TileState::Unauthorized;
    }
    if ( state == QLatin1String( "offline" ) ) {
        return TileState::Offline;
    }
    return TileState::Unavailable;
}

/// The state of a tile in words, for its summary; @p adbState is what adb
/// says for a device in another state.
QString stateWords( TileState state, const QString& adbState )
{
    switch ( state ) {
    case TileState::Running:
        return QStringLiteral( "running" );
    case TileState::Connected:
        return QStringLiteral( "connected" );
    case TileState::Unauthorized:
        return QStringLiteral( "unauthorized: allow USB debugging on the device" );
    case TileState::Offline:
        return QStringLiteral( "offline" );
    case TileState::Unavailable:
        return adbState;
    case TileState::Disconnected:
        break;
    }
    return QStringLiteral( "disconnected" );
}

/// The element of @p list whose serial is @p serial, or nullptr.
template <typename T>
const T* bySerial( const QList<T>& list, const QString& serial )
{
    const auto it = std::find_if( list.cbegin(), list.cend(),
                                  [ &serial ]( const T& item ) { return item.serial == serial; } );
    return it != list.cend() ? &*it : nullptr;
}

/// The row of @p serial, which adb lists as @p attached (nullptr: not), the
/// store remembers as @p known (nullptr: not), and whose session is
/// @p running (nullptr: none) at @p linesPerSecond.
TileRow rowOf( const QString& serial, const Device* attached, const KnownDevice* known,
               const RunningSession* running, qint64 linesPerSecond, const QDateTime& now )
{
    TileRow row;
    row.serial = serial;

    // The model adb names now, or else the one it named before.
    QString model;
    if ( attached && !attached->model.isEmpty() ) {
        model = attached->model;
    }
    else if ( known ) {
        model = known->model;
    }
    row.title = deviceTitle( serial, model );

    if ( running ) {
        row.state = TileState::Running;
    }
    else if ( attached ) {
        row.state = tileState( attached->state );
    }

    row.summary << QStringLiteral( "%1 · %2" )
                       .arg( serial,
                             stateWords( row.state, attached ? attached->state : QString() ) );
    if ( running ) {
        row.summary << QStringLiteral( "Writes %1" ).arg( running->filePath );
        row.linesPerSecond = linesPerSecond;
        row.details = sessionDetails( running->counters, linesPerSecond, now );
    }
    else if ( known && !known->savePath.isEmpty() ) {
        row.summary << QStringLiteral( "Saves to %1" ).arg( known->savePath );
    }
    else {
        row.summary << QStringLiteral( "Saves to the log directory" );
    }
    return row;
}

} // namespace

QString deviceTitle( const QString& serial, const QString& model )
{
    if ( model.isEmpty() ) {
        return serial;
    }
    const auto shown
        = serial.size() > kTitleSerialChars ? serial.left( 4 ) + QStringLiteral( "…" ) : serial;
    return QStringLiteral( "%1 · %2" ).arg( model, shown );
}

QString formatCount( qint64 count )
{
    // English, as every other word of the tile.
    return QLocale( QLocale::English, QLocale::UnitedStates ).toString( count );
}

QString formatSize( qint64 bytes )
{
    constexpr qint64 kUnit = 1024;
    if ( bytes < kUnit ) {
        return QStringLiteral( "%1 B" ).arg( bytes );
    }
    static const char* const units[] = { "KB", "MB", "GB", "TB" };
    double value = static_cast<double>( bytes ) / kUnit;
    std::size_t unit = 0;
    while ( value >= kUnit && unit + 1 < std::size( units ) ) {
        value /= kUnit;
        ++unit;
    }
    return QStringLiteral( "%1 %2" ).arg( value, 0, 'f', 1 ).arg( QLatin1String( units[ unit ] ) );
}

QString formatDuration( qint64 seconds )
{
    seconds = std::max<qint64>( seconds, 0 );
    const auto hours = seconds / 3600;
    const auto minutes = seconds / 60 % 60;
    const auto secs = seconds % 60;
    if ( hours > 0 ) {
        return QStringLiteral( "%1:%2:%3" )
            .arg( hours )
            .arg( minutes, 2, 10, QLatin1Char( '0' ) )
            .arg( secs, 2, 10, QLatin1Char( '0' ) );
    }
    return QStringLiteral( "%1:%2" ).arg( minutes ).arg( secs, 2, 10, QLatin1Char( '0' ) );
}

qint64 ratePerSecond( qint64 count, qint64 elapsedMs )
{
    if ( count <= 0 || elapsedMs <= 0 ) {
        return 0;
    }
    return count * 1000 / elapsedMs;
}

QStringList sessionDetails( const SessionCounters& counters, qint64 linesPerSecond,
                            const QDateTime& now )
{
    const auto rotations = counters.rotations == 1
                               ? QStringLiteral( "1 rotation" )
                               : QStringLiteral( "%1 rotations" ).arg( counters.rotations );
    const auto next
        = counters.nextRotationBytes > 0
              ? QStringLiteral( "rotates at %1" ).arg( formatSize( counters.nextRotationBytes ) )
              : QStringLiteral( "rotates on request" );
    return {
        QStringLiteral( "running %1 · %2 lines/s" )
            .arg( formatDuration( counters.startTime.secsTo( now ) ),
                  formatCount( linesPerSecond ) ),
        QStringLiteral( "%1 lines · %2" )
            .arg( formatCount( counters.lines ), formatSize( counters.bytes ) ),
        QStringLiteral( "file %1 · %2" ).arg( formatSize( counters.fileBytes ), rotations ),
        next,
    };
}

Roster deviceRoster( const QList<KnownDevice>& known, const QList<Device>& attached,
                     const QList<RunningSession>& running,
                     const QHash<QString, CounterSample>& previous, const QDateTime& now,
                     std::optional<qint64> pollAtMs )
{
    // The devices seen before, then those adb lists, then those that run a
    // session adb no longer lists.
    QStringList serials;
    for ( const auto& device : known ) {
        serials << device.serial;
    }
    for ( const auto& device : attached ) {
        if ( !serials.contains( device.serial ) ) {
            serials << device.serial;
        }
    }
    for ( const auto& session : running ) {
        if ( !serials.contains( session.serial ) ) {
            serials << session.serial;
        }
    }

    Roster roster;
    for ( const auto& serial : std::as_const( serials ) ) {
        const auto* session = bySerial( running, serial );
        if ( session ) {
            // A poll samples the lines anew; else the last poll's sample
            // stays.  A stopped session's sample is dropped: started again,
            // it counts anew.
            const auto before = previous.constFind( serial );
            const auto hasBefore = before != previous.cend();
            if ( pollAtMs ) {
                CounterSample sample;
                sample.lines = session->counters.lines;
                sample.atMs = *pollAtMs;
                sample.rate = hasBefore ? ratePerSecond( sample.lines - before->lines,
                                                         *pollAtMs - before->atMs )
                                        : 0;
                roster.samples.insert( serial, sample );
            }
            else if ( hasBefore ) {
                roster.samples.insert( serial, *before );
            }
        }
        roster.rows << rowOf( serial, bySerial( attached, serial ), bySerial( known, serial ),
                              session, roster.samples.value( serial ).rate, now );
    }
    return roster;
}

} // namespace logcat
