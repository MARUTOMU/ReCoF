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


vgui::BitmapTGA* vgui_LoadTGA(char const *pFilename);
vgui::BitmapTGA* vgui_LoadTGANoInvertAlpha(char const *pFilename);
// ReCoF (B393): a TGA file already in memory (as vgui_LoadTGA, alpha inverted for VGUI)
vgui::BitmapTGA* vgui_LoadTGAFromMemory(unsigned char *pData, int len);


#endif // VGUI_LOADTGA_H
