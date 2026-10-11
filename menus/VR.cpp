/*
VR.cpp - VR settings pages (Lambda1VR-PC / OpenXR builds)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Every setting here is an existing console variable; the pages are a front end for them.
Numbers are arrow controls (exact values, hold the trigger on an arrow to repeat), shown in
friendly units (cm, degrees, %) and converted back when written. Changes apply at once, so a
change can be checked by closing the menu; leaving a page writes the config.
*/

#include "Framework.h"
#include "PicButton.h"
#include "CheckBox.h"
#include "SpinControl.h"
#include "Action.h"
#include "YesNoMessageBox.h"
#include "StringArrayModel.h"

#define VR_MAX_ITEMS   32
#define VR_MAX_CHOICES 4

/*
 * A number or named choice linked to a cvar, or to one of the three numbers in a position cvar
 * ("right up back" in metres). The cvar value is value / scale on the screen and choice values
 * map to their names.
 */
class CMenuVRSpin : public CMenuSpinControl
{
public:
	CMenuVRSpin() : scale( 1.0f ), component( -1 ), numChoices( 0 ), model( NULL ), offBox( NULL ),
		label( "" ), labelWidth( 0 ) { }
	~CMenuVRSpin() { delete model; }

	// the label goes left of the arrows (the stock one above them takes a whole extra row)
	void Draw() override
	{
		Point pos( m_scPos.x - labelWidth * uiStatic.scaleX, m_scPos.y );
		Size size( labelWidth * uiStatic.scaleX, m_scSize.h );
		uint flags = FBitSet( iFlags, QMF_DROPSHADOW ) ? ETF_SHADOW : 0;

		UI_DrawString( font, pos, size, label, FBitSet( iFlags, QMF_GRAYED ) ? uiColorDkGrey : uiColorHelp,
			m_scChSize, QM_LEFT, flags | ETF_FORCECOL );
		CMenuSpinControl::Draw();
	}

	float scale;
	int component; // -1 = the whole cvar, 0-2 = one number of a position
	int numChoices;
	const char *names[VR_MAX_CHOICES];
	float values[VR_MAX_CHOICES];
	CStringArrayModel *model;

	// set for a value that is "off" at 0 (physical crouch): the box shows whether it is on,
	// the spin keeps the last value to write when it is ticked again
	CMenuCheckBox *offBox;
	float offFallback;

	const char *label;
	int labelWidth;

	float ReadCvar() const
	{
		const char *str = EngFuncs::GetCvarString( CvarName() );
		float v[3] = { 0, 0, 0 };

		if( component < 0 )
			return atof( str );

		sscanf( str, "%f %f %f", &v[0], &v[1], &v[2] );
		return v[component];
	}

	void WriteValue()
	{
		float v;
		char buf[64];

		if( numChoices )
			v = values[(int)GetCurrentValue()];
		else v = GetCurrentValue() / scale;

		if( offBox && !offBox->bChecked )
			v = 0.0f;

		if( component < 0 )
		{
			snprintf( buf, sizeof( buf ), "%g", v );
		}
		else
		{
			float pos[3] = { 0, 0, 0 };
			sscanf( EngFuncs::GetCvarString( CvarName() ), "%f %f %f", &pos[0], &pos[1], &pos[2] );
			pos[component] = roundf( v * 10000.0f ) / 10000.0f;
			snprintf( buf, sizeof( buf ), "%g %g %g", pos[0], pos[1], pos[2] );
		}

		EngFuncs::CvarSetString( CvarName(), buf );
	}

	static void GetCb( CMenuBaseItem *pSelf, void * )
	{
		CMenuVRSpin *self = (CMenuVRSpin *)pSelf;
		float v = self->ReadCvar();

		if( self->offBox )
		{
			// the engine treats 0 and anything from 0.98 up as off
			self->offBox->bChecked = v > 0.0f && v < 0.98f;
			if( !self->offBox->bChecked )
				v = self->offFallback / self->scale;
		}

		if( self->numChoices )
		{
			int best = 0;
			for( int i = 1; i < self->numChoices; i++ )
			{
				if( fabs( self->values[i] - v ) < fabs( self->values[best] - v ))
					best = i;
			}
			self->SetOriginalValue( best );
		}
		else self->SetOriginalValue( v * self->scale );
	}
};

class CMenuVRPage : public CMenuFramework
{
public:
	typedef CMenuFramework BaseClass;

	CMenuVRPage( const char *name ) : BaseClass( name ), m_iItems( 0 ), m_bLoading( false ) { }
	~CMenuVRPage() override;

protected:
	// page builders, used by _Init; items are laid out top to bottom, column by column
	void Heading( const char *text );
	CMenuCheckBox *Check( const char *label, const char *cvar, const char *def );
	CMenuVRSpin *Number( const char *label, const char *cvar, const char *def, float min, float max,
		float step, int precision = 0, const char *unit = "", float scale = 1.0f );
	CMenuVRSpin *Choice( const char *label, const char *cvar, const char *def, int count,
		const char **names, const float *values );
	void Position( const char *what, const char *cvar, const char *def );
	void NextColumn() { m_bBreak[m_iItems] = true; }
	void DependsOn( CMenuBaseItem *item, CMenuCheckBox *box );

	void AddCommonButtons();

	void _VidInit() override;
	void Reload() override;
	void Hide() override;

private:
	enum EKind { HEADING, CHECK, SPIN };
	CMenuBaseItem *Add( EKind kind, CMenuBaseItem *item, const char *cvar, const char *def );
	void UpdateGrayed();
	void ResetPage();
	static void ChangedCb( CMenuBaseItem *pSelf, void *pExtra );

	CMenuBaseItem *m_pItems[VR_MAX_ITEMS];
	EKind m_eKind[VR_MAX_ITEMS];
	const char *m_szCvar[VR_MAX_ITEMS];
	const char *m_szDefault[VR_MAX_ITEMS];
	CMenuCheckBox *m_pDepends[VR_MAX_ITEMS];
	bool m_bBreak[VR_MAX_ITEMS + 1];
	int m_iItems;
	bool m_bLoading; // reading the cvars: changes are not written back

	CMenuYesNoMessageBox m_ResetBox;
};

CMenuVRPage::~CMenuVRPage()
{
	for( int i = 0; i < m_iItems; i++ )
	{
		RemoveItem( *m_pItems[i] );
		delete m_pItems[i];
	}
}

CMenuBaseItem *CMenuVRPage::Add( EKind kind, CMenuBaseItem *item, const char *cvar, const char *def )
{
	if( m_iItems >= VR_MAX_ITEMS )
	{
		Host_Error( "Too many items on a VR page!" );
		return item;
	}

	m_pItems[m_iItems] = item;
	m_eKind[m_iItems] = kind;
	m_szCvar[m_iItems] = cvar;
	m_szDefault[m_iItems] = def;
	m_pDepends[m_iItems] = NULL;
	m_bBreak[m_iItems + 1] = false;
	m_iItems++;
	AddItem( item );
	return item;
}

void CMenuVRPage::Heading( const char *text )
{
	CMenuAction *label = new CMenuAction();

	label->iFlags = QMF_INACTIVE | QMF_DROPSHADOW;
	label->colorBase = uiPromptTextColor;
	label->eTextAlignment = QM_LEFT;
	label->szName = text;
	Add( HEADING, label, NULL, NULL );
}

CMenuCheckBox *CMenuVRPage::Check( const char *label, const char *cvar, const char *def )
{
	CMenuCheckBox *box = new CMenuCheckBox();

	box->szName = label;
	box->onChanged = CEventCallback( ChangedCb, this );
	Add( CHECK, box, cvar, def );
	if( cvar )
		box->LinkCvar( cvar );
	return box;
}

CMenuVRSpin *CMenuVRPage::Number( const char *label, const char *cvar, const char *def, float min, float max,
	float step, int precision, const char *unit, float scale )
{
	CMenuVRSpin *spin = new CMenuVRSpin();

	spin->label = label;
	spin->scale = scale;
	spin->Setup( min, max, step );
	spin->SetDisplayPrecision( precision );
	spin->SetDisplaySuffix( unit );
	spin->onCvarGet = CMenuVRSpin::GetCb;
	spin->onChanged = CEventCallback( ChangedCb, this );
	Add( SPIN, spin, cvar, def );
	return spin;
}

CMenuVRSpin *CMenuVRPage::Choice( const char *label, const char *cvar, const char *def, int count,
	const char **names, const float *values )
{
	CMenuVRSpin *spin = new CMenuVRSpin();

	spin->label = label;
	spin->numChoices = count;
	for( int i = 0; i < count; i++ )
	{
		spin->names[i] = names[i];
		spin->values[i] = values[i];
	}
	spin->model = new CStringArrayModel( spin->names, count );
	spin->Setup( spin->model );
	spin->onCvarGet = CMenuVRSpin::GetCb;
	spin->onChanged = CEventCallback( ChangedCb, this );
	Add( SPIN, spin, cvar, def );
	return spin;
}

void CMenuVRPage::Position( const char *heading, const char *cvar, const char *def )
{
	static const char *axes[3] = { "Right", "Up", "Forward" };
	// "back" in the cvar, shown as forward
	static const float scales[3] = { 100.0f, 100.0f, -100.0f };

	if( heading )
		Heading( heading );
	for( int i = 0; i < 3; i++ )
		Number( L( axes[i] ), cvar, def, -40.0f, 40.0f, 0.5f, 1, " cm", scales[i] )->component = i;
}

void CMenuVRPage::DependsOn( CMenuBaseItem *item, CMenuCheckBox *box )
{
	for( int i = 0; i < m_iItems; i++ )
	{
		if( m_pItems[i] == item )
			m_pDepends[i] = box;
	}
}

void CMenuVRPage::ChangedCb( CMenuBaseItem *pSelf, void *pExtra )
{
	CMenuVRPage *page = (CMenuVRPage *)pExtra;

	if( page->m_bLoading )
		return;

	for( int i = 0; i < page->m_iItems; i++ )
	{
		CMenuBaseItem *item = page->m_pItems[i];
		if( !page->m_szCvar[i] )
			continue;

		if( page->m_eKind[i] == SPIN )
		{
			// a spin with an on/off box is written when either changes
			CMenuVRSpin *spin = (CMenuVRSpin *)item;
			if( item == pSelf || spin->offBox == pSelf )
				spin->WriteValue();
		}
		else if( item == pSelf )
			((CMenuEditable *)item)->WriteCvar();
	}

	page->UpdateGrayed();
}

void CMenuVRPage::UpdateGrayed()
{
	for( int i = 0; i < m_iItems; i++ )
	{
		if( m_pDepends[i] )
			m_pItems[i]->SetGrayed( !m_pDepends[i]->bChecked );
	}
}

void CMenuVRPage::ResetPage()
{
	for( int i = 0; i < m_iItems; i++ )
	{
		if( m_szCvar[i] && m_szDefault[i] )
			EngFuncs::CvarSetString( m_szCvar[i], m_szDefault[i] );
	}

	m_bLoading = true;
	for( int i = 0; i < m_iItems; i++ )
	{
		if( m_szCvar[i] )
			m_pItems[i]->Reload();
	}
	m_bLoading = false;
	UpdateGrayed();
}

void CMenuVRPage::AddCommonButtons()
{
	m_ResetBox.SetMessage( L( "Reset the settings on this page to their defaults?" ) );
	m_ResetBox.onPositive = VoidCb( &CMenuVRPage::ResetPage );
	m_ResetBox.Link( this );

	AddButton( L( "Done" ), NULL, PC_DONE, VoidCb( &CMenuVRPage::Hide ));
	AddButton( L( "Reset page" ), NULL, "gfx/shell/btn_vr_reset", m_ResetBox.MakeOpenEvent() );
}

void CMenuVRPage::_VidInit()
{
	// two columns of rows right of the buttons: label | < value >
	const int left = 330, top = 220, rowHeight = 42, rows = 13;
	int colWidth = ( uiStatic.width - left - 20 ) / 2;
	int spinWidth = 230, labelWidth = colWidth - spinWidth - 30;
	int x = left, row = 0;

	for( int i = 0; i < m_iItems; i++ )
	{
		CMenuBaseItem *item = m_pItems[i];

		if( row > 0 && ( m_bBreak[i] || row >= rows ))
		{
			x += colWidth;
			row = 0;
		}

		int y = top + row * rowHeight;

		switch( m_eKind[i] )
		{
		case HEADING:
			item->SetRect( x, y + 4, colWidth - 20, 32 );
			break;
		case CHECK:
			item->SetCoord( x, y );
			break;
		case SPIN:
			((CMenuVRSpin *)item)->labelWidth = labelWidth;
			item->SetRect( x + labelWidth, y, spinWidth, 32 );
			break;
		}

		row++;
	}

	m_bLoading = true;
	for( int i = 0; i < m_iItems; i++ )
	{
		if( m_szCvar[i] )
			((CMenuEditable *)m_pItems[i])->LinkCvar( m_szCvar[i], CMenuEditable::CVAR_VALUE );
	}
	m_bLoading = false;
	UpdateGrayed();
}

// the window reloads every item from its cvar when shown: only read, never write back (a value the page
// has no choice for, e.g. another vr_control_scheme, must stay as it is until the user changes it)
void CMenuVRPage::Reload()
{
	m_bLoading = true;
	BaseClass::Reload();
	m_bLoading = false;
	UpdateGrayed();
}

// however the page is left (Done, Escape, the controller's menu button) the config is written, so a change
// survives a crash; the values themselves were already applied on every click
void CMenuVRPage::Hide()
{
	EngFuncs::ClientCmd( false, "host_writeconfig\n" );
	BaseClass::Hide();
}

/*
 * Page 1: comfort and movement
 */
class CMenuVRComfort : public CMenuVRPage
{
public:
	CMenuVRComfort() : CMenuVRPage( "CMenuVRComfort" ) { }
private:
	void _Init() override
	{
		static const char *turnNames[] = { "Snap", "Smooth" };
		static const float turnValues[] = { 0, 1 };
		static const char *dirNames[] = { "Head", "Off hand" };
		static const float walkValues[] = { 1, 0 };
		static const float ladderValues[] = { 0, 1 };
		static const char *layoutNames[] = { "HL2VR", "Lambda1VR" };
		static const float layoutValues[] = { 1, 0 };
		static const char *handNames[] = { "Right", "Left" };
		static const float handValues[] = { 0, 10 };

		banner.szName = L( "Comfort and movement" );
		AddItem( banner );
		AddCommonButtons();

		Heading( L( "Turning" ));
		Choice( L( "Turn mode" ), "vr_smoothturn", "1", 2, turnNames, turnValues );
		Number( L( "Snap angle / speed" ), "vr_turn_angle", "45", 15, 90, 5, 0, "\xc2\xb0" );
		Number( L( "Comfort mask" ), "vr_comfort_mask", "0", 0, 100, 5, 0, " %", 100.0f );
		Heading( L( "Height and crouching" ));
		Number( L( "Extra eye height" ), "vr_height_adjust", "0.1", -30, 50, 1, 0, " cm", 100.0f );
		CMenuCheckBox *crouch = Check( L( "Physical crouch" ), NULL, "0" );
		CMenuVRSpin *below = Number( L( "Crouch below" ), "vr_enable_crouching", "0",
			50, 95, 5, 0, " %", 100.0f );
		below->offBox = crouch;
		below->offFallback = 85.0f;
		DependsOn( below, crouch );

		NextColumn();
		Heading( L( "Movement and buttons" ));
		Choice( L( "Walk direction" ), "vr_walkdirection", "1", 2, dirNames, walkValues );
		Choice( L( "Ladder direction" ), "vr_ladder_aim", "0", 2, dirNames, ladderValues );
		Choice( L( "Jump / duck buttons" ), "vr_swap_jumpduck", "1", 2, layoutNames, layoutValues );
		Choice( L( "Weapon hand" ), "vr_control_scheme", "0", 2, handNames, handValues );
		Check( L( "Double-tap jump = crouch jump" ), "vr_quick_crouchjump", "0" );
	}
};

/*
 * Page 2: weapons and interaction, timing and stick feel
 */
class CMenuVRWeapons : public CMenuVRPage
{
public:
	CMenuVRWeapons() : CMenuVRPage( "CMenuVRWeapons" ) { }
private:
	void _Init() override
	{
		static const char *laserNames[] = { "Off", "Beam + dot", "Dot only" };
		static const float laserValues[] = { 0, 1, 2 };
		static const char *stickNames[] = { "Previous", "Next" };
		static const float stickValues[] = { 0, 1 };

		banner.szName = L( "Weapons" );
		AddItem( banner );
		AddCommonButtons();

		Heading( L( "Weapons" ));
		Choice( L( "Laser sight" ), "vr_lasersight", "1", 3, laserNames, laserValues );
		Number( L( "Laser beam width" ), "vr_laser_width", "0.3", 0.05f, 1.0f, 0.05f, 2 );
		Number( L( "Laser dot size" ), "vr_laser_dot_scale", "0.08", 0.02f, 0.5f, 0.02f, 2 );
		Number( L( "Gun pitch" ), "vr_weapon_pitchadjust", "-20", -45, 10, 1, 0, "\xc2\xb0" );
		Number( L( "Crowbar pitch" ), "vr_crowbar_pitchadjust", "-25", -45, 10, 1, 0, "\xc2\xb0" );
		Check( L( "Scope stabilisation" ), "vr_scope_stabilise", "1" );
		Heading( L( "Hands" ));
		CMenuCheckBox *reach = Check( L( "Use by reaching out" ), "vr_gesture_triggered_use", "1" );
		DependsOn( Number( L( "Reach distance" ), "vr_use_gesture_boundary", "0.25", 15, 60, 1, 0, " cm", 100.0f ), reach );
		Check( L( "Haptic bumps (backpack, edges)" ), "vr_controller_tracking_haptic", "1" );

		NextColumn();
		Heading( L( "Weapon selection" ));
		Check( L( "Weapon grid on Triangle" ), "vr_weapon_pointer", "1" );
		CMenuCheckBox *stick = Check( L( "Stick weapon menu" ), "vr_weapon_stick_menu", "0" );
		DependsOn( Choice( L( "Stick up selects" ), "vr_weapon_stick_up_next", "0", 2, stickNames, stickValues ), stick );
		DependsOn( Number( L( "Stick push to open" ), "vr_weapon_stick_threshold", "0.85", 0.5f, 1.0f, 0.05f, 2 ), stick );
		DependsOn( Number( L( "Stick hold to open" ), "vr_weapon_stick_hold", "0.15", 0, 0.5f, 0.05f, 2, " s" ), stick );
		Heading( L( "Timing" ));
		Number( L( "Reload: longest tap" ), "vr_reloadtimeoutms", "200", 100, 500, 25, 0, " ms" );
		Number( L( "Stick double click" ), "vr_laser_doubleclick", "0.35", 0.2f, 0.6f, 0.05f, 2, " s" );
		Heading( L( "Models and flashlight" ));
		Check( L( "Hand model" ), "vr_hand_model", "1" );
		Check( L( "Flashlight model" ), "vr_flashlight_model", "1" );
		Check( L( "Flashlight on the head" ), "vr_headtorch", "0" );
	}
};

/*
 * Page 3: hand HUD
 */
class CMenuVRHandHud : public CMenuVRPage
{
public:
	CMenuVRHandHud() : CMenuVRPage( "CMenuVRHandHud" ) { }
private:
	void _Init() override
	{
		banner.szName = L( "Hand HUD" );
		AddItem( banner );
		AddCommonButtons();

		// left column: everything on the off hand, right column: the weapon hand
		Heading( L( "Health panel (off hand)" ));
		CMenuVRSpin *size = Number( L( "Size" ), "vr_hud_hand_scale", "0.0005", 50, 200, 5, 0, " %", 200000.0f );
		CMenuVRSpin *tilt = Number( L( "Tilt" ), "vr_hud_hand_tilt", "-20", -90, 90, 5, 0, "\xc2\xb0" );
		CMenuVRSpin *yaw = Number( L( "Turn inwards" ), "vr_hud_hand_yaw", "90", -180, 180, 5, 0, "\xc2\xb0" );
		Position( NULL, "vr_hud_status_offset", "0.025 0.035 -0.02" );
		Heading( L( "Weapon grid (hold Triangle)" ));
		Number( L( "Size" ), "vr_weapon_pointer_scale", "0.00065", 50, 200, 5, 0, " %", 200000.0f );
		Number( L( "Turn inwards" ), "vr_weapon_pointer_yaw", "40", -180, 180, 5, 0, "\xc2\xb0" );
		Position( NULL, "vr_weapon_pointer_offset", "0 0.035 -0.35" );

		NextColumn();
		CMenuCheckBox *hands = Check( L( "HUD on the hands" ), "vr_hud_hands", "1" );
		DependsOn( size, hands );
		DependsOn( tilt, hands );
		DependsOn( yaw, hands );
		Heading( L( "Ammo panel (weapon hand)" ));
		DependsOn( Number( L( "Size" ), "vr_hud_ammo_scale", "0.0005", 50, 200, 5, 0, " %", 200000.0f ), hands );
		DependsOn( Number( L( "Tilt" ), "vr_hud_ammo_tilt", "0", -90, 90, 5, 0, "\xc2\xb0" ), hands );
		DependsOn( Number( L( "Turn inwards" ), "vr_hud_ammo_yaw", "20", -180, 180, 5, 0, "\xc2\xb0" ), hands );
		Position( NULL, "vr_hud_ammo_offset", "-0.01 0.02 -0.05" );
		Heading( L( "Close the menu to see a change" ));
	}
};

/*
 * Page 4: head HUD, menu panel and cinema screen
 */
class CMenuVRScreen : public CMenuVRPage
{
public:
	CMenuVRScreen() : CMenuVRPage( "CMenuVRScreen" ) { }
private:
	void _Init() override
	{
		banner.szName = L( "HUD and cinema" );
		AddItem( banner );
		AddCommonButtons();

		Heading( L( "HUD, menu and cinema screen" ));
		Number( L( "Distance" ), "vr_ui_distance", "2.2", 0.5f, 5.0f, 0.1f, 1, " m" );
		Number( L( "Width" ), "vr_ui_width", "2.6", 0.5f, 6.0f, 0.1f, 1, " m" );
		Number( L( "Follow head beyond" ), "vr_panel_head_angle", "70", 0, 180, 5, 0, "\xc2\xb0" );
		Number( L( "HUD glow" ), "vr_ui_glow_curve", "1.6", 0.5f, 4.0f, 0.1f, 1 );
		Heading( L( "Head-locked HUD position" ));
		Number( L( "Sideways" ), "vr_ui_offset_x", "-0.1", -100, 100, 1, 0, " cm", 100.0f );
		Number( L( "Up" ), "vr_ui_offset_y", "0", -100, 100, 1, 0, " cm", 100.0f );
		Check( L( "Console messages top left" ), "vr_notify", "0" );

		NextColumn();
		Heading( L( "Cinema (scripted cameras)" ));
		CMenuCheckBox *cinema = Check( L( "Show on a screen in the room" ), "vr_cinema", "1" );
		DependsOn( Number( L( "Fade time" ), "vr_cinema_fade", "0.2", 0, 1, 0.1f, 1, " s" ), cinema );
		Heading( L( "Rendering (after a restart)" ));
		Number( L( "Resolution" ), "vr_supersampling", "1.0", 0.5f, 2.0f, 0.1f, 1, "x" );
	}
};

ADD_MENU( menu_vr_comfort, CMenuVRComfort, UI_VRComfort_Menu );
ADD_MENU( menu_vr_weapons, CMenuVRWeapons, UI_VRWeapons_Menu );
ADD_MENU( menu_vr_handhud, CMenuVRHandHud, UI_VRHandHud_Menu );
ADD_MENU( menu_vr_screen, CMenuVRScreen, UI_VRScreen_Menu );

/*
 * Counter-Strike (the engine's CS VR mode, -game cstrike / czero): its own page set, the Half-Life pages above
 * stay as they are. Button mappings (vr_button_*) stay console-only.
 */
static bool UI_VRIsCounterStrike( void )
{
	return !stricmp( gMenu.m_gameinfo.gamefolder, "cstrike" ) || !stricmp( gMenu.m_gameinfo.gamefolder, "czero" );
}

class CMenuVRCSComfort : public CMenuVRPage
{
public:
	CMenuVRCSComfort() : CMenuVRPage( "CMenuVRCSComfort" ) { }
private:
	void _Init() override
	{
		static const char *turnNames[] = { "Snap", "Smooth" };
		static const float turnValues[] = { 0, 1 };
		static const char *dirNames[] = { "Head", "Off hand" };
		static const float walkValues[] = { 1, 0 };
		static const char *handNames[] = { "Right", "Left" };
		static const float handValues[] = { 0, 10 };

		banner.szName = L( "Comfort and movement" );
		AddItem( banner );
		AddCommonButtons();

		Heading( L( "Turning" ));
		Choice( L( "Turn mode" ), "vr_smoothturn", "1", 2, turnNames, turnValues );
		Number( L( "Snap angle / speed" ), "vr_turn_angle", "45", 15, 90, 5, 0, "\xc2\xb0" );
		Number( L( "Right stick dead zone" ), "vr_thumbstick_deadzone_right", "0.8", 30, 95, 5, 0, " %", 100.0f );
		Number( L( "Comfort mask" ), "vr_comfort_mask", "0", 0, 100, 5, 0, " %", 100.0f );

		NextColumn();
		Heading( L( "Body" ));
		Check( L( "Room-scale walking (6DoF)" ), "vr_6dof", "1" );
		Number( L( "Extra eye height" ), "vr_height_adjust", "0.1", -30, 50, 1, 0, " cm", 100.0f );
		Choice( L( "Walk direction" ), "vr_walkdirection", "1", 2, dirNames, walkValues );
		Choice( L( "Weapon hand" ), "vr_control_scheme", "0", 2, handNames, handValues );
	}
};

class CMenuVRCSWeapons : public CMenuVRPage
{
public:
	CMenuVRCSWeapons() : CMenuVRPage( "CMenuVRCSWeapons" ) { }
private:
	void _Init() override
	{
		static const char *gripNames[] = { "Grip (palm)", "Aim point" };
		static const float gripValues[] = { 1, 0 };

		banner.szName = L( "Weapons" );
		AddItem( banner );
		AddCommonButtons();

		Heading( L( "Aiming" ));
		CMenuCheckBox *dot = Check( L( "Aim dot" ), "vr_aimdot", "1" );
		DependsOn( Number( L( "Aim dot size" ), "vr_aimdot_scale", "0.1", 0.02f, 0.5f, 0.02f, 2 ), dot );
		Check( L( "Shots go where the barrel points" ), "vr_cs_converge", "1" );
		Choice( L( "Weapon held at" ), "vr_cs_grip_origin", "1", 2, gripNames, gripValues );
		Heading( L( "Grenades" ));
		Number( L( "Throw strength" ), "vr_throw_scale", "3.75", 1.0f, 8.0f, 0.25f, 2, "x" );
		// the fastest hand speed in this time before the release counts (0 = the release frame, as in Half-Life)
		Number( L( "Throw speed from the last" ), "vr_throw_window", "0.1", 0, 300, 20, 0, " ms", 1000.0f );

		NextColumn();
		Heading( L( "Feel" ));
		Check( L( "Controller haptics" ), "vr_haptics_enable", "1" );
		Heading( L( "Weapon grip calibration" ));
		Check( L( "Right stick adjusts the grip" ), "vr_weapon_calibration_on", "0" );
		Heading( L( "(left/right picks the value, up/down" ));
		Heading( L( " changes it; turn it off to play)" ));
	}
};

class CMenuVRCSHandHud : public CMenuVRPage
{
public:
	CMenuVRCSHandHud() : CMenuVRPage( "CMenuVRCSHandHud" ) { }
private:
	void _Init() override
	{
		banner.szName = L( "Hand HUD" );
		AddItem( banner );
		AddCommonButtons();

		// left column: the watch on the off hand, right column: the weapon hand
		Heading( L( "Watch: radar (off hand)" ));
		CMenuVRSpin *size = Number( L( "Radar size" ), "vr_hud_radar_scale", "0.0005", 50, 200, 5, 0, " %", 200000.0f );
		CMenuVRSpin *strip = Number( L( "Numbers size" ), "vr_hud_strip_scale", "0.0004", 50, 200, 5, 0, " %", 250000.0f );
		CMenuVRSpin *tilt = Number( L( "Tilt" ), "vr_hud_radar_tilt", "-75", -90, 90, 5, 0, "\xc2\xb0" );
		CMenuVRSpin *yaw = Number( L( "Turn inwards" ), "vr_hud_radar_yaw", "0", -180, 180, 5, 0, "\xc2\xb0" );
		CMenuVRSpin *spin = Number( L( "Rotate" ), "vr_hud_radar_spin", "90", -180, 180, 15, 0, "\xc2\xb0" );
		Position( NULL, "vr_hud_radar_offset", "0 0.02 0.08" );

		NextColumn();
		CMenuCheckBox *hands = Check( L( "HUD on the hands" ), "vr_hud_hands", "1" );
		DependsOn( size, hands );
		DependsOn( strip, hands );
		DependsOn( tilt, hands );
		DependsOn( yaw, hands );
		DependsOn( spin, hands );
		Heading( L( "Ammo panel (weapon hand)" ));
		DependsOn( Number( L( "Size" ), "vr_hud_ammo_scale", "0.0005", 50, 200, 5, 0, " %", 200000.0f ), hands );
		DependsOn( Number( L( "Tilt" ), "vr_hud_ammo_tilt", "0", -90, 90, 5, 0, "\xc2\xb0" ), hands );
		DependsOn( Number( L( "Turn inwards" ), "vr_hud_ammo_yaw", "20", -180, 180, 5, 0, "\xc2\xb0" ), hands );
		Position( NULL, "vr_hud_ammo_offset", "-0.01 0.02 -0.05" );
		Heading( L( "Close the menu to see a change" ));
	}
};

class CMenuVRCSGrid : public CMenuVRPage
{
public:
	CMenuVRCSGrid() : CMenuVRPage( "CMenuVRCSGrid" ) { }
private:
	void _Init() override
	{
		banner.szName = L( "Weapon grid" );
		AddItem( banner );
		AddCommonButtons();

		Heading( L( "Weapon grid (hold Triangle)" ));
		Number( L( "Size" ), "vr_weapon_pointer_scale", "0.00065", 50, 200, 5, 0, " %", 200000.0f );
		Number( L( "Tilt" ), "vr_hud_hand_tilt", "-20", -90, 90, 5, 0, "\xc2\xb0" );
		Number( L( "Turn inwards" ), "vr_weapon_pointer_yaw", "40", -180, 180, 5, 0, "\xc2\xb0" );

		NextColumn();
		Position( L( "Position from the off hand" ), "vr_weapon_pointer_offset", "0 0.035 -0.35" );
		Heading( L( "Close the menu to see a change" ));
	}
};

class CMenuVRCSBots : public CMenuVRPage
{
public:
	CMenuVRCSBots() : CMenuVRPage( "CMenuVRCSBots" ) { }
private:
	void _Init() override
	{
		static const char *skillNames[] = { "Easy", "Normal", "Hard", "Expert" };
		static const float skillValues[] = { 0, 1, 2, 3 };

		banner.szName = L( "Bots" );
		AddItem( banner );
		AddCommonButtons();

		Heading( L( "Offline games (New Game)" ));
		CMenuCheckBox *bots = Check( L( "Fill both teams with bots" ), "vr_cs_bots", "1" );
		DependsOn( Number( L( "Number of bots" ), "vr_cs_bot_count", "15", 1, 31, 1 ), bots );
		DependsOn( Choice( L( "Difficulty" ), "vr_cs_bot_difficulty", "1", 4, skillNames, skillValues ), bots );
		Number( L( "Round end after death" ), "vr_cs_skip_dead_round", "3", 0, 30, 1, 0, " s" );

		NextColumn();
		Heading( L( "The number applies at once," ));
		Heading( L( "difficulty to bots added later." ));
		Heading( L( "More bots than free slots: start" ));
		Heading( L( "a New Game with more Max players." ));
		Heading( L( "Round end 0 s = watch the round out." ));
	}
};

class CMenuVRCSScreen : public CMenuVRPage
{
public:
	CMenuVRCSScreen() : CMenuVRPage( "CMenuVRCSScreen" ) { }
private:
	void _Init() override
	{
		banner.szName = L( "HUD and scope" );
		AddItem( banner );
		AddCommonButtons();

		Heading( L( "HUD and menu panel" ));
		Number( L( "Distance" ), "vr_ui_distance", "2.2", 0.5f, 5.0f, 0.1f, 1, " m" );
		Number( L( "Width" ), "vr_ui_width", "2.6", 0.5f, 6.0f, 0.1f, 1, " m" );
		Number( L( "Follow head beyond" ), "vr_panel_head_angle", "70", 0, 180, 5, 0, "\xc2\xb0" );
		Number( L( "HUD glow" ), "vr_ui_glow_curve", "1.6", 0.5f, 4.0f, 0.1f, 1 );
		Heading( L( "Head-locked HUD position" ));
		Number( L( "Sideways" ), "vr_ui_offset_x", "-0.1", -100, 100, 1, 0, " cm", 100.0f );
		Number( L( "Up" ), "vr_ui_offset_y", "0", -100, 100, 1, 0, " cm", 100.0f );
		Check( L( "Console messages top left" ), "vr_notify", "0" );

		NextColumn();
		Heading( L( "Scope lens (weapon hand)" ));
		Number( L( "Size" ), "vr_scope_size", "0.05", 2, 15, 0.5f, 1, " cm", 100.0f );
		Position( NULL, "vr_scope_offset", "0 0.06 -0.08" );
		Heading( L( "Rendering (after a restart)" ));
		Number( L( "Resolution" ), "vr_supersampling", "1.0", 0.5f, 2.0f, 0.1f, 1, "x" );
	}
};

ADD_MENU( menu_vr_cs_comfort, CMenuVRCSComfort, UI_VRCSComfort_Menu );
ADD_MENU( menu_vr_cs_screen, CMenuVRCSScreen, UI_VRCSScreen_Menu );
ADD_MENU( menu_vr_cs_weapons, CMenuVRCSWeapons, UI_VRCSWeapons_Menu );
ADD_MENU( menu_vr_cs_handhud, CMenuVRCSHandHud, UI_VRCSHandHud_Menu );
ADD_MENU( menu_vr_cs_grid, CMenuVRCSGrid, UI_VRCSGrid_Menu );
ADD_MENU( menu_vr_cs_bots, CMenuVRCSBots, UI_VRCSBots_Menu );

/*
 * Options -> VR
 */
class CMenuVR : public CMenuFramework
{
public:
	CMenuVR() : CMenuFramework( "CMenuVR" ) { }
private:
	void _Init() override
	{
		banner.szName = L( "VR" );
		AddItem( banner );
		if( UI_VRIsCounterStrike( ))
		{
			AddButton( L( "Comfort and movement" ), L( "Turning, walking and height" ),
				"gfx/shell/btn_vr_comfort", UI_VRCSComfort_Menu, QMF_NOTIFY );
			AddButton( L( "Weapons" ), L( "Aim dot, grip, grenade throw, haptics, calibration" ),
				"gfx/shell/btn_vr_weapons", UI_VRCSWeapons_Menu, QMF_NOTIFY );
			AddButton( L( "Hand HUD" ), L( "Radar watch, numbers and ammo on your hands" ),
				"gfx/shell/btn_vr_handhud", UI_VRCSHandHud_Menu, QMF_NOTIFY );
			AddButton( L( "Weapon grid" ), L( "The weapon grid on Triangle" ),
				"gfx/shell/btn_vr_grid", UI_VRCSGrid_Menu, QMF_NOTIFY );
			AddButton( L( "Bots" ), L( "Bots in offline games: how many, how good" ),
				"gfx/shell/btn_vr_bots", UI_VRCSBots_Menu, QMF_NOTIFY );
			AddButton( L( "HUD and scope" ), L( "HUD and menu panel, scope lens, resolution" ),
				"gfx/shell/btn_vr_screen", UI_VRCSScreen_Menu, QMF_NOTIFY );
			AddButton( L( "Recenter" ), L( "Make where you stand and look now the centre" ),
				"gfx/shell/btn_vr_recenter", CEventCallback( 0, "vr_recenter\n" ), QMF_NOTIFY );
			AddButton( L( "Done" ), L( "Go back to Options" ),
				PC_DONE, VoidCb( &CMenuVR::SaveAndPopMenu ), QMF_NOTIFY );
			return;
		}
		AddButton( L( "Comfort and movement" ), L( "Turning, walking, height and crouching" ),
			"gfx/shell/btn_vr_comfort", UI_VRComfort_Menu, QMF_NOTIFY );
		AddButton( L( "Weapons" ), L( "Laser, weapon selection, timing, hands" ),
			"gfx/shell/btn_vr_weapons", UI_VRWeapons_Menu, QMF_NOTIFY );
		AddButton( L( "Hand HUD" ), L( "Health, ammo and weapon grid on your hands" ),
			"gfx/shell/btn_vr_handhud", UI_VRHandHud_Menu, QMF_NOTIFY );
		AddButton( L( "HUD and cinema" ), L( "HUD, menu and cinema screen size and position" ),
			"gfx/shell/btn_vr_screen", UI_VRScreen_Menu, QMF_NOTIFY );
		AddButton( L( "Recenter" ), L( "Make where you stand and look now the centre" ),
			"gfx/shell/btn_vr_recenter", CEventCallback( 0, "vr_recenter\n" ), QMF_NOTIFY );
		AddButton( L( "Done" ), L( "Go back to Options" ),
			PC_DONE, VoidCb( &CMenuVR::SaveAndPopMenu ), QMF_NOTIFY );
	}
};

ADD_MENU( menu_vr, CMenuVR, UI_VR_Menu );
