/*
 * Sound, in two layers, and CD music.
 *  - src/audio.c: the SPU voice layer. A playing sound is a handle (voice in the low
 *    byte, the voice's play serial above it); voices expire by g_soundClock.
 *  - src/audio_control.c: the game's sound API. Every call is a no-op unless
 *    Snd_Enable has run; samples come from sample banks loaded per level.
 *  - src/music_control.c: CD-DA music.
 * Requires <sys/types.h> and <LIBGTE.H>. Names without evidence are marked invented.
 */
#ifndef AUDIO_H
#define AUDIO_H

/* SPU values. names: invented (LIBSPU has no names for them) */
#define SPU_VOLUME_MAX 0x3fff /* largest voice, master and CD volume */
#define SOUND_NEAR_VOLUME \
    ( SPU_VOLUME_MAX * 3 / 4 ) /* volume of a SOUND_AT_NEAR sound before the distance is taken off, and of the weapon sounds. name: invented */
#define SPU_PITCH_NORMAL 0x1000   /* pitch 1.0: the sample's own rate */
#define SPU_SUSTAIN_LEVEL_MAX 0xf /* largest ADSR sustain level */
#define SPU_PITCH_SHIFT 12        /* log2 of SPU_PITCH_NORMAL: pitch is 4.12 fixed point */

/* playRequest_t.balance is a signed 2.14 fraction: each side takes volume * balance >> 14
 * from one ear and adds it to the other. name: invented */
#define SOUND_BALANCE_SHIFT 14

/* The menus' volume settings run 0..VOLUME_PERCENT_MAX (Snd_SetVolume, Music_SetVolume);
 * g_sfxLevel is the setting / SFX_LEVEL_STEP. names: invented */
#define VOLUME_PERCENT_MAX 100
#define SFX_LEVEL_STEP 10

/* Entries of the per-level tables g_levelSampleBanks and g_levelMusicTracks: one per
 * level (LEVEL_COUNT) and a closing 0. name: invented */
#define LEVEL_AUDIO_TABLE_LENGTH 36

/* Snd_PlayAt modes: how the volume falls with the x-z distance. names: invented, from the
 * volume each case computes */
enum soundAtMode_t {
    SOUND_AT_SOFT,     /* (SPU_VOLUME_MAX / 2 - distance) / 2, distance clamped to the far range */
    SOUND_AT_NEAR,     /* SPU_VOLUME_MAX * 3 / 4 - distance, distance clamped to the near range */
    SOUND_AT_SOFT_ALT, /* as SOUND_AT_SOFT; only Snd_PlayAtPosition passes it */
    SOUND_AT_FULL      /* SPU_VOLUME_MAX - distance, distance clamped to the far range */
};

/* Sample numbers: the resident bank (\SFX\PERMSFX.BIN) holds samples 0..40, the level
 * bank (Snd_SelectLevelBank) starts at SAMPLE_LEVEL_FIRST. name: invented */
#define SAMPLE_LEVEL_FIRST 41

/* Sample ids. names: invented, from the code that plays them; identity-TODO: the samples themselves */
#define SOUND_PLAYER_HURT 3 /* the player loses health (Player_DamageHealth, Player_Damage) */
#define SOUND_PULSE_RIFLE_PICKUP 5
#define SOUND_GRENADES_PICKUP 7
#define SOUND_PULSE_GRENADE 8 /* Particle_LaunchPulseGrenade */
#define SOUND_GRENADE_THROW 9 /* Player_ThrowGrenade */
#define SOUND_FLAMETHROWER_PICKUP 0xa
#define SOUND_FLAMETHROWER 0xb /* the flamethrower's firing sound, kept on channel 0 (Player_UpdateWeaponView) */
#define SOUND_PISTOL_PICKUP 0xc
#define SOUND_SHOTGUN_PUMP 0xf /* the shotgun pump (runtime evidence); also the shotgun pickup */
#define SOUND_SMART_GUN_PICKUP 0x12
#define SOUND_TRACKER_PING 0x15    /* the motion tracker pings with no contact (hud.c tracker) */
#define SOUND_TRACKER_CONTACT 0x16 /* the motion tracker pings with contacts, louder for more blips */
#define SOUND_POWER_DOWN 0x19      /* the shoulder lamp or night vision runs out (runtime evidence) */
#define SOUND_MAP_SCROLL 0x1a      /* the pause menu's automap scrolls (Pause_Run) */
#define SOUND_FLAME_JET 0x1c       /* a flame jet sprays (Actor_UpdateJet) */
#define SOUND_BIG_EXPLOSION 0x21   /* Particle_SpawnBigExplosion */
#define SOUND_SHOT_IMPACT_ALT 0x23 /* a shot hits a wall on levels 12..21 (Particle_Task) */
#define SOUND_SHOT_IMPACT 0x24 /* a shot hits a wall; the player's armour takes a death type 3 hit (Player_Damage) */
#define SOUND_SHOT_SPLASH 0x25 /* a shot hits a wall over a cell of type 8 or 9 */
#define SOUND_TALLY_TICK 0x27  /* the mission assessment tally ticks: every 8 frames while the results bars count up */
#define SOUND_PICKUP 0x28      /* every other pickup (Object_PickupsTask) */
#define SOUND_EXPLOSION 0x29   /* Particle_SpawnExplosion */
#define SOUND_DOOR_MOVE 0x2a   /* a door starts opening or closing */
#define SOUND_DOOR_STOP 0x2b   /* a door finishes opening or closing */
#define SOUND_SWITCH 0x5c      /* a switch panel is activated */
#define SOUND_LIFT_MOVE 0x5e   /* a lift starts moving */
#define SOUND_LIFT_STOP 0x5f   /* a lift reaches its end */
/* Timer_Task's announcements at set seconds, chosen by level */
#define SOUND_COUNTDOWN_TICK 0x62        /* runtime: a klaxon repeated every 3 s (levels 27, 30) */
#define SOUND_COUNTDOWN_ALERT 0x63       /* runtime: at 14 s on levels below 22; probably spoken */
#define SOUND_COUNTDOWN_WARNING 0x64     /* runtime: at 20 s and 17 s (levels 7, 9, 16, 19); probably spoken */
#define SOUND_COUNTDOWN_WARNING_ALT 0x65 /* runtime: as SOUND_COUNTDOWN_WARNING on levels 2, 5 */
#define SOUND_STEAM_JET 0x68             /* a steam jet sprays (Actor_UpdateJet) */
#define SOUND_WALL_SHATTER 0x6c          /* a wall block shatters (Object_BlockObjectTask) */
#define SOUND_PANEL_LAND 0x6d            /* a wall debris panel hits the floor (Particle_Task) */
#define SOUND_COLONIST 0x6e              /* a cocooned colonist reacts to the player (Actor_ColonistTask) */

/* One request to Snd_PlayVoice. */
typedef struct playRequest_s {
    int sample;
    int volume;
    int balance;
    int pitch;
    int reverb;
} playRequest_t;

/* One 14-word sample header of a bank file. Field names inferred from the
 * sampleSlot_t members Snd_LoadSampleBank copies them to. */
typedef struct sampleSource_s {
    long priority;
    long duration;
    long offset;
    long flags;
    long volume;
    long pitch;
    long ar;
    long aMode;
    long dr;
    long sr;
    long sMode;
    long sl;
    long rr;
    long rMode;
} sampleSource_t;

typedef struct sampleBank_s {
    long           count;
    long           size;
    sampleSource_t sources[1];
} sampleBank_t;

/* SPU voice layer (src/audio.c) */
/* name: inferred from use: Sys_VBlankHandler and Snd_TickClock advance it; voice expiry times are taken from it */
extern volatile unsigned short g_soundClock;
/* name: inferred from use: SCALE_BY_SFX_LEVEL scales voice volumes by it; Snd_SetVolume stores the menu value / 10 */
extern int g_sfxLevel;
extern unsigned short Snd_TickClock( void );
extern void Snd_ResetVoices( void );
extern int Snd_PlayVoice( playRequest_t *request );
extern void Snd_SPUSetMasterVolume( int volume, int balance );
extern void Snd_MuteVoices( void );
extern void Snd_RestoreVoices( int unused );
extern void Snd_SPUSetCDVolume( int volume, int balance );
extern int Snd_VoiceSetVolume( int handle, int volume, int balance );
extern int Snd_VoiceSetPitch( int handle, int pitch );
extern int Snd_VoiceSetNote( int handle, int note );
extern int Snd_StopVoice( int handle );
extern int Snd_VoiceOfHandle( int handle );
extern int Snd_VoiceReverbOn( int handle );
extern int Snd_VoiceReverbOff( int handle );
extern unsigned long Snd_LoadSampleBank( int first, sampleBank_t *data );
extern void Snd_FreeSampleBank( unsigned long address );
extern void Snd_InitSPU( void );
extern void Snd_StopAllVoices( void );
extern void Snd_InitSamplePriorities( void );

/* Game sound API (src/audio_control.c) */
extern int Snd_LoadBank( short index, short argument );
extern short Snd_FreeBank( short key );
extern short Snd_SelectLevelBank( short index );
extern int Snd_Init( void );
extern void Snd_Enable( void );
extern int Snd_Disable( void );
extern void Snd_StopAll( void );
extern int Snd_PlayRequest( playRequest_t *request );
extern void Snd_MuteAll( void );
extern void Snd_UnmuteAll( void );
extern int Snd_Stop( int handle );
extern int Snd_Adjust( int handle, int volume, int balance );
extern int Snd_Play( short sample, int pitch, int volume );
extern void Snd_SetVolume( int value );
/* identity-TODO: positional play at 0x8003d66c */
extern int Snd_PlayAt( short sample, const SVECTOR *position, short mode );
extern int Snd_PlayAtPosition( int sample, const SVECTOR *position );
extern int Snd_PlayAtXZ( int sample, short x, short z );
/* src/frontend.c: plays a menu sample through its own playRequest_t */
extern void Menu_PlayMenuSound( int sample );

/* CD music (src/music_control.c) */
extern void Music_Resume( void );
extern int Music_Init( void );
extern short Music_PlayTrack( int track );
extern int Music_Stop( void );
extern int Music_Pause( void );
extern int Music_Unpause( void );
extern void Music_Disable( void );
extern void Music_Enable( void );
extern void Music_SetVolume( int volume );
extern int Music_Poll( void );

/* src/music_control.c */
extern int Music_PlayLevel( int menuIndex );

/* One SPU voice's allocation record (g_voice, src/audio.c): when it expires, its priority and sample, its play serial. */
typedef struct voiceEntry_s {
    unsigned short expire;
    unsigned short priority;
    unsigned short sample;
    unsigned short serial;
    short          volumeLeft;
    short          volumeRight;
} voiceEntry_t;

/* Per-sample playback attributes, filled from the sample banks by Snd_LoadSampleBank. */
typedef struct sampleSlot_s {
    unsigned long  addr;
    unsigned short priority;
    unsigned short duration;
    unsigned short volume;
    unsigned short pitch;
    unsigned short flags;
    unsigned char  voice;
    unsigned char  reserved;
    unsigned short aMode;
    unsigned short sMode;
    unsigned short rMode;
    unsigned short ar;
    unsigned short dr;
    unsigned short sr;
    unsigned short rr;
    unsigned short sl;
} sampleSlot_t;

/* One sample bank slot (g_audioBanks, src/audio_control.c): the bank's file key and its SPU address. names: invented */
typedef struct audioResourceSlot_s {
    short          key;
    unsigned short reserved;
    unsigned long  spuAddress;
} audioResourceSlot_t;

/* The mix record Snd_Play fills (g_audioMix, src/audio_control.c). */
typedef struct audioMix_s {
    int unknown_00; /* identity-TODO: the whole record is write-only in retail (only the stores here, 0x8003d3a0..0x8003d3c8; no other reference to its address, so no library or DMA use either); no runtime read in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime) */
    int volume;
    int unknown_08; /* identity-TODO: as unknown_00 */
    int scale;
    int unknown_10; /* identity-TODO: as unknown_00 */
} audioMix_t;

#endif
