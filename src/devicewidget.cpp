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
 * @file devicewidget.cpp
 * @brief Implementation of the DeviceWidget dialog.
 *
 * The dialog only shows the plugin's Sessions (sessions.h) and calls it:
 * its buttons start, stop and rescan through Sessions, and Sessions'
 * signals refill the device combo box and the buttons.
 */

#include "devicewidget.h"
#include "adbprocess.h"
#include "plugin.h"
#include "sessions.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QSettings>
#include <QVBoxLayout>

namespace logcat {

// ── Construction ────────────────────────────────────────────────────────

DeviceWidget::DeviceWidget( Sessions* sessions, QWidget* parent )
    : QDialog( parent )
    , sessions_( sessions )
{
    setWindowTitle( "Android Logcat" );
    setMinimumWidth( 420 );

    // A top-level window of the plugin's own: when it is open while the
    // user closes LogSquirl's main window, LogSquirl must still quit.
    setAttribute( Qt::WA_QuitOnClose, false );

    auto* mainLayout = new QVBoxLayout( this );

    // ── Device group ─────────────────────────────────────────────────
    auto* deviceGroup = new QGroupBox( "Device", this );
    auto* deviceLayout = new QVBoxLayout( deviceGroup );

    auto* deviceRow = new QHBoxLayout();
    deviceCombo_ = new QComboBox( this );
    deviceCombo_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    deviceCombo_->setToolTip( "Select an ADB device" );
    deviceRow->addWidget( deviceCombo_ );

    refreshButton_ = new QPushButton( "⟳ Refresh", this );
    refreshButton_->setObjectName( "refresh" );
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

    stopAllButton_ = new QPushButton( "■ Stop All", this );
    stopAllButton_->setToolTip( "Stop all active logcat sessions" );
    buttonRow->addWidget( stopAllButton_ );

    buttonRow->addStretch();
    deviceLayout->addLayout( buttonRow );

    mainLayout->addWidget( deviceGroup );

    // ── Save-to-file group ───────────────────────────────────────────
    auto* saveGroup = new QGroupBox( "Save to file", this );
    auto* saveLayout = new QHBoxLayout( saveGroup );

    saveCheckBox_ = new QCheckBox( "Enable", this );
    saveCheckBox_->setToolTip( "Also save logcat output to a file" );
    saveLayout->addWidget( saveCheckBox_ );

    savePathEdit_ = new QLineEdit( this );
    savePathEdit_->setPlaceholderText( "/path/to/output.log" );
    savePathEdit_->setEnabled( false );
    saveLayout->addWidget( savePathEdit_ );

    browseButton_ = new QPushButton( "…", this );
    browseButton_->setFixedWidth( 30 );
    browseButton_->setEnabled( false );
    browseButton_->setToolTip( "Browse for a save file location" );
    saveLayout->addWidget( browseButton_ );

    mainLayout->addWidget( saveGroup );

    // ── ADB configuration group ──────────────────────────────────────
    auto* adbGroup = new QGroupBox( "ADB", this );
    auto* adbLayout = new QHBoxLayout( adbGroup );

    // A path from the configuration is shown as it is, never as rich text.
    adbPathLabel_ = new QLabel( this );
    adbPathLabel_->setTextFormat( Qt::PlainText );
    adbPathLabel_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    adbLayout->addWidget( adbPathLabel_, 1 );

    adbConfigButton_ = new QPushButton( "Configure…", this );
    adbConfigButton_->setToolTip( "Change the ADB executable path" );
    adbLayout->addWidget( adbConfigButton_ );

    mainLayout->addWidget( adbGroup );

    // ── Status line ──────────────────────────────────────────────────
    statusLabel_ = new QLabel( this );
    statusLabel_->setStyleSheet( "color: gray; font-style: italic;" );
    mainLayout->addWidget( statusLabel_ );

    mainLayout->addStretch();

    // ── Connect signals ──────────────────────────────────────────────
    connect( refreshButton_, &QPushButton::clicked, sessions_, &Sessions::refreshDevices );
    connect( startButton_, &QPushButton::clicked, this, &DeviceWidget::startCapture );
    connect( stopButton_, &QPushButton::clicked, this, &DeviceWidget::stopCapture );
    connect( stopAllButton_, &QPushButton::clicked, this, &DeviceWidget::stopAllCaptures );
    connect( browseButton_, &QPushButton::clicked, this, &DeviceWidget::browseSavePath );
    connect( adbConfigButton_, &QPushButton::clicked, this,
             [ this ]() { configureAdbPath( this ); } );
    connect( saveCheckBox_, &QCheckBox::toggled, savePathEdit_, &QLineEdit::setEnabled );
    connect( saveCheckBox_, &QCheckBox::toggled, browseButton_, &QPushButton::setEnabled );
    connect( deviceCombo_, &QComboBox::currentIndexChanged, this, [ this ]() { updateUiState(); } );

    // ── Follow the sessions ──────────────────────────────────────────
    connect( sessions_, &Sessions::devicesChanged, this, &DeviceWidget::updateDeviceCombo );
    connect( sessions_, &Sessions::scanningChanged, this, &DeviceWidget::updateRefreshButton );
    connect( sessions_, &Sessions::sessionStarted, this, &DeviceWidget::updateDeviceCombo );
    connect( sessions_, &Sessions::sessionStopped, this, &DeviceWidget::updateDeviceCombo );

    // Sessions has started the first device scan; devicesChanged() fills
    // in the result.
    updateDeviceCombo();
    updateRefreshButton();
}

// ── Private slots ───────────────────────────────────────────────────────

void DeviceWidget::updateRefreshButton()
{
    const bool scanning = sessions_->isScanning();
    refreshButton_->setEnabled( !scanning );
    refreshButton_->setText( scanning ? "Scanning…" : "⟳ Refresh" );
}

void DeviceWidget::updateDeviceCombo()
{
    const auto currentSelection = currentSerial();
    deviceCombo_->clear();

    const auto& devices = sessions_->devices();
    if ( devices.isEmpty() ) {
        deviceCombo_->addItem( "(no devices)" );
        deviceCombo_->setEnabled( false );
    }
    else {
        deviceCombo_->setEnabled( true );
        for ( const auto& serial : devices ) {
            // Mark devices that already have an active session
            if ( sessions_->isSessionActive( serial ) ) {
                deviceCombo_->addItem( serial + " ●", serial );
            }
            else {
                deviceCombo_->addItem( serial, serial );
            }
        }

        // Restore previous selection if still available
        const auto idx = deviceCombo_->findData( currentSelection );
        if ( idx >= 0 ) {
            deviceCombo_->setCurrentIndex( idx );
        }
    }

    updateUiState();
}

void DeviceWidget::startCapture()
{
    const auto serial = currentSerial();
    if ( serial.isEmpty() ) {
        return;
    }

    // Don't start twice for the same device
    if ( sessions_->isSessionActive( serial ) ) {
        hostLog( LOGSQUIRL_LOG_WARNING, "Logcat already running for " + serial );
        return;
    }

    // Determine save path (empty string disables saving)
    const auto savePath = ( saveCheckBox_->isChecked() && !savePathEdit_->text().isEmpty() )
                              ? savePathEdit_->text()
                              : QString();

    sessions_->startSession( serial, savePath );
}

void DeviceWidget::stopCapture()
{
    sessions_->stopSession( currentSerial() );
}

void DeviceWidget::stopAllCaptures()
{
    sessions_->stopAll();

    hostNotify( "All logcat sessions stopped." );
}

void DeviceWidget::browseSavePath()
{
    // An existing file is appended to, not replaced, so don't ask to replace it.
    const auto path = QFileDialog::getSaveFileName(
        this, "Save logcat output", savePathEdit_->text(), "Log files (*.log *.txt);;All files (*)",
        nullptr, QFileDialog::DontConfirmOverwrite );

    if ( !path.isEmpty() ) {
        savePathEdit_->setText( path );
    }
}

// ── Private helpers ─────────────────────────────────────────────────────

void DeviceWidget::updateUiState()
{
    const auto serial = currentSerial();
    const bool hasDevice = !serial.isEmpty();
    const bool isActive = hasDevice && sessions_->isSessionActive( serial );
    const int activeCount = sessions_->activeSessionCount();

    startButton_->setEnabled( hasDevice && !isActive );
    stopButton_->setEnabled( isActive );
    stopAllButton_->setEnabled( activeCount > 0 );
    stopAllButton_->setVisible( activeCount > 1 );

    if ( activeCount == 0 ) {
        statusLabel_->setText( "" );
    }
    else {
        statusLabel_->setText( QString( "%1 active session(s)" ).arg( activeCount ) );
    }

    // Show the current ADB path
    const auto adbPath = AdbProcess::findAdb();
    adbPathLabel_->setText( adbPath.isEmpty() ? "(not found)" : adbPath );
}

void DeviceWidget::configureAdbPath( QWidget* parent )
{
    const auto configDir = AdbProcess::configDir();
    if ( configDir.isEmpty() ) {
        hostLog( LOGSQUIRL_LOG_WARNING, "The ADB path cannot be configured: no config directory." );
        return;
    }
    QSettings settings( configDir + "/logcat.ini", QSettings::IniFormat );
    const auto currentPath = settings.value( "adb/path", "" ).toString();
    const auto detected = AdbProcess::findAdb();

    const auto prompt = QString( "ADB executable path:\n\n"
                                 "Detected: %1\nCurrent override: %2\n\n"
                                 "Leave empty to use auto-detection." )
                            .arg( detected.isEmpty() ? "(not found)" : detected,
                                  currentPath.isEmpty() ? "(none)" : currentPath );

    bool ok = false;
    const auto newPath = QInputDialog::getText( parent, "Configure ADB Path", prompt,
                                                QLineEdit::Normal, currentPath, &ok );

    if ( ok ) {
        settings.setValue( "adb/path", newPath );
        hostLog( LOGSQUIRL_LOG_INFO, newPath.isEmpty()
                                         ? "ADB path override cleared — using auto-detection."
                                         : "ADB path set to: " + newPath );
        sessions_->restartDeviceScan();
    }
}

QString DeviceWidget::currentSerial() const
{
    const auto data = deviceCombo_->currentData();
    return data.isValid() ? data.toString() : QString();
}

} // namespace logcat
