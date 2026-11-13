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
 * @file logfile_test.cpp
 * @brief BDD tests for LogFile, the file a Session writes.
 *
 * Every scenario works on real files in a temporary directory; none runs
 * adb.
 */

#include <catch2/catch.hpp>

#include "logfile.h"
#include "readonlydir.h"
#include "scopedenv.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using logcat::LogFile;
using logcat_test::ReadOnlyDir;
using logcat_test::ScopedEnv;

namespace {

QByteArray readFile( const QString& path )
{
    QFile file( path );
    return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray();
}

void writeFile( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( content );
}

/// The temporary directories of this process in @p root (tempdirs.h).
QStringList tempDirsIn( const QString& root )
{
    return QDir( root ).entryList( { "logsquirl-logcat-*" }, QDir::Dirs | QDir::NoDotAndDotDot );
}

} // namespace

SCENARIO( "a log file opens a save path by appending to it", "[logfile]" )
{
    QTemporaryDir logDir;

    GIVEN( "a save path that already holds a capture" )
    {
        const auto savePath = logDir.filePath( "capture.log" );
        writeFile( savePath, "earlier capture\n" );
        LogFile file( "emulator-5554", savePath );

        WHEN( "it is opened and written" )
        {
            QString error;
            REQUIRE( file.open( error ) );

            THEN( "its size counts the earlier capture" )
            {
                REQUIRE( file.path() == savePath );
                REQUIRE( file.isSavePath() );
                REQUIRE( file.size() == 16 );
            }

            AND_THEN( "the new lines follow the earlier capture" )
            {
                REQUIRE( file.writeLine( "first" ) );
                REQUIRE( file.writeLine( "second" ) );
                REQUIRE( file.flush() );
                REQUIRE( readFile( savePath ) == "earlier capture\nfirst\nsecond\n" );
            }
        }

        WHEN( "it is opened and discarded, as after a failed start" )
        {
            QString error;
            REQUIRE( file.open( error ) );
            file.discard();

            THEN( "the earlier capture is kept" )
            {
                REQUIRE( readFile( savePath ) == "earlier capture\n" );
                REQUIRE( file.path().isEmpty() );
            }
        }
    }

    GIVEN( "a save path in a directory that does not exist yet" )
    {
        const auto savePath = logDir.filePath( "sub/dir/capture.log" );
        LogFile file( "emulator-5554", savePath );

        WHEN( "it is opened" )
        {
            QString error;
            REQUIRE( file.open( error ) );

            THEN( "the directory and the file are created" )
            {
                REQUIRE( QFileInfo( savePath ).isFile() );
            }

#ifdef Q_OS_UNIX
            THEN( "the file is readable by the user only" )
            {
                struct stat info{};
                REQUIRE( ::stat( QFile::encodeName( savePath ).constData(), &info ) == 0 );
                REQUIRE( ( info.st_mode & 0777 ) == 0600 );
            }
#endif

            AND_WHEN( "it is discarded, as after a failed start" )
            {
                file.discard();

                THEN( "no empty log file is left behind" )
                {
                    REQUIRE_FALSE( QFileInfo::exists( savePath ) );
                    REQUIRE( file.path().isEmpty() );
                }
            }
        }
    }

    GIVEN( "a save path in the home directory, given with ~" )
    {
        QTemporaryDir home;
        const ScopedEnv homeEnv( "HOME", QFile::encodeName( home.path() ) );
        LogFile file( "emulator-5554", "~/capture.log" );

        WHEN( "it is opened" )
        {
            QString error;
            REQUIRE( file.open( error ) );

            THEN( "the file is in the home directory" )
            {
                REQUIRE( file.path() == QDir::cleanPath( home.filePath( "capture.log" ) ) );
                REQUIRE( QFileInfo::exists( file.path() ) );
            }
        }
    }

    GIVEN( "a relative save path" )
    {
        LogFile file( "emulator-5554", "relative-capture.log" );

        THEN( "it is refused before anything is tried, and says why" )
        {
            REQUIRE( file.pathError().contains( "not a full path" ) );
        }

        WHEN( "it is opened all the same" )
        {
            QString error;
            const auto opened = file.open( error );

            THEN( "it fails, and creates nothing in the working directory" )
            {
                REQUIRE_FALSE( opened );
                REQUIRE( error.contains( "not a full path" ) );
                REQUIRE_FALSE( QFileInfo::exists( "relative-capture.log" ) );
                REQUIRE( file.path().isEmpty() );
            }
        }
    }
}

SCENARIO( "a log file without a save path is a new file in a temporary directory", "[logfile]" )
{
    QTemporaryDir root;

    GIVEN( "no save path" )
    {
        LogFile file( "192.168.1.5:5555", {}, root.path() );

        WHEN( "it is opened" )
        {
            QString error;
            REQUIRE( file.open( error ) );

            THEN( "it is a new file named after the device, in a directory of this process" )
            {
                REQUIRE_FALSE( file.isSavePath() );
                REQUIRE( file.size() == 0 );
                REQUIRE( QFileInfo( file.path() ).fileName() == "logcat_192.168.1.5_5555.log" );
                REQUIRE( tempDirsIn( root.path() ).size() == 1 );
            }
        }
    }

    GIVEN( "a save path" )
    {
        QTemporaryDir logDir;
        LogFile file( "emulator-5554", logDir.filePath( "capture.log" ), root.path() );

        WHEN( "it is opened" )
        {
            QString error;
            REQUIRE( file.open( error ) );

            THEN( "no temporary directory is created" )
            {
                REQUIRE( tempDirsIn( root.path() ).isEmpty() );
            }
        }
    }
}

#ifdef Q_OS_UNIX
SCENARIO( "a log file is only ever a regular file", "[logfile]" )
{
    QTemporaryDir logDir;

    GIVEN( "a save path that is a FIFO" )
    {
        const auto fifo = logDir.filePath( "capture.log" );
        REQUIRE( ::mkfifo( QFile::encodeName( fifo ).constData(), 0600 ) == 0 );
        LogFile file( "emulator-5554", fifo );

        WHEN( "it is opened" )
        {
            QElapsedTimer timer;
            timer.start();
            QString error;
            const auto opened = file.open( error );

            THEN( "it is refused at once, without waiting for a reader" )
            {
                REQUIRE_FALSE( opened );
                REQUIRE( timer.elapsed() < 2000 );
                REQUIRE( error.contains( "not a regular file" ) );
                REQUIRE( file.path().isEmpty() );
            }
        }
    }

    GIVEN( "a save path that is a symlink to a file that does not exist" )
    {
        const auto target = logDir.filePath( "elsewhere.log" );
        const auto link = logDir.filePath( "capture.log" );
        REQUIRE( QFile::link( target, link ) );
        LogFile file( "emulator-5554", link );

        WHEN( "it is opened" )
        {
            QString error;
            const auto opened = file.open( error );

            THEN( "it is refused, and the link's target is not created" )
            {
                REQUIRE_FALSE( opened );
                REQUIRE( error.contains( "symbolic link" ) );
                REQUIRE_FALSE( QFileInfo::exists( target ) );
            }
        }
    }

    GIVEN( "a save path that is a symlink to an existing file" )
    {
        const auto target = logDir.filePath( "elsewhere.log" );
        writeFile( target, "theirs\n" );
        const auto link = logDir.filePath( "capture.log" );
        REQUIRE( QFile::link( target, link ) );
        LogFile file( "emulator-5554", link );

        WHEN( "it is opened" )
        {
            QString error;
            const auto opened = file.open( error );

            THEN( "it is refused, and discarding it leaves the target alone" )
            {
                REQUIRE_FALSE( opened );
                file.discard();
                REQUIRE( readFile( target ) == "theirs\n" );
                REQUIRE( QFileInfo( link ).isSymLink() );
            }
        }
    }
}

SCENARIO( "a log file that cannot be written says so, naming the file", "[logfile]" )
{
    GIVEN( "a log file that may grow by a few bytes only" )
    {
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        LogFile file( "emulator-5554", savePath );
        QString error;
        REQUIRE( file.open( error ) );

        // A file size limit makes every write past it fail with EFBIG, as a
        // full disk fails it; the signal it would send is ignored.
        struct rlimit before{};
        REQUIRE( ::getrlimit( RLIMIT_FSIZE, &before ) == 0 );
        const auto oldHandler = std::signal( SIGXFSZ, SIG_IGN );
        struct rlimit tiny = before;
        tiny.rlim_cur = 8;
        REQUIRE( ::setrlimit( RLIMIT_FSIZE, &tiny ) == 0 );

        WHEN( "more than that is written" )
        {
            // QFile buffers: the failure shows on a write or on the flush.
            const auto written = file.writeLine( "a line longer than eight bytes" ) && file.flush();
            const auto later = file.writeLine( "x" );
            ::setrlimit( RLIMIT_FSIZE, &before );

            THEN( "it fails, and every write after it as well" )
            {
                REQUIRE_FALSE( written );
                REQUIRE_FALSE( later );
                REQUIRE( file.failed() );
                REQUIRE( file.failure().contains( "Could not write the log file" ) );
                REQUIRE( file.failure().contains( savePath ) );
            }
        }

        ::setrlimit( RLIMIT_FSIZE, &before );
        std::signal( SIGXFSZ, oldHandler );
    }
}
#endif

SCENARIO( "a log file rotates to a new file", "[logfile]" )
{
    GIVEN( "a file written to a generated save path" )
    {
        QTemporaryDir logDir;
        const auto savePath = LogFile::generateLogPath( logDir.path(), "emulator-5554" );
        LogFile file( "emulator-5554", savePath );
        QString error;
        REQUIRE( file.open( error ) );
        REQUIRE( file.writeLine( "first" ) );

        WHEN( "it is rotated within the same second" )
        {
            const auto rotation = file.rotate( error );
            REQUIRE( file.writeLine( "after" ) );
            REQUIRE( file.flush() );

            THEN( "the capture goes on in a different, empty file next to it" )
            {
                REQUIRE( rotation == LogFile::Rotation::Rotated );
                REQUIRE( file.path() != savePath );
                REQUIRE( QFileInfo( file.path() ).absolutePath()
                         == QFileInfo( savePath ).absolutePath() );
                REQUIRE( readFile( file.path() ) == "after\n" );
            }

            THEN( "the old file keeps its content" )
            {
                REQUIRE( readFile( savePath ) == "first\n" );
            }
        }
    }

    GIVEN( "a file written to a save path in the home directory, given with ~" )
    {
        QTemporaryDir home;
        const ScopedEnv homeEnv( "HOME", QFile::encodeName( home.path() ) );
        LogFile file( "emulator-5554", "~/capture.log" );
        QString error;
        REQUIRE( file.open( error ) );

        WHEN( "it is rotated" )
        {
            const auto rotation = file.rotate( error );

            THEN( "the new file is in the home directory" )
            {
                REQUIRE( rotation == LogFile::Rotation::Rotated );
                REQUIRE( QFileInfo( file.path() ).absolutePath()
                         == QDir::cleanPath( home.path() ) );
                REQUIRE( QFileInfo::exists( file.path() ) );
            }
        }
    }

    GIVEN( "a file in a temporary directory" )
    {
        QTemporaryDir root;
        LogFile file( "192.168.1.5:5555", {}, root.path() );
        QString error;
        REQUIRE( file.open( error ) );
        const auto oldPath = file.path();
        REQUIRE( file.writeLine( "first" ) );

        WHEN( "it is rotated twice" )
        {
            REQUIRE( file.rotate( error ) == LogFile::Rotation::Rotated );
            const auto firstRotated = file.path();
            REQUIRE( file.rotate( error ) == LogFile::Rotation::Rotated );

            THEN( "the files are numbered, in the same directory, without the serial's ':'" )
            {
                REQUIRE( QFileInfo( firstRotated ).fileName() == "logcat_192.168.1.5_5555_1.log" );
                REQUIRE( QFileInfo( file.path() ).fileName() == "logcat_192.168.1.5_5555_2.log" );
                REQUIRE( QFileInfo( file.path() ).absolutePath()
                         == QFileInfo( oldPath ).absolutePath() );
            }

            THEN( "the old file keeps its content" )
            {
                REQUIRE( readFile( oldPath ) == "first\n" );
            }
        }
    }

#ifdef Q_OS_UNIX
    GIVEN( "a file whose directory no longer accepts new files" )
    {
        if ( !ReadOnlyDir::isEnforced() ) {
            WARN( "File permissions are not enforced (running as root?); skipped." );
            return;
        }

        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        LogFile file( "emulator-5554", savePath );
        QString error;
        REQUIRE( file.open( error ) );
        REQUIRE( file.writeLine( "first" ) );

        WHEN( "rotating fails" )
        {
            LogFile::Rotation rotation;
            {
                ReadOnlyDir readOnly( logDir.path() );
                rotation = file.rotate( error );
            }

            THEN( "it says so, and the capture goes on in the old file" )
            {
                REQUIRE( rotation == LogFile::Rotation::KeptOldFile );
                REQUIRE( error.contains( "still writing to " + savePath ) );
                REQUIRE( file.path() == savePath );
                REQUIRE( file.writeLine( "after" ) );
                REQUIRE( file.flush() );
                REQUIRE( readFile( savePath ) == "first\nafter\n" );
            }
        }

        WHEN( "rotating fails and the old file cannot be opened again either" )
        {
            LogFile::Rotation rotation;
            {
                QFile::setPermissions( savePath, QFileDevice::ReadOwner );
                ReadOnlyDir readOnly( logDir.path() );
                rotation = file.rotate( error );
            }

            THEN( "it says so, and no file is open" )
            {
                REQUIRE( rotation == LogFile::Rotation::Closed );
                REQUIRE( error.contains( "nor reopen " + savePath ) );
                REQUIRE_FALSE( file.isOpen() );
                REQUIRE_FALSE( file.writeLine( "after" ) );
            }
        }
    }
#endif
}

SCENARIO( "a log file keeps or releases its temporary directory", "[logfile]" )
{
    QTemporaryDir root;

    GIVEN( "a file in a temporary directory that is kept before it is destroyed" )
    {
        QString kept;
        QString path;
        {
            LogFile file( "emulator-5554", {}, root.path() );
            QString error;
            REQUIRE( file.open( error ) );
            path = file.path();
            kept = file.keepTempDir();
        }

        THEN( "the directory and its file are still there" )
        {
            REQUIRE_FALSE( kept.isEmpty() );
            REQUIRE( QFileInfo( kept ).isDir() );
            REQUIRE( QFileInfo( path ).absolutePath() == QFileInfo( kept ).absoluteFilePath() );
            REQUIRE( QFileInfo::exists( path ) );
        }
    }

    GIVEN( "a file in a temporary directory that is destroyed without keeping it" )
    {
        QString path;
        {
            LogFile file( "emulator-5554", {}, root.path() );
            QString error;
            REQUIRE( file.open( error ) );
            path = file.path();
        }

        THEN( "the directory is released: removed with its file" )
        {
            REQUIRE_FALSE( QFileInfo::exists( path ) );
            REQUIRE( tempDirsIn( root.path() ).isEmpty() );
        }
    }

    GIVEN( "a file at a save path" )
    {
        QTemporaryDir logDir;
        const auto savePath = logDir.filePath( "capture.log" );
        QString kept = "unset";
        {
            LogFile file( "emulator-5554", savePath, root.path() );
            QString error;
            REQUIRE( file.open( error ) );
            kept = file.keepTempDir();
        }

        THEN( "there is no directory to keep, and the file stays" )
        {
            REQUIRE( kept.isEmpty() );
            REQUIRE( QFileInfo::exists( savePath ) );
            REQUIRE( tempDirsIn( root.path() ).isEmpty() );
        }
    }
}

SCENARIO( "a typed path is made absolute, and a relative one refused", "[logfile]" )
{
    THEN( "~ is the home directory, and the path is cleaned" )
    {
        REQUIRE( LogFile::absoluteUserPath( "~" ) == QDir::homePath() );
        REQUIRE( LogFile::absoluteUserPath( " ~/logs/x.log " )
                 == QDir::homePath() + "/logs/x.log" );
        const auto root = QDir::rootPath();
        REQUIRE( LogFile::absoluteUserPath( root + "a/../b/c.log" ) == root + "b/c.log" );
    }

    THEN( "a relative path, or none, is empty" )
    {
        REQUIRE( LogFile::absoluteUserPath( "out.log" ).isEmpty() );
        REQUIRE( LogFile::absoluteUserPath( "logs/out.log" ).isEmpty() );
        REQUIRE( LogFile::absoluteUserPath( "~user/x.log" ).isEmpty() );
        REQUIRE( LogFile::absoluteUserPath( "  " ).isEmpty() );
    }
}
