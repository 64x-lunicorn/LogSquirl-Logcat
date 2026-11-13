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
 * @file devicetile.cpp
 * @brief Implementation of the device tile.
 */

#include "devicetile.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace logcat {

namespace {

/// A label that wraps, and lets the text be selected and copied.  Plain
/// text: a device names itself, and markup in its name is not rendered.
QLabel* textLabel( const char* name, QWidget* parent )
{
    auto* label = new QLabel( parent );
    label->setObjectName( name );
    label->setWordWrap( true );
    label->setTextFormat( Qt::PlainText );
    label->setTextInteractionFlags( Qt::TextSelectableByMouse );
    return label;
}

/// @p from mixed with @p to: 0 is @p from, 1 is @p to.
QColor mix( const QColor& from, const QColor& to, double amount )
{
    const auto channel
        = [ amount ]( int a, int b ) { return static_cast<int>( a + ( b - a ) * amount + 0.5 ); };
    return QColor( channel( from.red(), to.red() ), channel( from.green(), to.green() ),
                   channel( from.blue(), to.blue() ) );
}

/// The state as the dot's property and the stylesheet name it.
const char* stateName( TileState state )
{
    switch ( state ) {
    case TileState::Connected:
        return "connected";
    case TileState::Running:
        return "running";
    case TileState::Unauthorized:
        return "unauthorized";
    case TileState::Offline:
        return "offline";
    case TileState::Unavailable:
        return "unavailable";
    case TileState::Disconnected:
        break;
    }
    return "disconnected";
}

/// Have @p widget's style see a changed dynamic property.
void repolish( QWidget* widget )
{
    widget->style()->unpolish( widget );
    widget->style()->polish( widget );
}

/// The dot's diameter, in pixels.
constexpr int kDot = 10;

} // namespace

DeviceTile::DeviceTile( const QString& serial, QWidget* parent )
    : QFrame( parent )
    , serial_( serial )
    , dot_( new QLabel( this ) )
    , title_( textLabel( "title", this ) )
    , summary_( textLabel( "summary", this ) )
    , separator_( new QFrame( this ) )
    , details_( textLabel( "details", this ) )
    , startStop_( new QPushButton( this ) )
    , rotate_( new QPushButton( QStringLiteral( "↻ Rotate" ), this ) )
    , savePath_( new QPushButton( QStringLiteral( "Save to…" ), this ) )
    , forget_( new QPushButton( QStringLiteral( "Forget" ), this ) )
{
    setObjectName( "tile" );

    dot_->setObjectName( "state" );
    dot_->setFixedSize( kDot, kDot );

    title_->setText( serial );
    auto font = title_->font();
    font.setBold( true );
    title_->setFont( font );

    separator_->setObjectName( "separator" );
    separator_->setFixedHeight( 1 );
    separator_->hide();

    // Secondary: smaller than the title and the summary, and muted.
    font = details_->font();
    font.setPointSizeF( font.pointSizeF() * 0.9 );
    details_->setFont( font );
    details_->hide();

    startStop_->setObjectName( "startStop" );
    rotate_->setObjectName( "rotate" );
    rotate_->setToolTip(
        QStringLiteral( "Go on in a new file, in a new tab; the old tab keeps its file" ) );
    savePath_->setObjectName( "savePath" );
    savePath_->setToolTip( QStringLiteral( "Choose the file this device's logcat is saved to" ) );
    forget_->setObjectName( "forget" );
    forget_->setToolTip( QStringLiteral( "Remove this device until it is connected again" ) );

    auto* head = new QHBoxLayout;
    head->setSpacing( 8 );
    head->addWidget( dot_, 0, Qt::AlignVCenter );
    head->addWidget( title_, 1 );
    head->addWidget( startStop_ );

    auto* foot = new QHBoxLayout;
    foot->addStretch();
    foot->addWidget( rotate_ );
    foot->addWidget( savePath_ );
    foot->addWidget( forget_ );

    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 12, 10, 12, 10 );
    layout->setSpacing( 6 );
    layout->addLayout( head );
    layout->addWidget( summary_ );
    layout->addWidget( separator_ );
    layout->addWidget( details_ );
    layout->addLayout( foot );

    connect( startStop_, &QPushButton::clicked, this, [ this ] {
        if ( state_ == TileState::Running ) {
            Q_EMIT stopClicked();
        }
        else {
            Q_EMIT startClicked();
        }
    } );
    connect( rotate_, &QPushButton::clicked, this, &DeviceTile::rotateClicked );
    connect( savePath_, &QPushButton::clicked, this, &DeviceTile::savePathClicked );
    connect( forget_, &QPushButton::clicked, this, &DeviceTile::forgetClicked );

    setState( TileState::Disconnected );
    watched_ = parentWidget();
    if ( watched_ ) {
        watched_->installEventFilter( this );
    }
    applyStyle();
}

void DeviceTile::setTitle( const QString& title )
{
    title_->setText( title );
}

void DeviceTile::setSummary( const QStringList& lines )
{
    summary_->setText( lines.join( QLatin1Char( '\n' ) ) );
}

void DeviceTile::setDetails( const QStringList& lines )
{
    details_->setText( lines.join( QLatin1Char( '\n' ) ) );
    details_->setVisible( !lines.isEmpty() );
    separator_->setVisible( !lines.isEmpty() );
}

void DeviceTile::setState( TileState state )
{
    state_ = state;
    const bool running = state == TileState::Running;
    const bool disconnected = state == TileState::Disconnected;

    startStop_->setText( running ? QStringLiteral( "■ Stop" ) : QStringLiteral( "▶ Start" ) );
    startStop_->setEnabled( running || state == TileState::Connected );
    switch ( state ) {
    case TileState::Running:
        startStop_->setToolTip( QStringLiteral( "Stop the capture; its tab keeps the file" ) );
        break;
    case TileState::Connected:
        startStop_->setToolTip( QStringLiteral( "Run logcat, followed in a new tab" ) );
        break;
    case TileState::Disconnected:
        startStop_->setToolTip( QStringLiteral( "Connect the device to start it" ) );
        break;
    case TileState::Unauthorized:
        startStop_->setToolTip( QStringLiteral( "Allow USB debugging on the device to start it" ) );
        break;
    case TileState::Offline:
    case TileState::Unavailable:
        startStop_->setToolTip( QStringLiteral( "The device does not answer adb" ) );
        break;
    }
    rotate_->setVisible( running );
    savePath_->setVisible( !running );
    forget_->setVisible( disconnected );

    const auto* name = stateName( state );
    dot_->setProperty( "state", QString::fromLatin1( name ) );
    dot_->setToolTip( QString::fromLatin1( name ) );
    setProperty( "running", running );
    setProperty( "disconnected", disconnected );
    repolish( dot_ );
    repolish( this );
    repolish( title_ );
    repolish( summary_ );
}

void DeviceTile::changeEvent( QEvent* event )
{
    QFrame::changeEvent( event );
    switch ( event->type() ) {
    case QEvent::ParentChange:
        // Its stylesheet keeps a palette change of the parent from reaching
        // the tile: it watches the parent itself.
        if ( watched_ ) {
            watched_->removeEventFilter( this );
        }
        watched_ = parentWidget();
        if ( watched_ ) {
            watched_->installEventFilter( this );
        }
        applyStyle();
        break;
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
        applyStyle();
        break;
    default:
        break;
    }
}

bool DeviceTile::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == watched_ && event->type() == QEvent::PaletteChange ) {
        applyStyle();
    }
    return QFrame::eventFilter( watched, event );
}

void DeviceTile::applyStyle()
{
    // From the palette around the tile: its own is changed by its
    // stylesheet, and styling from it would drift a little further at each
    // palette change.
    const auto source = parentWidget() ? parentWidget()->palette() : QApplication::palette( this );
    const auto window = source.color( QPalette::Window );
    const auto text = source.color( QPalette::WindowText );
    const auto accent = source.color( QPalette::Highlight );
    const auto styledFor = QStringList{ window.name(), text.name(), accent.name() }.join( ' ' );
    if ( styledFor == styledFor_ ) {
        return;
    }
    styledFor_ = styledFor;
    const bool dark = window.lightness() < 128;
    const auto card = dark ? window.lighter( 125 ) : window.darker( 103 );
    const auto border = mix( window, text, 0.22 );
    const auto muted = mix( window, text, 0.62 );
    const auto idle = mix( window, text, 0.4 );
    setStyleSheet(
        QStringLiteral( "QFrame#tile { background: %1; border: 1px solid %2;"
                        " border-radius: 8px; }"
                        "QFrame#tile[running=\"true\"] { border-color: %3; }"
                        "QFrame#separator { background: %2; border: none; }"
                        "QLabel#title, QLabel#summary, QLabel#details"
                        " { background: transparent; border: none; color: %7; }"
                        "QLabel#details { color: %4; }"
                        "QFrame#tile[disconnected=\"true\"] QLabel#title,"
                        " QFrame#tile[disconnected=\"true\"] QLabel#summary { color: %4; }"
                        "QLabel#state { border: none; border-radius: %5px; background: %6; }"
                        "QLabel#state[state=\"disconnected\"] { background: transparent;"
                        " border: 1px solid %6; }"
                        "QLabel#state[state=\"running\"] { background: #2ea043; }"
                        "QLabel#state[state=\"unauthorized\"], QLabel#state[state=\"offline\"],"
                        " QLabel#state[state=\"unavailable\"] { background: #d29922; }" )
            .arg( card.name(), border.name(), accent.name(), muted.name() )
            .arg( kDot / 2 )
            .arg( idle.name(), text.name() ) );
}

} // namespace logcat
