#include <sys/types.h>
#include <LIBCD.H>
#include <HEAPFILE.H>
#include <HEAP.H>
#include <LIBGTE.H>
#include <AUDIO.H>
#include <ABS.H>
#include <CAMERA.H>
#include <VIEW.H>
typedef signed short s16;

/* g_audio_flags bits. name: inferred from audio_enable/audio_disable and audio_init. */
enum {
    AUDIO_ENABLED = 0x01,   /* set by audio_enable, cleared by audio_disable; every request tests it */
    AUDIO_NEED_INIT = 0x02  /* audio_init sets up the SPU once and clears it */
};

unsigned short g_audio_flags = AUDIO_NEED_INIT;

/* sound_play_at clamps the x-z distance (|major| + |minor|/2) to these before taking it off the
 * volume: SOUND_RANGE_NEAR for SOUND_AT_NEAR, SOUND_RANGE_FAR otherwise. names: invented */
#define SOUND_RANGE_NEAR 0x1400
#define SOUND_RANGE_FAR  0x1e00
/* sound_play_at's SOUND_AT_NEAR volume before the distance is taken off. name: invented */
#define SOUND_NEAR_VOLUME (SPU_VOLUME_MAX * 3 / 4)

/* Sample bank slots (g_audio_banks). name: invented */
#define AUDIO_BANK_COUNT 8

/* g_sound_balance steps: the full turn in 32 steps of 1 << 7 angle units. names: invented */
#define SOUND_BALANCE_STEPS 32
#define SOUND_BALANCE_STEP_SHIFT 7


typedef struct AudioResourceSlot {
    short key;
    unsigned short reserved;
    unsigned long spu_address;
} AudioResourceSlot;

typedef struct AudioMix {
    int unknown_00; /* identity-TODO: the whole record is write-only in retail (only the stores here, 0x8003d3a0..0x8003d3c8; no other reference to its address, so no library or DMA use either); no runtime read in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime, round 5) */
    int volume;
    int unknown_08; /* identity-TODO: as unknown_00 */
    int scale;
    int unknown_10; /* identity-TODO: as unknown_00 */
} AudioMix;

extern AudioResourceSlot g_audio_banks[];
/* name: inferred from use: sound_play fills it for each request */
extern PlayRequest g_sound_request;
extern AudioMix g_audio_mix;
/* Stereo balance by heading relative to the view, 32 steps of a triangle wave. name: invented */
static short g_sound_balance[SOUND_BALANCE_STEPS] = {
    0, 2048, 4096, 6144, 8192, 10240, 12288, 14336,
    16384, 14336, 12288, 10240, 8192, 6144, 4096, 2048,
    0, -2048, -4096, -6144, -8192, -10240, -12288, -14336,
    -16384, -14336, -12288, -10240, -8192, -6144, -4096, -2048
};
/* Sample bank key (its g_file_table index) per level (36 halfwords, .data 0x8007312c). */
/* name: analysis/runtime/globals.json (round 2) */
short g_level_sample_banks[LEVEL_AUDIO_TABLE_LENGTH] = {
    FILE_111SFX_BIN, FILE_112SFX_BIN, FILE_113SFX_BIN, FILE_122SFX_BIN,
    FILE_131SFX_BIN, FILE_114SFX_BIN, FILE_141SFX_BIN, FILE_115SFX_BIN,
    FILE_154SFX_BIN, FILE_155SFX_BIN, FILE_161SFX_BIN, FILE_162SFX_BIN,
    FILE_211SFX_BIN, FILE_212SFX_BIN, FILE_213SFX_BIN, FILE_222SFX_BIN,
    FILE_242SFX_BIN, FILE_231SFX_BIN, FILE_232SFX_BIN, FILE_243SFX_BIN,
    FILE_262SFX_BIN, FILE_263SFX_BIN, FILE_311SFX_BIN, FILE_321SFX_BIN,
    FILE_331SFX_BIN, FILE_321SFX_BIN, FILE_351SFX_BIN, FILE_352SFX_BIN,
    FILE_321SFX_BIN, FILE_371SFX_BIN, FILE_353SFX_BIN, FILE_321SFX_BIN,
    FILE_381SFX_BIN, FILE_321SFX_BIN, FILE_391SFX_BIN,
    0
};
/* The callee reads signed position halfwords at offsets zero and four. */

short audio_find_bank(short key)
{
    short i;
    for (i = 0; i < AUDIO_BANK_COUNT; ++i) {
        if (key == g_audio_banks[i].key)
            return i;
    }
    return -1;
}

/* name: inferred from its body, which loads a sample-bank file into a free bank slot */
int audio_load_bank(short index, short argument)
{
    short old_index;
    short new_index;
    HeapFile *record;

    if ((g_audio_flags & AUDIO_ENABLED) == 0)
        return 0;
    old_index = audio_find_bank(index);
    if (old_index != -1)
        return -1;
    new_index = audio_find_bank(-1);
    if (new_index == -1)
        return 0;

    record = &g_file_table[index];
    record->loaded_size = 0;
    record->buffer = 0;
    load_file(record);
    g_audio_banks[new_index].key = index;
    g_audio_banks[new_index].spu_address = sample_bank_load(argument, record->buffer);
    heap_free(record->loaded_size);
    sample_priorities_init();
    return -1;
}

/* Remove matching SPU allocations, or all slots except the reserved key. */
/* name: inferred from its body, which frees one bank slot, or all but the resident bank (FILE_PERMSFX_BIN) */
short audio_free_bank(short key)
{
    short found = -1;
    short i;

    if ((g_audio_flags & AUDIO_ENABLED) == 0)
        return 0;
    if (key == -1) {
        for (i = 0; i < AUDIO_BANK_COUNT; ++i) {
            if (g_audio_banks[i].key != -1 && g_audio_banks[i].key != FILE_PERMSFX_BIN) {
                sample_bank_free(g_audio_banks[i].spu_address);
                g_audio_banks[i].key = -1;
                g_audio_banks[i].spu_address = (unsigned long)-1;
            }
        }
        return -1;
    }

    for (i = 0; i < AUDIO_BANK_COUNT; ++i) {
        if (key == g_audio_banks[i].key)
            found = i;
    }
    if (found == -1)
        return 0;
    sample_bank_free(g_audio_banks[found].spu_address);
    g_audio_banks[found].key = -1;
    g_audio_banks[found].spu_address = (unsigned long)-1;
    return -1;
}

/* name: inferred from its body, which loads the level's sample bank after freeing the others */
short audio_select_level_bank(short index)
{
    short key;
    if ((unsigned short)index >= LEVEL_AUDIO_TABLE_LENGTH)
        return 0;
    key = g_level_sample_banks[index];
    if (audio_find_bank(key) != -1)
        return -1;
    audio_free_bank(-1);
    return audio_load_bank(key, SAMPLE_LEVEL_FIRST);
}

/* name: inferred from its body, which sets up the SPU and the resident bank once, else frees level banks */
int audio_init(void)
{
    unsigned short flags = g_audio_flags;
    int i;
    if ((flags & AUDIO_ENABLED) == 0)
        return 0;
    if (flags & AUDIO_NEED_INIT) {
        for (i = 0; i < AUDIO_BANK_COUNT; i++) {
            g_audio_banks[i].key = -1;
            g_audio_banks[i].spu_address = (unsigned long)-1;
        }
        spu_init();
        voices_reset();
        spu_set_master_volume(SPU_VOLUME_MAX, 0);
        sound_set_volume(VOLUME_PERCENT_MAX);
        g_audio_mix.unknown_00 = 0;
        g_audio_mix.volume = SPU_VOLUME_MAX / 2;
        g_audio_mix.unknown_08 = 0;
        g_audio_mix.scale = ONE;
        g_audio_mix.unknown_10 = 0;
        g_audio_flags &= ~AUDIO_NEED_INIT;
        audio_load_bank(FILE_PERMSFX_BIN, 0);
        return -1;
    }
    for (i = 0; i < AUDIO_BANK_COUNT; i++) {
        short key = g_audio_banks[i].key;
        if (key != -1 && key != FILE_PERMSFX_BIN)
            audio_free_bank(key);
    }
    return -1;
}

/* name: inferred from its body, which sets AUDIO_ENABLED */
void audio_enable(void) { g_audio_flags |= AUDIO_ENABLED; }

/* name: inferred from its body, which clears AUDIO_ENABLED */
int audio_disable(void) { return (int)(g_audio_flags &= ~AUDIO_ENABLED); }

/* name: inferred from its body, which stops every voice when audio is enabled */
void sound_stop_all(void) { if (g_audio_flags & AUDIO_ENABLED) voices_stop_all(); }

/* name: inferred from its body, which passes a PlayRequest to voice_play when audio is enabled */
int sound_play_request(PlayRequest *request)
{
    if (g_audio_flags & AUDIO_ENABLED) return voice_play(request);
    return -1;
}

/* name: inferred from its body, which mutes every voice when audio is enabled */
void sound_mute_all(void) { if (g_audio_flags & AUDIO_ENABLED) voices_mute(); }

/* name: inferred from its body, which restores every voice's volume when audio is enabled */
void sound_unmute_all(void) { if (g_audio_flags & AUDIO_ENABLED) voices_restore(0x400); } /* identity-TODO: voices_restore ignores its argument */

/* name: inferred from its body, which stops a handle through voice_stop when audio is enabled */
int sound_stop(int handle)
{
    if (g_audio_flags & AUDIO_ENABLED) {
        if (handle != -1) return voice_stop(handle);
        return -1;
    }
    return -1;
}

/* name: inferred from its body, which changes a playing handle's volume through voice_set_volume when audio is enabled */
int sound_adjust(int handle, int volume, int balance)
{
    if (g_audio_flags & AUDIO_ENABLED) {
        if (handle != -1) return voice_set_volume(handle, volume, balance);
        return -1;
    }
    return -1;
}

/* name: inferred from its body, which fills the shared PlayRequest and calls voice_play when audio is enabled */
int sound_play(short sample, int pitch, int volume)
{
    if ((g_audio_flags & AUDIO_ENABLED) == 0)
        return -1;
    g_sound_request.pitch = pitch;
    g_sound_request.volume = volume / 2;
    g_sound_request.sample = sample;
    g_sound_request.balance = 0;
    g_sound_request.reverb = 0;
    return voice_play(&g_sound_request);
}

/* name: inferred from its body, which stores the effects level (0..100) / 10 in g_sfx_level */
void sound_set_volume(int value) { value /= SFX_LEVEL_STEP; g_sfx_level = value; }

/* name: inferred from its body, which plays a sample at a position, its volume falling
 * with the distance from the player and its balance from the heading */
int sound_play_at(short sample, const SVECTOR *position, short mode)
{
    PlayRequest request;
    u_int dx, dz, distance;
    int heading;

    if ((g_audio_flags & AUDIO_ENABLED) == 0)
        return -1;
    dx = abs(position->vx - g_player.position.vx);
    dz = abs(position->vz - g_player.position.vz);
    if (dz < dx)
        distance = (dz >> 1) + dx;
    else
        distance = (dx >> 1) + dz;
    switch (mode) {
    case SOUND_AT_SOFT:
    case SOUND_AT_SOFT_ALT:
        if (distance > SOUND_RANGE_FAR)
            distance = SOUND_RANGE_FAR;
        request.volume = (SPU_VOLUME_MAX / 2 - distance) >> 1;
        break;
    case SOUND_AT_NEAR:
        if (distance > SOUND_RANGE_NEAR)
            distance = SOUND_RANGE_NEAR;
        request.volume = SOUND_NEAR_VOLUME - distance;
        break;
    case SOUND_AT_FULL:
        if (distance > SOUND_RANGE_FAR)
            distance = SOUND_RANGE_FAR;
        request.volume = SPU_VOLUME_MAX - distance;
        break;
    }
    heading = ANGLE_360 + ANGLE_180 - point_heading_xz(position, &g_player.position);
    request.pitch = -1;
    request.sample = sample;
    request.balance = g_sound_balance[((heading + g_player.angles.vy) >> SOUND_BALANCE_STEP_SHIFT) & (SOUND_BALANCE_STEPS - 1)];
    request.reverb = 0;
    return voice_play(&request);
}

/* name: inferred from its body, which plays a sample at a position through sound_play_at's SOUND_AT_SOFT_ALT */
int sound_play_at_position(int sample, const SVECTOR *position)
{
    return sound_play_at((short)sample, position, SOUND_AT_SOFT_ALT);
}

/* This positional wrapper shares the GP-resident audio flags above. */
/* name: inferred from its body, which plays a sample at map position x, z */
int sound_play_at_xz(int sample, short x, short z)
{
    SVECTOR position;
    if (!(g_audio_flags & AUDIO_ENABLED))
        return -1;
    position.vx = x;
    position.vy = 0;
    position.vz = z;
    return sound_play_at_position((short)sample, &position);
}
