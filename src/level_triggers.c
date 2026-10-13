#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <GAME.H>
#include <CAMERA.H>
#include <ACTOR.H>
#include <MODEL.H>
#include <PARTICLE.H>
#include <COMMAND.H>
typedef unsigned char u8;
typedef short s16;
typedef unsigned int u32;

/* name: runtime evidence, analysis/runtime/globals.json: 4-byte records {flags, command index, charges, -} from the level file, indexed by a cell's trigger byte (1-based) */
extern unsigned char g_cell_triggers[];
/* Module statics (.sbss 0x8008d1d8..0x8008d1e0), in their retail order. */
/* name: runtime evidence, analysis/runtime/globals.json: records in g_cell_triggers */
static short g_cell_trigger_count;
static volatile u32 g_level_commands;

/* name: runtime evidence, analysis/runtime/functions.json */
void cell_triggers_load(void *buffer, int count)
{
    unsigned char *source = (unsigned char *)buffer;
    unsigned char *out;
    short remaining;
    short i;
    source += CELL_TRIGGER_SIZE;
    out = g_cell_triggers;
    remaining = (short)(count - 1);
    g_cell_trigger_count = remaining;
    i = 0;
    while (i < remaining) {
        out[CELL_TRIGGER_FLAGS] = source[CELL_TRIGGER_FLAGS];
        out[CELL_TRIGGER_COMMAND] = source[CELL_TRIGGER_COMMAND];
        out[CELL_TRIGGER_CHARGES] = source[CELL_TRIGGER_CHARGES];
        source += CELL_TRIGGER_SIZE;
        out += CELL_TRIGGER_SIZE;
        ++i;
    }
}

/* name: inferred from its body, which takes the level's trigger commands */
void level_triggers_load(void)
{
    g_level_commands = g_level_map.trigger_commands;
    cell_triggers_load(g_level_map.cell_triggers, g_level_map.cell_trigger_count);
}

/* Four-byte records contain command, next index, and two byte operands.
 * The known caller ignores the return register. This void ABI view recovers
 * observed effects; the original source-level return type is unestablished.
 */
void level_commands_run(u8 index)
{
    s16 result = 0;

    do {
        u8 *command = (u8 *)g_level_commands + ((u8)index * LEVEL_COMMAND_SIZE);
        switch (command[LEVEL_COMMAND_CODE]) {
        case TRIGGER_LEVEL_END:
            g_game_flags |= GAME_FLAG_LEVEL_COMPLETE;
            result = -1;
            break;
        case TRIGGER_ACTOR_GROUP:
            actor_group_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
            result = -1;
            break;
        case TRIGGER_MAP_OBJECT:
            map_object_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
            result = -1;
            break;
        case TRIGGER_LIFT_LAST:
            result = lift_trigger_last(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
            break;
        case TRIGGER_DOOR_POWER_FINAL:
            result = door_power_final(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
            break;
        case TRIGGER_LIGHT_RAISE:
            light_raise(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
            result = -1;
            break;
        case TRIGGER_SWITCH:
            result = level_object_activate(command[LEVEL_COMMAND_INDEX], SWITCH_STATE_ON);
            break;
        case TRIGGER_DOOR_POWER:
            result = door_power(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
            break;
        case TRIGGER_LIFT:
            result = lift_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
            break;
        case TRIGGER_COMMAND_STATE:
            command_state_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
            break;
        }
        if (result == 0)
            break;
        index = command[LEVEL_COMMAND_NEXT];
    } while ((u8)index != TRIGGER_CHAIN_END);
}

/* Runs the cell trigger under the player: a trigger flagged for the player (1) runs
 * its command chain once on entering the cell (and the button-driven commands, 6 and
 * 4, while the use button is pressed); a finished chain uses up one of the trigger's
 * charges (0xff: endless). Player.last_trigger keeps the trigger the player stood in last.
 * name: inferred from its body (called by player_move with the use button) */
void player_cell_trigger(int buttons)
{
    MapCell *cell;
    u8 *trigger;
    u8 *command;
    s16 pressed;
    s16 result;
    s16 skip_entry;
    s16 still_inside;
    u8 index;

    if (g_cell_trigger_count == 0)
        return;
    cell = g_player.cell;
    if (cell->trigger != 0) {
        trigger = &g_cell_triggers[cell->trigger * CELL_TRIGGER_SIZE - CELL_TRIGGER_SIZE];
        if (!(trigger[CELL_TRIGGER_FLAGS] & TRIGGER_BY_PLAYER))
            return;
        if (trigger[CELL_TRIGGER_CHARGES] == 0)
            return;
        result = 0;
        pressed = buttons;
        still_inside = g_player.last_trigger == cell->trigger;
        skip_entry = still_inside;
        index = trigger[CELL_TRIGGER_COMMAND];
        do {
            command = (u8 *)g_level_commands + index * LEVEL_COMMAND_SIZE;
            switch (command[LEVEL_COMMAND_CODE]) {
            case TRIGGER_LEVEL_END:
                g_game_flags |= GAME_FLAG_LEVEL_COMPLETE;
                result = -1;
                break;
            case TRIGGER_ACTOR_GROUP:
                if (skip_entry)
                    break;
                actor_group_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_MAP_OBJECT:
                if (skip_entry)
                    break;
                map_object_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_LIFT_LAST:
                if (skip_entry)
                    break;
                result = lift_trigger_last(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_DOOR_POWER_FINAL:
                if (!pressed)
                    break;
                result = door_power_final(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_LIGHT_RAISE:
                if (skip_entry)
                    break;
                light_raise(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_SWITCH:
                if (!pressed)
                    break;
                result = level_object_activate(command[LEVEL_COMMAND_INDEX], trigger[CELL_TRIGGER_CHARGES]);
                break;
            case TRIGGER_DOOR_POWER:
                if (skip_entry)
                    break;
                result = door_power(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_LIFT:
                if (skip_entry)
                    break;
                result = lift_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_COMMAND_STATE:
                if (skip_entry)
                    break;
                command_state_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            }
            if (result == 0)
                goto failed;
            index = command[LEVEL_COMMAND_NEXT];
        } while (index != TRIGGER_CHAIN_END);
        if (trigger[CELL_TRIGGER_CHARGES] != TRIGGER_ENDLESS)
            trigger[CELL_TRIGGER_CHARGES]--;
    }
    g_player.last_trigger = cell->trigger;
    return;
failed:
    if (!still_inside)
        g_player.last_trigger = 0;
}

/* The actor version of player_cell_trigger: triggers flagged 4, no button commands;
 * Actor.last_trigger keeps the trigger the actor stood in last.
 * name: inferred from its body (called from the actor movement code) */
void actor_cell_trigger(Actor *actor)
{
    MapCell *cell;
    u8 *trigger;
    u8 *command;
    s16 result;
    s16 skip_entry;
    s16 still_inside;
    u8 index;

    if (g_cell_trigger_count == 0)
        return;
    cell = actor->cell;
    if (cell->trigger != 0) {
        trigger = &g_cell_triggers[cell->trigger * CELL_TRIGGER_SIZE - CELL_TRIGGER_SIZE];
        if (!(trigger[CELL_TRIGGER_FLAGS] & TRIGGER_BY_ACTOR))
            return;
        if (trigger[CELL_TRIGGER_CHARGES] == 0)
            return;
        result = 0;
        still_inside = actor->last_trigger == cell->trigger;
        skip_entry = still_inside;
        index = trigger[CELL_TRIGGER_COMMAND];
        do {
            command = (u8 *)g_level_commands + index * LEVEL_COMMAND_SIZE;
            switch (command[LEVEL_COMMAND_CODE]) {
            case TRIGGER_LEVEL_END:
                g_game_flags |= GAME_FLAG_LEVEL_COMPLETE;
                result = -1;
                break;
            case TRIGGER_ACTOR_GROUP:
                if (skip_entry)
                    break;
                actor_group_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_MAP_OBJECT:
                if (skip_entry)
                    break;
                map_object_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_LIFT_LAST:
                if (skip_entry)
                    break;
                result = lift_trigger_last(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_LIGHT_RAISE:
                if (skip_entry)
                    break;
                light_raise(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_DOOR_POWER:
                if (skip_entry)
                    break;
                result = door_power(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_LIFT:
                if (skip_entry)
                    break;
                result = lift_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_COMMAND_STATE:
                if (skip_entry)
                    break;
                command_state_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            }
            if (result == 0)
                goto failed;
            index = command[LEVEL_COMMAND_NEXT];
        } while (index != TRIGGER_CHAIN_END);
        if (trigger[CELL_TRIGGER_CHARGES] != TRIGGER_ENDLESS)
            trigger[CELL_TRIGGER_CHARGES]--;
    }
    actor->last_trigger = cell->trigger;
    return;
failed:
    if (!still_inside)
        actor->last_trigger = 0;
}

/* The particle version of actor_cell_trigger for triggers flagged 2, from the map cell
 * under the particle; Particle.last_trigger keeps the trigger it was in last. */
/* name: runtime evidence, analysis/runtime/functions.json */
void shot_cell_trigger(Particle *particle)
{
    MapCell *cell;
    u_int x;
    u_int z;
    u8 *trigger;
    u8 *command;
    s16 result;
    s16 skip_entry;
    s16 still_inside;
    u8 index;

    if (g_cell_trigger_count == 0)
        return;
    x = particle->position->vx + g_level_map.offset_x;
    z = particle->position->vz + g_level_map.offset_z;
    cell = MAP_CELL(x, z);
    if (cell->trigger != 0) {
        trigger = &g_cell_triggers[cell->trigger * CELL_TRIGGER_SIZE - CELL_TRIGGER_SIZE];
        if (!(trigger[CELL_TRIGGER_FLAGS] & TRIGGER_BY_SHOT))
            return;
        if (trigger[CELL_TRIGGER_CHARGES] == 0)
            return;
        result = 0;
        still_inside = particle->last_trigger == cell->trigger;
        skip_entry = still_inside;
        index = trigger[CELL_TRIGGER_COMMAND];
        do {
            command = (u8 *)g_level_commands + index * LEVEL_COMMAND_SIZE;
            switch (command[LEVEL_COMMAND_CODE]) {
            case TRIGGER_LEVEL_END:
                g_game_flags |= GAME_FLAG_LEVEL_COMPLETE;
                result = -1;
                break;
            case TRIGGER_ACTOR_GROUP:
                if (skip_entry)
                    break;
                actor_group_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_MAP_OBJECT:
                if (skip_entry)
                    break;
                map_object_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_LIFT_LAST:
                if (skip_entry)
                    break;
                result = lift_trigger_last(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_LIGHT_RAISE:
                if (skip_entry)
                    break;
                light_raise(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_DOOR_POWER:
                if (skip_entry)
                    break;
                result = door_power(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_LIFT:
                if (skip_entry)
                    break;
                result = lift_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_COMMAND_STATE:
                if (skip_entry)
                    break;
                command_state_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            }
            if (result == 0)
                goto failed;
            index = command[LEVEL_COMMAND_NEXT];
        } while (index != TRIGGER_CHAIN_END);
        if (trigger[CELL_TRIGGER_CHARGES] != TRIGGER_ENDLESS)
            trigger[CELL_TRIGGER_CHARGES]--;
    }
    particle->last_trigger = cell->trigger;
    return;
failed:
    if (!still_inside)
        particle->last_trigger = 0;
}

/* The particle version of actor_cell_trigger for triggers flagged 8, from the map cell
 * under the particle; Particle.last_trigger keeps the trigger it was in last. */
/* name: runtime evidence, analysis/runtime/functions.json */
void enemy_shot_cell_trigger(Particle *particle)
{
    MapCell *cell;
    u_int x;
    u_int z;
    u8 *trigger;
    u8 *command;
    s16 result;
    s16 skip_entry;
    s16 still_inside;
    u8 index;

    if (g_cell_trigger_count == 0)
        return;
    x = particle->position->vx + g_level_map.offset_x;
    z = particle->position->vz + g_level_map.offset_z;
    cell = MAP_CELL(x, z);
    if (cell->trigger != 0) {
        trigger = &g_cell_triggers[cell->trigger * CELL_TRIGGER_SIZE - CELL_TRIGGER_SIZE];
        if (!(trigger[CELL_TRIGGER_FLAGS] & TRIGGER_BY_ENEMY_SHOT))
            return;
        if (trigger[CELL_TRIGGER_CHARGES] == 0)
            return;
        result = 0;
        still_inside = particle->last_trigger == cell->trigger;
        skip_entry = still_inside;
        index = trigger[CELL_TRIGGER_COMMAND];
        do {
            command = (u8 *)g_level_commands + index * LEVEL_COMMAND_SIZE;
            switch (command[LEVEL_COMMAND_CODE]) {
            case TRIGGER_LEVEL_END:
                g_game_flags |= GAME_FLAG_LEVEL_COMPLETE;
                result = -1;
                break;
            case TRIGGER_ACTOR_GROUP:
                if (skip_entry)
                    break;
                actor_group_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_MAP_OBJECT:
                if (skip_entry)
                    break;
                map_object_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_LIFT_LAST:
                if (skip_entry)
                    break;
                result = lift_trigger_last(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_LIGHT_RAISE:
                if (skip_entry)
                    break;
                light_raise(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            case TRIGGER_DOOR_POWER:
                if (skip_entry)
                    break;
                result = door_power(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_LIFT:
                if (skip_entry)
                    break;
                result = lift_trigger(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                break;
            case TRIGGER_COMMAND_STATE:
                if (skip_entry)
                    break;
                command_state_add(command[LEVEL_COMMAND_AMOUNT], command[LEVEL_COMMAND_INDEX]);
                result = -1;
                break;
            }
            if (result == 0)
                goto failed;
            index = command[LEVEL_COMMAND_NEXT];
        } while (index != TRIGGER_CHAIN_END);
        if (trigger[CELL_TRIGGER_CHARGES] != TRIGGER_ENDLESS)
            trigger[CELL_TRIGGER_CHARGES]--;
    }
    particle->last_trigger = cell->trigger;
    return;
failed:
    if (!still_inside)
        particle->last_trigger = 0;
}
