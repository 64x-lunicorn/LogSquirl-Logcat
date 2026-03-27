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
 * @brief Implementation of the sidebar panel for logcat session management.
 *
 * The sidebar panel is a compact view designed to live in the LogSquirl
 * sidebar QTabWidget.  It shows:
 *
 *   1. A device selector (combo + refresh) with Start / Stop buttons.
 *   2. An active-sessions list with per-session line count, rotate,
 *      and stop buttons.
 *   3. A "Stop All" button when multiple sessions are running.
 *
 * Session logic is delegated entirely to DeviceWidget so that the
 * dialog and sidebar remain in sync at all times.
 */

#include "sidebarwidget.h"
#include "devicewidget.h"
#include "plugin.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>

namespace logcat {

// ── Construction ────────────────────────────────────────────────────────

SidebarWidget::SidebarWidget( DeviceWidget* deviceWidget, QWidget* parent )
    : QWidget( parent )
    , deviceWidget_( deviceWidget )
{
    auto* mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 4, 4, 4, 4 );
    mainLayout->setSpacing( 6 );

    // ── Device selector ──────────────────────────────────────────────
    auto* deviceGroup = new QGroupBox( "Device", this );
    auto* deviceLayout = new QVBoxLayout( deviceGroup );
    deviceLayout->setContentsMargins( 6, 6, 6, 6 );

    auto* deviceRow = new QHBoxLayout();
    deviceCombo_ = new QComboBox( this );
    deviceCombo_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    deviceCombo_->setToolTip( "Select an ADB device" );
    deviceRow->addWidget( deviceCombo_ );

    refreshButton_ = new QPushButton( "⟳", this );
    refreshButton_->setFixedWidth( 30 );
    refreshButton_->setToolTip( "Refresh device list" );
    deviceRow->addWidget( refreshButton_ );
    deviceLayout->addLayout( deviceRow );

    auto* buttonRow = new QHBoxLayout();
    startButton_ = new QPushButton( "▶ Start", this );
    startButton_->setToolTip( "Start logcat capture for the selected device" );
    buttonRow->addWidget( startButton_ );

    stopButton_ = new QPushButton( "■ Stop", this );
    stopButton_->setToolTip( "Stop logcat capture for the selected device" );
    buttonRow->addWidget( stopButton_ );
    deviceLayout->addLayout( buttonRow );

    mainLayout->addWidget( deviceGroup );

    // ── Log directory ────────────────────────────────────────────────
    auto* logDirGroup = new QGroupBox( "Log Directory", this );
    auto* logDirLayout = new QHBoxLayout( logDirGroup );
    logDirLayout->setContentsMargins( 6, 6, 6, 6 );

    logDirEdit_ = new QLineEdit( this );
    logDirEdit_->setPlaceholderText( "(logs saved to temp directory)" );
    logDirEdit_->setToolTip(
        "Directory where log files are saved.\n"
        "Files are named: YYYY-MM-dd_HHmmss_<device>.log" );
    logDirLayout->addWidget( logDirEdit_ );

    logDirBrowseButton_ = new QPushButton( "…", this );
    logDirBrowseButton_->setFixedWidth( 30 );
    logDirBrowseButton_->setToolTip( "Browse for log directory" );
    logDirLayout->addWidget( logDirBrowseButton_ );

    mainLayout->addWidget( logDirGroup );

    // ── Active sessions ──────────────────────────────────────────────
    auto* sessionsGroup = new QGroupBox( "Active Sessions", this );
    auto* sessionsLayout = new QVBoxLayout( sessionsGroup );
    sessionsLayout->setContentsMargins( 6, 6, 6, 6 );

    sessionList_ = new QListWidget( this );
    sessionList_->setAlternatingRowColors( true );
    sessionList_->setSelectionMode( QAbstractItemView::NoSelection );
    sessionsLayout->addWidget( sessionList_ );

    stopAllButton_ = new QPushButton( "■ Stop All", this );
    stopAllButton_->setToolTip( "Stop all active logcat sessions" );
    sessionsLayout->addWidget( stopAllButton_ );

    mainLayout->addWidget( sessionsGroup );

    // ── Status ───────────────────────────────────────────────────────
    statusLabel_ = new QLabel( this );
    statusLabel_->setStyleSheet( "color: gray; font-style: italic;" );
    mainLayout->addWidget( statusLabel_ );

    mainLayout->addStretch();

    // ── Connections ──────────────────────────────────────────────────
    connect( refreshButton_, &QPushButton::clicked,
             this, &SidebarWidget::refreshDevices );
    connect( startButton_, &QPushButton::clicked,
             this, &SidebarWidget::startCapture );
    connect( stopButton_, &QPushButton::clicked,
             this, &SidebarWidget::stopSelectedCapture );
    connect( stopAllButton_, &QPushButton::clicked,
             this, &SidebarWidget::stopAllCaptures );
    connect( logDirBrowseButton_, &QPushButton::clicked, this, [this]() {
        const auto dir = QFileDialog::getExistingDirectory(
            this, "Select Log Directory", logDirEdit_->text() );
        if ( !dir.isEmpty() ) {
            logDirEdit_->setText( dir );
            saveLogDir();
        }
    } );
    connect( logDirEdit_, &QLineEdit::editingFinished,
             this, &SidebarWidget::saveLogDir );
    connect( deviceCombo_, &QComboBox::currentIndexChanged,
             this, [this]() { updateUiState(); } );

    // Periodic refresh of line counts in the session list (every 1 second)
    refreshTimer_ = new QTimer( this );
    refreshTimer_->setInterval( 1000 );
    connect( refreshTimer_, &QTimer::timeout,
             this, &SidebarWidget::refreshSessionList );
    refreshTimer_->start();

    // Initial populate
    loadLogDir();
    refreshDevices();
    updateUiState();
}

// ── Private slots ───────────────────────────────────────────────────────

void SidebarWidget::refreshDevices()
{
    const auto currentSelection = currentSerial();
    deviceCombo_->clear();

    const auto devices = AdbProcess::discoverDevices();
    if ( devices.isEmpty() ) {
        deviceCombo_->addItem( "(no devices)" );
        deviceCombo_->setEnabled( false );
    }
    else {
        deviceCombo_->setEnabled( true );
        for ( const auto& serial : devices ) {
            if ( deviceWidget_->isSessionActive( serial ) ) {
                deviceCombo_->addItem( serial + " ●", serial );
            }
            else {
                deviceCombo_->addItem( serial, serial );
            }
        }

        const auto idx = deviceCombo_->findData( currentSelection );
        if ( idx >= 0 ) {
            deviceCombo_->setCurrentIndex( idx );
        }
    }

    rebuildSessionList();
    updateUiState();
}

void SidebarWidget::startCapture()
{
    const auto serial = currentSerial();
    if ( serial.isEmpty() ) {
        return;
    }

    const auto savePath = generateSavePath( serial );
    deviceWidget_->startSession( serial, savePath );
    refreshDevices();
}

void SidebarWidget::stopSelectedCapture()
{
    const auto serial = currentSerial();
    if ( serial.isEmpty() ) {
        return;
    }

    deviceWidget_->stopSession( serial );
    refreshDevices();
}

void SidebarWidget::stopAllCaptures()
{
    deviceWidget_->stopAll();

    if ( g_state.api && g_state.handle ) {
        g_state.api->show_notification( g_state.handle, "All logcat sessions stopped." );
    }

    refreshDevices();
}

void SidebarWidget::refreshSessionList()
{
    // Only rebuild when the active session count changes
    const int currentCount = deviceWidget_->activeSessionCount();
    if ( sessionList_->count() != currentCount ) {
        rebuildSessionList();
        updateUiState();
        return;
    }

    // Names don't change — nothing to refresh for existing items
}

// ── Private helpers ─────────────────────────────────────────────────────

void SidebarWidget::rebuildSessionList()
{
    sessionList_->clear();

    const auto serials = deviceWidget_->activeSerials();
    for ( const auto& serial : serials ) {
        auto* item = new QListWidgetItem();
        sessionList_->addItem( item );

        // Create a widget with Rotate and Stop buttons for this session
        auto* row = new QWidget( sessionList_ );
        auto* rowLayout = new QHBoxLayout( row );
        rowLayout->setContentsMargins( 2, 2, 2, 2 );
        rowLayout->setSpacing( 4 );

        auto* label = new QLabel( serial, row );
        label->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
        rowLayout->addWidget( label );

        auto* rotateBtn = new QPushButton( "↻", row );
        rotateBtn->setFixedSize( 26, 26 );
        rotateBtn->setToolTip( "Rotate log (start new session)" );
        rowLayout->addWidget( rotateBtn );

        auto* stopBtn = new QPushButton( "■", row );
        stopBtn->setFixedSize( 26, 26 );
        stopBtn->setToolTip( "Stop this session" );
        rowLayout->addWidget( stopBtn );

        connect( rotateBtn, &QPushButton::clicked, this,
                 [this, serial]() {
                     deviceWidget_->rotateSession( serial );
                     refreshDevices();
                 } );

        connect( stopBtn, &QPushButton::clicked, this,
                 [this, serial]() {
                     deviceWidget_->stopSession( serial );
                     refreshDevices();
                 } );

        // Replace the plain text item with the custom widget
        item->setSizeHint( row->sizeHint() );
        item->setText( {} );
        sessionList_->setItemWidget( item, row );
    }
}

QString SidebarWidget::currentSerial() const
{
    const auto data = deviceCombo_->currentData();
    return data.isValid() ? data.toString() : QString();
}

void SidebarWidget::updateUiState()
{
    const auto serial = currentSerial();
    const bool hasDevice = !serial.isEmpty();
    const bool isActive = hasDevice && deviceWidget_->isSessionActive( serial );
    const int activeCount = deviceWidget_->activeSessionCount();

    startButton_->setEnabled( hasDevice && !isActive );
    stopButton_->setEnabled( isActive );
    stopAllButton_->setEnabled( activeCount > 0 );
    stopAllButton_->setVisible( activeCount > 1 );

    if ( activeCount == 0 ) {
        statusLabel_->setText( {} );
    }
    else {
        statusLabel_->setText(
            QString( "%1 active session(s)" ).arg( activeCount ) );
    }
}

QString SidebarWidget::generateSavePath( const QString& serial ) const
{
    const auto dir = logDirEdit_->text().trimmed();
    if ( dir.isEmpty() ) {
        return {};
    }

    // Ensure the directory exists
    QDir().mkpath( dir );

    // Format: YYYY-MM-dd_HHmmss_<serial>.log
    const auto timestamp = QDateTime::currentDateTime().toString( "yyyy-MM-dd_HHmmss" );
    // Sanitise the serial for use as a filename component
    auto safeName = serial;
    safeName.replace( QRegularExpression( "[^a-zA-Z0-9._-]" ), "_" );
    return QDir( dir ).filePath(
        QString( "%1_%2.log" ).arg( timestamp, safeName ) );
}

void SidebarWidget::loadLogDir()
{
    const auto cfgDir = AdbProcess::configDir();
    if ( cfgDir.isEmpty() ) {
        return;
    }
    QSettings settings( cfgDir + "/logcat.ini", QSettings::IniFormat );
    logDirEdit_->setText( settings.value( "logcat/logDir" ).toString() );
}

void SidebarWidget::saveLogDir()
{
    const auto cfgDir = AdbProcess::configDir();
    if ( cfgDir.isEmpty() ) {
        return;
    }
    QSettings settings( cfgDir + "/logcat.ini", QSettings::IniFormat );
    settings.setValue( "logcat/logDir", logDirEdit_->text().trimmed() );
}

} // namespace logcat
