//
// written by BUzer for HL: Paranoia modification
//
//		2006

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "triangleapi.h"
#include "com_model.h"
#include "studio_util.h"
#include "glmanager.h"
#include "gl_texloader.h"
#include "rc_prof.h"	// ReCoF

#define DEAD_GRAYSCALE_TIME		5


cvar_t *v_posteffects;
cvar_t *v_grayscale;
int g_iWaterOldTexture_guess;	// texture binding saved and restored by CCgWater_guess::Render_guess
int iGreyFade_guess;		// GreyFade message (fade time in seconds, 0: DEAD_GRAYSCALE_TIME)
extern cvar_t *cv_specular_nocombiners;
extern cl_entity_t *pAttachCamEnt_guess;
extern int iAttachCamFlag_guess;
extern int iBoat_guess;
extern int g_iHeadShieldType_guess;
extern int g_iDickMode_guess;



float GetGrayscaleFactor()
{
	float grayscale = v_grayscale->value;
	// ReCoF (B172): retail left alpha unset unless black-and-white was on, so a fade back from grey
	// started from whatever the stack held; the maps' from_grey fades expect full grey
	float alpha = 255;
	bool attachcam = false;

	if (pAttachCamEnt_guess)
		attachcam = (iAttachCamFlag_guess == 0);

	if ((gHUD.m_iFlashFlags_guess & 0x40) && !IsViewEntityActive_guess() && !attachcam)
		grayscale = 0.6;

	if (gHUD.m_iBlackWhite_guess)
	{
		alpha = gHUD.m_iBlackWhite_guess;
		grayscale = alpha / 255;
	}

	if (g_iHeadShieldType_guess == 1)
		grayscale = 0.25;

	if (gHUD.m_flGreyFadeStart_guess)
	{
		float fact;
		if (iGreyFade_guess)
			fact = (gEngfuncs.GetClientTime() - gHUD.m_flGreyFadeStart_guess) / iGreyFade_guess;
		else
			fact = (gEngfuncs.GetClientTime() - gHUD.m_flGreyFadeStart_guess) / DEAD_GRAYSCALE_TIME;
		if (fact > 1) fact = 1;
		if (fact > grayscale) grayscale = fact;
	}

	if (gHUD.m_flGreyFadeEnd_guess)
	{
		alpha /= 255;
		float fact;
		if (iGreyFade_guess)
			fact = (gEngfuncs.GetClientTime() - gHUD.m_flGreyFadeEnd_guess) / iGreyFade_guess;
		else
			fact = (gEngfuncs.GetClientTime() - gHUD.m_flGreyFadeEnd_guess) / DEAD_GRAYSCALE_TIME;
		if (fact > 1) fact = 1;
		grayscale = alpha - fact * alpha;
		if (grayscale < 0) grayscale = 0;
	}

	if (iBoat_guess)
		grayscale = 0;

	return grayscale;
}


int g_screen_texture = 0;
int g_weights_texture = 0; //for cards with no register combiners support
bool bReinitScreenTexture_guess;

int UseRectangleTextures()
{
	if (gl.texture_rectangle_supported &&
		gl.texture_rectangle_max_size >= ScreenWidth &&
		gl.texture_rectangle_max_size >= ScreenHeight)
		return TRUE;

	return FALSE;
}

// ReCoF (B307): the rectangle screen texture has the screen's size; after a video mode change (rc_vidrebuild) it
// is allocated again at the new size under the same name. The 256x256 tiles of the other path do not depend on
// the screen
void RcResizeScreenTexture()
{
	if (!g_screen_texture || !UseRectangleTextures())
		return;

	gl.glEnable(GL_TEXTURE_RECTANGLE_NV);
	gl.glBindTexture(GL_TEXTURE_RECTANGLE_NV, g_screen_texture);
	gl.glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, 3, ScreenWidth, ScreenHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	gl.glDisable(GL_TEXTURE_RECTANGLE_NV);
}


void InitScreenTexture()
{
	if (g_screen_texture)
		return;
	
	if (!UseRectangleTextures())
	{
		// just create 256x256 texture
		unsigned char* pBlankTex = new unsigned char[256*256*3];
		memset(pBlankTex, 0, 256*256*3);
		int oldbinding;
		gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldbinding);
		gl.glBindTexture(GL_TEXTURE_2D, current_ext_texture_id);
		gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		gl.glTexImage2D (GL_TEXTURE_2D, 0, 3, 256, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, pBlankTex);
		gl.glBindTexture(GL_TEXTURE_2D, oldbinding);
		delete[] pBlankTex;
		g_screen_texture = current_ext_texture_id;
		current_ext_texture_id++;
		Log("Grayscale: created 256x256 2D texture\n");
		return;
	}

	unsigned char* pBlankTex = new unsigned char[ScreenWidth*ScreenHeight*3];
	memset(pBlankTex, 255, ScreenWidth*ScreenHeight*3);

	gl.glEnable(GL_TEXTURE_RECTANGLE_NV);
	gl.glBindTexture(GL_TEXTURE_RECTANGLE_NV, current_ext_texture_id);
	gl.glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	gl.glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gl.glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, 3, ScreenWidth, ScreenHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, pBlankTex);
	gl.glDisable(GL_TEXTURE_RECTANGLE_NV);
	Log("Grayscale: created %dx%d rectangle texture\n", ScreenWidth, ScreenHeight);

	g_screen_texture = current_ext_texture_id;
	current_ext_texture_id++;
	delete[] pBlankTex;
}


void MakeWeightsTexture()
{
	if (g_weights_texture)
		return;

	unsigned char buf[16*16*3];
	for (int i=0; i<(16*16); i++)
	{
		buf[3*i+0] = 168;
		buf[3*i+1] = 202;
		buf[3*i+2] = 139;
	}

	int oldbinding;
	gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldbinding);
	gl.glBindTexture(GL_TEXTURE_2D, current_ext_texture_id);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gl.glTexImage2D (GL_TEXTURE_2D, 0, 3, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, buf);
	gl.glBindTexture(GL_TEXTURE_2D, oldbinding);
	g_weights_texture = current_ext_texture_id;
	current_ext_texture_id++;
	Log("Grayscale: created weights texture\n");
}


void DrawQuad(int width, int height, int ofsX = 0, int ofsY = 0)
{
	gl.glTexCoord2f(ofsX,ofsY);
	gl.glVertex3f(0, 1, -1);
	gl.glTexCoord2f(ofsX,height+ofsY);
	gl.glVertex3f(0, 0, -1);
	gl.glTexCoord2f(width+ofsX,height+ofsY);
	gl.glVertex3f(1, 0, -1);
	gl.glTexCoord2f(width+ofsX,ofsY);
	gl.glVertex3f(1, 1, -1);
}



void ApplyPostEffects()
{
	RcProfScope prof( RCP_POST );	// ReCoF

	if (g_iDickMode_guess)
	{
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0, 1, 1, 0, 0.1f, 100);

		glDisable(GL_DEPTH_TEST);
		glCullFace(GL_NONE);
		glDisable(GL_CULL_FACE);
		glDisable(GL_TEXTURE_2D);
		glColor4f(gEngfuncs.pfnRandomFloat(0, 1), gEngfuncs.pfnRandomFloat(0, 1), gEngfuncs.pfnRandomFloat(0, 1), 1);

		glBegin(GL_QUADS);
		glVertex3f(0, 1, -1);
		glVertex3f(1, 1, -1);
		glVertex3f(1, 0, -1);
		glVertex3f(0, 0, -1);
		glEnd();

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_TEXTURE_2D);
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
	}

	if (bReinitScreenTexture_guess)
	{
		InitScreenTexture();
		bReinitScreenTexture_guess = false;
	}

	if (!gl.IsGLAllowed() || !v_posteffects->value)
		return;

	float grayscale = GetGrayscaleFactor();
	if (grayscale <= 0 || grayscale > 1)
		return;

	InitScreenTexture();
	int texenvmode1, texenvmode2, oldbinding1, oldbinding2;

	// setup ortho projection
	gl.glMatrixMode(GL_MODELVIEW);
	gl.glPushMatrix();
	gl.glLoadIdentity();
	gl.glMatrixMode(GL_PROJECTION);
	gl.glPushMatrix();
	gl.glLoadIdentity();
	gl.glOrtho(0, 1, 1, 0, 0.1, 100);

	int blendenabled = gl.glIsEnabled(GL_BLEND);
	gl.glEnable(GL_BLEND);
	gl.glColor4f(1, 1, 1, grayscale);
	gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//
	// Setup grayscale shader
	//
	if (gl.NV_combiners_supported && !cv_specular_nocombiners->value)
	{
		// use combiners
		GLfloat grayscale_weights[] = { 0.320000, 0.590000, 0.090000, 0.000000 };
		gl.glCombinerParameteriNV(GL_NUM_GENERAL_COMBINERS_NV, 1);
		
		// RC 1 setup: 
		// spare0.rgb = dot(tex0.rgb, {0.32, 0.59, 0.09})
		gl.glCombinerParameterfvNV(GL_CONSTANT_COLOR0_NV, grayscale_weights);
		gl.glCombinerInputNV (	GL_COMBINER0_NV, GL_RGB, GL_VARIABLE_A_NV, GL_CONSTANT_COLOR0_NV,
								GL_SIGNED_IDENTITY_NV, GL_RGB);
		gl.glCombinerInputNV (	GL_COMBINER0_NV, GL_RGB, GL_VARIABLE_B_NV, GL_TEXTURE0_ARB,
								GL_SIGNED_IDENTITY_NV, GL_RGB);
		gl.glCombinerOutputNV(	GL_COMBINER0_NV, GL_RGB,
								GL_SPARE0_NV,          // AB output
								GL_DISCARD_NV,         // CD output
								GL_DISCARD_NV,         // sum output
								GL_NONE, GL_NONE,
								GL_TRUE,               // AB = A dot B
								GL_FALSE, GL_FALSE );

		// Final RC setup:
		//	out.rgb = spare0.rgb
		//	out.a = spare0.a
		gl.glFinalCombinerInputNV ( GL_VARIABLE_A_NV, GL_ZERO,
									GL_UNSIGNED_IDENTITY_NV, GL_RGB );
		gl.glFinalCombinerInputNV ( GL_VARIABLE_B_NV, GL_ZERO,
									GL_UNSIGNED_IDENTITY_NV, GL_RGB );
		gl.glFinalCombinerInputNV ( GL_VARIABLE_C_NV, GL_ZERO,
									GL_UNSIGNED_IDENTITY_NV, GL_RGB );
		gl.glFinalCombinerInputNV ( GL_VARIABLE_D_NV, GL_SPARE0_NV,
									GL_UNSIGNED_IDENTITY_NV, GL_RGB );
		gl.glFinalCombinerInputNV ( GL_VARIABLE_E_NV, GL_ZERO,
									GL_UNSIGNED_IDENTITY_NV, GL_RGB );
		gl.glFinalCombinerInputNV ( GL_VARIABLE_F_NV, GL_ZERO,
									GL_UNSIGNED_IDENTITY_NV, GL_RGB );
		gl.glFinalCombinerInputNV ( GL_VARIABLE_G_NV, GL_PRIMARY_COLOR_NV,
									GL_UNSIGNED_IDENTITY_NV, GL_ALPHA );

		gl.glEnable(GL_REGISTER_COMBINERS_NV);
	}
	else
	{
		// use env_dot3
		MakeWeightsTexture();

		// 1st TU:
		// out = texture * 0.5 + 0.5
		gl.glActiveTextureARB(GL_TEXTURE0_ARB);
		gl.glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &texenvmode1);

		GLfloat temp[] = { 0.5, 0.5, 0.5, 0.5 };
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
		gl.glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, temp);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_INTERPOLATE_ARB);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB); // {1, 1, 1}
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_CONSTANT_ARB); // {0.5, 0.5, 0.5}
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1);

		// 2nd TU:
		// make dot3
		gl.glActiveTextureARB(GL_TEXTURE1_ARB);
		gl.glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &texenvmode2);
		gl.glEnable(GL_TEXTURE_2D);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_DOT3_RGB_ARB);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1);

		gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldbinding2);
		gl.glBindTexture(GL_TEXTURE_2D, g_weights_texture);

		gl.glActiveTextureARB(GL_TEXTURE0_ARB);
	}

	// copy screen to texture
	if (UseRectangleTextures())
	{
		gl.glEnable(GL_TEXTURE_RECTANGLE_NV);
		gl.glBindTexture(GL_TEXTURE_RECTANGLE_NV, g_screen_texture);
	//	gl.glCopyTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGB, 0, 0, ScreenWidth, ScreenHeight, 0);
		gl.glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_NV, 0, 0, 0, 0, 0, ScreenWidth, ScreenHeight);

		gl.glBegin(GL_QUADS);
		DrawQuad(ScreenWidth, ScreenHeight);
		gl.glEnd();

		gl.glDisable(GL_TEXTURE_RECTANGLE_NV);
	}
	else
	{
		gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldbinding1);
		gl.glBindTexture(GL_TEXTURE_2D, g_screen_texture);

		int ofsy2 = ScreenHeight;
		for(int ofsy = 0; ofsy < ScreenHeight; ofsy+=256, ofsy2-=256)
		{
			for(int ofsx = 0; ofsx < ScreenWidth; ofsx+=256)
			{
				gl.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ofsx, ofsy, 256, 256);
				gl.glBegin(GL_QUADS);
					gl.glTexCoord2f(0,0);
					gl.glVertex3f(ofsx/(float)ScreenWidth, ofsy2/(float)ScreenHeight, -1);
	
					gl.glTexCoord2f(1,0);
					gl.glVertex3f((ofsx+256)/(float)ScreenWidth, ofsy2/(float)ScreenHeight, -1);
					
					gl.glTexCoord2f(1,1);
					gl.glVertex3f((ofsx+256)/(float)ScreenWidth, (ofsy2-256)/(float)ScreenHeight, -1);
					
					gl.glTexCoord2f(0,1);
					gl.glVertex3f(ofsx/(float)ScreenWidth, (ofsy2-256)/(float)ScreenHeight, -1);
				gl.glEnd();
			}
		}

		gl.glBindTexture(GL_TEXTURE_2D, oldbinding1);
	}

	if (gl.NV_combiners_supported && !cv_specular_nocombiners->value)
	{
		gl.glDisable(GL_REGISTER_COMBINERS_NV);
	}
	else
	{
		gl.glActiveTextureARB(GL_TEXTURE1_ARB);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, texenvmode2);
		gl.glBindTexture(GL_TEXTURE_2D, oldbinding2);
		gl.glDisable(GL_TEXTURE_2D);
		gl.glActiveTextureARB(GL_TEXTURE0_ARB);
		gl.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, texenvmode1);
	}

	gl.glMatrixMode(GL_PROJECTION);
	gl.glPopMatrix();
	gl.glMatrixMode(GL_MODELVIEW);
	gl.glPopMatrix();

	if (!blendenabled) gl.glDisable(GL_BLEND);
}



void InitPostEffects()
{
	v_posteffects = gEngfuncs.pfnRegisterVariable( "gl_posteffects", "1", FCVAR_ARCHIVE );	// ReCoF (B306): the Advanced page sets it, so it is kept
	v_grayscale = gEngfuncs.pfnRegisterVariable( "gl_grayscale", "0", 0 );
}


//===============
// ReCoF (B308): the vignette
//
// Cry of Fear 1.0 to 1.55 darkened the four corners of the screen with gfx/vgui/vignette/vignette_tl, _tr, _bl and
// _br.tga (256 x 256, black, 85% opaque in the corner and clear towards the middle): four VGUI image panels of the
// HUD, always shown. 1.6a added the Vignette option (cl_vignette) and creates the panels hidden, but no code reads the
// option or shows them, so the effect was gone and the option did nothing. rc_vignette 1 draws the four images again,
// after the post effects and under the HUD, each corner scaled to the screen: 256 pixels at 768 lines, as 1.55 looked
// at 1024 x 768. A VGUI image keeps its own size, so the panels covered less and less of a bigger screen.
//===============
extern cvar_t *rc_vignette;			// input.cpp
extern int current_ext_texture_id;	// gl_texloader.cpp

static GLuint s_uiRcVignette[4];	// top left, top right, bottom left, bottom right
static int s_iRcVignetteState;		// 0 not loaded yet, 1 loaded, -1 an image could not be loaded

// uploads an uncompressed 24 or 32 bit TGA as RGBA, rows top first (CreateTexture only takes 24 bit ones)
static bool RcVignetteUpload( const char *name, GLuint id )
{
	int length = 0;
	byte *file = (byte *)gEngfuncs.COM_LoadFile( (char *)name, 5, &length );
	if ( !file )
		return false;

	bool ok = false;
	if ( length >= 18 )
	{
		int idlen = file[0], type = file[2], bpp = file[16], desc = file[17];
		int w = file[12] | ( file[13] << 8 ), h = file[14] | ( file[15] << 8 );
		int bytes = bpp / 8;
		if ( type == 2 && ( bpp == 24 || bpp == 32 ) && w > 0 && h > 0 && w <= 1024 && h <= 1024
			&& 18 + idlen + w * h * bytes <= length )
		{
			byte *rgba = new byte[w * h * 4];
			const byte *pixels = file + 18 + idlen;
			for ( int y = 0; y < h; y++ )
			{
				// a TGA keeps its rows bottom first unless bit 5 of the descriptor says top first
				const byte *in = pixels + ( ( desc & 0x20 ) ? y : h - 1 - y ) * w * bytes;
				byte *out = rgba + y * w * 4;
				for ( int x = 0; x < w; x++, in += bytes, out += 4 )
				{
					out[0] = in[2];
					out[1] = in[1];
					out[2] = in[0];
					out[3] = bytes == 4 ? in[3] : 255;
				}
			}
			gl.glBindTexture( GL_TEXTURE_2D, id );
			gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			gl.glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba );
			delete[] rgba;
			ok = true;
		}
	}
	gEngfuncs.COM_FreeFile( file );
	return ok;
}

void RcDrawVignette( void )
{
	if ( !rc_vignette || !rc_vignette->value || s_iRcVignetteState < 0 || !gl.IsGLAllowed() )
		return;

	int w = ScreenWidth, h = ScreenHeight;
	int size = ( h * 256 + 384 ) / 768;
	if ( size > w / 2 )
		size = w / 2;
	if ( size > h / 2 )
		size = h / 2;
	if ( size < 1 )
		return;

	// everything this changes is put back: the engine and the renderer keep their own record of the bound textures
	GLint unit = GL_TEXTURE0_ARB, oldtexture = 0;
	gl.glGetIntegerv( GL_ACTIVE_TEXTURE_ARB, &unit );
	if ( unit != GL_TEXTURE0_ARB )
		gl.glActiveTextureARB( GL_TEXTURE0_ARB );
	gl.glGetIntegerv( GL_TEXTURE_BINDING_2D, &oldtexture );
	gl.glPushAttrib( GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_TRANSFORM_BIT );

	// loaded the first time, and again when the renderer was restarted (a new context has none of the textures)
	if ( !s_iRcVignetteState || !glIsTexture( s_uiRcVignette[0] ) )
	{
		static const char *s_szNames[4] =
		{
			"gfx/vgui/vignette/vignette_tl.tga", "gfx/vgui/vignette/vignette_tr.tga",
			"gfx/vgui/vignette/vignette_bl.tga", "gfx/vgui/vignette/vignette_br.tga"
		};
		s_iRcVignetteState = 1;
		for ( int i = 0; i < 4; i++ )
		{
			if ( !s_uiRcVignette[i] )
				s_uiRcVignette[i] = current_ext_texture_id++;
			if ( !RcVignetteUpload( s_szNames[i], s_uiRcVignette[i] ) )
			{
				gEngfuncs.Con_Printf( "rc_vignette: cannot load %s\n", s_szNames[i] );
				s_iRcVignetteState = -1;
				break;
			}
		}
	}

	if ( s_iRcVignetteState > 0 )
	{
		gl.glMatrixMode( GL_PROJECTION );
		gl.glPushMatrix();
		gl.glLoadIdentity();
		gl.glOrtho( 0, w, h, 0, -1, 1 );
		gl.glMatrixMode( GL_MODELVIEW );
		gl.glPushMatrix();
		gl.glLoadIdentity();

		gl.glDisable( GL_DEPTH_TEST );
		gl.glDisable( GL_CULL_FACE );
		gl.glDisable( GL_ALPHA_TEST );
		gl.glDisable( GL_FOG );
		gl.glEnable( GL_TEXTURE_2D );
		gl.glEnable( GL_BLEND );
		gl.glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		gl.glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		gl.glColor4f( 1, 1, 1, 1 );

		const int x[4] = { 0, w - size, 0, w - size };
		const int y[4] = { 0, 0, h - size, h - size };
		for ( int i = 0; i < 4; i++ )
		{
			gl.glBindTexture( GL_TEXTURE_2D, s_uiRcVignette[i] );
			gl.glBegin( GL_QUADS );
			gl.glTexCoord2f( 0, 0 );
			gl.glVertex3f( x[i], y[i], 0 );
			gl.glTexCoord2f( 1, 0 );
			gl.glVertex3f( x[i] + size, y[i], 0 );
			gl.glTexCoord2f( 1, 1 );
			gl.glVertex3f( x[i] + size, y[i] + size, 0 );
			gl.glTexCoord2f( 0, 1 );
			gl.glVertex3f( x[i], y[i] + size, 0 );
			gl.glEnd();
		}

		gl.glMatrixMode( GL_PROJECTION );
		gl.glPopMatrix();
		gl.glMatrixMode( GL_MODELVIEW );
		gl.glPopMatrix();
	}

	gl.glPopAttrib();
	gl.glBindTexture( GL_TEXTURE_2D, oldtexture );
	if ( unit != GL_TEXTURE0_ARB )
		gl.glActiveTextureARB( unit );
}
