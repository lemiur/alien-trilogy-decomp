/* command_sequencer.c -- level script sequencer: each state steps through the level's command list,
 * holding each value for a delay set by control commands */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <TASK.H>
#include <COMMAND.H>
#include <GAME.H>
#include <TYPES.H>

/* Command.opcode: with COMMAND_CONTROL set, the low bits are a control code
 * (below) that runs at once; otherwise the command's value is shown. */
#define COMMAND_CONTROL 0x8000
#define COMMAND_CODE_MASK 0x7fff

/* Control codes. names: inferred from what Command_Run does for each */
enum {
    COMMAND_RESTART = 1, /* back to the state's first command */
    COMMAND_DELAY_1,     /* hold each value for 1, 2, 3, 4 or 16 ticks */
    COMMAND_DELAY_2,
    COMMAND_DELAY_3,
    COMMAND_DELAY_4,
    COMMAND_DELAY_16,
    COMMAND_RANDOM_DELAY_4, /* hold for a random 1..4, 1..8, 1..16 or 1..32 ticks */
    COMMAND_RANDOM_DELAY_8,
    COMMAND_RANDOM_DELAY_16,
    COMMAND_RANDOM_DELAY_32,
    COMMAND_STOP
};

/* Ticks the fixed delays hold a value, and the masks of the random delays (1 + Sys_Random() & mask
 * ticks). names: invented, after the control codes */
#define COMMAND_DELAY_3_TICKS 3
#define COMMAND_DELAY_4_TICKS 4
#define COMMAND_DELAY_16_TICKS 16
#define COMMAND_RANDOM_DELAY_4_MASK 3
#define COMMAND_RANDOM_DELAY_8_MASK 7
#define COMMAND_RANDOM_DELAY_16_MASK 15
#define COMMAND_RANDOM_DELAY_32_MASK 31


/* First command of each label (a run of equal Command.value).
 * name: inferred from Command_InitStates, which fills it */
extern command_t *g_commandLabels[];
/* name: inferred from it holding g_levelMap.stateCount */
extern u16 g_commandStateCount;

/*
==================
Command_Run

Runs the control commands at the state's position; -1 once it stops.
==================
*/
/* name: inferred from that */
int Command_Run( commandState_t *state )
{
    command_t *command = state->commandPointer;

    while ( command->opcode & COMMAND_CONTROL ) {
        switch ( command->opcode & COMMAND_CODE_MASK ) {
        case COMMAND_RESTART:
            command = state->commandPointer = state->commandStart;
            break;
        case COMMAND_DELAY_1:
            state->delay = state->reload = 1;
            state->commandPointer = ++command;
            break;
        case COMMAND_DELAY_2:
            state->delay = state->reload = 2;
            state->commandPointer = ++command;
            break;
        case COMMAND_DELAY_3:
            state->delay = state->reload = COMMAND_DELAY_3_TICKS;
            state->commandPointer = ++command;
            break;
        case COMMAND_DELAY_4:
            state->delay = state->reload = COMMAND_DELAY_4_TICKS;
            state->commandPointer = ++command;
            break;
        case COMMAND_DELAY_16:
            state->delay = state->reload = COMMAND_DELAY_16_TICKS;
            state->commandPointer = ++command;
            break;
        case COMMAND_RANDOM_DELAY_4:
            state->delay = state->reload = ( Sys_Random() & COMMAND_RANDOM_DELAY_4_MASK ) + 1;
            state->commandPointer = ++command;
            break;
        case COMMAND_RANDOM_DELAY_8:
            state->delay = state->reload = ( Sys_Random() & COMMAND_RANDOM_DELAY_8_MASK ) + 1;
            state->commandPointer = ++command;
            break;
        case COMMAND_RANDOM_DELAY_16:
            state->delay = state->reload = ( Sys_Random() & COMMAND_RANDOM_DELAY_16_MASK ) + 1;
            state->commandPointer = ++command;
            break;
        case COMMAND_RANDOM_DELAY_32:
            state->delay = state->reload = ( Sys_Random() & COMMAND_RANDOM_DELAY_32_MASK ) + 1;
            state->commandPointer = ++command;
            break;
        case COMMAND_STOP:
            state->flags |= STATE_STOPPED;
            return -1;
        }
    }
    return 0;
}

/*
==================
Command_Advance

Counts down the state's delay, then moves to its next command.
==================
*/
/* name: inferred from that */
void Command_Advance( commandState_t *state )
{
    if ( --state->delay == 0 ) {
        state->delay = state->reload;
        state->commandPointer++;
        if ( Command_Run( state ) == 0 )
            state->value = state->commandPointer->opcode;
    }
}

/*
==================
Command_InitStates
==================
*/
/* name: inferred from its body, which finds each state's start label in the level script */
void Command_InitStates( commandStart_t *start )
{
    command_t      *command = g_levelMap.commands;
    u16             previous = COMMAND_NO_LABEL;
    short           labels = 0;
    short           i;
    commandState_t *state;

    for ( i = 0; i < g_levelMap.commandCount; i++, command++ ) {
        if ( command->value != previous ) {
            g_commandLabels[labels++] = command;
            previous = command->value;
        }
    }
    state = g_commandStates;
    for ( i = 0; i < g_levelMap.stateCount; i++, start++, state++ ) {
        state->value = 0;
        state->flags = 0;
        state->delay = 1;
        state->reload = 1;
        state->current = start->param[0];
        state->limit = start->param[1];
        if ( start->label == -1 ) {
            state->flags |= STATE_STOPPED;
        } else {
            state->commandStart = state->commandPointer = g_commandLabels[start->label];
            state->commandPointer--;
            Command_Advance( state );
        }
    }
}

/*
==================
Command_StartSequencer
==================
*/
/* name: inferred from its body, which sets up the level's command states */
void Command_StartSequencer( void )
{
    u16 count = g_levelMap.stateCount;
    g_commandStateCount = count;
    if ( count != 0 )
        Command_InitStates( g_levelMap.commandStarts );
}

/*
==================
Command_AddState

Byte counters in the command states clamp at their per-state limit.
==================
*/
/* name: inferred from its body, which adds to a state's counter up to its limit */
void Command_AddState( int amount, s16 index )
{
    commandState_t *state = &g_commandStates[index];
    state->current = (u8)( state->current + (unsigned int)amount );
    if ( state->current > state->limit )
        state->current = state->limit;
}

/*
==================
Command_SequencerTask
==================
*/
/* name: inferred from its body, which advances every state whose counter reached its limit */
void Command_SequencerTask( TASK *task )
{
    u16 count = g_commandStateCount;
    s16 remaining = count;

    if ( count != 0 ) {
        commandState_t *state = g_commandStates;
        while ( remaining-- != 0 ) {
            if ( state->current >= state->limit && ( state->flags & STATE_STOPPED ) == 0 )
                Command_Advance( state );
            ++state;
        }
    }
    g_nextTask = task->Next;
}
