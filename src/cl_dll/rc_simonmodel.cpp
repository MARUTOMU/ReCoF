// ReCoF story co-op (B414): Simon's player model, made on this computer from the game's own files.
//
// Cry of Fear has no Simon model for players: its player models are the four police officers (models/player.mdl is
// police1 too), and Simon exists as models/cutscene/simon_clothes.mdl, a scene model with one sequence. Both are the
// same Biped skeleton in the same reference pose (every joint they share is within 0.01 units), compiled with other
// bone axes and with extra bones in Simon's (face, finger and toe tips, the bag). The model made here is police1.mdl
// with its skeleton, animations, hitboxes and attachments as they are, and Simon's body, textures and bag in place of
// the officer's: each of Simon's vertices and normals goes through his reference pose into world space and from there
// into the frame of the police bone of the same name (a bone police1 lacks gives its vertices to the nearest one it
// hangs from; the bag, which hangs from the root, rides the pelvis at its height).
//
// Nothing from the game is shipped: the file is made at the first start from the player's own copies, written to
// <game>/models/player/simon/simon.mdl, and made again when it is missing or not this version's. The same game files
// give the same bytes on every computer. hl.dll gives the players the model "simon" in the main campaign's story co-op
// (rc_coop.cpp, rc_coop_simon); a game without the file draws models/player.mdl, as for any missing player model.
#include "hud.h"
#include "cl_util.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define RC_SIMON_VERSION	1	// in the header's unused soundtable field: a file of another version is made again

static int RcI( const unsigned char *d, int ofs )
{
	int v;
	memcpy( &v, d + ofs, 4 );
	return v;
}

static void RcSetI( unsigned char *d, int ofs, int v )
{
	memcpy( d + ofs, &v, 4 );
}

static float RcF( const unsigned char *d, int ofs )
{
	float v;
	memcpy( &v, d + ofs, 4 );
	return v;
}

static void RcSetF( unsigned char *d, int ofs, float v )
{
	memcpy( d + ofs, &v, 4 );
}

// studiohdr_t fields (version 10)
enum
{
	H_LENGTH = 72, H_NUMBONES = 140, H_BONEINDEX = 144, H_NUMTEXTURES = 180, H_TEXTUREINDEX = 184, H_TEXTUREDATAINDEX = 188,
	H_NUMSKINREF = 192, H_NUMSKINFAMILIES = 196, H_SKININDEX = 200, H_NUMBODYPARTS = 204, H_BODYPARTINDEX = 208,
	H_SOUNDTABLE = 220, H_SIZE = 244,
};

#define RC_MAX_BONES	128

// a bone's reference pose in world space (GoldSrc's AngleQuaternion and QuaternionMatrix, chained to the parents)
static void RcBoneMatrices( const unsigned char *d, float m[][3][4] )
{
	int nb = RcI( d, H_NUMBONES ), bi = RcI( d, H_BONEINDEX );
	for ( int i = 0; i < nb && i < RC_MAX_BONES; i++ )
	{
		int o = bi + i * 112;
		int parent = RcI( d, o + 32 );
		float p[3], a[3];
		for ( int k = 0; k < 3; k++ )
		{
			p[k] = RcF( d, o + 64 + k * 4 );
			a[k] = RcF( d, o + 76 + k * 4 );
		}
		double sy = sin( a[2] * 0.5 ), cy = cos( a[2] * 0.5 ), sp = sin( a[1] * 0.5 ), cp = cos( a[1] * 0.5 ), sr = sin( a[0] * 0.5 ), cr = cos( a[0] * 0.5 );
		double x = sr * cp * cy - cr * sp * sy, y = cr * sp * cy + sr * cp * sy, z = cr * cp * sy - sr * sp * cy, w = cr * cp * cy + sr * sp * sy;
		float l[3][4] =
		{
			{ (float)( 1 - 2 * y * y - 2 * z * z ), (float)( 2 * x * y - 2 * w * z ), (float)( 2 * x * z + 2 * w * y ), p[0] },
			{ (float)( 2 * x * y + 2 * w * z ), (float)( 1 - 2 * x * x - 2 * z * z ), (float)( 2 * y * z - 2 * w * x ), p[1] },
			{ (float)( 2 * x * z - 2 * w * y ), (float)( 2 * y * z + 2 * w * x ), (float)( 1 - 2 * x * x - 2 * y * y ), p[2] },
		};
		if ( parent < 0 || parent >= i )
		{
			memcpy( m[i], l, sizeof( l ) );
			continue;
		}
		for ( int r = 0; r < 3; r++ )
		{
			for ( int c = 0; c < 4; c++ )
			{
				m[i][r][c] = m[parent][r][0] * l[0][c] + m[parent][r][1] * l[1][c] + m[parent][r][2] * l[2][c] + ( c == 3 ? m[parent][r][3] : 0 );
			}
		}
	}
}

static const char *RcBoneName( const unsigned char *d, int i )
{
	return (const char *)d + RcI( d, H_BONEINDEX ) + i * 112;
}

static int RcFindBone( const unsigned char *d, const char *name )
{
	int nb = RcI( d, H_NUMBONES );
	for ( int i = 0; i < nb; i++ )
	{
		if ( !strncmp( RcBoneName( d, i ), name, 32 ) )
			return i;
	}
	return -1;
}

static bool RcHeaderOk( const unsigned char *d, int len )
{
	if ( len < H_SIZE || memcmp( d, "IDST", 4 ) || RcI( d, 4 ) != 10 || RcI( d, H_LENGTH ) != len )
		return false;
	int nb = RcI( d, H_NUMBONES );
	return nb > 0 && nb <= RC_MAX_BONES && RcI( d, H_NUMBODYPARTS ) > 0 && RcI( d, H_BODYPARTINDEX ) > H_SIZE &&
		RcI( d, H_BODYPARTINDEX ) < len && RcI( d, H_TEXTUREINDEX ) > RcI( d, H_BODYPARTINDEX ) && RcI( d, H_TEXTUREDATAINDEX ) <= len;
}

// the model, from police1 (pp) and simon_clothes (ps); NULL when the files are not what this code knows
static unsigned char *RcBuildSimon( const unsigned char *pp, int lp, const unsigned char *ps, int ls, int *pLen )
{
	if ( !RcHeaderOk( pp, lp ) || !RcHeaderOk( ps, ls ) )
		return NULL;
	int pbp = RcI( pp, H_BODYPARTINDEX ), sbp = RcI( ps, H_BODYPARTINDEX );
	// police1's bones, sequences and the rest come before its body parts; Simon's body parts, textures, skins and
	// texture data are everything from his body parts on
	if ( RcI( pp, H_BONEINDEX ) >= pbp || RcI( ps, H_SKININDEX ) < sbp )
		return NULL;
	int delta = pbp - sbp;
	int len = pbp + ( ls - sbp );
	unsigned char *d = new unsigned char[len];
	memcpy( d, pp, pbp );
	memcpy( d + pbp, ps + sbp, ls - sbp );

	RcSetI( d, H_LENGTH, len );
	memset( d + 8, 0, 64 );
	strcpy( (char *)d + 8, "simon.mdl" );
	RcSetI( d, H_NUMTEXTURES, RcI( ps, H_NUMTEXTURES ) );
	RcSetI( d, H_TEXTUREINDEX, RcI( ps, H_TEXTUREINDEX ) + delta );
	RcSetI( d, H_TEXTUREDATAINDEX, RcI( ps, H_TEXTUREDATAINDEX ) + delta );
	RcSetI( d, H_NUMSKINREF, RcI( ps, H_NUMSKINREF ) );
	RcSetI( d, H_NUMSKINFAMILIES, RcI( ps, H_NUMSKINFAMILIES ) );
	RcSetI( d, H_SKININDEX, RcI( ps, H_SKININDEX ) + delta );
	RcSetI( d, H_NUMBODYPARTS, RcI( ps, H_NUMBODYPARTS ) );
	RcSetI( d, H_BODYPARTINDEX, sbp + delta );
	RcSetI( d, H_SOUNDTABLE, RC_SIMON_VERSION );

	int nt = RcI( d, H_NUMTEXTURES ), ti = RcI( d, H_TEXTUREINDEX );
	for ( int t = 0; t < nt; t++ )
		RcSetI( d, ti + t * 80 + 76, RcI( d, ti + t * 80 + 76 ) + delta );

	// Simon's bones, each given the police bone his vertices go to
	static float ms[RC_MAX_BONES][3][4], mp[RC_MAX_BONES][3][4];
	RcBoneMatrices( ps, ms );
	RcBoneMatrices( pp, mp );
	int nbs = RcI( ps, H_NUMBONES ), bsi = RcI( ps, H_BONEINDEX );
	int map[RC_MAX_BONES];
	int iPelvis = RcFindBone( pp, "Bip01 Pelvis" );
	for ( int b = 0; b < nbs; b++ )
	{
		int s = b, p = -1;
		while ( s >= 0 && ( p = RcFindBone( pp, RcBoneName( ps, s ) ) ) < 0 )
			s = RcI( ps, bsi + s * 112 + 32 );
		if ( p < 0 || ( p == 0 && b != 0 && iPelvis >= 0 ) )
			p = iPelvis >= 0 ? iPelvis : 0;	// the root moves the whole body: the bag rides the pelvis
		map[b] = p;
	}

	int nbp = RcI( d, H_NUMBODYPARTS ), bpi = RcI( d, H_BODYPARTINDEX );
	for ( int part = 0; part < nbp; part++ )
	{
		int po = bpi + part * 76;
		int nm = RcI( d, po + 64 ), mi = RcI( d, po + 72 ) + delta;
		RcSetI( d, po + 72, mi );
		for ( int m = 0; m < nm; m++ )
		{
			int mo = mi + m * 112;
			// meshindex, vertinfoindex, vertindex, norminfoindex, normindex, groupindex (the counts between stay)
			static const int s_iOffsets[6] = { 76, 84, 88, 96, 100, 108 };
			for ( int f = 0; f < 6; f++ )
			{
				if ( RcI( d, mo + s_iOffsets[f] ) )
					RcSetI( d, mo + s_iOffsets[f], RcI( d, mo + s_iOffsets[f] ) + delta );
			}
			int nmesh = RcI( d, mo + 72 ), meshi = RcI( d, mo + 76 );
			for ( int k = 0; k < nmesh; k++ )
				RcSetI( d, meshi + k * 20 + 4, RcI( d, meshi + k * 20 + 4 ) + delta );
			// vertices (bTranslate) and normals onto the police bones
			for ( int pass = 0; pass < 2; pass++ )
			{
				int n = RcI( d, mo + ( pass ? 92 : 80 ) ), info = RcI( d, mo + ( pass ? 96 : 84 ) ), data = RcI( d, mo + ( pass ? 100 : 88 ) );
				for ( int v = 0; v < n; v++ )
				{
					int bs = d[info + v];
					if ( bs >= nbs )
						continue;
					int bp = map[bs];
					float in[3], w[3], out[3];
					for ( int k = 0; k < 3; k++ )
						in[k] = RcF( d, data + v * 12 + k * 4 );
					for ( int r = 0; r < 3; r++ )
						w[r] = ms[bs][r][0] * in[0] + ms[bs][r][1] * in[1] + ms[bs][r][2] * in[2] + ( pass ? 0 : ms[bs][r][3] );
					if ( !pass )
					{
						for ( int r = 0; r < 3; r++ )
							w[r] -= mp[bp][r][3];
					}
					for ( int c = 0; c < 3; c++ )
						out[c] = mp[bp][0][c] * w[0] + mp[bp][1][c] * w[1] + mp[bp][2][c] * w[2];	// the transpose: the inverse rotation
					for ( int k = 0; k < 3; k++ )
						RcSetF( d, data + v * 12 + k * 4, out[k] );
					d[info + v] = (unsigned char)bp;
				}
			}
		}
	}
	*pLen = len;
	return d;
}

// <cof.exe's folder>\<game>\<rel>, with backslashes
static void RcGamePath( const char *rel, char *out, int len )
{
	char exe[MAX_PATH];
	GetModuleFileNameA( NULL, exe, sizeof( exe ) );
	char *slash = strrchr( exe, '\\' );
	if ( slash )
		*slash = 0;
	_snprintf( out, len - 1, "%s\\%s\\%s", exe, gEngfuncs.pfnGetGameDirectory(), rel );
	out[len - 1] = 0;
	for ( char *p = out; *p; p++ )
	{
		if ( *p == '/' )
			*p = '\\';
	}
}

// the model is there and of this version
static bool RcSimonPresent( const char *path )
{
	FILE *f = fopen( path, "rb" );
	if ( !f )
		return false;
	unsigned char h[H_SIZE];
	bool ok = fread( h, 1, H_SIZE, f ) == H_SIZE && !memcmp( h, "IDST", 4 ) && RcI( h, H_SOUNDTABLE ) == RC_SIMON_VERSION &&
		!strcmp( (char *)h + 8, "simon.mdl" );
	fclose( f );
	return ok;
}

// HUD_VidInit, once: makes models/player/simon/simon.mdl when it is missing
void RcSimonModel_Ensure( void )
{
	static bool s_bDone;
	if ( s_bDone )
		return;
	s_bDone = true;
	char path[MAX_PATH];
	RcGamePath( "models/player/simon/simon.mdl", path, sizeof( path ) );
	if ( RcSimonPresent( path ) )
		return;
	int lp = 0, ls = 0;
	unsigned char *pp = gEngfuncs.COM_LoadFile( "models/player/police1/police1.mdl", 5, &lp );
	unsigned char *ps = gEngfuncs.COM_LoadFile( "models/cutscene/simon_clothes.mdl", 5, &ls );
	int len = 0;
	unsigned char *d = ( pp && ps ) ? RcBuildSimon( pp, lp, ps, ls, &len ) : NULL;
	if ( pp )
		gEngfuncs.COM_FreeFile( pp );
	if ( ps )
		gEngfuncs.COM_FreeFile( ps );
	if ( !d )
	{
		gEngfuncs.Con_Printf( "ReCoF: Simon's player model was not made (police1.mdl or cutscene/simon_clothes.mdl is missing or not as expected)\n" );
		return;
	}
	char dir[MAX_PATH];
	RcGamePath( "models/player", dir, sizeof( dir ) );
	CreateDirectoryA( dir, NULL );
	RcGamePath( "models/player/simon", dir, sizeof( dir ) );
	CreateDirectoryA( dir, NULL );
	FILE *f = fopen( path, "wb" );
	bool ok = f && fwrite( d, 1, len, f ) == (size_t)len;
	if ( f )
		fclose( f );
	delete [] d;
	gEngfuncs.Con_Printf( ok ? "ReCoF: made Simon's player model (models/player/simon/simon.mdl, %d bytes)\n" :
		"ReCoF: could not write Simon's player model (%d bytes)\n", len );
}
