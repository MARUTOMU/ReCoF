// ReCoF (B422): the voice options the engine lacks
//
// rc_voice_mode: 0 push to talk (the engine's +voicerecord on a key, as before), 1 open mic: this file listens to the
// microphone itself and holds +voicerecord while the player speaks louder than rc_voice_open_level percent of full
// scale (and 0.7 s after), so a quiet room sends nothing.
// rc_voice_mic: the microphone, by its Windows name (the Options' Talking page lists them); empty, the Windows default.
// The engine opens the default capture device itself (hw.dll voice_record_dsound.cpp, voice_record_wavein.cpp): with a
// microphone chosen, voice_dsound 0 makes it record through waveIn, and hw.dll's waveInOpen import comes here, which
// swaps the Windows default for the chosen device. The engine opens its recorder when a game starts, so a new choice
// applies from the next map.

#include "hud.h"
#include "cl_util.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rc_steamnet.h"	// RcFindImport, RcPatch

static cvar_t *s_pRcVoiceMode;
static cvar_t *s_pRcVoiceLevel;
static cvar_t *s_pRcVoiceMic;

typedef MMRESULT ( WINAPI *RcWaveInOpen_t )( LPHWAVEIN, UINT, LPCWAVEFORMATEX, DWORD, DWORD, DWORD );
static RcWaveInOpen_t s_pfnRcWaveInOpen;	// hw.dll's own, before the patch
static int s_iRcEngineDevice = -2;			// the device the engine last opened (-1 the Windows default, -2 none yet)
static float s_flRcVoxPeak;					// the open mic's last level, 0 to 1
static int s_iRcVoxBuffers;					// the open mic's buffers heard (rc_voice_status: 0, no sound comes)

// a device name as the Options store it: no character a cvar value or the Options' list cannot hold
void RcVoice_CleanName( char *psz )
{
	for ( ; *psz; psz++ )
	{
		if ( *psz == '"' || *psz == ';' || *psz == '|' || *psz == '=' || *psz == '\n' || *psz == '\r' )
			*psz = ' ';
	}
}

// the chosen microphone's device number; WAVE_MAPPER (the Windows default) for none or a name no longer there
static UINT RcVoiceDevice( void )
{
	if ( !s_pRcVoiceMic || !s_pRcVoiceMic->string[0] )
		return WAVE_MAPPER;
	UINT n = waveInGetNumDevs();
	for ( UINT i = 0; i < n; i++ )
	{
		WAVEINCAPSA caps;
		if ( waveInGetDevCapsA( i, &caps, sizeof( caps ) ) != MMSYSERR_NOERROR )
			continue;
		caps.szPname[sizeof( caps.szPname ) - 1] = 0;
		RcVoice_CleanName( caps.szPname );
		if ( !strcmp( caps.szPname, s_pRcVoiceMic->string ) )
			return i;
	}
	return WAVE_MAPPER;
}

static MMRESULT WINAPI RcWaveInOpen( LPHWAVEIN phwi, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD dwCallback, DWORD dwInstance, DWORD fdwOpen )
{
	if ( uDeviceID == WAVE_MAPPER )
		uDeviceID = RcVoiceDevice();
	s_iRcEngineDevice = uDeviceID == WAVE_MAPPER ? -1 : (int)uDeviceID;
	return s_pfnRcWaveInOpen( phwi, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen );
}

bool RcVoice_Talking( void );

// rc_voice_status: what the voice options do now, to set the open mic's threshold by
static void RcVoiceStatus_f( void )
{
	UINT dev = RcVoiceDevice();
	gEngfuncs.Con_Printf( "ReCoF voice: open mic heard %d buffers\n", s_iRcVoxBuffers );
	gEngfuncs.Con_Printf( "ReCoF voice: %s, open mic level %.1f%% (threshold %g%%), %s; microphone %s (device %d), "
		"engine opened %d, %s\n", s_pRcVoiceMode->value == 1 ? "open mic" : "push to talk", s_flRcVoxPeak * 100,
		s_pRcVoiceLevel->value, RcVoice_Talking() ? "sending" : "quiet", s_pRcVoiceMic->string[0] ? s_pRcVoiceMic->string : "Windows default",
		dev == WAVE_MAPPER ? -1 : (int)dev, s_iRcEngineDevice, s_pfnRcWaveInOpen ? "hooked" : "NOT hooked" );
	UINT n = waveInGetNumDevs();
	for ( UINT i = 0; i < n; i++ )
	{
		WAVEINCAPSA caps;
		if ( waveInGetDevCapsA( i, &caps, sizeof( caps ) ) != MMSYSERR_NOERROR )
			continue;
		caps.szPname[sizeof( caps.szPname ) - 1] = 0;
		RcVoice_CleanName( caps.szPname );
		gEngfuncs.Con_Printf( "ReCoF voice: device %d \"%s\"\n", i, caps.szPname );
	}
}

void RcVoice_Init( void )
{
	s_pRcVoiceMode = gEngfuncs.pfnRegisterVariable( "rc_voice_mode", "0", FCVAR_ARCHIVE );
	s_pRcVoiceLevel = gEngfuncs.pfnRegisterVariable( "rc_voice_open_level", "4", FCVAR_ARCHIVE );
	s_pRcVoiceMic = gEngfuncs.pfnRegisterVariable( "rc_voice_mic", "", FCVAR_ARCHIVE );

	HMODULE hHw = GetModuleHandleA( "hw.dll" );
	HMODULE hMM = GetModuleHandleA( "winmm.dll" );
	void *pCurrent = hMM ? (void *)GetProcAddress( hMM, "waveInOpen" ) : NULL;
	void **ppSlot = hHw && pCurrent ? RcFindImport( hHw, "winmm.dll", -1, pCurrent ) : NULL;
	void *pOld = NULL;
	if ( ppSlot && RcPatch( ppSlot, (void *)RcWaveInOpen, &pOld ) )
		s_pfnRcWaveInOpen = (RcWaveInOpen_t)pOld;
	gEngfuncs.pfnAddCommand( "rc_voice_status", RcVoiceStatus_f );
}

// the open mic's own listening: 8000 Hz mono, four 40 ms buffers, read each frame
#define RC_VOX_BUFS		4
#define RC_VOX_SAMPLES	320

static HWAVEIN s_hRcVox;
static WAVEHDR s_RcVoxHdr[RC_VOX_BUFS];
static short s_sRcVox[RC_VOX_BUFS][RC_VOX_SAMPLES];
static char s_szRcVoxMic[64];		// the microphone it listens to
static DWORD s_dwRcVoxRetry;		// a microphone that would not open is tried again from here
static DWORD s_dwRcVoxLoudUntil;	// talking until here
static bool s_bRcVoxTalking;		// +voicerecord is held
static bool s_bRcSetDsound;			// voice_dsound was set to 0 here

static void RcVoxClose( void )
{
	if ( !s_hRcVox )
		return;
	waveInReset( s_hRcVox );
	for ( int i = 0; i < RC_VOX_BUFS; i++ )
		waveInUnprepareHeader( s_hRcVox, &s_RcVoxHdr[i], sizeof( WAVEHDR ) );
	waveInClose( s_hRcVox );
	s_hRcVox = NULL;
}

static void RcVoxOpen( void )
{
	WAVEFORMATEX wf;
	memset( &wf, 0, sizeof( wf ) );
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = 1;
	wf.nSamplesPerSec = 8000;
	wf.wBitsPerSample = 16;
	wf.nBlockAlign = 2;
	wf.nAvgBytesPerSec = 16000;
	if ( waveInOpen( &s_hRcVox, RcVoiceDevice(), &wf, 0, 0, CALLBACK_NULL ) != MMSYSERR_NOERROR )
	{
		s_hRcVox = NULL;
		s_dwRcVoxRetry = GetTickCount() + 5000;
		return;
	}
	for ( int i = 0; i < RC_VOX_BUFS; i++ )
	{
		memset( &s_RcVoxHdr[i], 0, sizeof( WAVEHDR ) );
		s_RcVoxHdr[i].lpData = (LPSTR)s_sRcVox[i];
		s_RcVoxHdr[i].dwBufferLength = sizeof( s_sRcVox[i] );
		waveInPrepareHeader( s_hRcVox, &s_RcVoxHdr[i], sizeof( WAVEHDR ) );
		waveInAddBuffer( s_hRcVox, &s_RcVoxHdr[i], sizeof( WAVEHDR ) );
	}
	waveInStart( s_hRcVox );
}

// the loudest sample of the buffers filled since the last frame, 0 to 1; each goes back to be filled again
static float RcVoxPeak( void )
{
	int iPeak = 0;
	for ( int i = 0; i < RC_VOX_BUFS; i++ )
	{
		WAVEHDR *h = &s_RcVoxHdr[i];
		if ( !( h->dwFlags & WHDR_DONE ) )
			continue;
		s_iRcVoxBuffers++;
		int n = h->dwBytesRecorded / 2;
		for ( int s = 0; s < n; s++ )
		{
			int v = abs( (int)s_sRcVox[i][s] );
			if ( v > iPeak )
				iPeak = v;
		}
		waveInAddBuffer( s_hRcVox, h, sizeof( WAVEHDR ) );
	}
	return iPeak / 32768.0f;
}

bool RcVoice_Talking( void )
{
	return s_bRcVoxTalking;
}

static void RcVoxTalk( bool bTalk )
{
	if ( bTalk == s_bRcVoxTalking )
		return;
	s_bRcVoxTalking = bTalk;
	gEngfuncs.pfnClientCmd( bTalk ? "+voicerecord\n" : "-voicerecord\n" );
}

void RcVoice_Frame( void )
{
	if ( !s_pRcVoiceMode )
		return;

	// a chosen microphone needs the engine's waveIn recorder, which the import above points at it
	if ( s_pRcVoiceMic->string[0] && gEngfuncs.pfnGetCvarFloat( (char *)"voice_dsound" ) != 0 )
	{
		gEngfuncs.Cvar_SetValue( (char *)"voice_dsound", 0 );
		s_bRcSetDsound = true;
	}
	else if ( !s_pRcVoiceMic->string[0] && s_bRcSetDsound )
	{
		gEngfuncs.Cvar_SetValue( (char *)"voice_dsound", 1 );
		s_bRcSetDsound = false;
	}

	// the open mic, in a multiplayer game with voice on
	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	bool bWant = s_pRcVoiceMode->value == 1 && gEngfuncs.GetMaxClients() > 1 && pszLevel && pszLevel[0] &&
		gEngfuncs.pfnGetCvarFloat( (char *)"voice_enable" ) != 0;
	if ( !bWant )
	{
		RcVoxClose();
		RcVoxTalk( false );
		return;
	}
	if ( s_hRcVox && strcmp( s_szRcVoxMic, s_pRcVoiceMic->string ) )
		RcVoxClose();	// another microphone was chosen
	if ( !s_hRcVox )
	{
		if ( (long)( GetTickCount() - s_dwRcVoxRetry ) < 0 )
			return;
		strncpy( s_szRcVoxMic, s_pRcVoiceMic->string, sizeof( s_szRcVoxMic ) - 1 );
		s_szRcVoxMic[sizeof( s_szRcVoxMic ) - 1] = 0;
		RcVoxOpen();
		if ( !s_hRcVox )
			return;
	}
	float flLevel = s_pRcVoiceLevel->value;
	if ( flLevel < 1 )
		flLevel = 1;
	float flPeak = RcVoxPeak();
	if ( flPeak > 0 || s_flRcVoxPeak > 0 )
		s_flRcVoxPeak = flPeak > s_flRcVoxPeak ? flPeak : s_flRcVoxPeak * 0.9f;	// held a moment for rc_voice_status
	if ( flPeak * 100 >= flLevel )
		s_dwRcVoxLoudUntil = GetTickCount() + 700;
	RcVoxTalk( (long)( GetTickCount() - s_dwRcVoxLoudUntil ) < 0 );
}
