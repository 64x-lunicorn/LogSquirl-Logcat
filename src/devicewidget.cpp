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
 * ARCHITECTURE OVERVIEW
 * ─────────────────────
 * The dialog maintains a map of serial → AdbProcess*.  When the user
 * clicks "Start":
 *
 *   1. An AdbProcess is created for the selected device serial.
 *   2. AdbProcess::start() launches `adb -s <serial> logcat`.
 *   3. Output is streamed to a temporary file.
 *   4. The host API's open_file() is called with the temp file path
 *      and follow=true, which opens a new tail-mode tab in LogSquirl.
 *   5. The session entry is stored in sessions_.
 *
 * When the user clicks "Stop":
 *   1. AdbProcess::stop() terminates the child process.
 *   2. The session is removed from sessions_.
 *   3. The LogSquirl tab remains open (the user can close it manually).
 *
 * Multiple devices can run simultaneously — each with its own tab.
 */

#include "devicewidget.h"
#include "plugin.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>

namespace logcat {

// ── Construction ────────────────────────────────────────────────────────

DeviceWidget::DeviceWidget( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( "Android Logcat" );
    setMinimumWidth( 420 );

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

    adbPathLabel_ = new QLabel( this );
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
    connect( refreshButton_, &QPushButton::clicked,
             this, &DeviceWidget::refreshDevices );
    connect( startButton_, &QPushButton::clicked,
             this, &DeviceWidget::startCapture );
    connect( stopButton_, &QPushButton::clicked,
             this, &DeviceWidget::stopCapture );
    connect( stopAllButton_, &QPushButton::clicked,
             this, &DeviceWidget::stopAllCaptures );
    connect( browseButton_, &QPushButton::clicked,
             this, &DeviceWidget::browseSavePath );
    connect( adbConfigButton_, &QPushButton::clicked,
             this, &DeviceWidget::configureAdbPath );
    connect( saveCheckBox_, &QCheckBox::toggled,
             savePathEdit_, &QLineEdit::setEnabled );
    connect( saveCheckBox_, &QCheckBox::toggled,
             browseButton_, &QPushButton::setEnabled );
    connect( deviceCombo_, &QComboBox::currentIndexChanged,
             this, [this]() { updateUiState(); } );

    // Initial device scan
    refreshDevices();
    updateUiState();
}

// ── Public methods ──────────────────────────────────────────────────────

void DeviceWidget::stopAll()
{
    const auto serials = sessions_.keys();
    for ( const auto& serial : serials ) {
        if ( auto* proc = sessions_.value( serial ) ) {
            proc->stop();
            proc->preserveTempFile();
            proc->deleteLater();
        }
    }
    sessions_.clear();
    updateUiState();
}

int DeviceWidget::activeSessionCount() const
{
    return sessions_.size();
}

// ── Private slots ───────────────────────────────────────────────────────

void DeviceWidget::refreshDevices()
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
            // Mark devices that already have an active session
            if ( sessions_.contains( serial ) ) {
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
    if ( sessions_.contains( serial ) ) {
        hostLog( LOGSQUIRL_LOG_WARNING,
                 qPrintable( "Logcat already running for " + serial ) );
        return;
    }

    // Determine save path (empty string disables saving)
    const auto savePath = ( saveCheckBox_->isChecked() && !savePathEdit_->text().isEmpty() )
                              ? savePathEdit_->text()
                              : QString();

    // Create and start the ADB process
    auto* proc = new AdbProcess( serial, savePath, this );

    connect( proc, &AdbProcess::started, this, [this, serial]() {
        hostLog( LOGSQUIRL_LOG_INFO,
                 qPrintable( "Logcat session started for " + serial ) );
    } );

    connect( proc, &AdbProcess::finished, this, [this, serial]( int ) {
        onSessionFinished( serial );
    } );

    connect( proc, &AdbProcess::errorOccurred, this, [this, serial]( const QString& msg ) {
        onSessionError( serial, msg );
    } );

    proc->start();

    if ( proc->isRunning() || !proc->tempFilePath().isEmpty() ) {
        sessions_.insert( serial, proc );

        // Ask the host to open the temp file in a follow-mode tab
        if ( g_state.api && g_state.handle ) {
            const auto path = proc->tempFilePath().toUtf8();
            g_state.api->open_file( g_state.handle, path.constData(), 1 );
        }

        // Notify via host notification
        if ( g_state.api && g_state.handle ) {
            g_state.api->show_notification(
                g_state.handle,
                qPrintable( QString( "Logcat started for %1" ).arg( serial ) ) );
        }
    }
    else {
        // start() failed synchronously — proc emitted errorOccurred already
        delete proc;
    }

    refreshDevices(); // Update combo box markers
}

void DeviceWidget::stopCapture()
{
    const auto serial = currentSerial();
    if ( serial.isEmpty() || !sessions_.contains( serial ) ) {
        return;
    }

    auto* proc = sessions_.take( serial );
    proc->stop();
    proc->preserveTempFile();
    proc->deleteLater();

    if ( g_state.api && g_state.handle ) {
        g_state.api->show_notification(
            g_state.handle,
            qPrintable( QString( "Logcat stopped for %1 (%2 lines)" )
                            .arg( serial )
                            .arg( proc->lineCount() ) ) );
    }

    refreshDevices();
}

void DeviceWidget::stopAllCaptures()
{
    stopAll();

    if ( g_state.api && g_state.handle ) {
        g_state.api->show_notification( g_state.handle, "All logcat sessions stopped." );
    }

    refreshDevices();
}

void DeviceWidget::browseSavePath()
{
    const auto path = QFileDialog::getSaveFileName(
        this, "Save logcat output", savePathEdit_->text(),
        "Log files (*.log *.txt);;All files (*)" );

    if ( !path.isEmpty() ) {
        savePathEdit_->setText( path );
    }
}

void DeviceWidget::onSessionFinished( const QString& serial )
{
    if ( sessions_.contains( serial ) ) {
        auto* proc = sessions_.take( serial );
        proc->deleteLater();

        hostLog( LOGSQUIRL_LOG_INFO,
                 qPrintable( QString( "Logcat session for %1 ended." ).arg( serial ) ) );
    }

    refreshDevices();
}

void DeviceWidget::onSessionError( const QString& serial, const QString& message )
{
    hostLog( LOGSQUIRL_LOG_ERROR, qPrintable( serial + ": " + message ) );

    if ( g_state.api && g_state.handle ) {
        g_state.api->show_notification(
            g_state.handle, qPrintable( "Logcat error (" + serial + "): " + message ) );
    }
}

// ── Private helpers ─────────────────────────────────────────────────────

void DeviceWidget::updateUiState()
{
    const auto serial = currentSerial();
    const bool hasDevice = !serial.isEmpty();
    const bool isActive = hasDevice && sessions_.contains( serial );
    const int activeCount = sessions_.size();

    startButton_->setEnabled( hasDevice && !isActive );
    stopButton_->setEnabled( isActive );
    stopAllButton_->setEnabled( activeCount > 0 );
    stopAllButton_->setVisible( activeCount > 1 );

    if ( activeCount == 0 ) {
        statusLabel_->setText( "" );
    }
    else {
        statusLabel_->setText(
            QString( "%1 active session(s)" ).arg( activeCount ) );
    }

    // Show the current ADB path
    const auto adbPath = AdbProcess::findAdb();
    adbPathLabel_->setText( adbPath.isEmpty() ? "(not found)" : adbPath );
}

void DeviceWidget::configureAdbPath()
{
    const auto configDir = AdbProcess::configDir();
    QSettings settings( configDir + "/logcat.ini", QSettings::IniFormat );
    const auto currentPath = settings.value( "adb/path", "" ).toString();
    const auto detected = AdbProcess::findAdb();

    const auto prompt
        = QString( "ADB executable path:\n\n"
                   "Detected: %1\nCurrent override: %2\n\n"
                   "Leave empty to use auto-detection." )
              .arg( detected.isEmpty() ? "(not found)" : detected,
                    currentPath.isEmpty() ? "(none)" : currentPath );

    bool ok = false;
    const auto newPath
        = QInputDialog::getText( this, "Configure ADB Path", prompt,
                                 QLineEdit::Normal, currentPath, &ok );

    if ( ok ) {
        settings.setValue( "adb/path", newPath );
        hostLog( LOGSQUIRL_LOG_INFO,
                 newPath.isEmpty()
                     ? "ADB path override cleared — using auto-detection."
                     : qPrintable( "ADB path set to: " + newPath ) );
        refreshDevices();
    }
}

QString DeviceWidget::currentSerial() const
{
    const auto data = deviceCombo_->currentData();
    return data.isValid() ? data.toString() : QString();
}

} // namespace logcat
