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

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <utility>

namespace logcat {

namespace {

/**
 * Whether @p a and @p b name the same file.  Existing files are compared
 * by their canonical path (resolving symlinks); otherwise the cleaned
 * absolute paths are compared.  QFileInfo's own operator== cannot be
 * used: two files that do not exist both have an empty canonical path,
 * and compare equal.
 */
bool isSameFile( const QString& a, const QString& b )
{
    const QFileInfo fileA( a );
    const QFileInfo fileB( b );
    if ( fileA.exists() && fileB.exists() ) {
        return fileA.canonicalFilePath() == fileB.canonicalFilePath();
    }
#if defined( Q_OS_WIN ) || defined( Q_OS_MACOS )
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return QDir::cleanPath( fileA.absoluteFilePath() )
               .compare( QDir::cleanPath( fileB.absoluteFilePath() ), sensitivity )
           == 0;
}

} // namespace

// ── Construction ────────────────────────────────────────────────────────

DeviceWidget::DeviceWidget( QWidget* parent )
    : QDialog( parent )
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
    connect( refreshButton_, &QPushButton::clicked, this, &DeviceWidget::refreshDevices );
    connect( startButton_, &QPushButton::clicked, this, &DeviceWidget::startCapture );
    connect( stopButton_, &QPushButton::clicked, this, &DeviceWidget::stopCapture );
    connect( stopAllButton_, &QPushButton::clicked, this, &DeviceWidget::stopAllCaptures );
    connect( browseButton_, &QPushButton::clicked, this, &DeviceWidget::browseSavePath );
    connect( adbConfigButton_, &QPushButton::clicked, this, &DeviceWidget::configureAdbPath );
    connect( saveCheckBox_, &QCheckBox::toggled, savePathEdit_, &QLineEdit::setEnabled );
    connect( saveCheckBox_, &QCheckBox::toggled, browseButton_, &QPushButton::setEnabled );
    connect( deviceCombo_, &QComboBox::currentIndexChanged, this, [ this ]() { updateUiState(); } );

    // ── Device discovery ─────────────────────────────────────────────
    scanProcess_ = new QProcess( this );
    connect( scanProcess_, &QProcess::finished, this, &DeviceWidget::onScanFinished );
    connect( scanProcess_, &QProcess::errorOccurred, this,
             [ this ]( QProcess::ProcessError error ) {
                 // No finished() follows a failed start
                 if ( error == QProcess::FailedToStart ) {
                     scanTimeout_->stop();
                     hostLog( LOGSQUIRL_LOG_WARNING,
                              "adb devices could not be started: " + scanProcess_->errorString() );
                     setDevices( {} );
                     onScanEnded();
                 }
             } );

    // The first scan may need to start the ADB server, which can take
    // several seconds; give up after 10.
    scanTimeout_ = new QTimer( this );
    scanTimeout_->setSingleShot( true );
    scanTimeout_->setInterval( 10000 );
    connect( scanTimeout_, &QTimer::timeout, this, [ this ]() {
        hostLog( LOGSQUIRL_LOG_WARNING, "adb devices timed out." );
        scanProcess_->kill();
    } );

    // Initial device scan
    updateDeviceCombo();
    refreshDevices();
}

DeviceWidget::~DeviceWidget()
{
    // The scan must not report into a half-destroyed widget, and a scan
    // that is still running is killed and reaped here rather than left
    // to ~QProcess, which only warns and waits for it.
    scanProcess_->disconnect( this );
    if ( scanProcess_->state() != QProcess::NotRunning ) {
        scanProcess_->kill();
        scanProcess_->waitForFinished( 1000 );
    }
}

// ── Public methods ──────────────────────────────────────────────────────

void DeviceWidget::stopAll( bool cleanupTempFiles )
{
    const auto serials = sessions_.keys();
    for ( const auto& serial : serials ) {
        auto* proc = takeSession( serial );
        proc->stop();
        if ( cleanupTempFiles ) {
            // Also the files of earlier rotations, which rotateSession()
            // preserved for their tabs: at shutdown the tabs go too.
            proc->removeTempFiles();
        }
        else {
            keepTempFiles( proc );
        }
        proc->deleteLater();
    }
    if ( cleanupTempFiles ) {
        // The tabs of sessions that ended before close with the host too.
        // These are the sessions' own temporary directories, never a save
        // path or the log directory.
        for ( const auto& dir : std::as_const( endedTempDirs_ ) ) {
            QDir( dir ).removeRecursively();
        }
        endedTempDirs_.clear();
    }
    updateDeviceCombo();
}

int DeviceWidget::activeSessionCount() const
{
    return sessions_.size();
}

QStringList DeviceWidget::activeSerials() const
{
    return sessions_.keys();
}

void DeviceWidget::rotateSession( const QString& serial )
{
    auto* proc = sessions_.value( serial, nullptr );
    if ( !proc || !proc->isRunning() ) {
        return;
    }

    const auto newPath = proc->rotateLog();
    if ( newPath.isEmpty() ) {
        // rotateLog() has reported why through errorOccurred()
        return;
    }

    // The old tab keeps showing the old file, so the temporary directory
    // must outlive this session (stopAll( true ) still removes it).
    proc->preserveTempFile();

    // Open the new temp file in a follow-mode tab
    if ( g_state.api && g_state.handle ) {
        g_state.api->open_file( g_state.handle, newPath.toUtf8().constData(), 1 );
        hostNotify( QString( "New session started for %1" ).arg( serial ) );
    }
}

bool DeviceWidget::startSession( const QString& serial, const QString& savePath )
{
    if ( serial.isEmpty() || sessions_.contains( serial ) ) {
        return false;
    }

    // Two sessions appending to one file would interleave their lines.
    if ( !savePath.isEmpty() && isFileInUse( savePath ) ) {
        const auto message = QString( "Logcat not started for %1: another session is already "
                                      "writing to %2." )
                                 .arg( serial, savePath );
        hostLog( LOGSQUIRL_LOG_WARNING, message );
        hostNotify( message );
        return false;
    }

    auto* proc = new AdbProcess( serial, savePath, this );

    connect( proc, &AdbProcess::finished, this,
             [ this, serial ]( int ) { onSessionFinished( serial ); } );

    connect( proc, &AdbProcess::errorOccurred, this,
             [ this, serial ]( const QString& msg ) { onSessionError( serial, msg ); } );

    if ( !proc->start() ) {
        // start() has reported why through errorOccurred()
        delete proc;
        return false;
    }

    sessions_.insert( serial, proc );

    // Ask the host to open the log file in a follow-mode tab
    if ( g_state.api && g_state.handle ) {
        const auto path = proc->tempFilePath().toUtf8();
        g_state.api->open_file( g_state.handle, path.constData(), 1 );
    }
    hostNotify( QString( "Logcat started for %1" ).arg( serial ) );

    updateDeviceCombo(); // Update combo box markers
    return true;
}

void DeviceWidget::stopSession( const QString& serial )
{
    auto* proc = takeSession( serial );
    if ( !proc ) {
        return;
    }

    proc->stop();
    keepTempFiles( proc );

    hostNotify(
        QString( "Logcat stopped for %1 (%2 lines)" ).arg( serial ).arg( proc->lineCount() ) );

    proc->deleteLater();
    updateDeviceCombo();
}

qint64 DeviceWidget::sessionLineCount( const QString& serial ) const
{
    auto* proc = sessions_.value( serial, nullptr );
    return proc ? proc->lineCount() : 0;
}

bool DeviceWidget::isSessionActive( const QString& serial ) const
{
    return sessions_.contains( serial );
}

// ── Private slots ───────────────────────────────────────────────────────

void DeviceWidget::refreshDevices()
{
    if ( scanProcess_->state() != QProcess::NotRunning ) {
        rescanPending_ = true;
        return;
    }

    const auto adb = AdbProcess::findAdb();
    if ( adb.isEmpty() ) {
        hostLog( LOGSQUIRL_LOG_WARNING, "adb not found — cannot discover devices." );
        setDevices( {} );
        return;
    }

    scanProcess_->setProgram( adb );
    scanProcess_->setArguments( { "devices" } );
    // Arm the timeout first: on Windows a failed start is reported from
    // inside start(), and that handler stops the timeout.
    scanTimeout_->start();
    scanProcess_->start();
    updateRefreshButton();
}

void DeviceWidget::restartDeviceScan()
{
    if ( scanProcess_->state() != QProcess::NotRunning ) {
        // Its result is not wanted, not even as an empty list.
        const QSignalBlocker blocker( scanProcess_ );
        scanTimeout_->stop();
        scanProcess_->kill();
        scanProcess_->waitForFinished( 1000 );
    }
    rescanPending_ = false;
    refreshDevices();
}

void DeviceWidget::onScanFinished( int exitCode, QProcess::ExitStatus exitStatus )
{
    scanTimeout_->stop();

    QStringList found;
    if ( exitStatus == QProcess::CrashExit ) {
        // Killed after the timeout, which has been logged already
    }
    else if ( exitCode != 0 ) {
        hostLog( LOGSQUIRL_LOG_WARNING,
                 "adb devices failed: "
                     + QString::fromUtf8( scanProcess_->readAllStandardError() ).trimmed() );
    }
    else {
        found = AdbProcess::parseDeviceList( scanProcess_->readAllStandardOutput() );
        hostLog( LOGSQUIRL_LOG_INFO, QString( "Discovered %1 device(s)." ).arg( found.size() ) );
    }

    setDevices( found );
    onScanEnded();
}

void DeviceWidget::onScanEnded()
{
    if ( rescanPending_ ) {
        rescanPending_ = false;
        refreshDevices();
    }
    else {
        updateRefreshButton();
    }
}

void DeviceWidget::updateRefreshButton()
{
    const bool scanning = scanProcess_->state() != QProcess::NotRunning;
    refreshButton_->setEnabled( !scanning );
    refreshButton_->setText( scanning ? "Scanning…" : "⟳ Refresh" );
}

void DeviceWidget::setDevices( const QStringList& devices )
{
    devices_ = devices;
    updateDeviceCombo();
    Q_EMIT devicesChanged();
}

void DeviceWidget::updateDeviceCombo()
{
    const auto currentSelection = currentSerial();
    deviceCombo_->clear();

    if ( devices_.isEmpty() ) {
        deviceCombo_->addItem( "(no devices)" );
        deviceCombo_->setEnabled( false );
    }
    else {
        deviceCombo_->setEnabled( true );
        for ( const auto& serial : devices_ ) {
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
        hostLog( LOGSQUIRL_LOG_WARNING, "Logcat already running for " + serial );
        return;
    }

    // Determine save path (empty string disables saving)
    const auto savePath = ( saveCheckBox_->isChecked() && !savePathEdit_->text().isEmpty() )
                              ? savePathEdit_->text()
                              : QString();

    startSession( serial, savePath );
}

void DeviceWidget::stopCapture()
{
    stopSession( currentSerial() );
}

void DeviceWidget::stopAllCaptures()
{
    stopAll();

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

void DeviceWidget::onSessionFinished( const QString& serial )
{
    auto* proc = takeSession( serial );
    if ( !proc ) {
        return;
    }

    // Preserve the temp file so the LogSquirl tab keeps its content.
    // When using a save path the file is already persistent.
    keepTempFiles( proc );
    proc->deleteLater();

    hostLog( LOGSQUIRL_LOG_INFO, QString( "Logcat session for %1 ended." ).arg( serial ) );

    // adb exits when its device goes away; find out whether it did
    refreshDevices();
}

void DeviceWidget::onSessionError( const QString& serial, const QString& message )
{
    hostLog( LOGSQUIRL_LOG_ERROR, serial + ": " + message );

    hostNotify( "Logcat error (" + serial + "): " + message );
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
        statusLabel_->setText( QString( "%1 active session(s)" ).arg( activeCount ) );
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

    const auto prompt = QString( "ADB executable path:\n\n"
                                 "Detected: %1\nCurrent override: %2\n\n"
                                 "Leave empty to use auto-detection." )
                            .arg( detected.isEmpty() ? "(not found)" : detected,
                                  currentPath.isEmpty() ? "(none)" : currentPath );

    bool ok = false;
    const auto newPath = QInputDialog::getText( this, "Configure ADB Path", prompt,
                                                QLineEdit::Normal, currentPath, &ok );

    if ( ok ) {
        settings.setValue( "adb/path", newPath );
        hostLog( LOGSQUIRL_LOG_INFO, newPath.isEmpty()
                                         ? "ADB path override cleared — using auto-detection."
                                         : "ADB path set to: " + newPath );
        restartDeviceScan();
    }
}

AdbProcess* DeviceWidget::takeSession( const QString& serial )
{
    auto* proc = sessions_.take( serial );
    if ( proc ) {
        // The session is over as far as this widget is concerned.  Stopping
        // it emits finished(), and onSessionFinished() must not act on that:
        // it would preserve a temp file that stopAll( true ) is cleaning up,
        // and rescan the devices once per session.
        proc->disconnect( this );
    }
    return proc;
}

void DeviceWidget::keepTempFiles( AdbProcess* proc )
{
    const auto dir = proc->preserveTempFile();
    if ( !dir.isEmpty() && !endedTempDirs_.contains( dir ) ) {
        endedTempDirs_.append( dir );
    }
}

bool DeviceWidget::isFileInUse( const QString& path ) const
{
    for ( const auto* proc : sessions_ ) {
        if ( isSameFile( proc->tempFilePath(), path ) ) {
            return true;
        }
    }
    return false;
}

QString DeviceWidget::currentSerial() const
{
    const auto data = deviceCombo_->currentData();
    return data.isValid() ? data.toString() : QString();
}

} // namespace logcat
