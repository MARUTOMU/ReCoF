//========= ReCoF (B398): the GameUI's look ====================================
//
// Every GameUI dialog (Options, the console, the server list, the in-game menu) takes its colours and fonts from the
// scheme the engine loads from resource/trackerscheme.res: grey 90 text on translucent grey 40 and Tahoma 16 in
// Cry of Fear, and whatever a mod writes into that file on the player's computer. RcTheme_Install puts a scheme
// manager of ReCoF's own between the GameUI's controls (vgui_controls, built into GameUI.dll) and vgui2.dll's: it
// hands out every scheme wrapped, and the wrapper answers the colours and fonts of ReCoF's theme, so the look is the
// same on every computer and no game file changes. Anything the theme does not name goes to the real scheme.
//
// The theme: dark panels, light grey text in three steps, Cry of Fear's red for selection and the hovered entry,
// thin grey borders; the font is Inter (SIL Open Font License, src/gameui/fonts), linked into GameUI.dll as RCDATA and
// registered for this process only with AddFontMemResourceEx, so no font is installed or added to the game folder.
//
//=============================================================================

#include <windows.h>
#include <string.h>

#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <vgui/IBorder.h>	// B413
#include <vgui/IPanel.h>
#include <vgui_controls/Controls.h>
#include "Color.h"

#undef CreateFont	// windows.h

namespace vgui2
{
extern ISchemeManager *g_pSchemeInterface;	// vgui_controls' scheme manager (controls.cpp)
}
using namespace vgui2;

// the RCDATA ids of tools/units.json "rcdata"
#define RC_FONT_INTER_REGULAR	101
#define RC_FONT_INTER_SEMIBOLD	102

//-----------------------------------------------------------------------------
// The palette: the scheme's base colours (its "Colors" section), then the BaseSettings names the controls ask for,
// each pointing at a base colour as trackerscheme.res does
//-----------------------------------------------------------------------------
struct RcThemeColor_t
{
	const char	*name;
	int			r, g, b, a;
};

static const RcThemeColor_t s_RcColors[] =
{
	// text
	{ "BaseText",			205, 205, 205, 255 },	// text panes, lists
	{ "BrightBaseText",		245, 245, 245, 255 },	// the brightest text
	{ "SelectedText",		255, 255, 255, 255 },
	{ "DimBaseText",		150, 150, 150, 255 },
	{ "LabelDimText",		150, 150, 150, 255 },	// help and info lines
	{ "ControlText",		210, 210, 210, 255 },	// every control's text
	{ "BrightControlText",	235, 70, 60, 255 },		// a selected control, a check, a section title: Cry of Fear's red
	{ "DisabledText1",		105, 105, 105, 255 },
	{ "DisabledText2",		20, 20, 20, 255 },		// the inset under disabled text
	{ "DimListText",		125, 125, 125, 255 },

	// backgrounds
	{ "ControlBG",			34, 34, 34, 255 },		// buttons, tabs, the dialog body
	{ "ControlDarkBG",		24, 24, 24, 255 },		// scroll bar and slider troughs
	{ "WindowBG",			16, 16, 16, 245 },		// text entries, consoles
	{ "SelectionBG",		150, 24, 20, 255 },		// a selected entry or text: Cry of Fear's deep red
	{ "SelectionBG2",		70, 22, 20, 255 },		// the same without focus
	{ "ListBG",				14, 14, 14, 240 },		// lists, the server list, check boxes

	// title bar
	{ "TitleText",			235, 235, 235, 255 },
	{ "TitleDimText",		140, 140, 140, 255 },
	{ "TitleBG",			12, 12, 12, 250 },
	{ "TitleDimBG",			20, 20, 20, 250 },

	// sliders
	{ "SliderTickColor",	110, 110, 110, 255 },
	{ "SliderTrackColor",	60, 60, 60, 255 },

	// borders: one thin grey line, the selected button's in red
	{ "BorderBright",		95, 95, 95, 255 },
	{ "BorderDark",			8, 8, 8, 255 },
	{ "BorderSelection",	235, 70, 60, 255 },

	// the in-game text menu (Taskbar.cpp): grey, red under the cursor as the picture menu's entries
	{ "InGameDesktop/MenuColor",			200, 200, 200, 255 },
	{ "InGameDesktop/ArmedMenuColor",		235, 70, 60, 255 },
	{ "InGameDesktop/DepressedMenuColor",	170, 30, 25, 255 },
};

struct RcThemeAlias_t
{
	const char	*name;
	const char	*base;
};

// trackerscheme.res BaseSettings: what each control's colour name means
static const RcThemeAlias_t s_RcAliases[] =
{
	{ "FgColor", "ControlText" },
	{ "BgColor", "ControlBG" },
	{ "LabelBgColor", "ControlBG" },
	{ "SubPanelBgColor", "ControlBG" },
	{ "DisabledFgColor1", "DisabledText1" },
	{ "DisabledFgColor2", "DisabledText2" },
	{ "TitleBarFgColor", "TitleText" },
	{ "TitleBarDisabledFgColor", "TitleDimText" },
	{ "TitleBarBgColor", "TitleBG" },
	{ "TitleBarDisabledBgColor", "TitleDimBG" },
	{ "TitleButtonFgColor", "BorderBright" },
	{ "TitleButtonBgColor", "ControlBG" },
	{ "TitleButtonDisabledFgColor", "TitleDimText" },
	{ "TitleButtonDisabledBgColor", "TitleDimBG" },
	{ "TextCursorColor", "BaseText" },
	{ "URLTextColor", "BrightBaseText" },
	{ "Menu/FgColor", "DimBaseText" },
	{ "Menu/BgColor", "ControlBG" },
	{ "Menu/ArmedFgColor", "BrightBaseText" },
	{ "Menu/ArmedBgColor", "SelectionBG" },
	{ "Menu/DividerColor", "BorderDark" },
	{ "MenuButton/ButtonArrowColor", "DimBaseText" },
	{ "MenuButton/ButtonBgColor", "WindowBG" },
	{ "MenuButton/ArmedArrowColor", "BrightBaseText" },
	{ "MenuButton/ArmedBgColor", "SelectionBG" },
	{ "Slider/SliderFgColor", "ControlText" },
	{ "Slider/SliderBgColor", "ControlDarkBG" },
	{ "ScrollBarSlider/BgColor", "ControlBG" },
	{ "ScrollBarSlider/ScrollBarSliderFgColor", "BorderBright" },
	{ "ScrollBarSlider/ScrollBarSliderBgColor", "ControlDarkBG" },
	{ "ScrollBarSlider/ButtonFgColor", "DimBaseText" },
	{ "WindowFgColor", "BaseText" },
	{ "WindowBgColor", "WindowBG" },
	{ "WindowDisabledFgColor", "DimBaseText" },
	{ "WindowDisabledBgColor", "ListBG" },
	{ "SelectionFgColor", "SelectedText" },
	{ "SelectionBgColor", "SelectionBG" },
	{ "ListSelectionFgColor", "SelectedText" },
	{ "ListBgColor", "ListBG" },
	{ "BuddyListBgColor", "ListBG" },
	{ "ChatBgColor", "WindowBG" },
	{ "StatusSelectFgColor", "BrightBaseText" },
	{ "StatusSelectFgColor2", "BrightControlText" },
	{ "CheckButtonBorder1", "BorderDark" },
	{ "CheckButtonBorder2", "BorderBright" },
	{ "CheckButtonCheck", "BrightControlText" },
	{ "CheckBgColor", "ListBG" },
	{ "SectionTextColor", "BrightControlText" },
	{ "SectionDividerColor", "BorderBright" },
	{ "ButtonFgColor", "ControlText" },
	{ "ButtonBgColor", "ControlBG" },
	{ "ButtonArmedFgColor", "BrightBaseText" },
	{ "ButtonArmedBgColor", "SelectionBG2" },
	{ "ButtonDepressedFgColor", "BrightBaseText" },
	{ "ButtonDepressedBgColor", "SelectionBG" },
	{ "ButtonFocusBorder", "BorderSelection" },
};

static bool RcThemeLookup( const char *name, Color &out )
{
	if ( !name )
		return false;
	int i;
	for ( i = 0; i < (int)( sizeof( s_RcAliases ) / sizeof( s_RcAliases[0] ) ); i++ )
	{
		if ( !stricmp( name, s_RcAliases[i].name ) )
		{
			name = s_RcAliases[i].base;
			break;
		}
	}
	for ( i = 0; i < (int)( sizeof( s_RcColors ) / sizeof( s_RcColors[0] ) ); i++ )
	{
		if ( !stricmp( name, s_RcColors[i].name ) )
		{
			out.SetColor( s_RcColors[i].r, s_RcColors[i].g, s_RcColors[i].b, s_RcColors[i].a );
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// The fonts: the scheme's names the controls ask for, in Inter (the registered faces "Inter" and "Inter SemiBold"),
// at the pixel heights of the retail scheme for now. Made on first use, once the surface is up; a face that fails
// (no font registered) falls back to the real scheme's font
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// B399: the scale of the GameUI: the screen's height over 720, at least 1 (the dialogs were laid out for 720 lines
// and look as before up to there), at most 4. RcUiScaled gives a 720p length at this screen; the theme's fonts,
// proportional layouts (GetProportionalScaledValue) and the dialogs' own sizes use it
//-----------------------------------------------------------------------------
float RcMenuScaleSetting( void );	// rc_menu_scale, RcOptions.cpp

float RcUiScale( void )
{
	int wide = 0, tall = 0;
	if ( surface() )
		surface()->GetScreenSize( wide, tall );
	// B423: the width limits it too, and below 720p it shrinks to 75 %: at 4:3 the Options window (660 x 500 at 720p)
	// took 82 % of an 800 x 600 screen and covered the game menu's list
	float scale = tall / 720.0f;
	if ( wide > 0 && wide / 1080.0f < scale )
		scale = wide / 1080.0f;
	if ( scale < 0.75f )
		scale = 0.75f;

	// the player's menu size (rc_menu_scale, 100 to 200 percent) on top, as far as the Options window (500 lines at
	// 720p) still fits on the screen
	float user = RcMenuScaleSetting() / 100.0f;
	if ( user < 1.0f )
		user = 1.0f;
	if ( user > 2.0f )
		user = 2.0f;
	scale *= user;
	float fit = tall * 0.92f / 500.0f;
	if ( fit >= 1.0f && scale > fit )
		scale = fit;

	if ( scale > 4.0f )
		scale = 4.0f;
	return scale;
}

int RcUiScaled( int value )
{
	return (int)( value * RcUiScale() + ( value >= 0 ? 0.5f : -0.5f ) );
}

struct RcThemeFont_t
{
	const char	*name;
	const char	*face;
	int			tall;
	int			weight;
	int			flags;
	HFont		font;
	float		scale;	// the scale it was made at; another screen height makes it again
};

static RcThemeFont_t s_RcFonts[] =
{
	{ "Default",			"Inter",			15, 400, ISurface::FONTFLAG_ANTIALIAS, 0, 0 },
	{ "DefaultBold",		"Inter SemiBold",	15, 600, ISurface::FONTFLAG_ANTIALIAS, 0, 0 },
	{ "DefaultUnderline",	"Inter",			15, 400, ISurface::FONTFLAG_ANTIALIAS | ISurface::FONTFLAG_UNDERLINE, 0, 0 },
	{ "DefaultSmall",		"Inter",			13, 400, ISurface::FONTFLAG_ANTIALIAS, 0, 0 },
	{ "DefaultSmallBold",	"Inter SemiBold",	13, 600, ISurface::FONTFLAG_ANTIALIAS, 0, 0 },
	{ "DefaultVerySmall",	"Inter",			12, 400, ISurface::FONTFLAG_ANTIALIAS, 0, 0 },
	{ "MenuLarge",			"Inter SemiBold",	22, 600, ISurface::FONTFLAG_ANTIALIAS, 0, 0 },	// the in-game text menu
	{ "UiHeadline",			"Inter SemiBold",	20, 600, ISurface::FONTFLAG_ANTIALIAS, 0, 0 },
	// the glyphs of checks, arrows and the close box: the retail size, grown with the rest (a proportional scheme
	// font would be sized from 480 lines)
	{ "Marlett",			"Marlett",			14, 0, ISurface::FONTFLAG_SYMBOL, 0, 0 },
};

static bool s_bRcFontsRegistered;

static HFont RcThemeFont( const char *name )
{
	if ( !name || !s_bRcFontsRegistered || !surface() )
		return 0;
	for ( int i = 0; i < (int)( sizeof( s_RcFonts ) / sizeof( s_RcFonts[0] ) ); i++ )
	{
		RcThemeFont_t *f = &s_RcFonts[i];
		if ( stricmp( name, f->name ) )
			continue;
		float scale = RcUiScale();
		if ( f->scale != scale )
		{
			f->scale = scale;
			f->font = 0;
			HFont h = surface()->CreateFont();
			if ( h && surface()->AddGlyphSetToFont( h, f->face, RcUiScaled( f->tall ), f->weight, 0, 0, f->flags, 0, 0 ) )
				f->font = h;
		}
		return f->font;
	}
	return 0;
}

// the module this code is in (GameUI.dll), where the fonts are linked
static HMODULE RcThisModule( void )
{
	MEMORY_BASIC_INFORMATION mbi;
	if ( !VirtualQuery( (void *)RcThisModule, &mbi, sizeof( mbi ) ) )
		return NULL;
	return (HMODULE)mbi.AllocationBase;
}

// a font linked in as RCDATA, registered for this process only (gdi32's AddFontMemResourceEx, Windows 2000 and Wine)
static bool RcRegisterFont( int id )
{
	typedef HANDLE ( WINAPI *AddFontMem_t )( PVOID, DWORD, PVOID, DWORD * );
	HMODULE hThis = RcThisModule();
	HMODULE hGdi = GetModuleHandleA( "gdi32.dll" );
	AddFontMem_t pAdd = hGdi ? (AddFontMem_t)GetProcAddress( hGdi, "AddFontMemResourceEx" ) : NULL;
	HRSRC hRes = hThis ? FindResourceA( hThis, MAKEINTRESOURCE( id ), MAKEINTRESOURCE( 10 ) ) : NULL;	// RT_RCDATA
	if ( !pAdd || !hRes )
		return false;
	HGLOBAL hData = LoadResource( hThis, hRes );
	void *pData = hData ? LockResource( hData ) : NULL;
	DWORD nFonts = 0;
	return pData && pAdd( pData, SizeofResource( hThis, hRes ), NULL, &nFonts ) != NULL && nFonts > 0;
}

//-----------------------------------------------------------------------------
// B413: the borders, flat as cof-enhanced's windows: a hairline round buttons, boxes, lists and windows (lighter while
// pressed, red with the keyboard's focus), nothing round a tab and a red line under the chosen one, nothing on the
// title bar's and scroll bars' buttons; in place of trackerscheme.res's raised and sunken frames. A name not in the
// list keeps the scheme's own border.
//-----------------------------------------------------------------------------
int RcUiScaled( int value );

enum { RCB_NONE, RCB_LINE, RCB_UNDER };

struct RcBorderDef_t
{
	const char	*name;
	int			style;
	int			r, g, b, a;
	int			inset;
};

static const RcBorderDef_t s_RcBorderDefs[] =
{
	{ "BaseBorder", RCB_LINE, 62, 62, 62, 255, 1 },	// text boxes, lists
	{ "InsetBorder", RCB_LINE, 62, 62, 62, 255, 1 },
	{ "ComboBoxBorder", RCB_LINE, 80, 80, 80, 255, 1 },
	{ "BrowserBorder", RCB_LINE, 62, 62, 62, 255, 1 },
	{ "ButtonBorder", RCB_LINE, 90, 90, 90, 255, 1 },
	{ "RaisedBorder", RCB_LINE, 90, 90, 90, 255, 1 },
	{ "ButtonDepressedBorder", RCB_LINE, 160, 160, 160, 255, 1 },
	{ "ButtonKeyFocusBorder", RCB_LINE, 235, 70, 60, 255, 1 },
	{ "FrameBorder", RCB_LINE, 95, 95, 95, 255, 1 },
	{ "MenuBorder", RCB_LINE, 80, 80, 80, 255, 1 },
	{ "ToolTipBorder", RCB_LINE, 95, 95, 95, 255, 1 },
	{ "TabBorder", RCB_NONE, 0, 0, 0, 0, 0 },
	{ "TabActiveBorder", RCB_UNDER, 235, 70, 60, 255, 0 },
	{ "TitleButtonBorder", RCB_NONE, 0, 0, 0, 0, 0 },
	{ "TitleButtonDisabledBorder", RCB_NONE, 0, 0, 0, 0, 0 },
	{ "TitleButtonDepressedBorder", RCB_NONE, 0, 0, 0, 0, 0 },
	{ "ScrollBarButtonBorder", RCB_NONE, 0, 0, 0, 0, 0 },
	{ "ScrollBarButtonDepressedBorder", RCB_NONE, 0, 0, 0, 0, 0 },
};

#define RC_BORDERS	( sizeof( s_RcBorderDefs ) / sizeof( s_RcBorderDefs[0] ) )

class CRcBorder : public IBorder
{
public:
	const RcBorderDef_t	*m_pDef;

	virtual void Paint( VPANEL panel )
	{
		int wide, tall;
		ipanel()->GetSize( panel, wide, tall );
		Paint( 0, 0, wide, tall );
	}
	virtual void Paint( int x0, int y0, int x1, int y1 )
	{
		if ( !m_pDef || m_pDef->style == RCB_NONE )
			return;
		surface()->DrawSetColor( m_pDef->r, m_pDef->g, m_pDef->b, m_pDef->a );
		if ( m_pDef->style == RCB_UNDER )
		{
			int thick = RcUiScaled( 2 );
			surface()->DrawFilledRect( x0, y1 - thick, x1, y1 );
			return;
		}
		surface()->DrawOutlinedRect( x0, y0, x1, y1 );
	}
	virtual void Paint( int x0, int y0, int x1, int y1, int breakSide, int breakStart, int breakStop )
	{
		Paint( x0, y0, x1, y1 );
	}
	virtual void SetInset( int left, int top, int right, int bottom ) {}
	virtual void GetInset( int &left, int &top, int &right, int &bottom )
	{
		left = top = right = bottom = m_pDef ? m_pDef->inset : 0;
	}
	virtual void ApplySchemeSettings( IScheme *pScheme, KeyValues *inResourceData ) {}
	virtual const char *GetName() { return m_pDef ? m_pDef->name : ""; }
	virtual void SetName( const char *name ) {}
};

static CRcBorder s_RcBorders[RC_BORDERS];

static IBorder *RcThemeBorder( const char *name )
{
	for ( int i = 0; name && i < (int)RC_BORDERS; i++ )
	{
		if ( !stricmp( name, s_RcBorderDefs[i].name ) )
		{
			s_RcBorders[i].m_pDef = &s_RcBorderDefs[i];
			return &s_RcBorders[i];
		}
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// The wrappers
//-----------------------------------------------------------------------------
class CRcScheme : public IScheme
{
public:
	IScheme *m_pReal;

	virtual const char *GetResourceString( const char *stringName ) { return m_pReal->GetResourceString( stringName ); }
	virtual IBorder *GetBorder( const char *borderName )
	{
		IBorder *b = RcThemeBorder( borderName );	// B413
		return b ? b : m_pReal->GetBorder( borderName );
	}
	virtual HFont GetFont( const char *fontName, bool proportional )
	{
		HFont f = RcThemeFont( fontName );
		return f ? f : m_pReal->GetFont( fontName, proportional );
	}
	virtual Color GetColor( const char *colorName, Color defaultColor )
	{
		Color c;
		if ( RcThemeLookup( colorName, c ) )
			return c;
		return m_pReal->GetColor( colorName, defaultColor );
	}
};

#define RC_MAX_SCHEMES	16

class CRcSchemeManager : public ISchemeManager
{
public:
	ISchemeManager	*m_pReal;
	CRcScheme		m_Schemes[RC_MAX_SCHEMES];
	int				m_nSchemes;

	virtual HScheme LoadSchemeFromFile( const char *fileName, const char *tag ) { return m_pReal->LoadSchemeFromFile( fileName, tag ); }
	virtual void ReloadSchemes() { m_pReal->ReloadSchemes(); }
	virtual HScheme GetDefaultScheme() { return m_pReal->GetDefaultScheme(); }
	virtual HScheme GetScheme( const char *tag ) { return m_pReal->GetScheme( tag ); }
	virtual IImage *GetImage( const char *imageName, bool hardwareFiltered ) { return m_pReal->GetImage( imageName, hardwareFiltered ); }
	virtual HTexture GetImageID( const char *imageName, bool hardwareFiltered ) { return m_pReal->GetImageID( imageName, hardwareFiltered ); }
	virtual IScheme *GetIScheme( HScheme scheme )
	{
		IScheme *pReal = m_pReal->GetIScheme( scheme );
		if ( !pReal )
			return NULL;
		for ( int i = 0; i < m_nSchemes; i++ )
		{
			if ( m_Schemes[i].m_pReal == pReal )
				return &m_Schemes[i];
		}
		if ( m_nSchemes >= RC_MAX_SCHEMES )
			return pReal;
		m_Schemes[m_nSchemes].m_pReal = pReal;
		return &m_Schemes[m_nSchemes++];
	}
	virtual void Shutdown( bool full ) { m_pReal->Shutdown( full ); }
	// B399: a proportional layout (the retail Options pages) is in 720p pixels and grows with the GameUI's scale
	virtual int GetProportionalScaledValue( int normalizedValue ) { return RcUiScaled( normalizedValue ); }
	virtual int GetProportionalNormalizedValue( int scaledValue ) { return (int)( scaledValue / RcUiScale() + 0.5f ); }
};

static CRcSchemeManager s_RcSchemeManager;

//-----------------------------------------------------------------------------
// CGameUI::Initialize, after VGuiControls_Init: the fonts registered and ReCoF's scheme manager in place
//-----------------------------------------------------------------------------
void RcTheme_Install( void )
{
	if ( !g_pSchemeInterface || g_pSchemeInterface == &s_RcSchemeManager )
		return;
	s_bRcFontsRegistered = RcRegisterFont( RC_FONT_INTER_REGULAR ) && RcRegisterFont( RC_FONT_INTER_SEMIBOLD );
	s_RcSchemeManager.m_pReal = g_pSchemeInterface;
	g_pSchemeInterface = &s_RcSchemeManager;
}
