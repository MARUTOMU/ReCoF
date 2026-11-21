// ReCoF (B405): the modern look of ReCoF's own VGUI1 screens, see rc_ui.h.
#include "hud.h"
#include "cl_util.h"
#include "vgui_TeamFortressViewport.h"
#include "rc_ui.h"
#include <string.h>
#include <ctype.h>

// GameUI.dll's theme (RcTheme.cpp s_RcColors), with each colour's opacity
const RcUiColor RCC_PANEL		= { 30, 30, 30, 238 };
const RcUiColor RCC_BAND		= { 14, 14, 14, 248 };
const RcUiColor RCC_BORDER		= { 95, 95, 95, 255 };
const RcUiColor RCC_TITLE		= { 235, 235, 235, 255 };
const RcUiColor RCC_TEXT		= { 200, 200, 200, 255 };
const RcUiColor RCC_DIM			= { 140, 140, 140, 255 };
const RcUiColor RCC_HI			= { 245, 245, 245, 255 };
const RcUiColor RCC_ACCENT		= { 235, 70, 60, 255 };
const RcUiColor RCC_FILL		= { 150, 24, 20, 255 };
const RcUiColor RCC_SELECT		= { 150, 24, 20, 80 };
const RcUiColor RCC_HOVER		= { 255, 255, 255, 22 };
const RcUiColor RCC_PRESS		= { 255, 255, 255, 40 };
const RcUiColor RCC_DISABLED	= { 100, 100, 100, 255 };
const RcUiColor RCC_SEPARATOR	= { 255, 255, 255, 40 };
const RcUiColor RCC_INPUT		= { 10, 10, 10, 200 };
const RcUiColor RCC_SCRIM		= { 0, 0, 0, 150 };

float RcUiScale( void )
{
	// B423: a narrow screen (4:3, 5:4) limits it too: 1092 x 768 is the 16:9-like width its panels were made for
	float h = ScreenHeight / 768.0f, w = ScreenWidth / 1092.0f;
	float s = ( w < h ? w : h ) * 0.88f;
	float user = gEngfuncs.pfnGetCvarFloat( "rc_menu_scale" ) / 100.0f;
	if ( user < 1.0f )
		user = 1.0f;
	if ( user > 2.0f )
		user = 2.0f;
	s *= user;
	if ( s < 0.6f )
		s = 0.6f;
	return s;
}

int RcUiS( float value )
{
	return (int)( value * RcUiScale() + ( value >= 0 ? 0.5f : -0.5f ) );
}

// B410: the main menu and its New Game screen keep Cry of Fear's pictures unless rc_menu_text is 1
bool RcUiModern( void )
{
	return gEngfuncs.pfnGetCvarFloat( "rc_menu_text" ) != 0;
}

struct RcUiFontDef_t
{
	const char	*face;
	float		size;	// in 768-line units
	int			weight;
};

static const RcUiFontDef_t s_RcUiFontDefs[RCF_COUNT] =
{
	{ "Inter",			24, 400 },	// RCF_ITEM
	{ "Inter SemiBold",	46, 600 },	// RCF_WORD
	{ "Inter",			19, 400 },	// RCF_WORDSUB
	{ "Inter SemiBold",	17, 600 },	// RCF_TITLE
	{ "Inter",			19, 400 },	// RCF_BODY
	{ "Inter",			16, 400 },	// RCF_LABEL
	{ "Inter SemiBold",	19, 600 },	// RCF_BUTTON
};

// a Windows font whose glyphs come out grey: Windows draws them with ClearType's coloured edges where the system has
// it on (orange and blue fringes on grey text over the menu's dark scene); each glyph's colours become their average
class CRcUiFont : public vgui::Font
{
public:
	CRcUiFont( const char *name, int tall, int weight ) : vgui::Font( name, tall, 0, 0, weight, false, false, false, false ) {}

	virtual void getCharRGBA( int ch, int rgbaX, int rgbaY, int rgbaWide, int rgbaTall, uchar *rgba )
	{
		vgui::Font::getCharRGBA( ch, rgbaX, rgbaY, rgbaWide, rgbaTall, rgba );
		int a, b, c;
		getCharABCwide( ch, a, b, c );
		int wide = a + b + c > b ? a + b + c : b;
		if ( wide > rgbaWide - rgbaX )
			wide = rgbaWide - rgbaX;
		int tall = getTall();
		if ( tall > rgbaTall - rgbaY )
			tall = rgbaTall - rgbaY;
		for ( int y = 0; y < tall; y++ )
		{
			uchar *p = rgba + ( ( rgbaY + y ) * rgbaWide + rgbaX ) * 4;
			for ( int x = 0; x < wide; x++, p += 4 )
			{
				int v = ( p[0] + p[1] + p[2] ) / 3;
				p[0] = p[1] = p[2] = (uchar)v;
			}
		}
	}
};

static vgui::Font *s_pRcUiFonts[RCF_COUNT];
static float s_flRcUiFontScale = -1;

vgui::Font *RcUiFont( int role )
{
	if ( role < 0 || role >= RCF_COUNT )
		role = RCF_BODY;
	float s = RcUiScale();
	if ( s != s_flRcUiFontScale )
	{
		// a new scale (mode change, Menu size): new fonts; the old ones stay, a panel may still hold one
		for ( int i = 0; i < RCF_COUNT; i++ )
			s_pRcUiFonts[i] = NULL;
		s_flRcUiFontScale = s;
	}
	if ( !s_pRcUiFonts[role] )
	{
		const RcUiFontDef_t &d = s_RcUiFontDefs[role];
		int tall = (int)( d.size * s + 0.5f );
		if ( tall < 8 )
			tall = 8;
		s_pRcUiFonts[role] = new CRcUiFont( d.face, tall, d.weight );
	}
	return s_pRcUiFonts[role];
}

//-----------------------------------------------------------------------------
// CRcUiButton
//-----------------------------------------------------------------------------
CRcUiButton::CRcUiButton( const char *text, int x, int y, int wide, int tall, int style, int font )
	: CommandButton( "", x, y, wide, tall, true, true )
{
	m_iRcStyle = style;
	m_iRcFont = font;
	m_bRcSelected = false;
	m_bRcDisabled = false;
	m_bRcCentred = ( style == STYLE_BOX || style == STYLE_PRIMARY );
	m_szRcText[0] = 0;
	setRcText( text );
	setBgColor( 0, 0, 0, 255 );
}

void CRcUiButton::setRcText( const char *text )
{
	strncpy( m_szRcText, text ? text : "", sizeof( m_szRcText ) - 1 );
	m_szRcText[sizeof( m_szRcText ) - 1] = 0;
	repaint();
}

int CRcUiButton::textWide( void )
{
	int wide = 0, tall = 0;
	RcUiFont( m_iRcFont )->getTextSize( m_szRcText, wide, tall );
	return wide;
}

void CRcUiButton::paintBackground()
{
	int wide, tall;
	getSize( wide, tall );
	bool bArmed = isArmed() && !m_bRcDisabled;
	const RcUiColor *pFill = NULL;
	switch ( m_iRcStyle )
	{
	case STYLE_ITEM:
		if ( bArmed )
		{
			// a short red bar before the entry under the cursor
			vgui::Font *pFont = RcUiFont( m_iRcFont );
			int tw, tt;
			pFont->getTextSize( m_szRcText, tw, tt );
			int x = m_bRcCentred ? ( wide - tw ) / 2 : 0;
			int bar = RcUiS( 3 ) > 2 ? RcUiS( 3 ) : 2;
			int gap = RcUiS( 12 );
			if ( x - gap - bar >= 0 )
			{
				drawSetColor( RCC_ACCENT.r, RCC_ACCENT.g, RCC_ACCENT.b, 255 - RCC_ACCENT.a );
				drawFilledRect( x - gap - bar, tall / 2 - tt / 3, x - gap, tall / 2 + tt / 3 );
			}
		}
		return;
	case STYLE_ROW:
		pFill = m_bRcSelected ? &RCC_SELECT : bArmed ? &RCC_HOVER : NULL;
		break;
	case STYLE_BOX:
		pFill = bArmed ? &RCC_HOVER : &RCC_INPUT;
		break;
	case STYLE_PRIMARY:
		pFill = &RCC_FILL;
		break;
	}
	if ( pFill )
	{
		int r = pFill->r, g = pFill->g, b = pFill->b;
		if ( m_iRcStyle == STYLE_PRIMARY && bArmed )
		{
			r = 185;
			g = 32;
			b = 26;
		}
		if ( m_iRcStyle == STYLE_PRIMARY && m_bRcDisabled )
		{
			r = 60;
			g = 30;
			b = 28;
		}
		drawSetColor( r, g, b, 255 - pFill->a );
		drawFilledRect( 0, 0, wide, tall );
	}
	if ( m_iRcStyle == STYLE_BOX )
	{
		const RcUiColor &c = bArmed ? RCC_TEXT : RCC_BORDER;
		drawSetColor( c.r, c.g, c.b, 255 - c.a );
		drawOutlinedRect( 0, 0, wide, tall );
	}
}

void CRcUiButton::paint()
{
	int wide, tall;
	getSize( wide, tall );
	bool bArmed = isArmed() && !m_bRcDisabled;
	const RcUiColor *pColor;
	if ( m_iRcStyle == STYLE_ROW && m_bRcSelected )
		pColor = &RCC_ACCENT;	// shown as chosen even where this player cannot change it (B406, a lobby's guest)
	else if ( m_bRcDisabled )
		pColor = &RCC_DISABLED;
	else if ( m_iRcStyle == STYLE_PRIMARY || bArmed )
		pColor = &RCC_HI;
	else
		pColor = &RCC_TEXT;
	vgui::Font *pFont = RcUiFont( m_iRcFont );
	int tw, tt;
	pFont->getTextSize( m_szRcText, tw, tt );
	int x = m_bRcCentred ? ( wide - tw ) / 2 : ( m_iRcStyle == STYLE_ITEM ? 0 : RcUiS( 12 ) );
	drawSetTextFont( pFont );
	drawSetTextColor( pColor->r, pColor->g, pColor->b, 255 - pColor->a );
	drawSetTextPos( x, ( tall - tt ) / 2 );
	drawPrintText( m_szRcText, strlen( m_szRcText ) );
}

//-----------------------------------------------------------------------------
// CRcUiText
//-----------------------------------------------------------------------------
CRcUiText::CRcUiText( const char *text, int x, int y, int wide, int tall, int font, const RcUiColor &color )
	: vgui::Panel( x, y, wide, tall )
{
	m_iFont = font;
	m_iAlign = 0;
	m_Color = color;
	m_szText[0] = 0;
	setRcText( text );
	setBgColor( 0, 0, 0, 255 );
}

void CRcUiText::setRcText( const char *text )
{
	strncpy( m_szText, text ? text : "", sizeof( m_szText ) - 1 );
	m_szText[sizeof( m_szText ) - 1] = 0;
	repaint();
}

void CRcUiText::paint()
{
	if ( !m_szText[0] )
		return;
	int wide, tall, tw, tt;
	getSize( wide, tall );
	vgui::Font *pFont = RcUiFont( m_iFont );
	pFont->getTextSize( m_szText, tw, tt );
	int x = m_iAlign == 1 ? ( wide - tw ) / 2 : m_iAlign == 2 ? wide - tw : 0;
	drawSetTextFont( pFont );
	drawSetTextColor( m_Color.r, m_Color.g, m_Color.b, 255 - m_Color.a );
	drawSetTextPos( x, ( tall - tt ) / 2 );
	drawPrintText( m_szText, strlen( m_szText ) );
}

//-----------------------------------------------------------------------------
// CRcUiFrame
//-----------------------------------------------------------------------------
CRcUiFrame::CRcUiFrame( const char *title, int x, int y, int wide, int tall ) : vgui::Panel( x, y, wide, tall )
{
	m_szTitle[0] = 0;
	setRcTitle( title );
	setBgColor( 0, 0, 0, 255 );
}

void CRcUiFrame::setRcTitle( const char *title )
{
	int i = 0;
	for ( ; title && title[i] && i < (int)sizeof( m_szTitle ) - 1; i++ )
		m_szTitle[i] = (char)toupper( (unsigned char)title[i] );
	m_szTitle[i] = 0;
	repaint();
}

void CRcUiFrame::paintBackground()
{
	int wide, tall;
	getSize( wide, tall );
	int band = bandTall();
	drawSetColor( RCC_PANEL.r, RCC_PANEL.g, RCC_PANEL.b, 255 - RCC_PANEL.a );
	drawFilledRect( 0, band, wide, tall );
	drawSetColor( RCC_BAND.r, RCC_BAND.g, RCC_BAND.b, 255 - RCC_BAND.a );
	drawFilledRect( 0, 0, wide, band );
	drawSetColor( RCC_SEPARATOR.r, RCC_SEPARATOR.g, RCC_SEPARATOR.b, 255 - RCC_SEPARATOR.a );
	drawFilledRect( 0, band - 1, wide, band );
	drawSetColor( RCC_BORDER.r, RCC_BORDER.g, RCC_BORDER.b, 255 - RCC_BORDER.a );
	drawOutlinedRect( 0, 0, wide, tall );
}

void CRcUiFrame::paint()
{
	vgui::Font *pFont = RcUiFont( RCF_TITLE );
	int tw, tt;
	pFont->getTextSize( m_szTitle, tw, tt );
	drawSetTextFont( pFont );
	drawSetTextColor( RCC_TITLE.r, RCC_TITLE.g, RCC_TITLE.b, 255 - RCC_TITLE.a );
	drawSetTextPos( RcUiS( 24 ), ( bandTall() - tt ) / 2 );
	drawPrintText( m_szTitle, strlen( m_szTitle ) );
}

//-----------------------------------------------------------------------------
// CRcUiWordmark
//-----------------------------------------------------------------------------
CRcUiWordmark::CRcUiWordmark( int x, int y, int wide, int tall ) : vgui::Panel( x, y, wide, tall )
{
	setBgColor( 0, 0, 0, 255 );
}

int CRcUiWordmark::blockTall( void )
{
	return RcUiFont( RCF_WORD )->getTall() + RcUiS( 4 ) + RcUiFont( RCF_WORDSUB )->getTall();
}

// a line of capitals with extra space between the letters, centred in the panel
static int RcUiSpacedWide( vgui::Font *pFont, const char *text, int track )
{
	int total = 0;
	for ( int i = 0; text[i]; i++ )
	{
		char ch[2] = { text[i], 0 };
		int w, t;
		pFont->getTextSize( ch, w, t );
		total += w + ( text[i + 1] ? track : 0 );
	}
	return total;
}

void CRcUiWordmark::paint()
{
	static const char s_szWord[] = "CRY OF FEAR";
	static const char s_szSub[] = "RECOF";
	int wide, tall;
	getSize( wide, tall );
	vgui::Font *pWord = RcUiFont( RCF_WORD ), *pSub = RcUiFont( RCF_WORDSUB );
	int y = 0;
	const char *lines[2] = { s_szWord, s_szSub };
	vgui::Font *fonts[2] = { pWord, pSub };
	int tracks[2] = { RcUiS( 4 ), RcUiS( 5 ) };
	const RcUiColor *colors[2] = { &RCC_HI, &RCC_DIM };
	for ( int l = 0; l < 2; l++ )
	{
		int x = ( wide - RcUiSpacedWide( fonts[l], lines[l], tracks[l] ) ) / 2;
		drawSetTextFont( fonts[l] );
		drawSetTextColor( colors[l]->r, colors[l]->g, colors[l]->b, 255 - colors[l]->a );
		for ( int i = 0; lines[l][i]; i++ )
		{
			char ch[2] = { lines[l][i], 0 };
			int w, t;
			fonts[l]->getTextSize( ch, w, t );
			drawSetTextPos( x, y );
			drawPrintText( ch, 1 );
			x += w + tracks[l];
		}
		y += fonts[l]->getTall() + RcUiS( 4 );
	}
}
