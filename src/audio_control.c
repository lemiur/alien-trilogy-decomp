/* audio_control.c -- the game's sound API: sample banks and the play, stop and volume calls */
#include <sys/types.h>
#include <LIBCD.H>
#include <HEAPFILE.H>
#include <HEAP.H>
#include <LIBGTE.H>
#include <AUDIO.H>
#include <ABS.H>
#include <CAMERA.H>
#include <VIEW.H>
#include <TYPES.H>

/* g_audioFlags bits. name: inferred from Snd_Enable/Snd_Disable and Snd_Init. */
enum {
    AUDIO_ENABLED = 0x01,  /* set by Snd_Enable, cleared by Snd_Disable; every request tests it */
    AUDIO_NEED_INIT = 0x02 /* Snd_Init sets up the SPU once and clears it */
};

unsigned short g_audioFlags = AUDIO_NEED_INIT;

/* Snd_PlayAt clamps the x-z distance (|major| + |minor|/2) to these before taking it off the
 * volume: SOUND_RANGE_NEAR for SOUND_AT_NEAR, SOUND_RANGE_FAR otherwise. names: invented */
#define SOUND_RANGE_NEAR 0x1400
#define SOUND_RANGE_FAR 0x1e00
/* The volume Snd_RestoreVoices is passed; it ignores it. name: invented */
#define SOUND_UNMUTE_VOLUME 0x400

/* Sample bank slots (g_audioBanks). name: invented */
#define AUDIO_BANK_COUNT 8

/* g_soundBalance steps: the full turn in 32 steps of 1 << 7 angle units. names: invented */
#define SOUND_BALANCE_STEPS 32
#define SOUND_BALANCE_STEP_SHIFT 7


extern audioResourceSlot_t g_audioBanks[];
/* name: inferred from use: Snd_Play fills it for each request */
extern playRequest_t g_soundRequest;
extern audioMix_t    g_audioMix;
/* Stereo balance by heading relative to the view, 32 steps of a triangle wave. name: invented */
static short g_soundBalance[SOUND_BALANCE_STEPS] = { 0,      2048,   4096,   6144,   8192,  10240,  12288,  14336,
                                                     16384,  14336,  12288,  10240,  8192,  6144,   4096,   2048,
                                                     0,      -2048,  -4096,  -6144,  -8192, -10240, -12288, -14336,
                                                     -16384, -14336, -12288, -10240, -8192, -6144,  -4096,  -2048 };
/* Sample bank key (its g_fileTable index) per level (36 halfwords, .data 0x8007312c). */
/* name: analysis/runtime/globals.json */
short g_levelSampleBanks[LEVEL_AUDIO_TABLE_LENGTH] = {
    FILE_111SFX_BIN, FILE_112SFX_BIN, FILE_113SFX_BIN, FILE_122SFX_BIN, FILE_131SFX_BIN, FILE_114SFX_BIN,
    FILE_141SFX_BIN, FILE_115SFX_BIN, FILE_154SFX_BIN, FILE_155SFX_BIN, FILE_161SFX_BIN, FILE_162SFX_BIN,
    FILE_211SFX_BIN, FILE_212SFX_BIN, FILE_213SFX_BIN, FILE_222SFX_BIN, FILE_242SFX_BIN, FILE_231SFX_BIN,
    FILE_232SFX_BIN, FILE_243SFX_BIN, FILE_262SFX_BIN, FILE_263SFX_BIN, FILE_311SFX_BIN, FILE_321SFX_BIN,
    FILE_331SFX_BIN, FILE_321SFX_BIN, FILE_351SFX_BIN, FILE_352SFX_BIN, FILE_321SFX_BIN, FILE_371SFX_BIN,
    FILE_353SFX_BIN, FILE_321SFX_BIN, FILE_381SFX_BIN, FILE_321SFX_BIN, FILE_391SFX_BIN, 0
};
/* The callee reads signed position halfwords at offsets zero and four. */

/*
==================
Snd_FindBank
==================
*/
short Snd_FindBank( short key )
{
    short i;
    for ( i = 0; i < AUDIO_BANK_COUNT; ++i ) {
        if ( key == g_audioBanks[i].key )
            return i;
    }
    return -1;
}

/*
==================
Snd_LoadBank
==================
*/
/* name: inferred from its body, which loads a sample-bank file into a free bank slot */
int Snd_LoadBank( short index, short argument )
{
    short       oldIndex;
    short       newIndex;
    heapFile_t *record;

    if ( ( g_audioFlags & AUDIO_ENABLED ) == 0 )
        return 0;
    oldIndex = Snd_FindBank( index );
    if ( oldIndex != -1 )
        return -1;
    newIndex = Snd_FindBank( -1 );
    if ( newIndex == -1 )
        return 0;

    record = &g_fileTable[index];
    record->loadedSize = 0;
    record->buffer = 0;
    Sys_LoadFile( record );
    g_audioBanks[newIndex].key = index;
    g_audioBanks[newIndex].spuAddress = Snd_LoadSampleBank( argument, record->buffer );
    Heap_Free( record->loadedSize );
    Snd_InitSamplePriorities();
    return -1;
}

/*
==================
Snd_FreeBank

Remove matching SPU allocations, or all slots except the reserved key.
==================
*/
/* name: inferred from its body, which frees one bank slot, or all but the resident bank (FILE_PERMSFX_BIN) */
short Snd_FreeBank( short key )
{
    short found = -1;
    short i;

    if ( ( g_audioFlags & AUDIO_ENABLED ) == 0 )
        return 0;
    if ( key == -1 ) {
        for ( i = 0; i < AUDIO_BANK_COUNT; ++i ) {
            if ( g_audioBanks[i].key != -1 && g_audioBanks[i].key != FILE_PERMSFX_BIN ) {
                Snd_FreeSampleBank( g_audioBanks[i].spuAddress );
                g_audioBanks[i].key = -1;
                g_audioBanks[i].spuAddress = (unsigned long)-1;
            }
        }
        return -1;
    }

    for ( i = 0; i < AUDIO_BANK_COUNT; ++i ) {
        if ( key == g_audioBanks[i].key )
            found = i;
    }
    if ( found == -1 )
        return 0;
    Snd_FreeSampleBank( g_audioBanks[found].spuAddress );
    g_audioBanks[found].key = -1;
    g_audioBanks[found].spuAddress = (unsigned long)-1;
    return -1;
}

/*
==================
Snd_SelectLevelBank
==================
*/
/* name: inferred from its body, which loads the level's sample bank after freeing the others */
short Snd_SelectLevelBank( short index )
{
    short key;
    if ( (unsigned short)index >= LEVEL_AUDIO_TABLE_LENGTH )
        return 0;
    key = g_levelSampleBanks[index];
    if ( Snd_FindBank( key ) != -1 )
        return -1;
    Snd_FreeBank( -1 );
    return Snd_LoadBank( key, SAMPLE_LEVEL_FIRST );
}

/*
==================
Snd_Init
==================
*/
/* name: inferred from its body, which sets up the SPU and the resident bank once, else frees level banks */
int Snd_Init( void )
{
    unsigned short flags = g_audioFlags;
    int            i;
    if ( ( flags & AUDIO_ENABLED ) == 0 )
        return 0;
    if ( flags & AUDIO_NEED_INIT ) {
        for ( i = 0; i < AUDIO_BANK_COUNT; i++ ) {
            g_audioBanks[i].key = -1;
            g_audioBanks[i].spuAddress = (unsigned long)-1;
        }
        Snd_InitSPU();
        Snd_ResetVoices();
        Snd_SPUSetMasterVolume( SPU_VOLUME_MAX, 0 );
        Snd_SetVolume( VOLUME_PERCENT_MAX );
        g_audioMix.unknown_00 = 0;
        g_audioMix.volume = SPU_VOLUME_MAX / 2;
        g_audioMix.unknown_08 = 0;
        g_audioMix.scale = ONE;
        g_audioMix.unknown_10 = 0;
        g_audioFlags &= ~AUDIO_NEED_INIT;
        Snd_LoadBank( FILE_PERMSFX_BIN, 0 );
        return -1;
    }
    for ( i = 0; i < AUDIO_BANK_COUNT; i++ ) {
        short key = g_audioBanks[i].key;
        if ( key != -1 && key != FILE_PERMSFX_BIN )
            Snd_FreeBank( key );
    }
    return -1;
}

/*
==================
Snd_Enable
==================
*/
/* name: inferred from its body, which sets AUDIO_ENABLED */
void Snd_Enable( void )
{
    g_audioFlags |= AUDIO_ENABLED;
}

/*
==================
Snd_Disable
==================
*/
/* name: inferred from its body, which clears AUDIO_ENABLED */
int Snd_Disable( void )
{
    return (int)( g_audioFlags &= ~AUDIO_ENABLED );
}

/*
==================
Snd_StopAll
==================
*/
/* name: inferred from its body, which stops every voice when audio is enabled */
void Snd_StopAll( void )
{
    if ( g_audioFlags & AUDIO_ENABLED )
        Snd_StopAllVoices();
}

/*
==================
Snd_PlayRequest
==================
*/
/* name: inferred from its body, which passes a playRequest_t to Snd_PlayVoice when audio is enabled */
int Snd_PlayRequest( playRequest_t *request )
{
    if ( g_audioFlags & AUDIO_ENABLED )
        return Snd_PlayVoice( request );
    return -1;
}

/*
==================
Snd_MuteAll
==================
*/
/* name: inferred from its body, which mutes every voice when audio is enabled */
void Snd_MuteAll( void )
{
    if ( g_audioFlags & AUDIO_ENABLED )
        Snd_MuteVoices();
}

/*
==================
Snd_UnmuteAll
==================
*/
/* name: inferred from its body, which restores every voice's volume when audio is enabled */
void Snd_UnmuteAll( void )
{
    if ( g_audioFlags & AUDIO_ENABLED )
        Snd_RestoreVoices( SOUND_UNMUTE_VOLUME );
} /* identity-TODO: Snd_RestoreVoices ignores its argument */

/*
==================
Snd_Stop
==================
*/
/* name: inferred from its body, which stops a handle through Snd_StopVoice when audio is enabled */
int Snd_Stop( int handle )
{
    if ( g_audioFlags & AUDIO_ENABLED ) {
        if ( handle != -1 )
            return Snd_StopVoice( handle );
        return -1;
    }
    return -1;
}

/*
==================
Snd_Adjust
==================
*/
/* name: inferred from its body, which changes a playing handle's volume through Snd_VoiceSetVolume when audio is enabled */
int Snd_Adjust( int handle, int volume, int balance )
{
    if ( g_audioFlags & AUDIO_ENABLED ) {
        if ( handle != -1 )
            return Snd_VoiceSetVolume( handle, volume, balance );
        return -1;
    }
    return -1;
}

/*
==================
Snd_Play
==================
*/
/* name: inferred from its body, which fills the shared playRequest_t and calls Snd_PlayVoice when audio is enabled */
int Snd_Play( short sample, int pitch, int volume )
{
    if ( ( g_audioFlags & AUDIO_ENABLED ) == 0 )
        return -1;
    g_soundRequest.pitch = pitch;
    g_soundRequest.volume = volume / 2;
    g_soundRequest.sample = sample;
    g_soundRequest.balance = 0;
    g_soundRequest.reverb = 0;
    return Snd_PlayVoice( &g_soundRequest );
}

/*
==================
Snd_SetVolume
==================
*/
/* name: inferred from its body, which stores the effects level (0..100) / 10 in g_sfxLevel */
void Snd_SetVolume( int value )
{
    value /= SFX_LEVEL_STEP;
    g_sfxLevel = value;
}

/*
==================
Snd_PlayAt
==================
*/
/* name: inferred from its body, which plays a sample at a position, its volume falling
 * with the distance from the player and its balance from the heading */
int Snd_PlayAt( short sample, const SVECTOR *position, short mode )
{
    playRequest_t request;
    u_int         dx, dz, distance;
    int           heading;

    if ( ( g_audioFlags & AUDIO_ENABLED ) == 0 )
        return -1;
    dx = abs( position->vx - g_player.position.vx );
    dz = abs( position->vz - g_player.position.vz );
    if ( dz < dx )
        distance = ( dz >> 1 ) + dx;
    else
        distance = ( dx >> 1 ) + dz;
    switch ( mode ) {
    case SOUND_AT_SOFT:
    case SOUND_AT_SOFT_ALT:
        if ( distance > SOUND_RANGE_FAR )
            distance = SOUND_RANGE_FAR;
        request.volume = ( SPU_VOLUME_MAX / 2 - distance ) >> 1;
        break;
    case SOUND_AT_NEAR:
        if ( distance > SOUND_RANGE_NEAR )
            distance = SOUND_RANGE_NEAR;
        request.volume = SOUND_NEAR_VOLUME - distance;
        break;
    case SOUND_AT_FULL:
        if ( distance > SOUND_RANGE_FAR )
            distance = SOUND_RANGE_FAR;
        request.volume = SPU_VOLUME_MAX - distance;
        break;
    }
    heading = ANGLE_360 + ANGLE_180 - Geo_PointHeadingXZ( position, &g_player.position );
    request.pitch = -1;
    request.sample = sample;
    request.balance =
        g_soundBalance[( ( heading + g_player.angles.vy ) >> SOUND_BALANCE_STEP_SHIFT ) & ( SOUND_BALANCE_STEPS - 1 )];
    request.reverb = 0;
    return Snd_PlayVoice( &request );
}

/*
==================
Snd_PlayAtPosition
==================
*/
/* name: inferred from its body, which plays a sample at a position through Snd_PlayAt's SOUND_AT_SOFT_ALT */
int Snd_PlayAtPosition( int sample, const SVECTOR *position )
{
    return Snd_PlayAt( (short)sample, position, SOUND_AT_SOFT_ALT );
}

/*
==================
Snd_PlayAtXZ

This positional wrapper shares the GP-resident audio flags above.
==================
*/
/* name: inferred from its body, which plays a sample at map position x, z */
int Snd_PlayAtXZ( int sample, short x, short z )
{
    SVECTOR position;
    if ( !( g_audioFlags & AUDIO_ENABLED ) )
        return -1;
    position.vx = x;
    position.vy = 0;
    position.vz = z;
    return Snd_PlayAtPosition( (short)sample, &position );
}
