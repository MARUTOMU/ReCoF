// ======================================
// Cry of Fear inventory panel
// (class names from the RTTI of client.dll, file name guessed)
// ======================================

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "vgui_TeamFortressViewport.h"
#include "RECBInventory_guess.h"
#include "Documents_guess.h"
#include "HUDControl_guess.h"
#include "jp_subtitle_render.h"
#include "VGUI_SurfaceBase.h"
#include "VGUI_Bitmap.h"	// JP-MOD (build38): objective text Bitmap
#include <stdarg.h>	// JP-MOD: objective label setText capture
#include <stdio.h>	// JP-MOD: objective label debug log	// JP-MOD: full SurfaceBase definition (createNewTextureID)	// JP-MOD (objective label)
extern cvar_t *jp_poc;	// JP-MOD: defined in jp_subtitle_render.cpp
#include "..\game_shared\vgui_loadtga.h"

extern int iWheelchair_guess;

bool g_bInventoryFadeOut_guess;

// plays a sound by name; only __CmdFunc_Unlockables_guess (hud.cpp) calls it
void InventorySound_guess( char *pszSound, float flVolume )
{
	gEngfuncs.pfnPlaySoundByName( pszSound, flVolume );
}

// JP-MOD: objective label subclass. The stock Label renders the UTF-8
// Japanese objective line byte-by-byte (mojibake). When JP mode is on,
// paint() draws the line with the shared JP renderer instead, reading
// the text from the same source the caller uses (the HUD hint line 1,
// where '@' separates lines). Otherwise the retail paint runs.
// JP-MOD: debug log for the objective label (verification builds only).
static void JpLogObj( const char *fmt, ... )
{
	FILE *f = fopen( "jp_debug.log", "a" );
	if ( !f )
		return;
	va_list ap;
	va_start( ap, fmt );
	vfprintf( f, fmt, ap );
	va_end( ap );
	fclose( f );
}

// BUILD40: the stock Bitmap filled via setSize/_rgba never showed
// (builds 38/39), while a BitmapTGA from vgui_LoadTGA did (build37
// probe). So build the text image through the exact TGA path, from
// memory (vgui_LoadTGAFromMemory). This build draws the same line
// twice to settle the alpha convention: strip A stores alpha inverted
// (255-coverage, the Valve file convention the loader then inverts),
// strip B stores alpha straight.
static unsigned char *JpMakeTGA( const unsigned char *rgbaTopDown, int w, int h, bool invertAlpha, int &outLen )
{
	outLen = 18 + w * h * 4;
	unsigned char *tga = (unsigned char *)malloc( outLen );
	if ( !tga )
		return NULL;
	memset( tga, 0, 18 );
	tga[2] = 2;
	tga[12] = (unsigned char)( w & 0xff );
	tga[13] = (unsigned char)( ( w >> 8 ) & 0xff );
	tga[14] = (unsigned char)( h & 0xff );
	tga[15] = (unsigned char)( ( h >> 8 ) & 0xff );
	tga[16] = 32;
	tga[17] = 8;
	unsigned char *px = tga + 18;
	for ( int y = 0; y < h; y++ )
	{
		const unsigned char *srow = rgbaTopDown + (size_t)( h - 1 - y ) * w * 4;
		for ( int x = 0; x < w; x++ )
		{
			unsigned char r = srow[x * 4 + 0];
			unsigned char g = srow[x * 4 + 1];
			unsigned char b = srow[x * 4 + 2];
			unsigned char a = srow[x * 4 + 3];
			if ( invertAlpha )
				a = (unsigned char)( 255 - a );
			unsigned char *d = px + ( (size_t)y * w + x ) * 4;
			d[0] = b;
			d[1] = g;
			d[2] = r;
			d[3] = a;
		}
	}
	return tga;
}

class JpObjectiveLabel : public Label
{
	char m_szJpText[256];
	char m_szRendered[256];
	vgui::BitmapTGA *m_pImgA;
	vgui::BitmapTGA *m_pImgB;
public:
	JpObjectiveLabel( const char *text, int x, int y, int wide, int tall )
		: Label( text, x, y, wide, tall )
	{
		m_szJpText[0] = '\0';
		m_szRendered[0] = '\0';
		m_pImgA = NULL;
		m_pImgB = NULL;
	}

	virtual ~JpObjectiveLabel()
	{
		delete m_pImgA;
		delete m_pImgB;
	}

	// JP-MOD: capture the text the game sets on this label (the objective
	// line). paint() must use this copy: the HUD hint buffer it was copied
	// from is volatile and may hold another line by paint time.
	virtual void setText( int textBufferLen, const char *text )
	{
		if ( text )
		{
			strncpy( m_szJpText, text, 255 );
			m_szJpText[255] = '\0';
		}
		Label::setText( textBufferLen, text );
	}

	virtual void setText( const char *format, ... )
	{
		char buf[256];
		va_list ap;
		va_start( ap, format );
		vsnprintf( buf, sizeof( buf ), format, ap );
		va_end( ap );
		buf[sizeof( buf ) - 1] = '\0';
		strncpy( m_szJpText, buf, 255 );
		m_szJpText[255] = '\0';
		Label::setText( "%s", buf );
	}

	bool BuildImages( const char *pszLine )
	{
		int iWide, iTall;
		getSize( iWide, iTall );
		if ( iWide <= 0 || iTall <= 0 )
			return false;

		char szLine[256];
		strncpy( szLine, pszLine, 255 );
		szLine[255] = '\0';
		for ( char *p = szLine; *p; p++ )
		{
			if ( *p == '@' || *p == '\n' )
			{
				*p = '\0';
				break;
			}
		}

		const int iPx = 16;
		if ( !JpSharedInitFont( iPx ) )
			return false;

		const int iStripH = 30;
		unsigned char *buf = (unsigned char *)calloc( (size_t)iWide * iStripH, 4 );
		if ( !buf )
			return false;

		float fW = JpSharedMeasure( szLine );
		float penX = ( iWide - fW ) / 2.0f;
		int iBase = iStripH / 2 + JpSharedAscentPx() - 8;
		const unsigned char *pS = (const unsigned char *)szLine;
		int iLen = (int)strlen( szLine );
		int pos = 0;
		while ( pos < iLen )
		{
			int cp;
			if ( !JpSharedDecodeUtf8( pS, iLen, pos, cp ) )
				break;
			const JpGlyph *g = JpSharedGlyphMetrics( cp );
			if ( !g )
				break;
			unsigned char *rgba = NULL;
			if ( JpSharedGlyphBitmap( cp, &rgba ) && rgba && g->w > 0 && g->h > 0 )
			{
				for ( int pass = 0; pass < 2; pass++ )
				{
					int ox = ( pass == 0 ) ? 1 : 0;
					int oy = ( pass == 0 ) ? 1 : 0;
					int sc = ( pass == 0 ) ? 0 : 255;
					int gx0 = (int)( penX + 0.5f ) + g->xoff + ox;
					int gy0 = iBase + g->yoff + oy;
					for ( int gy = 0; gy < g->h; gy++ )
					{
						int dy = gy0 + gy;
						if ( dy < 0 || dy >= iStripH )
							continue;
						for ( int gx = 0; gx < g->w; gx++ )
						{
							int dx = gx0 + gx;
							if ( dx < 0 || dx >= iWide )
								continue;
							int sa = rgba[( gy * g->w + gx ) * 4 + 3];
							if ( !sa )
								continue;
							unsigned char *d = buf + ( (size_t)dy * iWide + dx ) * 4;
							float fa = sa / 255.0f;
							float da = d[3] / 255.0f;
							float oa = fa + da * ( 1.0f - fa );
							if ( oa <= 0.0f )
								continue;
							d[0] = (unsigned char)( ( sc * fa + d[0] * da * ( 1.0f - fa ) ) / oa + 0.5f );
							d[1] = (unsigned char)( ( sc * fa + d[1] * da * ( 1.0f - fa ) ) / oa + 0.5f );
							d[2] = (unsigned char)( ( sc * fa + d[2] * da * ( 1.0f - fa ) ) / oa + 0.5f );
							d[3] = (unsigned char)( oa * 255.0f + 0.5f );
						}
					}
				}
				free( rgba );
			}
			penX += g->adv;
		}

		int lenA = 0, lenB = 0;
		unsigned char *tgaA = JpMakeTGA( buf, iWide, iStripH, true, lenA );
		unsigned char *tgaB = JpMakeTGA( buf, iWide, iStripH, false, lenB );
		free( buf );
		vgui::BitmapTGA *pA = tgaA ? vgui_LoadTGAFromMemory( tgaA, lenA ) : NULL;
		vgui::BitmapTGA *pB = tgaB ? vgui_LoadTGAFromMemory( tgaB, lenB ) : NULL;
		free( tgaA );
		free( tgaB );
		if ( !pA && !pB )
			return false;
		if ( pA )
		{
			pA->setPos( 0, 4 );
			pA->setColor( Color( 255, 255, 255, 255 ) );
		}
		if ( pB )
		{
			pB->setPos( 0, 52 );
			pB->setColor( Color( 255, 255, 255, 255 ) );
		}
		delete m_pImgA;
		delete m_pImgB;
		m_pImgA = pA;
		m_pImgB = pB;
		strncpy( m_szRendered, pszLine, 255 );
		m_szRendered[255] = '\0';
		JpLogObj( "OBJ tga: A=%p B=%p [%s]\n", (void *)pA, (void *)pB, pszLine );
		return true;
	}

	virtual void paint()
	{
		// BUILD42: variable isolation. Three images, all doPaint at
		// (0,0), drawn in order: memory 640x480 opaque red, file
		// 303x99 check picture (known-good in item slots), memory
		// 301x97 opaque green. The final look of the box tells which
		// combinations render at all.
		static vgui::BitmapTGA *s_pMemBig = NULL;
		static vgui::BitmapTGA *s_pFileSmall = NULL;
		static vgui::BitmapTGA *s_pMemBox = NULL;
		static bool s_bTried = false;
		if ( !s_bTried )
		{
			s_bTried = true;
			{
				unsigned char *buf = (unsigned char *)malloc( (size_t)640 * 480 * 4 );
				for ( int i = 0; i < 640 * 480; i++ )
				{
					buf[i * 4 + 0] = 255;
					buf[i * 4 + 1] = 0;
					buf[i * 4 + 2] = 0;
					buf[i * 4 + 3] = 255;
				}
				int len = 0;
				unsigned char *tga = JpMakeTGA( buf, 640, 480, true, len );
				free( buf );
				s_pMemBig = tga ? vgui_LoadTGAFromMemory( tga, len ) : NULL;
				free( tga );
			}
			s_pFileSmall = vgui_LoadTGA( "gfx/vgui/ammo/glock_check.tga" );
			{
				unsigned char *buf = (unsigned char *)malloc( (size_t)301 * 97 * 4 );
				for ( int i = 0; i < 301 * 97; i++ )
				{
					buf[i * 4 + 0] = 0;
					buf[i * 4 + 1] = 255;
					buf[i * 4 + 2] = 0;
					buf[i * 4 + 3] = 255;
				}
				int len = 0;
				unsigned char *tga = JpMakeTGA( buf, 301, 97, true, len );
				free( buf );
				s_pMemBox = tga ? vgui_LoadTGAFromMemory( tga, len ) : NULL;
				free( tga );
			}
			int w1 = -1, h1 = -1, w2 = -1, h2 = -1, w3 = -1, h3 = -1;
			if ( s_pMemBig )
				s_pMemBig->getSize( w1, h1 );
			if ( s_pFileSmall )
				s_pFileSmall->getSize( w2, h2 );
			if ( s_pMemBox )
				s_pMemBox->getSize( w3, h3 );
			JpLogObj( "OBJ 42: membig=%p %dx%d file=%p %dx%d membox=%p %dx%d\n",
				(void *)s_pMemBig, w1, h1, (void *)s_pFileSmall, w2, h2, (void *)s_pMemBox, w3, h3 );
		}
		if ( s_pMemBig )
			s_pMemBig->doPaint( this );
		if ( s_pFileSmall )
			s_pFileSmall->doPaint( this );
		if ( s_pMemBox )
			s_pMemBox->doPaint( this );
	}

};

CRECBInventory::CRECBInventory( int iTrans, int iRemoveMe, int x, int y, int wide, int tall ) : CMenuPanel( iTrans, iRemoveMe, x, y, wide, tall )
{
	int iWide, iTall;
	int iX, iY;
	int i;

	m_pBackgroundPanel_guess = new Panel( 0, 0, ScreenWidth, ScreenHeight );
	m_pBackgroundPanel_guess->setParent( this );
	m_pBackgroundPanel_guess->setBgColor( 0, 0, 0, 0 );

	m_iThree_guess = 0;

	m_pBackgroundImage_guess = vgui_LoadTGA( "gfx/vgui/backgroundtoall.tga" );
	if ( m_pBackgroundImage_guess )
		m_pBackgroundImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/backgroundtoall.tga!\n" );

	m_pBackgroundImage_guess->getSize( iWide, iTall );
	iX = ScreenWidth / 2 - iWide / 2;
	iY = ScreenHeight / 2 - iTall / 2;

	m_pBackground_guess = new ImagePanel( m_pBackgroundImage_guess );
	m_pBackground_guess->setParent( this );
	m_pBackground_guess->setPos( iX, iY );
	m_pBackground_guess->setVisible( true );

	iX = ScreenWidth / 2 - 374;
	iY = ScreenHeight / 2 - 240;

	m_pInventoryImage_guess = vgui_LoadTGA( "gfx/vgui/640_inventory.tga" );
	if ( m_pInventoryImage_guess )
		m_pInventoryImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_inventory.tga!\n" );

	m_pInventory_guess = new ImagePanel( m_pInventoryImage_guess );
	m_pInventory_guess->setParent( this );
	m_pInventory_guess->setPos( iX, iY );
	m_pInventory_guess->setVisible( true );

	m_pItemImages_guess[0] = new CImageLabel( NULL, 31, 68 );
	m_pItemImages_guess[0]->setVisible( true );
	m_pItemImages_guess[0]->setParent( m_pInventory_guess );
	m_pItemImages_guess[0]->setImage( NULL );

	m_pItemImages_guess[1] = new CImageLabel( NULL, 133, 68 );
	m_pItemImages_guess[1]->setVisible( true );
	m_pItemImages_guess[1]->setParent( m_pInventory_guess );
	m_pItemImages_guess[1]->setImage( NULL );

	m_pItemImages_guess[2] = new CImageLabel( NULL, 235, 68 );
	m_pItemImages_guess[2]->setVisible( true );
	m_pItemImages_guess[2]->setParent( m_pInventory_guess );
	m_pItemImages_guess[2]->setImage( NULL );

	m_pItemImages_guess[3] = new CImageLabel( NULL, 31, 227 );
	m_pItemImages_guess[3]->setVisible( true );
	m_pItemImages_guess[3]->setParent( m_pInventory_guess );
	m_pItemImages_guess[3]->setImage( NULL );

	m_pItemImages_guess[4] = new CImageLabel( NULL, 133, 227 );
	m_pItemImages_guess[4]->setVisible( true );
	m_pItemImages_guess[4]->setParent( m_pInventory_guess );
	m_pItemImages_guess[4]->setImage( NULL );

	m_pItemImages_guess[5] = new CImageLabel( NULL, 235, 227 );
	m_pItemImages_guess[5]->setVisible( true );
	m_pItemImages_guess[5]->setParent( m_pInventory_guess );
	m_pItemImages_guess[5]->setImage( NULL );

	m_pItemImages_guess[6] = new CImageLabel( NULL, 398, 68 );
	m_pItemImages_guess[6]->setVisible( true );
	m_pItemImages_guess[6]->setParent( m_pInventory_guess );
	m_pItemImages_guess[6]->setImage( NULL );

	m_pItemImages_guess[7] = new CImageLabel( NULL, 500, 68 );
	m_pItemImages_guess[7]->setVisible( true );
	m_pItemImages_guess[7]->setParent( m_pInventory_guess );
	m_pItemImages_guess[7]->setImage( NULL );

	m_pItemImages_guess[8] = new CImageLabel( NULL, 31, 348 );
	m_pItemImages_guess[8]->setVisible( true );
	m_pItemImages_guess[8]->setParent( m_pInventory_guess );
	m_pItemImages_guess[8]->setImage( NULL );

	m_pItemImages_guess[9] = new CImageLabel( NULL, 602, 68 );
	m_pItemImages_guess[9]->setVisible( true );
	m_pItemImages_guess[9]->setParent( m_pInventory_guess );
	m_pItemImages_guess[9]->setImage( NULL );

	m_pItemButtons_guess[0] = new ColorButton( "", 36, 73, 99, 99, false, false );
	m_pItemButtons_guess[0]->setParent( m_pInventory_guess );
	m_pItemButtons_guess[0]->addActionSignal( new CRECBInventoryHandler_Command( this, 1 ) );

	m_pItemButtons_guess[1] = new ColorButton( "", 138, 73, 99, 99, false, false );
	m_pItemButtons_guess[1]->setParent( m_pInventory_guess );
	m_pItemButtons_guess[1]->addActionSignal( new CRECBInventoryHandler_Command( this, 2 ) );

	m_pItemButtons_guess[2] = new ColorButton( "", 240, 73, 99, 99, false, false );
	m_pItemButtons_guess[2]->setParent( m_pInventory_guess );
	m_pItemButtons_guess[2]->addActionSignal( new CRECBInventoryHandler_Command( this, 3 ) );

	m_pItemButtons_guess[3] = new ColorButton( "", 36, 232, 99, 99, false, false );
	m_pItemButtons_guess[3]->setParent( m_pInventory_guess );
	m_pItemButtons_guess[3]->addActionSignal( new CRECBInventoryHandler_Command( this, 4 ) );

	m_pItemButtons_guess[4] = new ColorButton( "", 138, 232, 99, 99, false, false );
	m_pItemButtons_guess[4]->setParent( m_pInventory_guess );
	m_pItemButtons_guess[4]->addActionSignal( new CRECBInventoryHandler_Command( this, 5 ) );

	m_pItemButtons_guess[5] = new ColorButton( "", 240, 232, 99, 99, false, false );
	m_pItemButtons_guess[5]->setParent( m_pInventory_guess );
	m_pItemButtons_guess[5]->addActionSignal( new CRECBInventoryHandler_Command( this, 6 ) );

	m_pOptionButtons_guess[0] = new ColorButton( "", 373, 226, 363, 47, false, false );
	m_pOptionButtons_guess[0]->setParent( m_pInventory_guess );
	m_pOptionButtons_guess[0]->addActionSignal( new CRECBInventoryHandler_Command( this, 7 ) );
	m_pOptionButtons_guess[0]->setVisible( false );

	m_pOptionButtons_guess[1] = new ColorButton( "", 373, 276, 363, 47, false, false );
	m_pOptionButtons_guess[1]->setParent( m_pInventory_guess );
	m_pOptionButtons_guess[1]->addActionSignal( new CRECBInventoryHandler_Command( this, 8 ) );
	m_pOptionButtons_guess[1]->setVisible( false );

	m_pOptionButtons_guess[2] = new ColorButton( "", 373, 325, 363, 47, false, false );
	m_pOptionButtons_guess[2]->setParent( m_pInventory_guess );
	m_pOptionButtons_guess[2]->addActionSignal( new CRECBInventoryHandler_Command( this, 10 ) );
	m_pOptionButtons_guess[2]->setVisible( false );

	m_pOptionButtons_guess[3] = new ColorButton( "", 373, 376, 363, 47, false, false );
	m_pOptionButtons_guess[3]->setParent( m_pInventory_guess );
	m_pOptionButtons_guess[3]->addActionSignal( new CRECBInventoryHandler_Command( this, 15 ) );
	m_pOptionButtons_guess[3]->setVisible( false );

	m_pOptionButtons_guess[4] = new ColorButton( "", 373, 226, 363, 97, false, false );
	m_pOptionButtons_guess[4]->setParent( m_pInventory_guess );
	m_pOptionButtons_guess[4]->addActionSignal( new CRECBInventoryHandler_Command( this, 16 ) );

	for ( i = 0; i < 6; i++ )
	{
		m_pItemButtons_guess[i]->setUnArmedBorderColor( 0, 0, 0, 255 );
		m_pItemButtons_guess[i]->setArmedBorderColor( 255, 255, 255, 128 );
		m_pItemButtons_guess[i]->setUnArmedColor( 0, 0, 0, 255 );
		m_pItemButtons_guess[i]->setArmedColor( 0, 0, 0, 255 );
	}

	for ( i = 0; i < 5; i++ )
	{
		m_pOptionButtons_guess[i]->setUnArmedBorderColor( 0, 0, 0, 255 );
		m_pOptionButtons_guess[i]->setArmedBorderColor( 255, 255, 255, 128 );
		m_pOptionButtons_guess[i]->setUnArmedColor( 0, 0, 0, 255 );
		m_pOptionButtons_guess[i]->setArmedColor( 0, 0, 0, 255 );
	}

	m_pQuickSlot1Button_guess = new ColorButton( "", 403, 73, 99, 99, false, false );
	m_pQuickSlot1Button_guess->setParent( m_pInventory_guess );
	m_pQuickSlot1Button_guess->addActionSignal( new CRECBInventoryHandler_Command( this, 12 ) );
	m_pQuickSlot1Button_guess->setUnArmedBorderColor( 0, 0, 0, 255 );
	m_pQuickSlot1Button_guess->setArmedBorderColor( 255, 255, 255, 255 );
	m_pQuickSlot1Button_guess->setUnArmedColor( 0, 0, 0, 255 );
	m_pQuickSlot1Button_guess->setArmedColor( 0, 0, 0, 255 );

	m_pQuickSlot2Button_guess = new ColorButton( "", 505, 73, 99, 99, false, false );
	m_pQuickSlot2Button_guess->setParent( m_pInventory_guess );
	m_pQuickSlot2Button_guess->addActionSignal( new CRECBInventoryHandler_Command( this, 13 ) );
	m_pQuickSlot2Button_guess->setUnArmedBorderColor( 0, 0, 0, 255 );
	m_pQuickSlot2Button_guess->setArmedBorderColor( 255, 255, 255, 255 );
	m_pQuickSlot2Button_guess->setUnArmedColor( 0, 0, 0, 255 );
	m_pQuickSlot2Button_guess->setArmedColor( 0, 0, 0, 255 );

	m_pQuickSlot3Button_guess = new ColorButton( "", 607, 73, 99, 99, false, false );
	m_pQuickSlot3Button_guess->setParent( m_pInventory_guess );
	m_pQuickSlot3Button_guess->addActionSignal( new CRECBInventoryHandler_Command( this, 14 ) );
	m_pQuickSlot3Button_guess->setUnArmedBorderColor( 0, 0, 0, 255 );
	m_pQuickSlot3Button_guess->setArmedBorderColor( 255, 255, 255, 255 );
	m_pQuickSlot3Button_guess->setUnArmedColor( 0, 0, 0, 255 );
	m_pQuickSlot3Button_guess->setArmedColor( 0, 0, 0, 255 );

	m_pPopupImage_guess = new CImageLabel( NULL, 0, 0 );
	m_pPopupImage_guess->setVisible( false );
	m_pPopupImage_guess->setParent( this );
	m_pPopupImage_guess->setImage( NULL );

	m_pMenuImage_guess = new CImageLabel( NULL, -5, -5 );
	m_pMenuImage_guess->setVisible( false );
	m_pMenuImage_guess->setParent( m_pInventory_guess );
	m_pMenuImage_guess->setImage( vgui_LoadTGA( "gfx/vgui/inventory_weapons.tga" ) );

	m_pMenuButtons_guess[0] = new ColorButton( "", 0, 0, 126, 42, false, false );
	m_pMenuButtons_guess[0]->setParent( m_pInventory_guess );
	m_pMenuButtons_guess[0]->addActionSignal( new CRECBInventoryHandler_Command( this, 50 ) );
	m_pMenuButtons_guess[0]->setVisible( false );
	m_pMenuButtons_guess[0]->setContentAlignment( Label::a_center );

	m_pMenuButtons_guess[1] = new ColorButton( "", 0, 42, 126, 42, false, false );
	m_pMenuButtons_guess[1]->setParent( m_pInventory_guess );
	m_pMenuButtons_guess[1]->addActionSignal( new CRECBInventoryHandler_Command( this, 51 ) );
	m_pMenuButtons_guess[1]->setVisible( false );
	m_pMenuButtons_guess[1]->setContentAlignment( Label::a_center );

	m_pMenuButtons_guess[2] = new ColorButton( "", 0, 84, 126, 42, false, false );
	m_pMenuButtons_guess[2]->setParent( m_pInventory_guess );
	m_pMenuButtons_guess[2]->addActionSignal( new CRECBInventoryHandler_Command( this, 52 ) );
	m_pMenuButtons_guess[2]->setVisible( false );
	m_pMenuButtons_guess[2]->setContentAlignment( Label::a_center );

	m_pMenuButtons_guess[3] = new ColorButton( "", 0, 126, 126, 42, false, false );
	m_pMenuButtons_guess[3]->setParent( m_pInventory_guess );
	m_pMenuButtons_guess[3]->addActionSignal( new CRECBInventoryHandler_Command( this, 53 ) );
	m_pMenuButtons_guess[3]->setVisible( false );
	m_pMenuButtons_guess[3]->setContentAlignment( Label::a_center );

	m_pMenuButtons_guess[4] = new ColorButton( "", 0, 126, 42, 42, false, false );
	m_pMenuButtons_guess[4]->setParent( m_pInventory_guess );
	m_pMenuButtons_guess[4]->addActionSignal( new CRECBInventoryHandler_Command( this, 54 ) );
	m_pMenuButtons_guess[4]->setVisible( false );
	m_pMenuButtons_guess[4]->setContentAlignment( Label::a_center );

	m_pMenuButtons_guess[5] = new ColorButton( "", 42, 126, 42, 42, false, false );
	m_pMenuButtons_guess[5]->setParent( m_pInventory_guess );
	m_pMenuButtons_guess[5]->addActionSignal( new CRECBInventoryHandler_Command( this, 55 ) );
	m_pMenuButtons_guess[5]->setVisible( false );
	m_pMenuButtons_guess[5]->setContentAlignment( Label::a_center );

	m_pMenuButtons_guess[6] = new ColorButton( "", 84, 126, 42, 42, false, false );
	m_pMenuButtons_guess[6]->setParent( m_pInventory_guess );
	m_pMenuButtons_guess[6]->addActionSignal( new CRECBInventoryHandler_Command( this, 56 ) );
	m_pMenuButtons_guess[6]->setVisible( false );
	m_pMenuButtons_guess[6]->setContentAlignment( Label::a_center );

	for ( i = 0; i < 7; i++ )
	{
		m_pMenuButtons_guess[i]->setUnArmedBorderColor( 255, 255, 255, 128 );
		m_pMenuButtons_guess[i]->setArmedBorderColor( 255, 255, 255, 0 );
		m_pMenuButtons_guess[i]->setUnArmedColor( 255, 255, 255, 128 );
		m_pMenuButtons_guess[i]->setArmedColor( 255, 255, 255, 0 );
	}

	m_pSelection_guess = new Panel( 36, 73, 99, 99 );
	m_pSelection_guess->setParent( m_pInventory_guess );
	m_pSelection_guess->setBgColor( 0, 0, 0, 255 );
	m_pSelection_guess->setVisible( false );
	m_pSelectionBorder = new LineBorder( Color( 255, 0, 0, 255 ) );
	m_pSelection_guess->setBorder( m_pSelectionBorder );
	m_pSelection_guess->setPaintBorderEnabled( true );

	CSchemeManager *pSchemes = gViewPort->GetSchemeManager();
	SchemeHandle_t hInventoryScheme = pSchemes->getSchemeHandle( "inventory" );
	Font *pInventoryFont = pSchemes->getFont( hInventoryScheme );

	m_pObjectiveLabel_guess = new JpObjectiveLabel( "", 404, 354, 301, 97 );
	m_pObjectiveLabel_guess->setParent( m_pInventory_guess );
	m_pObjectiveLabel_guess->setBgColor( 0, 0, 0, 255 );
	m_pObjectiveLabel_guess->setFgColor( 255, 255, 255, 0 );
	m_pObjectiveLabel_guess->setFont( pInventoryFont );
	m_pObjectiveLabel_guess->setContentAlignment( Label::a_center );
	m_pObjectiveLabel_guess->setVisible( true );
}

void CRECBInventory::ActionSignal( int cmd )
{
	char szCommand[256];
	char szCmd[32];
	int i;

	const char *pszLevel = gEngfuncs.pfnGetLevelName();

	switch ( cmd )
	{
	case 0:
		break;
	case 1:
		SelectItem_guess( 1 );
		break;
	case 2:
		SelectItem_guess( 2 );
		break;
	case 3:
		SelectItem_guess( 3 );
		break;
	case 4:
		SelectItem_guess( 4 );
		break;
	case 5:
		SelectItem_guess( 5 );
		break;
	case 6:
		SelectItem_guess( 6 );
		break;
	case 7:
		SetAction_guess( 1 );
		break;
	case 8:
		SetAction_guess( 2 );
		break;
	case 9:
		SetAction_guess( 3 );
		break;
	case 10:
		SetAction_guess( 4 );
		break;
	case 11:
		gViewPort->HideTopMenu();
		break;
	case 12:
		break;
	case 13:
		break;
	case 14:
		break;
	case 15:
		if ( !strstr( pszLevel, "cof_" ) )
			SetAction_guess( 7 );
		break;
	case 16:
		if ( !strstr( pszLevel, "cof_" ) && gViewPort && gViewPort->m_pDocuments_guess )
		{
			gViewPort->HideTopMenu();
			gViewPort->m_pDocuments_guess->setVisible( true );
			gViewPort->UpdateCursorState();
		}
		break;
	case 50:
		sprintf( szCmd, "inventoryequip %i\n", m_iSelected_guess );
		gEngfuncs.pfnServerCmd( szCmd );
		m_pMenuImage_guess->setVisible( false );
		for ( i = 0; i < 7; i++ )
			m_pMenuButtons_guess[i]->setVisible( false );
		break;
	case 51:
		{
			int x, y;

			m_iCombine_guess = m_iSelected_guess;
			m_pItemButtons_guess[m_iSelected_guess - 1]->getPos( x, y );
			m_iSelectionAlpha_guess = 255;
			g_bInventoryFadeOut_guess = true;
			m_pMenuImage_guess->setVisible( false );
			m_pSelection_guess->setPos( x, y );
			m_pSelection_guess->setVisible( true );
			for ( i = 0; i < 7; i++ )
				m_pMenuButtons_guess[i]->setVisible( false );
		}
		break;
	case 52:
		sprintf( szCmd, "inventorydrop %i\n", m_iSelected_guess );
		gEngfuncs.pfnServerCmd( szCmd );
		m_pMenuImage_guess->setVisible( false );
		for ( i = 0; i < 7; i++ )
			m_pMenuButtons_guess[i]->setVisible( false );
		break;
	case 53:
		{
			int x, y;

			m_iDualWield_guess = m_iSelected_guess;
			m_pItemButtons_guess[m_iSelected_guess - 1]->getPos( x, y );
			m_iSelectionAlpha_guess = 255;
			g_bInventoryFadeOut_guess = true;
			m_pSelection_guess->setPos( x, y );
			m_pSelection_guess->setVisible( true );
			m_pMenuImage_guess->setVisible( false );
			for ( i = 0; i < 7; i++ )
				m_pMenuButtons_guess[i]->setVisible( false );
		}
		break;
	case 54:
		if ( strlen( gHUD.GetSlotItem_guess( m_iSelected_guess ) ) && strstr( gHUD.GetSlotItem_guess( m_iSelected_guess ), "weapon_" ) )
		{
			m_pItemImages_guess[6]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( m_iSelected_guess ) ) ) );
			strncpy( gHUD.m_szInventoryWeapon1_guess, gHUD.GetSlotItem_guess( m_iSelected_guess ), 128 );
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_click.wav", 1 );
			sprintf( szCommand, "quickselset1 %i\n", m_iSelected_guess );
			gEngfuncs.pfnServerCmd( szCommand );
			m_pMenuImage_guess->setVisible( false );
			for ( i = 0; i < 7; i++ )
				m_pMenuButtons_guess[i]->setVisible( false );
		}
		else
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );

		m_iAction_guess = 0;
		break;
	case 55:
		if ( strlen( gHUD.GetSlotItem_guess( m_iSelected_guess ) ) && strstr( gHUD.GetSlotItem_guess( m_iSelected_guess ), "weapon_" ) )
		{
			m_pItemImages_guess[7]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( m_iSelected_guess ) ) ) );
			strncpy( gHUD.m_szInventoryWeapon2_guess, gHUD.GetSlotItem_guess( m_iSelected_guess ), 128 );
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_click.wav", 1 );
			sprintf( szCommand, "quickselset2 %i\n", m_iSelected_guess );
			gEngfuncs.pfnServerCmd( szCommand );
			m_pMenuImage_guess->setVisible( false );
			for ( i = 0; i < 7; i++ )
				m_pMenuButtons_guess[i]->setVisible( false );
		}
		else
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );

		m_iAction_guess = 0;
		break;
	case 56:
		if ( strlen( gHUD.GetSlotItem_guess( m_iSelected_guess ) ) && strstr( gHUD.GetSlotItem_guess( m_iSelected_guess ), "weapon_" ) )
		{
			m_pItemImages_guess[9]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( m_iSelected_guess ) ) ) );
			strncpy( gHUD.m_szInventoryWeapon3_guess, gHUD.GetSlotItem_guess( m_iSelected_guess ), 128 );
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_click.wav", 1 );
			sprintf( szCommand, "quickselset3 %i\n", m_iSelected_guess );
			gEngfuncs.pfnServerCmd( szCommand );
			m_pMenuImage_guess->setVisible( false );
			for ( i = 0; i < 7; i++ )
				m_pMenuButtons_guess[i]->setVisible( false );
		}
		else
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );

		m_iAction_guess = 0;
		break;
	default:
		gEngfuncs.Con_DPrintf( "Unknown Inventory ActionSignal %i.\n", cmd );
		break;
	}
}

void CRECBInventory::ClearItems_guess()
{
	for ( int i = 0; i < 10; i++ )
		m_pItemImages_guess[i]->setImage( NULL );

	m_iAction_guess = 0;
	m_iActionItem2_guess = 0;
	m_iActionItem1_guess = 0;
	m_iHoverItem_guess = 0;
}

void CRECBInventory::DoAction_guess( int slot )
{
	char szCommand[256];
	char szCmd[32];

	if ( m_iAction_guess <= 0 )
		return;

	const char *pszLevel = gEngfuncs.pfnGetLevelName();

	switch ( m_iAction_guess )
	{
	case 1:
		sprintf( szCmd, "inventoryequip %i\n", slot );
		gEngfuncs.pfnServerCmd( szCmd );
		break;
	case 2:
		if ( !m_iActionItem1_guess || !m_iActionItem2_guess )
			return;

		if ( m_iActionItem1_guess == m_iActionItem2_guess )
		{
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );
		}
		else
		{
			if ( strlen( gHUD.CheckItem( "combined_result", gHUD.GetSlotItem_guess( m_iActionItem1_guess ) ) ) <= 1 )
				break;

			sprintf( szCommand, "inventorycombine %i %i %s\n", m_iActionItem1_guess, m_iActionItem2_guess, gHUD.CheckItem( "combined_result", gHUD.GetSlotItem_guess( m_iActionItem1_guess ) ) );
			gEngfuncs.pfnServerCmd( szCommand );
		}

		m_iActionItem1_guess = 0;
		m_iActionItem2_guess = 0;
		break;
	case 3:
		sprintf( szCmd, "inventoryuse %i\n", slot );
		gEngfuncs.pfnServerCmd( szCmd );
		break;
	case 4:
		sprintf( szCmd, "inventorydrop %i\n", slot );
		gEngfuncs.pfnServerCmd( szCmd );
		break;
	case 5:
		if ( strlen( gHUD.GetSlotItem_guess( slot ) ) && strstr( gHUD.GetSlotItem_guess( slot ), "weapon_" ) )
		{
			m_pItemImages_guess[6]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( slot ) ) ) );
			strncpy( gHUD.m_szInventoryWeapon1_guess, gHUD.GetSlotItem_guess( slot ), 128 );
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_click.wav", 1 );
			sprintf( szCmd, "quickselset1 %i\n", slot );
			gEngfuncs.pfnServerCmd( szCmd );
		}
		else
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );
		break;
	case 6:
		if ( strlen( gHUD.GetSlotItem_guess( slot ) ) && strstr( gHUD.GetSlotItem_guess( slot ), "weapon_" ) )
		{
			m_pItemImages_guess[7]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( slot ) ) ) );
			strncpy( gHUD.m_szInventoryWeapon2_guess, gHUD.GetSlotItem_guess( slot ), 128 );
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_click.wav", 1 );
			sprintf( szCmd, "quickselset2 %i\n", slot );
			gEngfuncs.pfnServerCmd( szCmd );
		}
		else
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );
		break;
	case 7:
		if ( !strstr( pszLevel, "cof_" ) && m_iActionItem1_guess && m_iActionItem2_guess )
		{
			if ( m_iActionItem1_guess == m_iActionItem2_guess )
			{
				gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );
				m_iActionItem2_guess = 0;
				m_iActionItem1_guess = 0;
				return;
			}

			if ( strlen( gHUD.CheckItem( "classname", gHUD.GetSlotItem_guess( m_iActionItem1_guess ) ) ) > 1 )
			{
				sprintf( szCommand, "inventorydualwield %i %i\n", m_iActionItem1_guess, m_iActionItem2_guess );
				gEngfuncs.pfnServerCmd( szCommand );
				m_iActionItem2_guess = 0;
				m_iActionItem1_guess = 0;
				return;
			}
		}
		return;
	case 8:
		if ( strlen( gHUD.GetSlotItem_guess( slot ) ) && strstr( gHUD.GetSlotItem_guess( slot ), "weapon_" ) )
		{
			m_pItemImages_guess[9]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( slot ) ) ) );
			strncpy( gHUD.m_szInventoryWeapon3_guess, gHUD.GetSlotItem_guess( slot ), 128 );
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_click.wav", 1 );
			sprintf( szCmd, "quickselset3 %i\n", slot );
			gEngfuncs.pfnServerCmd( szCmd );
		}
		else
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );
		break;
	default:
		return;
	}

	m_iAction_guess = 0;
}

void CRECBInventory::Initialize()
{
	if ( gViewPort && gViewPort->m_pRECBInventory_guess )
		gViewPort->m_pRECBInventory_guess->Update_guess();

	m_pItemImages_guess[6]->setImage( NULL );
	gHUD.m_szInventoryWeapon1_guess[0] = 0;
	if ( strlen( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon1_guess ) ) > 2 )
		m_pItemImages_guess[6]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon1_guess ) ) );

	m_pItemImages_guess[7]->setImage( NULL );
	gHUD.m_szInventoryWeapon2_guess[0] = 0;
	if ( strlen( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon2_guess ) ) > 2 )
		m_pItemImages_guess[7]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon2_guess ) ) );

	m_pItemImages_guess[9]->setImage( NULL );
	gHUD.m_szInventoryWeapon3_guess[0] = 0;
	if ( strlen( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon3_guess ) ) > 2 )
		m_pItemImages_guess[9]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon3_guess ) ) );
}

int CRECBInventory::KeyInput( int down, int keynum, const char *pszCurrentBinding )
{
	if ( pszCurrentBinding )
	{
		if ( !strcmp( pszCurrentBinding, "toggleconsole" ) )
			return 1;

		if ( !strcmp( pszCurrentBinding, "+inventory" ) && isVisible() )
		{
			gViewPort->HideTopMenu();
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_close.wav", 1 );
		}
	}

	return 0;
}

bool CRECBInventory::MouseOver_guess( int x, int y, int wide, int tall )
{
	int cx, cy;

	if ( !gViewPort )
		return false;

	getApp()->getCursorPos( cx, cy );

	if ( cx >= x && cx <= x + wide && cy >= y && cy <= y + wide )
		return true;

	return false;
}

int MsgInventChck_guess( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );

	if ( gViewPort && gViewPort->m_pRECBInventory_guess )
		gViewPort->m_pRECBInventory_guess->Update_guess();

	return 1;
}

int MsgQSlotClear_guess( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	int iSlot = READ_BYTE();
	int iOn = READ_BYTE();

	if ( gViewPort->m_pRECBInventory_guess )
	{
		switch ( iSlot )
		{
		case 1:
			if ( !iOn )
			{
				gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[6]->setImage( NULL );
				gHUD.m_szInventoryWeapon1_guess[0] = 0;
			}
			else
			{
				if ( strlen( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon1_guess ) ) > 2 )
					gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[6]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon1_guess ) ) );
			}
			break;
		case 2:
			if ( !iOn )
			{
				gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[7]->setImage( NULL );
				gHUD.m_szInventoryWeapon2_guess[0] = 0;
			}
			else
			{
				if ( strlen( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon2_guess ) ) > 2 )
					gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[7]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon2_guess ) ) );
			}
			break;
		case 3:
			if ( !iOn )
			{
				gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[9]->setImage( NULL );
				gHUD.m_szInventoryWeapon3_guess[0] = 0;
			}
			else
			{
				if ( strlen( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon3_guess ) ) > 2 )
					gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[9]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.m_szInventoryWeapon3_guess ) ) );
			}
			break;
		}
	}

	return 1;
}

int MsgQSlotUpdate_guess( const char *pszName, int iSize, void *pbuf )
{
	char szCommand[128];

	BEGIN_READ( pbuf, iSize );
	int iSlot = READ_BYTE();
	int iItem = READ_BYTE();

	if ( gViewPort->m_pRECBInventory_guess )
	{
		switch ( iSlot )
		{
		case 1:
			if ( strlen( gHUD.GetSlotItem_guess( iItem ) ) && strstr( gHUD.GetSlotItem_guess( iItem ), "weapon_" ) )
			{
				gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[6]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( iItem ) ) ) );
				strncpy( gHUD.m_szInventoryWeapon1_guess, gHUD.GetSlotItem_guess( iItem ), 128 );
				sprintf( szCommand, "quickselset1 %i\n", iItem );
				gEngfuncs.pfnServerCmd( szCommand );
			}
			break;
		case 2:
			if ( strlen( gHUD.GetSlotItem_guess( iItem ) ) && strstr( gHUD.GetSlotItem_guess( iItem ), "weapon_" ) )
			{
				gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[7]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( iItem ) ) ) );
				strncpy( gHUD.m_szInventoryWeapon2_guess, gHUD.GetSlotItem_guess( iItem ), 128 );
				sprintf( szCommand, "quickselset2 %i\n", iItem );
				gEngfuncs.pfnServerCmd( szCommand );
			}
			break;
		case 3:
			if ( strlen( gHUD.GetSlotItem_guess( iItem ) ) && strstr( gHUD.GetSlotItem_guess( iItem ), "weapon_" ) )
			{
				gViewPort->m_pRECBInventory_guess->m_pItemImages_guess[9]->setImage( vgui_LoadTGA( gHUD.CheckItem( "inventory_picture", gHUD.GetSlotItem_guess( iItem ) ) ) );
				strncpy( gHUD.m_szInventoryWeapon3_guess, gHUD.GetSlotItem_guess( iItem ), 128 );
				sprintf( szCommand, "quickselset3 %i\n", iItem );
				gEngfuncs.pfnServerCmd( szCommand );
			}
			break;
		}
	}

	return 1;
}

void CRECBInventory::SelectItem_guess( int slot )
{
	char szCommand[256];
	char szCmd[32];

	if ( m_iDualWield_guess )
	{
		if ( m_iDualWield_guess != slot )
		{
			if ( strlen( gHUD.CheckItem( "classname", gHUD.GetSlotItem_guess( slot ) ) ) > 1 )
			{
				sprintf( szCmd, "inventorydualwield %i %i\n", m_iDualWield_guess, slot );
				gEngfuncs.pfnServerCmd( szCmd );
				m_iDualWield_guess = 0;
				g_bInventoryFadeOut_guess = false;
				m_pSelection_guess->setVisible( false );
				return;
			}

			m_iDualWield_guess = 0;
			g_bInventoryFadeOut_guess = false;
			m_pSelection_guess->setVisible( false );
			gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );
			return;
		}
	}
	else
	{
		if ( m_iCombine_guess )
		{
			if ( m_iCombine_guess != slot )
			{
				if ( strlen( gHUD.CheckItem( "combined_result", gHUD.GetSlotItem_guess( slot ) ) ) > 1 )
				{
					sprintf( szCommand, "inventorycombine %i %i %s\n", m_iCombine_guess, slot, gHUD.CheckItem( "combined_result", gHUD.GetSlotItem_guess( m_iCombine_guess ) ) );
					gEngfuncs.pfnServerCmd( szCommand );
					m_iDualWield_guess = 0;
					m_iCombine_guess = 0;	// ReCoF (B151): retail left the combine slot armed after a combine
					g_bInventoryFadeOut_guess = false;
					m_pSelection_guess->setVisible( false );
					return;
				}

				m_iCombine_guess = 0;
				g_bInventoryFadeOut_guess = false;
				m_pSelection_guess->setVisible( false );
				gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );
				return;
			}
		}
		else
		{
			if ( strlen( gHUD.GetSlotItem_guess( slot ) ) > 1 )
			{
				int x, y, wide, tall;
				bool bItem = false;

				m_iSelected_guess = slot;
				gEngfuncs.pfnPlaySoundByName( "inventory/inventory_toggle.wav", 1 );

				if ( !strstr( gHUD.GetSlotItem_guess( m_iSelected_guess ), "weapon_" ) )
					bItem = true;

				m_pItemButtons_guess[m_iSelected_guess - 1]->getPos( x, y );
				m_pItemButtons_guess[m_iSelected_guess - 1]->getSize( wide, tall );
				x += wide;
				m_pMenuButtons_guess[0]->getSize( wide, tall );

				m_pMenuImage_guess->setVisible( true );
				m_pMenuImage_guess->setPos( x - 5, y - 5 );

				if ( bItem )
					m_pMenuImage_guess->setImage( vgui_LoadTGA( "gfx/vgui/inventory_items.tga" ) );
				else
					m_pMenuImage_guess->setImage( vgui_LoadTGA( "gfx/vgui/inventory_weapons.tga" ) );

				for ( int i = 1; i <= 7; i++ )
				{
					m_pMenuButtons_guess[i - 1]->setPos( x, y );

					if ( i >= 4 )
					{
						if ( !bItem )
							m_pMenuButtons_guess[i - 1]->setVisible( true );
						else
							m_pMenuButtons_guess[i - 1]->setVisible( false );
					}
					else
					{
						m_pMenuButtons_guess[i - 1]->setVisible( true );
					}

					if ( i <= 4 )
						y += tall;

					if ( i >= 5 )
						x += 42;
				}
			}
			return;
		}
	}

	gEngfuncs.pfnPlaySoundByName( "inventory/inventory_no.wav", 1 );
}

void CRECBInventory::SetAction_guess( int action )
{
	gEngfuncs.pfnPlaySoundByName( "inventory/inventory_toggle.wav", 1 );

	m_iAction_guess = action;
	m_iActionItem2_guess = 0;
	m_iActionItem1_guess = 0;
}

void CRECBInventory::Update_guess()
{
	const char *pszLevel = gEngfuncs.pfnGetLevelName();

	if ( strstr( pszLevel, "cof_" ) )
	{
		m_pBackground_guess->setVisible( true );
		m_pInventory_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_inventory_p.tga" ) );

		m_pItemButtons_guess[0]->setPos( 36, 91 );
		m_pItemImages_guess[0]->setPos( 31, 86 );
		m_pItemButtons_guess[1]->setPos( 138, 91 );
		m_pItemImages_guess[1]->setPos( 133, 86 );
		m_pItemButtons_guess[2]->setPos( 240, 91 );
		m_pItemImages_guess[2]->setPos( 235, 86 );
		m_pItemButtons_guess[3]->setPos( 36, 213 );
		m_pItemImages_guess[3]->setPos( 31, 208 );
		m_pItemButtons_guess[4]->setPos( 138, 213 );
		m_pItemImages_guess[4]->setPos( 133, 208 );
		m_pItemButtons_guess[5]->setPos( 240, 213 );
		m_pItemImages_guess[5]->setPos( 235, 208 );
		m_pItemImages_guess[8]->setPos( 31, 329 );
		m_pQuickSlot1Button_guess->setPos( 403, 90 );
		m_pQuickSlot2Button_guess->setPos( 505, 90 );
		m_pQuickSlot3Button_guess->setPos( 607, 90 );
		m_pItemImages_guess[6]->setPos( 398, 85 );
		m_pItemImages_guess[7]->setPos( 500, 85 );
		m_pItemImages_guess[9]->setPos( 602, 85 );

		m_pOptionButtons_guess[0]->setSize( 363, 42 );
		m_pOptionButtons_guess[0]->setPos( 373, 243 );
		m_pOptionButtons_guess[1]->setSize( 363, 42 );
		m_pOptionButtons_guess[1]->setPos( 373, 285 );
		m_pOptionButtons_guess[2]->setSize( 363, 42 );
		m_pOptionButtons_guess[2]->setPos( 373, 327 );
		m_pOptionButtons_guess[3]->setVisible( false );
		m_pOptionButtons_guess[4]->setVisible( false );

		setBgColor( 255, 0, 0, 255 );
	}
	else
	{
		m_pBackground_guess->setVisible( true );

		if ( m_iThree_guess )
			m_pInventory_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_inventory_three.tga" ) );
		else
			m_pInventory_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_inventory.tga" ) );


		m_pItemButtons_guess[0]->setPos( 36, 73 );
		m_pItemImages_guess[0]->setPos( 31, 68 );
		m_pItemButtons_guess[1]->setPos( 138, 73 );
		m_pItemImages_guess[1]->setPos( 133, 68 );
		m_pItemButtons_guess[2]->setPos( 240, 73 );
		m_pItemImages_guess[2]->setPos( 235, 68 );
		m_pItemButtons_guess[3]->setPos( 36, 232 );
		m_pItemImages_guess[3]->setPos( 31, 227 );
		m_pItemButtons_guess[4]->setPos( 138, 232 );
		m_pItemImages_guess[4]->setPos( 133, 227 );
		m_pItemButtons_guess[5]->setPos( 240, 232 );
		m_pItemImages_guess[5]->setPos( 235, 227 );
		m_pItemImages_guess[8]->setPos( 31, 348 );
		m_pQuickSlot1Button_guess->setPos( 403, 73 );
		m_pQuickSlot2Button_guess->setPos( 505, 73 );
		m_pQuickSlot3Button_guess->setPos( 607, 73 );
		m_pItemImages_guess[6]->setPos( 398, 68 );
		m_pItemImages_guess[7]->setPos( 500, 68 );
		m_pItemImages_guess[9]->setPos( 602, 68 );

		m_pOptionButtons_guess[0]->setSize( 363, 47 );
		m_pOptionButtons_guess[0]->setPos( 373, 226 );
		m_pOptionButtons_guess[1]->setSize( 363, 47 );
		m_pOptionButtons_guess[1]->setPos( 373, 276 );
		m_pOptionButtons_guess[2]->setSize( 363, 47 );
		m_pOptionButtons_guess[2]->setPos( 373, 325 );
		m_pOptionButtons_guess[3]->setVisible( false );
		m_pOptionButtons_guess[4]->setVisible( true );

		setBgColor( 0, 0, 0, 255 );
	}

	m_pBackgroundPanel_guess->setBgColor( 0, 0, 0, 0 );
}

void VGUI_QSlotInit_guess()
{
	gEngfuncs.pfnHookUserMsg( "QSlotClear", MsgQSlotClear_guess );
	gEngfuncs.pfnHookUserMsg( "QSlotUpdate", MsgQSlotUpdate_guess );
	gEngfuncs.pfnHookUserMsg( "InventChck", MsgInventChck_guess );
}

void CRECBInventory::setVisible( bool state )
{
	int i;

	for ( i = 0; i < 7; i++ )
		m_pMenuButtons_guess[i]->setVisible( false );

	m_pMenuImage_guess->setVisible( false );
	m_pSelection_guess->setVisible( false );

	m_iSelectionAlpha_guess = 0;
	m_iSelected_guess = 0;
	m_iCombine_guess = 0;
	m_iDualWield_guess = 0;
	m_iAction_guess = 0;
	m_iActionItem2_guess = 0;
	m_iActionItem1_guess = 0;
	m_iHoverItem_guess = 0;

	if ( state )
	{
		if ( m_pItemImages_guess[8] )
			m_pItemImages_guess[8]->setImage( NULL );

		if ( m_pPopupImage_guess )
			m_pPopupImage_guess->setImage( NULL );

		m_bUnknownE0_guess = false;
		m_iUnknownE4_guess = 0;

		if ( strlen( gHUD.m_szObjectiveTitle_guess ) > 2 && strlen( gHUD.m_szObjectiveText_guess ) > 2 )
		{
			ParseHintTokens( gHUD.m_szObjectiveTitle_guess, gHUD.m_szObjectiveText_guess );

			if ( strlen( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess ) > 2 )
			{
				char szText[256];
				unsigned int j;

				for ( i = 0, j = 0; j < strlen( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess ); j++ )
				{
					i++;

					if ( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[j] == '@' )
						szText[j] = '\n';
					else
						szText[j] = gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[j];
				}

				szText[i] = 0;
				m_pObjectiveLabel_guess->setText( szText );
			}

			m_pObjectiveLabel_guess->setVisible( true );
		}
		else
			m_pObjectiveLabel_guess->setVisible( false );
	}
	else
	{
		if ( m_pPopupImage_guess )
			m_pPopupImage_guess->setImage( NULL );

		m_bUnknownE0_guess = false;
		m_iUnknownE4_guess = 0;

		if ( gEngfuncs.pfnGetLevelName() )
		{
			for ( i = 0; i < 5; i++ )
			{
				if ( m_pOptionButtons_guess[i] && m_pOptionButtons_guess[i]->isVisible() )
					m_pOptionButtons_guess[i]->cursorExited();
			}

			for ( i = 0; i < 6; i++ )
			{
				if ( m_pItemButtons_guess[i] && m_pItemButtons_guess[i]->isVisible() )
					m_pItemButtons_guess[i]->cursorExited();
			}

			m_pQuickSlot1Button_guess->cursorExited();
			m_pQuickSlot2Button_guess->cursorExited();
			m_pQuickSlot3Button_guess->cursorExited();
		}

		m_pItemImages_guess[8]->setImage( NULL );
	}

	Panel::setVisible( state );
}

// ReCoF (B334): the bag or pocket item whose picture m_pItemImages_guess[8] shows (CRECBInventory::solve)
static char s_szRcHoverItem[128];

void CRECBInventory::solve()
{
	int i;

	if ( isVisible() )
	{
		if ( m_pSelection_guess->isVisible() )
		{
			if ( g_bInventoryFadeOut_guess )
			{
				if ( m_iSelectionAlpha_guess > 0 )
					m_iSelectionAlpha_guess -= 15;

				if ( m_iSelectionAlpha_guess <= 0 )
				{
					g_bInventoryFadeOut_guess = false;
					m_iSelectionAlpha_guess = 0;
				}
			}
			else
			{
				if ( m_iSelectionAlpha_guess < 255 )
					m_iSelectionAlpha_guess += 15;

				if ( m_iSelectionAlpha_guess >= 255 )
				{
					g_bInventoryFadeOut_guess = true;
					m_iSelectionAlpha_guess = 255;
				}
			}

			// ReCoF (B151): retail made a new LineBorder every frame and never freed the old one
			m_pSelectionBorder->setLineColor( 255, 0, 0, m_iSelectionAlpha_guess );
			m_pSelection_guess->setBorder( m_pSelectionBorder );
		}

		if ( iWheelchair_guess && isVisible() )
		{
			gViewPort->HideTopMenu();
			return;
		}

		if ( getApp()->isMouseDown( MOUSE_LEFT, NULL ) )
		{
			bool bArmed = false;

			for ( i = 0; i < 7; i++ )
			{
				if ( m_pMenuButtons_guess[i]->isArmed() )
					bArmed = true;
			}

			if ( !bArmed )
			{
				for ( i = 0; i < 7; i++ )
					m_pMenuButtons_guess[i]->setVisible( false );

				m_pMenuImage_guess->setVisible( false );
			}
		}

		if ( m_pPopupImage_guess->isVisible() )
			m_pPopupImage_guess->setVisible( false );

		// ReCoF (B334): retail loaded the item's picture from disk again on every frame while the item was pointed
		// at, and kept every copy with a texture of its own; it is loaded when the item pointed at changes, as the
		// quick slots below already did
		for ( i = 1; i <= 6; i++ )
		{
			if ( m_pItemButtons_guess[i - 1]->isArmed() && strlen( gHUD.GetSlotItem_guess( i ) ) > 1 )
			{
				if ( m_iHoverItem_guess != i || strcmp( s_szRcHoverItem, gHUD.GetSlotItem_guess( i ) ) )
				{
					m_pItemImages_guess[8]->setImage( vgui_LoadTGA( gHUD.CheckItem( "check_picture", gHUD.GetSlotItem_guess( i ) ) ) );
					strncpy( s_szRcHoverItem, gHUD.GetSlotItem_guess( i ), sizeof( s_szRcHoverItem ) - 1 );
					s_szRcHoverItem[sizeof( s_szRcHoverItem ) - 1] = 0;
				}

				m_iHoverItem_guess = i;
			}
		}

		if ( m_pQuickSlot1Button_guess->isArmed() && m_iHoverItem_guess != 7 && strlen( gHUD.m_szInventoryWeapon1_guess ) > 1 )
		{
			m_iHoverItem_guess = 7;
			m_pItemImages_guess[8]->setImage( vgui_LoadTGA( gHUD.CheckItem( "check_picture", gHUD.m_szInventoryWeapon1_guess ) ) );
		}

		if ( m_pQuickSlot2Button_guess->isArmed() && m_iHoverItem_guess != 8 && strlen( gHUD.m_szInventoryWeapon2_guess ) > 1 )
		{
			m_iHoverItem_guess = 8;
			m_pItemImages_guess[8]->setImage( vgui_LoadTGA( gHUD.CheckItem( "check_picture", gHUD.m_szInventoryWeapon2_guess ) ) );
		}

		if ( m_pQuickSlot3Button_guess->isArmed() && m_iHoverItem_guess != 9 && strlen( gHUD.m_szInventoryWeapon3_guess ) > 1 )
		{
			m_iHoverItem_guess = 9;
			m_pItemImages_guess[8]->setImage( vgui_LoadTGA( gHUD.CheckItem( "check_picture", gHUD.m_szInventoryWeapon3_guess ) ) );
		}

		if ( m_iHoverItem_guess >= 1 && m_iHoverItem_guess <= 6 && !m_pItemButtons_guess[m_iHoverItem_guess - 1]->isArmed() )
		{
			m_pItemImages_guess[8]->setImage( NULL );
			m_iHoverItem_guess = 0;
		}

		if ( m_iHoverItem_guess == 7 && !m_pQuickSlot1Button_guess->isArmed() )
		{
			m_pItemImages_guess[8]->setImage( NULL );
			m_iHoverItem_guess = 0;
		}

		if ( m_iHoverItem_guess == 8 && !m_pQuickSlot2Button_guess->isArmed() )
		{
			m_pItemImages_guess[8]->setImage( NULL );
			m_iHoverItem_guess = 0;
		}

		if ( m_iHoverItem_guess == 9 && !m_pQuickSlot3Button_guess->isArmed() )
		{
			m_pItemImages_guess[8]->setImage( NULL );
			m_iHoverItem_guess = 0;
		}

		if ( m_pQuickSlot1Button_guess->isArmed() && m_iHoverItem_guess != 7 )
			m_iHoverItem_guess = 7;

		if ( m_pQuickSlot2Button_guess->isArmed() && m_iHoverItem_guess != 8 )
			m_iHoverItem_guess = 8;

		if ( m_pQuickSlot3Button_guess->isArmed() && m_iHoverItem_guess != 9 )
			m_iHoverItem_guess = 9;
	}

	Panel::solve();
}
