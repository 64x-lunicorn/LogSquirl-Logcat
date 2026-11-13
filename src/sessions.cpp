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
 * @file sessions.cpp
 * @brief Implementation of Sessions: the logcat session of each device.
 *
 * Sessions keeps a map of serial → AdbProcess*.  startSession():
 *
 *   1. An AdbProcess is created for the device serial.
 *   2. AdbProcess::start() launches `adb -s <serial> logcat`.
 *   3. Output is streamed to a temporary file (or the save path).
 *   4. The host API's open_file() is called with the file path and
 *      follow=true, which opens a new tail-mode tab in LogSquirl.
 *   5. The session is stored in sessions_, and sessionStarted() emitted.
 *
 * stopSession():
 *   1. AdbProcess::stop() terminates the child process.
 *   2. The session is removed from sessions_, and sessionStopped() emitted.
 *   3. The LogSquirl tab remains open (the user can close it manually).
 */

#include "sessions.h"
#include "plugin.h"

#include <QDir>
#include <QFileInfo>

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

/// Open @p path in a new followed tab.
void openFollowed( const QString& path )
{
    if ( g_state.api && g_state.handle ) {
        g_state.api->open_file( g_state.handle, path.toUtf8().constData(), 1 );
    }
}

} // namespace

// ── Construction ────────────────────────────────────────────────────────

Sessions::Sessions( QObject* parent )
    : QObject( parent )
{
}

// ── Sessions ────────────────────────────────────────────────────────────

bool Sessions::startSession( const QString& serial, const QString& savePath )
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

    connect( proc, &AdbProcess::finished, this, [ this, serial ]( int ) {
        guarded( "ending a logcat session", [ this, &serial ] { onSessionFinished( serial ); } );
    } );

    connect( proc, &AdbProcess::errorOccurred, this, [ this, serial ]( const QString& msg ) {
        guarded( "reporting a logcat error",
                 [ this, &serial, &msg ] { onSessionError( serial, msg ); } );
    } );

    if ( !proc->start() ) {
        // start() has reported why through errorOccurred()
        delete proc;
        return false;
    }

    sessions_.insert( serial, proc );

    // Ask the host to open the log file in a follow-mode tab
    openFollowed( proc->logFilePath() );
    hostNotify( QString( "Logcat started for %1" ).arg( serial ) );

    Q_EMIT sessionStarted( serial );
    return true;
}

void Sessions::stopSession( const QString& serial )
{
    auto* proc = takeSession( serial );
    if ( !proc ) {
        return;
    }

    proc->stop();
    proc->preserveTempFile();

    hostNotify(
        QString( "Logcat stopped for %1 (%2 lines)" ).arg( serial ).arg( proc->counters().lines ) );

    proc->deleteLater();
    Q_EMIT sessionStopped( serial );
}

void Sessions::stopAll()
{
    // Stopped together: one after the other, adbs that do not exit would
    // block the UI for two seconds each.
    QList<AdbProcess*> stopped;
    const auto serials = sessions_.keys();
    for ( const auto& serial : serials ) {
        stopped.append( takeSession( serial ) );
    }
    AdbProcess::stopAll( stopped );
    for ( auto* proc : stopped ) {
        proc->preserveTempFile();
        proc->deleteLater();
    }
    for ( const auto& serial : serials ) {
        Q_EMIT sessionStopped( serial );
    }
}

void Sessions::rotateSession( const QString& serial )
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
    // must outlive this session (until LogSquirl quits, see tempdirs.h).
    proc->preserveTempFile();

    // Open the new temp file in a follow-mode tab
    openFollowed( newPath );
    hostNotify( QString( "New session started for %1" ).arg( serial ) );

    Q_EMIT sessionChanged( serial );
}

bool Sessions::isSessionActive( const QString& serial ) const
{
    return sessions_.contains( serial );
}

int Sessions::activeSessionCount() const
{
    return static_cast<int>( sessions_.size() );
}

QStringList Sessions::activeSerials() const
{
    return sessions_.keys();
}

SessionCounters Sessions::counters( const QString& serial ) const
{
    const auto* proc = sessions_.value( serial, nullptr );
    return proc ? proc->counters() : SessionCounters{};
}

QString Sessions::filePath( const QString& serial ) const
{
    const auto* proc = sessions_.value( serial, nullptr );
    return proc ? proc->logFilePath() : QString();
}

// ── Private ─────────────────────────────────────────────────────────────

void Sessions::onSessionFinished( const QString& serial )
{
    auto* proc = takeSession( serial );
    if ( !proc ) {
        return;
    }

    // Preserve the temp file so the LogSquirl tab keeps its content.
    // When using a save path the file is already persistent.
    proc->preserveTempFile();
    proc->deleteLater();

    hostLog( LOGSQUIRL_LOG_INFO, QString( "Logcat session for %1 ended." ).arg( serial ) );
    Q_EMIT sessionStopped( serial );

    // adb exits when its device goes away: the scan finds out whether it did
    Q_EMIT sessionEnded( serial );
}

void Sessions::onSessionError( const QString& serial, const QString& message )
{
    hostLog( LOGSQUIRL_LOG_ERROR, serial + ": " + message );

    hostNotify( "Logcat error (" + serial + "): " + message );
}

AdbProcess* Sessions::takeSession( const QString& serial )
{
    auto* proc = sessions_.take( serial );
    if ( proc ) {
        // The session is over as far as Sessions is concerned.  Stopping
        // it emits finished(), and onSessionFinished() must not act on that:
        // it would end the session a second time, and rescan the devices
        // once per session.
        proc->disconnect( this );
    }
    return proc;
}

bool Sessions::isFileInUse( const QString& path ) const
{
    // The file a session writes to, as start() expands a path with ~.  A
    // relative path, which start() refuses, is no session's file.
    const auto file = LogFile::absoluteUserPath( path );
    if ( file.isEmpty() ) {
        return false;
    }
    for ( const auto* proc : sessions_ ) {
        if ( isSameFile( proc->logFilePath(), file ) ) {
            return true;
        }
    }
    return false;
}

} // namespace logcat
