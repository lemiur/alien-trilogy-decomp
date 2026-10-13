/*
 * Level script states (src/command_sequencer.c): each state steps through the
 * level's command list (LEVEL.H Command), holding each value for a delay. Model
 * rows animate by a state's value (src/model_draw.c).
 * Requires <sys/types.h> and <LEVEL.H>.
 */
#ifndef COMMAND_H
#define COMMAND_H

/* CommandState.flags */
#define STATE_STOPPED 1

typedef struct CommandState {
    short value;
    u_short flags;
    u_char delay;
    u_char reload;
    u_char current;
    u_char limit;
    Command *command_start;
    Command *command_pointer;
} CommandState;

/* name: inferred from use: command_states_init sets up g_level_map.state_count of them */
extern CommandState g_command_states[];

struct TASK;

/* src/command_sequencer.c */
extern void command_state_add(int amount, short index);
extern void command_sequencer_start(void);
extern void command_sequencer_task(struct TASK *task);

/* Command.value no level command holds: command_states_init's first label differs from it. name: invented */
#define COMMAND_NO_LABEL 0xffff

#endif
