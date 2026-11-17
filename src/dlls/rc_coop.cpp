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
// G12: seconds before a dead player comes back next to the group, with his things (0: Cry of Fear's own rule, jump
// after 60 s or wait sv_auto_respawn_time, without them)
cvar_t	rc_coop_respawn_time = { "rc_coop_respawn_time", "10", FCVAR_SERVER };

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
		m_bWasAlive[i] = false;
		m_bRestore[i] = false;
		m_iSnapItems[i] = 0;
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

static void RcCoopSyncKeys( void );
static void RcCoopSyncCodes( void );
static float s_flRcNextKeySync;

void CHalfLifeStoryCoop::Think( void )
{
	CHalfLifeMultiplay::Think();

	if ( gpGlobals->time >= s_flRcNextKeySync || gpGlobals->time < s_flRcNextKeySync - 2 )
	{
		s_flRcNextKeySync = gpGlobals->time + 1;
		RcCoopSyncKeys();
		RcCoopSyncCodes();
	}

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

		// G12: when a player dies, note his weapons and ammo and bring his respawn forward; when he is back (the
		// respawn takes everything away), give them back
		CBasePlayer *pPl = (CBasePlayer *)pEnt;
		bool bAlive = pEnt->IsAlive() && pEnt->pev->deadflag == DEAD_NO;
		if ( m_bWasAlive[i] && !bAlive && pEnt->pev->fuser4 == 0 && rc_coop_respawn_time.value > 0 )
		{
			m_iSnapItems[i] = 0;
			for ( int s = 0; s < MAX_ITEM_TYPES; s++ )
			{
				for ( CBasePlayerItem *pItem = pPl->m_rgpPlayerItems[s]; pItem && m_iSnapItems[i] < 24; pItem = pItem->m_pNext )
					m_iszSnapItem[i][m_iSnapItems[i]++] = pItem->pev->classname;
			}
			memcpy( m_rgSnapAmmo[i], pPl->m_rgAmmo, sizeof( m_rgSnapAmmo[i] ) );
			m_bRestore[i] = true;
			float flBack = gpGlobals->time + rc_coop_respawn_time.value;
			if ( pPl->m_flDeadAutoReviveTime == 0 || pPl->m_flDeadAutoReviveTime > flBack )
				pPl->m_flDeadAutoReviveTime = flBack;
			if ( pPl->m_flCoopRespawnDelayTime > flBack )
				pPl->m_flCoopRespawnDelayTime = flBack;
		}
		else if ( !m_bWasAlive[i] && bAlive && m_bRestore[i] )
		{
			m_bRestore[i] = false;
			for ( int n = 0; n < m_iSnapItems[i]; n++ )
			{
				const char *pszName = STRING( m_iszSnapItem[i][n] );
				if ( !pPl->HasNamedPlayerItem( pszName ) )
					pPl->GiveNamedItem( pszName, false, true );
			}
			for ( int a = 0; a < MAX_AMMO_SLOTS; a++ )
			{
				if ( m_rgSnapAmmo[i][a] > pPl->m_rgAmmo[a] )
					pPl->m_rgAmmo[a] = m_rgSnapAmmo[i][a];
			}
			g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s is back with %d weapons\n", STRING( pEnt->pev->netname ), m_iSnapItems[i] ) );
		}
		m_bWasAlive[i] = bAlive;

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

// G9: the item_key pickups (keys, fuses, story objects) belong to the group: each living player carries as many of
// each as the group picked up and has not used, so any of them can open the lock; a lock that takes one takes it from
// everyone. Kept across level changes with the players' inventories.
#define RC_TEAM_KEYS	16
static string_t s_iszKeyName[RC_TEAM_KEYS], s_iszKeyModel[RC_TEAM_KEYS], s_iszKeyClass[RC_TEAM_KEYS];
static int s_iKeyCount[RC_TEAM_KEYS];

static int RcCoopKeySlot( const char *pszName )
{
	for ( int k = 0; k < RC_TEAM_KEYS; k++ )
	{
		if ( s_iszKeyName[k] && !strcmp( STRING( s_iszKeyName[k] ), pszName ) )
			return k;
	}
	return -1;
}

static int RcCoopCopies( CBasePlayer *pPlayer, const char *pszName )
{
	int n = 0, last = pPlayer->m_bThreeSlots ? 3 : 6;
	for ( int s = 1; s <= last; s++ )
	{
		if ( pPlayer->inventorySlot[s] && FStrEq( STRING( pPlayer->inventorySlot[s] ), pszName ) )
			n++;
	}
	return n;
}

// CKeyitemModel::Use, after the item went into the taker's inventory
void RcCoop_KeyTaken( CBaseEntity *pKey )
{
	if ( !RcCoopRules() || FStringNull( pKey->pev->message ) )
		return;
	int k = RcCoopKeySlot( STRING( pKey->pev->message ) );
	if ( k < 0 )
	{
		for ( k = 0; k < RC_TEAM_KEYS && s_iszKeyName[k]; k++ )
			;
		if ( k >= RC_TEAM_KEYS )
			return;
		s_iszKeyName[k] = pKey->pev->message;
		s_iszKeyModel[k] = pKey->pev->model;
		s_iszKeyClass[k] = pKey->pev->classname;
		s_iKeyCount[k] = 0;
	}
	s_iKeyCount[k]++;
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s is the group's now (%d)\n", STRING( s_iszKeyName[k] ), s_iKeyCount[k] ) );
}

// CCofYesNo took the item from the player who used it: one fewer for the group, and from everyone
void RcCoop_KeyUsed( const char *pszItem )
{
	if ( !RcCoopRules() )
		return;
	int k = RcCoopKeySlot( pszItem );
	if ( k < 0 )
		return;
	if ( s_iKeyCount[k] > 0 )
		s_iKeyCount[k]--;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		while ( RcCoopCopies( pPlayer, pszItem ) > s_iKeyCount[k] )
			pPlayer->HasItemInSlot( (char *)pszItem, true );
	}
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s used, the group has %d\n", pszItem, s_iKeyCount[k] ) );
	if ( s_iKeyCount[k] == 0 )
		s_iszKeyName[k] = 0;
}

// every second from Think: each living player gets the group's items he lacks (a newcomer, a respawn, the others
// after a pickup), if his inventory has room
static void RcCoopSyncKeys( void )
{
	for ( int k = 0; k < RC_TEAM_KEYS; k++ )
	{
		if ( !s_iszKeyName[k] || s_iKeyCount[k] <= 0 )
			continue;
		const char *pszName = STRING( s_iszKeyName[k] );
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
			if ( !RcCoopPresent( pEnt ) )
				continue;
			CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
			int n = RcCoopCopies( pPlayer, pszName );
			while ( n < s_iKeyCount[k] && pPlayer->AddItemToInventory( s_iszKeyName[k], s_iszKeyModel[k], s_iszKeyClass[k] ) )
				n++;
		}
	}
}

// G10: Cry of Fear rolls a padlock combination for each player when he spawns and gives a keypad code to the player
// who reads it (a note, a screen, a board); a code one player reads would not open the lock for another. The group
// shares one: the padlock combination of the first player in the game, and the keypad code a player learned last.
static int s_iRcTeamPad[5];
static int s_iRcTeamKeypad = -1;
static int s_iRcKeypadSet[33];	// what this player's keypad code was set to by the group (to tell his own news)

static void RcCoopSyncCodes( void )
{
	int i;
	for ( i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() || pEnt->pev->fuser4 != 0 )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		if ( !s_iRcTeamPad[1] && pPlayer->m_iPadlockNumber[1] && pPlayer->m_iPadlockNumber[2] && pPlayer->m_iPadlockNumber[3] && pPlayer->m_iPadlockNumber[4] )
		{
			for ( int d = 1; d <= 4; d++ )
				s_iRcTeamPad[d] = pPlayer->m_iPadlockNumber[d];
		}
		// a code this player learned himself (different from what the group gave him) is the group's now
		int code = pPlayer->m_iKeypadNumber;
		if ( code != s_iRcTeamKeypad && code != s_iRcKeypadSet[i] && code != 9999 && code != 0 )
		{
			s_iRcTeamKeypad = code;
			g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: keypad code %d is the group's now\n", code ) );
		}
	}
	for ( i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() || pEnt->pev->fuser4 != 0 )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		if ( s_iRcTeamPad[1] )
		{
			for ( int d = 1; d <= 4; d++ )
				pPlayer->m_iPadlockNumber[d] = s_iRcTeamPad[d];
		}
		if ( s_iRcTeamKeypad >= 0 && pPlayer->m_iKeypadNumber != s_iRcTeamKeypad )
			pPlayer->m_iKeypadNumber = s_iRcTeamKeypad;
		s_iRcKeypadSet[i] = pPlayer->m_iKeypadNumber;
	}
}

// G5: cof_teleport moved pMoved. Fired by a trigger he walked into (an arrival room), the other players standing in
// that trigger go with him (its wait would ignore their touch and leave them behind); fired by anything else (a scene's
// relays and managers), the whole group goes. They land on free spots around him.
void RcCoop_TeleportFollow( CBasePlayer *pMoved, CBaseEntity *pCaller )
{
	if ( !RcCoopRules() || !pMoved )
		return;
	bool bTrigger = pCaller && pCaller->pev->solid == SOLID_TRIGGER && !strncmp( STRING( pCaller->pev->classname ), "trigger_", 8 );
	int nMoved = 0;

	// the destination is taken by a teammate who came the same way before: step beside him
	for ( int j = 1; j <= gpGlobals->maxClients; j++ )
	{
		CBaseEntity *pOther = UTIL_PlayerByIndex( j );
		if ( !RcCoopPresent( pOther ) || pOther == pMoved )
			continue;
		Vector d = pOther->pev->origin - pMoved->pev->origin;
		if ( fabs( d.x ) >= 32 || fabs( d.y ) >= 32 || fabs( d.z ) >= 72 )
			continue;
		Vector vecBase = pOther->pev->origin;
		static const float s_flStep[3] = { 40, 72, 104 };
		for ( int r = 0; r < 3; r++ )
		{
			bool bDone = false;
			for ( int a = 0; a < 8 && !bDone; a++ )
			{
				float yaw = ( pMoved->pev->angles.y + 90 + a * 45 ) * ( M_PI / 180.0 );
				Vector vecTry = vecBase + Vector( cos( yaw ) * s_flStep[r], sin( yaw ) * s_flStep[r], 0 );
				if ( RcCoopSpotFree( vecBase, vecTry, pMoved->edict() ) )
				{
					UTIL_SetOrigin( pMoved, vecTry );
					pMoved->m_vPreviousOrigin = vecTry;
					bDone = true;
				}
			}
			if ( bDone )
				break;
		}
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s stepped beside a teammate at the teleport's end\n", STRING( pMoved->pev->netname ) ) );
		break;
	}
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) || pEnt == pMoved )
			continue;
		if ( bTrigger && !RcCoopInside( pCaller, pEnt ) )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		Vector vecBase = pMoved->pev->origin;
		Vector vecSpot = vecBase;
		static const float s_flRing[3] = { 40, 72, 104 };
		bool bFound = false;
		for ( int r = 0; r < 3 && !bFound; r++ )
		{
			for ( int a = 0; a < 8 && !bFound; a++ )
			{
				float yaw = ( pMoved->pev->angles.y + 180 + a * 45 + i * 15 ) * ( M_PI / 180.0 );
				Vector vecTry = vecBase + Vector( cos( yaw ) * s_flRing[r], sin( yaw ) * s_flRing[r], 0 );
				if ( RcCoopSpotFree( vecBase, vecTry, pPlayer->edict() ) )
				{
					vecSpot = vecTry;
					bFound = true;
				}
			}
		}
		UTIL_SetOrigin( pPlayer, vecSpot );
		pPlayer->m_vPreviousOrigin = vecSpot;
		pPlayer->pev->angles = pMoved->pev->angles;
		pPlayer->pev->v_angle.y = pMoved->pev->angles.y;
		pPlayer->pev->fixangle = TRUE;
		pPlayer->pev->velocity = pPlayer->pev->basevelocity = g_vecZero;
		pPlayer->pev->flags &= ~FL_ONGROUND;
		pPlayer->b_GoingThroughADoor = false;
		nMoved++;
	}
	if ( nMoved )
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %d teammates went along with %s (%s)\n", nMoved, STRING( pMoved->pev->netname ),
			bTrigger ? "same trigger" : "scene" ) );
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
		char szSlots[256];
		szSlots[0] = 0;
		for ( int s = 1; s <= 6; s++ )
		{
			const char *pszSlot = pPlayer->inventorySlot[s] ? STRING( pPlayer->inventorySlot[s] ) : "";
			const char *pszShort = strrchr( pszSlot, '/' );
			pszShort = pszShort ? pszShort + 1 : pszSlot;
			if ( strlen( szSlots ) + strlen( pszShort ) + 2 < sizeof( szSlots ) )
			{
				strcat( szSlots, " " );
				strcat( szSlots, pszShort[0] ? pszShort : "-" );
			}
		}
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: player %d slots:%s, padlock %d%d%d%d, keypad %d\n", i, szSlots,
			pPlayer->m_iPadlockNumber[1], pPlayer->m_iPadlockNumber[2], pPlayer->m_iPadlockNumber[3], pPlayer->m_iPadlockNumber[4], pPlayer->m_iKeypadNumber ) );
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
	CVAR_REGISTER( &rc_coop_respawn_time );
}
