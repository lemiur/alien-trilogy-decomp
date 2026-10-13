/* Level script sequencer: each state steps through the level's command list,
 * holding each value for a delay set by control commands. */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <TASK.H>
#include <COMMAND.H>
#include <GAME.H>

/* Command.opcode: with COMMAND_CONTROL set, the low bits are a control code
 * (below) that runs at once; otherwise the command's value is shown. */
#define COMMAND_CONTROL 0x8000
#define COMMAND_CODE_MASK 0x7fff

/* Control codes. names: inferred from what command_run does for each */
enum {
    COMMAND_RESTART = 1,       /* back to the state's first command */
    COMMAND_DELAY_1,           /* hold each value for 1, 2, 3, 4 or 16 ticks */
    COMMAND_DELAY_2,
    COMMAND_DELAY_3,
    COMMAND_DELAY_4,
    COMMAND_DELAY_16,
    COMMAND_RANDOM_DELAY_4,    /* hold for a random 1..4, 1..8, 1..16 or 1..32 ticks */
    COMMAND_RANDOM_DELAY_8,
    COMMAND_RANDOM_DELAY_16,
    COMMAND_RANDOM_DELAY_32,
    COMMAND_STOP
};

typedef unsigned char u8;
typedef unsigned short u16;
typedef short s16;



/* First command of each label (a run of equal Command.value).
 * name: inferred from command_states_init, which fills it */
extern Command *g_command_labels[];
/* name: inferred from it holding g_level_map.state_count */
extern u16 g_command_state_count;

/* Runs the control commands at the state's position; -1 once it stops.
 * name: inferred from that */
int command_run(CommandState *state)
{
    Command *command = state->command_pointer;

    while (command->opcode & COMMAND_CONTROL) {
        switch (command->opcode & COMMAND_CODE_MASK) {
        case COMMAND_RESTART:
            command = state->command_pointer = state->command_start;
            break;
        case COMMAND_DELAY_1:
            state->delay = state->reload = 1;
            state->command_pointer = ++command;
            break;
        case COMMAND_DELAY_2:
            state->delay = state->reload = 2;
            state->command_pointer = ++command;
            break;
        case COMMAND_DELAY_3:
            state->delay = state->reload = 3;
            state->command_pointer = ++command;
            break;
        case COMMAND_DELAY_4:
            state->delay = state->reload = 4;
            state->command_pointer = ++command;
            break;
        case COMMAND_DELAY_16:
            state->delay = state->reload = 16;
            state->command_pointer = ++command;
            break;
        case COMMAND_RANDOM_DELAY_4:
            state->delay = state->reload = (random() & 3) + 1;
            state->command_pointer = ++command;
            break;
        case COMMAND_RANDOM_DELAY_8:
            state->delay = state->reload = (random() & 7) + 1;
            state->command_pointer = ++command;
            break;
        case COMMAND_RANDOM_DELAY_16:
            state->delay = state->reload = (random() & 15) + 1;
            state->command_pointer = ++command;
            break;
        case COMMAND_RANDOM_DELAY_32:
            state->delay = state->reload = (random() & 31) + 1;
            state->command_pointer = ++command;
            break;
        case COMMAND_STOP:
            state->flags |= STATE_STOPPED;
            return -1;
        }
    }
    return 0;
}

/* Counts down the state's delay, then moves to its next command.
 * name: inferred from that */
void command_advance(CommandState *state)
{
    if (--state->delay == 0) {
        state->delay = state->reload;
        state->command_pointer++;
        if (command_run(state) == 0)
            state->value = state->command_pointer->opcode;
    }
}

/* name: inferred from its body, which finds each state's start label in the level script */
void command_states_init(CommandStart *start)
{
    Command *command = g_level_map.commands;
    u16 previous = COMMAND_NO_LABEL;
    short labels = 0;
    short i;
    CommandState *state;

    for (i = 0; i < g_level_map.command_count; i++, command++) {
        if (command->value != previous) {
            g_command_labels[labels++] = command;
            previous = command->value;
        }
    }
    state = g_command_states;
    for (i = 0; i < g_level_map.state_count; i++, start++, state++) {
        state->value = 0;
        state->flags = 0;
        state->delay = 1;
        state->reload = 1;
        state->current = start->param[0];
        state->limit = start->param[1];
        if (start->label == -1) {
            state->flags |= STATE_STOPPED;
        } else {
            state->command_start = state->command_pointer = g_command_labels[start->label];
            state->command_pointer--;
            command_advance(state);
        }
    }
}

/* name: inferred from its body, which sets up the level's command states */
void command_sequencer_start(void)
{
    u16 count = g_level_map.state_count;
    g_command_state_count = count;
    if (count != 0)
        command_states_init(g_level_map.command_starts);
}

/* Byte counters in the command states clamp at their per-state limit. */
/* name: inferred from its body, which adds to a state's counter up to its limit */
void command_state_add(int amount, s16 index)
{
    CommandState *state = &g_command_states[index];
    state->current = (u8)(state->current + (unsigned int)amount);
    if (state->current > state->limit)
        state->current = state->limit;
}

/* name: inferred from its body, which advances every state whose counter reached its limit */
void command_sequencer_task(TASK *task)
{
    u16 count = g_command_state_count;
    s16 remaining = count;

    if (count != 0) {
        CommandState *state = g_command_states;
        while (remaining-- != 0) {
            if (state->current >= state->limit && (state->flags & STATE_STOPPED) == 0)
                command_advance(state);
            ++state;
        }
    }
    g_next_task = task->Next;
}
