#include <sys/types.h>
#include <STDIO.H>
#include <LIBCD.H>
#include <LIBGTE.H>
#include <AUDIO.H>

/* g_music_flags bits. name: inferred from the CD routines below. */
enum {
    MUSIC_ENABLED = 0x01,   /* cleared by music_disable, set by music_enable; every request tests it */
    MUSIC_NEED_TOC = 0x02,  /* music_init reads the TOC once and clears it */
    MUSIC_POLLING = 0x04    /* a track was started; music_poll polls the drive status */
};

/* Ticks between status polls once a track has started. */
#define MUSIC_POLL_TICKS 0x190
/* Ticks before the next poll once the drive has stopped. name: invented */
#define MUSIC_IDLE_TICKS 0x3c
/* g_music_timer's initial value. name: invented */
#define MUSIC_START_TICKS 100

/* Local CD buffers: music_init's parameter frame (9-16 bytes of locals in retail's frame) and
 * the 8-byte CdlGetlocP result. names: invented */
#define CD_PARAM_BUFFER_SIZE 16
#define CD_RESULT_SIZE 8

unsigned short g_music_flags = MUSIC_NEED_TOC;
short g_music_result = -1;
short g_music_callback_state = 0;
short g_music_timer = MUSIC_START_TICKS;

extern int g_8d530;
/* Module statics (.sbss 0x8008d200..0x8008d210), in their retail order. */
static u_char g_cd_mode;
static u_char g_cd_requested_command;
static short g_cd_track_count;
extern CdlLOC g_cd_toc[];
/* CD track per level (36 halfwords, .data 0x80073174). */
/* name: analysis/runtime/globals.json (round 2) */
short g_level_music_tracks[LEVEL_AUDIO_TABLE_LENGTH] = {
    10, 18, 2, 24, 16, 2, 14, 2,
    12, 2, 20, 22, 10, 24, 18, 12,
    14, 24, 20, 14, 6, 22, 18, 8,
    4, 8, 16, 6, 8, 14, 4, 8,
    20, 8, 22, 0
};

void music_resume(void)
{
    u_char *mode = &g_cd_mode;
    g_8d530 = 1;
    *mode = CdlModeRept | CdlModeAP | CdlModeDA;
    CdControlB(CdlSetmode, mode, 0);
    g_cd_requested_command = CdlSeekP;
    CdControl(CdlSeekP, (u_char *)&g_cd_toc[g_music_result], 0);
}

void music_cd_complete(int status, u_char *result)
{
    if (status == CdlComplete) {
        if (g_cd_requested_command == CdlSeekP) {
            g_cd_requested_command = CdlPlay;
            CdControl(CdlPlay, 0, 0);
        }
    } else {
        printf("\n******\nCbComplete error: %s\n", CdIntstr(status));
    }
}

void music_cd_data_ready(int status, u_char *result)
{
    if (status != CdlDataReady)
        printf("cbdataready error:%s\n", CdIntstr(status));
}

/* name: inferred from its body, which sets up CD audio playback */
int music_init(void)
{
    unsigned short flags = g_music_flags;
    u_char param[CD_PARAM_BUFFER_SIZE];
    if ((flags & MUSIC_ENABLED) == 0 || (flags & MUSIC_NEED_TOC) == 0)
        return 0;
    music_set_volume(VOLUME_PERCENT_MAX);
    g_8d530 = 0;
    CdSetDebug(0);
    param[0] = CdlModeRept | CdlModeAP | CdlModeDA;
    CdControlB(CdlSetmode, param, 0);
    g_cd_track_count = CdGetToc(g_cd_toc);
    g_music_result = -1;
    g_music_callback_state = 0;
    g_music_timer = MUSIC_POLL_TICKS;
    g_music_flags &= ~MUSIC_NEED_TOC;
    return -1;
}

/* name: inferred from its body, which starts a CD audio track */
short music_play_track(int value)
{
    unsigned short flags = g_music_flags;
    short selected = (short)value;
    u_char mode;
    if ((flags & MUSIC_ENABLED) == 0)
        return 0;
    if (flags & MUSIC_NEED_TOC)
        music_init();
    if (selected == -1)
        return (short)music_stop();
    if (g_cd_track_count < selected)
        selected = g_cd_track_count;
    mode = CdlModeRept | CdlModeDA;
    CdControlB(CdlSetmode, &mode, 0);
    g_cd_requested_command = CdlPlay;
    CdControl(CdlPlay, (u_char *)&g_cd_toc[selected], 0);
    g_music_result = selected;
    g_music_callback_state = 0;
    g_music_timer = MUSIC_POLL_TICKS;
    g_music_flags |= MUSIC_POLLING;
    return -1;
}

int music_stop(void)
{
    unsigned short flags = g_music_flags;
    g_music_result = -1;
    g_music_callback_state = 0;
    if ((flags & MUSIC_ENABLED) == 0)
        return 0;
    if (flags & MUSIC_NEED_TOC)
        music_init();
    g_cd_requested_command = CdlStop;
    CdControl(CdlStop, 0, 0);
    g_music_timer = MUSIC_POLL_TICKS;
    g_music_flags = g_music_flags & ~MUSIC_POLLING;
    return -1;
}

/* name: inferred from its body, which pauses CD audio */
int music_pause(void)
{
    if ((g_music_flags & MUSIC_ENABLED) == 0)
        return 0;
    g_cd_requested_command = CdlPause;
    CdControl(CdlPause, 0, 0);
    return -1;
}

/* name: inferred from its body, which resumes CD audio */
int music_unpause(void)
{
    if ((g_music_flags & MUSIC_ENABLED) == 0)
        return 0;
    g_cd_requested_command = CdlPlay;
    CdControl(CdlPlay, 0, 0);
    return -1;
}

/* name: runtime evidence, analysis/runtime/functions.json */
int music_play_level(int menu_index)
{
    short index = (short)menu_index;
    if ((g_music_flags & MUSIC_ENABLED) == 0)
        return 0;
    if (g_music_flags & MUSIC_NEED_TOC)
        music_init();
    if ((unsigned short)menu_index >= LEVEL_AUDIO_TABLE_LENGTH)
        index = 0;
    music_play_track(g_level_music_tracks[index]);
    return -1;
}

/* name: inferred from its body, which clears the music enable flag */
void music_disable(void) { g_music_flags &= ~MUSIC_ENABLED; }

/* name: inferred from its body, which sets the music enable flag */
void music_enable(void) { g_music_flags |= MUSIC_ENABLED; }

/* name: inferred from its body, which sets the CD audio volume */
void music_set_volume(int volume)
{
    if (g_music_flags & MUSIC_ENABLED) spu_set_cd_volume(volume * (SPU_VOLUME_MAX / VOLUME_PERCENT_MAX), 0);
}

/* Poll the drive status after the signed timer expires. */
/* name: inferred from its body, which polls the CD audio state each frame */
int music_poll(void)
{
    short old_timer;
    u_char result[CD_RESULT_SIZE];
    u_char status;
    if ((g_music_flags & MUSIC_POLLING) == 0)
        return 0;
    old_timer = g_music_timer;
    g_music_timer = old_timer - 1;
    if (old_timer < 0) {
        CdControlB(CdlGetlocP, 0, result);
        status = result[0];
        if (status < CdlStatStandby) {
            g_music_timer = MUSIC_IDLE_TICKS;
            g_music_flags &= ~MUSIC_POLLING;
            return 0;
        }
        if (status & CdlStatError) {
            g_music_timer = MUSIC_POLL_TICKS;
            music_play_track(g_music_result);
            return -1;
        }
    }
    return 0;
}
