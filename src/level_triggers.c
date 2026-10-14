/* level_triggers.c -- level triggers: loading the cell and level trigger tables, and the player, actor and shot trigger tests */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <GAME.H>
#include <CAMERA.H>
#include <ACTOR.H>
#include <MODEL.H>
#include <PARTICLE.H>
#include <COMMAND.H>
#include <TYPES.H>

/* name: runtime evidence, analysis/runtime/globals.json: 4-byte records {flags, command index, charges, -} from the level file, indexed by a cell's trigger byte (1-based) */
extern cellTrigger_t g_cellTriggers[];
/* Module statics (.sbss 0x8008d1d8..0x8008d1e0), in their retail order. */
/* name: runtime evidence, analysis/runtime/globals.json: records in g_cellTriggers */
static short           g_cellTriggerCount;
static levelCommand_t *g_levelCommands;

/*
==================
Trigger_LoadCellTriggers
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Trigger_LoadCellTriggers( cellTrigger_t *source, int count )
{
    cellTrigger_t *out;
    short          remaining;
    short          i;
    source++;
    out = g_cellTriggers;
    remaining = (short)( count - 1 );
    g_cellTriggerCount = remaining;
    i = 0;
    while ( i < remaining ) {
        out->flags = source->flags;
        out->command = source->command;
        out->charges = source->charges;
        source++;
        out++;
        ++i;
    }
}

/*
==================
Trigger_LoadLevelTriggers
==================
*/
/* name: inferred from its body, which takes the level's trigger commands */
void Trigger_LoadLevelTriggers( void )
{
    g_levelCommands = g_levelMap.triggerCommands;
    Trigger_LoadCellTriggers( g_levelMap.cellTriggers, g_levelMap.cellTriggerCount );
}

/*
==================
Trigger_RunLevelCommands

Four-byte records contain command, next index, and two byte operands.
The known caller ignores the return register. This void ABI view recovers
observed effects; the original source-level return type is unestablished.
==================
*/
void Trigger_RunLevelCommands( u8 index )
{
    s16 result = 0;

    do {
        levelCommand_t *command = &g_levelCommands[(u8)index];
        switch ( command->code ) {
        case TRIGGER_LEVEL_END:
            g_gameFlags |= GAME_FLAG_LEVEL_COMPLETE;
            result = -1;
            break;
        case TRIGGER_ACTOR_GROUP:
            Actor_GroupAdd( command->amount, command->index );
            result = -1;
            break;
        case TRIGGER_MAP_OBJECT:
            Object_Trigger( command->amount, command->index );
            result = -1;
            break;
        case TRIGGER_LIFT_LAST:
            result = Lift_TriggerLast( command->amount, command->index );
            break;
        case TRIGGER_DOOR_POWER_FINAL:
            result = Door_PowerFinal( command->amount, command->index );
            break;
        case TRIGGER_LIGHT_RAISE:
            Light_Raise( command->amount, command->index );
            result = -1;
            break;
        case TRIGGER_SWITCH:
            result = Object_ActivateLevelObject( command->index, SWITCH_STATE_ON );
            break;
        case TRIGGER_DOOR_POWER:
            result = Door_Power( command->amount, command->index );
            break;
        case TRIGGER_LIFT:
            result = Lift_Trigger( command->amount, command->index );
            break;
        case TRIGGER_COMMAND_STATE:
            Command_AddState( command->amount, command->index );
            break;
        }
        if ( result == 0 )
            break;
        index = command->next;
    } while ( (u8)index != TRIGGER_CHAIN_END );
}

/*
==================
Trigger_PlayerCell

Runs the cell trigger under the player: a trigger flagged for the player (1) runs
its command chain once on entering the cell (and the button-driven commands, 6 and
4, while the use button is pressed); a finished chain uses up one of the trigger's
charges (0xff: endless). Player.lastTrigger keeps the trigger the player stood in last.
==================
*/
/* name: inferred from its body (called by Player_Move with the use button) */
void Trigger_PlayerCell( int buttons )
{
    mapCell_t      *cell;
    cellTrigger_t  *trigger;
    levelCommand_t *command;
    s16             pressed;
    s16             result;
    s16             skipEntry;
    s16             stillInside;
    u8              index;

    if ( g_cellTriggerCount == 0 )
        return;
    cell = g_player.cell;
    if ( cell->trigger != 0 ) {
        trigger = &g_cellTriggers[cell->trigger - 1];
        if ( !( trigger->flags & TRIGGER_BY_PLAYER ) )
            return;
        if ( trigger->charges == 0 )
            return;
        result = 0;
        pressed = buttons;
        stillInside = g_player.lastTrigger == cell->trigger;
        skipEntry = stillInside;
        index = trigger->command;
        do {
            command = &g_levelCommands[index];
            switch ( command->code ) {
            case TRIGGER_LEVEL_END:
                g_gameFlags |= GAME_FLAG_LEVEL_COMPLETE;
                result = -1;
                break;
            case TRIGGER_ACTOR_GROUP:
                if ( skipEntry )
                    break;
                Actor_GroupAdd( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_MAP_OBJECT:
                if ( skipEntry )
                    break;
                Object_Trigger( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_LIFT_LAST:
                if ( skipEntry )
                    break;
                result = Lift_TriggerLast( command->amount, command->index );
                break;
            case TRIGGER_DOOR_POWER_FINAL:
                if ( !pressed )
                    break;
                result = Door_PowerFinal( command->amount, command->index );
                break;
            case TRIGGER_LIGHT_RAISE:
                if ( skipEntry )
                    break;
                Light_Raise( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_SWITCH:
                if ( !pressed )
                    break;
                result = Object_ActivateLevelObject( command->index, trigger->charges );
                break;
            case TRIGGER_DOOR_POWER:
                if ( skipEntry )
                    break;
                result = Door_Power( command->amount, command->index );
                break;
            case TRIGGER_LIFT:
                if ( skipEntry )
                    break;
                result = Lift_Trigger( command->amount, command->index );
                break;
            case TRIGGER_COMMAND_STATE:
                if ( skipEntry )
                    break;
                Command_AddState( command->amount, command->index );
                result = -1;
                break;
            }
            if ( result == 0 )
                goto failed;
            index = command->next;
        } while ( index != TRIGGER_CHAIN_END );
        if ( trigger->charges != TRIGGER_ENDLESS )
            trigger->charges--;
    }
    g_player.lastTrigger = cell->trigger;
    return;
failed:
    if ( !stillInside )
        g_player.lastTrigger = 0;
}

/*
==================
Trigger_ActorCell

The actor version of Trigger_PlayerCell: triggers flagged 4, no button commands;
Actor.lastTrigger keeps the trigger the actor stood in last.
==================
*/
/* name: inferred from its body (called from the actor movement code) */
void Trigger_ActorCell( actor_t *actor )
{
    mapCell_t      *cell;
    cellTrigger_t  *trigger;
    levelCommand_t *command;
    s16             result;
    s16             skipEntry;
    s16             stillInside;
    u8              index;

    if ( g_cellTriggerCount == 0 )
        return;
    cell = actor->cell;
    if ( cell->trigger != 0 ) {
        trigger = &g_cellTriggers[cell->trigger - 1];
        if ( !( trigger->flags & TRIGGER_BY_ACTOR ) )
            return;
        if ( trigger->charges == 0 )
            return;
        result = 0;
        stillInside = actor->lastTrigger == cell->trigger;
        skipEntry = stillInside;
        index = trigger->command;
        do {
            command = &g_levelCommands[index];
            switch ( command->code ) {
            case TRIGGER_LEVEL_END:
                g_gameFlags |= GAME_FLAG_LEVEL_COMPLETE;
                result = -1;
                break;
            case TRIGGER_ACTOR_GROUP:
                if ( skipEntry )
                    break;
                Actor_GroupAdd( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_MAP_OBJECT:
                if ( skipEntry )
                    break;
                Object_Trigger( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_LIFT_LAST:
                if ( skipEntry )
                    break;
                result = Lift_TriggerLast( command->amount, command->index );
                break;
            case TRIGGER_LIGHT_RAISE:
                if ( skipEntry )
                    break;
                Light_Raise( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_DOOR_POWER:
                if ( skipEntry )
                    break;
                result = Door_Power( command->amount, command->index );
                break;
            case TRIGGER_LIFT:
                if ( skipEntry )
                    break;
                result = Lift_Trigger( command->amount, command->index );
                break;
            case TRIGGER_COMMAND_STATE:
                if ( skipEntry )
                    break;
                Command_AddState( command->amount, command->index );
                result = -1;
                break;
            }
            if ( result == 0 )
                goto failed;
            index = command->next;
        } while ( index != TRIGGER_CHAIN_END );
        if ( trigger->charges != TRIGGER_ENDLESS )
            trigger->charges--;
    }
    actor->lastTrigger = cell->trigger;
    return;
failed:
    if ( !stillInside )
        actor->lastTrigger = 0;
}

/*
==================
Trigger_ShotCell

The particle version of Trigger_ActorCell for triggers flagged 2, from the map cell
under the particle; Particle.lastTrigger keeps the trigger it was in last.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Trigger_ShotCell( particle_t *particle )
{
    mapCell_t      *cell;
    u_int           x;
    u_int           z;
    cellTrigger_t  *trigger;
    levelCommand_t *command;
    s16             result;
    s16             skipEntry;
    s16             stillInside;
    u8              index;

    if ( g_cellTriggerCount == 0 )
        return;
    x = particle->position->vx + g_levelMap.offsetX;
    z = particle->position->vz + g_levelMap.offsetZ;
    cell = MAP_CELL( x, z );
    if ( cell->trigger != 0 ) {
        trigger = &g_cellTriggers[cell->trigger - 1];
        if ( !( trigger->flags & TRIGGER_BY_SHOT ) )
            return;
        if ( trigger->charges == 0 )
            return;
        result = 0;
        stillInside = particle->lastTrigger == cell->trigger;
        skipEntry = stillInside;
        index = trigger->command;
        do {
            command = &g_levelCommands[index];
            switch ( command->code ) {
            case TRIGGER_LEVEL_END:
                g_gameFlags |= GAME_FLAG_LEVEL_COMPLETE;
                result = -1;
                break;
            case TRIGGER_ACTOR_GROUP:
                if ( skipEntry )
                    break;
                Actor_GroupAdd( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_MAP_OBJECT:
                if ( skipEntry )
                    break;
                Object_Trigger( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_LIFT_LAST:
                if ( skipEntry )
                    break;
                result = Lift_TriggerLast( command->amount, command->index );
                break;
            case TRIGGER_LIGHT_RAISE:
                if ( skipEntry )
                    break;
                Light_Raise( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_DOOR_POWER:
                if ( skipEntry )
                    break;
                result = Door_Power( command->amount, command->index );
                break;
            case TRIGGER_LIFT:
                if ( skipEntry )
                    break;
                result = Lift_Trigger( command->amount, command->index );
                break;
            case TRIGGER_COMMAND_STATE:
                if ( skipEntry )
                    break;
                Command_AddState( command->amount, command->index );
                result = -1;
                break;
            }
            if ( result == 0 )
                goto failed;
            index = command->next;
        } while ( index != TRIGGER_CHAIN_END );
        if ( trigger->charges != TRIGGER_ENDLESS )
            trigger->charges--;
    }
    particle->lastTrigger = cell->trigger;
    return;
failed:
    if ( !stillInside )
        particle->lastTrigger = 0;
}

/*
==================
Trigger_EnemyShotCell

The particle version of Trigger_ActorCell for triggers flagged 8, from the map cell
under the particle; Particle.lastTrigger keeps the trigger it was in last.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Trigger_EnemyShotCell( particle_t *particle )
{
    mapCell_t      *cell;
    u_int           x;
    u_int           z;
    cellTrigger_t  *trigger;
    levelCommand_t *command;
    s16             result;
    s16             skipEntry;
    s16             stillInside;
    u8              index;

    if ( g_cellTriggerCount == 0 )
        return;
    x = particle->position->vx + g_levelMap.offsetX;
    z = particle->position->vz + g_levelMap.offsetZ;
    cell = MAP_CELL( x, z );
    if ( cell->trigger != 0 ) {
        trigger = &g_cellTriggers[cell->trigger - 1];
        if ( !( trigger->flags & TRIGGER_BY_ENEMY_SHOT ) )
            return;
        if ( trigger->charges == 0 )
            return;
        result = 0;
        stillInside = particle->lastTrigger == cell->trigger;
        skipEntry = stillInside;
        index = trigger->command;
        do {
            command = &g_levelCommands[index];
            switch ( command->code ) {
            case TRIGGER_LEVEL_END:
                g_gameFlags |= GAME_FLAG_LEVEL_COMPLETE;
                result = -1;
                break;
            case TRIGGER_ACTOR_GROUP:
                if ( skipEntry )
                    break;
                Actor_GroupAdd( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_MAP_OBJECT:
                if ( skipEntry )
                    break;
                Object_Trigger( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_LIFT_LAST:
                if ( skipEntry )
                    break;
                result = Lift_TriggerLast( command->amount, command->index );
                break;
            case TRIGGER_LIGHT_RAISE:
                if ( skipEntry )
                    break;
                Light_Raise( command->amount, command->index );
                result = -1;
                break;
            case TRIGGER_DOOR_POWER:
                if ( skipEntry )
                    break;
                result = Door_Power( command->amount, command->index );
                break;
            case TRIGGER_LIFT:
                if ( skipEntry )
                    break;
                result = Lift_Trigger( command->amount, command->index );
                break;
            case TRIGGER_COMMAND_STATE:
                if ( skipEntry )
                    break;
                Command_AddState( command->amount, command->index );
                result = -1;
                break;
            }
            if ( result == 0 )
                goto failed;
            index = command->next;
        } while ( index != TRIGGER_CHAIN_END );
        if ( trigger->charges != TRIGGER_ENDLESS )
            trigger->charges--;
    }
    particle->lastTrigger = cell->trigger;
    return;
failed:
    if ( !stillInside )
        particle->lastTrigger = 0;
}
