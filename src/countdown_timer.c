/* countdown_timer.c -- the countdown timer task (Timer_Start, Timer_Task) */
#include <sys/types.h>
#include <LIBGTE.H>
#include <MODEL.H>
#include <AUDIO.H>
#include <GAME.H>
#include <CAMERA.H>
#include <TEXT.H>
#include <VIEW.H>
#include <TYPES.H>
#include <TASK.H>
#include <MENU.H>
/* Timer_Task's state[]: frames per second, seconds left, frames left in the second, frames run */
enum {
    COUNTDOWN_PERIOD,
    COUNTDOWN_SECONDS,
    COUNTDOWN_TICKS,
    COUNTDOWN_FRAMES
};

/* Countdown length in seconds, by level (Timer_Start). names: invented */
#define COUNTDOWN_SECONDS_SHORT 30
#define COUNTDOWN_SECONDS_MEDIUM 45
#define COUNTDOWN_SECONDS_DEFAULT 60

/* Seconds left at which Timer_Task plays a cue (the sound depends on the level). names: invented */
enum {
    COUNTDOWN_CUE_TICK_4 = 2,
    COUNTDOWN_CUE_TICK_3 = 5,
    COUNTDOWN_CUE_TICK_2 = 8,
    COUNTDOWN_CUE_TICK_1 = 11,
    COUNTDOWN_CUE_ALERT = 14,
    COUNTDOWN_CUE_WARNING_AGAIN = 17,
    COUNTDOWN_CUE_WARNING = 20
};

/* The time is drawn green down to COUNTDOWN_URGENT seconds, then red, blinking with frame bit
 * COUNTDOWN_BLINK, at COUNTDOWN_TEXT_X, COUNTDOWN_TEXT_Y. names: invented */
#define COUNTDOWN_URGENT 18
#define COUNTDOWN_BLINK 8
#define COUNTDOWN_TEXT_X 16
#define COUNTDOWN_TEXT_Y 19
#define COUNTDOWN_TEXT_SIZE 3 /* two digits and the terminator */


/*
==================
Timer_Task
==================
*/
void Timer_Task( TASK *task )
{
    short changed;
    u32   seconds;
    u8    color[RGB_CHANNELS];
    char  text[COUNTDOWN_TEXT_SIZE];

    task->state[COUNTDOWN_TICKS]--;
    task->state[COUNTDOWN_FRAMES]++;
    changed = 0;
    if ( task->state[COUNTDOWN_TICKS] == 0 ) {
        task->state[COUNTDOWN_TICKS] = task->state[COUNTDOWN_PERIOD];
        if ( task->state[COUNTDOWN_SECONDS] != 0 ) {
            task->state[COUNTDOWN_SECONDS]--;
            changed = -1;
        }
    }
    seconds = task->state[COUNTDOWN_SECONDS];
    text[0] = seconds / DECIMAL_RADIX + '0';
    text[1] = seconds % DECIMAL_RADIX | '0';
    text[2] = 0;
    if ( changed ) {
        switch ( task->state[COUNTDOWN_SECONDS] ) {
        case COUNTDOWN_CUE_WARNING:
        case COUNTDOWN_CUE_WARNING_AGAIN:
            switch ( g_level ) {
            case LEVEL_115_AMMUNITION_DUMP_3:
            case LEVEL_155_SECURITY_CATWALKS:
            case LEVEL_242_TUNNELS_AND_DUCTS_1:
            case LEVEL_243_TUNNELS_AND_DUCTS_2:
                Snd_Play( SOUND_COUNTDOWN_WARNING, -1, SPU_VOLUME_MAX );
                break;
            case LEVEL_113_AMMUNITION_DUMP:
            case LEVEL_114_AMMUNITION_DUMP_2:
                Snd_Play( SOUND_COUNTDOWN_WARNING_ALT, -1, SPU_VOLUME_MAX );
                break;
            case LEVEL_352_INORGANICS_1:
            case LEVEL_353_INORGANICS_2:
                Snd_Play( SOUND_COUNTDOWN_TICK, -1, SPU_VOLUME_MAX );
                break;
            }
            break;
        case COUNTDOWN_CUE_ALERT:
            if ( g_level < LEVEL_311_TUNNELS )
                Snd_Play( SOUND_COUNTDOWN_ALERT, -1, SPU_VOLUME_MAX );
            else
                Snd_Play( SOUND_COUNTDOWN_TICK, -1, SPU_VOLUME_MAX );
            break;
        case COUNTDOWN_CUE_TICK_1:
        case COUNTDOWN_CUE_TICK_2:
        case COUNTDOWN_CUE_TICK_3:
        case COUNTDOWN_CUE_TICK_4:
            if ( g_level >= LEVEL_311_TUNNELS )
                Snd_Play( SOUND_COUNTDOWN_TICK, -1, SPU_VOLUME_MAX );
            break;
        }
    }
    if ( task->state[COUNTDOWN_SECONDS] >= COUNTDOWN_URGENT ) {
        color[0] = 0;
        color[1] = g_palette->colors[0].g;
        color[2] = 0;
    } else {
        color[0] = g_palette->colors[0].r;
        color[1] = ( task->state[COUNTDOWN_FRAMES] & COUNTDOWN_BLINK ) ? g_palette->colors[0].g : 0;
        color[2] = 0;
        if ( task->state[COUNTDOWN_SECONDS] == 0 )
            g_gameFlags |= GAME_FLAG_LEVEL_COMPLETE;
    }
    Text_DrawText( text, COUNTDOWN_TEXT_X, COUNTDOWN_TEXT_Y, color );
    g_nextTask = task->Next;
}

/*
==================
Timer_Start
==================
*/
/* name: inferred from its body, which starts the level countdown task */
void Timer_Start( short mode )
{
    TASK *task;
    if ( Menu_ModeInTable( mode ) == 0 )
        return;
    if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_INDESTRUCTIBLE ) )
        return;
    task = TaskCreate( TASK_PRIORITY_COUNTDOWN, Timer_Task );
    if ( task == 0 )
        return;
    switch ( mode ) {
    case LEVEL_155_SECURITY_CATWALKS:
        task->state[COUNTDOWN_SECONDS] = COUNTDOWN_SECONDS_SHORT;
        break;
    case LEVEL_352_INORGANICS_1:
    case LEVEL_353_INORGANICS_2:
        task->state[COUNTDOWN_SECONDS] = COUNTDOWN_SECONDS_MEDIUM;
        break;
    default:
        task->state[COUNTDOWN_SECONDS] = COUNTDOWN_SECONDS_DEFAULT;
        break;
    }
    task->state[COUNTDOWN_PERIOD] = TICKS_PER_SECOND;
    task->state[COUNTDOWN_TICKS] = TICKS_PER_SECOND;
    task->state[COUNTDOWN_FRAMES] = 0;
}
