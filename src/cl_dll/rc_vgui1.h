// ReCoF (B401): Cry of Fear's own picture screens (VGUI1) at the screen's size
#ifndef RC_VGUI1_H
#define RC_VGUI1_H

#include "VGUI_ImagePanel.h"

// the size of the picture menus: the screen's height over 720 (at least 1, so 720p and below are as before), times
// the player's Menu size (rc_menu_scale, 100 to 200 percent), held where the main menu still fits
float RcMenuScale( void );
int RcMenuScaled( int value );

// an ImagePanel that draws its picture at RcMenuScale (the texture stretched, Cry of Fear's own art); at 1 it is
// the retail ImagePanel
class CRcImagePanel : public vgui::ImagePanel
{
public:
	CRcImagePanel( vgui::Image *image );
	virtual void setImage( vgui::Image *image );

protected:
	virtual void paintBackground();

private:
	vgui::Image	*m_pRcImage;
	int			m_iRcTexture;	// the texture this panel uploads its picture to (once per picture)
	vgui::Image	*m_pRcUploaded;
};

#endif
