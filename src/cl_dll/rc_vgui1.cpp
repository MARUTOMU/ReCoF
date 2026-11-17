// ReCoF (B401): Cry of Fear's own picture screens (VGUI1) at the screen's size. Its menus are pictures drawn at their
// pixel size, laid out for 640 x 480 to 1280 x 720: at 1080p and above they shrink to a small column. CRcImagePanel
// draws a picture at RcMenuScale by uploading the picture's own pixels (CRcTGA, every TGA vgui_LoadTGA makes) to a
// texture of its own and stretching it; nothing changes at 720p and below.

#include "hud.h"
#include "cl_util.h"
#include "rc_vgui1.h"
#include "..\game_shared\vgui_loadtga.h"
#include "VGUI_SurfaceBase.h"

float RcMenuScale( void )
{
	// B428: from 960 lines up (was 720): at 1280 x 960 the main menu took 1.33 times its pictures and filled the
	// screen; the player's Menu size (rc_menu_scale) still makes it larger
	float scale = ScreenHeight / 960.0f;
	if ( scale < 1.0f )
		scale = 1.0f;
	float user = gEngfuncs.pfnGetCvarFloat( "rc_menu_scale" ) / 100.0f;
	if ( user < 1.0f )
		user = 1.0f;
	if ( user > 2.0f )
		user = 2.0f;
	scale *= user;
	// the main menu's column (twelve rows of 38) stays clear of the logo above the screen's middle; B423: below 664
	// lines too (800 x 600 put New Game on the logo), where the pictures are drawn smaller than their pixels
	float fit = ScreenHeight / 664.0f;
	if ( scale > fit )
		scale = fit;
	if ( scale > 4.0f )
		scale = 4.0f;
	return scale;
}

int RcMenuScaled( int value )
{
	return (int)( value * RcMenuScale() + ( value >= 0 ? 0.5f : -0.5f ) );
}

CRcImagePanel::CRcImagePanel( vgui::Image *image ) : vgui::ImagePanel( image )
{
	m_pRcImage = NULL;
	m_iRcTexture = 0;
	m_pRcUploaded = NULL;
	setImage( image );
}

void CRcImagePanel::setImage( vgui::Image *image )
{
	vgui::ImagePanel::setImage( image );
	m_pRcImage = image;
	if ( image )
	{
		int wide, tall;
		image->getSize( wide, tall );
		setSize( RcMenuScaled( wide ), RcMenuScaled( tall ) );
	}
}

void CRcImagePanel::paintBackground()
{
	// the size follows the scale (the panel may be made before the config sets rc_menu_scale, or the mode change)
	if ( m_pRcImage )
	{
		int iw, it, pw, pt;
		m_pRcImage->getSize( iw, it );
		getSize( pw, pt );
		if ( pw != RcMenuScaled( iw ) || pt != RcMenuScaled( it ) )
			setSize( RcMenuScaled( iw ), RcMenuScaled( it ) );
	}

	CRcTGA *pTGA = dynamic_cast<CRcTGA *>( m_pRcImage );
	if ( !pTGA || !pTGA->RcRGBA() || RcMenuScale() == 1.0f )
	{
		vgui::ImagePanel::paintBackground();
		return;
	}

	int wide, tall;
	pTGA->getSize( wide, tall );
	if ( !m_iRcTexture )
		m_iRcTexture = getSurfaceBase()->createNewTextureID();
	if ( m_pRcUploaded != pTGA )
	{
		drawSetTextureRGBA( m_iRcTexture, (const char *)pTGA->RcRGBA(), wide, tall );
		m_pRcUploaded = pTGA;
	}

	vgui::Color color;
	pTGA->getColor( color );
	int r, g, b, a;
	color.getColor( r, g, b, a );
	drawSetColor( r, g, b, a );
	drawSetTexture( m_iRcTexture );
	drawTexturedRect( 0, 0, RcMenuScaled( wide ), RcMenuScaled( tall ) );
}
