//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "../cl_dll/wrect.h"
#include "../cl_dll/cl_dll.h"
#include "vgui.h"
#include "vgui_loadtga.h"
#include "vgui_inputstream.h"


// ---------------------------------------------------------------------- //
// Helper class for loading tga files.
// ---------------------------------------------------------------------- //
class MemoryInputStream : public vgui::InputStream
{
public:
				MemoryInputStream()
				{
					m_pData = NULL;
					m_DataLen = m_ReadPos = 0;
				}
		
	virtual void  seekStart(bool& success)					{m_ReadPos=0; success=true;}
	virtual void  seekRelative(int count,bool& success)		{m_ReadPos+=count; success=true;}
	virtual void  seekEnd(bool& success)					{m_ReadPos=m_DataLen; success=true;}
	virtual int   getAvailable(bool& success)				{success=false; return 0;} // This is what vgui does for files...
	
	virtual uchar readUChar(bool& success)					
	{
		if(m_ReadPos>=0 && m_ReadPos<m_DataLen)
		{
			success=true;
			uchar ret = m_pData[m_ReadPos];
			++m_ReadPos;
			return ret;
		}
		else
		{
			success=false;
			return 0;
		}
	}

	virtual void  readUChar(uchar* buf,int count,bool& success)
	{
		for(int i=0; i < count; i++)
			buf[i] = readUChar(success);
	}

	virtual void  close(bool& success) 
	{
		m_pData = NULL;
		m_DataLen = m_ReadPos = 0;
	}

	uchar		*m_pData;
	int			m_DataLen;
	int			m_ReadPos;
};

vgui::BitmapTGA* vgui_LoadTGA(char const *pFilename)
{
	MemoryInputStream stream;
	
	stream.m_pData = gEngfuncs.COM_LoadFile((char*)pFilename, 5, &stream.m_DataLen);
	if(!stream.m_pData)
		return NULL;
	
	stream.m_ReadPos = 0;
	vgui::BitmapTGA *pRet = new CRcTGA(&stream, true);
	gEngfuncs.COM_FreeFile(stream.m_pData);
	
	return pRet;
}

// ReCoF (B393): a TGA file already in memory (the main menu's Story Co-op label, client.dll rc_coopmenu.cpp)
vgui::BitmapTGA* vgui_LoadTGAFromMemory(unsigned char *pData, int len)
{
	MemoryInputStream stream;

	stream.m_pData = pData;
	stream.m_DataLen = len;
	stream.m_ReadPos = 0;
	return new CRcTGA(&stream, true);
}

// ReCoF (B397): the picture menus' hover pictures, each loaded once (retail loaded one at every hover and never freed
// the one it replaced)
vgui::BitmapTGA* vgui_LoadTGACached(char const *pFilename)
{
	static char s_szName[64][64];
	static vgui::BitmapTGA *s_pImage[64];
	static int s_nImages;
	int i;

	for ( i = 0; i < s_nImages; i++ )
	{
		if ( !stricmp( s_szName[i], pFilename ) )
			return s_pImage[i];
	}
	vgui::BitmapTGA *pImage = vgui_LoadTGA( pFilename );
	if ( pImage && s_nImages < 64 && strlen( pFilename ) < 64 )
	{
		strcpy( s_szName[s_nImages], pFilename );
		s_pImage[s_nImages++] = pImage;
	}
	return pImage;
}

vgui::BitmapTGA* vgui_LoadTGANoInvertAlpha(char const *pFilename)
{
	MemoryInputStream stream;
	
	stream.m_pData = gEngfuncs.COM_LoadFile((char*)pFilename, 5, &stream.m_DataLen);
	if(!stream.m_pData)
		return NULL;
	
	stream.m_ReadPos = 0;
	vgui::BitmapTGA *pRet = new CRcTGA(&stream, false);
	gEngfuncs.COM_FreeFile(stream.m_pData);
	
	return pRet;
}
