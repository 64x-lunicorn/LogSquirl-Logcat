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
 * @file sidebarwidget.cpp
 * @brief Implementation of the sidebar tab: one tile per device.
 *
 * The tiles are kept across updates — a rescan every few seconds must not
 * rebuild them under the user's mouse — and only added, removed, reordered
 * and updated as the devices and sessions change.  Session logic is in
 * Sessions, the devices seen before in the DeviceStore; the tab shows them
 * and calls them.
 */

#include "sidebarwidget.h"

#include "adbprocess.h"
#include "devicestore.h"
#include "devicetile.h"
#include "plugin.h"
#include "sessions.h"
#include "settingsdialog.h"

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace logcat {

namespace {

/// The longest serial a title shows whole.
constexpr qsizetype kTitleSerialChars = 8;

/// What the tile of a device in @p state is.
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

// ── Construction ────────────────────────────────────────────────────────

SidebarWidget::SidebarWidget( DeviceStore* store, Sessions* sessions, QWidget* parent )
    : QWidget( parent )
    , store_( store )
    , sessions_( sessions )
    , stopAll_( new QPushButton( QStringLiteral( "■ Stop all" ), this ) )
    , empty_( new QLabel(
          QStringLiteral( "No device yet: connect one with USB debugging on, and it shows here." ),
          this ) )
    , tiles_( new QVBoxLayout )
    , scanTimer_( new QTimer( this ) )
    , pollTimer_( new QTimer( this ) )
{
    auto* settings = new QPushButton( QStringLiteral( "Settings…" ), this );
    settings->setObjectName( "settings" );
    settings->setToolTip( QStringLiteral( "The log directory and the adb executable" ) );
    stopAll_->setObjectName( "stopAll" );
    stopAll_->setToolTip( QStringLiteral( "Stop every capture; their tabs keep the files" ) );
    empty_->setObjectName( "empty" );
    empty_->setWordWrap( true );

    auto* buttons = new QHBoxLayout;
    buttons->addWidget( settings );
    buttons->addStretch();
    buttons->addWidget( stopAll_ );

    // The tiles scroll, the buttons stay.
    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout( content );
    contentLayout->setContentsMargins( 0, 0, 0, 0 );
    contentLayout->setSpacing( 8 );
    tiles_->setSpacing( 8 );
    contentLayout->addWidget( empty_ );
    contentLayout->addLayout( tiles_ );
    contentLayout->addStretch();
    auto* scroll = new QScrollArea( this );
    scroll->setWidgetResizable( true );
    scroll->setFrameShape( QFrame::NoFrame );
    scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    scroll->setWidget( content );

    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 4, 4, 4, 4 );
    layout->addLayout( buttons );
    layout->addWidget( scroll, 1 );

    connect( settings, &QPushButton::clicked, this, &SidebarWidget::openSettings );
    connect( stopAll_, &QPushButton::clicked, this, [ this ] {
        sessions_->stopAll();
        hostNotify( "All logcat sessions stopped." );
    } );

    // ── Follow the devices and the sessions ──────────────────────────
    connect( sessions_, &Sessions::devicesChanged, this, &SidebarWidget::onDevicesChanged );
    connect( sessions_, &Sessions::sessionStarted, this, &SidebarWidget::reload );
    connect( sessions_, &Sessions::sessionStopped, this, &SidebarWidget::reload );
    connect( sessions_, &Sessions::sessionChanged, this, &SidebarWidget::reload );
    connect( store_, &DeviceStore::changed, this, &SidebarWidget::reload );

    // A device plugged in or unplugged shows by itself.
    scanTimer_->setObjectName( "deviceScan" );
    scanTimer_->setInterval( kScanIntervalMs );
    connect( scanTimer_, &QTimer::timeout, sessions_, &Sessions::refreshDevices );
    scanTimer_->start();

    // A running session's lines emit no signal: its tile polls them.
    pollTimer_->setObjectName( "counterPoll" );
    pollTimer_->setInterval( kCounterPollMs );
    connect( pollTimer_, &QTimer::timeout, this, &SidebarWidget::pollCounters );
    pollTimer_->start();

    // Sessions has started the first device scan; devicesChanged() fills
    // in the result.
    onDevicesChanged();
}

// ── Showing the devices ─────────────────────────────────────────────────

void SidebarWidget::onDevicesChanged()
{
    store_->remember( sessions_->attachedDevices() );
    reload();
}

void SidebarWidget::reload()
{
    const auto known = store_->list();
    const auto& attached = sessions_->attachedDevices();

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
    const auto active = sessions_->activeSerials();
    for ( const auto& serial : active ) {
        if ( !serials.contains( serial ) ) {
            serials << serial;
        }
    }

    // Later: a tile's own click (Forget) may have led here.
    for ( auto it = tileBySerial_.begin(); it != tileBySerial_.end(); ) {
        if ( serials.contains( it.key() ) ) {
            ++it;
            continue;
        }
        tiles_->removeWidget( *it );
        ( *it )->setParent( nullptr );
        ( *it )->deleteLater();
        it = tileBySerial_.erase( it );
    }

    for ( int i = 0; i < serials.size(); ++i ) {
        const auto& serial = serials[ i ];
        auto* tile = tileFor( serial );
        if ( tiles_->indexOf( tile ) != i ) {
            tiles_->removeWidget( tile );
            tiles_->insertWidget( i, tile );
        }
        const auto attachedIt
            = std::find_if( attached.cbegin(), attached.cend(),
                            [ &serial ]( const Device& d ) { return d.serial == serial; } );
        const auto knownIt
            = std::find_if( known.cbegin(), known.cend(),
                            [ &serial ]( const KnownDevice& d ) { return d.serial == serial; } );
        updateTile( tile, attachedIt != attached.cend() ? &*attachedIt : nullptr,
                    knownIt != known.cend() ? &*knownIt : nullptr );
    }

    empty_->setVisible( serials.isEmpty() );
    stopAll_->setEnabled( !active.isEmpty() );
}

DeviceTile* SidebarWidget::tileFor( const QString& serial )
{
    if ( auto* tile = tileBySerial_.value( serial ) ) {
        return tile;
    }
    auto* tile = new DeviceTile( serial, this );
    connect( tile, &DeviceTile::startClicked, this, [ this, serial ] { start( serial ); } );
    connect( tile, &DeviceTile::stopClicked, this,
             [ this, serial ] { sessions_->stopSession( serial ); } );
    connect( tile, &DeviceTile::rotateClicked, this,
             [ this, serial ] { sessions_->rotateSession( serial ); } );
    connect( tile, &DeviceTile::savePathClicked, this,
             [ this, serial ] { editSavePath( serial ); } );
    connect( tile, &DeviceTile::forgetClicked, this,
             [ this, serial ] { store_->forget( serial ); } );
    tileBySerial_.insert( serial, tile );
    return tile;
}

void SidebarWidget::updateTile( DeviceTile* tile, const Device* attached, const KnownDevice* known )
{
    const auto serial = tile->serial();

    // The model adb names now, or else the one it named before.
    QString model;
    if ( attached && !attached->model.isEmpty() ) {
        model = attached->model;
    }
    else if ( known ) {
        model = known->model;
    }
    tile->setTitle( deviceTitle( serial, model ) );

    TileState state = TileState::Disconnected;
    if ( sessions_->isSessionActive( serial ) ) {
        state = TileState::Running;
    }
    else if ( attached ) {
        state = tileState( attached->state );
    }
    tile->setState( state );

    QStringList summary{ QStringLiteral( "%1 · %2" )
                             .arg( serial,
                                   stateWords( state, attached ? attached->state : QString() ) ) };
    if ( state == TileState::Running ) {
        summary << QStringLiteral( "Writes %1" ).arg( sessions_->filePath( serial ) );
    }
    else if ( known && !known->savePath.isEmpty() ) {
        summary << QStringLiteral( "Saves to %1" ).arg( known->savePath );
    }
    else {
        summary << QStringLiteral( "Saves to the log directory" );
    }
    tile->setSummary( summary );
    updateDetails( tile );
}

void SidebarWidget::updateDetails( DeviceTile* tile )
{
    const auto serial = tile->serial();
    if ( !sessions_->isSessionActive( serial ) ) {
        // A stopped tile shows only the device; a new session counts anew.
        samples_.remove( serial );
        tile->setDetails( {} );
        return;
    }
    tile->setDetails( sessionDetails( sessions_->counters( serial ), samples_.value( serial ).rate,
                                      QDateTime::currentDateTime() ) );
}

void SidebarWidget::pollCounters()
{
    for ( auto* tile : std::as_const( tileBySerial_ ) ) {
        const auto serial = tile->serial();
        if ( sessions_->isSessionActive( serial ) ) {
            const auto lines = sessions_->counters( serial ).lines;
            auto it = samples_.find( serial );
            if ( it == samples_.end() ) {
                it = samples_.insert( serial, {} );
            }
            else {
                it->rate = ratePerSecond( lines - it->lines, it->since.elapsed() );
            }
            it->lines = lines;
            it->since.start();
        }
        updateDetails( tile );
    }
}

// ── Acting on a tile ────────────────────────────────────────────────────

void SidebarWidget::start( const QString& serial )
{
    // Its tile may still show it connected for a moment after it is not.
    if ( !sessions_->devices().contains( serial ) ) {
        return;
    }
    sessions_->startSession( serial, store_->savePathFor( serial ) );
}

void SidebarWidget::editSavePath( const QString& serial )
{
    const auto known = store_->find( serial );

    auto* dialog = new QDialog( this );
    dialog->setObjectName( "savePathDialog" );
    dialog->setAttribute( Qt::WA_DeleteOnClose );
    dialog->setWindowTitle( QStringLiteral( "Save to" ) );
    dialog->setMinimumWidth( 420 );

    auto* prompt = new QLabel( QStringLiteral( "The file the logcat of %1 is appended to:" )
                                   .arg( deviceTitle( serial, known ? known->model : QString() ) ),
                               dialog );
    prompt->setTextFormat( Qt::PlainText );
    prompt->setWordWrap( true );

    auto* edit = new QLineEdit( known ? known->savePath : QString(), dialog );
    edit->setObjectName( "savePath" );
    edit->setPlaceholderText( QStringLiteral( "(a new file in the log directory at each Start)" ) );
    auto* browse = new QPushButton( QStringLiteral( "…" ), dialog );
    browse->setFixedWidth( 30 );
    browse->setToolTip( QStringLiteral( "Browse for the file" ) );
    auto* row = new QHBoxLayout;
    row->addWidget( edit, 1 );
    row->addWidget( browse );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog );
    auto* layout = new QVBoxLayout( dialog );
    layout->addWidget( prompt );
    layout->addLayout( row );
    layout->addWidget( buttons );

    connect( buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject );
    connect( browse, &QPushButton::clicked, dialog, [ dialog, edit ] {
        // An existing file is appended to, not replaced, so don't ask to replace it.
        const auto path = QFileDialog::getSaveFileName(
            dialog, QStringLiteral( "Save logcat output" ), edit->text(),
            QStringLiteral( "Log files (*.log *.txt);;All files (*)" ), nullptr,
            QFileDialog::DontConfirmOverwrite );
        if ( !path.isEmpty() ) {
            edit->setText( path );
        }
    } );
    connect( dialog, &QDialog::accepted, this,
             [ this, serial, edit ] { store_->setSavePath( serial, edit->text().trimmed() ); } );

    dialog->open();
}

void SidebarWidget::openSettings()
{
    auto* dialog = new SettingsDialog( store_, sessions_, this );
    dialog->setAttribute( Qt::WA_DeleteOnClose );
    dialog->open();
}

} // namespace logcat
