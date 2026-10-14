/* music_control.c -- CD-DA music */
#include <sys/types.h>
#include <STDIO.H>
#include <LIBCD.H>
#include <LIBGTE.H>
#include <AUDIO.H>

/* g_musicFlags bits. name: inferred from the CD routines below. */
enum {
    MUSIC_ENABLED = 0x01,  /* cleared by Music_Disable, set by Music_Enable; every request tests it */
    MUSIC_NEED_TOC = 0x02, /* Music_Init reads the TOC once and clears it */
    MUSIC_POLLING = 0x04   /* a track was started; Music_Poll polls the drive status */
};

/* Ticks between status polls once a track has started. */
#define MUSIC_POLL_TICKS 0x190
/* Ticks before the next poll once the drive has stopped. name: invented */
#define MUSIC_IDLE_TICKS 0x3c
/* g_musicTimer's initial value. name: invented */
#define MUSIC_START_TICKS 100

/* Local CD buffers: Music_Init's parameter frame (9-16 bytes of locals in retail's frame) and
 * the 8-byte CdlGetlocP result. names: invented */
#define CD_PARAM_BUFFER_SIZE 16
#define CD_RESULT_SIZE 8

unsigned short g_musicFlags = MUSIC_NEED_TOC;
short          g_musicResult = -1;
short          g_musicCallbackState = 0;
short          g_musicTimer = MUSIC_START_TICKS;

/* Set by Music_Resume, cleared by Music_Init; nothing in retail reads it.
 * name: inferred from those two stores */
extern int g_musicResumed;
/* Module statics (.sbss 0x8008d200..0x8008d210), in their retail order. */
static u_char g_cdMode;
static u_char g_cdRequestedCommand;
static short  g_cdTrackCount;
extern CdlLOC g_cdToc[];
/* CD track per level (36 halfwords, .data 0x80073174). */
/* name: analysis/runtime/globals.json */
short g_levelMusicTracks[LEVEL_AUDIO_TABLE_LENGTH] = { 10, 18, 2,  24, 16, 2,  14, 2,  12, 2,  20, 22,
                                                       10, 24, 18, 12, 14, 24, 20, 14, 6,  22, 18, 8,
                                                       4,  8,  16, 6,  8,  14, 4,  8,  20, 8,  22, 0 };

/*
==================
Music_Resume
==================
*/
void Music_Resume( void )
{
    u_char *mode = &g_cdMode;
    g_musicResumed = 1;
    *mode = CdlModeRept | CdlModeAP | CdlModeDA;
    CdControlB( CdlSetmode, mode, 0 );
    g_cdRequestedCommand = CdlSeekP;
    CdControl( CdlSeekP, (u_char *)&g_cdToc[g_musicResult], 0 );
}

/*
==================
Music_CDComplete
==================
*/
void Music_CDComplete( int status, u_char *result )
{
    if ( status == CdlComplete ) {
        if ( g_cdRequestedCommand == CdlSeekP ) {
            g_cdRequestedCommand = CdlPlay;
            CdControl( CdlPlay, 0, 0 );
        }
    } else {
        printf( "\n******\nCbComplete error: %s\n", CdIntstr( status ) );
    }
}

/*
==================
Music_CDDataReady
==================
*/
void Music_CDDataReady( int status, u_char *result )
{
    if ( status != CdlDataReady )
        printf( "cbdataready error:%s\n", CdIntstr( status ) );
}

/*
==================
Music_Init
==================
*/
/* name: inferred from its body, which sets up CD audio playback */
int Music_Init( void )
{
    unsigned short flags = g_musicFlags;
    u_char         param[CD_PARAM_BUFFER_SIZE];
    if ( ( flags & MUSIC_ENABLED ) == 0 || ( flags & MUSIC_NEED_TOC ) == 0 )
        return 0;
    Music_SetVolume( VOLUME_PERCENT_MAX );
    g_musicResumed = 0;
    CdSetDebug( 0 );
    param[0] = CdlModeRept | CdlModeAP | CdlModeDA;
    CdControlB( CdlSetmode, param, 0 );
    g_cdTrackCount = CdGetToc( g_cdToc );
    g_musicResult = -1;
    g_musicCallbackState = 0;
    g_musicTimer = MUSIC_POLL_TICKS;
    g_musicFlags &= ~MUSIC_NEED_TOC;
    return -1;
}

/*
==================
Music_PlayTrack
==================
*/
/* name: inferred from its body, which starts a CD audio track */
short Music_PlayTrack( int value )
{
    unsigned short flags = g_musicFlags;
    short          selected = (short)value;
    u_char         mode;
    if ( ( flags & MUSIC_ENABLED ) == 0 )
        return 0;
    if ( flags & MUSIC_NEED_TOC )
        Music_Init();
    if ( selected == -1 )
        return (short)Music_Stop();
    if ( g_cdTrackCount < selected )
        selected = g_cdTrackCount;
    mode = CdlModeRept | CdlModeDA;
    CdControlB( CdlSetmode, &mode, 0 );
    g_cdRequestedCommand = CdlPlay;
    CdControl( CdlPlay, (u_char *)&g_cdToc[selected], 0 );
    g_musicResult = selected;
    g_musicCallbackState = 0;
    g_musicTimer = MUSIC_POLL_TICKS;
    g_musicFlags |= MUSIC_POLLING;
    return -1;
}

/*
==================
Music_Stop
==================
*/
int Music_Stop( void )
{
    unsigned short flags = g_musicFlags;
    g_musicResult = -1;
    g_musicCallbackState = 0;
    if ( ( flags & MUSIC_ENABLED ) == 0 )
        return 0;
    if ( flags & MUSIC_NEED_TOC )
        Music_Init();
    g_cdRequestedCommand = CdlStop;
    CdControl( CdlStop, 0, 0 );
    g_musicTimer = MUSIC_POLL_TICKS;
    g_musicFlags = g_musicFlags & ~MUSIC_POLLING;
    return -1;
}

/*
==================
Music_Pause
==================
*/
/* name: inferred from its body, which pauses CD audio */
int Music_Pause( void )
{
    if ( ( g_musicFlags & MUSIC_ENABLED ) == 0 )
        return 0;
    g_cdRequestedCommand = CdlPause;
    CdControl( CdlPause, 0, 0 );
    return -1;
}

/*
==================
Music_Unpause
==================
*/
/* name: inferred from its body, which resumes CD audio */
int Music_Unpause( void )
{
    if ( ( g_musicFlags & MUSIC_ENABLED ) == 0 )
        return 0;
    g_cdRequestedCommand = CdlPlay;
    CdControl( CdlPlay, 0, 0 );
    return -1;
}

/*
==================
Music_PlayLevel
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Music_PlayLevel( int menuIndex )
{
    short index = (short)menuIndex;
    if ( ( g_musicFlags & MUSIC_ENABLED ) == 0 )
        return 0;
    if ( g_musicFlags & MUSIC_NEED_TOC )
        Music_Init();
    if ( (unsigned short)menuIndex >= LEVEL_AUDIO_TABLE_LENGTH )
        index = 0;
    Music_PlayTrack( g_levelMusicTracks[index] );
    return -1;
}

/*
==================
Music_Disable
==================
*/
/* name: inferred from its body, which clears the music enable flag */
void Music_Disable( void )
{
    g_musicFlags &= ~MUSIC_ENABLED;
}

/*
==================
Music_Enable
==================
*/
/* name: inferred from its body, which sets the music enable flag */
void Music_Enable( void )
{
    g_musicFlags |= MUSIC_ENABLED;
}

/*
==================
Music_SetVolume
==================
*/
/* name: inferred from its body, which sets the CD audio volume */
void Music_SetVolume( int volume )
{
    if ( g_musicFlags & MUSIC_ENABLED )
        Snd_SPUSetCDVolume( volume * ( SPU_VOLUME_MAX / VOLUME_PERCENT_MAX ), 0 );
}

/*
==================
Music_Poll

Poll the drive status after the signed timer expires.
==================
*/
/* name: inferred from its body, which polls the CD audio state each frame */
int Music_Poll( void )
{
    short  oldTimer;
    u_char result[CD_RESULT_SIZE];
    u_char status;
    if ( ( g_musicFlags & MUSIC_POLLING ) == 0 )
        return 0;
    oldTimer = g_musicTimer;
    g_musicTimer = oldTimer - 1;
    if ( oldTimer < 0 ) {
        CdControlB( CdlGetlocP, 0, result );
        status = result[0];
        if ( status < CdlStatStandby ) {
            g_musicTimer = MUSIC_IDLE_TICKS;
            g_musicFlags &= ~MUSIC_POLLING;
            return 0;
        }
        if ( status & CdlStatError ) {
            g_musicTimer = MUSIC_POLL_TICKS;
            Music_PlayTrack( g_musicResult );
            return -1;
        }
    }
    return 0;
}
