//=========================================================
// cof_phonecall: rings the player's mobile phone and plays
// a call with subtitles when it is answered
//=========================================================

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"

extern void UTIL_ProMessage( CBaseEntity *pEntity, const char *pMessage, int colour, bool all );

// CoF: declared in cof_subtitlemain.h in the original source
class CSubtitle : public CBaseEntity
{
public:
	void Spawn( void );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	virtual int ObjectCaps( void ) { return CBaseEntity :: ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	float CurrentLine;

	void KeyValue( KeyValueData *pkvd );

	virtual int		Save( CSave &save );
	virtual int		Restore( CRestore &restore );
	static	TYPEDESCRIPTION m_SaveData[];
};

class CPhoneCall : public CBaseEntity
{
public:
	void Spawn( void );
	void Precache( void );

	bool m_bRinging;

	void EXPORT WaitForCall( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	virtual int ObjectCaps( void ) { return CBaseEntity :: ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	virtual int		Save( CSave &save );
	virtual int		Restore( CRestore &restore );

	void EXPORT ChangeSubtitle( void );
	void EXPORT EndCall( void );

	EHANDLE m_pPlayer;
	int m_iSubtitleNumber[33];
	float m_fSubtitleNumberNext[33];
	string_t m_sAudioFile;
	int m_iCurrentSubtitle;

	static	TYPEDESCRIPTION m_SaveData[];

	EHANDLE m_hPhoneUser;
	bool m_bAnswered;

	// ReCoF (B471): story co-op rings a teammate who can answer and starts a dropped call again; the target fires
	// once, after 30 s with nobody able to answer
	float m_flRcNoTaker;
	bool m_bRcFired;
	bool RcCanTake( CBasePlayer *pPlayer );
	CBasePlayer *RcTaker( void );
	void RcDrop( CBasePlayer *pPlayer );
	void RcFire( void );
};

TYPEDESCRIPTION	CPhoneCall::m_SaveData[] =
{
	DEFINE_ARRAY( CPhoneCall, m_iSubtitleNumber, FIELD_INTEGER, 33 ),
	DEFINE_ARRAY( CPhoneCall, m_fSubtitleNumberNext, FIELD_FLOAT, 33 ),
	DEFINE_FIELD( CPhoneCall, m_sAudioFile, FIELD_SOUNDNAME ),
	DEFINE_FIELD( CPhoneCall, m_iCurrentSubtitle, FIELD_INTEGER ),
	DEFINE_FIELD( CPhoneCall, m_hPhoneUser, FIELD_EHANDLE ),
	DEFINE_FIELD( CPhoneCall, m_bAnswered, FIELD_BOOLEAN ),
	DEFINE_FIELD( CPhoneCall, m_pPlayer, FIELD_EHANDLE ),
	DEFINE_FIELD( CPhoneCall, m_flRcNoTaker, FIELD_TIME ),	// ReCoF (B471)
	DEFINE_FIELD( CPhoneCall, m_bRcFired, FIELD_BOOLEAN ),
};

IMPLEMENT_SAVERESTORE( CPhoneCall, CBaseEntity );

LINK_ENTITY_TO_CLASS( cof_phonecall, CPhoneCall );

void CPhoneCall::Spawn( void )
{
	Precache();

	UTIL_SetOrigin( this, pev->origin );
	pev->solid = SOLID_NOT;
	pev->effects = EF_NODRAW;
	pev->v_angle = g_vecZero;

	m_bRinging = false;

	if ( !m_iCurrentSubtitle )
		m_iCurrentSubtitle = 1;
}

void CPhoneCall::Precache( void )
{
	if ( m_sAudioFile )
		PRECACHE_SOUND( (char *)STRING( m_sAudioFile ) );

	if ( pev->iuser1 )
		PRECACHE_SOUND( "phone/phone_ringing.wav" );
}

void CPhoneCall::KeyValue( KeyValueData *pkvd )
{
	if ( FStrEq( pkvd->szKeyName, "subtitlenumber1" ) )
	{
		m_iSubtitleNumber[1] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber2" ) )
	{
		m_iSubtitleNumber[2] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber3" ) )
	{
		m_iSubtitleNumber[3] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber4" ) )
	{
		m_iSubtitleNumber[4] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber5" ) )
	{
		m_iSubtitleNumber[5] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber6" ) )
	{
		m_iSubtitleNumber[6] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber7" ) )
	{
		m_iSubtitleNumber[7] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber8" ) )
	{
		m_iSubtitleNumber[8] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber9" ) )
	{
		m_iSubtitleNumber[9] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber10" ) )
	{
		m_iSubtitleNumber[10] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber11" ) )
	{
		m_iSubtitleNumber[11] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber12" ) )
	{
		m_iSubtitleNumber[12] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber13" ) )
	{
		m_iSubtitleNumber[13] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber14" ) )
	{
		m_iSubtitleNumber[14] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber15" ) )
	{
		m_iSubtitleNumber[15] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber16" ) )
	{
		m_iSubtitleNumber[16] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber17" ) )
	{
		m_iSubtitleNumber[17] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber18" ) )
	{
		m_iSubtitleNumber[18] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber19" ) )
	{
		m_iSubtitleNumber[19] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber20" ) )
	{
		m_iSubtitleNumber[20] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber21" ) )
	{
		m_iSubtitleNumber[21] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber22" ) )
	{
		m_iSubtitleNumber[22] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber23" ) )
	{
		m_iSubtitleNumber[23] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber24" ) )
	{
		m_iSubtitleNumber[24] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber25" ) )
	{
		m_iSubtitleNumber[25] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber26" ) )
	{
		m_iSubtitleNumber[26] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber27" ) )
	{
		m_iSubtitleNumber[27] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber28" ) )
	{
		m_iSubtitleNumber[28] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber29" ) )
	{
		m_iSubtitleNumber[29] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber30" ) )
	{
		m_iSubtitleNumber[30] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber31" ) )
	{
		m_iSubtitleNumber[31] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitlenumber32" ) )
	{
		m_iSubtitleNumber[32] = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime1" ) )
	{
		m_fSubtitleNumberNext[1] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime2" ) )
	{
		m_fSubtitleNumberNext[2] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime3" ) )
	{
		m_fSubtitleNumberNext[3] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime4" ) )
	{
		m_fSubtitleNumberNext[4] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime5" ) )
	{
		m_fSubtitleNumberNext[5] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime6" ) )
	{
		m_fSubtitleNumberNext[6] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime7" ) )
	{
		m_fSubtitleNumberNext[7] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime8" ) )
	{
		m_fSubtitleNumberNext[8] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime9" ) )
	{
		m_fSubtitleNumberNext[9] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime10" ) )
	{
		m_fSubtitleNumberNext[10] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime11" ) )
	{
		m_fSubtitleNumberNext[11] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime12" ) )
	{
		m_fSubtitleNumberNext[12] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime13" ) )
	{
		m_fSubtitleNumberNext[13] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime14" ) )
	{
		m_fSubtitleNumberNext[14] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime15" ) )
	{
		m_fSubtitleNumberNext[15] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime16" ) )
	{
		m_fSubtitleNumberNext[16] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime17" ) )
	{
		m_fSubtitleNumberNext[17] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime18" ) )
	{
		m_fSubtitleNumberNext[18] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime19" ) )
	{
		m_fSubtitleNumberNext[19] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime20" ) )
	{
		m_fSubtitleNumberNext[20] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime21" ) )
	{
		m_fSubtitleNumberNext[21] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime22" ) )
	{
		m_fSubtitleNumberNext[22] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime23" ) )
	{
		m_fSubtitleNumberNext[23] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime24" ) )
	{
		m_fSubtitleNumberNext[24] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime25" ) )
	{
		m_fSubtitleNumberNext[25] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime26" ) )
	{
		m_fSubtitleNumberNext[26] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime27" ) )
	{
		m_fSubtitleNumberNext[27] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime28" ) )
	{
		m_fSubtitleNumberNext[28] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime29" ) )
	{
		m_fSubtitleNumberNext[29] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime30" ) )
	{
		m_fSubtitleNumberNext[30] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime31" ) )
	{
		m_fSubtitleNumberNext[31] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "subtitletime32" ) )
	{
		m_fSubtitleNumberNext[32] = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "audiofile" ) )
	{
		m_sAudioFile = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseEntity::KeyValue( pkvd );
}

// a player in the game who can take a call: alive, not one who left (B320), with a client (B391) and a phone
bool CPhoneCall::RcCanTake( CBasePlayer *pPlayer )
{
	if ( !pPlayer || !pPlayer->IsPlayer() || !pPlayer->IsAlive() || pPlayer->pev->deadflag != DEAD_NO || pPlayer->pev->fuser4 != 0 )
		return false;
	char *pszInfo = g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() );
	return pszInfo && pszInfo[0] && pPlayer->HasNamedPlayerItem( "weapon_mobile" );
}

// the activator when he can take the call, else the first teammate who can
CBasePlayer *CPhoneCall::RcTaker( void )
{
	CBaseEntity *pWas = m_pPlayer;
	if ( pWas && pWas->IsPlayer() && RcCanTake( (CBasePlayer *)pWas ) )
		return (CBasePlayer *)pWas;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( pEnt && pEnt->IsPlayer() && RcCanTake( (CBasePlayer *)pEnt ) )
			return (CBasePlayer *)pEnt;
	}
	return NULL;
}

// this player's part of the call ends: its sounds, the holds and the bars
void CPhoneCall::RcDrop( CBasePlayer *pPlayer )
{
	if ( !pPlayer )
		return;
	STOP_SOUND( pPlayer->edict(), CHAN_AUTO, pev->iuser1 ? "phone/phone_ringing.wav" : "phone/phone_ringtone.wav" );
	if ( m_sAudioFile )
		STOP_SOUND( pPlayer->edict(), CHAN_AUTO, STRING( m_sAudioFile ) );
	if ( pPlayer->m_iUsingComputerOrPhone == 3 )
		pPlayer->m_iUsingComputerOrPhone = 0;
	if ( pPlayer->pev->fuser3 == 5 )
		pPlayer->pev->fuser3 = 0;
	pPlayer->m_bInPhoneCall = false;
	pPlayer->m_iHideHUD &= ~HIDEHUD_HEALTH;
	pPlayer->m_DrawBars = 0;
}

void CPhoneCall::RcFire( void )
{
	if ( pev->target && !m_bRcFired )
	{
		m_bRcFired = true;
		FireTargets( STRING( pev->target ), this, this, USE_TOGGLE, 0 );
	}
}

void CPhoneCall::WaitForCall( void )
{
	// ReCoF (B471): story co-op rings whoever can take the call; with nobody for 30 s the story goes on without it
	if ( RcCoop_Story() )
	{
		CBasePlayer *pTaker = RcTaker();
		if ( !pTaker )
		{
			if ( m_flRcNoTaker <= 0 )
				m_flRcNoTaker = gpGlobals->time;
			if ( gpGlobals->time - m_flRcNoTaker > 30 )
			{
				g_engfuncs.pfnServerPrint( "ReCoF co-op: nobody could take a phone call, the story goes on\n" );
				RcFire();
				SetThink( NULL );
				return;
			}
			SetNextThink( 1 );
			return;
		}
		m_flRcNoTaker = 0;
		CBaseEntity *pWas = m_pPlayer;
		if ( pTaker != pWas )
		{
			if ( pWas && pWas->IsPlayer() )
				RcDrop( (CBasePlayer *)pWas );
			m_pPlayer = pTaker;
			m_bRinging = false;
		}
	}

	if ( !m_pPlayer || !m_pPlayer->IsPlayer() )
		return;

	CBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)m_pPlayer->pev );

	if ( !pPlayer )
		return;

	// wait until the player stands on the ground
	if ( !( pPlayer->pev->flags & FL_ONGROUND ) )
	{
		SetNextThink( 0.1 );
		return;
	}

	if ( !pPlayer->HasNamedPlayerItem( "weapon_mobile" ) )
		return;

	if ( !m_bRinging )
	{
		m_bRinging = true;

		if ( pev->iuser1 )
		{
			EMIT_SOUND_DYN( ENT( pPlayer->pev ), CHAN_AUTO, "phone/phone_ringing.wav", 1, 0.8, 0, 100 );

			pPlayer->m_iHideHUD |= HIDEHUD_HEALTH;
			pPlayer->m_DrawBars = 1;

			pPlayer->pev->fuser3 = 5;

			SetNextThink( 3 );
			return;
		}
		else
		{
			EMIT_SOUND_DYN( ENT( pPlayer->pev ), CHAN_AUTO, "phone/phone_ringtone.wav", 1, 0.8, 0, 100 );

			pPlayer->m_iUsingComputerOrPhone = 3;
		}
	}

	if ( pPlayer->m_pActiveItem && FClassnameIs( pPlayer->m_pActiveItem->edict(), "weapon_mobile" ) && pPlayer->IsAlive() && gpGlobals->time > pPlayer->m_flNextAttack )
	{
		pPlayer->m_iHideHUD |= HIDEHUD_HEALTH;
		pPlayer->m_DrawBars = 1;

		pPlayer->pev->fuser3 = 5;

		m_iCurrentSubtitle = 1;

		SetNextThink( 3 );
		SetThink( &CPhoneCall::ChangeSubtitle );
		m_hPhoneUser = pPlayer;
		pPlayer->m_flNextAttack = gpGlobals->time + 4;
	}

	// CoF: also reached after the call was answered, which overrides the 3 second delay above (original bug)
	SetNextThink( 0.1 );
}

void CPhoneCall::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( !pActivator )
		return;

	if ( !pActivator->IsPlayer() )
		return;

	CBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)pActivator->pev );

	m_bAnswered = false;
	m_bRcFired = false;	// ReCoF (B471)
	m_flRcNoTaker = 0;

	if ( pPlayer )
	{
		m_pPlayer = pPlayer;
		SetNextThink( 0.1 );
		SetThink( &CPhoneCall::WaitForCall );
	}
}

void CPhoneCall::ChangeSubtitle( void )
{
	// ReCoF (B471): in story co-op a call whose taker died, left or put the phone away rings again (for him or a
	// teammate) instead of stopping for good with its target never fired
	if ( RcCoop_Story() )
	{
		CBaseEntity *pUser = m_hPhoneUser;
		CBasePlayer *pTaker = ( pUser && pUser->IsPlayer() ) ? (CBasePlayer *)pUser : NULL;
		if ( !pTaker || !RcCanTake( pTaker ) || !pTaker->m_pActiveItem || !FClassnameIs( pTaker->m_pActiveItem->edict(), "weapon_mobile" ) )
		{
			RcDrop( pTaker );
			m_bAnswered = false;
			m_bRinging = false;
			m_iCurrentSubtitle = 1;
			m_hPhoneUser = NULL;
			SetThink( &CPhoneCall::WaitForCall );
			SetNextThink( 1 );
			g_engfuncs.pfnServerPrint( "ReCoF co-op: a phone call was dropped, it rings again\n" );
			return;
		}
	}

	if ( !m_hPhoneUser || !m_hPhoneUser->IsPlayer() )
		return;

	CBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)m_hPhoneUser->pev );

	if ( !pPlayer || !pPlayer->m_pActiveItem || !FClassnameIs( pPlayer->m_pActiveItem->edict(), "weapon_mobile" ) )
		return;

	if ( !pPlayer->IsAlive() )
	{
		UTIL_ProMessage( m_hPhoneUser, " ", 2, false );
		EMIT_SOUND( ENT( m_hPhoneUser->pev ), CHAN_AUTO, "common/null.wav", 1, 0.8 );
		return;
	}

	CMobile *pMobile = GetClassPtr( (CMobile *)pPlayer->m_pActiveItem->pev );

	if ( !pMobile )
		return;

	if ( !m_bAnswered )
	{
		m_bAnswered = true;
		SetNextThink( m_fSubtitleNumberNext[m_iCurrentSubtitle] );
		SetThink( &CPhoneCall::ChangeSubtitle );

		if ( pev->iuser1 )
			STOP_SOUND( pPlayer->edict(), CHAN_AUTO, "phone/phone_ringing.wav" );
		else
			STOP_SOUND( pPlayer->edict(), CHAN_AUTO, "phone/phone_ringtone.wav" );

		EMIT_SOUND_DYN( ENT( pPlayer->pev ), CHAN_AUTO, "phone/tone_send.wav", 1, 0.8, 0, 100 );

		if ( m_sAudioFile )
			EMIT_SOUND( ENT( m_hPhoneUser->pev ), CHAN_AUTO, STRING( m_sAudioFile ), 1, 0.8 );

		// raise the phone to the ear
		if ( pPlayer->m_iMobileFlashMode )
		{
			pMobile->m_flTimeWeaponIdle = gpGlobals->time + 1.1;
			pMobile->SendWeaponAnim( 13 ); // MOBILE_RAISE_FLASH
			pPlayer->m_bInPhoneCall = true;
		}
		else
		{
			pMobile->m_flTimeWeaponIdle = gpGlobals->time + 1.1;
			pMobile->SendWeaponAnim( 12 ); // MOBILE_RAISE_SMS
			pPlayer->m_bInPhoneCall = true;
		}

		return;
	}

	int nextsubtitle = m_iCurrentSubtitle + 1;

	CBaseEntity *pSubMain = NULL;
	pSubMain = UTIL_FindEntityByClassname( NULL, "subtitle_main" );

	if ( pSubMain )
	{
		CBaseEntity *pPlayer = UTIL_FirstPlayer_rc();

		if ( pPlayer )
		{
			if ( m_iSubtitleNumber[m_iCurrentSubtitle] )
			{
				GetClassPtr( (CSubtitle *)pSubMain->pev )->CurrentLine = m_iSubtitleNumber[m_iCurrentSubtitle];
				pSubMain->Use( pPlayer, pPlayer, USE_TOGGLE, 0 );
			}
			else
				ALERT( at_console, "SUBTITLE CHANGE: Could not change number!\n" );
		}
	}

	if ( nextsubtitle <= 32 )
	{
		if ( m_iSubtitleNumber[nextsubtitle] && m_fSubtitleNumberNext[nextsubtitle] > 0 )
		{
			m_iCurrentSubtitle++;
			SetNextThink( m_fSubtitleNumberNext[m_iCurrentSubtitle] );
		}

		if ( m_fSubtitleNumberNext[nextsubtitle] < 0.1 )
		{
			SetThink( &CPhoneCall::EndCall );
			SetNextThink( 1 );
		}
	}
}

void CPhoneCall::EndCall( void )
{
	// ReCoF (B471): the target fires and the call's holds end even when the phone is no longer in hand; retail
	// returned first, so the story stopped there and the player kept the bars, the hidden HUD, fuser3 5 and the call
	// lock (charliee1w 6446bb0f, 03fb6cad)
	CBaseEntity *pUser = m_hPhoneUser;
	CBasePlayer *pPlayer = ( pUser && pUser->IsPlayer() ) ? GetClassPtr( (CBasePlayer *)pUser->pev ) : NULL;

	RcFire();
	SetThink( NULL );

	if ( !pPlayer )
		return;

	pPlayer->m_iUsingComputerOrPhone = 0;

	if ( !pPlayer->m_pActiveItem || !FClassnameIs( pPlayer->m_pActiveItem->edict(), "weapon_mobile" ) )
	{
		pPlayer->m_bInPhoneCall = false;
		pPlayer->m_iHideHUD &= ~HIDEHUD_HEALTH;
		pPlayer->m_DrawBars = 0;
		pPlayer->pev->fuser3 = 0;
		return;
	}

	CMobile *pMobile = GetClassPtr( (CMobile *)pPlayer->m_pActiveItem->pev );

	if ( !pMobile )
	{
		pPlayer->m_bInPhoneCall = false;
		pPlayer->m_iHideHUD &= ~HIDEHUD_HEALTH;
		pPlayer->m_DrawBars = 0;
		pPlayer->pev->fuser3 = 0;
		return;
	}

	// lower the phone
	if ( pPlayer->m_iMobileFlashMode )
	{
		pMobile->m_flTimeWeaponIdle = gpGlobals->time + 1.1;
		pMobile->SendWeaponAnim( 16 ); // MOBILE_LOWER_FLASH
		pPlayer->m_flNextAttack = gpGlobals->time + 1.1;
		pPlayer->m_bInPhoneCall = false;
	}
	else
	{
		pMobile->m_flTimeWeaponIdle = gpGlobals->time + 1.1;
		pMobile->SendWeaponAnim( 15 ); // MOBILE_LOWER_SMS
		pPlayer->m_flNextAttack = gpGlobals->time + 1.1;
		pPlayer->m_bInPhoneCall = false;
	}

	EMIT_SOUND_DYN( ENT( pPlayer->pev ), CHAN_AUTO, "phone/tone_send.wav", 1, 0.8, 0, 100 );

	pPlayer->m_iHideHUD &= ~HIDEHUD_HEALTH;
	pPlayer->m_DrawBars = 0;

	pPlayer->pev->fuser3 = 0;
}
