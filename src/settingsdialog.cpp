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
 * @file settingsdialog.cpp
 * @brief Implementation of the settings dialog.
 */

#include "settingsdialog.h"

#include "plugin.h"
#include "settings.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace logcat {

namespace {

/// @p edit with a browse button "…" named @p browseName after it.
QHBoxLayout* withBrowse( QLineEdit* edit, QPushButton* browse, const char* browseName )
{
    browse->setObjectName( browseName );
    browse->setFixedWidth( 30 );
    auto* row = new QHBoxLayout;
    row->addWidget( edit, 1 );
    row->addWidget( browse );
    return row;
}

} // namespace

SettingsDialog::SettingsDialog( Settings* settings, QWidget* parent )
    : QDialog( parent )
    , settings_( settings )
    , logDir_( new QLineEdit( this ) )
    , adbPath_( new QLineEdit( this ) )
{
    setWindowTitle( QStringLiteral( "Logcat Settings" ) );
    setMinimumWidth( 460 );

    logDir_->setObjectName( "logDir" );
    logDir_->setText( settings_->customLogDir() );
    logDir_->setPlaceholderText( QStringLiteral( "(logs/ in the plugin's settings directory)" ) );
    logDir_->setToolTip( QStringLiteral( "Where a device without a save path of its own writes "
                                         "a new file at each Start:\n"
                                         "YYYY-MM-dd_HHmmss_<serial>.log" ) );
    auto* browseLogDir = new QPushButton( QStringLiteral( "…" ), this );
    browseLogDir->setToolTip( QStringLiteral( "Browse for the log directory" ) );

    adbPath_->setObjectName( "adbPath" );
    adbPath_->setText( settings_->adbPathOverride() );
    // What an empty field uses (with an override, findAdb() would name that)
    const auto detected = adbPath_->text().isEmpty() ? settings_->findAdb() : QString();
    adbPath_->setPlaceholderText(
        detected.isEmpty() ? QStringLiteral( "(found automatically)" )
                           : QStringLiteral( "(found automatically: %1)" ).arg( detected ) );
    adbPath_->setToolTip( QStringLiteral( "The adb executable; empty to find it in ANDROID_HOME, "
                                          "ANDROID_SDK_ROOT or the PATH" ) );
    auto* browseAdb = new QPushButton( QStringLiteral( "…" ), this );
    browseAdb->setToolTip( QStringLiteral( "Browse for the adb executable" ) );

    auto* form = new QFormLayout;
    form->addRow( QStringLiteral( "Log directory" ),
                  withBrowse( logDir_, browseLogDir, "browseLogDir" ) );
    form->addRow( QStringLiteral( "ADB" ), withBrowse( adbPath_, browseAdb, "browseAdb" ) );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );

    auto* layout = new QVBoxLayout( this );
    layout->addLayout( form );
    layout->addWidget( buttons );

    // Each handler runs through guarded(): an exception must not unwind
    // through Qt's event loop into the host.
    connect( buttons, &QDialogButtonBox::accepted, this,
             [ this ] { guarded( "saving the settings", [ this ] { accept(); } ); } );
    connect( buttons, &QDialogButtonBox::rejected, this,
             [ this ] { guarded( "closing the settings", [ this ] { reject(); } ); } );
    connect( browseLogDir, &QPushButton::clicked, this, [ this ] {
        guarded( "choosing the log directory", [ this ] {
            const auto dir = QFileDialog::getExistingDirectory(
                this, QStringLiteral( "Select Log Directory" ), logDir_->text() );
            if ( !dir.isEmpty() ) {
                logDir_->setText( dir );
            }
        } );
    } );
    connect( browseAdb, &QPushButton::clicked, this, [ this ] {
        guarded( "choosing the adb executable", [ this ] {
            const auto path = QFileDialog::getOpenFileName(
                this, QStringLiteral( "Select the adb Executable" ), adbPath_->text() );
            if ( !path.isEmpty() ) {
                adbPath_->setText( path );
            }
        } );
    } );
}

void SettingsDialog::accept()
{
    settings_->setLogDir( logDir_->text() );
    // A new path has the devices rescanned (Settings::adbPathChanged()).
    settings_->setAdbPathOverride( adbPath_->text() );
    QDialog::accept();
}

} // namespace logcat
