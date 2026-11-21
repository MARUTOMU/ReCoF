//mp3 support added by Killar

#include "hud.h"
#include "cl_util.h"
#include "mp3.h"
#include "rc_client.h"

#include <irrKlang.h>
#include <windows.h>

// ReCoF (B185): retail opened "<game>/mp3/<song>" and "<game>/sound/<sample>" relative to the current
// folder, so music and these sounds were silent when the game started from another folder; build the
// path from the folder of the running exe (no static buffer: a new global here would move later data)
static void BuildGamePath( char *out, int size, const char *sub, const char *name )
{
	const char *game = gEngfuncs.pfnGetGameDirectory();
	char base[MAX_PATH];
	DWORD len = GetModuleFileNameA( NULL, base, sizeof( base ) );
	char *slash = ( len && len < sizeof( base ) ) ? strrchr( base, '\\' ) : NULL;

	if ( slash && !strchr( game, ':' ) )
	{
		*slash = 0;
		_snprintf( out, size, "%s/%s/%s/%s", base, game, sub, name );
	}
	else
		_snprintf( out, size, "%s/%s/%s", game, sub, name );

	out[size - 1] = 0;
}

using namespace irrklang;

char *UTIL_VarArgs_guess( const char *format, ... );

// static: the wrappers keep g_pMP3_guess in a register across the calls into CMP3
static CMP3		*g_pMP3_guess;			// 0x105440A4
static ISoundEngine	*g_pSoundEngine_guess;	// 0x105440A8
static ISound		*g_pSFX_guess;			// 0x105440AC, the looping sound effect (type 1)
static ISound		*g_pMusic_guess;		// 0x105440B0

//-----------------------------------------------------------------------------
// B364: a music track is created on a thread of its own. The game's irrKlang.dll (1.3.0) reads the whole mp3 file
// in 4 KB pieces on the thread that calls play2D, at every start of a track, one it played before too: 2,729 reads
// for brandon.mp3 (11 MB), 15 ms on an i7-12700KF with the file in memory and far more from a disk. A player's
// performance log (0.2.3-beta.7, Ryzen 9 5900X) had 17 of its 47 stutters there, 20 to 111 ms, under PlayMP3. With
// rc_music_thread 1 (the default) PlayMP3 stops the old track at once as before and hands the new one to the music
// thread, which creates it paused; the next frame (CMP3::Frame) takes it over, sets its volume and starts it, so a
// track starts a few frames later than before. A lock keeps the calls into irrKlang from the game's code on one
// thread at a time: the sound effects wait for it, what runs each frame (volumes and fades) skips a frame instead.
// rc_music_thread 0 creates the track on the main thread, as retail did
//-----------------------------------------------------------------------------
static cvar_t		*rc_music_thread;
static bool			s_bRcSoundLock;		// the music thread exists: irrKlang calls take s_csRcSound
static CRITICAL_SECTION	s_csRcSound;			// every call into irrKlang from the game's code
static CRITICAL_SECTION	s_csRcMusic;			// the request and the track handed over
static HANDLE		s_hRcMusicThread, s_hRcMusicWake;
static volatile LONG	s_lRcMusicQuit;
static char			s_szRcMusicPath[256];	// the track asked for, empty once the thread took it
static bool			s_bRcMusicLoop, s_bRcMusicPaused;	// how, and whether the game was paused then
static LONG			s_lRcMusicWant;		// counts the requests and the stops of the main thread
static ISound		*s_pRcMusicReady;		// a track created for request s_lRcMusicReadyGen, not taken yet
static LONG			s_lRcMusicReadyGen;
static bool			s_bRcMusicReadyPaused;

class RcSoundLock
{
public:
	// bTry: does not wait, Owns() says whether it got the lock
	RcSoundLock( bool bTry = false )
	{
		m_bLocked = false;
		m_bOwns = true;
		if ( !s_bRcSoundLock )
			return;
		if ( bTry )
			m_bOwns = TryEnterCriticalSection( &s_csRcSound ) != 0;
		else
			EnterCriticalSection( &s_csRcSound );
		m_bLocked = m_bOwns;
	}
	~RcSoundLock() { if ( m_bLocked ) LeaveCriticalSection( &s_csRcSound ); }
	bool Owns( void ) const { return m_bOwns; }
private:
	bool m_bOwns, m_bLocked;
};

static DWORD WINAPI RcMusicThread( LPVOID )
{
	while ( !s_lRcMusicQuit )
	{
		WaitForSingleObject( s_hRcMusicWake, INFINITE );

		while ( !s_lRcMusicQuit )
		{
			char szPath[256];
			EnterCriticalSection( &s_csRcMusic );
			LONG lGen = s_lRcMusicWant;
			bool bLoop = s_bRcMusicLoop, bPaused = s_bRcMusicPaused;
			strcpy( szPath, s_szRcMusicPath );
			s_szRcMusicPath[0] = 0;
			LeaveCriticalSection( &s_csRcMusic );
			if ( !szPath[0] )
				break;

			ISound *pSound;
			{
				RcSoundLock lock;
				pSound = g_pSoundEngine_guess ? g_pSoundEngine_guess->play2D( szPath, bLoop, true, true ) : NULL;
			}

			// handed over unless a newer request or a stop came meanwhile; what is not handed over is let go
			EnterCriticalSection( &s_csRcMusic );
			if ( pSound && lGen == s_lRcMusicWant )
			{
				ISound *pOld = s_pRcMusicReady;
				s_pRcMusicReady = pSound;
				s_lRcMusicReadyGen = lGen;
				s_bRcMusicReadyPaused = bPaused;
				pSound = pOld;
			}
			LeaveCriticalSection( &s_csRcMusic );

			if ( pSound )
			{
				RcSoundLock lock;
				pSound->stop();
				pSound->drop();
			}
		}
	}

	return 0;
}

// starts the music thread the first time, from the main thread while no other thread calls irrKlang
static bool RcMusicStart( void )
{
	if ( s_hRcMusicThread )
		return true;

	s_hRcMusicWake = CreateEventA( NULL, FALSE, FALSE, NULL );
	if ( !s_hRcMusicWake )
		return false;

	InitializeCriticalSection( &s_csRcSound );
	InitializeCriticalSection( &s_csRcMusic );
	s_lRcMusicQuit = 0;
	s_bRcSoundLock = true;
	s_hRcMusicThread = CreateThread( NULL, 64 * 1024, RcMusicThread, NULL, 0, NULL );
	if ( !s_hRcMusicThread )
	{
		s_bRcSoundLock = false;
		DeleteCriticalSection( &s_csRcSound );
		DeleteCriticalSection( &s_csRcMusic );
		CloseHandle( s_hRcMusicWake );
		s_hRcMusicWake = NULL;
		return false;
	}

	return true;
}

static void RcMusicRequest( const char *pszPath, bool bLoop, bool bPaused )
{
	EnterCriticalSection( &s_csRcMusic );
	strncpy( s_szRcMusicPath, pszPath, sizeof( s_szRcMusicPath ) - 1 );
	s_szRcMusicPath[sizeof( s_szRcMusicPath ) - 1] = 0;
	s_bRcMusicLoop = bLoop;
	s_bRcMusicPaused = bPaused;
	s_lRcMusicWant++;
	LeaveCriticalSection( &s_csRcMusic );
	SetEvent( s_hRcMusicWake );
}

// a stop: the track being created, if any, is let go when it is ready
static void RcMusicCancel( void )
{
	if ( !s_hRcMusicThread )
		return;

	EnterCriticalSection( &s_csRcMusic );
	s_szRcMusicPath[0] = 0;
	s_lRcMusicWant++;
	LeaveCriticalSection( &s_csRcMusic );
}

// from CMP3::Frame, holding s_csRcSound: the track the music thread made becomes the music, set up as PlayMP3 did.
// One the game paused after asking for it stays paused, as setAllSoundsPaused would have paused it; one whose fade
// out (StopMP3 after PlayMP3, which clears the fade) is already over is let go
static void RcMusicTakeOver( CMP3 *pMP3 )
{
	if ( !s_hRcMusicThread )
		return;

	EnterCriticalSection( &s_csRcMusic );
	ISound *pSound = s_pRcMusicReady;
	bool bCurrent = s_lRcMusicReadyGen == s_lRcMusicWant;
	bool bPausedThen = s_bRcMusicReadyPaused;
	s_pRcMusicReady = NULL;
	LeaveCriticalSection( &s_csRcMusic );

	if ( !pSound )
		return;

	if ( bCurrent && pMP3->m_flFadeoutDuration > 0 && gEngfuncs.GetClientTime() >= pMP3->m_flFadeoutStart + pMP3->m_flFadeoutDuration * 0.95 )
		bCurrent = false;

	if ( !bCurrent )
	{
		pSound->stop();
		pSound->drop();
		return;
	}

	g_pMusic_guess = pSound;
	float flVolume = CVAR_GET_FLOAT( "MP3Volume" );
	if ( flVolume > 1 )
		flVolume = 1;
	if ( flVolume < 0 )
		flVolume = 0;
	g_pMusic_guess->setVolume( flVolume );
	if ( !pMP3->m_iIsPaused_guess || bPausedThen )
		g_pMusic_guess->setIsPaused( false );
}

// from IrrKlangDestroy_guess, before the sound engine goes. If the thread does not end (a read stuck on a dying
// disk), it returns false: the sound engine stays and client.dll stays loaded until the process ends (as B351)
static bool RcMusicShutdown( void )
{
	if ( !s_hRcMusicThread )
		return true;

	InterlockedExchange( &s_lRcMusicQuit, 1 );
	SetEvent( s_hRcMusicWake );
	if ( WaitForSingleObject( s_hRcMusicThread, 5000 ) != WAIT_OBJECT_0 )
	{
		HMODULE hSelf;
		GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)RcMusicShutdown, &hSelf );
		return false;	// the thread may still be in irrKlang: the engine and the locks stay
	}

	CloseHandle( s_hRcMusicThread );
	CloseHandle( s_hRcMusicWake );
	s_hRcMusicThread = s_hRcMusicWake = NULL;

	if ( s_pRcMusicReady )
	{
		s_pRcMusicReady->stop();
		s_pRcMusicReady->drop();
		s_pRcMusicReady = NULL;
	}

	s_bRcSoundLock = false;
	DeleteCriticalSection( &s_csRcSound );
	DeleteCriticalSection( &s_csRcMusic );
	return true;
}

// from RcClient_Init
void RcMusic_Init( void )
{
	rc_music_thread = gEngfuncs.pfnRegisterVariable( "rc_music_thread", "1", FCVAR_ARCHIVE );
}

CMP3::CMP3()
{
	VectorClear( m_vecUnknown08_guess );
	VectorClear( m_vecUnknown14_guess );
	VectorClear( m_vecUnknown20_guess );
}

void CMP3::Disabled_guess( void )
{
	return;
}

void CMP3::Frame( void )
{
	if ( !g_pSoundEngine_guess || !m_bInitialized_guess )
		return;

	if ( !m_pLocalPlayer_guess )
		m_pLocalPlayer_guess = gEngfuncs.GetLocalPlayer();

	// B364: while the music thread is in irrKlang, the volumes and the fades wait for the next frame
	RcSoundLock lock( true );
	if ( !lock.Owns() )
		return;

	RcMusicTakeOver( this );

	if ( g_pSFX_guess )
	{
		float flVolume = CVAR_GET_FLOAT( "volume" );
		if ( flVolume > 1 )
			flVolume = 1;
		if ( flVolume < 0 )
			flVolume = 0;
		g_pSFX_guess->setVolume( flVolume );
	}

	if ( g_pMusic_guess )
	{
		float flVolume = CVAR_GET_FLOAT( "MP3Volume" );
		if ( flVolume > 1 )
			flVolume = 1;
		if ( flVolume < 0 )
			flVolume = 0;
		g_pMusic_guess->setVolume( flVolume );
	}

	// buz - fade out the music
	if ( g_pMusic_guess && m_flFadeoutStart + m_flFadeoutDuration > gEngfuncs.GetClientTime() )
	{
		float delta = ( gEngfuncs.GetClientTime() - m_flFadeoutStart ) / m_flFadeoutDuration;
		if ( delta >= 0.95 )
		{
			// fadeout finished, turn off
			g_pMusic_guess->stop();
			return;
		}

		int vol = ( CVAR_GET_FLOAT( "MP3Volume" ) * 255 ) - (int)( delta * 255 );
		if ( vol < 0 )
			vol = 0;
		if ( vol > 255 )
			vol = 255;

		float flVolume = vol / 255.0f;
		if ( flVolume < 0 )
			flVolume = 0;
		if ( flVolume > 1 )
			flVolume = 1;

		if ( flVolume > 0.05 )
			g_pMusic_guess->setVolume( flVolume );
		else
			g_pMusic_guess->stop();
	}

	if ( m_bHold_guess && g_pMusic_guess )
	{
		float flVolume = m_iVolume_guess / 255.0f;
		if ( flVolume < 0 )
			flVolume = 0;
		if ( flVolume > 1 )
			flVolume = 1;
		g_pMusic_guess->setVolume( flVolume );
	}
}

int CMP3::Initialize( void )
{
	m_bInitialized_guess = true;

	if ( g_pSoundEngine_guess )
	{
		m_pszPending_guess = NULL;
		m_pLocalPlayer_guess = NULL;
		m_iUnknown2C_guess = 0;
		m_iUnknown30_guess = 0;
		m_bUnknown34_guess = false;
		m_bUnknown35_guess = false;
		m_szMP3File[0] = 0;
		m_iIsPlaying = 0;
		m_iIsPaused_guess = 0;
		m_iVolume_guess = 0;
		m_bHold_guess = false;
		m_flFadeoutStart = 0;
		m_flFadeoutDuration = 0;
		m_flFadeinStart_guess = 0;
		m_flFadeinDuration_guess = 0;
		m_bLoop_guess = false;
		m_bStopping_guess = false;
		return 1;
	}

	return 0;
}

void IrrKlangCreate_guess( void )
{
	IrrKlangDestroy_guess();

	g_pMP3_guess = new CMP3;
	if ( g_pMP3_guess )
	{
		g_pMP3_guess->m_bInitialized_guess = false;
		g_pSoundEngine_guess = createIrrKlangDevice();
	}
}

void IrrKlangDestroy_guess( void )
{
	if ( !RcMusicShutdown() )	// B364: the music thread ends before the sound engine goes
		return;

	if ( g_pSoundEngine_guess )
		g_pSoundEngine_guess->drop();

	if ( g_pMP3_guess )
	{
		delete g_pMP3_guess;
		g_pMP3_guess = NULL;
	}
}

void IrrKlangFrame_guess( void )
{
	if ( g_pMP3_guess )
	{
		if ( !g_pMP3_guess->m_bInitialized_guess )
			g_pMP3_guess->Initialize();
		g_pMP3_guess->Frame();
	}
}

void IrrKlangInitialize_guess( void )
{
	if ( g_pMP3_guess )
		g_pMP3_guess->Initialize();
}

bool IrrKlangIsPaused_guess( void )
{
	if ( g_pMP3_guess && g_pMP3_guess->m_iIsPaused_guess )
		return true;
	return false;
}

void IrrKlangPlayMP3_guess( const char *pszFile )
{
	if ( g_pMP3_guess )
		g_pMP3_guess->PlayMP3( pszFile );
}

void IrrKlangPlaySFX_guess( const char *pszFile, int iType, bool bFlag, vec3_t vecOrigin )
{
	if ( g_pMP3_guess )
		g_pMP3_guess->PlaySFX_guess( pszFile, iType, bFlag, vecOrigin );
}

void IrrKlangSetFade_guess( float flParm1, float flParm2, float flParm3, float flParm4 )
{
	if ( g_pMP3_guess )
	{
		if ( flParm1 >= 0 )
			g_pMP3_guess->m_flFadeoutStart = flParm1;
		if ( flParm2 >= 0 )
			g_pMP3_guess->m_flFadeoutDuration = flParm2;
		if ( flParm3 >= 0 )
			g_pMP3_guess->m_flFadeinStart_guess = flParm3;
		if ( flParm4 >= 0 )
			g_pMP3_guess->m_flFadeinDuration_guess = flParm4;
	}
}

void IrrKlangSetLoop_guess( bool bLoop )
{
	if ( g_pMP3_guess )
		g_pMP3_guess->m_bLoop_guess = bLoop;
}

void IrrKlangSetPaused_guess( bool bPaused )
{
	if ( g_pMP3_guess )
		g_pMP3_guess->SetPaused_guess( bPaused );
}

void IrrKlangSetStopping_guess( bool bStopping )
{
	if ( g_pMP3_guess )
		g_pMP3_guess->m_bStopping_guess = bStopping;
}

void IrrKlangStop_guess( float flFadeTime )
{
	if ( g_pMP3_guess )
		g_pMP3_guess->StopMP3( flFadeTime );
}

// prints the average value of a block of bytes, never called
void __stdcall OutputAverage_guess( const unsigned char *pData, unsigned int iSize )
{
	int iTotal = 0;

	for ( unsigned int i = 0; i < iSize; i++ )
		iTotal += pData[i];

	gEngfuncs.pfnCenterPrint( UTIL_VarArgs_guess( "%.3f\n", (float)iTotal / (float)iSize ) );
}

int CMP3::PlayMP3( const char *pszSong )
{
	if ( !m_bInitialized_guess || !g_pSoundEngine_guess || gEngfuncs.CheckParm( "-nomusic", NULL ) )
		return 0;

	// the end songs never loop
	if ( pszSong )
	{
		if ( !strcmp( pszSong, "ap3hall1end.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "aparthall2end.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "aparthall2end2.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "aphall1end.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "basement1end.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "cityend.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "coopend.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "doctorend.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "docvic.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "hit1.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "hit2.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "hit3.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "nightmareend.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "sawend.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "sewerhit.mp3" ) )
			m_bLoop_guess = false;
		if ( !strcmp( pszSong, "trainhit.mp3" ) )
			m_bLoop_guess = false;
	}

	if ( m_bHold_guess )
	{
		m_pszPending_guess = pszSong;
		return 0;
	}

	if ( m_pszPending_guess && pszSong && !strcmp( m_pszPending_guess, pszSong ) )
		return 0;

	// buz: we are playing this song now, dont do anything
	if ( !strcmp( pszSong, m_szMP3File ) )
		return 0;

	m_flFadeoutStart = 0;
	m_flFadeoutDuration = 0;
	m_flFadeinStart_guess = 0;
	m_flFadeinDuration_guess = 0;

	char song[256];

	BuildGamePath( song, sizeof( song ), "mp3", pszSong );

	if ( rc_music_thread && rc_music_thread->value && RcMusicStart() )
	{
		// B364: the music thread creates the track, the next frame starts it
		{
			RcSoundLock lock;
			if ( g_pMusic_guess )
				g_pMusic_guess->stop();
		}
		g_pMusic_guess = NULL;
		RcMusicRequest( song, m_bLoop_guess, m_iIsPaused_guess != 0 );
	}
	else if ( m_bLoop_guess )
	{
		RcSoundLock lock;
		if ( g_pMusic_guess )
			g_pMusic_guess->stop();
		g_pMusic_guess = g_pSoundEngine_guess->play2D( song, true, true, true );
		if ( g_pMusic_guess )
		{
			float flVolume = CVAR_GET_FLOAT( "MP3Volume" );
			if ( flVolume > 1 )
				flVolume = 1;
			if ( flVolume < 0 )
				flVolume = 0;
			g_pMusic_guess->setVolume( flVolume );
			g_pMusic_guess->setIsPaused( false );
		}
	}
	else
	{
		RcSoundLock lock;
		if ( g_pMusic_guess )
			g_pMusic_guess->stop();
		g_pMusic_guess = g_pSoundEngine_guess->play2D( song, false, true, true );
		if ( g_pMusic_guess )
		{
			float flVolume = CVAR_GET_FLOAT( "MP3Volume" );
			if ( flVolume > 1 )
				flVolume = 1;
			if ( flVolume < 0 )
				flVolume = 0;
			g_pMusic_guess->setVolume( flVolume );
			g_pMusic_guess->setIsPaused( false );
		}
	}

	m_iIsPlaying = 1;
	strcpy( m_szMP3File, pszSong );
	return 1;
}

// PlayMP3 with a full path, never called
int CMP3::PlayMP3Path_guess( const char *pszPath )
{
	if ( !m_bInitialized_guess || !g_pSoundEngine_guess || gEngfuncs.CheckParm( "-nomusic", NULL ) )
		return 0;

	RcSoundLock lock;	// B364
	RcMusicCancel();

	if ( m_iIsPlaying && g_pMusic_guess )
		g_pMusic_guess->stop();

	char song[256];

	sprintf( song, "%s", pszPath );

	if ( g_pMusic_guess )
		g_pMusic_guess->stop();
	g_pMusic_guess = g_pSoundEngine_guess->play2D( pszPath, false, true, true );
	if ( g_pMusic_guess )
	{
		g_pMusic_guess->setVolume( CVAR_GET_FLOAT( "MP3Volume" ) );
		g_pMusic_guess->setIsPaused( false );
	}

	m_iIsPlaying = 1;
	return 1;
}

void CMP3::PlaySFX_guess( const char *pszSample, int iType, bool bFlag, Vector vecOrigin )
{
	if ( !m_bInitialized_guess || !g_pSoundEngine_guess )
		return;

	RcSoundLock lock;	// B364: waits while the music thread is in irrKlang

	char szNewSample[256];
	char szPath[256];

	// weapons/<folder>/<file> plays weapons_new/<file>
	if ( strstr( pszSample, "weapons/" ) )
	{
		bool bFound = false;
		int j = 0;

		for ( unsigned int i = 0; i < strlen( pszSample ); i++ )
		{
			if ( bFound )
				szPath[j++] = pszSample[i];
			else if ( pszSample[i] == '/' || pszSample[i] == '\\' )
				bFound = true;
		}

		if ( bFound )
		{
			szPath[j] = 0;
			strcpy( szNewSample, "weapons_new/" );
			strcat( szNewSample, szPath );
			pszSample = szNewSample;
		}
	}

	if ( iType == 1 )
	{
		if ( g_pSFX_guess )
			g_pSFX_guess->stop();

		BuildGamePath( szPath, sizeof( szPath ), "sound", pszSample );
		g_pSFX_guess = g_pSoundEngine_guess->play2D( szPath, true, true, true );
		if ( g_pSFX_guess )
		{
			float flVolume = CVAR_GET_FLOAT( "volume" );
			if ( flVolume > 1 )
				flVolume = 1;
			if ( flVolume < 0 )
				flVolume = 0;
			g_pSFX_guess->setVolume( flVolume );
			g_pSFX_guess->setPlaybackSpeed( RcWeaponSoundSpeed() );	// B318: retail pfnRandomFloat( -0.1, 0.1 ) + 1.0
			g_pSFX_guess->setIsPaused( false );
		}
	}
	else if ( iType == 2 )
	{
		if ( g_pSFX_guess )
			g_pSFX_guess->stop();
	}
	else
	{
		BuildGamePath( szPath, sizeof( szPath ), "sound", pszSample );
		ISound *pSound = g_pSoundEngine_guess->play2D( szPath, false, true, true );
		if ( pSound )
		{
			float flVolume = CVAR_GET_FLOAT( "volume" );
			if ( flVolume > 1 )
				flVolume = 1;
			if ( flVolume < 0 )
				flVolume = 0;
			pSound->setVolume( flVolume );
			pSound->setPlaybackSpeed( RcWeaponSoundSpeed() );	// B318: retail pfnRandomFloat( -0.1, 0.1 ) + 1.0
			pSound->setIsPaused( false );
		}
	}
}

int CMP3::SetPaused_guess( bool bPaused )
{
	if ( !m_bInitialized_guess || !g_pSoundEngine_guess )
		return 0;

	RcSoundLock lock;	// B364

	if ( bPaused )
	{
		m_iIsPaused_guess = 1;
		g_pSoundEngine_guess->setAllSoundsPaused( true );
		return 1;
	}

	m_iIsPaused_guess = 0;
	g_pSoundEngine_guess->setAllSoundsPaused( false );
	return 1;
}

int CMP3::Shutdown( void )
{
	return 1;
}

int CMP3::StopMP3( float fade )
{
	if ( !m_bInitialized_guess )
		return 0;

	if ( !g_pSoundEngine_guess || gEngfuncs.CheckParm( "-nomusic", NULL ) )
		return 1;

	m_bStopping_guess = true;

	if ( m_bHold_guess )
		m_bHold_guess = false;

	m_szMP3File[0] = 0;

	if ( fade == 0 )
	{
		RcMusicCancel();	// B364: and a track the music thread is creating
		RcSoundLock lock;
		if ( g_pMusic_guess )
			g_pMusic_guess->stop();
		m_iIsPlaying = 0;
		m_flFadeoutStart = 0;
		m_flFadeoutDuration = 0;
		m_bStopping_guess = false;
		return 1;
	}

	m_flFadeoutStart = gEngfuncs.GetClientTime();
	m_flFadeoutDuration = fade;
	return 1;
}
