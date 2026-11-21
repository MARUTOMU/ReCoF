//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef VGUI_LOADTGA_H
#define VGUI_LOADTGA_H
#ifdef _WIN32
#pragma once
#endif


#include "vgui_bitmaptga.h"

// ReCoF (B401): every TGA the functions below make; its pixels are open to CRcImagePanel (cl_dll rc_vgui1.cpp), which
// draws a picture at the menus' scale
class CRcTGA : public vgui::BitmapTGA
{
public:
	CRcTGA( vgui::InputStream *is, bool invertAlpha ) : vgui::BitmapTGA( is, invertAlpha ) {}
	const unsigned char *RcRGBA( void ) { return _rgba; }
};


vgui::BitmapTGA* vgui_LoadTGA(char const *pFilename);
vgui::BitmapTGA* vgui_LoadTGANoInvertAlpha(char const *pFilename);
// ReCoF (B393): a TGA file already in memory (as vgui_LoadTGA, alpha inverted for VGUI)
vgui::BitmapTGA* vgui_LoadTGAFromMemory(unsigned char *pData, int len);
// ReCoF (B397): a picture the menus swap in and out under the cursor, loaded once and kept (do not delete it)
vgui::BitmapTGA* vgui_LoadTGACached(char const *pFilename);


#endif // VGUI_LOADTGA_H
