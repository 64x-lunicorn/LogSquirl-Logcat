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
 * Sessions, the devices seen before in the DeviceStore, what a tile shows
 * in the device roster; the tab shows the roster's rows and calls the
 * others.
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
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace logcat {

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

    // Each handler runs through guarded(): an exception must not unwind
    // through Qt's event loop into the host.
    connect( settings, &QPushButton::clicked, this,
             [ this ] { guarded( "opening the settings", [ this ] { openSettings(); } ); } );
    connect( stopAll_, &QPushButton::clicked, this, [ this ] {
        guarded( "stopping all captures", [ this ] {
            sessions_->stopAll();
            hostNotify( "All logcat sessions stopped." );
        } );
    } );

    // ── Follow the devices and the sessions ──────────────────────────
    connect( sessions_, &Sessions::devicesChanged, this,
             [ this ] { guarded( "showing the devices", [ this ] { onDevicesChanged(); } ); } );
    connect( sessions_, &Sessions::sessionStarted, this,
             [ this ] { guarded( "showing a started capture", [ this ] { reload(); } ); } );
    connect( sessions_, &Sessions::sessionStopped, this,
             [ this ] { guarded( "showing a stopped capture", [ this ] { reload(); } ); } );
    connect( sessions_, &Sessions::sessionChanged, this,
             [ this ] { guarded( "showing a rotated log", [ this ] { reload(); } ); } );
    connect( store_, &DeviceStore::changed, this,
             [ this ] { guarded( "showing the known devices", [ this ] { reload(); } ); } );

    // A device plugged in or unplugged shows by itself, while the tab shows
    // (showEvent()).
    scanTimer_->setObjectName( "deviceScan" );
    scanTimer_->setInterval( kScanIntervalMs );
    connect( scanTimer_, &QTimer::timeout, this, [ this ] {
        guarded( "refreshing the devices", [ this ] { sessions_->refreshDevices(); } );
    } );

    // A running session's lines emit no signal: its tile polls them, while
    // the tab shows.
    pollTimer_->setObjectName( "counterPoll" );
    pollTimer_->setInterval( kCounterPollMs );
    connect( pollTimer_, &QTimer::timeout, this,
             [ this ] { guarded( "showing the line counts", [ this ] { pollCounters(); } ); } );

    clock_.start();

    // Sessions has started the first device scan; devicesChanged() fills
    // in the result.
    onDevicesChanged();
}

// ── Showing the devices ─────────────────────────────────────────────────

void SidebarWidget::showEvent( QShowEvent* event )
{
    QWidget::showEvent( event );
    // The devices and the counters may have changed while it was hidden.
    guarded( "showing the Logcat tab", [ this ] {
        sessions_->refreshDevices();
        pollCounters();
        scanTimer_->start();
        pollTimer_->start();
    } );
}

void SidebarWidget::hideEvent( QHideEvent* event )
{
    QWidget::hideEvent( event );
    guarded( "hiding the Logcat tab", [ this ] {
        scanTimer_->stop();
        pollTimer_->stop();
    } );
}

void SidebarWidget::onDevicesChanged()
{
    store_->remember( sessions_->attachedDevices() );
    reload();
}

void SidebarWidget::reload()
{
    showRoster( std::nullopt );
}

void SidebarWidget::pollCounters()
{
    showRoster( clock_.elapsed() );
}

void SidebarWidget::showRoster( std::optional<qint64> pollAtMs )
{
    QList<RunningSession> running;
    const auto active = sessions_->activeSerials();
    for ( const auto& serial : active ) {
        running.append( { serial, sessions_->filePath( serial ), sessions_->counters( serial ) } );
    }
    const auto roster = deviceRoster( store_->list(), sessions_->attachedDevices(), running,
                                      samples_, QDateTime::currentDateTime(), pollAtMs );
    samples_ = roster.samples;

    QStringList serials;
    for ( const auto& row : roster.rows ) {
        serials << row.serial;
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

    for ( int i = 0; i < roster.rows.size(); ++i ) {
        const auto& row = roster.rows[ i ];
        auto* tile = tileFor( row.serial );
        if ( tiles_->indexOf( tile ) != i ) {
            tiles_->removeWidget( tile );
            tiles_->insertWidget( i, tile );
        }
        tile->setTitle( row.title );
        tile->setState( row.state );
        tile->setSummary( row.summary );
        tile->setDetails( row.details );
    }

    empty_->setVisible( roster.rows.isEmpty() );
    stopAll_->setEnabled( !running.isEmpty() );
}

DeviceTile* SidebarWidget::tileFor( const QString& serial )
{
    if ( auto* tile = tileBySerial_.value( serial ) ) {
        return tile;
    }
    auto* tile = new DeviceTile( serial, this );
    connect( tile, &DeviceTile::startClicked, this, [ this, serial ] {
        guarded( "starting a capture", [ this, &serial ] { start( serial ); } );
    } );
    connect( tile, &DeviceTile::stopClicked, this, [ this, serial ] {
        guarded( "stopping a capture", [ this, &serial ] { sessions_->stopSession( serial ); } );
    } );
    connect( tile, &DeviceTile::rotateClicked, this, [ this, serial ] {
        guarded( "rotating a log", [ this, &serial ] { sessions_->rotateSession( serial ); } );
    } );
    connect( tile, &DeviceTile::savePathClicked, this, [ this, serial ] {
        guarded( "editing a save path", [ this, &serial ] { editSavePath( serial ); } );
    } );
    connect( tile, &DeviceTile::forgetClicked, this, [ this, serial ] {
        guarded( "forgetting a device", [ this, &serial ] { store_->forget( serial ); } );
    } );
    tileBySerial_.insert( serial, tile );
    return tile;
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

    // Each handler runs through guarded(), as the constructor's.
    connect( buttons, &QDialogButtonBox::accepted, dialog,
             [ dialog ] { guarded( "saving the save path", [ dialog ] { dialog->accept(); } ); } );
    connect( buttons, &QDialogButtonBox::rejected, dialog, [ dialog ] {
        guarded( "cancelling the save path dialog", [ dialog ] { dialog->reject(); } );
    } );
    connect( browse, &QPushButton::clicked, dialog, [ dialog, edit ] {
        guarded( "choosing a save path", [ dialog, edit ] {
            // An existing file is appended to, not replaced, so don't ask to replace it.
            const auto path = QFileDialog::getSaveFileName(
                dialog, QStringLiteral( "Save logcat output" ), edit->text(),
                QStringLiteral( "Log files (*.log *.txt);;All files (*)" ), nullptr,
                QFileDialog::DontConfirmOverwrite );
            if ( !path.isEmpty() ) {
                edit->setText( path );
            }
        } );
    } );
    connect( dialog, &QDialog::accepted, this, [ this, serial, edit ] {
        guarded( "saving a save path", [ this, &serial, edit ] {
            store_->setSavePath( serial, edit->text().trimmed() );
        } );
    } );

    dialog->open();
}

void SidebarWidget::openSettings()
{
    auto* dialog = new SettingsDialog( store_, sessions_, this );
    dialog->setAttribute( Qt::WA_DeleteOnClose );
    dialog->open();
}

} // namespace logcat
