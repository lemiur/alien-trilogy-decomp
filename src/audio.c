/* audio.c -- sound effects: SPU setup, sample-bank upload and voice allocation */
#include <sys/types.h>
#include <LIBSPU.H>
#include <LIBGTE.H>
#include <AUDIO.H>

#define VOICE_COUNT 24
#define SAMPLE_COUNT 256
/* SpuInitMalloc's block count for g_spuMallocTable. name: invented */
#define SPU_MALLOC_BLOCKS 16
/* Entries of g_samplePriorityList, and the priority its first class gets. names: invented */
#define SAMPLE_PRIORITY_LIST_LENGTH 30
#define SAMPLE_PRIORITY_TOP 3

/* A playing sound is identified by a handle: its voice in the low byte, the
 * voice's play serial above it. A handle goes stale once the voice is reused. */
#define HANDLE_SERIAL_SHIFT 8
#define SOUND_HANDLE( voice, serial ) ( ( voice ) | ( ( serial ) << HANDLE_SERIAL_SHIFT ) )
#define HANDLE_VOICE_MASK 0xff
#define HANDLE_VOICE( handle ) ( ( handle ) & HANDLE_VOICE_MASK )
#define HANDLE_SERIAL( handle ) ( ( handle ) >> HANDLE_SERIAL_SHIFT )

/* voiceEntry_t.expire of a looping sound: it never expires. */
#define EXPIRE_NEVER 0xffff

/* Once g_soundClock reaches SOUND_CLOCK_REBASE, Snd_PlayVoice moves it and every live expiry
 * down by that much (16-bit wrap). name: invented */
#define SOUND_CLOCK_REBASE 0x8000

/* sampleSlot_t.flags. names: inferred from their effect on voice choice and expiry in Snd_PlayVoice */
#define SAMPLE_REUSE_VOICE 1    /* restart on the voice the sample last played on */
#define SAMPLE_FIXED_DURATION 2 /* duration counts clock ticks, not scaled by pitch */
#define SAMPLE_LOOP 4

/* Every voice attribute a key-on sets; SPU_VOICE_LSAX is added for loops. */
#define VOICE_ATTR_KEY_ON                                                                                              \
    ( SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH | SPU_VOICE_WDSA | SPU_VOICE_ADSR_AMODE | SPU_VOICE_ADSR_SMODE \
      | SPU_VOICE_ADSR_RMODE | SPU_VOICE_ADSR_AR | SPU_VOICE_ADSR_DR | SPU_VOICE_ADSR_SR | SPU_VOICE_ADSR_RR | SPU_VOICE_ADSR_SL )

/* g_sfxLevel settings (0 silent .. 9 and 10 full), named by the fraction of the volume they leave.
 * The unit fractions SFX_TENTH, SFX_FIFTH and SFX_THIRD are the divisors SCALE_BY_SFX_LEVEL uses.
 * names: invented */
enum {
    SFX_LEVEL_SILENT = 0,
    SFX_LEVEL_TENTH = 1,
    SFX_LEVEL_FIFTH = 2,
    SFX_LEVEL_THIRD = 3,
    SFX_LEVEL_TWO_FIFTHS = 4,
    SFX_LEVEL_QUARTER = 5,
    SFX_LEVEL_THREE_FIFTHS = 6,
    SFX_LEVEL_TWO_THIRDS = 7,
    SFX_LEVEL_FOUR_FIFTHS = 8,
    SFX_LEVEL_FULL = 9,
    SFX_LEVEL_FULL_MAX = 10
};
#define SFX_TENTH 10
#define SFX_FIFTH 5
#define SFX_THIRD 3

/* Scales a volume by the sound-effects level g_sfxLevel. */
#define SCALE_BY_SFX_LEVEL( volume )         \
    switch ( g_sfxLevel ) {                  \
    case SFX_LEVEL_SILENT:                   \
        volume = 0;                          \
        break;                               \
    case SFX_LEVEL_TENTH:                    \
        volume /= SFX_TENTH;                 \
        break;                               \
    case SFX_LEVEL_FIFTH:                    \
        volume /= SFX_FIFTH;                 \
        break;                               \
    case SFX_LEVEL_THIRD:                    \
        volume /= SFX_THIRD;                 \
        break;                               \
    case SFX_LEVEL_TWO_FIFTHS:               \
        volume = ( volume / SFX_FIFTH ) * 2; \
        break;                               \
    case SFX_LEVEL_QUARTER:                  \
        volume >>= 2;                        \
        break;                               \
    case SFX_LEVEL_THREE_FIFTHS:             \
        volume = ( volume / SFX_FIFTH ) * 3; \
        break;                               \
    case SFX_LEVEL_TWO_THIRDS:               \
        volume = ( volume / SFX_THIRD ) * 2; \
        break;                               \
    case SFX_LEVEL_FOUR_FIFTHS:              \
        volume = ( volume / SFX_FIFTH ) * 4; \
        break;                               \
    case SFX_LEVEL_FULL:                     \
    case SFX_LEVEL_FULL_MAX:                 \
        break;                               \
    }

extern voiceEntry_t g_voice[VOICE_COUNT];
/* name: inferred from sampleSlot_t */
extern sampleSlot_t g_sampleSlots[SAMPLE_COUNT];
/* SPU memory management table for SpuInitMalloc's 16 blocks.
 * name: inferred from that call */
extern char g_spuMallocTable[];

/* name: inferred from voiceEntry_t.serial, which takes its value per key-on */
short g_playSerial = 0;

/* Sample ids by priority class: SAMPLE_PRIORITY_TOP for the first run, one lower after each -1.
 * name: inferred from Snd_InitSamplePriorities, which assigns them */
short g_samplePriorityList[SAMPLE_PRIORITY_LIST_LENGTH] = {
    11, 24, 28, 96, 104, 102, 103, -1, 98, 99, 100, 101, -1, 5,  6,
    7,  8,  9,  10, 11,  12,  13,  14, 15, 16, 17,  18,  19, 20, -1,
};

/*
==================
Snd_TickClock
==================
*/
/* name: inferred from g_soundClock, the time base of the sound voices' expiry */
unsigned short Snd_TickClock( void )
{
    return ++g_soundClock;
}

/*
==================
Snd_ResetVoices
==================
*/
/* name: inferred from its body, which clears every voice's expiry, priority and serial and the sound clock */
void Snd_ResetVoices( void )
{
    voiceEntry_t *entry = g_voice;
    int           i;
    for ( i = 0; i < VOICE_COUNT; i++ ) {
        entry->expire = 0;
        entry->priority = 0;
        entry->serial = 0;
        entry++;
    }
    g_soundClock = 0;
}

/*
==================
Snd_PlayVoice
==================
*/
/* name: inferred from its body, which starts a sample on a chosen voice and returns the voice handle */
int Snd_PlayVoice( playRequest_t *request )
{
    sampleSlot_t  *slot = &g_sampleSlots[request->sample];
    voiceEntry_t  *entry;
    SpuVoiceAttr   attr;
    unsigned short lowest;
    int            voice;
    int            volume;
    int            balance;
    int            pitch;
    unsigned long  bit;
    int            offset;
    int            i;

    if ( g_soundClock >= SOUND_CLOCK_REBASE ) {
        entry = g_voice;
        g_soundClock += SOUND_CLOCK_REBASE;
        for ( i = 0; i < VOICE_COUNT; i++ ) {
            if ( entry->expire >= SOUND_CLOCK_REBASE ) {
                if ( entry->expire != EXPIRE_NEVER )
                    entry->expire += SOUND_CLOCK_REBASE;
            } else {
                entry->expire = 0;
            }
        }
    }
    i = 0;
    if ( ( slot->flags & SAMPLE_REUSE_VOICE ) && g_voice[slot->voice].sample == request->sample ) {
        voice = slot->voice;
    } else {
        lowest = slot->priority;
        voice = -1;
        entry = g_voice;
        for ( i = 0; i < VOICE_COUNT; i++, entry++ ) {
            if ( entry->expire <= g_soundClock )
                break;
            if ( entry->expire == EXPIRE_NEVER ) {
                long status = SpuGetKeyStatus( SPU_KEYCH( i ) );
                if ( status == SPU_ON_ENV_OFF || status == SPU_OFF )
                    break;
            }
            if ( entry->priority < lowest ) {
                lowest = entry->priority;
                voice = i;
            }
        }
        if ( i < VOICE_COUNT )
            voice = i;
    }
    if ( voice != -1 ) {
        slot->voice = voice;
        entry = &g_voice[voice];
        volume = request->volume;
        balance = request->balance;
        if ( volume == -1 )
            volume = slot->volume;
        SCALE_BY_SFX_LEVEL( volume )
        pitch = request->pitch;
        if ( pitch == -1 )
            pitch = slot->pitch;
        if ( slot->flags & SAMPLE_LOOP )
            entry->expire = EXPIRE_NEVER;
        else if ( slot->flags & SAMPLE_FIXED_DURATION )
            entry->expire = slot->duration + g_soundClock;
        else
            entry->expire = g_soundClock + ( slot->duration << SPU_PITCH_SHIFT ) / pitch;
        entry->priority = slot->priority;
        entry->sample = request->sample;
        entry->serial = g_playSerial;
        g_playSerial++;
        bit = SPU_KEYCH( voice );
        SpuSetKey( SPU_OFF, bit );
        attr.mask = VOICE_ATTR_KEY_ON;
        attr.voice = bit;
        offset = ( volume * balance ) >> SOUND_BALANCE_SHIFT;
        attr.volume.left = volume - offset;
        attr.volume.right = volume + offset;
        attr.addr = slot->addr;
        if ( slot->flags & SAMPLE_LOOP ) {
            attr.loop_addr = slot->addr;
            attr.mask = VOICE_ATTR_KEY_ON | SPU_VOICE_LSAX;
        }
        attr.pitch = pitch;
        attr.a_mode = slot->aMode;
        attr.s_mode = slot->sMode;
        attr.r_mode = slot->rMode;
        attr.ar = slot->ar;
        attr.dr = slot->dr;
        attr.sr = slot->sr;
        attr.rr = slot->rr;
        attr.sl = slot->sl;
        SpuSetKeyOnWithAttr( &attr );
        entry->volumeLeft = attr.volume.left;
        entry->volumeRight = attr.volume.right;
        if ( request->reverb )
            SpuSetReverbVoice( SPU_ON, bit );
        else
            SpuSetReverbVoice( SPU_OFF, bit );
        return SOUND_HANDLE( voice, entry->serial );
    }
    return -1;
}

/*
==================
Snd_SPUSetMasterVolume
==================
*/
/* name: inferred from its body, which sets the SPU master volume and balance */
void Snd_SPUSetMasterVolume( int volume, int balance )
{
    SpuCommonAttr attr;
    int           offset = ( volume * balance ) >> SOUND_BALANCE_SHIFT;
    attr.mask = SPU_COMMON_MVOLL | SPU_COMMON_MVOLR;
    attr.mvol.left = volume - offset;
    attr.mvol.right = volume + offset;
    SpuSetCommonAttr( &attr );
}

/*
==================
Snd_MuteVoices
==================
*/
/* name: inferred from its body, which sets every voice's volume to 0 */
void Snd_MuteVoices( void )
{
    SpuVoiceAttr attr;
    attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR;
    attr.voice = SPU_ALLCH;
    attr.volume.left = 0;
    attr.volume.right = 0;
    SpuSetVoiceAttr( &attr );
}

/*
==================
Snd_RestoreVoices
==================
*/
/* name: inferred from its body, which restores every voice's saved volume */
void Snd_RestoreVoices( int unused )
{
    SpuVoiceAttr attr;
    int          i;

    for ( i = 0; i < VOICE_COUNT; i++ ) {
        attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR;
        attr.volume.left = g_voice[i].volumeLeft;
        attr.volume.right = g_voice[i].volumeRight;
        attr.voice = SPU_KEYCH( i );
        SpuSetVoiceAttr( &attr );
    }
}

/*
==================
Snd_SPUSetCDVolume
==================
*/
/* name: inferred from its body, which sets the SPU CD input volume and balance */
void Snd_SPUSetCDVolume( int volume, int balance )
{
    SpuCommonAttr attr;
    int           offset = ( volume * balance ) >> SOUND_BALANCE_SHIFT;
    attr.mask = SPU_COMMON_CDVOLL | SPU_COMMON_CDVOLR;
    attr.cd.volume.left = volume - offset;
    attr.cd.volume.right = volume + offset;
    SpuSetCommonAttr( &attr );
}

/*
==================
Snd_VoiceSetVolume
==================
*/
/* name: inferred from its body, which sets a playing handle's volume and balance */
int Snd_VoiceSetVolume( int handle, int volume, int balance )
{
    int voice;

    SCALE_BY_SFX_LEVEL( volume )
    voice = HANDLE_VOICE( handle );
    if ( g_voice[voice].serial == HANDLE_SERIAL( handle ) ) {
        SpuVoiceAttr attr;
        int          offset = ( volume * balance ) >> SOUND_BALANCE_SHIFT;
        attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR;
        attr.voice = SPU_KEYCH( voice );
        attr.volume.left = volume - offset;
        attr.volume.right = volume + offset;
        SpuSetVoiceAttr( &attr );
        g_voice[voice].volumeLeft = attr.volume.left;
        g_voice[voice].volumeRight = attr.volume.right;
        return 0;
    }
    return -1;
}

/*
==================
Snd_VoiceSetPitch
==================
*/
/* name: inferred from its SPU_VOICE_PITCH attribute */
int Snd_VoiceSetPitch( int handle, int pitch )
{
    int voice = HANDLE_VOICE( handle );

    if ( g_voice[voice].serial == HANDLE_SERIAL( handle ) ) {
        SpuVoiceAttr attr;
        attr.mask = SPU_VOICE_PITCH;
        attr.pitch = pitch;
        attr.voice = SPU_KEYCH( voice );
        SpuSetVoiceAttr( &attr );
    } else {
        return -1;
    }
    return 0;
}

/*
==================
Snd_VoiceSetNote
==================
*/
/* name: inferred from its SPU_VOICE_NOTE attribute */
int Snd_VoiceSetNote( int handle, int note )
{
    int voice = HANDLE_VOICE( handle );

    if ( g_voice[voice].serial == HANDLE_SERIAL( handle ) ) {
        SpuVoiceAttr attr;
        attr.mask = SPU_VOICE_NOTE;
        attr.note = note;
        attr.voice = SPU_KEYCH( voice );
        SpuSetVoiceAttr( &attr );
    } else {
        return -1;
    }
    return 0;
}

/*
==================
Snd_StopVoice
==================
*/
/* name: inferred from its body, which keys off a playing handle's voice */
int Snd_StopVoice( int handle )
{
    int voice = HANDLE_VOICE( handle );
    if ( g_voice[voice].serial == HANDLE_SERIAL( handle ) ) {
        SpuSetKey( SPU_OFF, SPU_KEYCH( voice ) );
        g_voice[voice].expire = g_soundClock;
        return 0;
    }
    return -1;
}

/*
==================
Snd_VoiceOfHandle

The voice of a live handle, -1 for a stale one.
==================
*/
/* name: inferred from that */
int Snd_VoiceOfHandle( int handle )
{
    int voice = HANDLE_VOICE( handle );
    if ( g_voice[voice].serial == HANDLE_SERIAL( handle ) ) {
        return voice;
    }
    return -1;
}

/*
==================
Snd_VoiceReverbOn
==================
*/
/* name: inferred from SpuSetReverbVoice(SPU_ON) for its voice */
int Snd_VoiceReverbOn( int handle )
{
    int voice = HANDLE_VOICE( handle );
    if ( g_voice[voice].serial == HANDLE_SERIAL( handle ) ) {
        SpuSetReverbVoice( SPU_ON, SPU_KEYCH( voice ) );
        return 0;
    }
    return -1;
}

/*
==================
Snd_VoiceReverbOff
==================
*/
/* name: inferred from SpuSetReverbVoice(SPU_OFF) for its voice */
int Snd_VoiceReverbOff( int handle )
{
    int voice = HANDLE_VOICE( handle );
    if ( g_voice[voice].serial == HANDLE_SERIAL( handle ) ) {
        SpuSetReverbVoice( SPU_OFF, SPU_KEYCH( voice ) );
        return 0;
    }
    return -1;
}

/*
==================
f_43e98

A second copy of Snd_VoiceOfHandle; nothing distinguishes the two.
==================
*/
/* identity-TODO: no jal, pointer word, lui/lo pair or $gp operand in retail reaches it. */
int f_43e98( int handle )
{
    int voice = HANDLE_VOICE( handle );
    if ( g_voice[voice].serial == HANDLE_SERIAL( handle ) ) {
        return voice;
    }
    return -1;
}

/*
==================
Snd_LoadSampleBank
==================
*/
/* name: inferred from its body, which uploads a sample bank to SPU memory and fills the sample slots */
unsigned long Snd_LoadSampleBank( int first, sampleBank_t *data )
{
    long            count = data->count;
    sampleSource_t *source = data->sources;
    unsigned char  *samples = (unsigned char *)&source[count];
    unsigned long   addr;
    int             i;

    addr = SpuMalloc( data->size );
    if ( addr != -1 ) {
        SpuSetTransferStartAddr( addr );
        SpuWrite( samples, data->size );
        for ( i = 0; i < count; source++, first++, i++ ) {
            g_sampleSlots[first].addr = addr + source->offset;
            g_sampleSlots[first].priority = source->priority;
            g_sampleSlots[first].duration = source->duration;
            g_sampleSlots[first].volume = source->volume;
            g_sampleSlots[first].pitch = source->pitch;
            g_sampleSlots[first].flags = source->flags;
            g_sampleSlots[first].voice = 0;
            g_sampleSlots[first].ar = source->ar;
            g_sampleSlots[first].dr = source->dr;
            g_sampleSlots[first].sr = source->sr;
            g_sampleSlots[first].sl = source->sl;
            g_sampleSlots[first].rr = source->rr;
            if ( source->aMode == 0 )
                g_sampleSlots[first].aMode = SPU_VOICE_LINEARIncN;
            else
                g_sampleSlots[first].aMode = source->aMode;
            if ( source->sMode == 0 )
                g_sampleSlots[first].sMode = SPU_VOICE_LINEARIncN;
            else
                g_sampleSlots[first].sMode = source->sMode;
            if ( source->rMode == 0 )
                g_sampleSlots[first].rMode = SPU_VOICE_LINEARDecN;
            else
                g_sampleSlots[first].rMode = source->rMode;
        }
        SpuIsTransferCompleted( 1 );
    }
    return addr;
}

/*
==================
Snd_FreeSampleBank
==================
*/
/* name: inferred from its body, which frees a bank's SPU memory */
void Snd_FreeSampleBank( unsigned long address )
{
    SpuFree( address );
}

/*
==================
Snd_InitSPU
==================
*/
/* name: inferred from its body, which initialises the SPU, its reverb and every voice */
void Snd_InitSPU( void )
{
    SpuVoiceAttr  voice;
    SpuCommonAttr common;
    SpuReverbAttr reverb;
    int           volume = SPU_VOLUME_MAX;
    SpuInit();
    SpuStart();
    SpuInitMalloc( SPU_MALLOC_BLOCKS, g_spuMallocTable );
    SpuSetTransferMode( 0 );
    common.mask = SPU_COMMON_MVOLL | SPU_COMMON_MVOLR | SPU_COMMON_CDVOLL | SPU_COMMON_CDVOLR | SPU_COMMON_CDREV | SPU_COMMON_CDMIX;
    common.mvol.left = volume;
    common.mvol.right = volume;
    common.cd.volume.left = volume;
    common.cd.volume.right = volume;
    common.cd.reverb = SPU_OFF;
    common.cd.mix = SPU_ON;
    SpuSetCommonAttr( &common );
    SpuReserveReverbWorkArea( SPU_ON );
    reverb.mask = SPU_REV_MODE | SPU_REV_DEPTHL | SPU_REV_DEPTHR | SPU_REV_DELAYTIME | SPU_REV_FEEDBACK;
    reverb.mode = SPU_REV_MODE_OFF;
    SpuSetReverbModeParam( &reverb );
    SpuSetReverb( 1 );
    voice.mask = VOICE_ATTR_KEY_ON;
    voice.voice = SPU_ALLCH;
    voice.volume.left = volume;
    voice.volume.right = volume;
    voice.pitch = SPU_PITCH_NORMAL;
    voice.a_mode = SPU_VOICE_LINEARIncN;
    voice.s_mode = SPU_VOICE_LINEARIncN;
    voice.r_mode = SPU_VOICE_LINEARDecN;
    voice.ar = 0;
    voice.dr = 0;
    voice.sr = 0;
    voice.rr = 0;
    voice.sl = SPU_SUSTAIN_LEVEL_MAX;
    SpuSetVoiceAttr( &voice );
}

/*
==================
Snd_StopAllVoices
==================
*/
/* name: inferred from its body, which keys off every voice and clears the voice table */
void Snd_StopAllVoices( void )
{
    short         voiceIndex = 0;
    short         sampleIndex = 0;
    voiceEntry_t *voice;

    g_soundClock = 0;
    voice = g_voice;
    for ( voiceIndex = 0; voiceIndex < VOICE_COUNT; ++voiceIndex ) {
        for ( sampleIndex = 0; sampleIndex < VOICE_COUNT; ++sampleIndex ) {
            Snd_StopVoice( ( voice->serial << HANDLE_SERIAL_SHIFT ) | sampleIndex );
        }
        voice->expire = 0;
        voice->priority = 0;
        voice->serial = 0;
        ++voice;
    }
}

/*
==================
Snd_InitSamplePriorities
==================
*/
/* name: inferred from its body, which sets each sample's priority from the priority list */
void Snd_InitSamplePriorities( void )
{
    short i;
    short j;
    int   value;
    short slot;

    i = 0;
    do {
        g_sampleSlots[i].priority = 0;
        ++i;
    } while ( i < SAMPLE_COUNT );

    value = SAMPLE_PRIORITY_TOP;
    j = 0;
    do {
        slot = g_samplePriorityList[j];
        if ( slot == -1 )
            --value;
        else
            g_sampleSlots[slot].priority = value;
        ++j;
    } while ( (unsigned short)j < SAMPLE_PRIORITY_LIST_LENGTH );
}
