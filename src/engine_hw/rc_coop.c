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
#include "rc_coop.h"

#define RC_MAX_CLIENTS	32
#define RC_CARRY_SECONDS	90

static char rc_szCoop[] = "rc_coop";
static char rc_szSkip[] = "rc_coop_skip";	// hl.dll rc_coop.cpp, G6
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
