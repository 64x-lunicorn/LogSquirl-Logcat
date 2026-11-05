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
 * @file presets_test.cpp
 * @brief BDD tests for the filter group and highlighter set the plugin
 *        ships: read as LogSquirl reads an exported file, named in
 *        plugin.json and packaged, and matching the logcat the plugin writes.
 *
 * The plugin runs `adb logcat` without `-v`, so it writes the threadtime
 * format; a device logging with `-v uid` adds the user before the PID.
 */

#include <catch2/catch.hpp>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

#include <map>

namespace {

/// The host's versions of the settings it imports (HighlighterSetCollection_
/// VERSION and the others): a file of a newer one is not imported.
constexpr int kHighlighterSetCollectionVersion = 2;
constexpr int kHighlighterSetVersion = 3;
constexpr int kPredefinedFiltersCollectionVersion = 3;
constexpr int kPredefinedFilterSetVersion = 1;

/// A file of the repository.
QString sourceFile( const QString& path )
{
    return QDir( QStringLiteral( LOGCAT_SOURCE_DIR ) ).filePath( path );
}

/// The JSON object in the file @p path.
QJsonObject jsonOf( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto document = QJsonDocument::fromJson( file.readAll() );
    REQUIRE( document.isObject() );
    return document.object();
}

/// The strings of the JSON array @p value.
QStringList stringsOf( const QJsonValue& value )
{
    QStringList strings;
    for ( const auto& item : value.toArray() ) {
        strings << item.toString();
    }
    return strings;
}

/// A rule of the files: its pattern, and a highlighter's colours.
struct Rule {
    QRegularExpression pattern;
    QColor fore;
    QColor back;
};

/// The filters of logcat_filter.conf, by name, as LogSquirl imports them.
std::map<QString, Rule> filters()
{
    QSettings file( sourceFile( "presets/logcat_filter.conf" ), QSettings::IniFormat );
    REQUIRE( file.status() == QSettings::NoError );
    file.beginGroup( "PredefinedFiltersCollection" );
    REQUIRE( file.value( "version" ).toInt() == kPredefinedFiltersCollectionVersion );
    REQUIRE( file.beginReadArray( "sets" ) == 1 );
    file.setArrayIndex( 0 );
    file.beginGroup( "PredefinedFilterSet" );
    REQUIRE( file.value( "version" ).toInt() == kPredefinedFilterSetVersion );
    REQUIRE( file.value( "name" ).toString() == "Logcat" );
    REQUIRE( file.value( "id" ).toString().size() == 32 );
    std::map<QString, Rule> rules;
    const int size = file.beginReadArray( "filters" );
    for ( int i = 0; i < size; ++i ) {
        file.setArrayIndex( i );
        REQUIRE( file.value( "regex" ).toBool() );
        rules[ file.value( "name" ).toString() ].pattern
            = QRegularExpression( file.value( "filter" ).toString() );
    }
    return rules;
}

/// The highlighters of logcat_highlighter.conf, in order, as LogSquirl
/// imports them.
std::vector<Rule> highlighters()
{
    QSettings file( sourceFile( "presets/logcat_highlighter.conf" ), QSettings::IniFormat );
    REQUIRE( file.status() == QSettings::NoError );
    file.beginGroup( "HighlighterSetCollection" );
    REQUIRE( file.value( "version" ).toInt() == kHighlighterSetCollectionVersion );
    REQUIRE( file.beginReadArray( "sets" ) == 1 );
    file.setArrayIndex( 0 );
    file.beginGroup( "HighlighterSet" );
    REQUIRE( file.value( "version" ).toInt() == kHighlighterSetVersion );
    REQUIRE( file.value( "name" ).toString() == "Logcat levels" );
    REQUIRE( file.value( "id" ).toString().size() == 32 );
    std::vector<Rule> rules;
    const int size = file.beginReadArray( "highlighters" );
    for ( int i = 0; i < size; ++i ) {
        file.setArrayIndex( i );
        REQUIRE( file.value( "use_regex" ).toBool() );
        Rule rule;
        rule.pattern = QRegularExpression( file.value( "regexp" ).toString() );
        rule.fore = QColor( file.value( "fore_colour" ).toString() );
        rule.back = QColor( file.value( "back_colour" ).toString() );
        rules.push_back( rule );
    }
    return rules;
}

/// How many of @p rules match @p line.
int matching( const std::vector<Rule>& rules, const QString& line )
{
    int count = 0;
    for ( const auto& rule : rules ) {
        count += rule.pattern.match( line ).hasMatch() ? 1 : 0;
    }
    return count;
}

/// Logcat lines as the plugin writes them (threadtime), and one with a user.
const QString kInfo = "11-05 12:00:00.123  1234  1250 I ActivityManager: Start proc com.example";
const QString kWarning
    = "11-05 12:00:00.124 u0_a123  1234  1250 W Choreographer: Skipped 42 frames";
const QString kAnr = "11-05 12:00:01.000  1234  1250 E ActivityManager: ANR in com.example.app";
const QString kJavaCrash = "11-05 12:00:02.000  4321  4321 E AndroidRuntime: FATAL EXCEPTION: main";
const QString kJavaStack
    = "11-05 12:00:02.001  4321  4321 E AndroidRuntime: \tat com.example.Main.run(Main.java:7)";
const QString kNativeCrash
    = "11-05 12:00:03.000  5555  5555 F libc    : Fatal signal 11 (SIGSEGV), code 1";
const QString kTombstone = "11-05 12:00:03.100  6666  6666 F DEBUG   : *** *** *** *** *** ***";
const QString kCrashBuffer = "--------- beginning of crash";
const QString kOtherError = "11-05 12:00:04.000  1234  1250 E Camera  : open failed";

} // namespace

SCENARIO( "The plugin ships its filter group and highlighter set", "[presets]" )
{
    GIVEN( "plugin.json and the packaging" )
    {
        const auto manifest = jsonOf( sourceFile( "plugin.json" ) );
        const auto ci = jsonOf( sourceFile( ".github/plugin-ci.json" ) );

        THEN( "plugin.json names both files by the name they have beside the library" )
        {
            REQUIRE( stringsOf( manifest[ "predefined_filters" ] )
                     == QStringList{ "logcat_filter.conf" } );
            REQUIRE( stringsOf( manifest[ "highlighter_sets" ] )
                     == QStringList{ "logcat_highlighter.conf" } );
        }

        THEN( "the release package carries both, flat beside the library" )
        {
            const auto packaged = stringsOf( ci[ "package_files" ] );
            REQUIRE( packaged.contains( "presets/logcat_filter.conf" ) );
            REQUIRE( packaged.contains( "presets/logcat_highlighter.conf" ) );
            for ( const auto& path : packaged ) {
                REQUIRE( QFile::exists( sourceFile( path ) ) );
            }
        }
    }
}

SCENARIO( "The Logcat filter group finds crashes, ANRs and errors", "[presets]" )
{
    GIVEN( "the filters as LogSquirl imports them" )
    {
        const auto rules = filters();

        THEN( "there are Crashes, ANRs and Errors and worse, each a valid pattern" )
        {
            REQUIRE( rules.size() == 3 );
            for ( const auto* name : { "Crashes", "ANRs", "Errors and worse" } ) {
                INFO( name );
                REQUIRE( rules.count( name ) == 1 );
                REQUIRE( rules.at( name ).pattern.isValid() );
            }
        }

        THEN( "Crashes takes Java and native crashes and the crash buffer, not other errors" )
        {
            const auto& crashes = rules.at( "Crashes" ).pattern;
            for ( const auto& line :
                  { kJavaCrash, kJavaStack, kNativeCrash, kTombstone, kCrashBuffer } ) {
                INFO( line.toStdString() );
                REQUIRE( crashes.match( line ).hasMatch() );
            }
            for ( const auto& line : { kInfo, kWarning, kAnr, kOtherError } ) {
                INFO( line.toStdString() );
                REQUIRE_FALSE( crashes.match( line ).hasMatch() );
            }
        }

        THEN( "ANRs takes the ANR line only" )
        {
            const auto& anrs = rules.at( "ANRs" ).pattern;
            REQUIRE( anrs.match( kAnr ).hasMatch() );
            REQUIRE_FALSE( anrs.match( kOtherError ).hasMatch() );
            REQUIRE_FALSE( anrs.match( kJavaCrash ).hasMatch() );
        }

        THEN( "Errors and worse takes the E and F lines, not I or W" )
        {
            const auto& errors = rules.at( "Errors and worse" ).pattern;
            for ( const auto& line : { kAnr, kJavaCrash, kNativeCrash, kTombstone, kOtherError } ) {
                INFO( line.toStdString() );
                REQUIRE( errors.match( line ).hasMatch() );
            }
            REQUIRE_FALSE( errors.match( kInfo ).hasMatch() );
            REQUIRE_FALSE( errors.match( kWarning ).hasMatch() );
            // A tag that ends in an E is not a level.
            REQUIRE_FALSE(
                errors.match( "11-05 12:00:00.123  1234  1250 I TAGE E : x" ).hasMatch() );
        }
    }
}

SCENARIO( "The Logcat levels highlighter set colours F, E and W lines", "[presets]" )
{
    GIVEN( "the highlighters as LogSquirl imports them" )
    {
        const auto rules = highlighters();

        THEN( "there are three, with valid patterns and opaque colours" )
        {
            REQUIRE( rules.size() == 3 );
            for ( const auto& rule : rules ) {
                REQUIRE( rule.pattern.isValid() );
                REQUIRE( rule.fore.isValid() );
                REQUIRE( rule.back.isValid() );
                REQUIRE( rule.back.alpha() == 255 );
            }
        }

        THEN( "each F, E and W line gets exactly one, an I line none" )
        {
            for ( const auto& line : { kNativeCrash, kJavaCrash, kWarning } ) {
                INFO( line.toStdString() );
                REQUIRE( matching( rules, line ) == 1 );
            }
            REQUIRE( matching( rules, kInfo ) == 0 );
            REQUIRE( matching( rules, kCrashBuffer ) == 0 );
        }

        THEN( "F, E and W are told apart: F white on dark red, the others dark text" )
        {
            REQUIRE( rules[ 0 ].pattern.match( kNativeCrash ).hasMatch() );
            REQUIRE( rules[ 0 ].fore == QColor( Qt::white ) );
            REQUIRE( rules[ 1 ].pattern.match( kJavaCrash ).hasMatch() );
            REQUIRE( rules[ 2 ].pattern.match( kWarning ).hasMatch() );
            REQUIRE( rules[ 1 ].back != rules[ 2 ].back );
        }
    }
}
