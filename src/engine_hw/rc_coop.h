// ReCoF story co-op, engine part (rc_coop.c): every player carried through changelevel2

#ifndef RC_COOP_H
#define RC_COOP_H

int RcCoop_AllowChangelevel2( char *pszLevel );	// Host_Changelevel2_f, multiplayer
int RcCoop_Carried( struct client_s *cl );	// SV_SpawnServer and SV_New_f skip the game's disconnect and connect
int RcCoop_Loadgame( struct client_s *cl );	// SV_WriteSpawn: keep the restored player
void RcCoop_Spawned( struct client_s *cl );	// SV_WriteSpawn: in place of sv.loadgame = FALSE
void RcCoop_Inactivate( void );	// Host_Changelevel2_f, before SV_ActivateServer

#endif
