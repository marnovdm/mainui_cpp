/*
SpinControl.h - spin selector
Copyright (C) 2010 Uncle Mike
Copyright (C) 2017 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "extdll_menu.h"
#include "BaseMenu.h"
#include "SpinControl.h"
#include "Utils.h"
#include "Scissor.h"

CMenuSpinControl::CMenuSpinControl()  : BaseClass(), m_szBackground(),
		m_szLeftArrow(), m_szRightArrow(), m_szLeftArrowFocus(), m_szRightArrowFocus(),
		m_flMinValue(0), m_flMaxValue(1), m_flCurValue(0), m_flRange(0.1), m_pModel( NULL ),
		m_iFloatPrecision(0), m_szSuffix( "" ), m_iHeldArrow(0), m_iHoldStart(0), m_iHoldSteps(0)
{
	m_szBackground = 0;
	m_szLeftArrow = UI_LEFTARROW;
	m_szLeftArrowFocus = UI_LEFTARROWFOCUS;
	m_szRightArrow = UI_RIGHTARROW;
	m_szRightArrowFocus = UI_RIGHTARROWFOCUS;
	m_szDisplay[0] = 0;

	eTextAlignment = QM_CENTER;
	eFocusAnimation = QM_HIGHLIGHTIFFOCUS;
	iFlags |= QMF_DROPSHADOW;
}

/*
=================
CMenuSpinControl::Init
=================
*/
void CMenuSpinControl::VidInit( void )
{
	colorBase.SetDefault( uiColorHelp );

	BaseClass::VidInit();
}

/*
=================
CMenuSpinControl::ArrowAtCursor

-1 if the cursor is on the left arrow, 1 on the right one, 0 otherwise
=================
*/
int CMenuSpinControl::ArrowAtCursor()
{
	Size arrow;
	Point left, right;

	// calculate size and position for the arrows
	arrow.w = m_scSize.h + UI_OUTLINE_WIDTH * 2 * uiStatic.scaleX;
	arrow.h = m_scSize.h + UI_OUTLINE_WIDTH * 2 * uiStatic.scaleY;

	left.x = m_scPos.x + UI_OUTLINE_WIDTH * uiStatic.scaleX;
	left.y = m_scPos.y - UI_OUTLINE_WIDTH * uiStatic.scaleY;
	right.x = m_scPos.x + (m_scSize.w - arrow.w) - UI_OUTLINE_WIDTH * uiStatic.scaleX;
	right.y = m_scPos.y - UI_OUTLINE_WIDTH * uiStatic.scaleY;

	if( UI_CursorInRect( left, arrow ))
		return -1;
	if( UI_CursorInRect( right, arrow ))
		return 1;
	return 0;
}

/*
=================
CMenuSpinControl::Step

one step left (dir < 0) or right; at the end of the range it buzzes and ends a hold
=================
*/
void CMenuSpinControl::Step( int dir, int event )
{
	const char *sound = dir < 0 ? MoveLeft() : MoveRight();

	_Event( event );
	if( sound != uiStatic.sounds[SND_BUZZ] )
	{
		Display();
		_Event( QM_CHANGED );
	}
	else m_iHeldArrow = 0; // end of the range: stop repeating

	// repeated steps are silent, only the first one and the end of the range click
	if( event != QM_CHANGED || sound == uiStatic.sounds[SND_BUZZ] )
		PlayLocalSound( sound );
}

/*
=================
CMenuSpinControl::RepeatHeldArrow

hold to repeat: while the mouse button (the VR trigger) is held on an arrow, step on after 0.4 s,
8 steps a second, 20 a second once held for 1.5 s
=================
*/
void CMenuSpinControl::RepeatHeldArrow()
{
	if( !m_iHeldArrow )
		return;

	// the cursor left the arrow or the control: stop
	if( this != m_pParent->ItemAtCursor() || ArrowAtCursor() != m_iHeldArrow || FBitSet( iFlags, QMF_GRAYED|QMF_INACTIVE ))
	{
		m_iHeldArrow = 0;
		return;
	}

	int held = uiStatic.realTime - m_iHoldStart;
	int due;

	if( held < 400 )
		due = 0;
	else if( held < 1500 )
		due = ( held - 400 ) * 8 / 1000 + 1;
	else due = 1100 * 8 / 1000 + 1 + ( held - 1500 ) * 20 / 1000;

	while( m_iHeldArrow && m_iHoldSteps < due )
	{
		m_iHoldSteps++;
		Step( m_iHeldArrow, QM_CHANGED );
	}
}

bool CMenuSpinControl::KeyDown( int key )
{
	const char *sound = 0;

	if( UI::Key::IsLeftMouse( key ) && FBitSet( iFlags, QMF_HASMOUSEFOCUS ))
	{
		// step on press, not on release, so holding the button can repeat
		int dir = ArrowAtCursor();

		if( !dir )
			return false;

		m_iHeldArrow = dir;
		m_iHoldStart = uiStatic.realTime;
		m_iHoldSteps = 0;
		Step( dir, QM_PRESSED );
		return true;
	}

	if( UI::Key::IsLeftArrow( key ) && !FBitSet( iFlags, QMF_MOUSEONLY ))
		sound = MoveLeft();
	else if( UI::Key::IsRightArrow( key ) && !FBitSet( iFlags, QMF_MOUSEONLY ))
		sound = MoveRight();

	if( sound )
	{
		_Event( QM_PRESSED );
		if( sound != uiStatic.sounds[SND_BUZZ] )
		{
			Display();
			_Event( QM_CHANGED );
		}
		PlayLocalSound( sound );
	}
	return sound != NULL;
}

/*
=================
CMenuSpinControl::Key
=================
*/
bool CMenuSpinControl::KeyUp( int key )
{
	// the mouse steps on press (KeyDown) and repeats while held; the release only ends the hold
	if( UI::Key::IsLeftMouse( key ) && m_iHeldArrow )
	{
		m_iHeldArrow = 0;
		_Event( QM_RELEASED );
		return true;
	}

	return false;
}

/*
=================
CMenuSpinControl::Draw
=================
*/
void CMenuSpinControl::Draw( void )
{
	int	leftFocus, rightFocus;
	Size arrow;
	Point left, right;
	Point scCenterPos;
	Size scCenterBox;
	uint textflags = ( iFlags & QMF_DROPSHADOW ) ? ETF_SHADOW : 0;

	RepeatHeldArrow();

	if( szStatusText && iFlags & QMF_NOTIFY )
	{
		Point coord;

		coord.x = m_scPos.x + m_scSize.w + 16 * uiStatic.scaleX;
		coord.y = m_scPos.y + m_scSize.h / 2 - EngFuncs::ConsoleCharacterHeight() / 2;

		int	r, g, b;

		UnpackRGB( r, g, b, uiColorHelp );
		EngFuncs::DrawSetTextColor( r, g, b );
		EngFuncs::DrawConsoleString( coord, szStatusText );
	}

	int textHeight = m_scPos.y - (m_scChSize * 1.5f);
	UI_DrawString( font, m_scPos.x - UI_OUTLINE_WIDTH, textHeight, m_scSize.w + UI_OUTLINE_WIDTH * 2, m_scChSize, szName, uiColorHelp, m_scChSize, QM_LEFT, textflags | ETF_FORCECOL | ETF_NOSIZELIMIT );

	// calculate size and position for the arrows
	arrow.w = m_scSize.h + UI_OUTLINE_WIDTH * 2;
	arrow.h = m_scSize.h + UI_OUTLINE_WIDTH * 2;

	left.x = m_scPos.x - UI_OUTLINE_WIDTH;
	left.y = m_scPos.y - UI_OUTLINE_WIDTH;
	right.x = m_scPos.x + (m_scSize.w - arrow.w) + UI_OUTLINE_WIDTH;
	right.y = m_scPos.y - UI_OUTLINE_WIDTH;

	scCenterPos.x = m_scPos.x + arrow.w;
	scCenterPos.y = m_scPos.y;

	scCenterBox.w = m_scSize.w - arrow.w * 2;
	scCenterBox.h = m_scSize.h;


	if( m_szBackground )
	{
		UI_DrawPic( scCenterPos, scCenterBox, uiColorWhite, m_szBackground );
	}
	else
	{
		// draw the background
		UI_FillRect( scCenterPos, scCenterBox, uiColorBlack );

		// draw the rectangle
		UI_DrawRectangle( scCenterPos, scCenterBox, uiInputFgColor );
	}

	if( iFlags & QMF_GRAYED )
	{
		UI::Scissor::PushScissor( scCenterPos, scCenterBox );
		UI_DrawString( font, scCenterPos, scCenterBox, m_szDisplay, uiColorDkGrey, m_scChSize, eTextAlignment, textflags | ETF_FORCECOL );
		UI::Scissor::PopScissor();
		UI_DrawPic( left, arrow, uiColorDkGrey, m_szLeftArrow );
		UI_DrawPic( right, arrow, uiColorDkGrey, m_szRightArrow );
		return; // grayed
	}

	if(this != m_pParent->ItemAtCursor())
	{
		UI::Scissor::PushScissor( scCenterPos, scCenterBox );
		UI_DrawString( font, scCenterPos, scCenterBox, m_szDisplay, colorBase, m_scChSize, eTextAlignment, textflags );
		UI::Scissor::PopScissor();
		UI_DrawPic(left, arrow, colorBase, m_szLeftArrow);
		UI_DrawPic(right, arrow, colorBase, m_szRightArrow);
		return;		// No focus
	}

	// see which arrow has the mouse focus
	leftFocus = UI_CursorInRect( left, arrow );
	rightFocus = UI_CursorInRect( right, arrow );

	if( eFocusAnimation == QM_HIGHLIGHTIFFOCUS )
	{
		UI::Scissor::PushScissor( scCenterPos, scCenterBox );
		UI_DrawString( font, scCenterPos, scCenterBox, m_szDisplay, colorFocus, m_scChSize, eTextAlignment, textflags );
		UI::Scissor::PopScissor();
		UI_DrawPic( left, arrow, colorBase, (leftFocus) ? m_szLeftArrowFocus : m_szLeftArrow );
		UI_DrawPic( right, arrow, colorBase, (rightFocus) ? m_szRightArrowFocus : m_szRightArrow );
	}
	else if( eFocusAnimation == QM_PULSEIFFOCUS )
	{
		int	color;

		color = PackAlpha( colorBase, 255 * (0.5f + 0.5f * sin( (float)uiStatic.realTime / UI_PULSE_DIVISOR )));

		UI::Scissor::PushScissor( scCenterPos, scCenterBox );
		UI_DrawString( font, scCenterPos, scCenterBox, m_szDisplay, color, m_scChSize, eTextAlignment, textflags );
		UI::Scissor::PopScissor();
		UI_DrawPic( left, arrow, (leftFocus) ? color : (int)colorBase, (leftFocus) ? m_szLeftArrowFocus : m_szLeftArrow );
		UI_DrawPic( right, arrow, (rightFocus) ? color : (int)colorBase, (rightFocus) ? m_szRightArrowFocus : m_szRightArrow );
	}
}

const char *CMenuSpinControl::MoveLeft()
{
	const char *sound;

	if( m_flCurValue > m_flMinValue )
	{
		m_flCurValue -= m_flRange;
		m_flCurValue = roundf( m_flCurValue * 10000.0f ) / 10000.0f; // no float drift after many steps
		if( m_flCurValue < m_flMinValue )
			m_flCurValue = m_flMinValue;
		sound = uiStatic.sounds[SND_MOVE];
	}
	else sound = uiStatic.sounds[SND_BUZZ];

	return sound;
}

const char *CMenuSpinControl::MoveRight()
{
	const char *sound;

	if( m_flCurValue < m_flMaxValue )
	{
		m_flCurValue += m_flRange;
		m_flCurValue = roundf( m_flCurValue * 10000.0f ) / 10000.0f;
		if( m_flCurValue > m_flMaxValue )
			m_flCurValue = m_flMaxValue;
		sound = uiStatic.sounds[SND_MOVE];
	}
	else sound = uiStatic.sounds[SND_BUZZ];

	return sound;
}

void CMenuSpinControl::UpdateEditable()
{
	switch( m_eType )
	{
	case CVAR_STRING:
		SetCurrentValue( CvarString() );
		break;
	case CVAR_VALUE:
		SetCurrentValue( CvarValue() );
		break;
	}
}

void CMenuSpinControl::Setup( float minValue, float maxValue, float range )
{
	m_flMinValue = minValue;
	m_flMaxValue = maxValue;
	m_flRange = range;
}

void CMenuSpinControl::Setup( CMenuBaseArrayModel *model )
{
	m_pModel = model;
	m_flMinValue = 0;
	m_flMaxValue = model->GetRows() - 1;
	m_flRange = 1;
}

void CMenuSpinControl::SetCurrentValue( float curValue )
{
	bool notify = m_flCurValue != curValue;
	m_flCurValue = curValue;
	Display();

	if( notify ) _Event( QM_CHANGED );
}

void CMenuSpinControl::SetCurrentValue( const char *stringValue )
{
	ASSERT( m_pModel );

	if( !m_pModel )
		return;

	float oldValue = m_flCurValue;

	for( int i = 0; i <= (int)m_flMaxValue; i++ )
	{
		if( !strcmp( m_pModel->GetText( i ), stringValue ) )
		{
			m_flCurValue = i;
			Display();
			return;
		}
	}

	m_flCurValue = -1;
	SetCvarString( stringValue );
	ForceDisplayString( stringValue );

	if( oldValue != m_flCurValue )
		_Event( QM_CHANGED );
}

void CMenuSpinControl::SetDisplayPrecision( short precision )
{
	m_iFloatPrecision = precision;
}

void CMenuSpinControl::Display()
{
	if( !m_pModel )
	{
		SetCvarValue( m_flCurValue );

		// no "-0.0" after stepping down to zero
		float display = fabs( m_flCurValue ) < 0.00005f ? 0.0f : m_flCurValue;
		snprintf( m_szDisplay, sizeof( m_szDisplay ), "%.*f%s", m_iFloatPrecision, display, m_szSuffix );
	}
	else
	{
		ASSERT( m_flCurValue >= m_flMinValue && m_flCurValue <= m_flMaxValue );
		const char *stringValue = m_pModel->GetText( (int)m_flCurValue );

		switch( m_eType )
		{
		case CVAR_STRING: SetCvarString( stringValue ); break;
		case CVAR_VALUE: SetCvarValue( m_flCurValue ); break;
		}

		Q_strncpy( m_szDisplay, stringValue, sizeof( m_szDisplay ));
	}
}

void CMenuSpinControl::ForceDisplayString( const char *display )
{
	Q_strncpy( m_szDisplay, display, sizeof( m_szDisplay ));
}
