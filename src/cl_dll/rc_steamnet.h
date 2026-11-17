// ReCoF co-op spike: the engine's UDP through Steam's relay network (rc_steamnet.cpp)

#ifndef RC_STEAMNET_H
#define RC_STEAMNET_H

typedef void ( *RcSteamPrint_t )( const char *fmt, ... );

// the host asks Steam for a FakeIP and carries the friends' datagrams to the server socket on iServerPort; bMap127
// shows each friend to the engine as 127.77.0.<n> (for sv_lan 1)
bool RcSteamNet_Host( int iServerPort, bool bRelayOnly, bool bMap127, RcSteamPrint_t pfnPrint );

// a friend sets up the Steam port for the host's FakeIP (169.254.x.x:port); the caller then runs connect
bool RcSteamNet_Join( const char *pszAddr, bool bRelayOnly, RcSteamPrint_t pfnPrint );

// polls the FakeIP request and writes the counters to rc_coop_steam.log (from HUD_Frame)
void RcSteamNet_Frame( void );

void RcSteamNet_Status( RcSteamPrint_t pfnPrint );

#endif
