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
}
