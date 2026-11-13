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
 * @file logfile.cpp
 * @brief Implementation of LogFile, the file a logcat Session writes.
 */

#include "logfile.h"
#include "tempdirs.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace logcat {

namespace {

/// Make a device serial usable as part of a file name on every platform.
QString safeFileName( const QString& serial )
{
    auto name = serial;
    name.replace( QRegularExpression( "[^a-zA-Z0-9._-]" ), "_" );
    return name;
}

} // namespace

LogFile::LogFile( const QString& serial, const QString& savePath, const QString& tempRoot )
    : serial_( serial )
    , savePath_( savePath )
    , tempRoot_( tempRoot )
{
}

LogFile::~LogFile()
{
    file_.close();
}

QString LogFile::absoluteUserPath( const QString& path )
{
    auto expanded = path.trimmed();
    if ( expanded == QStringLiteral( "~" ) ) {
        expanded = QDir::homePath();
    }
    else if ( expanded.startsWith( QStringLiteral( "~/" ) )
              || expanded.startsWith( QStringLiteral( "~\\" ) ) ) {
        expanded = QDir::homePath() + expanded.mid( 1 );
    }
    if ( expanded.isEmpty() || QDir::isRelativePath( expanded ) ) {
        return {};
    }
    return QDir::cleanPath( expanded );
}

QString LogFile::generateLogPath( const QString& dir, const QString& serial,
                                  const QDateTime& timestamp )
{
    const QDir logDir( dir );
    const auto stem = timestamp.toString( "yyyy-MM-dd_HHmmss" ) + "_" + safeFileName( serial );

    auto path = logDir.filePath( stem + ".log" );
    for ( int n = 2; QFileInfo::exists( path ); ++n ) {
        path = logDir.filePath( QString( "%1_%2.log" ).arg( stem ).arg( n ) );
    }
    return path;
}

QString LogFile::pathError() const
{
    if ( isSavePath() && absoluteUserPath( savePath_ ).isEmpty() ) {
        return QString( "The log file %1 is not a full path; give one such as %2." )
            .arg( savePath_, QDir( QDir::homePath() ).filePath( "logcat.log" ) );
    }
    return {};
}

bool LogFile::open( QString& error )
{
    if ( const auto pathProblem = pathError(); !pathProblem.isEmpty() ) {
        error = pathProblem;
        return false;
    }

    // Nothing is ever truncated: a save path is appended to, so that Stop
    // and Start with the same path keep the earlier capture, and the temp
    // file must be new.
    QString path;
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    if ( isSavePath() ) {
        path = absoluteUserPath( savePath_ );
        QDir().mkpath( QFileInfo( path ).absolutePath() );
        mode |= QIODevice::Append;
    }
    else {
        if ( !tempDir_ ) {
            tempDir_ = std::make_unique<QTemporaryDir>(
                tempDirTemplate( tempRoot_.isEmpty() ? tempRoot() : tempRoot_ ) );
        }
        if ( !tempDir_->isValid() ) {
            tempDir_.reset();
            error = QStringLiteral( "Failed to create temporary directory." );
            return false;
        }
        path = tempDir_->filePath( "logcat_" + safeFileName( serial_ ) + ".log" );
        mode |= QIODevice::NewOnly;
    }

    failure_.clear();
    created_ = !QFileInfo::exists( path );
    QString reason;
    if ( !openPath( path, mode, reason ) ) {
        error = "Failed to open log file " + path + ": " + reason;
        file_.setFileName( {} );
        path_.clear();
        return false;
    }
    return true;
}

void LogFile::close()
{
    file_.close();
}

void LogFile::discard()
{
    file_.close();
    if ( created_ && !path_.isEmpty() ) {
        QFile::remove( path_ );
    }
    file_.setFileName( {} );
    path_.clear();
}

bool LogFile::writeLine( const QByteArray& line )
{
    if ( failed() || !isOpen() ) {
        return false;
    }
    if ( file_.write( line ) != line.size() || file_.write( "\n", 1 ) != 1 ) {
        fail();
        return false;
    }
    return true;
}

bool LogFile::flush()
{
    if ( failed() || !isOpen() ) {
        return false;
    }
    if ( !file_.flush() ) {
        fail();
        return false;
    }
    return true;
}

void LogFile::fail()
{
    if ( failure_.isEmpty() ) {
        failure_
            = QString( "Could not write the log file %1 (%2)" ).arg( path_, file_.errorString() );
    }
}

LogFile::Rotation LogFile::rotate( QString& error )
{
    if ( !isOpen() ) {
        error = QStringLiteral( "No log file is open to rotate." );
        return Rotation::Closed;
    }

    // A new timestamped file next to the save path (the directory open()
    // wrote to: the save path as typed may start with ~), or the next
    // number in the temporary directory.  Either way the file must be new:
    // opening an existing one would truncate an earlier capture.
    QString newPath;
    if ( isSavePath() ) {
        newPath
            = generateLogPath( QFileInfo( absoluteUserPath( savePath_ ) ).absolutePath(), serial_ );
    }
    else {
        newPath = tempDir_->filePath(
            QString( "logcat_%1_%2.log" ).arg( safeFileName( serial_ ) ).arg( rotations_ + 1 ) );
    }

    // The old file stays on disk, for the tab that shows it.
    const auto oldPath = path_;
    file_.close();

    QString reason;
    if ( !openPath( newPath, QIODevice::WriteOnly | QIODevice::NewOnly, reason ) ) {
        // Keep capturing into the old file: a Session left running with a
        // closed file would silently drop everything from here on.
        QString reopenError;
        if ( openPath( oldPath, QIODevice::WriteOnly | QIODevice::Append, reopenError ) ) {
            error = QString( "Could not rotate the log to %1 (%2); still writing to %3." )
                        .arg( newPath, reason, oldPath );
            return Rotation::KeptOldFile;
        }
        error = QString( "Could not rotate the log to %1 (%2), nor reopen %3 (%4)" )
                    .arg( newPath, reason, oldPath, reopenError );
        return Rotation::Closed;
    }

    ++rotations_;
    created_ = true;
    return Rotation::Rotated;
}

QString LogFile::keepTempDir()
{
    if ( isSavePath() || !tempDir_ ) {
        return {};
    }
    tempDir_->setAutoRemove( false );
    return tempDir_->path();
}

bool LogFile::openPath( const QString& path, QIODevice::OpenMode mode, QString& error )
{
    file_.setFileName( path );
#ifdef Q_OS_UNIX
    // One open that cannot block on a FIFO nor follow a symlink, then a
    // check of what was opened: a check before the open could be raced.
    int flags = O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    flags |= mode.testFlag( QIODevice::NewOnly ) ? O_EXCL : 0;
    flags |= mode.testFlag( QIODevice::Append ) ? O_APPEND : 0;
    const int fd = ::open( QFile::encodeName( path ).constData(), flags, S_IRUSR | S_IWUSR );
    if ( fd < 0 ) {
        // A FIFO without a reader (ENXIO) and a symlink (ELOOP) are what
        // the flags refuse; anything else is the system's own reason.
        if ( errno == ENXIO ) {
            error = QStringLiteral( "not a regular file" );
        }
        else if ( errno == ELOOP ) {
            error = QStringLiteral( "a symbolic link, not a regular file" );
        }
        else {
            error = QString::fromLocal8Bit( std::strerror( errno ) );
        }
        return false;
    }
    struct stat info{};
    if ( ::fstat( fd, &info ) != 0 || !S_ISREG( info.st_mode ) ) {
        ::close( fd );
        error = QStringLiteral( "not a regular file" );
        return false;
    }
    // A regular file never blocks; the flag only kept the open from waiting.
    ::fcntl( fd, F_SETFL, ::fcntl( fd, F_GETFL ) & ~O_NONBLOCK );
    if ( !file_.open( fd, mode & ~QIODevice::NewOnly, QFileDevice::AutoCloseHandle ) ) {
        ::close( fd );
        error = file_.errorString();
        return false;
    }
    path_ = path;
    return true;
#else
    const QFileInfo info( path );
    if ( info.isSymLink() || ( info.exists() && !info.isFile() ) ) {
        error = QStringLiteral( "not a regular file" );
        return false;
    }
    if ( !file_.open( mode ) ) {
        error = file_.errorString();
        return false;
    }
    if ( mode.testFlag( QIODevice::NewOnly ) || !info.exists() ) {
        file_.setPermissions( QFileDevice::ReadOwner | QFileDevice::WriteOwner );
    }
    path_ = path;
    return true;
#endif
}

} // namespace logcat
