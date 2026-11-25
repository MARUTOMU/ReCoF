// ======================================
// Cry of Fear HUD panel
// (class name from the RTTI of client.dll, file name guessed)
// ======================================

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "const.h"
#include "entity_types.h"
#include "cdll_int.h"
#include "vgui_TeamFortressViewport.h"
#include "HUDControl_guess.h"
#include "vgui_ScorePanel.h"
#include "..\game_shared\vgui_loadtga.h"
#include "windows.h"

#include <string.h>
#include <stdio.h>
#include "rc_client.h"	// ReCoF
#include "jp_subtitle_render.h"	// JP-POC
#include "VGUI_SurfaceBase.h"	// JP-POC: createNewTextureID via getSurfaceBase()
#include <stdarg.h>	// JP-POC-DEBUG
#include <stdlib.h>	// JP-POC

extern cvar_t *jp_poc;	// JP-POC: defined in vgui_subtitles.cpp

// JP-POC-DEBUG: append diagnostics to jp_debug.log in the game root.
static void JpLogHud(const char *fmt, ...)
{
	FILE *f = fopen("jp_debug.log", "a");
	if (!f) return;
	va_list ap; va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}

// JP-MOD: hint label subclass. The stock Label cannot render UTF-8
// Japanese, and JP text drawn from CHUDControl::paint() is painted over
// by the hint bar ImagePanel (a sibling child painted after the parent).
// Drawing from inside the label's own paint() puts the JP text on top
// of the bar, because the label is created after the bar panel.
class JpHintLabel : public Label
{
public:
	JpHintLabel( const char *text, int x, int y, int wide, int tall )
		: Label( text, x, y, wide, tall )
	{
	}

	virtual void paint()
	{
		CHUDControl *hud = gViewPort ? gViewPort->m_pHUDControl_guess : NULL;
		const char *pszLine = NULL;
		if ( hud && jp_poc && jp_poc->value && hud->m_bHint_guess )
		{
			switch ( hud->m_iHintLine_guess )
			{
			case 1: pszLine = hud->m_szHintLine1_guess; break;
			case 2: pszLine = hud->m_szHintLine2_guess; break;
			case 3: pszLine = hud->m_szHintLine3_guess; break;
			}
		}
		if ( !pszLine || !JpTextIsUtf8Japanese( pszLine ) )
		{
			Label::paint();
			return;
		}
		// JP line: skip Label::paint() (it would mojibake) and draw the
		// line with the shared JP renderer, centered in the 800x48 box.
		char szDraw[256];
		strncpy( szDraw, pszLine, 255 );
		szDraw[255] = '\0';
		for ( int ci = 0; szDraw[ci]; ci++ )
		{
			if ( szDraw[ci] == '@' )
				szDraw[ci] = ' ';
		}
		int iPx = ( hud->m_pCreditsFont_guess ) ? hud->m_pCreditsFont_guess->getTall() : 0;
		if ( iPx < 8 )
			iPx = 16;
		if ( !JpSharedInitFont( iPx ) )
			return;
		float fW = JpSharedMeasure( szDraw );
		if ( fW <= 0 )
			return;
		static int s_iLabelLog = 0;
		if ( s_iLabelLog < 20 )
		{
			s_iLabelLog++;
			JpLogHud( "HINT labeldraw: line=%d alpha=%d px=%d w=%.1f",
				hud->m_iHintLine_guess, hud->m_iHintAlpha_guess, iPx, (double)fW );
		}
		int iBase = ( 48 - iPx ) / 2 + JpSharedAscentPx();
		const unsigned char *pS = (const unsigned char *)szDraw;
		int iLen = (int)strlen( szDraw );
		for ( int pass = 0; pass < 2; pass++ )
		{
			int ox = ( pass == 0 ) ? 1 : 0;
			int oy = ( pass == 0 ) ? 1 : 0;
			int cr = ( pass == 0 ) ? 0 : 255;
			int cg = ( pass == 0 ) ? 0 : 255;
			int cb = ( pass == 0 ) ? 0 : 255;
			float penX = 400.0f - fW / 2.0f;
			int pos = 0;
			while ( pos < iLen )
			{
				int cp;
				if ( !JpSharedDecodeUtf8( pS, iLen, pos, cp ) )
					break;
				const JpGlyph *g = JpSharedGlyphMetrics( cp );
				if ( !g )
					break;
				if ( g->texId < 0 )
				{
					unsigned char *rgba = NULL;
					if ( JpSharedGlyphBitmap( cp, &rgba ) )
					{
						if ( rgba )
						{
							int texId = getSurfaceBase()->createNewTextureID();
							if ( texId > 0 )
							{
								drawSetTextureRGBA( texId, (const char *)rgba, g->w, g->h );
								JpSharedSetGlyphTexture( g, texId );
							}
							else
								JpSharedSetGlyphTexture( g, 0 );
							free( rgba );
						}
						else
							JpSharedSetGlyphTexture( g, 0 );
					}
					else
						JpSharedSetGlyphTexture( g, 0 );
				}
				if ( g->texId > 0 && g->w > 0 && g->h > 0 )
				{
					int dx = (int)( penX + 0.5f ) + g->xoff + ox;
					int dy = iBase + g->yoff + oy;
					drawSetTexture( g->texId );
					drawSetColor( cr, cg, cb, hud->m_iHintAlpha_guess );
					drawTexturedRect( dx, dy, dx + g->w, dy + g->h );
				}
				penX += g->adv;
			}
		}
	}
};

// a method of CHud in 1.6a (called with ecx = &gHUD): the key bound to a command, read from config.cfg

extern bool		g_bHandsLoaded_guess;
extern bool		bWheelchairLock_guess;
extern int		g_iDickMode_guess;
extern int		iWheelchair_guess;

int		g_iLetterboxHeight_guess;
float	g_flLookingAt_guess;
int		g_iSimonMode_guess;


int MsgStamina_guess( const char *pszName, int iSize, void *pbuf )
{
	if ( gViewPort->m_pHUDControl_guess )
	{
		BEGIN_READ( pbuf, iSize );
		gViewPort->m_pHUDControl_guess->m_iStamina_guess = READ_BYTE();
		float flTime = READ_COORD();
		if ( flTime > 0 )
			gViewPort->m_pHUDControl_guess->m_flStaminaFlashTime_guess = gEngfuncs.GetClientTime() + flTime;
	}

	return 1;
}

int MsgCoFLogo_guess( const char *pszName, int iSize, void *pbuf )
{
	if ( !gViewPort->m_pHUDControl_guess )
		return 1;

	BEGIN_READ( pbuf, iSize );
	gViewPort->m_pHUDControl_guess->m_iLogo_guess = READ_BYTE();
	gViewPort->m_pHUDControl_guess->m_iLogoAlpha_guess = 255;
	gViewPort->m_pHUDControl_guess->m_bLogoF5D_guess = false;
	gViewPort->m_pHUDControl_guess->m_flLogoTime_guess = 0;

	switch ( gViewPort->m_pHUDControl_guess->m_iLogo_guess )
	{
	case 0:
		gViewPort->m_pHUDControl_guess->m_pLogoImage_guess = vgui_LoadTGA( "gfx/vgui/640_logo_logo.tga" );
		break;
	case 1:
		gViewPort->m_pHUDControl_guess->m_pLogoImage_guess = vgui_LoadTGA( "gfx/vgui/640_logo_andreas.tga" );
		break;
	case 2:
		gViewPort->m_pHUDControl_guess->m_pLogoImage_guess = vgui_LoadTGA( "gfx/vgui/640_logo_james.tga" );
		break;
	case 3:
		gViewPort->m_pHUDControl_guess->m_pLogoImage_guess = vgui_LoadTGA( "gfx/vgui/640_logo_jordy.tga" );
		break;
	case 4:
		gViewPort->m_pHUDControl_guess->m_pLogoImage_guess = vgui_LoadTGA( "gfx/vgui/640_logo_stig.tga" );
		break;
	case 5:
		gViewPort->m_pHUDControl_guess->m_pLogoImage_guess = vgui_LoadTGA( "gfx/vgui/640_logo_lasse.tga" );
		break;
	case 6:
		gViewPort->m_pHUDControl_guess->m_pLogoImage_guess = vgui_LoadTGA( "gfx/vgui/640_logo_aina.tga" );
		break;
	}

	gViewPort->m_pHUDControl_guess->m_pLogo_guess = new ImagePanel( gViewPort->m_pHUDControl_guess->m_pLogoImage_guess );
	gViewPort->m_pHUDControl_guess->m_pLogo_guess->setParent( gViewPort->m_pHUDControl_guess );
	int x = ScreenWidth / 2 - gViewPort->m_pHUDControl_guess->m_pLogo_guess->getWide() / 2;
	int y = ScreenHeight / 2 - gViewPort->m_pHUDControl_guess->m_pLogo_guess->getTall() / 2;
	gViewPort->m_pHUDControl_guess->m_pLogo_guess->setPos( x, y );
	gViewPort->m_pHUDControl_guess->m_pLogoImage_guess->setColor( Color( 255, 255, 255, 255 ) );

	if ( gViewPort->m_pHUDControl_guess->m_iLogo_guess == 0 )
		gViewPort->m_pHUDControl_guess->m_flLogoTime_guess = gEngfuncs.GetClientTime() + 5;
	else
		gViewPort->m_pHUDControl_guess->m_flLogoTime_guess = gEngfuncs.GetClientTime() + 3;

	gViewPort->m_pHUDControl_guess->m_bShowLogo_guess = true;

	return 1;
}

int MsgShowHint_guess( const char *pszName, int iSize, void *pbuf )
{
	char szImage[256];

	BEGIN_READ( pbuf, iSize );

	if ( gViewPort->m_pHUDControl_guess )
	{
		strcpy( szImage, READ_STRING() );

		if ( strlen( szImage ) > 1 )
		{
			gViewPort->m_pHUDControl_guess->m_pInventoryHint_guess->setImage( vgui_LoadTGA( szImage ) );
			gViewPort->m_pHUDControl_guess->m_pInventoryHint_guess->setPos( gViewPort->m_pHUDControl_guess->m_pInventoryHint_guess->getWide() * 1.2 - gViewPort->m_pHUDControl_guess->m_pInventoryHint_guess->getWide(), gViewPort->m_pHUDControl_guess->m_iInventoryHintY_guess );
			gViewPort->m_pHUDControl_guess->m_bInventoryHint_guess = true;
			gViewPort->m_pHUDControl_guess->m_flHintSlideInTime_guess = gEngfuncs.GetClientTime();
			gViewPort->m_pHUDControl_guess->m_flHintSlideInLength_guess = 1;
			gViewPort->m_pHUDControl_guess->m_flHintSlideOutTime_guess = gEngfuncs.GetClientTime() + 15;
			gViewPort->m_pHUDControl_guess->m_flHintSlideOutLength_guess = 1;
		}
	}

	return 1;
}

int MsgCameraZoom_guess( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	gHUD.m_flCameraZoomFOV_guess = READ_COORD();
	gHUD.m_iCameraZoom_guess = READ_BYTE();
	gHUD.m_bCameraZoomOn_guess = READ_BYTE() != 0;

	if ( gHUD.m_iCameraZoom_guess )
		gHUD.m_flNextSwayTime_guess = gHUD.m_flTime;

	return 1;
}

int MsgHealthCap_guess( const char *pszName, int iSize, void *pbuf )
{
	if ( gViewPort->m_pHUDControl_guess )
	{
		BEGIN_READ( pbuf, iSize );
		int iType = READ_BYTE();

		switch ( iType )
		{
		case 0:
			gViewPort->m_pHUDControl_guess->m_pHealthBarFull_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar_bar.tga" ) );
			gViewPort->m_pHUDControl_guess->m_pHealthBar_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar.tga" ) );
			gViewPort->m_pHUDControl_guess->m_pHealthBarEmpty_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar_barempty.tga" ) );
			break;
		case 1:
			gViewPort->m_pHUDControl_guess->m_pHealthBarFull_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar_bar2.tga" ) );
			gViewPort->m_pHUDControl_guess->m_pHealthBar_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar2.tga" ) );
			gViewPort->m_pHUDControl_guess->m_pHealthBarEmpty_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar_barempty2.tga" ) );
			break;
		case 2:
			gViewPort->m_pHUDControl_guess->m_pHealthBarFull_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar_bar3.tga" ) );
			gViewPort->m_pHUDControl_guess->m_pHealthBar_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar3.tga" ) );
			gViewPort->m_pHUDControl_guess->m_pHealthBarEmpty_guess->setImage( vgui_LoadTGA( "gfx/vgui/640_healthbar_barempty3.tga" ) );
			break;
		}
	}

	return 1;
}

int MsgChapter_guess( const char *pszName, int iSize, void *pbuf )
{
	char szSound[256];

	BEGIN_READ( pbuf, iSize );

	if ( gViewPort->m_pHUDControl_guess )
	{
		strcpy( gViewPort->m_pHUDControl_guess->m_szChapter_guess, READ_STRING() );
		gViewPort->m_pHUDControl_guess->m_flChapterAlpha_guess = 255;
		gViewPort->m_pHUDControl_guess->m_bChapterFadeOut_guess = false;
		gViewPort->m_pHUDControl_guess->m_bChapterF1E_guess = false;
		gViewPort->m_pHUDControl_guess->m_flChapterHoldTime_guess = 0;
		gViewPort->m_pHUDControl_guess->m_flChapterNextChar_guess = 0;
		gViewPort->m_pHUDControl_guess->m_iChapterChars_guess = 0;

		strncpy( szSound, READ_STRING(), 256 );
		if ( strlen( szSound ) > 1 )
			gEngfuncs.pfnPlaySoundByName( szSound, 1 );
	}

	return 1;
}

int MsgControlHint_guess( const char *pszName, int iSize, void *pbuf )
{
	char szName[256];
	char szFile[256];
	char szText[256];

	BEGIN_READ( pbuf, iSize );

	if ( gViewPort->m_pHUDControl_guess )
	{
		strncpy( szName, READ_STRING(), 256 );
		strncpy( szFile, READ_STRING(), 256 );
		ParseHintTokens( szName, szFile );

		gViewPort->m_pHUDControl_guess->m_iHintLine_guess = 1;

		if ( strlen( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess ) > 2 )
		{
			int iLen = 0;
			for ( unsigned int i = 0; i < strlen( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess ); i++ )
			{
				if ( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[i] == '@' )
				{
					iLen++;
					szText[i] = ' ';
				}
				else
				{
					iLen++;
					szText[i] = gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[i];
				}
			}
			szText[iLen] = '\0';

			int w, h;
			// ReCoF (B135): Label::setText is printf style; retail passed player names and chat text as the format
			gViewPort->m_pHUDControl_guess->m_pHintLabel_guess->setText( "%s", szText );
			gViewPort->m_pHUDControl_guess->m_pCreditsFont_guess->getTextSize( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess, w, h );
			gViewPort->m_pHUDControl_guess->m_pHintLabel_guess->setPos( ScreenWidth / 2 - 400, -48 );
			gViewPort->m_pHUDControl_guess->m_bHint_guess = true;
			gViewPort->m_pHUDControl_guess->m_iHintAlpha_guess = 255;
			gViewPort->m_pHUDControl_guess->m_flHintTime_guess = gEngfuncs.GetClientTime() + 5;
		}
	}

	return 1;
}

int MsgDevDonator_guess( const char *pszName, int iSize, void *pbuf )
{
	if ( gViewPort->m_pHUDControl_guess )
	{
		BEGIN_READ( pbuf, iSize );
		int iPlayer = READ_BYTE();
		gViewPort->m_pHUDControl_guess->m_iDevDonator_guess[iPlayer] = READ_BYTE();
	}

	return 1;
}

int MsgHoldBreath_guess( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	gHUD.m_iHoldBreath_guess = READ_BYTE();

	return 1;
}

int MsgIntrodn_guess( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	int iType = READ_BYTE();
	READ_BYTE();
	int iCoop = READ_BYTE();

	if ( !gViewPort || !gViewPort->m_pHUDControl_guess )
		return 1;

	int iLanguage;
	if ( iCoop )
		iLanguage = CVAR_GET_FLOAT( "cl_coop_language" );
	else
		iLanguage = CVAR_GET_FLOAT( "cof_subtitlelanguage" );

	if ( !gViewPort->m_pHUDControl_guess->m_pIntroduction_guess->isVisible() )
	{
		gViewPort->m_pHUDControl_guess->m_iIntroductionAlpha_guess = 255;
		gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess->setColor( Color( 255, 255, 255, 255 ) );
		gViewPort->m_pHUDControl_guess->m_bIntroductionFadeIn_guess = true;

		// JP: when the Japanese subtitle mode is on, load the Japanese
		// introduction cards (the stock switch has no case for us).
		if ( jp_poc && jp_poc->value )
		{
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/japanese/japanese_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/japanese/japanese_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/japanese/japanese_coop.TGA" );
		}
		else
		{
		switch ( iLanguage )
		{
		case 0:
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/english/english_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/english/english_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/english/english_coop.TGA" );
			break;
		case 2:
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/dutch/dutch_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/dutch/dutch_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/dutch/dutch_coop.TGA" );
			break;
		case 3:
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/french/french_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/french/french_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/french/french_coop.TGA" );
			break;
		case 4:
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/german/german_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/german/german_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/german/german_coop.TGA" );
			break;
		case 5:
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/norwegian/norwegian_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/norwegian/norwegian_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/norwegian/norwegian_coop.TGA" );
			break;
		case 6:
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/spanish/spanish_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/spanish/spanish_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/spanish/spanish_coop.TGA" );
			break;
		case 7:
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/swedish/swedish_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/swedish/swedish_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/swedish/swedish_coop.TGA" );
			break;
		default:
			if ( iType == 0 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/english/english_sp.TGA" );
			else if ( iType == 1 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/english/english_doc.TGA" );
			else if ( iType == 2 )
				gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/english/english_coop.TGA" );
			break;
		}
		}

		gViewPort->m_pHUDControl_guess->m_pIntroduction_guess->setImage( gViewPort->m_pHUDControl_guess->m_pIntroductionImage_guess );
		gViewPort->m_pHUDControl_guess->m_pIntroduction_guess->setVisible( true );
	}
	else
		gViewPort->m_pHUDControl_guess->m_bIntroductionFadeIn_guess = false;

	return 1;
}

int MsgLobby_guess( const char *pszName, int iSize, void *pbuf )
{
	if ( gViewPort->m_pHUDControl_guess )
	{
		BEGIN_READ( pbuf, iSize );
		int iPlayer = READ_BYTE();
		int iReady = READ_BYTE();
		int iLobby = READ_BYTE();
		int i;

		if ( iLobby )
		{
			gViewPort->m_pHUDControl_guess->m_bLobby_guess = false;
			gViewPort->m_pHUDControl_guess->m_pVoteLabel_guess->setVisible( false );
			gViewPort->m_pHUDControl_guess->m_pVoteKickLabel_guess->setVisible( false );
			for ( i = 0; i < 4; i++ )
				gViewPort->m_pHUDControl_guess->m_pLobbyLabel_guess[i]->setVisible( false );
			gViewPort->m_pHUDControl_guess->m_pLobbyBackground_guess->setVisible( false );
		}
		else
		{
			gViewPort->m_pHUDControl_guess->m_bLobby_guess = true;
			gViewPort->m_pHUDControl_guess->m_pVoteLabel_guess->setVisible( true );
			gViewPort->m_pHUDControl_guess->m_pVoteKickLabel_guess->setVisible( true );
			for ( i = 0; i < 4; i++ )
				gViewPort->m_pHUDControl_guess->m_pLobbyLabel_guess[i]->setVisible( true );
			gViewPort->m_pHUDControl_guess->m_pLobbyBackground_guess->setImage( vgui_LoadTGA( "gfx/vgui/lobby_background.tga" ) );
			gViewPort->m_pHUDControl_guess->m_pLobbyBackground_guess->setSize( 800, 600 );
			gViewPort->m_pHUDControl_guess->m_pLobbyBackground_guess->setPos( ScreenWidth / 2 - 400, ScreenHeight / 2 - 300 );
			gViewPort->m_pHUDControl_guess->m_pLobbyBackground_guess->setVisible( true );
		}

		int iIndex = iPlayer - 1;
		if ( iIndex >= 0 && iIndex < 4 )
		{
			if ( iReady )
				gViewPort->m_pHUDControl_guess->m_bPlayerReady_guess[iIndex] = true;
			else
				gViewPort->m_pHUDControl_guess->m_bPlayerReady_guess[iIndex] = false;
		}
	}

	return 1;
}

int MsgLookingAt_guess( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	g_flLookingAt_guess = READ_BYTE();

	return 1;
}

int MsgMPModel_guess( const char *pszName, int iSize, void *pbuf )
{
	g_bHandsLoaded_guess = false;

	return 1;
}

int MsgSimonMode_guess( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	g_iSimonMode_guess = READ_BYTE();

	return 1;
}

int MsgTimerMode_guess( const char *pszName, int iSize, void *pbuf )
{
	if ( gViewPort->m_pHUDControl_guess )
	{
		BEGIN_READ( pbuf, iSize );
		int iMode = READ_BYTE();
		int iSeconds = READ_BYTE();
		int iMinutes = READ_BYTE();
		int iHours = READ_BYTE();
		int iLimit = READ_SHORT();

		if ( iMode == 0 )
		{
			gViewPort->m_pHUDControl_guess->m_bTimer_guess = false;
			gViewPort->m_pHUDControl_guess->m_flTimerNextSecond_guess = gEngfuncs.GetClientTime();
			gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess = 0;
			gViewPort->m_pHUDControl_guess->m_iTimerMinutes_guess = 0;
			gViewPort->m_pHUDControl_guess->m_iTimerHours_guess = 0;
		}
		else if ( iMode == 1 )
		{
			gViewPort->m_pHUDControl_guess->m_bTimer_guess = true;
			gViewPort->m_pHUDControl_guess->m_flTimerNextSecond_guess = gEngfuncs.GetClientTime();
			gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess = iSeconds;
			gViewPort->m_pHUDControl_guess->m_iTimerMinutes_guess = iMinutes;
			gViewPort->m_pHUDControl_guess->m_iTimerHours_guess = iHours;
			gViewPort->m_pHUDControl_guess->m_iTimerLimit_guess = iLimit;
		}
		else if ( iMode == 2 )
		{
			gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess = iSeconds;
			gViewPort->m_pHUDControl_guess->m_iTimerMinutes_guess = iMinutes;
			gViewPort->m_pHUDControl_guess->m_iTimerHours_guess = iHours;
			gViewPort->m_pHUDControl_guess->m_iTimerLimit_guess = iLimit;
		}
	}

	return 1;
}

int MsgTimerPlus_guess( const char *pszName, int iSize, void *pbuf )
{
	char szSeconds[32];
	char szMinutes[32];
	char szHours[32];
	char szTime[128];

	BEGIN_READ( pbuf, iSize );
	int iSeconds = READ_BYTE();
	int iShow = READ_BYTE();
	int iMode = READ_BYTE();

	if ( gViewPort && gViewPort->m_pHUDControl_guess )
	{
		switch ( iMode )
		{
		case 1:
			gViewPort->m_pHUDControl_guess->m_bTimerPaused_guess = true;
			return 1;
		case 2:
			gViewPort->m_pHUDControl_guess->m_bTimerPaused_guess = false;
			break;
		case 3:
			gViewPort->m_pHUDControl_guess->m_bTimerPaused_guess = true;
			break;
		}

		gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess += iSeconds;

		if ( gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess > 59 )
		{
			gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess = ( gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess > 60 ) ? gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess - 60 : 0;
			gViewPort->m_pHUDControl_guess->m_iTimerMinutes_guess++;
		}

		if ( gViewPort->m_pHUDControl_guess->m_iTimerMinutes_guess > 59 )
		{
			gViewPort->m_pHUDControl_guess->m_iTimerHours_guess++;
			gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess = 0;
			gViewPort->m_pHUDControl_guess->m_iTimerMinutes_guess = 0;
		}

		int iSecondsLeft = 60 - gViewPort->m_pHUDControl_guess->m_iTimerSeconds_guess;
		int iMinutesLeft = gViewPort->m_pHUDControl_guess->m_iTimerLimit_guess / 60 - gViewPort->m_pHUDControl_guess->m_iTimerMinutes_guess - 1;

		if ( iMinutesLeft < 0 )
			iMinutesLeft = 0;
		if ( iSecondsLeft < 0 )
			iSecondsLeft = 0;
		if ( gViewPort->m_pHUDControl_guess->m_iTimerHours_guess < 0 )
			gViewPort->m_pHUDControl_guess->m_iTimerHours_guess = 0;

		sprintf( szSeconds, ( iSecondsLeft >= 10 ) ? "%i" : "0%i", iSecondsLeft );
		sprintf( szMinutes, ( iMinutesLeft >= 10 ) ? "%i" : "0%i", iMinutesLeft );
		sprintf( szHours, ( gViewPort->m_pHUDControl_guess->m_iTimerHours_guess >= 10 ) ? "%i" : "0%i", gViewPort->m_pHUDControl_guess->m_iTimerHours_guess );
		sprintf( szTime, "%s:%s:%s", szHours, szMinutes, szSeconds );
		gViewPort->m_pHUDControl_guess->m_pTimerLabel_guess->setText( szTime );
		gViewPort->m_pHUDControl_guess->m_flTimerNextSecond_guess = gEngfuncs.GetClientTime() + 1;
		gEngfuncs.pfnPlaySoundByName( "common/survival_timeplus.wav", 2 );

		if ( iShow )
		{
			for ( int i = 0; i < 32; i++ )
			{
				if ( gViewPort && !gViewPort->m_pHUDControl_guess->m_pTimerPlusLabel_guess[i]->isVisible() )
				{
					int w, h;
					gViewPort->m_pHUDControl_guess->m_pCreditsFont_guess->getTextSize( szTime, w, h );
					gViewPort->m_pHUDControl_guess->m_pTimerPlusLabel_guess[i]->setFgColor( 0, 128, 0, 0 );
					gViewPort->m_pHUDControl_guess->m_pTimerPlusLabel_guess[i]->setPos( ScreenWidth / 2 + w / 2, 0 );
					gViewPort->m_pHUDControl_guess->m_pTimerLabel_guess->setFgColor( 128, 255, 128, 0 );
					sprintf( szTime, "+%i", iSeconds );
					gViewPort->m_pHUDControl_guess->m_pTimerPlusLabel_guess[i]->setText( szTime );
					gViewPort->m_pHUDControl_guess->m_pTimerPlusLabel_guess[i]->setVisible( true );
					break;
				}
			}
		}
	}

	return 1;
}

void ParseHintTokens( char *pszName, char *pszFile )
{
	char szBefore1[256];
	char szBefore2[256];
	char szBefore3[256];
	char token[1024];
	char szAfter[256];
	bool bFound;
	int i, j;

	if ( !gViewPort->m_pHUDControl_guess )
		return;

	gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[0] = '\0';
	gViewPort->m_pHUDControl_guess->m_szHintLine2_guess[0] = '\0';
	gViewPort->m_pHUDControl_guess->m_szHintLine3_guess[0] = '\0';
	gViewPort->m_pHUDControl_guess->m_szHintNoButton_guess[0] = '\0';
	gViewPort->m_pHUDControl_guess->m_szHintCfgButton_guess[0] = '\0';

	char *pfile = (char *)gEngfuncs.COM_LoadFile( pszFile, 5, NULL );
	if ( !pfile )
	{
		gEngfuncs.Con_Printf( "ParseHintTokens Couldn't open file %s\n", pszFile );
		return;
	}

	while ( pfile = gEngfuncs.COM_ParseFile( pfile, token ) )
	{
		if ( _stricmp( token, pszName ) )
			continue;

		pfile = gEngfuncs.COM_ParseFile( pfile, token );
		if ( _stricmp( token, "{" ) )
		{
			gEngfuncs.Con_Printf( "ParseHintTokens Error parsing file %s. (expected { )\n", pszFile );
			return;
		}

		pfile = gEngfuncs.COM_ParseFile( pfile, token );
		while ( _stricmp( token, "}" ) )
		{
			if ( !_stricmp( token, "line1" ) )
			{
				pfile = gEngfuncs.COM_ParseFile( pfile, token );
				strncpy( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess, token, 256 );
			}
			else if ( !_stricmp( token, "line2" ) )
			{
				pfile = gEngfuncs.COM_ParseFile( pfile, token );
				strncpy( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess, token, 256 );
			}
			else if ( !_stricmp( token, "line3" ) )
			{
				pfile = gEngfuncs.COM_ParseFile( pfile, token );
				strncpy( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess, token, 256 );
			}
			else if ( !_stricmp( token, "nobutton" ) )
			{
				pfile = gEngfuncs.COM_ParseFile( pfile, token );
				strncpy( gViewPort->m_pHUDControl_guess->m_szHintNoButton_guess, token, 256 );
			}
			else if ( !_stricmp( token, "cfgbutton" ) )
			{
				pfile = gEngfuncs.COM_ParseFile( pfile, token );
				strncpy( gViewPort->m_pHUDControl_guess->m_szHintCfgButton_guess, token, 64 );
			}

			pfile = gEngfuncs.COM_ParseFile( pfile, token );
		}
	}

	// pfile is NULL here: the loaded file is never freed
	gEngfuncs.COM_FreeFile( pfile );

	// JP-MOD: translate the raw hint lines (the '*' key token is still
	// intact at this point; the substitution below then runs on the JP
	// text exactly as it would on the EN text).
	if ( jp_poc && jp_poc->value )
	{
		const char *pszJpLine;
		pszJpLine = JpLookupHintLine( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess );
		if ( pszJpLine )
		{
			strncpy( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess, pszJpLine, 255 );
			gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[255] = '\0';
		}
		pszJpLine = JpLookupHintLine( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess );
		if ( pszJpLine )
		{
			strncpy( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess, pszJpLine, 255 );
			gViewPort->m_pHUDControl_guess->m_szHintLine2_guess[255] = '\0';
		}
		pszJpLine = JpLookupHintLine( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess );
		if ( pszJpLine )
		{
			strncpy( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess, pszJpLine, 255 );
			gViewPort->m_pHUDControl_guess->m_szHintLine3_guess[255] = '\0';
		}
		JpLogHud( "HINT xlate: l1jp=%d l2jp=%d l3jp=%d l1=%.40s",
			JpTextIsUtf8Japanese( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess ) ? 1 : 0,
			JpTextIsUtf8Japanese( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess ) ? 1 : 0,
			JpTextIsUtf8Japanese( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess ) ? 1 : 0,
			gViewPort->m_pHUDControl_guess->m_szHintLine1_guess );
	}

	if ( strlen( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess ) > 2 )
	{
		bFound = false;
		j = 0;
		for ( i = 0; i < 256; i++ )
		{
			if ( !bFound )
			{
				if ( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[i] == '*' )
				{
					szBefore1[i] = '\0';
					bFound = true;
				}
				else
					szBefore1[i] = gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[i];
			}
			else
			{
				if ( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[i] != '\0' )
					szAfter[j++] = gViewPort->m_pHUDControl_guess->m_szHintLine1_guess[i];
				else
					szAfter[j++] = '\0';
			}
		}

		strcpy( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess, szBefore1 );

		if ( bFound )
		{
			if ( gHUD.GetKeyForCommand_guess( gViewPort->m_pHUDControl_guess->m_szHintCfgButton_guess ) )
				strcat( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess, gHUD.GetKeyForCommand_guess( gViewPort->m_pHUDControl_guess->m_szHintCfgButton_guess ) );
			else
				strcat( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess, gViewPort->m_pHUDControl_guess->m_szHintNoButton_guess );
			strcat( gViewPort->m_pHUDControl_guess->m_szHintLine1_guess, szAfter );
		}
	}

	if ( strlen( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess ) > 2 )
	{
		bFound = false;
		j = 0;
		for ( i = 0; i < 256; i++ )
		{
			if ( !bFound )
			{
				if ( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess[i] == '*' )
				{
					szBefore2[i] = '\0';
					bFound = true;
				}
				else
					szBefore2[i] = gViewPort->m_pHUDControl_guess->m_szHintLine2_guess[i];
			}
			else
			{
				if ( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess[i] != '\0' )
					szAfter[j++] = gViewPort->m_pHUDControl_guess->m_szHintLine2_guess[i];
				else
					szAfter[j++] = '\0';
			}
		}

		strcpy( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess, szBefore2 );

		if ( bFound )
		{
			if ( gHUD.GetKeyForCommand_guess( gViewPort->m_pHUDControl_guess->m_szHintCfgButton_guess ) )
				strcat( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess, gHUD.GetKeyForCommand_guess( gViewPort->m_pHUDControl_guess->m_szHintCfgButton_guess ) );
			else
				strcat( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess, gViewPort->m_pHUDControl_guess->m_szHintNoButton_guess );
			strcat( gViewPort->m_pHUDControl_guess->m_szHintLine2_guess, szAfter );
		}
	}

	if ( strlen( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess ) > 2 )
	{
		bFound = false;
		j = 0;
		for ( i = 0; i < 256; i++ )
		{
			if ( !bFound )
			{
				if ( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess[i] == '*' )
				{
					szBefore3[i] = '\0';
					bFound = true;
				}
				else
					szBefore3[i] = gViewPort->m_pHUDControl_guess->m_szHintLine3_guess[i];
			}
			else
			{
				if ( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess[i] != '\0' )
					szAfter[j++] = gViewPort->m_pHUDControl_guess->m_szHintLine3_guess[i];
				else
					szAfter[j++] = '\0';
			}
		}

		strcpy( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess, szBefore3 );

		if ( bFound )
		{
			if ( gHUD.GetKeyForCommand_guess( gViewPort->m_pHUDControl_guess->m_szHintCfgButton_guess ) )
				strcat( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess, gHUD.GetKeyForCommand_guess( gViewPort->m_pHUDControl_guess->m_szHintCfgButton_guess ) );
			else
				strcat( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess, gViewPort->m_pHUDControl_guess->m_szHintNoButton_guess );
			strcat( gViewPort->m_pHUDControl_guess->m_szHintLine3_guess, szAfter );
		}
	}
}

void HUDControlInit_guess()
{
	gEngfuncs.pfnHookUserMsg( "Stamina", MsgStamina_guess );
	gEngfuncs.pfnHookUserMsg( "CoFLogo", MsgCoFLogo_guess );
	gEngfuncs.pfnHookUserMsg( "ShowHint", MsgShowHint_guess );
	gEngfuncs.pfnHookUserMsg( "CameraZoom", MsgCameraZoom_guess );
	gEngfuncs.pfnHookUserMsg( "HealthCap", MsgHealthCap_guess );
	gEngfuncs.pfnHookUserMsg( "Chapter", MsgChapter_guess );
	gEngfuncs.pfnHookUserMsg( "ControlHint", MsgControlHint_guess );
	gEngfuncs.pfnHookUserMsg( "Timer", MsgTimerMode_guess );
	gEngfuncs.pfnHookUserMsg( "Lobby", MsgLobby_guess );
	gEngfuncs.pfnHookUserMsg( "MPModel", MsgMPModel_guess );
	gEngfuncs.pfnHookUserMsg( "HoldBreath", MsgHoldBreath_guess );
	gEngfuncs.pfnHookUserMsg( "DevDonator", MsgDevDonator_guess );
	gEngfuncs.pfnHookUserMsg( "Introdn", MsgIntrodn_guess );
	gEngfuncs.pfnHookUserMsg( "LookingAt", MsgLookingAt_guess );
	gEngfuncs.pfnHookUserMsg( "TimerPlus", MsgTimerPlus_guess );
	gEngfuncs.pfnHookUserMsg( "SimonMode", MsgSimonMode_guess );
}

CHUDControl::CHUDControl() : Panel( 0, 0, ScreenWidth, ScreenHeight )
{
	int i;

	m_pLobbyBackground_guess = new ImagePanel( vgui_LoadTGA( "gfx/vgui/vignette/vignette_tl.tga" ) );
	m_pLobbyBackground_guess->setParent( this );
	m_pLobbyBackground_guess->setPos( ScreenWidth / 2 - 400, ScreenHeight / 2 - 300 );
	m_pLobbyBackground_guess->setVisible( true );

	m_pVignetteTL_guess = new ImagePanel( vgui_LoadTGA( "gfx/vgui/vignette/vignette_tl.tga" ) );
	m_pVignetteTL_guess->setParent( this );
	m_pVignetteTL_guess->setPos( 0, 0 );
	m_pVignetteTL_guess->setVisible( false );

	m_pVignetteTR_guess = new ImagePanel( vgui_LoadTGA( "gfx/vgui/vignette/vignette_tr.tga" ) );
	m_pVignetteTR_guess->setParent( this );
	m_pVignetteTR_guess->setPos( ScreenWidth - 256, 0 );
	m_pVignetteTR_guess->setVisible( false );

	m_pVignetteBL_guess = new ImagePanel( vgui_LoadTGA( "gfx/vgui/vignette/vignette_bl.tga" ) );
	m_pVignetteBL_guess->setParent( this );
	m_pVignetteBL_guess->setPos( 0, ScreenHeight - 256 );
	m_pVignetteBL_guess->setVisible( false );

	m_pVignetteBR_guess = new ImagePanel( vgui_LoadTGA( "gfx/vgui/vignette/vignette_br.tga" ) );
	m_pVignetteBR_guess->setParent( this );
	m_pVignetteBR_guess->setPos( ScreenWidth - 256, ScreenHeight - 256 );
	m_pVignetteBR_guess->setVisible( false );

	if ( m_pSMSIconImage_guess = vgui_LoadTGA( "gfx/vgui/640_hudelem_smsicon.tga" ) )
		m_pSMSIconImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_hudelem_smsicon.tga!\n" );

	if ( m_pInventoryHintImage_guess = vgui_LoadTGA( "gfx/vgui/hints/hint_inventory.tga" ) )
		m_pInventoryHintImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/hints/hint_inventory.tga!\n" );

	if ( m_pCrosshairImage_guess = vgui_LoadTGA( "gfx/vgui/crosshair.tga" ) )
		m_pCrosshairImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/crosshair.tga!\n" );

	if ( m_pLogoImage_guess = vgui_LoadTGA( "gfx/vgui/640_logo_logo.tga" ) )
		m_pLogoImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_logo_logo.tga!\n" );

	if ( m_pHealthBarImage_guess = vgui_LoadTGA( "gfx/vgui/640_healthbar.tga" ) )
		m_pHealthBarImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_healthbar.tga!\n" );

	CSchemeManager *pSchemes = gViewPort->GetSchemeManager();
	SchemeHandle_t hCreditsScheme = pSchemes->getSchemeHandle( "credits" );
	SchemeHandle_t hChapterTitleScheme = pSchemes->getSchemeHandle( "chaptertitle" );
	SchemeHandle_t hPagerScheme = pSchemes->getSchemeHandle( "pager" );
	Font *pFont = pSchemes->getFont( hCreditsScheme );
	m_pPagerFont_guess = pSchemes->getFont( hPagerScheme );
	m_pCreditsFont_guess = pSchemes->getFont( hCreditsScheme );
	m_pChapterTitleFont_guess = pSchemes->getFont( hChapterTitleScheme );

	if ( m_pHealthBarFullImage_guess = vgui_LoadTGA( "gfx/vgui/640_healthbar_bar.tga" ) )
		m_pHealthBarFullImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_healthbar_bar.tga!\n" );

	if ( m_pHealthBarEmptyImage_guess = vgui_LoadTGA( "gfx/vgui/640_healthbar_barempty.tga" ) )
		m_pHealthBarEmptyImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_healthbar_barempty.tga!\n" );

	if ( m_pStaminaBarImage_guess = vgui_LoadTGA( "gfx/vgui/640_staminabar.tga" ) )
		m_pStaminaBarImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_staminabar.tga!\n" );

	if ( m_pStaminaBarFullImage_guess = vgui_LoadTGA( "gfx/vgui/640_staminabar_bar.tga" ) )
		m_pStaminaBarFullImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_staminabar_bar.tga!\n" );

	if ( m_pStaminaBarEmptyImage_guess = vgui_LoadTGA( "gfx/vgui/640_staminabar_barempty.tga" ) )
		m_pStaminaBarEmptyImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_staminabar_barempty.tga!\n" );

	if ( m_pHintBarImage_guess = vgui_LoadTGA( "gfx/vgui/640_hint_bar.tga" ) )
		m_pHintBarImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/640_hint_bar.tga!\n" );

	if ( m_pIntroductionImage_guess = vgui_LoadTGA( "gfx/vgui/introductions/english/english_sp.tga" ) )
		m_pIntroductionImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/introductions/english/english_sp.tga!\n" );

	for ( i = 0; i < 4; i++ )
	{
		if ( m_pFollowIcon_guess[i] = vgui_LoadTGA( "gfx/vgui/coop_follow_icon.tga" ) )
			m_pFollowIcon_guess[i]->setColor( Color( 255, 255, 255, 1 ) );
		else
			gEngfuncs.Con_Printf( "Cannot load gfx/vgui/coop_follow_icon.tga %i!\n", i );

		if ( m_pSyringeIcon_guess[i] = vgui_LoadTGA( "gfx/vgui/coop_syringe_icon.tga" ) )
			m_pSyringeIcon_guess[i]->setColor( Color( 255, 255, 255, 1 ) );
		else
			gEngfuncs.Con_Printf( "Cannot load gfx/vgui/coop_syringe_icon.tga %i!\n", i );
	}

	setBgColor( 0, 0, 0, 255 );

	m_pIntroduction_guess = new ImagePanel( m_pIntroductionImage_guess );
	m_pIntroduction_guess->setParent( this );
	m_pIntroduction_guess->setPos( ScreenWidth / 2 - 256, ScreenHeight / 2 - 256 );
	m_pIntroduction_guess->setVisible( false );
	m_pIntroductionImage_guess->setColor( Color( 255, 255, 255, m_iIntroductionAlpha_guess ) );

	m_pSMSIcon_guess = new ImagePanel( m_pSMSIconImage_guess );
	m_pSMSIcon_guess->setParent( this );
	m_pSMSIcon_guess->setPos( m_pSMSIcon_guess->getWide(), m_pSMSIcon_guess->getTall() );
	m_pSMSIcon_guess->setVisible( false );

	m_pInventoryHint_guess = new ImagePanel( m_pInventoryHintImage_guess );
	m_pInventoryHint_guess->setParent( this );
	m_pInventoryHint_guess->setPos( m_pInventoryHint_guess->getWide() * 1.2 - m_pInventoryHint_guess->getWide(), m_iInventoryHintY_guess );
	m_pInventoryHint_guess->setVisible( false );

	m_pLogo_guess = new ImagePanel( m_pLogoImage_guess );
	m_pLogo_guess->setParent( this );
	int x = ScreenWidth / 2 - m_pLogo_guess->getWide() / 2;
	int y = ScreenHeight / 2 - m_pLogo_guess->getTall() / 2;
	m_pLogo_guess->setPos( x, y );
	m_pLogoImage_guess->setColor( Color( 255, 255, 255, 255 ) );
	m_pLogo_guess->setVisible( false );

	m_pOverlay_guess = new Panel( 0, 0, ScreenWidth, ScreenHeight );
	m_pOverlay_guess->setParent( this );
	m_pOverlay_guess->setSize( ScreenWidth, ScreenHeight );
	m_pOverlay_guess->setPos( 0, 0 );
	m_pOverlay_guess->setBgColor( 33, 177, 120, 128 );
	m_pOverlay_guess->setVisible( false );

	m_pHealthBar_guess = new ImagePanel( m_pHealthBarImage_guess );
	m_pHealthBar_guess->setParent( this );
	m_pHealthBar_guess->setPos( m_pHealthBar_guess->getWide(), ScreenHeight - m_pHealthBar_guess->getTall() );
	m_pHealthBar_guess->setVisible( true );

	m_pHealthBarFull_guess = new ImagePanel( m_pHealthBarFullImage_guess );
	m_pHealthBarFull_guess->setParent( m_pHealthBar_guess );
	m_pHealthBarFull_guess->setPos( 11, 5 );
	m_pHealthBarFull_guess->setVisible( true );

	m_pHealthBarEmpty_guess = new ImagePanel( m_pHealthBarEmptyImage_guess );
	m_pHealthBarEmpty_guess->setParent( m_pHealthBar_guess );
	m_pHealthBarEmpty_guess->setPos( 11, 5 );
	m_pHealthBarEmpty_guess->setVisible( true );

	m_pStaminaBar_guess = new ImagePanel( m_pStaminaBarImage_guess );
	m_pStaminaBar_guess->setParent( this );
	m_pStaminaBar_guess->setPos( m_pStaminaBar_guess->getWide() * 2, ScreenHeight - m_pStaminaBar_guess->getTall() );
	m_pStaminaBar_guess->setVisible( true );

	m_pStaminaBarFull_guess = new ImagePanel( m_pStaminaBarFullImage_guess );
	m_pStaminaBarFull_guess->setParent( m_pStaminaBar_guess );
	m_pStaminaBarFull_guess->setPos( 11, 5 );
	m_pStaminaBarFull_guess->setVisible( true );

	m_pStaminaBarEmpty_guess = new ImagePanel( m_pStaminaBarEmptyImage_guess );
	m_pStaminaBarEmpty_guess->setParent( m_pStaminaBar_guess );
	m_pStaminaBarEmpty_guess->setPos( 11, 5 );
	m_pStaminaBarEmpty_guess->setVisible( true );

	int wide, tall;
	m_pStaminaBarEmptyImage_guess->getSize( wide, tall );

	m_pStaminaFlash_guess = new Panel( 255, 0, 0, 255 );
	m_pStaminaFlash_guess->setParent( m_pStaminaBarFull_guess );
	m_pStaminaFlash_guess->setSize( wide, tall );
	m_pStaminaFlash_guess->setPos( 0, 0 );
	m_pStaminaFlash_guess->setBgColor( 255, 0, 0, 255 );
	m_pStaminaFlash_guess->setVisible( true );

	m_pBlackScreen_guess = new Panel( 0, 0, 0, 0 );
	m_pBlackScreen_guess->setParent( this );
	m_pBlackScreen_guess->setSize( ScreenWidth, ScreenHeight );
	m_pBlackScreen_guess->setBgColor( 0, 0, 0, 255 );
	m_pBlackScreen_guess->setVisible( false );

	m_pVoteLabel_guess = new Label( " ", ScreenWidth / 2 - 128, ScreenHeight / 2, 256, 32 );
	m_pVoteLabel_guess->setParent( this );
	m_pVoteLabel_guess->setFont( m_pPagerFont_guess );
	m_pVoteLabel_guess->setFgColor( 255, 255, 255, 0 );
	m_pVoteLabel_guess->setBgColor( 0, 0, 0, 255 );
	m_pVoteLabel_guess->setText( "Press Vote button when ready" );
	m_pVoteLabel_guess->setContentAlignment( Label::a_center );
	m_pVoteLabel_guess->setVisible( false );

	for ( i = 0; i < 4; i++ )
	{
		m_pLobbyLabel_guess[i] = new Label( " ", ScreenWidth / 2 - 512 + i * 256, ScreenHeight / 2 + 32, 256, 32 );
		m_pLobbyLabel_guess[i]->setParent( m_pLobbyBackground_guess );
		m_pLobbyLabel_guess[i]->setFont( m_pPagerFont_guess );
		m_pLobbyLabel_guess[i]->setFgColor( 255, 255, 255, 0 );
		m_pLobbyLabel_guess[i]->setBgColor( 0, 0, 0, 255 );
		m_pLobbyLabel_guess[i]->setText( "Gay" );
		m_pLobbyLabel_guess[i]->setContentAlignment( Label::a_center );
		m_pLobbyLabel_guess[i]->setVisible( false );
	}

	m_pLobbyLabel_guess[0]->setPos( 116, 257 );
	m_pLobbyLabel_guess[1]->setPos( 116, 440 );
	m_pLobbyLabel_guess[2]->setPos( 429, 257 );
	m_pLobbyLabel_guess[3]->setPos( 429, 440 );

	m_pVoteKickLabel_guess = new Label( " ", ScreenWidth / 2 - 128, ScreenHeight / 2 + 64, 256, 32 );
	m_pVoteKickLabel_guess->setParent( this );
	m_pVoteKickLabel_guess->setFont( m_pPagerFont_guess );
	m_pVoteKickLabel_guess->setFgColor( 255, 255, 255, 0 );
	m_pVoteKickLabel_guess->setBgColor( 0, 0, 0, 255 );
	m_pVoteKickLabel_guess->setText( "Type votekick name in chat to votekick a player" );
	m_pVoteKickLabel_guess->setContentAlignment( Label::a_center );
	m_pVoteKickLabel_guess->setVisible( false );

	m_pVoteLabel_guess->setParent( m_pLobbyBackground_guess );
	m_pVoteLabel_guess->setPos( 272, 492 );
	m_pVoteKickLabel_guess->setParent( m_pLobbyBackground_guess );
	m_pVoteKickLabel_guess->setPos( 272, 524 );

	m_pTimerLabel_guess = new Label( " ", 0, 0, ScreenWidth, 64 );
	m_pTimerLabel_guess->setParent( this );
	m_pTimerLabel_guess->setFont( m_pCreditsFont_guess );
	m_pTimerLabel_guess->setFgColor( 255, 255, 255, 0 );
	m_pTimerLabel_guess->setBgColor( 0, 0, 0, 255 );
	m_pTimerLabel_guess->setText( "00:00:00" );
	m_pTimerLabel_guess->setContentAlignment( Label::a_center );
	m_pTimerLabel_guess->setVisible( false );

	for ( i = 0; i < 32; i++ )
	{
		m_pTimerPlusLabel_guess[i] = new Label( " ", 0, 0, 64, 128 );
		m_pTimerPlusLabel_guess[i]->setParent( this );
		m_pTimerPlusLabel_guess[i]->setFont( m_pChapterTitleFont_guess );
		m_pTimerPlusLabel_guess[i]->setFgColor( 0, 128, 0, 0 );
		m_pTimerPlusLabel_guess[i]->setBgColor( 0, 0, 0, 255 );
		m_pTimerPlusLabel_guess[i]->setText( "+0" );
		m_pTimerPlusLabel_guess[i]->setContentAlignment( Label::a_northwest );
		m_pTimerPlusLabel_guess[i]->setVisible( false );
	}

	m_pMessageLabel2_guess = new Label( " ", 0, ScreenHeight / 2, ScreenWidth, 64 );
	m_pMessageLabel2_guess->setParent( this );
	m_pMessageLabel2_guess->setFont( m_pChapterTitleFont_guess );
	m_pMessageLabel2_guess->setFgColor( 0, 0, 0, 0 );
	m_pMessageLabel2_guess->setBgColor( 0, 0, 0, 255 );
	m_pMessageLabel2_guess->setText( " " );
	m_pMessageLabel2_guess->setContentAlignment( Label::a_center );

	m_pLookingAtLabel_guess = new Label( " ", ScreenWidth / 2 - 128, ScreenHeight / 2 - 40, 256, 32 );
	m_pLookingAtLabel_guess->setParent( this );
	m_pLookingAtLabel_guess->setFont( m_pPagerFont_guess );
	m_pLookingAtLabel_guess->setFgColor( 255, 255, 255, 0 );
	m_pLookingAtLabel_guess->setBgColor( 255, 255, 255, 255 );
	m_pLookingAtLabel_guess->setText( " " );
	m_pLookingAtLabel_guess->setContentAlignment( Label::a_center );

	int iBarX, iBarY;
	m_pStaminaBar_guess->getPos( iBarX, iBarY );

	m_pDeathLabel_guess = new Label( "", ScreenWidth - 133, 5, 128, 40 );
	m_pDeathLabel_guess->setParent( this );
	m_pDeathLabel_guess->setFont( m_pPagerFont_guess );
	m_pDeathLabel_guess->setFgColor( 255, 0, 0, 255 );
	m_pDeathLabel_guess->setBgColor( 255, 255, 255, 255 );
	m_pDeathLabel_guess->setText( "" );
	m_pDeathLabel_guess->setContentAlignment( Label::a_east );

	for ( i = 0; i < 4; i++ )
	{
		m_pPlayerLabel_guess[i] = new Label( "", ScreenWidth - 133, ScreenHeight / 1.5 + i * 48, 128, 48 );
		m_pPlayerLabel_guess[i]->setParent( this );
		m_pPlayerLabel_guess[i]->setFont( m_pPagerFont_guess );
		m_pPlayerLabel_guess[i]->setFgColor( 255, 255, 255, 0 );
		m_pPlayerLabel_guess[i]->setBgColor( 255, 255, 255, 255 );
		m_pPlayerLabel_guess[i]->setText( "" );
		m_pPlayerLabel_guess[i]->setContentAlignment( Label::a_east );
		m_pPlayerLabel_guess[i]->setVisible( false );

		m_pFollowIconPanel_guess[i] = new ImagePanel( m_pFollowIcon_guess[i] );
		m_pFollowIconPanel_guess[i]->setParent( this );
		m_pFollowIconPanel_guess[i]->setPos( ScreenWidth - 261, ScreenHeight / 1.5 + i * 48 );
		m_pFollowIconPanel_guess[i]->setVisible( false );

		m_pSyringeIconPanel_guess[i] = new ImagePanel( m_pSyringeIcon_guess[i] );
		m_pSyringeIconPanel_guess[i]->setParent( this );
		m_pSyringeIconPanel_guess[i]->setPos( ScreenWidth - 133, ScreenHeight / 1.5 + i * 48 );
		m_pSyringeIconPanel_guess[i]->setVisible( false );
	}

	for ( i = 0; i < 5; i++ )
	{
		if ( !m_iSayTextY_guess )
			m_iSayTextY_guess = ScreenHeight / 2;
		if ( !m_iSayTextX_guess )
			m_iSayTextX_guess = 5;

		int w, h;
		m_pPagerFont_guess->getTextSize( "ysizetestg", w, h );

		m_pSayTextLabel_guess[i] = new Label( "", m_iSayTextX_guess, m_iSayTextY_guess + i * 32, ScreenWidth, 32 );
		m_pSayTextLabel_guess[i]->setParent( this );
		m_pSayTextLabel_guess[i]->setFont( m_pPagerFont_guess );
		m_pSayTextLabel_guess[i]->setFgColor( 255, 255, 255, 0 );
		m_pSayTextLabel_guess[i]->setBgColor( 0, 0, 0, 255 );
		m_pSayTextLabel_guess[i]->setText( "" );
		m_pSayTextLabel_guess[i]->setContentAlignment( Label::a_west );
		m_pSayTextLabel_guess[i]->setVisible( true );
	}

	g_iLetterboxHeight_guess = ScreenHeight / 480.0f * 25.0f + 0.5f;

	m_pLetterboxTop_guess = new Panel( 0, 0, ScreenWidth, g_iLetterboxHeight_guess );
	m_pLetterboxTop_guess->setParent( this );
	m_pLetterboxTop_guess->setSize( ScreenWidth, g_iLetterboxHeight_guess );
	m_pLetterboxTop_guess->setBgColor( 0, 0, 0, 0 );
	m_pLetterboxTop_guess->setVisible( true );

	m_pLetterboxBottom_guess = new Panel( 0, ScreenHeight - g_iLetterboxHeight_guess, ScreenWidth, g_iLetterboxHeight_guess );
	m_pLetterboxBottom_guess->setParent( this );
	m_pLetterboxBottom_guess->setSize( ScreenWidth, g_iLetterboxHeight_guess );
	m_pLetterboxBottom_guess->setBgColor( 0, 0, 0, 0 );
	m_pLetterboxBottom_guess->setVisible( true );

	m_pMessageLabel_guess = new Label( " ", 0, 0, ScreenWidth, 64 );
	m_pMessageLabel_guess->setParent( this );
	m_pMessageLabel_guess->setFont( m_pCreditsFont_guess );
	m_pMessageLabel_guess->setFgColor( 0, 0, 0, 0 );
	m_pMessageLabel_guess->setBgColor( 0, 0, 0, 255 );
	m_pMessageLabel_guess->setText( " " );
	m_pMessageLabel_guess->setContentAlignment( Label::a_center );

	// the x position uses ScreenHeight, the ControlHint message corrects it with ScreenWidth
	m_pHintBar_guess = new ImagePanel( m_pHintBarImage_guess );
	m_pHintBar_guess->setParent( this );
	m_pHintBar_guess->setPos( ScreenHeight / 2 - 400, -48 );
	m_pHintBar_guess->setVisible( false );

	m_pHintLabel_guess = new JpHintLabel( " ", ScreenHeight / 2 - 400, -48, 800, 48 );
	m_pHintLabel_guess->setParent( this );
	m_pHintLabel_guess->setFont( m_pCreditsFont_guess );
	m_pHintLabel_guess->setFgColor( 0, 0, 0, 0 );
	m_pHintLabel_guess->setBgColor( 0, 0, 0, 255 );
	m_pHintLabel_guess->setText( " " );
	m_pHintLabel_guess->setContentAlignment( Label::a_center );

	Initialize();
}

CHUDControl::~CHUDControl()
{
	delete m_pSMSIconImage_guess;
	delete m_pInventoryHintImage_guess;
	delete m_pCrosshairImage_guess;
	delete m_pHealthBarImage_guess;
	delete m_pHealthBarFullImage_guess;
	delete m_pHealthBarEmptyImage_guess;
	delete m_pStaminaBarImage_guess;
	delete m_pStaminaBarFullImage_guess;
	delete m_pStaminaBarEmptyImage_guess;
	delete m_pLogoImage_guess;
	delete m_pHintBarImage_guess;
	delete m_pIntroductionImage_guess;
}

void CHUDControl::HandleHUDVisibility_guess()
{
	int i;

	if ( !strcmp( gEngfuncs.pfnGetLevelName(), "maps/c_nightmare.bsp" ) )
	{
		if ( m_pSMSIcon_guess->isVisible() )
			m_pSMSIcon_guess->setVisible( false );
		if ( m_pInventoryHint_guess->isVisible() )
			m_pInventoryHint_guess->setVisible( false );
		if ( m_pHealthBar_guess->isVisible() )
			m_pHealthBar_guess->setVisible( false );
		if ( m_pHealthBarFull_guess->isVisible() )
			m_pHealthBarFull_guess->setVisible( false );
		if ( m_pHealthBarEmpty_guess->isVisible() )
			m_pHealthBarEmpty_guess->setVisible( false );
		if ( m_pStaminaBar_guess->isVisible() )
			m_pStaminaBar_guess->setVisible( false );
		if ( m_pStaminaBarFull_guess->isVisible() )
			m_pStaminaBarFull_guess->setVisible( false );
		if ( m_pStaminaBarEmpty_guess->isVisible() )
			m_pStaminaBarEmpty_guess->setVisible( false );
		return;
	}

	if ( !gHUD.m_pCvarDraw->value || ( gHUD.m_iHideHUDDisplay & ( HIDEHUD_ALL | HIDEHUD_HEALTH ) ) )
	{
		if ( m_pSMSIcon_guess->isVisible() )
			m_pSMSIcon_guess->setVisible( false );
		if ( m_pInventoryHint_guess->isVisible() )
			m_pInventoryHint_guess->setVisible( false );
		if ( m_pTimerLabel_guess->isVisible() )
			m_pTimerLabel_guess->setVisible( false );
		for ( i = 0; i < 32; i++ )
		{
			if ( m_pTimerPlusLabel_guess[i]->isVisible() )
				m_pTimerPlusLabel_guess[i]->setVisible( false );
		}
		if ( m_pOverlay_guess->isVisible() )
			m_pOverlay_guess->setVisible( false );
		if ( m_pHealthBar_guess->isVisible() )
			m_pHealthBar_guess->setVisible( false );
		if ( m_pHealthBarFull_guess->isVisible() )
			m_pHealthBarFull_guess->setVisible( false );
		if ( m_pHealthBarEmpty_guess->isVisible() )
			m_pHealthBarEmpty_guess->setVisible( false );
		if ( m_pStaminaBar_guess->isVisible() )
			m_pStaminaBar_guess->setVisible( false );
		if ( m_pStaminaBarFull_guess->isVisible() )
			m_pStaminaBarFull_guess->setVisible( false );
		if ( m_pStaminaBarEmpty_guess->isVisible() )
			m_pStaminaBarEmpty_guess->setVisible( false );
	}

	if ( !gHUD.m_iHideHUDDisplay && gHUD.m_pCvarDraw->value )
	{
		if ( !m_pSMSIcon_guess->isVisible() && m_bSMSIcon_guess )
			m_pSMSIcon_guess->setVisible( true );
		if ( !m_pInventoryHint_guess->isVisible() && m_bInventoryHint_guess )
			m_pInventoryHint_guess->setVisible( true );
		if ( !m_pHealthBar_guess->isVisible() )
			m_pHealthBar_guess->setVisible( true );
		if ( !m_pHealthBarFull_guess->isVisible() )
			m_pHealthBarFull_guess->setVisible( true );
		if ( !m_pHealthBarEmpty_guess->isVisible() )
			m_pHealthBarEmpty_guess->setVisible( true );
		if ( !m_pStaminaBar_guess->isVisible() )
			m_pStaminaBar_guess->setVisible( true );
		if ( !m_pStaminaBarFull_guess->isVisible() )
			m_pStaminaBarFull_guess->setVisible( true );
		if ( !m_pStaminaBarEmpty_guess->isVisible() )
			m_pStaminaBarEmpty_guess->setVisible( true );
	}
}

void CHUDControl::Initialize()
{
	int i;

	m_bTimerPaused_guess = false;
	g_iSimonMode_guess = 0;
	gHUD.m_szSleeveTexture_guess[0] = '\0';
	gHUD.m_szFingerTexture_guess[0] = '\0';
	gHUD.m_szGloveTexture_guess[0] = '\0';

	m_pTimerLabel_guess->setFgColor( 128, 255, 128, 0 );

	if ( m_bTimer_guess || ( m_pTimerLabel_guess && m_pTimerLabel_guess->isVisible() ) )
	{
		if ( m_pTimerLabel_guess )
		{
			m_bTimer_guess = false;
			m_pTimerLabel_guess->setVisible( false );
			for ( i = 0; i < 32; i++ )
			{
				if ( m_pTimerPlusLabel_guess[i]->isVisible() )
					m_pTimerPlusLabel_guess[i]->setVisible( false );
			}
		}
	}

	m_iTimerLimit_guess = 0;
	m_iTimerSeconds_guess = 0;
	m_iTimerMinutes_guess = 0;
	m_iTimerHours_guess = 0;
	m_iIntroductionAlpha_guess = 255;
	m_bIntroductionFadeIn_guess = false;
	m_pIntroduction_guess->setVisible( false );
	m_pLobbyBackground_guess->setVisible( false );
	m_bLobby_guess = false;

	for ( i = 0; i <= MAX_PLAYERS; i++ )
	{
		gHUD.m_Health.m_iAdLibState_guess[i] = 0;
		gHUD.m_Health.m_bSyringe_guess[i] = false;
		gHUD.m_Health.m_flAdLibTime_guess[i] = gEngfuncs.GetClientTime();
	}

	for ( i = 0; i < 4; i++ )
	{
		m_bPlayerReady_guess[i] = false;
		m_pFollowIconPanel_guess[i]->setVisible( false );
		m_pSyringeIconPanel_guess[i]->setVisible( false );
	}

	m_pLookingAtLabel_guess->setText( "" );
	m_pLookingAtLabel_guess->setVisible( false );
	m_flLookingAtTime_guess = gEngfuncs.GetClientTime();
	m_flTimerPlusTime_guess = gEngfuncs.GetClientTime();
	m_flTimerNextSecond_guess = gEngfuncs.GetClientTime();
	m_pDeathLabel_guess->setText( "" );
	m_iDeathAlpha_guess = 255;
	m_bLogoF5C_guess = false;
	m_iLogo_guess = 0;
	setVisible( true );

	m_flSMSIconTime_guess = gEngfuncs.GetClientTime();
	m_iSMSIconAlpha_guess = 255;
	m_pSMSIconImage_guess->setColor( Color( 255, 255, 255, 255 ) );
	ShowSMSIcon_guess( false );
	m_pLogo_guess->setVisible( false );
	m_bSMSIcon_guess = false;

	m_iMessageType_guess = 5;
	m_iChapterType_guess = 5;
	m_flStaminaFlashTime_guess = gEngfuncs.GetClientTime();
	m_szHintLine1_guess[0] = '\0';
	m_szHintLine2_guess[0] = '\0';
	m_szHintLine3_guess[0] = '\0';
	m_szHintNoButton_guess[0] = '\0';
	m_iLogoAlpha_guess = 255;
	m_bShowLogo_guess = false;
	m_szMessage2_guess[0] = '\0';
	m_flHintSlideInTime_guess = -1;
	m_flHintSlideInLength_guess = -1;
	m_flHintSlideOutTime_guess = -1;
	m_flHintSlideOutLength_guess = -1;
	m_flLogoTime_guess = 0;
	m_iMessageType_guess = 0;
	m_iMessageTime_guess = 0;
	m_iMessageC4_guess = 0;
	m_iMessageC8_guess = 0;
	m_iMessageCC_guess = 0;
	m_iMessageD0_guess = 0;
	m_szMessage_guess[0] = '\0';
	m_flZoomLength_guess = 0;
	m_flZoomTime_guess = 0;
	m_flZoomFOV_guess = 0;

	for ( i = 0; i < 5; i++ )
		m_pSayTextLabel_guess[i]->setText( "" );

	m_pMessageLabel2_guess->setText( "" );
	m_pMessageLabel_guess->setText( "" );
	m_pHintLabel_guess->setText( "" );

	m_bHint_guess = false;
	m_iHintAlpha_guess = 255;
	m_szChapter_guess[0] = '\0';
	m_flMessageAlpha_guess = 0;
	m_bMessageFadeOut_guess = false;
	m_bMessageHold_guess = false;
	m_flMessageHoldTime_guess = 0;
	m_flMessageF24_guess = 0;
	m_iMessageChars_guess = 0;
	m_iChapterType_guess = 0;
	m_flChapterAlpha_guess = 0;
	m_bChapterFadeOut_guess = false;
	m_bChapterF1E_guess = false;
	m_flChapterHoldTime_guess = 0;
	m_flChapterNextChar_guess = 0;
	m_iChapterChars_guess = 0;

	m_pInventoryHint_guess->setVisible( false );
	m_bInventoryHint_guess = false;
	m_flInventoryHintTime_guess = gEngfuncs.GetClientTime();
	m_iInventoryHintY_guess = -160;
}

void CHUDControl::LogoUpdate_guess()
{
	if ( m_bShowLogo_guess )
	{
		if ( CVAR_GET_FLOAT( "host_framerate" ) != 0 )
		{
			if ( m_pLogo_guess->isVisible() )
				m_pLogo_guess->setVisible( false );
			m_bShowLogo_guess = false;
			m_pLogoImage_guess->setColor( Color( 255, 255, 255, 255 ) );
			return;
		}

		if ( gEngfuncs.GetClientTime() < m_flLogoTime_guess )
		{
			if ( !m_pLogo_guess->isVisible() )
				m_pLogo_guess->setVisible( true );

			m_iLogoAlpha_guess -= 15;
			if ( m_iLogoAlpha_guess < 0 )
				m_iLogoAlpha_guess = 0;
			m_pLogoImage_guess->setColor( Color( 255, 255, 255, m_iLogoAlpha_guess ) );
		}
		else
		{
			m_iLogoAlpha_guess += 15;
			if ( m_iLogoAlpha_guess > 255 )
				m_iLogoAlpha_guess = 255;
			m_pLogoImage_guess->setColor( Color( 255, 255, 255, m_iLogoAlpha_guess ) );

			if ( m_pLogo_guess->isVisible() && m_iLogoAlpha_guess >= 255 )
			{
				m_pLogo_guess->setVisible( false );
				m_bShowLogo_guess = false;
				m_iLogoAlpha_guess = 255;
			}
		}
	}
	else
	{
		if ( m_pLogo_guess->isVisible() )
			m_pLogo_guess->setVisible( false );
	}
}

// JP-POC: draw a ProFont message line with the JP renderer (embedded
// Noto Sans JP subset, one texture per glyph, shared cache). The line is
// centered in the same strip the retail label uses. Two passes: shadow
// (+1,+1 black) then body, mirroring the subtitle panel renderer.
// NOTE: this VGUI surface uses INVERTED alpha (0 = opaque, 255 =
// transparent), same as the label fgColor. Pass the message alpha
// through unchanged; build #8 passed 255-a and the line was only
// visible as flashes during the fades (flicker / instant disappear).
bool CHUDControl::JpDrawProMessage_guess( const char *text, int r, int g, int b, int opacity )
{
	int pixelHeight = m_pCreditsFont_guess ? m_pCreditsFont_guess->getTall() : 0;
	if ( pixelHeight < 8 )
		pixelHeight = 16;
	if ( !JpSharedInitFont( pixelHeight ) )
		return false;

	float width = JpSharedMeasure( text );
	if ( width <= 0 )
		return false;

	int topY = ScreenHeight - g_iLetterboxHeight_guess * 3 + ( 64 - pixelHeight ) / 2;
	int baseline = topY + JpSharedAscentPx();

	const unsigned char *s = (const unsigned char *)text;
	int len = (int)strlen( text );

	for ( int pass = 0; pass < 2; pass++ )
	{
		int ox = ( pass == 0 ) ? 1 : 0;
		int oy = ( pass == 0 ) ? 1 : 0;
		int cr = ( pass == 0 ) ? 0 : r;
		int cg = ( pass == 0 ) ? 0 : g;
		int cb = ( pass == 0 ) ? 0 : b;

		float penX = ScreenWidth / 2.0f - width / 2.0f;
		int pos = 0;
		while ( pos < len )
		{
			if ( s[pos] == '*' )
				break;
			int cp;
			if ( !JpSharedDecodeUtf8( s, len, pos, cp ) )
				break;
			const JpGlyph *g = JpSharedGlyphMetrics( cp );
			if ( !g )
				break;
			if ( g->texId < 0 )
			{
				unsigned char *rgba = NULL;
				if ( JpSharedGlyphBitmap( cp, &rgba ) )
				{
					if ( rgba )
					{
						int texId = getSurfaceBase()->createNewTextureID();
						if ( texId > 0 )
						{
							drawSetTextureRGBA( texId, (const char *)rgba, g->w, g->h );
							JpSharedSetGlyphTexture( g, texId );
						}
						else
							JpSharedSetGlyphTexture( g, 0 );
						free( rgba );
					}
					else
						JpSharedSetGlyphTexture( g, 0 );
				}
			}
			if ( g->texId > 0 && g->w > 0 && g->h > 0 )
			{
				int dx = (int)( penX + 0.5f ) + g->xoff + ox;
				int dy = baseline + g->yoff + oy;
				drawSetTexture( g->texId );
				drawSetColor( cr, cg, cb, opacity );
				drawTexturedRect( dx, dy, dx + g->w, dy + g->h );
			}
			penX += g->adv;
		}
	}
	return true;
}

void CHUDControl::RenderMessages_guess()
{
	float flTime = gEngfuncs.GetClientTime();
	int bStep = !RcSmooth_RenderOnly();	// ReCoF (B315): the hint bar slides and fades in game frames only

	// control hint bar
	if ( m_bHint_guess )
	{
		int x, y;

		if ( m_flHintTime_guess > flTime )
		{
			if ( !m_pHintLabel_guess->isVisible() )
				m_pHintLabel_guess->setVisible( true );
			if ( !m_pHintBar_guess->isVisible() )
				m_pHintBar_guess->setVisible( true );

			if ( m_iHintAlpha_guess > 0 && bStep )
				m_iHintAlpha_guess -= 10;
			if ( m_iHintAlpha_guess < 0 )
				m_iHintAlpha_guess = 0;

			m_pHintLabel_guess->getPos( x, y );
			if ( y < 0 && bStep )
				y += 2;
			if ( y > 0 )
				y = 0;

			m_pHintBar_guess->setPos( ScreenWidth / 2 - 400, y );
			m_pHintBarImage_guess->setColor( Color( 255, 255, 255, m_iHintAlpha_guess ) );
			m_pHintLabel_guess->setPos( ScreenWidth / 2 - 400, y );
			m_pHintLabel_guess->setFgColor( 255, 255, 255, m_iHintAlpha_guess );
		}
		else
		{
			if ( m_iHintAlpha_guess < 255 && bStep )
				m_iHintAlpha_guess += 10;
			if ( m_iHintAlpha_guess > 255 )
				m_iHintAlpha_guess = 255;

			m_pHintLabel_guess->setFgColor( 255, 255, 255, m_iHintAlpha_guess );

			m_pHintLabel_guess->getPos( x, y );
			if ( y > -48 && bStep )
				y -= 2;
			if ( y < -48 )
				y = -48;

			m_pHintLabel_guess->setPos( ScreenWidth / 2 - 400, y );
			m_pHintBar_guess->setPos( ScreenWidth / 2 - 400, y );
			m_pHintBarImage_guess->setColor( Color( 255, 255, 255, m_iHintAlpha_guess ) );

			if ( m_iHintAlpha_guess >= 255 )
			{
				int w, h;

				switch ( m_iHintLine_guess )
				{
				case 1:
					m_iHintLine_guess = 2;
					if ( strlen( m_szHintLine2_guess ) > 2 )
					{
						m_pHintLabel_guess->setText( m_szHintLine2_guess );
						m_pCreditsFont_guess->getTextSize( m_szHintLine2_guess, w, h );
						m_pHintLabel_guess->setPos( ScreenWidth / 2 - 400, -48 );
						m_bHint_guess = true;
						m_iHintAlpha_guess = 255;
						m_flHintTime_guess = gEngfuncs.GetClientTime() + 5;
					}
					else
					{
						if ( !m_pHintLabel_guess->isVisible() )
						{
							m_bHint_guess = false;
							m_pHintLabel_guess->setVisible( true );
						}
						if ( m_pHintBar_guess->isVisible() == true )
							m_pHintBar_guess->setVisible( false );
					}
					break;
				case 2:
					m_iHintLine_guess = 3;
					if ( strlen( m_szHintLine3_guess ) > 2 )
					{
						m_pHintLabel_guess->setText( m_szHintLine3_guess );
						m_pCreditsFont_guess->getTextSize( m_szHintLine3_guess, w, h );
						m_pHintLabel_guess->setPos( ScreenWidth / 2 - 400, -48 );
						m_bHint_guess = true;
						m_iHintAlpha_guess = 255;
						m_flHintTime_guess = gEngfuncs.GetClientTime() + 5;
					}
					else
					{
						if ( !m_pHintLabel_guess->isVisible() )
						{
							m_bHint_guess = false;
							m_pHintLabel_guess->setVisible( true );
						}
						if ( m_pHintBar_guess->isVisible() == true )
							m_pHintBar_guess->setVisible( false );
					}
					break;
				case 3:
					if ( !m_pHintLabel_guess->isVisible() )
					{
						m_bHint_guess = false;
						m_pHintLabel_guess->setVisible( true );
					}
					if ( m_pHintBar_guess->isVisible() == true )
						m_pHintBar_guess->setVisible( false );
					break;
				default:
					if ( !m_pHintLabel_guess->isVisible() )
					{
						m_bHint_guess = false;
						m_pHintLabel_guess->setVisible( true );
					}
					if ( m_pHintBar_guess->isVisible() == true )
						m_pHintBar_guess->setVisible( false );
					break;
				}
			}
		}
	}
	else
	{
		if ( m_pHintLabel_guess->isVisible() )
			m_pHintLabel_guess->setVisible( false );
		if ( m_pHintBar_guess->isVisible() == true )
			m_pHintBar_guess->setVisible( false );
	}

	// chapter title
	if ( m_szChapter_guess[0] )
	{
		int w, h, x, y;

		if ( m_bChapterHold_guess && flTime > m_flChapterHoldTime_guess )
		{
			m_bChapterFadeOut_guess = true;
			m_bChapterHold_guess = false;
			m_flChapterAlpha_guess = 0;
			m_iChapterChars_guess = strlen( m_szChapter_guess );
		}

		if ( flTime > m_flChapterNextChar_guess )
		{
			m_flChapterNextChar_guess = flTime + 0.025;
			if ( !m_bMessageFadeOut_guess )
			{
				if ( m_iChapterChars_guess < (int)strlen( m_szChapter_guess ) )
					m_iChapterChars_guess++;
			}
		}

		if ( !m_bChapterFadeOut_guess )
		{
			if ( !m_bChapterHold_guess )
			{
				if ( m_flChapterAlpha_guess > 0 )
					m_flChapterAlpha_guess = m_flChapterAlpha_guess - gHUD.m_flTimeDelta * 250;
				if ( m_flChapterAlpha_guess < 0 )
				{
					m_bChapterHold_guess = true;
					m_flChapterAlpha_guess = 0;
					m_flChapterHoldTime_guess = flTime + 3.0;
				}
			}
		}
		else if ( m_bChapterFadeOut_guess == true && !m_bChapterHold_guess )
		{
			if ( m_flChapterAlpha_guess < 255.0 )
				m_flChapterAlpha_guess = gHUD.m_flTimeDelta * 125 + m_flChapterAlpha_guess;
			if ( m_flChapterAlpha_guess > 255.0 )
			{
				m_bChapterFadeOut_guess = false;
				m_flChapterAlpha_guess = 255;
				m_szChapter_guess[0] = '\0';
			}
		}

		// JP: chapter titles come from cof_chapter entities; translate via
		// the map-text table and draw with the shared JP renderer instead
		// of the VGUI label (which cannot render Japanese).
		const char *pszChapterJp = NULL;
		if ( jp_poc && jp_poc->value )
			pszChapterJp = JpLookupMapText( m_szChapter_guess );
		int iChapterPx = 0;
		float fChapterJw = 0;
		if ( pszChapterJp )
		{
			iChapterPx = m_pChapterTitleFont_guess ? m_pChapterTitleFont_guess->getTall() : 0;
			if ( iChapterPx < 8 )
				iChapterPx = 16;
			if ( JpSharedInitFont( iChapterPx ) )
				fChapterJw = JpSharedMeasure( pszChapterJp );
			if ( fChapterJw <= 0 )
				pszChapterJp = NULL;
		}

		drawSetTextFont( m_pChapterTitleFont_guess );
		if ( m_iChapterType_guess <= 5 )
			drawSetTextColor( 255, 255, 255, (int)m_flChapterAlpha_guess );
		else
			drawSetTextColor( 255, 0, 0, (int)m_flChapterAlpha_guess );
		m_pChapterTitleFont_guess->getTextSize( m_szChapter_guess, w, h );
		x = ScreenWidth / 2 - w / 2;
		y = ScreenHeight / 2 - h / 2;
		drawSetTextPos( x, y );

		if ( pszChapterJp )
		{
			w = (int)( fChapterJw + 0.5f );
			h = iChapterPx;
			x = ScreenWidth / 2 - w / 2;
			y = ScreenHeight / 2 - h / 2;
		}

		if ( m_szChapter_guess[0] && m_iChapterType_guess <= 5 )
			gEngfuncs.pfnFillRGBA( x, y + h, (int)( m_iChapterChars_guess / (float)strlen( m_szChapter_guess ) * w ), 2, 255, 255, 255, (int)( 255.0f - m_flChapterAlpha_guess ) );

		m_pMessageLabel2_guess->setSize( ScreenWidth, 64 );
		m_pMessageLabel2_guess->setText( pszChapterJp ? "" : m_szChapter_guess );
		m_pMessageLabel2_guess->setPos( 0, ScreenHeight / 2 - 32 );
		m_pMessageLabel2_guess->setContentAlignment( Label::a_center );
		if ( m_iChapterType_guess <= 5 )
			m_pMessageLabel2_guess->setFgColor( 255, 255, 255, (int)m_flChapterAlpha_guess );
		else
			m_pMessageLabel2_guess->setFgColor( 255, 0, 0, (int)m_flChapterAlpha_guess );

		if ( pszChapterJp )
		{
			int iCR = 255, iCG = 255, iCB = 255;
			if ( m_iChapterType_guess > 5 )
			{
				iCG = 0;
				iCB = 0;
			}
			int iAlpha = (int)m_flChapterAlpha_guess;
			int iBase = ScreenHeight / 2 - iChapterPx / 2 + JpSharedAscentPx();
			const unsigned char *pS = (const unsigned char *)pszChapterJp;
			int iLen = (int)strlen( pszChapterJp );
			for ( int pass = 0; pass < 2; pass++ )
			{
				int ox = ( pass == 0 ) ? 1 : 0;
				int oy = ( pass == 0 ) ? 1 : 0;
				int cr = ( pass == 0 ) ? 0 : iCR;
				int cg = ( pass == 0 ) ? 0 : iCG;
				int cb = ( pass == 0 ) ? 0 : iCB;
				float penX = ScreenWidth / 2.0f - fChapterJw / 2.0f;
				int pos = 0;
				while ( pos < iLen )
				{
					int cp;
					if ( !JpSharedDecodeUtf8( pS, iLen, pos, cp ) )
						break;
					const JpGlyph *g = JpSharedGlyphMetrics( cp );
					if ( !g )
						break;
					if ( g->texId < 0 )
					{
						unsigned char *rgba = NULL;
						if ( JpSharedGlyphBitmap( cp, &rgba ) )
						{
							if ( rgba )
							{
								int texId = getSurfaceBase()->createNewTextureID();
								if ( texId > 0 )
								{
									drawSetTextureRGBA( texId, (const char *)rgba, g->w, g->h );
									JpSharedSetGlyphTexture( g, texId );
								}
								else
									JpSharedSetGlyphTexture( g, 0 );
								free( rgba );
							}
							else
								JpSharedSetGlyphTexture( g, 0 );
						}
						else
							JpSharedSetGlyphTexture( g, 0 );
					}
					if ( g->texId > 0 && g->w > 0 && g->h > 0 )
					{
						int dx = (int)( penX + 0.5f ) + g->xoff + ox;
						int dy = iBase + g->yoff + oy;
						drawSetTexture( g->texId );
						drawSetColor( cr, cg, cb, iAlpha );
						drawTexturedRect( dx, dy, dx + g->w, dy + g->h );
					}
					penX += g->adv;
				}
			}
		}
	}

	// message strip
	if ( !(int)CVAR_GET_FLOAT( "cl_subtitles" ) )
		return;

	if ( m_szMessage_guess[0] )
	{
		unsigned int iLen2 = strlen( m_szMessage2_guess );
		if ( strlen( m_szMessage_guess ) > 1 )
		{
			if ( m_bMessageHold_guess && flTime > m_flMessageHoldTime_guess )
			{
				m_bMessageFadeOut_guess = true;
				m_bMessageHold_guess = false;
				m_flMessageAlpha_guess = 0;
				m_iMessageTime_guess = 0;
				m_iMessageChars_guess = strlen( m_szChapter_guess );
			}

			if ( !m_bMessageFadeOut_guess )
			{
				if ( !m_bMessageHold_guess )
				{
					if ( m_flMessageAlpha_guess > 0 )
						m_flMessageAlpha_guess = m_flMessageAlpha_guess - gHUD.m_flTimeDelta * 500;
					if ( m_flMessageAlpha_guess < 0 )
					{
						m_bMessageHold_guess = true;
						m_flMessageAlpha_guess = 0;
						m_flMessageHoldTime_guess = flTime + 4.0;
					}
				}
			}
			else if ( m_bMessageFadeOut_guess == true && !m_bMessageHold_guess )
			{
				if ( m_flMessageAlpha_guess < 255.0 )
					m_flMessageAlpha_guess = gHUD.m_flTimeDelta * 500 + m_flMessageAlpha_guess;
				if ( m_flMessageAlpha_guess > 255.0 )
				{
					m_bMessageFadeOut_guess = false;
					m_flMessageAlpha_guess = 255;
					m_szMessage_guess[0] = '\0';
				}
			}

			int a = m_flMessageAlpha_guess;

			// JP-POC: ProFont lines in Japanese UTF-8 are drawn by the JP
			// renderer; the retail label cannot display them.
			bool bJpMsg = (jp_poc && jp_poc->value && JpTextIsUtf8Japanese( m_szMessage_guess ));
			{
				static char s_szEvalLogged[256] = "";
				if ( strcmp( s_szEvalLogged, m_szMessage_guess ) != 0 )
				{
					strncpy( s_szEvalLogged, m_szMessage_guess, 255 );
					s_szEvalLogged[255] = 0;
					char hexbuf[80]; hexbuf[0] = 0;
					const unsigned char *hb = (const unsigned char *)m_szMessage_guess;
					for (int i = 0; i < 12 && hb[i]; i++) { char tmp[4]; sprintf(tmp, "%02X ", hb[i]); strcat(hexbuf, tmp); }
					JpLogHud( "RenderMsg EVAL: jp_poc=%.1f detect=%d bJp=%d bytes=%s text=%.40s",
						jp_poc ? (double)jp_poc->value : -1.0,
						JpTextIsUtf8Japanese( m_szMessage_guess ) ? 1 : 0, bJpMsg ? 1 : 0, hexbuf, m_szMessage_guess );
				}
			}
			drawSetTextFont( m_pCreditsFont_guess );

			switch ( m_iMessageType_guess )
			{
			case 0:
			case 3:
				drawSetTextColor( 255, 255, 0, a );
				break;
			case 1:
				drawSetTextColor( 0, 128, 255, a );
				break;
			case 4:
				drawSetTextColor( 0, 255, 0, a );
				break;
			case 5:
				drawSetTextColor( 200, 200, 200, a );
				break;
			case 2:
			case 6:
				drawSetTextColor( 255, 0, 0, a );
				break;
			}

			int w, h, w2, x, y;
			m_pCreditsFont_guess->getTextSize( m_szMessage_guess, w, h );
			if ( iLen2 > 1 )
				m_pCreditsFont_guess->getTextSize( m_szMessage2_guess, w2, h );

			if ( m_iMessageType_guess <= 5 )
			{
				x = ScreenWidth / 2 - w / 2;
				y = ScreenHeight - h * 2;
			}
			else
			{
				x = 10;
				y = ScreenHeight / 2 - h / 2;
			}

			drawSetTextPos( x, iLen2 > 1 ? y - h : y );

			m_pMessageLabel_guess->setSize( ScreenWidth, 64 );
			if ( bJpMsg )
				m_pMessageLabel_guess->setText( " " );
			else
				m_pMessageLabel_guess->setText( m_szMessage_guess );
			m_pCreditsFont_guess->getTextSize( m_szMessage_guess, w, h );
			m_pMessageLabel_guess->setPos( 0, ScreenHeight - g_iLetterboxHeight_guess * 3 );
			m_pMessageLabel_guess->setContentAlignment( Label::a_center );

			switch ( m_iMessageType_guess )
			{
			case 0:
			case 3:
				m_pMessageLabel_guess->setFgColor( 255, 255, 0, a );
				break;
			case 1:
				m_pMessageLabel_guess->setFgColor( 0, 128, 255, a );
				break;
			case 4:
				m_pMessageLabel_guess->setFgColor( 0, 255, 0, a );
				break;
			case 5:
				m_pMessageLabel_guess->setFgColor( 200, 200, 200, a );
				break;
			case 2:
			case 6:
				m_pMessageLabel_guess->setFgColor( 255, 0, 0, a );
				break;
			}

			if ( bJpMsg )
			{
				int jr = 200, jg = 200, jb = 200;
				switch ( m_iMessageType_guess )
				{
				case 0: case 3: jr = 255; jg = 255; jb = 0; break;
				case 1: jr = 0; jg = 128; jb = 255; break;
				case 4: jr = 0; jg = 255; jb = 0; break;
				case 2: case 6: jr = 255; jg = 0; jb = 0; break;
				}
				JpDrawProMessage_guess( m_szMessage_guess, jr, jg, jb, a );
				static char s_szJpLogged[256] = "";
				if ( strcmp( s_szJpLogged, m_szMessage_guess ) != 0 )
				{
					strncpy( s_szJpLogged, m_szMessage_guess, 255 );
					s_szJpLogged[255] = 0;
					JpLogHud( "ProFont JP: type=%d a=%d text=%.40s", m_iMessageType_guess, a, m_szMessage_guess );
				}
			}

			if ( iLen2 > 1 )
			{
				m_pCreditsFont_guess->getTextSize( m_szMessage2_guess, w, h );
				drawSetTextPos( ScreenWidth / 2 - w / 2, ScreenHeight - h * 2 );
			}
		}
	}
	else
	{
		m_iMessageCC_guess = 0;
		m_iMessageD0_guess = 0;
		m_iMessageC4_guess = 0;
		m_iMessageC8_guess = 0;
		m_iMessageType_guess = 5;
		if ( strlen( m_szMessage_guess ) > 1 )
			m_szMessage_guess[0] = '\0';
		if ( strlen( m_szMessage2_guess ) > 1 )
			m_szMessage2_guess[0] = '\0';
	}
}

void CHUDControl::ShowSMSIcon_guess( bool bShow )
{
	m_iSMSIconAlpha_guess = 255;
	m_bSMSIcon_guess = bShow;
	m_pSMSIcon_guess->setVisible( bShow );
}

// called by TeamFortressViewport::UpdateOnPlayerInfo, empty in 1.6a
void CHUDControl::UpdateOnPlayerInfo_guess()
{
}

void CHUDControl::paint()
{
	int h, w;
	int x, y;

	if ( g_iDickMode_guess )
	{
		switch ( gEngfuncs.pfnRandomLong( 1, 4 ) )
		{
		case 1:
			drawSetTextFont( m_pCreditsFont_guess );
			drawSetTextColor( 255, 255, 0, 1 );
			m_pCreditsFont_guess->getTextSize( "Dicks", w, h );
			x = gEngfuncs.pfnRandomLong( 0, 50 ) + ( ScreenWidth / 2 - w / 2 );
			y = gEngfuncs.pfnRandomLong( 0, 50 ) + ( ScreenHeight / 2 - h / 2 );
			drawSetTextPos( x, y );
			drawPrintText( "Dicks", 5 );
			break;
		case 2:
			drawSetTextFont( m_pCreditsFont_guess );
			drawSetTextColor( 255, 255, 0, 1 );
			m_pCreditsFont_guess->getTextSize( "Balls", w, h );
			x = gEngfuncs.pfnRandomLong( 0, 50 ) + ( ScreenWidth / 2 - w / 2 );
			y = gEngfuncs.pfnRandomLong( 0, 50 ) + ( ScreenHeight / 2 - h / 2 );
			drawSetTextPos( x, y );
			drawPrintText( "Balls", 5 );
			break;
		case 3:
			drawSetTextFont( m_pCreditsFont_guess );
			drawSetTextColor( 255, 255, 0, 1 );
			m_pCreditsFont_guess->getTextSize( "Dongs", w, h );
			x = gEngfuncs.pfnRandomLong( 0, 50 ) + ( ScreenWidth / 2 - w / 2 );
			y = gEngfuncs.pfnRandomLong( 0, 50 ) + ( ScreenHeight / 2 - h / 2 );
			drawSetTextPos( x, y );
			drawPrintText( "Dongs", 5 );
			break;
		case 4:
			drawSetTextFont( m_pCreditsFont_guess );
			drawSetTextColor( 255, 255, 0, 1 );
			m_pCreditsFont_guess->getTextSize( "Butts", w, h );
			x = gEngfuncs.pfnRandomLong( 0, 50 ) + ( ScreenWidth / 2 - w / 2 );
			y = gEngfuncs.pfnRandomLong( 0, 50 ) + ( ScreenHeight / 2 - h / 2 );
			drawSetTextPos( x, y );
			drawPrintText( "Butts", 5 );
			break;
		}
	}

	RenderMessages_guess();

	// JP-MOD: draw a pending JP title request (filled by CHudMessage).
	if ( g_jpTitleRequest.bActive )
	{
		g_jpTitleRequest.bActive = false;
		if ( gHUD.m_flTime - g_jpTitleRequest.flSetTime < 0.25f &&
			JpSharedInitFont( g_jpTitleRequest.iPixelHeight ) )
		{
			for ( int li = 0; li < g_jpTitleRequest.iLineCount; li++ )
			{
				const unsigned char *s = (const unsigned char *)g_jpTitleRequest.szLine[li];
				int iSLen = (int)strlen( g_jpTitleRequest.szLine[li] );
				for ( int pass = 0; pass < 2; pass++ )
				{
					int ox = ( pass == 0 ) ? 1 : 0;
					int oy = ( pass == 0 ) ? 1 : 0;
					int cr = ( pass == 0 ) ? 0 : g_jpTitleRequest.iR;
					int cg = ( pass == 0 ) ? 0 : g_jpTitleRequest.iG;
					int cb = ( pass == 0 ) ? 0 : g_jpTitleRequest.iB;
					float penX = (float)g_jpTitleRequest.iLineX[li];
					int pos = 0;
					while ( pos < iSLen )
					{
						int cp;
						if ( !JpSharedDecodeUtf8( s, iSLen, pos, cp ) )
							break;
						const JpGlyph *g = JpSharedGlyphMetrics( cp );
						if ( !g )
							break;
						if ( g->texId < 0 )
						{
							unsigned char *rgba = NULL;
							if ( JpSharedGlyphBitmap( cp, &rgba ) )
							{
								if ( rgba )
								{
									int texId = getSurfaceBase()->createNewTextureID();
									if ( texId > 0 )
									{
										drawSetTextureRGBA( texId, (const char *)rgba, g->w, g->h );
										JpSharedSetGlyphTexture( g, texId );
									}
									else
										JpSharedSetGlyphTexture( g, 0 );
									free( rgba );
								}
								else
									JpSharedSetGlyphTexture( g, 0 );
							}
							else
								JpSharedSetGlyphTexture( g, 0 );
						}
						if ( g->texId > 0 && g->w > 0 && g->h > 0 )
						{
							int dx = (int)( penX + 0.5f ) + g->xoff + ox;
							int dy = g_jpTitleRequest.iLineBaseY[li] + g->yoff + oy;
							drawSetTexture( g->texId );
							drawSetColor( cr, cg, cb, g_jpTitleRequest.iAlpha );
							drawTexturedRect( dx, dy, dx + g->w, dy + g->h );
						}
						penX += g->adv;
					}
				}
			}
		}
	}

	Panel::paint();
}

void CHUDControl::paintBackground()
{
	Panel::paintBackground();
}

void CHUDControl::solve()
{
	int i;
	int r, g, b, a;
	int x, y, x2, y2;
	char szText[128];
	char szTime[128];
	char szSeconds[32];
	char szMinutes[32];
	char szHours[32];

	// introduction picture
	if ( m_pIntroduction_guess->isVisible() )
	{
		if ( m_bIntroductionFadeIn_guess )
		{
			if ( m_iIntroductionAlpha_guess > 0 )
				m_iIntroductionAlpha_guess -= 2;
			if ( m_iIntroductionAlpha_guess < 0 )
				m_iIntroductionAlpha_guess = 0;
			m_pIntroductionImage_guess->setColor( Color( 255, 255, 255, m_iIntroductionAlpha_guess ) );
		}
		else
		{
			if ( m_iIntroductionAlpha_guess < 255 )
				m_iIntroductionAlpha_guess += 2;
			m_pIntroductionImage_guess->setColor( Color( 255, 255, 255, m_iIntroductionAlpha_guess ) );

			if ( m_iIntroductionAlpha_guess >= 255 )
			{
				m_iIntroductionAlpha_guess = 255;
				m_pIntroductionImage_guess->setColor( Color( 255, 255, 255, 255 ) );
				m_pIntroduction_guess->setVisible( false );
			}
		}
	}

	// timer colors and the "+" popups
	if ( m_bTimer_guess )
	{
		if ( gEngfuncs.GetClientTime() > m_flTimerPlusTime_guess )
		{
			m_flTimerPlusTime_guess = gEngfuncs.GetClientTime() + 0.05;

			m_pTimerLabel_guess->getFgColor( r, g, b, a );
			if ( r < 255 )
			{
				b += 5;
				r += 5;
			}
			if ( r > 255 )
			{
				r = 255;
				b = 255;
				m_pTimerLabel_guess->setFgColor( 255, g, 255, a );
			}
			if ( g_iSimonMode_guess )
			{
				r = 128;
				g = 0;
				b = 0;
			}
			m_pTimerLabel_guess->setFgColor( r, g, b, a );

			for ( i = 0; i < 32; i++ )
			{
				if ( m_pTimerPlusLabel_guess[i]->isVisible() )
				{
					m_pTimerPlusLabel_guess[i]->getFgColor( r, g, b, a );
					a += 15;
					if ( a > 255 )
					{
						a = 255;
						m_pTimerPlusLabel_guess[i]->setFgColor( r, g, b, 255 );
						m_pTimerPlusLabel_guess[i]->setVisible( false );
					}
					else
					{
						m_pTimerPlusLabel_guess[i]->setFgColor( r, g, b, a );
						m_pTimerPlusLabel_guess[i]->getPos( x, y );
						x++;
						m_pTimerPlusLabel_guess[i]->setPos( x, y );
					}
				}
			}
		}
	}

	// lobby
	if ( m_pLobbyLabel_guess[0] && m_bLobby_guess )
	{
		if ( gHUD.GetKeyForCommand_guess( "+attack" ) )
			sprintf( szText, "Press %s to become ready/unready", gHUD.GetKeyForCommand_guess( "+attack" ) );
		else
			strcpy( szText, "Press Primary Attack to become ready/unready" );
		m_pVoteLabel_guess->setText( "%s", szText );

		for ( i = 0; i < 4; i++ )
		{
			if ( g_PlayerInfoList[i + 1].name && strlen( g_PlayerInfoList[i + 1].name ) >= 1 )
			{
				char szReady[256];
				if ( m_bPlayerReady_guess[i] )
				{
					m_pLobbyLabel_guess[i]->setFgColor( 0, 128, 0, 0 );
					sprintf( szReady, "%s - Ready", g_PlayerInfoList[i + 1].name );
				}
				else
				{
					m_pLobbyLabel_guess[i]->setFgColor( 128, 0, 0, 0 );
					sprintf( szReady, "%s - Not ready", g_PlayerInfoList[i + 1].name );
				}
				m_pLobbyLabel_guess[i]->setText( "%s", szReady );
			}
			else
			{
				m_pLobbyLabel_guess[i]->setText( "-" );
				m_pLobbyLabel_guess[i]->setFgColor( 255, 255, 255, 0 );
			}
		}
	}

	// survival timer
	if ( m_bTimer_guess )
	{
		if ( !m_pTimerLabel_guess->isVisible() )
			m_pTimerLabel_guess->setVisible( true );

		if ( gEngfuncs.GetClientTime() > m_flTimerNextSecond_guess && !m_bTimerPaused_guess )
		{
			m_iTimerSeconds_guess++;
			if ( m_iTimerSeconds_guess > 59 )
			{
				m_iTimerSeconds_guess = 0;
				m_iTimerMinutes_guess++;
			}

			int iSecondsLeft = 60 - m_iTimerSeconds_guess;
			int iMinutesLeft = m_iTimerLimit_guess / 60 - m_iTimerMinutes_guess - 1;

			if ( iMinutesLeft < 0 )
				iMinutesLeft = 0;
			if ( iSecondsLeft < 0 )
				iSecondsLeft = 0;
			if ( m_iTimerHours_guess < 0 )
				m_iTimerHours_guess = 0;

			sprintf( szSeconds, ( iSecondsLeft >= 10 ) ? "%i" : "0%i", iSecondsLeft );
			sprintf( szMinutes, ( iMinutesLeft >= 10 ) ? "%i" : "0%i", iMinutesLeft );
			sprintf( szHours, ( m_iTimerHours_guess >= 10 ) ? "%i" : "0%i", m_iTimerHours_guess );
			sprintf( szTime, "%s:%s:%s", szHours, szMinutes, szSeconds );
			m_pTimerLabel_guess->setText( szTime );
			m_flTimerNextSecond_guess = gEngfuncs.GetClientTime() + 1;
		}
	}

	if ( gHUD.m_iFlashFlags_guess & 0x40 )
	{
		if ( !m_pOverlay_guess->isVisible() )
			m_pOverlay_guess->setVisible( true );
		else
			m_pOverlay_guess->setBgColor( 33, 177, 120, gEngfuncs.pfnRandomLong( 0, 5 ) + 192 );
	}
	else
	{
		if ( m_pOverlay_guess->isVisible() )
			m_pOverlay_guess->setVisible( false );
	}

	if ( gHUD.m_fPlayerDead )
	{
		m_pMessageLabel2_guess->setText( "" );
		m_pMessageLabel_guess->setText( "" );
		m_pHintLabel_guess->setText( "" );

		m_bHint_guess = false;
		m_iHintAlpha_guess = 255;
		m_szChapter_guess[0] = '\0';
		m_flMessageAlpha_guess = 0;
		m_bMessageFadeOut_guess = false;
		m_bMessageHold_guess = false;
		m_flMessageHoldTime_guess = 0;
		m_flMessageF24_guess = 0;
		m_iMessageChars_guess = 0;
		m_iChapterType_guess = 0;
		m_flChapterAlpha_guess = 0;
		m_bChapterFadeOut_guess = false;
		m_bChapterF1E_guess = false;
		m_flChapterHoldTime_guess = 0;
		m_flChapterNextChar_guess = 0;
		m_iChapterChars_guess = 0;
	}

	// co-op player list
	// ReCoF (B438): and the chat lines, which went to the console only: on the police co-op's cof_ maps, and in story
	// co-op too (the server's physinfo rcc 1, hl.dll rc_coop.cpp; charliee1w 373f57c7)
	const char *pszRcCoop = gEngfuncs.PhysInfo_ValueForKey( "rcc" );
	if ( ( gEngfuncs.pfnGetLevelName() && strstr( gEngfuncs.pfnGetLevelName(), "cof_" ) ) || ( pszRcCoop && pszRcCoop[0] == '1' ) )
	{
		if ( m_iDeathAlpha_guess < 255 )
			m_iDeathAlpha_guess += 5;
		if ( m_iDeathAlpha_guess < 0 )
			m_iDeathAlpha_guess = 0;
		else if ( m_iDeathAlpha_guess > 255 )
			m_iDeathAlpha_guess = 255;

		if ( !m_pDeathLabel_guess->isVisible() )
			m_pDeathLabel_guess->setVisible( true );
		m_pDeathLabel_guess->setFgColor( 255, 0, 0, m_iDeathAlpha_guess );

		for ( i = 0; i < 5; i++ )
		{
			if ( !m_pSayTextLabel_guess[i]->isVisible() )
				m_pSayTextLabel_guess[i]->setVisible( true );
		}

		for ( i = 0; i < 4; i++ )
		{
			if ( g_PlayerInfoList[i + 1].name && strlen( g_PlayerInfoList[i + 1].name ) >= 1 )
			{
				if ( !m_pPlayerLabel_guess[i]->isVisible() )
					m_pPlayerLabel_guess[i]->setVisible( true );

				if ( gHUD.m_Health.m_flAdLibTime_guess[i] > gEngfuncs.GetClientTime() )
				{
					if ( !m_pFollowIconPanel_guess[i]->isVisible() )
						m_pFollowIconPanel_guess[i]->setVisible( true );
				}
				else
				{
					if ( m_pFollowIconPanel_guess[i]->isVisible() )
						m_pFollowIconPanel_guess[i]->setVisible( false );
				}

				if ( gHUD.m_Health.m_bSyringe_guess[i] )
				{
					if ( !m_pSyringeIconPanel_guess[i]->isVisible() )
						m_pSyringeIconPanel_guess[i]->setVisible( true );
				}
				else
				{
					if ( m_pSyringeIconPanel_guess[i]->isVisible() )
						m_pSyringeIconPanel_guess[i]->setVisible( false );
				}

				int w, h, w2, h2;
				sprintf( szTime, "%s - Health %i - Ping %d", g_PlayerInfoList[i + 1].name, gHUD.m_Health.m_iCoopHealth_guess[i + 1], g_PlayerInfoList[i + 1].ping );
				m_pPagerFont_guess->getTextSize( szTime, w, h );
				m_pPlayerLabel_guess[i]->getSize( w2, h2 );
				m_pPlayerLabel_guess[i]->getPos( x, y );
				m_pPlayerLabel_guess[i]->setSize( w, h );
				m_pPlayerLabel_guess[i]->setPos( ScreenWidth - w - 5, h * i + ScreenHeight / 1.5 );
				m_pPlayerLabel_guess[i]->setText( szTime );
				m_pPlayerLabel_guess[i]->getPos( x, y );
				m_pFollowIconPanel_guess[i]->setPos( x - 28, y + 4 );
				m_pSyringeIconPanel_guess[i]->setPos( x - 14, y + 4 );

				if ( m_iDevDonator_guess[i + 1] == 1 )
					m_pPlayerLabel_guess[i]->setFgColor( 117, 0, 0, 0 );
				else if ( m_iDevDonator_guess[i + 1] == 2 )
					m_pPlayerLabel_guess[i]->setFgColor( 255, 211, 81, 0 );
				else
					m_pPlayerLabel_guess[i]->setFgColor( 255, 255, 255, 0 );
			}
			else
			{
				if ( m_pPlayerLabel_guess[i]->isVisible() )
					m_pPlayerLabel_guess[i]->setVisible( false );
				if ( m_pFollowIconPanel_guess[i]->isVisible() )
					m_pFollowIconPanel_guess[i]->setVisible( false );
				if ( m_pSyringeIconPanel_guess[i]->isVisible() )
					m_pSyringeIconPanel_guess[i]->setVisible( false );
			}
		}

		if ( m_flLookingAtTime_guess > gEngfuncs.GetClientTime() )
		{
			if ( !m_pLookingAtLabel_guess->isVisible() )
				m_pLookingAtLabel_guess->setVisible( true );
		}
		else
		{
			if ( m_pLookingAtLabel_guess->isVisible() )
				m_pLookingAtLabel_guess->setVisible( false );
		}

		if ( g_flLookingAt_guess != 0 && g_flLookingAt_guess <= 4 )
		{
			char *pszName = g_PlayerInfoList[(int)g_flLookingAt_guess].name;
			if ( pszName )
			{
				m_flLookingAtTime_guess = gEngfuncs.GetClientTime() + 0.1;
				m_pLookingAtLabel_guess->setText( pszName );
			}
		}
	}
	else
	{
		for ( i = 0; i < 4; i++ )
		{
			if ( m_pPlayerLabel_guess[i]->isVisible() )
				m_pPlayerLabel_guess[i]->setVisible( false );
			if ( m_pFollowIconPanel_guess[i]->isVisible() )
				m_pFollowIconPanel_guess[i]->setVisible( false );
			if ( m_pSyringeIconPanel_guess[i]->isVisible() )
				m_pSyringeIconPanel_guess[i]->setVisible( false );
		}

		for ( i = 0; i < 5; i++ )
		{
			if ( m_pSayTextLabel_guess[i]->isVisible() )
				m_pSayTextLabel_guess[i]->setVisible( false );
		}

		if ( m_pLookingAtLabel_guess->isVisible() )
			m_pLookingAtLabel_guess->setVisible( false );
		if ( m_pDeathLabel_guess->isVisible() )
			m_pDeathLabel_guess->setVisible( false );
		if ( m_iDeathAlpha_guess < 255 )
			m_iDeathAlpha_guess = 255;
	}

	// stamina flash
	m_pStaminaFlash_guess->getBgColor( r, g, b, a );
	if ( m_flStaminaFlashTime_guess > gEngfuncs.GetClientTime() )
	{
		m_pStaminaFlash_guess->setBgColor( 255, 0, 0, 128 );
	}
	else
	{
		if ( a < 255 )
		{
			a += 15;
			m_pStaminaFlash_guess->setBgColor( r, g, b, a );
		}
		if ( a > 255 )
		{
			a = 255;
			m_pStaminaFlash_guess->setBgColor( r, g, b, 255 );
		}
	}

	// letterbox
	static bool s_bLetterboxFrame = false;
	int iSpeed = g_iLetterboxHeight_guess / 15;
	if ( iSpeed <= 0 )
		iSpeed = 1;

	if ( gHUD.m_BlackBar_guess.m_iOn_guess )
	{
		s_bLetterboxFrame = !s_bLetterboxFrame;
		if ( s_bLetterboxFrame )
		{
			m_pLetterboxTop_guess->getPos( x, y );
			m_pLetterboxBottom_guess->getPos( x2, y2 );

			if ( y < 0 )
				y += iSpeed;
			if ( y > 0 )
				y = 0;
			if ( y2 > ScreenHeight - g_iLetterboxHeight_guess )
				y2 -= iSpeed;
			if ( y2 < ScreenHeight - g_iLetterboxHeight_guess )
				y2 = ScreenHeight - g_iLetterboxHeight_guess;

			m_pLetterboxTop_guess->setPos( x, y );
			m_pLetterboxBottom_guess->setPos( x2, y2 );
		}
	}
	else
	{
		s_bLetterboxFrame = !s_bLetterboxFrame;
		if ( s_bLetterboxFrame )
		{
			m_pLetterboxTop_guess->getPos( x, y );
			m_pLetterboxBottom_guess->getPos( x2, y2 );

			if ( y > -g_iLetterboxHeight_guess )
				y -= iSpeed;
			if ( y < -g_iLetterboxHeight_guess )
				y = -g_iLetterboxHeight_guess;
			if ( y2 < ScreenHeight )
				y2 += iSpeed;
			if ( y2 > ScreenHeight )
				y2 = ScreenHeight;

			m_pLetterboxTop_guess->setPos( x, y );
			m_pLetterboxBottom_guess->setPos( x2, y2 );
		}
	}

	HandleHUDVisibility_guess();
	LogoUpdate_guess();

	if ( iWheelchair_guess && m_pStaminaBar_guess->isVisible() )
		m_pStaminaBar_guess->setVisible( false );
	if ( iWheelchair_guess && m_pStaminaBarFull_guess->isVisible() )
		m_pStaminaBarFull_guess->setVisible( false );
	if ( iWheelchair_guess && m_pStaminaBarEmpty_guess->isVisible() )
		m_pStaminaBarEmpty_guess->setVisible( false );

	// camera zoom
	if ( gEngfuncs.GetClientTime() >= m_flZoomTime_guess )
	{
		if ( m_flZoomTime_guess + m_flZoomLength_guess > gEngfuncs.GetClientTime() )
		{
			float flFrac = ( gEngfuncs.GetClientTime() - m_flZoomTime_guess ) / m_flZoomLength_guess;
			if ( flFrac > 1.0f )
				gHUD.m_iFOV = m_flZoomFOV_guess;

			if ( gEngfuncs.GetClientTime() > m_flZoomTime_guess + m_flZoomLength_guess )
				gHUD.m_iFOV = m_flZoomFOV_guess;
			else
				gHUD.m_iFOV = 90 - (int)( flFrac * m_flZoomFOV_guess );

			if ( gHUD.m_iFOV > m_flZoomFOV_guess )
				gHUD.m_iFOV = m_flZoomFOV_guess;
		}
	}

	// sms icon fade
	if ( m_pSMSIcon_guess->isVisible() && m_bSMSIcon_guess )
	{
		if ( gEngfuncs.GetClientTime() > m_flSMSIconTime_guess )
		{
			if ( m_iSMSIconAlpha_guess > 0 )
			{
				m_iSMSIconAlpha_guess -= 15;
				m_pSMSIconImage_guess->setColor( Color( 255, 255, 255, m_iSMSIconAlpha_guess ) );
			}
			m_flSMSIconTime_guess = gEngfuncs.GetClientTime() + 0.05;
		}
	}

	// inventory hint slide
	if ( m_pInventoryHint_guess->isVisible() && m_bInventoryHint_guess )
	{
		m_pInventoryHint_guess->getPos( x, y );

		if ( gEngfuncs.GetClientTime() >= m_flHintSlideInTime_guess )
		{
			if ( m_flHintSlideInTime_guess + m_flHintSlideInLength_guess > gEngfuncs.GetClientTime() )
			{
				m_pInventoryHint_guess->setPos( x, (int)( ( gEngfuncs.GetClientTime() - m_flHintSlideInTime_guess ) / m_flHintSlideInLength_guess * 160 ) - 154 );
				if ( y < -161 )
				{
					m_bInventoryHint_guess = false;
					y = -161;
				}
			}
		}

		if ( gEngfuncs.GetClientTime() >= m_flHintSlideOutTime_guess )
		{
			if ( m_flHintSlideOutTime_guess + m_flHintSlideOutLength_guess > gEngfuncs.GetClientTime() )
			{
				float flFrac = ( gEngfuncs.GetClientTime() - m_flHintSlideOutTime_guess ) / m_flHintSlideOutLength_guess;
				if ( flFrac > 1.0f )
					m_bInventoryHint_guess = false;
				m_pInventoryHint_guess->setPos( x, (int)( flFrac * -160 ) );
				if ( y < -161 )
				{
					m_bInventoryHint_guess = false;
					y = -161;
				}
				else if ( y > 0 )
					y = 0;
			}
		}

		if ( gEngfuncs.GetClientTime() < m_flHintSlideOutTime_guess && ( GetAsyncKeyState( VK_RETURN ) & 0x8000 ) )
		{
			m_flHintSlideInTime_guess = 0;
			m_flHintSlideInLength_guess = 0;
			m_flHintSlideOutTime_guess = gEngfuncs.GetClientTime();
			m_flHintSlideOutLength_guess = 1;
		}
	}

	// health and stamina bars
	int iWide = m_pHealthBarEmpty_guess->getWide();
	int iHealth = gHUD.m_Health.m_iHealth;
	if ( bWheelchairLock_guess )
		iHealth = 0;
	if ( iHealth < 0 )
		iHealth = 0;
	m_pHealthBarEmpty_guess->setSize( iWide, ( 100 - iHealth ) * 1.18 );
	m_pStaminaBarEmpty_guess->setSize( m_pStaminaBarEmpty_guess->getWide(), ( 100 - m_iStamina_guess ) * 1.18 );

	Panel::solve();
}
