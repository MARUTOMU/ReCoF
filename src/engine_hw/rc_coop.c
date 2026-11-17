/*
rc_coop.c

ReCoF story co-op, engine part: carry every player through the single-player level transition (changelevel2) when
the game runs in story co-op (hl.dll's rc_coop cvar with more than one player slot).

The single-player transition already saves and restores every player slot (SaveGamestate, CreateEntityList,
LoadGamestate); in multiplayer three things stop it: Host_Changelevel2_f refuses, the reconnect handshake calls the
game's ClientDisconnect and ClientConnect on the restored player (SV_SpawnServer, SV_New_f), and SV_WriteSpawn keeps
the restored player for the first client only. The functions here answer those places for the clients that were in
the game when the transition began ("carried"), for at most 90 seconds.

Everything in this file lives in a section of its own after .data (.rcdata, made by the /MERGE and /SECTION flags of
tools/units.json), so no address in .text, .rdata, .data or .bss that the speedrun tools read moves. No string or
floating point literals: VC6 builds hw.dll without string pooling and would put them in .data or .rdata; the texts
are char arrays here instead.
*/
#pragma code_seg( ".rctext" )
#pragma data_seg( ".rcdata" )
#pragma bss_seg( ".rcbss" )
#pragma const_seg( ".rcdata" )

#include "quakedef.h"
#include "mathlib.h"
#include "const.h"
#include "usercmd.h"
#include "cvardef.h"
#include "cvar.h"
#include "cmd.h"
#include "info.h"
#include "host.h"
#include "net.h"
#include "protocol.h"
#include "GL_MODEL.H"
#include "progs.h"
#include "eiface.h"
#include "delta.h"
#include "server.h"
#include "client.h"
#include "sound.h"
#include "world.h"
#include "rc_coop.h"

#define RC_MAX_CLIENTS	32
#define RC_CARRY_SECONDS	90

static char rc_szCoop[] = "rc_coop";
static char rc_szSkip[] = "rc_coop_skip";	// hl.dll rc_coop.cpp, G6
static char rc_szLeft[] = "rc_coop_left";	// client.dll rc_client.cpp, the guest's reconnect
static char rc_szOne[] = "1";
static char rc_szCarry[] = "ReCoF co-op: carrying %i players to %s\n";

static int rc_iCarried[RC_MAX_CLIENTS];	// 1 while this slot's player waits to be restored on the new level
static int rc_bTransition;	// a co-op changelevel2 happened in this server's life
static double rc_flTransition;	// realtime of the last one

// story co-op: hl.dll's rc_coop is on and the server has more than one slot
static int RcCoop_On( void )
{
	cvar_t	*v;

	if ( svs.maxclients <= 1 )
		return 0;
	v = Cvar_FindVar( rc_szCoop );
	return v && *(int *)&v->value != 0;
}

// Host_Changelevel2_f: in story co-op, let the transition through and mark every client in the game as carried
int RcCoop_AllowChangelevel2( char *pszLevel )
{
	int			i, n;
	client_t	*cl;

	if ( !RcCoop_On() )
		return 0;

	n = 0;
	for ( i = 0, cl = svs.clients; i < svs.maxclients && i < RC_MAX_CLIENTS; i++, cl++ )
	{
		rc_iCarried[i] = ( cl->active && cl->spawned && !cl->fakeclient ) ? 1 : 0;
		n += rc_iCarried[i];
	}
	rc_bTransition = 1;
	rc_flTransition = realtime;
	Con_Printf( rc_szCarry, n, pszLevel );
	return 1;
}

// this client's player is being carried through a co-op transition (the mark lapses after 90 s, so a client that
// dropped on the way cannot keep it for a newcomer in the same slot)
int RcCoop_Carried( client_t *cl )
{
	int	i;

	if ( !RcCoop_On() )
		return 0;	// single player and other multiplayer games never skip the game DLL's connect
	i = cl - svs.clients;
	if ( i < 0 || i >= RC_MAX_CLIENTS || !rc_iCarried[i] )
		return 0;
	if ( (int)( realtime - rc_flTransition ) > RC_CARRY_SECONDS )
	{
		rc_iCarried[i] = 0;
		return 0;
	}
	return 1;
}

// SV_WriteSpawn: does this client keep a restored player? Single player and every load: sv.loadgame as before
int RcCoop_Loadgame( client_t *cl )
{
	if ( !sv.loadgame )
		return 0;
	if ( !rc_bTransition || !RcCoop_On() )
		return 1;
	return RcCoop_Carried( cl );
}

// SV_WriteSpawn after svc_restore: single player clears sv.loadgame at the first spawn; a co-op transition keeps it
// until the last carried client has spawned
void RcCoop_Spawned( client_t *cl )
{
	int	i;

	if ( !rc_bTransition || !RcCoop_On() )
	{
		sv.loadgame = FALSE;
		return;
	}
	i = cl - svs.clients;
	if ( i >= 0 && i < RC_MAX_CLIENTS )
		rc_iCarried[i] = 0;
	for ( i = 0; i < svs.maxclients && i < RC_MAX_CLIENTS; i++ )
	{
		if ( RcCoop_Carried( &svs.clients[i] ) )
			return;
	}
	sv.loadgame = FALSE;
	rc_bTransition = 0;
}

// Host_Changelevel2_f, after the players were restored and before SV_ActivateServer sends "reconnect": a carried
// client is inactive until it spawns again, as SV_InactivateClients makes a client on a multiplayer level change, so
// the server sends it no entity updates and PF_MessageEnd_I drops the game's user messages to it while it loads (they
// would reach it before the new level's user message list: svc_bad); its restored player stays in its slot
void RcCoop_Inactivate( void )
{
	int			i;
	client_t	*cl;

	if ( !rc_bTransition || !RcCoop_On() )
		return;
	for ( i = 0, cl = svs.clients; i < svs.maxclients && i < RC_MAX_CLIENTS; i++, cl++ )
	{
		if ( !rc_iCarried[i] )
			continue;
		cl->active = FALSE;
		cl->spawned = FALSE;
		cl->connected = TRUE;
		cl->fully_connected = FALSE;
		SZ_Clear( &cl->netchan.message );
		SZ_Clear( &cl->datagram );
	}
}

// Host_ValidSave: the story co-op host may save the party's game (every player slot is in the save)
int RcCoop_Enabled( void )
{
	return RcCoop_On();
}

static char rc_szLoaded[] = "ReCoF co-op: %i saved players wait for their players to come back\n";

// Host_Load, after Host_Map loaded the save: LoadGamestate created every saved player, but the load dropped every
// client and SV_WriteSpawn would keep a restored player only for the first to spawn. Each slot with a saved player is
// carried, as on a level change, so the client that takes it back keeps it (the host's first; the others in the order
// they come back, within 90 s)
void RcCoop_Loaded( void )
{
	int		i, n;
	edict_t	*ent;

	if ( !RcCoop_On() || !sv.loadgame )
		return;
	n = 0;
	for ( i = 0; i < svs.maxclients && i < RC_MAX_CLIENTS; i++ )
	{
		ent = EDICT_NUM( i + 1 );
		rc_iCarried[i] = ( ent && !ent->free && ent->pvPrivateData ) ? 1 : 0;
		n += rc_iCarried[i];
	}
	if ( !n )
		return;
	rc_bTransition = 1;
	rc_flTransition = realtime;
	Con_Printf( rc_szLoaded, n );
}

// SV_ConnectClient, after SV_FindEmptySlot (the first free slot): once a story co-op save is loaded, a client that
// comes back takes the free slot whose saved player has his name (the save keeps each player's netname), and a
// client the save has no player for takes a free slot without a saved player while there is one, so 3 or 4 players
// coming back in another order each get their own saved player. A name the server made unique ("(1)...") matches
// the one saved only if the players come back in the same order; the co-op commands give a player who kept the
// default name his Steam name (client.dll rc_client.cpp RcCoopDefaultName)
void RcCoop_PickSlot( char *name, int *pslot, client_t **ppClient )
{
	int			i, iFree;
	client_t	*cl;
	edict_t		*ent;

	if ( !rc_bTransition || !RcCoop_On() || !name || !name[0] )
		return;
	iFree = -1;
	for ( i = 0, cl = svs.clients; i < svs.maxclients && i < RC_MAX_CLIENTS; i++, cl++ )
	{
		if ( cl->active || cl->spawned || cl->connected )
			continue;
		if ( !RcCoop_Carried( cl ) )
		{
			if ( iFree < 0 )
				iFree = i;
			continue;
		}
		ent = EDICT_NUM( i + 1 );
		if ( ent && !ent->free && ent->v.netname && !Q_strcmp( pr_strings + ent->v.netname, name ) )
		{
			*pslot = i;
			*ppClient = cl;
			return;
		}
	}
	if ( iFree >= 0 && RcCoop_Carried( *ppClient ) )
	{
		*pslot = iFree;
		*ppClient = &svs.clients[iFree];
	}
}

// Host_TogglePause_f, Host_Pause_f and Host_Unpause_f: in story co-op only the host's own client (the loopback one)
// pauses; a guest's pause, and the pause GameUI asks for when a guest opens his menu, are refused
int RcCoop_GuestPause( void )
{
	return RcCoop_On() && host_client && host_client->netchan.remote_address.type != NA_LOOPBACK;
}

static double rc_flSkipStep = 0.1;	// a fast forward step, as a 10 fps frame (no floating point literal in code)
static int rc_iSkipSteps = 19;		// with the frame's own step, about 2 s of game time a frame, as single player's skip

// SV_Frame, after the frame's own physics: in single player a scene skip sets host_framerate 2 and the whole game
// runs two seconds a frame; in multiplayer Host_FilterTime ignores host_framerate, as jumping realtime would time out
// every client. In story co-op the server runs extra 0.1 s physics steps instead while hl.dll's rc_coop_skip is on
// (the host's skip), so game time hurries and realtime, and the clients' connections, do not. hl.dll ends the skip
// when no player is in a scene any more.
void RcCoop_SkipPhysics( void )
{
	int		i;
	double	flSave;
	cvar_t	*v;

	if ( !RcCoop_On() || !SV_IsSimulating() )
		return;
	v = Cvar_FindVar( rc_szSkip );
	if ( !v || *(int *)&v->value == 0 )
		return;
	flSave = host_frametime;
	host_frametime = rc_flSkipStep;
	for ( i = 0; i < rc_iSkipSteps; i++ )
	{
		gGlobalVariables.frametime = host_frametime;
		SV_Physics();
		sv.time += host_frametime;
	}
	host_frametime = flSave;
	gGlobalVariables.frametime = host_frametime;
}

void CL_Disconnect_f( void );

/*
B394: story co-op proximity voice. Retail plays every voice on the listener's own entity, so it is flat and the same
from anywhere. In story co-op (the server's physinfo "rcc" 1) with client.dll's rc_voice_3d on, a teammate's voice
plays on his entity, so the mixer places it where he stands and lowers it with distance (silent at rc_voice_range
units, as the mixer's attenuation is its 1000-unit clip distance divided by the range); and when the world stands
between the listener and the speaker's head, his samples go through a one-pole low pass (about 800 Hz at the
mixer's 22050 Hz) at rc_voice_wall percent of the volume, eased in and out. Integer arithmetic and constants in this
section only (no floating point literal in code).
*/
#define RC_VOICE_CHANNELS	5

static char rc_szVoice3D[] = "rc_voice_3d";
static char rc_szVoiceRange[] = "rc_voice_range";
static char rc_szVoiceWall[] = "rc_voice_wall";
static char rc_szRcc[] = "rcc";
static double rc_flVoiceClip = 1000;	// the mixer's nominal clip distance (snd_dma.c sound_nominal_clip_dist)
static float rc_flVoiceEye = 28;	// a player's eyes above his origin
static float rc_flVoiceOne = 1;
static int rc_iVoiceEnt[RC_VOICE_CHANNELS];	// the entity each voice channel plays on
static int rc_iVoiceLp[RC_VOICE_CHANNELS];	// its low pass now, 256 for none
static int rc_iVoiceGain[RC_VOICE_CHANNELS];	// its volume now, 256 for full
static int rc_iVoiceY[RC_VOICE_CHANNELS];	// the low pass's last output
static int rc_iVoiceWall[RC_VOICE_CHANNELS];	// the world was between the listener and the speaker at the last block
static char rc_szVoiceShow[] = "voice_showchannels";
static char rc_szVoiceStart[] = "ReCoF voice: channel %i plays on entity %i (%s)\n";
static char rc_szVoiceFlat[] = "flat";
static char rc_szVoice3DNote[] = "where he stands";
static char rc_szVoiceWallOn[] = "ReCoF voice: channel %i behind a wall, %i%% volume\n";
static char rc_szVoiceWallOff[] = "ReCoF voice: channel %i in sight\n";

static int RcVoice_Show( void )
{
	cvar_t	*v;

	v = Cvar_FindVar( rc_szVoiceShow );
	return v && (int)v->value;
}

int RcVoice_ChannelEntity( int iChannel );	// voice.cpp

// the proximity voice is on: a story co-op server and rc_voice_3d
static int RcVoice_On( void )
{
	cvar_t	*v;
	char	*rcc;

	v = Cvar_FindVar( rc_szVoice3D );
	if ( !v || !(int)v->value )
		return 0;
	rcc = Info_ValueForKey( cls.physinfo, rc_szRcc );
	return rcc && rcc[0] == '1';
}

// VoiceSE_StartChannel: the entity a voice channel plays on, the speaker's in story co-op
int RcVoice_Entity( int iChannel )
{
	int	ent, e;

	ent = cl.viewentity;
	if ( iChannel < 0 || iChannel >= RC_VOICE_CHANNELS )
		return ent;
	if ( RcVoice_On() )
	{
		e = RcVoice_ChannelEntity( iChannel );
		if ( e > 0 && e < cl.num_entities )
			ent = e;
	}
	rc_iVoiceEnt[iChannel] = ent;
	rc_iVoiceLp[iChannel] = 256;
	rc_iVoiceGain[iChannel] = 256;
	rc_iVoiceY[iChannel] = 0;
	rc_iVoiceWall[iChannel] = 0;
	if ( RcVoice_Show() )
		Con_Printf( rc_szVoiceStart, iChannel, ent, ent == cl.viewentity ? rc_szVoiceFlat : rc_szVoice3DNote );
	return ent;
}

// VoiceSE_EndChannel: the entity it was started on
int RcVoice_EndEntity( int iChannel )
{
	if ( iChannel < 0 || iChannel >= RC_VOICE_CHANNELS || !rc_iVoiceEnt[iChannel] )
		return cl.viewentity;
	return rc_iVoiceEnt[iChannel];
}

// VoiceSE_StartChannel: the attenuation, so a voice falls silent at rc_voice_range units (retail's 1 for the flat voice)
float RcVoice_Attn( int iChannel )
{
	cvar_t	*r;

	if ( iChannel < 0 || iChannel >= RC_VOICE_CHANNELS || rc_iVoiceEnt[iChannel] == cl.viewentity )
		return rc_flVoiceOne;
	r = Cvar_FindVar( rc_szVoiceRange );
	if ( !r || (int)r->value < 100 )
		return rc_flVoiceOne;
	return (float)( rc_flVoiceClip / r->value );
}

// VoiceSE_GetSoundDataCallback: a speaker the world hides from the listener is muffled and quieter
void RcVoice_Filter( int iChannel, short *pSamples, int nSamples )
{
	int		ent, i, lpT, gT, lp, g, y;
	vec3_t	eye;
	trace_t	trace;
	cvar_t	*w;

	if ( iChannel < 0 || iChannel >= RC_VOICE_CHANNELS || nSamples <= 0 )
		return;
	ent = rc_iVoiceEnt[iChannel];
	if ( ent <= 0 || ent == cl.viewentity || ent >= cl.num_entities || !cl.worldmodel )
		return;

	VectorCopy( cl_entities[ent].origin, eye );
	eye[2] += rc_flVoiceEye;
	Q_memset( &trace, 0, sizeof( trace ) );
	trace.fraction = rc_flVoiceOne;
	SV_RecursiveHullCheck( cl.worldmodel->hulls, 0, 0, 1, listener_origin, eye, &trace );

	lpT = 256;
	gT = 256;
	if ( trace.fraction < rc_flVoiceOne )
	{
		w = Cvar_FindVar( rc_szVoiceWall );
		gT = w ? (int)w->value * 256 / 100 : 26;
		if ( gT < 0 )
			gT = 0;
		if ( gT > 256 )
			gT = 256;
		lpT = 52;	// 1 - e^(-2 pi 800 / 22050), in 256ths
	}
	if ( ( lpT < 256 ) != rc_iVoiceWall[iChannel] )
	{
		rc_iVoiceWall[iChannel] = lpT < 256;
		if ( RcVoice_Show() )
		{
			if ( lpT < 256 )
				Con_Printf( rc_szVoiceWallOn, iChannel, gT * 100 / 256 );
			else
				Con_Printf( rc_szVoiceWallOff, iChannel );
		}
	}

	// a quarter of the way to the targets each block, so stepping out from a wall does not click
	lp = rc_iVoiceLp[iChannel];
	g = rc_iVoiceGain[iChannel];
	lp += ( lpT - lp ) / 4;
	g += ( gT - g ) / 4;
	if ( lp - lpT < 4 && lpT - lp < 4 )
		lp = lpT;
	if ( g - gT < 4 && gT - g < 4 )
		g = gT;
	rc_iVoiceLp[iChannel] = lp;
	rc_iVoiceGain[iChannel] = g;

	y = rc_iVoiceY[iChannel];
	if ( lp >= 256 && g >= 256 )
	{
		rc_iVoiceY[iChannel] = pSamples[nSamples - 1];
		return;
	}
	for ( i = 0; i < nSamples; i++ )
	{
		y += ( ( pSamples[i] - y ) * lp ) >> 8;
		pSamples[i] = (short)( ( y * g ) >> 8 );
	}
	rc_iVoiceY[iChannel] = y;
}

// the console's and the menu's disconnect: the user leaves, so a story co-op guest's game does not connect again to the
// host as it does when the host drops it (a load of a co-op save); the server's own drops call CL_Disconnect_f directly
void RcCoop_Disconnect_f( void )
{
	if ( Cvar_FindVar( rc_szLeft ) )
		Cvar_Set( rc_szLeft, rc_szOne );
	CL_Disconnect_f();
}
