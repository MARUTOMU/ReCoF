// ReCoF story co-op: Simon's story for 2 to 4 players on a listen server (rc_coop 1 with maxplayers above 1).
//
// CHalfLifeStoryCoop is the multiplayer rules without deathmatch: the story's level exits work (CChangeLevel returns
// early under deathmatch), the engine carries every player through the level transition (src/engine_hw/rc_coop.c),
// players are teammates, and a player who joins or respawns comes in next to a teammate instead of on a start spot
// that may already be taken or sit in a wall. Single player never makes these rules (InstallGameRules).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"
#include "game.h"

// G1: how long an exit a player walks into waits for the rest of the group before it brings them (0: at once)
cvar_t	rc_coop_exit_wait = { "rc_coop_exit_wait", "60", FCVAR_SERVER };

static CHalfLifeStoryCoop *RcCoopRules( void )
{
	return g_pGameRules ? dynamic_cast<CHalfLifeStoryCoop *>( g_pGameRules ) : NULL;
}

// a living, present teammate other than pSkip (B320: fuser4 1 is a player who left)
static CBasePlayer *RcCoopAnchor( CBasePlayer *pSkip )
{
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || pEnt == pSkip || !pEnt->IsPlayer() )
			continue;
		if ( pEnt->pev->fuser4 != 0 || !pEnt->IsAlive() || pEnt->pev->deadflag != DEAD_NO )
			continue;
		return (CBasePlayer *)pEnt;
	}
	return NULL;
}

// a spot for a standing player at vecSpot: the hull fits, there is floor under it and nothing blocks the way from
// vecFrom (so nobody lands behind a wall)
static bool RcCoopSpotFree( const Vector &vecFrom, const Vector &vecSpot, edict_t *pIgnore )
{
	TraceResult tr;

	UTIL_TraceHull( vecSpot, vecSpot, dont_ignore_monsters, human_hull, pIgnore, &tr );
	if ( tr.fStartSolid || tr.fAllSolid )
		return false;

	UTIL_TraceLine( vecFrom, vecSpot, ignore_monsters, pIgnore, &tr );
	if ( tr.flFraction < 1.0 )
		return false;

	UTIL_TraceHull( vecSpot, vecSpot - Vector( 0, 0, 72 ), ignore_monsters, human_hull, pIgnore, &tr );
	if ( tr.flFraction >= 1.0 || tr.fStartSolid )
		return false;

	return true;
}

CHalfLifeStoryCoop::CHalfLifeStoryCoop()
{
	for ( int i = 0; i < 33; i++ )
	{
		m_flSeen[i] = 0;
		m_bProtected[i] = false;
		m_flOldTakeDamage[i] = DAMAGE_AIM;
		m_bArmed[i] = false;
	}
}

// the player's box overlaps the entity's box
static bool RcCoopInside( CBaseEntity *pBox, CBaseEntity *pPlayer )
{
	return pPlayer->pev->absmin.x <= pBox->pev->absmax.x && pPlayer->pev->absmax.x >= pBox->pev->absmin.x &&
		pPlayer->pev->absmin.y <= pBox->pev->absmax.y && pPlayer->pev->absmax.y >= pBox->pev->absmin.y &&
		pPlayer->pev->absmin.z <= pBox->pev->absmax.z && pPlayer->pev->absmax.z >= pBox->pev->absmin.z;
}

// a player in the game: alive, not one who left (B320 fuser4)
static bool RcCoopPresent( CBaseEntity *pEnt )
{
	return pEnt && pEnt->IsPlayer() && pEnt->pev->fuser4 == 0 && pEnt->IsAlive() && pEnt->pev->deadflag == DEAD_NO;
}

void CHalfLifeStoryCoop::PlayerThink( CBasePlayer *pPlayer )
{
	CHalfLifeMultiplay::PlayerThink( pPlayer );
}

// CmdStart: this player's client sends commands, so it has loaded the level (PreThink returns early in some scenes)
void RcCoop_PlayerCmd( CBasePlayer *pPlayer )
{
	CHalfLifeStoryCoop *pRules = RcCoopRules();
	if ( !pRules )
		return;
	int i = ENTINDEX( pPlayer->edict() );
	if ( i >= 1 && i <= 32 )
		pRules->m_flSeen[i] = gpGlobals->time > 0 ? gpGlobals->time : 0.001;
}

void CHalfLifeStoryCoop::Think( void )
{
	CHalfLifeMultiplay::Think();

	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() )
			continue;

		// G3: a player whose client has not sent a command on this level yet (loading after a level change) cannot
		// be hurt until it does
		bool bLoading = m_flSeen[i] == 0 || gpGlobals->time - m_flSeen[i] > 2.0;
		if ( bLoading && !m_bProtected[i] && pEnt->pev->takedamage != DAMAGE_NO && pEnt->IsAlive() )
		{
			m_bProtected[i] = true;
			m_flOldTakeDamage[i] = pEnt->pev->takedamage;
			pEnt->pev->takedamage = DAMAGE_NO;
		}
		else if ( !bLoading && m_bProtected[i] )
		{
			m_bProtected[i] = false;
			if ( pEnt->pev->takedamage == DAMAGE_NO )
				pEnt->pev->takedamage = m_flOldTakeDamage[i];
		}

		// G2: a player who arrived inside an exit (the way back, often) arms its exits once he has stood outside all
		if ( !m_bArmed[i] && !bLoading )
		{
			bool bInside = false;
			CBaseEntity *pExit = NULL;
			while ( !bInside && ( pExit = UTIL_FindEntityByClassname( pExit, "trigger_changelevel" ) ) != NULL )
				bInside = RcCoopInside( pExit, pEnt );
			if ( !bInside )
				m_bArmed[i] = true;
		}
	}
}

// G1: put the players who are not in the exit next to the one who reached it, with their weapons, so the level
// change carries them from where it carries him
static void RcCoopGather( CBaseEntity *pTrigger, CBaseEntity *pActivator )
{
	if ( !pActivator || !pActivator->IsPlayer() )
		return;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) || pEnt == pActivator || ( pTrigger && RcCoopInside( pTrigger, pEnt ) ) )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		Vector vecBase = pActivator->pev->origin;
		Vector vecSpot = vecBase;
		static const float s_flRing[3] = { 40, 72, 104 };
		bool bFound = false;
		for ( int r = 0; r < 3 && !bFound; r++ )
		{
			for ( int a = 0; a < 8 && !bFound; a++ )
			{
				float yaw = ( pActivator->pev->angles.y + 180 + a * 45 + i * 15 ) * ( M_PI / 180.0 );
				Vector vecTry = vecBase + Vector( cos( yaw ) * s_flRing[r], sin( yaw ) * s_flRing[r], 0 );
				if ( RcCoopSpotFree( vecBase, vecTry, pPlayer->edict() ) )
				{
					vecSpot = vecTry;
					bFound = true;
				}
			}
		}
		UTIL_SetOrigin( pPlayer, vecSpot );
		pPlayer->pev->velocity = g_vecZero;
		for ( int s = 0; s < MAX_ITEM_TYPES; s++ )
		{
			for ( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[s]; pItem; pItem = pItem->m_pNext )
				UTIL_SetOrigin( pItem, vecSpot );
		}
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s brought to the exit\n", STRING( pPlayer->pev->netname ) ) );
	}
}

static EHANDLE s_hRcWaitExit;
static float s_flRcWaitStart, s_flRcWaitTouch, s_flRcWaitMsg;
static bool s_bRcGoNow;

// CChangeLevel::ChangeLevelNow: true to change level now. Outside story co-op always true.
bool RcCoop_ExitReady( CBaseEntity *pTrigger, CBaseEntity *pActivator, bool bTouch )
{
	CHalfLifeStoryCoop *pRules = RcCoopRules();
	if ( !pRules )
		return true;

	// G2: a player who arrived inside this exit does not send the group back until he has left every exit once
	if ( bTouch && pActivator && pActivator->IsPlayer() )
	{
		int a = ENTINDEX( pActivator->edict() );
		if ( a >= 1 && a <= 32 && !pRules->m_bArmed[a] )
			return false;
	}

	int nPresent = 0, nInside = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) )
			continue;
		nPresent++;
		if ( RcCoopInside( pTrigger, pEnt ) )
			nInside++;
	}

	float flWait = rc_coop_exit_wait.value;
	bool bGo = !bTouch || flWait <= 0 || nInside >= nPresent || s_bRcGoNow;
	if ( !bGo )
	{
		// a new wait: another exit, or nobody stood in this one for 2 s
		if ( (CBaseEntity *)s_hRcWaitExit != pTrigger || gpGlobals->time - s_flRcWaitTouch > 2.0 )
		{
			s_hRcWaitExit = pTrigger;
			s_flRcWaitStart = gpGlobals->time;
			s_flRcWaitMsg = 0;
		}
		s_flRcWaitTouch = gpGlobals->time;
		float flLeft = s_flRcWaitStart + flWait - gpGlobals->time;
		if ( flLeft <= 0 )
			bGo = true;
		else if ( gpGlobals->time >= s_flRcWaitMsg )
		{
			s_flRcWaitMsg = gpGlobals->time + 1.0;
			hudtextparms_t tp;
			memset( &tp, 0, sizeof( tp ) );
			tp.x = -1;
			tp.y = 0.8;
			tp.r1 = tp.g1 = tp.b1 = 220;
			tp.a1 = 255;
			tp.fadeoutTime = 0.2;
			tp.holdTime = 1.2;
			tp.channel = 4;
			UTIL_HudMessageAll( tp, UTIL_VarArgs( "Waiting at the exit: %d of %d players (%d s)", nInside, nPresent, (int)( flLeft + 0.99 ) ) );
			if ( (int)( flLeft + 0.99 ) % 5 == 0 )
				g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: waiting at the exit, %d of %d players, %d s\n", nInside, nPresent, (int)( flLeft + 0.99 ) ) );
		}
	}
	if ( !bGo )
		return false;

	RcCoopGather( pTrigger, pActivator );
	s_hRcWaitExit = NULL;
	s_bRcGoNow = false;
	return true;
}

// rc_coop_go (server command, so the host's console or rcon only): the exit someone waits at fires now
static void RcCoopGo( void )
{
	s_bRcGoNow = true;
	g_engfuncs.pfnServerPrint( "ReCoF co-op: the next exit touch goes now\n" );
}

BOOL CHalfLifeStoryCoop::IsDeathmatch( void )
{
	return FALSE;
}

BOOL CHalfLifeStoryCoop::IsCoOp( void )
{
	return TRUE;
}

int CHalfLifeStoryCoop::PlayerRelationship( CBaseEntity *pPlayer, CBaseEntity *pTarget )
{
	return GR_TEAMMATE;
}

BOOL CHalfLifeStoryCoop::FPlayerCanTakeDamage( CBasePlayer *pPlayer, CBaseEntity *pAttacker )
{
	// no friendly fire between players
	if ( pAttacker && pAttacker != pPlayer && pAttacker->IsPlayer() )
		return FALSE;

	return CHalfLifeMultiplay::FPlayerCanTakeDamage( pPlayer, pAttacker );
}

edict_t *CHalfLifeStoryCoop::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	CBasePlayer *pAnchor = RcCoopAnchor( pPlayer );
	edict_t *pStart = FIND_ENTITY_BY_CLASSNAME( NULL, "info_player_start" );

	if ( !pAnchor )
	{
		// the first player: the start single player uses
		if ( FNullEnt( pStart ) )
			return CHalfLifeMultiplay::GetPlayerSpawnSpot( pPlayer );

		pPlayer->pev->origin = pStart->v.origin + Vector( 0, 0, 1 );
		pPlayer->pev->v_angle = g_vecZero;
		pPlayer->pev->velocity = g_vecZero;
		pPlayer->pev->angles = pStart->v.angles;
		pPlayer->pev->punchangle = g_vecZero;
		pPlayer->pev->fixangle = TRUE;
		pPlayer->m_vPreviousOrigin = pPlayer->pev->origin;
		if ( pStart->v.target )
			FireTargets( STRING( pStart->v.target ), pPlayer, pPlayer, USE_TOGGLE, 0 );
		return pStart;
	}

	// next to a teammate: rings of 8 spots at 48, 80 and 112 units, the first that is free
	Vector vecBase = pAnchor->pev->origin;
	Vector vecSpot = vecBase;
	bool bFound = false;
	static const float s_flRing[3] = { 48, 80, 112 };
	for ( int r = 0; r < 3 && !bFound; r++ )
	{
		for ( int a = 0; a < 8 && !bFound; a++ )
		{
			float yaw = ( pAnchor->pev->angles.y + 180 + a * 45 ) * ( M_PI / 180.0 );
			Vector vecTry = vecBase + Vector( cos( yaw ) * s_flRing[r], sin( yaw ) * s_flRing[r], 0 );
			if ( RcCoopSpotFree( vecBase, vecTry, pPlayer->edict() ) )
			{
				vecSpot = vecTry;
				bFound = true;
			}
		}
	}

	pPlayer->pev->origin = vecSpot + Vector( 0, 0, 1 );
	pPlayer->pev->v_angle = g_vecZero;
	pPlayer->pev->velocity = g_vecZero;
	pPlayer->pev->angles = pAnchor->pev->angles;
	pPlayer->pev->angles.x = 0;
	pPlayer->pev->angles.z = 0;
	pPlayer->pev->punchangle = g_vecZero;
	pPlayer->pev->fixangle = TRUE;
	pPlayer->m_vPreviousOrigin = pPlayer->pev->origin;
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s comes in next to %s%s\n", STRING( pPlayer->pev->netname ), STRING( pAnchor->pev->netname ), bFound ? "" : " (no free spot, on him)" ));

	return pAnchor->edict();
}

// rc_coop_dump (server command): every player's slot, health, place and weapons, for tests
static void RcCoopDump( void )
{
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: map %s, %d slots, rules %s\n", STRING( gpGlobals->mapname ), gpGlobals->maxClients,
		g_pGameRules && g_pGameRules->IsCoOp() && !g_pGameRules->IsDeathmatch() ? "story co-op" : "other" ));
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		char szItems[512];
		szItems[0] = 0;
		int nItems = 0;
		for ( int s = 0; s < MAX_ITEM_TYPES; s++ )
		{
			for ( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[s]; pItem; pItem = pItem->m_pNext )
			{
				if ( strlen( szItems ) + strlen( STRING( pItem->pev->classname ) ) + 2 < sizeof( szItems ) )
				{
					strcat( szItems, " " );
					strcat( szItems, STRING( pItem->pev->classname ) );
				}
				nItems++;
			}
		}
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: player %d health %.0f %s at %.0f %.0f %.0f, left %d, %d items:%s\n", i, pPlayer->pev->health,
			pPlayer->IsAlive() ? "alive" : "dead", pPlayer->pev->origin.x, pPlayer->pev->origin.y, pPlayer->pev->origin.z,
			pPlayer->pev->fuser4 != 0, nItems, szItems ));
	}
}

void RcCoop_Init( void )
{
	g_engfuncs.pfnAddServerCommand( "rc_coop_dump", RcCoopDump );
	g_engfuncs.pfnAddServerCommand( "rc_coop_go", RcCoopGo );
	CVAR_REGISTER( &rc_coop_exit_wait );
}
