/*
 * Sound, in two layers, and CD music.
 *  - src/audio.c: the SPU voice layer. A playing sound is a handle (voice in the low
 *    byte, the voice's play serial above it); voices expire by g_sound_clock.
 *  - src/audio_control.c: the game's sound API. Every call is a no-op unless
 *    audio_enable has run; samples come from sample banks loaded per level.
 *  - src/music_control.c: CD-DA music.
 * Requires <sys/types.h> and <LIBGTE.H>. Names without evidence are marked invented.
 */
#ifndef AUDIO_H
#define AUDIO_H

/* SPU values. names: invented (LIBSPU has no names for them) */
#define SPU_VOLUME_MAX        0x3fff /* largest voice, master and CD volume */
#define SPU_PITCH_NORMAL      0x1000 /* pitch 1.0: the sample's own rate */
#define SPU_SUSTAIN_LEVEL_MAX 0xf    /* largest ADSR sustain level */
#define SPU_PITCH_SHIFT       12     /* log2 of SPU_PITCH_NORMAL: pitch is 4.12 fixed point */

/* PlayRequest.balance is a signed 2.14 fraction: each side takes volume * balance >> 14
 * from one ear and adds it to the other. name: invented */
#define SOUND_BALANCE_SHIFT 14

/* The menus' volume settings run 0..VOLUME_PERCENT_MAX (sound_set_volume, music_set_volume);
 * g_sfx_level is the setting / SFX_LEVEL_STEP. names: invented */
#define VOLUME_PERCENT_MAX 100
#define SFX_LEVEL_STEP 10

/* Entries of the per-level tables g_level_sample_banks and g_level_music_tracks: one per
 * level (LEVEL_COUNT) and a closing 0. name: invented */
#define LEVEL_AUDIO_TABLE_LENGTH 36

/* sound_play_at modes: how the volume falls with the x-z distance. names: invented, from the
 * volume each case computes */
enum SoundAtMode {
    SOUND_AT_SOFT,      /* (SPU_VOLUME_MAX / 2 - distance) / 2, distance clamped to the far range */
    SOUND_AT_NEAR,      /* SPU_VOLUME_MAX * 3 / 4 - distance, distance clamped to the near range */
    SOUND_AT_SOFT_ALT,  /* as SOUND_AT_SOFT; only sound_play_at_position passes it */
    SOUND_AT_FULL       /* SPU_VOLUME_MAX - distance, distance clamped to the far range */
};

/* Sample numbers: the resident bank (\SFX\PERMSFX.BIN) holds samples 0..40, the level
 * bank (audio_select_level_bank) starts at SAMPLE_LEVEL_FIRST. name: invented */
#define SAMPLE_LEVEL_FIRST 41

/* Sample ids. names: invented, from the code that plays them; identity-TODO: the samples themselves */
#define SOUND_PLAYER_HURT 3      /* the player loses health (player_damage_health, player_damage) */
#define SOUND_PULSE_RIFLE_PICKUP 5
#define SOUND_GRENADES_PICKUP 7
#define SOUND_PULSE_GRENADE 8    /* pulse_grenade_launch */
#define SOUND_GRENADE_THROW 9    /* player_throw_grenade */
#define SOUND_FLAMETHROWER_PICKUP 0xa
#define SOUND_FLAMETHROWER 0xb   /* the flamethrower's firing sound, kept on channel 0 (weapon_view_update) */
#define SOUND_PISTOL_PICKUP 0xc
#define SOUND_SHOTGUN_PUMP 0xf    /* the shotgun pump (runtime evidence); also the shotgun pickup */
#define SOUND_SMART_GUN_PICKUP 0x12
#define SOUND_TRACKER_PING 0x15    /* the motion tracker pings with no contact (hud.c tracker) */
#define SOUND_TRACKER_CONTACT 0x16 /* the motion tracker pings with contacts, louder for more blips */
#define SOUND_POWER_DOWN 0x19    /* the shoulder lamp or night vision runs out (runtime evidence) */
#define SOUND_MAP_SCROLL 0x1a    /* the pause menu's automap scrolls (pause_menu_run) */
#define SOUND_FLAME_JET 0x1c     /* a flame jet sprays (jet_update) */
#define SOUND_BIG_EXPLOSION 0x21 /* big_explosion_spawn */
#define SOUND_SHOT_IMPACT_ALT 0x23 /* a shot hits a wall on levels 12..21 (particles_task) */
#define SOUND_SHOT_IMPACT 0x24   /* a shot hits a wall; the player's armour takes a death type 3 hit (player_damage) */
#define SOUND_SHOT_SPLASH 0x25   /* a shot hits a wall over a cell of type 8 or 9 */
#define SOUND_TALLY_TICK 0x27    /* the mission assessment tally ticks: every 8 frames while the results bars count up */
#define SOUND_PICKUP 0x28        /* every other pickup (pickups_task) */
#define SOUND_EXPLOSION 0x29     /* explosion_spawn */
#define SOUND_DOOR_MOVE 0x2a     /* a door starts opening or closing */
#define SOUND_DOOR_STOP 0x2b     /* a door finishes opening or closing */
#define SOUND_SWITCH 0x5c        /* a switch panel is activated */
#define SOUND_LIFT_MOVE 0x5e     /* a lift starts moving */
#define SOUND_LIFT_STOP 0x5f     /* a lift reaches its end */
/* countdown_task's announcements at set seconds, chosen by level */
#define SOUND_COUNTDOWN_TICK 0x62         /* runtime: a klaxon repeated every 3 s (levels 27, 30) */
#define SOUND_COUNTDOWN_ALERT 0x63        /* runtime: at 14 s on levels below 22; probably spoken */
#define SOUND_COUNTDOWN_WARNING 0x64      /* runtime: at 20 s and 17 s (levels 7, 9, 16, 19); probably spoken */
#define SOUND_COUNTDOWN_WARNING_ALT 0x65  /* runtime: as SOUND_COUNTDOWN_WARNING on levels 2, 5 */
#define SOUND_STEAM_JET 0x68     /* a steam jet sprays (jet_update) */
#define SOUND_WALL_SHATTER 0x6c  /* a wall block shatters (block_object_task) */
#define SOUND_PANEL_LAND 0x6d    /* a wall debris panel hits the floor (particles_task) */
#define SOUND_COLONIST 0x6e      /* a cocooned colonist reacts to the player (colonist_task) */

/* One request to voice_play. */
typedef struct PlayRequest {
    int sample;
    int volume;
    int balance;
    int pitch;
    int reverb;
} PlayRequest;

/* One 14-word sample header of a bank file. Field names inferred from the
 * SampleSlot members sample_bank_load copies them to. */
typedef struct SampleSource {
    long priority;
    long duration;
    long offset;
    long flags;
    long volume;
    long pitch;
    long ar;
    long a_mode;
    long dr;
    long sr;
    long s_mode;
    long sl;
    long rr;
    long r_mode;
} SampleSource;

typedef struct SampleBank {
    long count;
    long size;
    SampleSource sources[1];
} SampleBank;

/* SPU voice layer (src/audio.c) */
/* name: inferred from use: vblank_handler and sound_clock_tick advance it; voice expiry times are taken from it */
extern volatile unsigned short g_sound_clock;
/* name: inferred from use: SCALE_BY_SFX_LEVEL scales voice volumes by it; sound_set_volume stores the menu value / 10 */
extern int g_sfx_level;
extern unsigned short sound_clock_tick(void);
extern void voices_reset(void);
extern int voice_play(PlayRequest *request);
extern void spu_set_master_volume(int volume, int balance);
extern void voices_mute(void);
extern void voices_restore(int unused);
extern void spu_set_cd_volume(int volume, int balance);
extern int voice_set_volume(int handle, int volume, int balance);
extern int voice_set_pitch(int handle, int pitch);
extern int voice_set_note(int handle, int note);
extern int voice_stop(int handle);
extern int voice_of_handle(int handle);
extern int voice_reverb_on(int handle);
extern int voice_reverb_off(int handle);
extern unsigned long sample_bank_load(int first, SampleBank *data);
extern void sample_bank_free(unsigned long address);
extern void spu_init(void);
extern void voices_stop_all(void);
extern void sample_priorities_init(void);

/* Game sound API (src/audio_control.c) */
extern int audio_load_bank(short index, short argument);
extern short audio_free_bank(short key);
extern short audio_select_level_bank(short index);
extern int audio_init(void);
extern void audio_enable(void);
extern int audio_disable(void);
extern void sound_stop_all(void);
extern int sound_play_request(PlayRequest *request);
extern void sound_mute_all(void);
extern void sound_unmute_all(void);
extern int sound_stop(int handle);
extern int sound_adjust(int handle, int volume, int balance);
extern int sound_play(short sample, int pitch, int volume);
extern void sound_set_volume(int value);
/* identity-TODO: positional play at 0x8003d66c, not matched yet */
extern int sound_play_at(short sample, const SVECTOR *position, short mode);
extern int sound_play_at_position(int sample, const SVECTOR *position);
extern int sound_play_at_xz(int sample, short x, short z);
/* src/frontend.c: plays a menu sample through its own PlayRequest */
extern void play_menu_sound(int sample);

/* CD music (src/music_control.c) */
extern void music_resume(void);
extern int music_init(void);
extern short music_play_track(int track);
extern int music_stop(void);
extern int music_pause(void);
extern int music_unpause(void);
extern void music_disable(void);
extern void music_enable(void);
extern void music_set_volume(int volume);
extern int music_poll(void);

/* src/music_control.c */
extern int music_play_level(int menu_index);

#endif
