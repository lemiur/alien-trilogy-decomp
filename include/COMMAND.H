/*
 * Level script states (src/command_sequencer.c): each state steps through the
 * level's command list (LEVEL.H Command), holding each value for a delay. Model
 * rows animate by a state's value (src/model_draw.c).
 * Requires <sys/types.h> and <LEVEL.H>.
 */
#ifndef COMMAND_H
#define COMMAND_H

/* commandState_t.flags */
#define STATE_STOPPED 1

typedef struct commandState_s {
    short      value;
    u_short    flags;
    u_char     delay;
    u_char     reload;
    u_char     current;
    u_char     limit;
    command_t *commandStart;
    command_t *commandPointer;
} commandState_t;

/* name: inferred from use: Command_InitStates sets up g_levelMap.stateCount of them */
extern commandState_t g_commandStates[];

struct TASK;

/* src/command_sequencer.c */
extern void Command_AddState( int amount, short index );
extern void Command_StartSequencer( void );
extern void Command_SequencerTask( struct TASK *task );

/* Command.value no level command holds: Command_InitStates's first label differs from it. name: invented */
#define COMMAND_NO_LABEL 0xffff

#endif
