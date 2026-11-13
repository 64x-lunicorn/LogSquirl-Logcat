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
 * @file devicestore.cpp
 * @brief Implementation of DeviceStore.
 */

#include "devicestore.h"
#include "logfile.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

namespace logcat {

namespace {

constexpr auto kDevices = "devices";
constexpr auto kLogDir = "logcat/logDir";

} // namespace

DeviceStore::DeviceStore( const QString& settingsFile, QObject* parent )
    : QObject( parent )
    , settingsFile_( settingsFile )
{
}

QList<KnownDevice> DeviceStore::list() const
{
    QList<KnownDevice> devices;
    if ( settingsFile_.isEmpty() ) {
        return devices;
    }
    QSettings settings( settingsFile_, QSettings::IniFormat );
    const int size = settings.beginReadArray( kDevices );
    for ( int i = 0; i < size; ++i ) {
        settings.setArrayIndex( i );
        KnownDevice device;
        device.serial = settings.value( "serial" ).toString();
        device.model = settings.value( "model" ).toString();
        device.savePath = settings.value( "savePath" ).toString();
        if ( !device.serial.isEmpty() ) {
            devices.append( device );
        }
    }
    settings.endArray();
    return devices;
}

std::optional<KnownDevice> DeviceStore::find( const QString& serial ) const
{
    const auto devices = list();
    const auto it
        = std::find_if( devices.cbegin(), devices.cend(),
                        [ &serial ]( const KnownDevice& d ) { return d.serial == serial; } );
    if ( it == devices.cend() ) {
        return std::nullopt;
    }
    return *it;
}

void DeviceStore::remember( const QList<Device>& attached )
{
    if ( settingsFile_.isEmpty() ) {
        return;
    }
    auto devices = list();
    bool changed = false;
    for ( const auto& device : attached ) {
        const auto it
            = std::find_if( devices.begin(), devices.end(), [ &device ]( const KnownDevice& d ) {
                  return d.serial == device.serial;
              } );
        if ( it == devices.end() ) {
            devices.append( { device.serial, device.model, {} } );
            changed = true;
        }
        else if ( !device.model.isEmpty() && it->model != device.model ) {
            it->model = device.model;
            changed = true;
        }
    }
    if ( changed ) {
        write( devices );
        Q_EMIT this->changed();
    }
}

void DeviceStore::forget( const QString& serial )
{
    auto devices = list();
    const auto removed
        = devices.removeIf( [ &serial ]( const KnownDevice& d ) { return d.serial == serial; } );
    if ( removed > 0 ) {
        write( devices );
        Q_EMIT changed();
    }
}

void DeviceStore::setSavePath( const QString& serial, const QString& path )
{
    auto devices = list();
    for ( auto& device : devices ) {
        if ( device.serial == serial && device.savePath != path ) {
            device.savePath = path;
            write( devices );
            Q_EMIT changed();
            return;
        }
    }
}

QString DeviceStore::logDir() const
{
    if ( settingsFile_.isEmpty() ) {
        return {};
    }
    const QSettings settings( settingsFile_, QSettings::IniFormat );
    return settings.value( kLogDir ).toString();
}

void DeviceStore::setLogDir( const QString& dir )
{
    if ( settingsFile_.isEmpty() ) {
        return;
    }
    QSettings settings( settingsFile_, QSettings::IniFormat );
    settings.setValue( kLogDir, dir.trimmed() );
}

QString DeviceStore::savePathFor( const QString& serial ) const
{
    if ( const auto known = find( serial ); known && !known->savePath.isEmpty() ) {
        return known->savePath;
    }

    auto dir = logDir().trimmed();
    if ( dir.isEmpty() ) {
        // Default to logs/ beside the settings, so that captures are kept.
        if ( settingsFile_.isEmpty() ) {
            return {};
        }
        dir = QFileInfo( settingsFile_ ).absolutePath() + "/logs";
    }

    // Ensure the directory exists.  A relative one is left as it is:
    // AdbProcess::start() refuses it with a message, rather than create it
    // in LogSquirl's working directory.
    if ( const auto absolute = LogFile::absoluteUserPath( dir ); !absolute.isEmpty() ) {
        dir = absolute;
        QDir().mkpath( dir );
    }

    // Format: YYYY-MM-dd_HHmmss_<serial>.log
    return LogFile::generateLogPath( dir, serial );
}

void DeviceStore::write( const QList<KnownDevice>& devices )
{
    QSettings settings( settingsFile_, QSettings::IniFormat );
    // Remove the array first: a shorter one would keep the entries after it.
    settings.remove( kDevices );
    settings.beginWriteArray( kDevices, static_cast<int>( devices.size() ) );
    for ( int i = 0; i < devices.size(); ++i ) {
        settings.setArrayIndex( i );
        settings.setValue( "serial", devices[ i ].serial );
        settings.setValue( "model", devices[ i ].model );
        settings.setValue( "savePath", devices[ i ].savePath );
    }
    settings.endArray();
}

} // namespace logcat
