// ======================================
// Cry of Fear subtitle language menu
// (class names from the RTTI of client.dll, file name guessed)
// ======================================

#include "hud.h"
#include "cl_util.h"
#include "vgui_TeamFortressViewport.h"
#include "Language_guess.h"
#include "..\game_shared\vgui_loadtga.h"
#include "event_api.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <io.h>
#include <sys/stat.h>

extern cvar_t *jp_poc;

// JP-MOD (build #46): make the GameUI / menu files follow the subtitle
// language selection. GameUI reads its localization files only once at
// startup, so the file swap below takes effect on the next launch; the
// in-game text (subtitles etc.) switches immediately via jp_poc as before.
static void JpApplyUiLanguage( bool japanese );

CLanguage::CLanguage() : Panel( 0, 0, ScreenWidth, ScreenHeight )
{
	int x, y;
	CommandButton *pButton;

	m_pBackgroundImage_guess = vgui_LoadTGA( "gfx/vgui/language_settings.tga" );
	if ( m_pBackgroundImage_guess )
		m_pBackgroundImage_guess->setColor( Color( 255, 255, 255, 1 ) );
	else
		gEngfuncs.Con_Printf( "Cannot load gfx/vgui/language_settings.tga!\n" );

	x = ScreenWidth / 2 - 320;
	y = ScreenHeight / 2 - 240;

	m_pBackground_guess = new ImagePanel( m_pBackgroundImage_guess );
	m_pBackground_guess->setParent( this );
	m_pBackground_guess->setPos( x, y );
	m_pBackground_guess->setVisible( true );

	pButton = new CommandButton( "", 83, 64, 158, 117, true, true );
	pButton->setParent( m_pBackground_guess );
	pButton->addActionSignal( new CLanguageHandler_Command( this, 1 ) );

	pButton = new CommandButton( "", 241, 64, 158, 117, true, true );
	pButton->setParent( m_pBackground_guess );
	pButton->addActionSignal( new CLanguageHandler_Command( this, 7 ) );

	pButton = new CommandButton( "", 399, 64, 158, 117, true, true );
	pButton->setParent( m_pBackground_guess );
	pButton->addActionSignal( new CLanguageHandler_Command( this, 5 ) );

	pButton = new CommandButton( "", 83, 181, 158, 117, true, true );
	pButton->setParent( m_pBackground_guess );
	pButton->addActionSignal( new CLanguageHandler_Command( this, 2 ) );

	pButton = new CommandButton( "", 241, 181, 158, 117, true, true );
	pButton->setParent( m_pBackground_guess );
	pButton->addActionSignal( new CLanguageHandler_Command( this, 3 ) );

	pButton = new CommandButton( "", 399, 181, 158, 117, true, true );
	pButton->setParent( m_pBackground_guess );
	pButton->addActionSignal( new CLanguageHandler_Command( this, 4 ) );

	pButton = new CommandButton( "", 83, 298, 158, 117, true, true );
	pButton->setParent( m_pBackground_guess );
	pButton->addActionSignal( new CLanguageHandler_Command( this, 6 ) );

	pButton = new CommandButton( "", 241, 298, 158, 117, true, true );
	pButton->setParent( m_pBackground_guess );
	pButton->addActionSignal( new CLanguageHandler_Command( this, 8 ) );

	setBgColor( 0, 0, 0, 255 );

	Initialize();
}

void CLanguage::ActionSignal( int cmd )
{
	cl_entity_t *pPlayer = gEngfuncs.GetLocalPlayer();

	if ( !pPlayer )
		return;

	switch ( cmd )
	{
	case 0:
		break;
	case 1:
		if ( jp_poc ) jp_poc->value = 0.0f;
		gEngfuncs.pfnServerCmd( "subtitleset 1" );
		setVisible( false );
		gViewPort->ShowVGUIMenu( 37 );
		gEngfuncs.pEventAPI->EV_PlaySound( -1, pPlayer->curstate.origin, CHAN_WEAPON, "ui/3d_click.wav", 1.0, 0.8, 0, PITCH_NORM );
		gViewPort->UpdateCursorState();
		break;
	case 2:
		if ( jp_poc ) jp_poc->value = 0.0f;
		gEngfuncs.pfnServerCmd( "subtitleset 2" );
		setVisible( false );
		gViewPort->ShowVGUIMenu( 37 );
		gEngfuncs.pEventAPI->EV_PlaySound( -1, pPlayer->curstate.origin, CHAN_WEAPON, "ui/3d_click.wav", 1.0, 0.8, 0, PITCH_NORM );
		gViewPort->UpdateCursorState();
		break;
	case 3:
		if ( jp_poc ) jp_poc->value = 0.0f;
		gEngfuncs.pfnServerCmd( "subtitleset 3" );
		setVisible( false );
		gViewPort->ShowVGUIMenu( 37 );
		gEngfuncs.pEventAPI->EV_PlaySound( -1, pPlayer->curstate.origin, CHAN_WEAPON, "ui/3d_click.wav", 1.0, 0.8, 0, PITCH_NORM );
		gViewPort->UpdateCursorState();
		break;
	case 4:
		if ( jp_poc ) jp_poc->value = 0.0f;
		gEngfuncs.pfnServerCmd( "subtitleset 4" );
		setVisible( false );
		gViewPort->ShowVGUIMenu( 37 );
		gEngfuncs.pEventAPI->EV_PlaySound( -1, pPlayer->curstate.origin, CHAN_WEAPON, "ui/3d_click.wav", 1.0, 0.8, 0, PITCH_NORM );
		gViewPort->UpdateCursorState();
		break;
	case 5:
		if ( jp_poc ) jp_poc->value = 0.0f;
		gEngfuncs.pfnServerCmd( "subtitleset 5" );
		setVisible( false );
		gViewPort->ShowVGUIMenu( 37 );
		gEngfuncs.pEventAPI->EV_PlaySound( -1, pPlayer->curstate.origin, CHAN_WEAPON, "ui/3d_click.wav", 1.0, 0.8, 0, PITCH_NORM );
		gViewPort->UpdateCursorState();
		break;
	case 6:
		if ( jp_poc ) jp_poc->value = 0.0f;
		gEngfuncs.pfnServerCmd( "subtitleset 6" );
		setVisible( false );
		gViewPort->ShowVGUIMenu( 37 );
		gEngfuncs.pEventAPI->EV_PlaySound( -1, pPlayer->curstate.origin, CHAN_WEAPON, "ui/3d_click.wav", 1.0, 0.8, 0, PITCH_NORM );
		gViewPort->UpdateCursorState();
		break;
	case 7:
		if ( jp_poc ) jp_poc->value = 0.0f;
		gEngfuncs.pfnServerCmd( "subtitleset 7" );
		setVisible( false );
		gViewPort->ShowVGUIMenu( 37 );
		gEngfuncs.pEventAPI->EV_PlaySound( -1, pPlayer->curstate.origin, CHAN_WEAPON, "ui/3d_click.wav", 1.0, 0.8, 0, PITCH_NORM );
		gViewPort->UpdateCursorState();
		break;
	case 8:
		if ( jp_poc ) jp_poc->value = 1.0f;
		gEngfuncs.pfnServerCmd( "subtitleset 1" );
		setVisible( false );
		gViewPort->ShowVGUIMenu( 37 );
		gEngfuncs.pEventAPI->EV_PlaySound( -1, pPlayer->curstate.origin, CHAN_WEAPON, "ui/3d_click.wav", 1.0, 0.8, 0, PITCH_NORM );
		gViewPort->UpdateCursorState();
		break;
	default:
		gEngfuncs.Con_DPrintf( "Unknown Language ActionSignal %i.\n", cmd );
		break;
	}

	// JP-MOD: keep the GameUI/menu files and the saved setting in sync.
	if ( cmd >= 1 && cmd <= 8 )
		JpApplyUiLanguage( cmd == 8 );
}

void CLanguage::Initialize()
{
	setVisible( false );
}

CLanguage::~CLanguage( void )
{
	delete m_pBackgroundImage_guess;
}

// ======================================
// JP-MOD: UI language follow (build #46)
// The mod ships both variants of every display file under
//   <gamedir>/jpmod/ui/<jp|en>/<path relative to the game root>
// plus texture patch bins for the models under
//   <gamedir>/jpmod/mdl/<jp|en>/<bin>
// On a language selection we copy the chosen variant over the live
// files, rewrite the model texture blocks, and persist jp_poc in
// userconfig.cfg so the choice survives a restart.
// ======================================

static bool JpUiCopyIfChanged( const char *src, const char *dst )
{
	FILE *in = fopen( src, "rb" );
	if ( !in )
		return false;
	fseek( in, 0, SEEK_END );
	long srcSize = ftell( in );
	fseek( in, 0, SEEK_SET );

	FILE *cur = fopen( dst, "rb" );
	if ( cur )
	{
		fseek( cur, 0, SEEK_END );
		long curSize = ftell( cur );
		fseek( cur, 0, SEEK_SET );
		if ( curSize == srcSize )
		{
			// Same size: skip the write when the content already matches.
			static unsigned char bufA[65536];
			static unsigned char bufB[65536];
			bool same = true;
			for ( ;; )
			{
				size_t ra = fread( bufA, 1, sizeof( bufA ), in );
				size_t rb = fread( bufB, 1, sizeof( bufB ), cur );
				if ( ra != rb || ( ra && memcmp( bufA, bufB, ra ) != 0 ) )
				{
					same = false;
					break;
				}
				if ( !ra )
					break;
			}
			fclose( cur );
			if ( same )
			{
				fclose( in );
				return true;
			}
			fseek( in, 0, SEEK_SET );
		}
		else
		{
			fclose( cur );
		}
	}

	_chmod( dst, _S_IWRITE );	// clear read-only (user.scr) if present
	FILE *out = fopen( dst, "wb" );
	if ( !out )
	{
		fclose( in );
		return false;
	}
	static unsigned char buf[65536];
	size_t n;
	while ( ( n = fread( buf, 1, sizeof( buf ), in ) ) > 0 )
		fwrite( buf, 1, n, out );
	fclose( out );
	fclose( in );
	return true;
}

static void JpUiApplyMdl( const char *gameRoot, const char *lang,
	const char *mdlRel, const char *binName,
	const char *targets[], int numTargets )
{
	char binPath[512], mdlPath[512];
	snprintf( binPath, sizeof( binPath ), "%s/cryoffear/jpmod/mdl/%s/%s", gameRoot, lang, binName );
	snprintf( mdlPath, sizeof( mdlPath ), "%s/%s", gameRoot, mdlRel );

	FILE *bf = fopen( binPath, "rb" );
	if ( !bf )
		return;
	fseek( bf, 0, SEEK_END );
	long binSize = ftell( bf );
	fseek( bf, 0, SEEK_SET );
	unsigned char *bin = (unsigned char *)malloc( binSize );
	if ( !bin || fread( bin, 1, binSize, bf ) != (size_t)binSize )
	{
		free( bin );
		fclose( bf );
		return;
	}
	fclose( bf );

	FILE *mf = fopen( mdlPath, "r+b" );
	if ( !mf )
	{
		free( bin );
		return;
	}
	unsigned char hdr[188];
	if ( fread( hdr, 1, sizeof( hdr ), mf ) != sizeof( hdr ) )
	{
		fclose( mf );
		free( bin );
		return;
	}
	int numTex, texIdx;
	memcpy( &numTex, hdr + 180, 4 );
	memcpy( &texIdx, hdr + 184, 4 );

	long binOff = 0;
	for ( int t = 0; t < numTargets; t++ )
	{
		bool done = false;
		for ( int i = 0; i < numTex && !done; i++ )
		{
			unsigned char ent[80];
			fseek( mf, texIdx + i * 80, SEEK_SET );
			if ( fread( ent, 1, sizeof( ent ), mf ) != sizeof( ent ) )
				break;
			char name[65];
			memcpy( name, ent, 64 );
			name[64] = '\0';
			if ( _stricmp( name, targets[t] ) != 0 )
				continue;
			int w, h, idx;
			memcpy( &w, ent + 68, 4 );
			memcpy( &h, ent + 72, 4 );
			memcpy( &idx, ent + 76, 4 );
			long size = (long)w * (long)h;
			if ( binOff + size <= binSize )
			{
				fseek( mf, idx, SEEK_SET );
				fwrite( bin + binOff, 1, size, mf );
			}
			binOff += size;
			done = true;
		}
		if ( !done )
			break;	// texture missing: bin offsets no longer line up
	}
	fclose( mf );
	free( bin );
}

static void JpUiPersistCvar( const char *gameDir, bool japanese )
{
	char cfgPath[512];
	snprintf( cfgPath, sizeof( cfgPath ), "%s/userconfig.cfg", gameDir );

	// Read the current file (if any), dropping every jp_poc line.
	static char kept[65536];
	kept[0] = '\0';
	size_t keptLen = 0;
	FILE *in = fopen( cfgPath, "rb" );
	if ( in )
	{
		char line[1024];
		while ( fgets( line, sizeof( line ), in ) )
		{
			const char *p = line;
			while ( *p == ' ' || *p == '\t' )
				p++;
			if ( strncmp( p, "jp_poc", 6 ) == 0 &&
				( p[6] == ' ' || p[6] == '\t' || p[6] == '\r' || p[6] == '\n' || p[6] == '\0' ) )
				continue;
			size_t ll = strlen( line );
			if ( keptLen + ll < sizeof( kept ) - 1 )
			{
				memcpy( kept + keptLen, line, ll );
				keptLen += ll;
				kept[keptLen] = '\0';
			}
		}
		fclose( in );
	}

	_chmod( cfgPath, _S_IWRITE );
	FILE *out = fopen( cfgPath, "wb" );
	if ( !out )
		return;
	fwrite( kept, 1, keptLen, out );
	if ( keptLen && kept[keptLen - 1] != '\n' )
		fputc( '\n', out );
	fprintf( out, "jp_poc %d\n", japanese ? 1 : 0 );
	fclose( out );
}

static void JpApplyUiLanguage( bool japanese )
{
	char gameDir[512], gameRoot[512];
	const char *gd = gEngfuncs.pfnGetGameDirectory();
	if ( !gd || !gd[0] )
		return;
	strncpy( gameDir, gd, sizeof( gameDir ) - 1 );
	gameDir[sizeof( gameDir ) - 1] = '\0';
	// Normalize separators and strip trailing slashes.
	for ( char *p = gameDir; *p; p++ )
		if ( *p == '\\' )
			*p = '/';
	size_t len = strlen( gameDir );
	while ( len && gameDir[len - 1] == '/' )
		gameDir[--len] = '\0';
	// Game root = parent of the gamedir.
	strncpy( gameRoot, gameDir, sizeof( gameRoot ) - 1 );
	gameRoot[sizeof( gameRoot ) - 1] = '\0';
	char *slash = strrchr( gameRoot, '/' );
	if ( slash )
		*slash = '\0';
	else
		strcpy( gameRoot, "." );	// relative gamedir: CWD is the game root

	const char *lang = japanese ? "jp" : "en";

	// 1) File swap per manifest.
	char manifestPath[512];
	snprintf( manifestPath, sizeof( manifestPath ), "%s/jpmod/ui/manifest.txt", gameDir );
	FILE *mf = fopen( manifestPath, "rb" );
	if ( mf )
	{
		char line[512];
		while ( fgets( line, sizeof( line ), mf ) )
		{
			size_t ll = strlen( line );
			while ( ll && ( line[ll - 1] == '\r' || line[ll - 1] == '\n' || line[ll - 1] == ' ' || line[ll - 1] == '\t' ) )
				line[--ll] = '\0';
			if ( !ll )
				continue;
			char src[512], dst[512];
			snprintf( src, sizeof( src ), "%s/jpmod/ui/%s/%s", gameDir, lang, line );
			snprintf( dst, sizeof( dst ), "%s/%s", gameRoot, line );
			JpUiCopyIfChanged( src, dst );
			// user.scr must stay read-only after the swap (same as the
			// installer sets it), otherwise the game rewrites it.
			if ( strcmp( line, "user.scr" ) == 0 )
				_chmod( dst, _S_IREAD );
		}
		fclose( mf );
	}

	// 2) Model texture blocks (phone screen / handwritten books).
	static const char *phoneTex[] = {
		"fullbright_mess1.bmp", "fullbright_mess2.bmp", "fullbright_mess3.bmp",
		"fullbright_mess4.bmp", "fullbright_mess5.bmp", "fullbright_mess6.bmp",
		"fullbright_mess7.bmp", "fullbright_lowbattery.bmp", "tex1.bmp"
	};
	static const char *bookTex[] = {
		"page1.BMP", "page2.BMP", "page3.BMP", "page4.BMP", "page5.BMP"
	};
	static const char *oneTex[] = { "tex1.BMP" };
	JpUiApplyMdl( gameRoot, lang, "cryoffear/models/weapons/mobile/v_mobile.mdl",
		"v_mobile_textures.bin", phoneTex, 9 );
	JpUiApplyMdl( gameRoot, lang, "cryoffear/models/bookmenu.mdl",
		"bookmenu_textures.bin", bookTex, 5 );
	JpUiApplyMdl( gameRoot, lang, "cryoffear/models/Props/Blandat/e1pages1.mdl",
		"e1pages_textures.bin", oneTex, 1 );
	JpUiApplyMdl( gameRoot, lang, "cryoffear/models/Props/Blandat/e1pages2.mdl",
		"e1pages_textures.bin", oneTex, 1 );
	JpUiApplyMdl( gameRoot, lang, "cryoffear/models/Props/Blandat/e1pages3.mdl",
		"e1pages_textures.bin", oneTex, 1 );
	JpUiApplyMdl( gameRoot, lang, "cryoffear/models/Props/Blandat/e1bookbig.mdl",
		"e1bookbig_textures.bin", oneTex, 1 );
	JpUiApplyMdl( gameRoot, lang, "cryoffear/models/Props/Blandat/bookofdeath.mdl",
		"bookofdeath_textures.bin", oneTex, 1 );

	// 3) Persist the choice for the next launch.
	JpUiPersistCvar( gameDir, japanese );
}
