/* object_render.c -- level objects: their models, creation, ticking, drawing and tasks */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <CAMERA.H>
#include <TASK.H>
#include <HEIGHT.H>
#include <AUDIO.H>
#include <GAME.H>
#include <VIEW.H>
#include <MODEL.H>
#include <SPRITEUV.H>
#include <TEXTURE.H>
#include <TEXT.H>
#include <HUD.H>
#include <PARTICLE.H>
#include <TRACK.H>
#include <SPRITE.H>
#include <ACTOR.H>
#include <LIBCD.H>
#include <HEAPFILE.H>
#include <HEAP.H>
#include <COMMAND.H>
#include <DEBRIS.H>
#include <TYPES.H>


extern levelObject_t *g_levelObjects;
/* invented names: ring offsets (x, z) of the 8 inner and 16 outer cells around an explosion */
static short g_blastNearOffsets[BLAST_NEAR_CELLS * 2] = { -1, -1, 0, -1, 1, -1, 1, 0, 1, 1, 0, 1, -1, 1, -1, 0 };
static short g_blastFarOffsets[BLAST_FAR_CELLS * 2] = { -2, -2, -1, -2, 0, -2, 1,  -2, 2,  -2, 2,  -1, 2,  0, 2,  1,
                                                        2,  2,  1,  2,  0, 2,  -1, 2,  -2, 2,  -2, 1,  -2, 0, -2, -1 };
/* invented names: the cells of those rings that an explosion reaches (Object_ExplosionDamage) */
extern mapCell_t *g_blastNear[BLAST_NEAR_CELLS];
extern mapCell_t *g_blastFar[BLAST_FAR_CELLS];
#define ABS( x ) ( ( x ) >= 0 ? ( x ) : -( x ) )
extern void Object_CreateLevelObjects( levelObjectRecord_t *record, s16 count );
/* name: invented; Object_CreateLevelObjects keeps the level's object records and their count */
extern levelObjectRecord_t *g_levelObjectRecords;
extern s16                  g_levelObjectCount;
/* name: invented; level object models, filled at level load (identity-TODO: layout of the table) */
extern dispatchRow_t *g_levelObjectModels[];
extern void Object_MarkLevelObjectCells( levelObject_t *block );
extern void Object_LevelObjectTask( TASK *task );
extern void Object_LevelObjectSpinTask( TASK *task );
extern void Object_LevelObjectHitTask( TASK *task );
extern void Object_LevelObjectDebrisTask( TASK *task );
extern void Object_BlockObjectTask( TASK *task );
extern void Object_SwitchPanelTask( TASK *task );
extern void Object_SwitchPanelActiveTask( TASK *task );

/*
==================
Object_FixupLevelObjectModel

Fixes up a level object model's rows after loading: vertex indices become pointers into
vertices, the file's 16-bit face kind (with bit 7 ending the list) becomes the palette
flags and the dispatch selector (14 for the tall model).
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Object_FixupLevelObjectModel( modelRow_t *face, SVECTOR *vertices, u8 flags, s16 tall )
{
    int   index;
    short last;
    u8    value;

    if ( tall )
        flags += MODEL_PALETTE_TALL_OFFSET;
    do {
        index = face->file.vertices[QUAD_VERTEX_TOP_LEFT];
        face->row.vertices[QUAD_VERTEX_TOP_LEFT] = vertices + index;
        index = face->file.vertices[QUAD_VERTEX_TOP_RIGHT];
        face->row.vertices[QUAD_VERTEX_TOP_RIGHT] = vertices + index;
        index = face->file.vertices[QUAD_VERTEX_BOTTOM_RIGHT];
        face->row.vertices[QUAD_VERTEX_BOTTOM_RIGHT] = vertices + index;
        index = face->file.vertices[QUAD_VERTEX_BOTTOM_LEFT];
        face->row.vertices[QUAD_VERTEX_BOTTOM_LEFT] = vertices + index;
        last = face->file.kind & ROW_FLAG_LAST;
        if ( tall ) {
            face->row.selector = ROW_SELECT_TALL;
        } else {
            switch ( (s16)( face->file.kind & ~ROW_FLAG_LAST ) ) {
            case FACE_KIND_QUAD:
            case FACE_KIND_QUAD_OTHER:
                face->row.selector = ROW_SELECT_QUAD;
                break;
            case FACE_KIND_QUAD_MIRROR:
                face->row.selector = ROW_SELECT_QUAD_ALT;
                break;
            case FACE_KIND_TRIANGLE ... FACE_KIND_TRIANGLE_LAST:
                face->row.selector = ROW_SELECT_TRI;
                break;
            default:
                face->row.selector = ROW_SELECT_TRI;
                break;
            }
        }
        value = flags;
        if ( last )
            value |= ROW_FLAG_LAST;
        face->row.flags = value;
        face++;
    } while ( !( value & ROW_FLAG_LAST ) );
}

/*
==================
Object_LoadLevelObjectModels
==================
*/
/* name: invented; loads the level object models (g_fileTable[FILE_OBJ3D_BND]) into
 * g_levelObjectModels and allocates g_levelObjects */
/* name: static evidence, analysis/runtime/functions.json */
void Object_LoadLevelObjectModels( void )
{
    short       i;
    short       count;
    short       index;
    u_int       size;
    u_char     *p;
    modelRow_t *rows;
    SVECTOR    *vertices;
    u_char      text[BND_COUNT_DIGITS + 1];

    for ( i = 0; i < LEVEL_OBJECT_MODEL_COUNT; i++ )
        g_levelObjectModels[i] = 0;
    Sys_LoadFile( &g_fileTable[FILE_OBJ3D_BND] );
    p = g_fileTable[FILE_OBJ3D_BND].buffer;
    p += BND_HEADER_SIZE;
    text[0] = *p++;
    text[1] = *p++;
    text[2] = *p++;
    text[BND_COUNT_DIGITS - 1] = *p++;
    text[BND_COUNT_DIGITS] = 0;
    count = Text_ParseDecimal( text );
    for ( i = 0; i < count; i++ ) {
        p++;
        text[0] = *p++;
        text[1] = *p++;
        text[2] = *p++;
        text[BND_INDEX_DIGITS] = 0;
        index = Text_ParseDecimal( text );
        size = Text_ReadBigEndian32( p );
        p += BND_LENGTH_SIZE;
        if ( size & 1 )
            size++;
        rows = ( (modelBlock_t *)p )->rows;
        vertices = (SVECTOR *)( rows + ( (modelBlock_t *)p )->rowCount );
        if ( index == LEVEL_OBJECT_MODEL_TALL )
            Object_FixupLevelObjectModel( rows, vertices, MODEL_PALETTE, -1 );
        else
            Object_FixupLevelObjectModel( rows, vertices, MODEL_PALETTE, 0 );
        g_levelObjectModels[index] = &rows->row;
        p += size;
    }
    g_levelObjects = Heap_Alloc( LEVEL_OBJECT_MAX * sizeof( levelObject_t ) );
}

/* Whether an explosion at height is stopped by cell: no cell, a wall type, a height step
 * of BLAST_STEP_LIMIT or more, or a closed door (Object_ExplosionDamage). */
#define BLAST_BLOCKED( cell, height )                                                                              \
    ( ( cell ) == 0 || ( cell )->type >= CELL_TYPE_WALL || ABS( ( cell )->value - ( height ) ) >= BLAST_STEP_LIMIT \
      || ( ( cell )->type == CELL_TYPE_DOOR && ( cell )->ceiling - ( cell )->value < BLAST_DOOR_GAP ) )

/*
==================
Object_ExplosionDamage
==================
*/
/* name: invented; an explosion at position in cell: gathers the two rings of cells around
 * it, drops the outer cells shielded by blocked inner ones, then hurts the player, kills
 * actors and starts the destruction timers of level objects in the remaining cells. */
/* name: static evidence, analysis/runtime/functions.json */
void Object_ExplosionDamage( SVECTOR *position, mapCell_t *cell )
{
    u_char     height = cell->value;
    u_int      x = (u_int)( position->vx + g_levelMap.offsetX ) >> MAP_CELL_SHIFT;
    u_int      z = (u_int)( position->vz + g_levelMap.offsetZ ) >> MAP_CELL_SHIFT;
    short     *offset;
    mapCell_t *near;
    s16        i;

    offset = g_blastNearOffsets;
    for ( i = 0; i < BLAST_NEAR_CELLS; i++ ) {
        u_int cx = x + *offset++;
        u_int cz = z + *offset++;
        if ( cx < g_levelMap.width && cz < g_levelMap.height )
            g_blastNear[i] = g_levelMap.cells + g_levelMap.width * cz + cx;
        else
            g_blastNear[i] = 0;
    }
    offset = g_blastFarOffsets;
    for ( i = 0; i < BLAST_FAR_CELLS; i++ ) {
        u_int cx = x + *offset++;
        u_int cz = z + *offset++;
        if ( cx < g_levelMap.width && cz < g_levelMap.height )
            g_blastFar[i] = g_levelMap.cells + g_levelMap.width * cz + cx;
        else
            g_blastFar[i] = 0;
    }
    near = g_blastNear[BLAST_NEAR_NEG_X_NEG_Z];
    if ( BLAST_BLOCKED( near, height ) ) {
        g_blastNear[BLAST_NEAR_NEG_X_NEG_Z] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_NEG_X_NEG_Z )] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_NEG_X_NEG_Z ) + 1] = 0;
        g_blastFar[BLAST_FAR_CELLS - 1] = 0;
    }
    near = g_blastNear[BLAST_NEAR_NEG_Z];
    if ( BLAST_BLOCKED( near, height ) ) {
        g_blastNear[BLAST_NEAR_NEG_Z] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_NEG_Z )] = 0;
    }
    near = g_blastNear[BLAST_NEAR_POS_X_NEG_Z];
    if ( BLAST_BLOCKED( near, height ) ) {
        g_blastNear[BLAST_NEAR_POS_X_NEG_Z] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_POS_X_NEG_Z ) - 1] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_POS_X_NEG_Z )] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_POS_X_NEG_Z ) + 1] = 0;
    }
    near = g_blastNear[BLAST_NEAR_POS_X];
    if ( BLAST_BLOCKED( near, height ) ) {
        g_blastNear[BLAST_NEAR_POS_X] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_POS_X )] = 0;
    }
    near = g_blastNear[BLAST_NEAR_POS_X_POS_Z];
    if ( BLAST_BLOCKED( near, height ) ) {
        g_blastNear[BLAST_NEAR_POS_X_POS_Z] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_POS_X_POS_Z ) - 1] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_POS_X_POS_Z )] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_POS_X_POS_Z ) + 1] = 0;
    }
    near = g_blastNear[BLAST_NEAR_POS_Z];
    if ( BLAST_BLOCKED( near, height ) ) {
        g_blastNear[BLAST_NEAR_POS_Z] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_POS_Z )] = 0;
    }
    near = g_blastNear[BLAST_NEAR_NEG_X_POS_Z];
    if ( BLAST_BLOCKED( near, height ) ) {
        g_blastNear[BLAST_NEAR_NEG_X_POS_Z] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_NEG_X_POS_Z ) - 1] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_NEG_X_POS_Z )] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_NEG_X_POS_Z ) + 1] = 0;
    }
    near = g_blastNear[BLAST_NEAR_NEG_X];
    if ( BLAST_BLOCKED( near, height ) ) {
        g_blastNear[BLAST_NEAR_NEG_X] = 0;
        g_blastFar[BLAST_FAR_BEHIND( BLAST_NEAR_NEG_X )] = 0;
    }
    for ( i = 0; i < BLAST_NEAR_CELLS; i++ ) {
        mapCell_t *target = g_blastNear[i];
        if ( target != 0 && target->marker != 0 ) {
            if ( target->marker == CELL_MARKER_PLAYER ) {
                Player_Hit( BLAST_NEAR_DAMAGE, DEATH_HAZARD );
            } else if ( target->marker != 0 && target->marker < CELL_MARKER_ACTOR_LIMIT ) {
                actor_t *actor = &g_actorPool[target->occupant];
                if ( actor->kind != ACTOR_KIND_BOSS )
                    Actor_Damage( actor, actor->health, 0 );
                Particle_SpawnExplosion( actor->position.vx, actor->position.vy + ACTOR_BLAST_HEIGHT, actor->position.vz );
            } else {
                levelObject_t *object = &g_levelObjects[target->occupant];
                if ( !( object->flags & LEVEL_OBJECT_ARMED ) ) {
                    object->flags |= LEVEL_OBJECT_ARMED;
                    object->hits = ( Sys_Random() & BLAST_NEAR_HITS_MASK ) + BLAST_NEAR_HITS_MIN;
                }
            }
        }
    }
    for ( i = 0; i < BLAST_FAR_CELLS; i++ ) {
        mapCell_t *target = g_blastFar[i];
        if ( target != 0 && target->marker != 0 ) {
            if ( target->marker == CELL_MARKER_PLAYER ) {
                Player_Hit( BLAST_FAR_DAMAGE, DEATH_HAZARD );
            } else if ( target->marker != 0 && target->marker < CELL_MARKER_ACTOR_LIMIT ) {
                actor_t *actor = &g_actorPool[target->occupant];
                if ( actor->kind != ACTOR_KIND_BOSS )
                    Actor_Damage( actor, actor->health, 0 );
                Particle_SpawnExplosion( actor->position.vx, actor->position.vy + ACTOR_BLAST_HEIGHT, actor->position.vz );
            } else {
                levelObject_t *object = &g_levelObjects[target->occupant];
                if ( !( object->flags & LEVEL_OBJECT_ARMED ) ) {
                    object->flags |= LEVEL_OBJECT_ARMED;
                    object->hits = ( Sys_Random() & BLAST_FAR_HITS_MASK ) + BLAST_FAR_HITS_MIN;
                }
            }
        }
    }
}

/*
==================
Object_TickLevelObject
==================
*/
void Object_TickLevelObject( int index, int elapsed )
{
    levelObject_t *entry = &g_levelObjects[(u8)index];
    u16            remaining;

    if ( entry->flags & LEVEL_OBJECT_ARMED )
        return;
    remaining = entry->state - elapsed;
    entry->state = remaining;
    if ( (short)remaining <= 0 ) {
        entry->flags |= LEVEL_OBJECT_ARMED;
        entry->hits = ( Sys_Random() & TIMER_HITS_MASK ) + 1;
    }
}

/*
==================
Object_ActivateLevelObject
==================
*/
int Object_ActivateLevelObject( u8 index, u8 status )
{
    levelObject_t *entry = &g_levelObjects[index];
    if ( entry->state != 0 )
        return 0;
    if ( entry->type == LEVEL_OBJECT_SWITCH_BATTERY || entry->type == LEVEL_OBJECT_SWITCH_BATTERY_ALT ) {
        if ( g_player.batteries == 0 ) {
            Hud_ShowMessage( "Battery required" );
            return 0;
        }
        g_player.batteries--;
    }
    Snd_Play( SOUND_SWITCH, -1, SPU_VOLUME_MAX );
    g_player.tallies[entry->type]++;
    entry->state = status == SWITCH_STATE_ON ? SWITCH_STATE_ON : SWITCH_STATE_REARMS;
    return -1;
}

/*
==================
Object_MarkLevelObjectCells
==================
*/
void Object_MarkLevelObjectCells( levelObject_t *block )
{
    mapCell_t *base = block->cell;
    s16        outer = 0;
    if ( block->depth == 0 )
        return;
    do {
        s16        inner = 0;
        mapCell_t *row;
        if ( inner < block->width ) {
            row = base;
            do {
                row->marker = block->type;
                row->occupant = block->occupant;
                row++;
                inner++;
            } while ( inner < block->width );
        }
        base += g_levelMap.width;
        outer++;
    } while ( outer < block->depth );
}

/*
==================
Object_ClearLevelObjectCells
==================
*/
void Object_ClearLevelObjectCells( levelObject_t *block )
{
    mapCell_t *base = block->cell;
    s16        outer = 0;
    if ( block->depth == 0 )
        return;
    do {
        mapCell_t *row = base;
        s16        inner = 0;
        if ( inner < block->width ) {
            do {
                row->marker = 0;
                row++;
                inner++;
            } while ( inner < block->width );
        }
        base += g_levelMap.width;
        outer++;
    } while ( outer < block->depth );
}

/*
==================
Object_CreateLevelObjects
==================
*/
/* name: inferred from use (Object_InitLevelObjects): builds each level object from its record and starts its task */
void Object_CreateLevelObjects( levelObjectRecord_t *record, s16 count )
{
    levelObject_t *object = g_levelObjects;
    s16            i;
    u_int          x, z;
    TASK          *task;
    mapCell_t     *cell;

    g_levelObjectRecords = record;
    g_levelObjectCount = count;
    for ( i = 0; i < count; i++, record++, object++ ) {
        object->type = record->type;
        object->flags = record->flags;
        object->state = record->state << LEVEL_OBJECT_STATE_SHIFT;
        object->drop[0] = record->drop[0];
        object->drop[1] = record->drop[1];
        object->width = record->width;
        object->depth = record->depth;
        object->commandBase = record->commandBase;
        object->commandCount = record->commandCount;
        object->facing = record->facing;
        object->position.vx = ( record->x << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetX;
        object->position.vz = ( record->z << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetZ;
        object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ );
        object->rotation.vx = object->rotation.vy = object->rotation.vz = 0;
        object->occupant = i;
        object->hits = 0;
        x = object->position.vx + g_levelMap.offsetX;
        z = object->position.vz + g_levelMap.offsetZ;
        cell = MAP_CELL( x, z );
        object->cell = cell;
        cell->marker = object->type;
        cell->occupant = object->occupant;
        switch ( object->facing ) {
        case FACING_NEG_Z:
            object->rotation.vy = ANGLE_180;
            break;
        case FACING_POS_X_NEG_Z:
            object->rotation.vy = ANGLE_225;
            break;
        case FACING_POS_X:
            object->rotation.vy = ANGLE_90;
            break;
        case FACING_POS_X_POS_Z:
            object->rotation.vy = -ANGLE_45;
            break;
        case FACING_POS_Z:
            object->rotation.vy = 0;
            break;
        case FACING_NEG_X_POS_Z:
            object->rotation.vy = ANGLE_45;
            break;
        case FACING_NEG_X:
            object->rotation.vy = -ANGLE_90;
            break;
        case FACING_NEG_X_NEG_Z:
            object->rotation.vy = ANGLE_135;
            break;
        }
        if ( object->type < TALLY_COUNT )
            g_levelMap.kindCounts[object->type]++;
        switch ( object->type ) {
        case LEVEL_OBJECT_EXPLOSIVE:
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_LevelObjectDebrisTask );
            object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_EXPLOSIVE];
            break;
        case LEVEL_OBJECT_BLOCK:
        case LEVEL_OBJECT_WALL:
        case LEVEL_OBJECT_BLOCK_SMALL:
            Object_MarkLevelObjectCells( object );
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_BlockObjectTask );
            break;
        case LEVEL_OBJECT_SWITCH:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_SwitchPanelTask );
            if ( g_level >= LEVEL_311_TUNNELS )
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_LATE];
            else
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH];
            task->state[SWITCH_TASK_TIMER] = SWITCH_BLINK_TICKS;
            task->state[SWITCH_TASK_FRAME] = 0;
            object->state = 0;
            break;
        case LEVEL_OBJECT_SWITCH_ALT:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_SwitchPanelTask );
            if ( g_level >= LEVEL_311_TUNNELS )
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_ALT_LATE];
            else
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_ALT];
            task->state[SWITCH_TASK_TIMER] = SWITCH_BLINK_TICKS;
            task->state[SWITCH_TASK_FRAME] = 0;
            object->state = 0;
            break;
        case LEVEL_OBJECT_SWITCH_BATTERY:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_SwitchPanelTask );
            if ( g_level >= LEVEL_311_TUNNELS )
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_LATE];
            else
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_BATTERY];
            task->state[SWITCH_TASK_TIMER] = SWITCH_BLINK_TICKS;
            task->state[SWITCH_TASK_FRAME] = 0;
            object->state = 0;
            break;
        case LEVEL_OBJECT_SWITCH_BATTERY_ALT:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_SwitchPanelTask );
            if ( g_level >= LEVEL_311_TUNNELS )
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT_LATE];
            else
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT];
            task->state[SWITCH_TASK_TIMER] = SWITCH_BLINK_TICKS;
            task->state[SWITCH_TASK_FRAME] = 0;
            object->state = 0;
            break;
        case LEVEL_OBJECT_TARGET:
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_LevelObjectHitTask );
            if ( g_level >= LEVEL_311_TUNNELS )
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_TARGET_LATE];
            else
                object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_TARGET];
            break;
        case LEVEL_OBJECT_TARGET_ALT:
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_LevelObjectHitTask );
            object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_TARGET_ALT];
            break;
        case LEVEL_OBJECT_TILTING:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_LevelObjectSpinTask );
            object->state = 0;
            object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_TALL];
            break;
        case LEVEL_OBJECT_STATIC_FIRST ... LEVEL_OBJECT_STATIC_LAST:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_LevelObjectTask );
            object->model = g_levelObjectModels[object->type - LEVEL_OBJECT_STATIC_FIRST + LEVEL_OBJECT_MODEL_STATIC];
            break;
        default:
            task = TaskCreate( TASK_PRIORITY_OBJECTS, Object_LevelObjectHitTask );
            object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_TARGET];
            break;
        }
        if ( object->model == 0 )
            object->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_TARGET];
        task->state[0] = (unsigned int)object;
    }
}

/*
==================
Object_InitLevelObjects
==================
*/
/* name: inferred from its body, which creates the level's pickups */
void Object_InitLevelObjects( void )
{
    s16 count = g_levelMap.pickupCount;
    if ( count != 0 ) {
        Object_CreateLevelObjects( g_levelMap.pickups, count );
    }
}

/*
==================
Object_DrawLevelObject
==================
*/
void Object_DrawLevelObject( levelObject_t *object )
{
    VECTOR scale;
    if ( ( object->cell->mask & g_visibleCellMask ) == 0 )
        return;
    if ( object->type == LEVEL_OBJECT_TARGET && g_level >= LEVEL_311_TUNNELS )
        g_modelUvs = g_panelUvs;
    else
        g_modelUvs = g_pickupUvs;
    LoadAverageShort12( &object->position, &g_viewPosition, ONE, -ONE, &g_relativePosition );
    PushMatrix();
    ApplyMatrix( &g_viewMatrix, &g_relativePosition, &g_translation );
    if ( (u_long)( g_translation.vz - 1 ) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE ) {
        RotMatrix( &object->rotation, &g_matrixWork );
        MulMatrix2( &g_viewMatrix, &g_matrixWork );
        if ( object->type != LEVEL_OBJECT_EXPLOSIVE ) {
            scale.vx = LEVEL_OBJECT_SCALE;
            scale.vy = LEVEL_OBJECT_SCALE;
            scale.vz = LEVEL_OBJECT_SCALE;
            ScaleMatrix( &g_matrixWork, &scale );
        }
        SetRotMatrix( &g_matrixWork );
        TransMatrix( &g_matrixWork, &g_translation );
        SetTransMatrix( &g_matrixWork );
        if ( g_translation.vz < MODEL_NEAR_DISTANCE )
            Model_DrawNearInCell( object->model, object->cell );
        else if ( g_translation.vz < MODEL_FAR_DISTANCE )
            Model_DrawMidInCell( object->model, object->cell );
        else
            Model_DrawFarInCell( object->model, object->cell );
    }
    PopMatrix();
    g_modelUvs = g_spriteUvs;
}

/*
==================
Object_DrawLevelObjectTall
==================
*/
void Object_DrawLevelObjectTall( levelObject_t *object )
{
    VECTOR scale;
    if ( ( object->cell->mask & g_visibleCellMask ) == 0 )
        return;
    g_modelUvs = g_panelUvs;
    LoadAverageShort12( &object->position, &g_viewPosition, ONE, -ONE, &g_relativePosition );
    PushMatrix();
    ApplyMatrix( &g_viewMatrix, &g_relativePosition, &g_translation );
    if ( (u_long)( g_translation.vz - 1 ) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE ) {
        RotMatrix( &object->rotation, &g_matrixWork );
        MulMatrix2( &g_viewMatrix, &g_matrixWork );
        scale.vx = LEVEL_OBJECT_TALL_SCALE;
        scale.vy = LEVEL_OBJECT_TALL_SCALE_Y;
        scale.vz = LEVEL_OBJECT_TALL_SCALE;
        ScaleMatrix( &g_matrixWork, &scale );
        SetRotMatrix( &g_matrixWork );
        TransMatrix( &g_matrixWork, &g_translation );
        SetTransMatrix( &g_matrixWork );
        if ( g_translation.vz < MODEL_CLOSE_DISTANCE )
            Model_DrawNearInCell( object->model, object->cell );
        else if ( g_translation.vz < MODEL_FAR_DISTANCE )
            Model_DrawMidInCell( object->model, object->cell );
        else
            Model_DrawFarInCell( object->model, object->cell );
    }
    PopMatrix();
    g_modelUvs = g_spriteUvs;
}

/*
==================
Object_DrawLevelObjectUnscaled
==================
*/
void Object_DrawLevelObjectUnscaled( levelObject_t *object )
{
    if ( ( object->cell->mask & g_visibleCellMask ) == 0 )
        return;
    LoadAverageShort12( &object->position, &g_viewPosition, ONE, -ONE, &g_relativePosition );
    PushMatrix();
    ApplyMatrix( &g_viewMatrix, &g_relativePosition, &g_translation );
    if ( (u_long)( g_translation.vz - 1 ) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE ) {
        RotMatrix( &object->rotation, &g_matrixWork );
        MulMatrix2( &g_viewMatrix, &g_matrixWork );
        SetRotMatrix( &g_matrixWork );
        TransMatrix( &g_matrixWork, &g_translation );
        SetTransMatrix( &g_matrixWork );
        if ( g_translation.vz < MODEL_CLOSE_DISTANCE )
            Model_DrawNearInCell( object->model, object->cell );
        else if ( g_translation.vz < MODEL_FAR_DISTANCE )
            Model_DrawMidInCell( object->model, object->cell );
        else
            Model_DrawFarInCell( object->model, object->cell );
    }
    PopMatrix();
}

/*
==================
Object_LevelObjectTask
==================
*/
/* RULE-EXCEPTION: R17-dead-local, VECTOR scale: type, name and 16-byte size from Object_DrawLevelObjectTall,
 * whose body this is minus the ScaleMatrix step and which keeps VECTOR scale at the same 0x18-0x27(sp)
 * slot; retail never touches the slot. Ruled out: an inlined callee (the call set has no Object_Draw* or
 * ScaleMatrix; a shared inline panel-draw helper with a tall flag gives a 0x38 frame and one saved
 * register fewer), an aggregate temporary (no struct copy or return), SDK macro locals (the libgte steps
 * are out-of-line calls), and dead scale stores (none in retail). */
void Object_LevelObjectTask( TASK *task )
{
    levelObject_t *object = (levelObject_t *)task->state[0];
    VECTOR         scale;
    object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ );
    if ( object->cell->mask & g_visibleCellMask ) {
        g_modelUvs = g_panelUvs;
        LoadAverageShort12( &object->position, &g_viewPosition, ONE, -ONE, &g_relativePosition );
        PushMatrix();
        ApplyMatrix( &g_viewMatrix, &g_relativePosition, &g_translation );
        if ( (u_long)( g_translation.vz - 1 ) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE ) {
            RotMatrix( &object->rotation, &g_matrixWork );
            MulMatrix2( &g_viewMatrix, &g_matrixWork );
            SetRotMatrix( &g_matrixWork );
            TransMatrix( &g_matrixWork, &g_translation );
            SetTransMatrix( &g_matrixWork );
            if ( g_translation.vz < MODEL_NEAR_DISTANCE )
                Model_DrawNearInCell( object->model, object->cell );
            else if ( g_translation.vz < MODEL_FAR_DISTANCE )
                Model_DrawMidInCell( object->model, object->cell );
            else
                Model_DrawFarInCell( object->model, object->cell );
        }
        PopMatrix();
        g_modelUvs = g_spriteUvs;
    }
    g_nextTask = task->Next;
}

/*
==================
Object_LevelObjectSpinTask
==================
*/
void Object_LevelObjectSpinTask( TASK *task )
{
    levelObject_t *object = (levelObject_t *)task->state[0];
    object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ );
    Object_DrawLevelObjectUnscaled( object );
    if ( object->state && object->rotation.vz >= LEVEL_OBJECT_TILT_LIMIT )
        object->rotation.vz -= LEVEL_OBJECT_TILT_STEP;
    g_nextTask = task->Next;
}

/*
==================
Object_LevelObjectHitTask
==================
*/
void Object_LevelObjectHitTask( TASK *task )
{
    levelObject_t *object = (levelObject_t *)task->state[0];
    object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ );
    if ( object->flags & LEVEL_OBJECT_ARMED ) {
        s16 remaining = object->hits;
        remaining--;
        if ( remaining == 0 ) {
            mapCell_t *cell = object->cell;
            g_player.tallies[object->type]++;
            cell->marker = 0;
            if ( object->drop[0] != LEVEL_OBJECT_NO_DROP ) {
                if ( ( object->flags & LEVEL_OBJECT_DROPS_ACTOR ) == 0 )
                    Object_LevelObjectDropItem( object, object->drop[0] );
                else
                    Actor_SpawnFromObject( object, object->drop[0] );
            }
            if ( object->drop[1] != LEVEL_OBJECT_NO_DROP )
                Object_LevelObjectDropItem( object, object->drop[1] );
            Particle_SpawnExplosion( object->position.vx, object->position.vy, object->position.vz );
            Debris_WalkPanelList( object );
            TaskDie( task );
            return;
        }
        object->hits = remaining;
    }
    Object_DrawLevelObject( object );
    g_nextTask = task->Next;
}

/*
==================
Object_LevelObjectDebrisTask
==================
*/
void Object_LevelObjectDebrisTask( TASK *task )
{
    levelObject_t *object = (levelObject_t *)task->state[0];
    object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ );
    if ( object->flags & LEVEL_OBJECT_ARMED ) {
        s16 remaining = object->hits;
        remaining--;
        if ( remaining == 0 ) {
            mapCell_t *cell = object->cell;
            g_player.tallies[object->type]++;
            cell->marker = 0;
            Particle_SpawnBigExplosion( object->position.vx, object->position.vy - EXPLOSIVE_BLAST_DROP,
                                        object->position.vz );
            Debris_WalkPanelList( object );
            Object_ExplosionDamage( &object->position, object->cell );
            TaskDie( task );
            return;
        }
        object->hits = remaining;
    }
    Object_DrawLevelObject( object );
    g_nextTask = task->Next;
}

/*
==================
Object_ExplodeBlockObject

Tallies a destroyed block object and bursts explosions on the side facing the player.
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Object_ExplodeBlockObject( levelObject_t *object )
{
    SVECTOR position;
    short   count;
    s16     i;
    g_player.tallies[object->type]++;
    position.vx = object->position.vx + ( object->width - 1 ) * MAP_CELL_HALF;
    position.vy = object->position.vy + BLOCK_BLAST_HEIGHT;
    position.vz = object->position.vz + ( object->depth - 1 ) * MAP_CELL_HALF;
    count = BLOCK_BLAST_COUNT;
    if ( object->type == LEVEL_OBJECT_BLOCK_SMALL )
        count = 1;
    switch ( object->facing ) {
    case FACING_NEG_Z:
    case FACING_POS_Z:
        if ( position.vz < g_player.position.vz ) {
            position.vz += MAP_CELL_SIZE;
            for ( i = 0; i < count; i++ ) {
                s16 dx = ( Sys_Random() & MAP_CELL_MASK ) - MAP_CELL_HALF;
                s16 dy = ( Sys_Random() & ( BLOCK_BLAST_SPREAD_Y - 1 ) ) - BLOCK_BLAST_SPREAD_Y / 2;
                Particle_SpawnExplosion( position.vx + dx, position.vy + dy, position.vz );
            }
        } else {
            position.vz -= MAP_CELL_SIZE;
            for ( i = 0; i < count; i++ ) {
                s16 dx = ( Sys_Random() & MAP_CELL_MASK ) - MAP_CELL_HALF;
                s16 dy = ( Sys_Random() & ( BLOCK_BLAST_SPREAD_Y - 1 ) ) - BLOCK_BLAST_SPREAD_Y / 2;
                Particle_SpawnExplosion( position.vx + dx, position.vy + dy, position.vz );
            }
        }
        break;
    case FACING_POS_X:
    case FACING_NEG_X:
        if ( position.vx < g_player.position.vx ) {
            position.vx += MAP_CELL_SIZE;
            for ( i = 0; i < count; i++ ) {
                s16 dz = ( Sys_Random() & MAP_CELL_MASK ) - MAP_CELL_HALF;
                s16 dy = ( Sys_Random() & ( BLOCK_BLAST_SPREAD_Y - 1 ) ) - BLOCK_BLAST_SPREAD_Y / 2;
                Particle_SpawnExplosion( position.vx, position.vy + dy, position.vz + dz );
            }
        } else {
            position.vx -= MAP_CELL_SIZE;
            for ( i = 0; i < count; i++ ) {
                s16 dz = ( Sys_Random() & MAP_CELL_MASK ) - MAP_CELL_HALF;
                s16 dy = ( Sys_Random() & ( BLOCK_BLAST_SPREAD_Y - 1 ) ) - BLOCK_BLAST_SPREAD_Y / 2;
                Particle_SpawnExplosion( position.vx, position.vy + dy, position.vz + dz );
            }
        }
        break;
    default:
        Particle_SpawnExplosion( position.vx, position.vy, position.vz );
        break;
    }
}


/*
==================
Object_BlockObjectTask

Task of a destructible block object (types 0x15, 0x1c, 0x1d): when its hits run out it
explodes (0x1c instead runs Debris_ShatterWall with a sound), clears its cells, raises its
command states and drops its items.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Object_BlockObjectTask( TASK *task )
{
    levelObject_t *object = (levelObject_t *)task->state[0];
    s16            i;
    if ( object->flags & LEVEL_OBJECT_ARMED ) {
        s16 remaining = object->hits;
        remaining--;
        if ( remaining == 0 ) {
            if ( object->type != LEVEL_OBJECT_WALL ) {
                Object_ExplodeBlockObject( object );
                Object_ClearLevelObjectCells( object );
                for ( i = 0; i < object->commandCount; i++ )
                    Command_AddState( 1, object->commandBase + i );
                if ( object->drop[0] != LEVEL_OBJECT_NO_DROP ) {
                    if ( ( object->flags & LEVEL_OBJECT_DROPS_ACTOR ) == 0 )
                        Object_LevelObjectDropItem( object, object->drop[0] );
                    else
                        Actor_SpawnFromObject( object, object->drop[0] );
                }
                if ( object->drop[1] != LEVEL_OBJECT_NO_DROP )
                    Object_LevelObjectDropItem( object, object->drop[1] );
            } else {
                Debris_ShatterWall( object );
                Snd_PlayAt( SOUND_WALL_SHATTER, &object->position, SOUND_AT_FULL );
                Object_ClearLevelObjectCells( object );
                for ( i = 0; i < object->commandCount; i++ )
                    Command_AddState( 1, object->commandBase + i );
            }
            TaskDie( task );
            return;
        }
        object->hits = remaining;
    }
    g_nextTask = task->Next;
}

/* Picks frame of a switch panel's model set by its type (the _LATE sets from LEVEL_311_TUNNELS on). */
#define LEVEL_OBJECT_SET_FRAME( object, frame )                                                              \
    if ( ( object )->type == LEVEL_OBJECT_SWITCH ) {                                                         \
        if ( g_level >= LEVEL_311_TUNNELS )                                                                  \
            ( object )->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_LATE + ( frame )];             \
        else                                                                                                 \
            ( object )->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH + ( frame )];                  \
    } else if ( ( object )->type == LEVEL_OBJECT_SWITCH_ALT ) {                                              \
        if ( g_level >= LEVEL_311_TUNNELS )                                                                  \
            ( object )->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_ALT_LATE + ( frame )];         \
        else                                                                                                 \
            ( object )->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_ALT + ( frame )];              \
    } else if ( ( object )->type == LEVEL_OBJECT_SWITCH_BATTERY ) {                                          \
        if ( g_level >= LEVEL_311_TUNNELS )                                                                  \
            ( object )->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_LATE + ( frame )];     \
        else                                                                                                 \
            ( object )->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_BATTERY + ( frame )];          \
    } else {                                                                                                 \
        if ( g_level >= LEVEL_311_TUNNELS )                                                                  \
            ( object )->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT_LATE + ( frame )]; \
        else                                                                                                 \
            ( object )->model = g_levelObjectModels[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT + ( frame )];      \
    }

/*
==================
Object_SwitchPanelActiveTask

Task of an activated switch panel: blinks frames 2/3 while state[2] counts down, then
holds frame 3; when the object's state returns to 2 it goes back to Object_SwitchPanelTask.
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Object_SwitchPanelActiveTask( TASK *task )
{
    levelObject_t *object = (levelObject_t *)task->state[SWITCH_TASK_OBJECT];
    object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ );
    Object_DrawLevelObjectTall( object );
    if ( task->state[SWITCH_TASK_BLINKS] != 0 ) {
        if ( task->state[SWITCH_TASK_TIMER] == 0 ) {
            if ( --task->state[SWITCH_TASK_BLINKS] == 0 ) {
                LEVEL_OBJECT_SET_FRAME( object, SWITCH_FRAME_HELD );
                task->state[SWITCH_TASK_TIMER] = SWITCH_HOLD_TICKS;
            } else {
                s16 frame;
                task->state[SWITCH_TASK_TIMER] = SWITCH_ACTIVE_BLINK_TICKS;
                frame = task->state[SWITCH_TASK_FRAME] ^ 1;
                task->state[SWITCH_TASK_FRAME] = frame;
                LEVEL_OBJECT_SET_FRAME( object, SWITCH_FRAME_ACTIVE + frame );
            }
        } else {
            task->state[SWITCH_TASK_TIMER]--;
        }
    } else if ( task->state[SWITCH_TASK_TIMER] != 0 ) {
        if ( --task->state[SWITCH_TASK_TIMER] == 0 && object->state == SWITCH_STATE_REARMS ) {
            object->state = 0;
            task->state[SWITCH_TASK_TIMER] = SWITCH_BLINK_TICKS;
            task->state[SWITCH_TASK_FRAME] = 0;
            task->callback = Object_SwitchPanelTask;
            LEVEL_OBJECT_SET_FRAME( object, SWITCH_FRAME_HELD );
        }
    }
    g_nextTask = task->Next;
}

/*
==================
Object_SwitchPanelTask

Task of an idle switch panel (types 0x16, 0x18, 0x1a, 0x1b): blinks frames 0/1; once
activated (state) it shows frame 2, sounds and hands over to Object_SwitchPanelActiveTask.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Object_SwitchPanelTask( TASK *task )
{
    levelObject_t *object = (levelObject_t *)task->state[SWITCH_TASK_OBJECT];
    object->position.vy = FloorHeight( object->position.vx + g_levelMap.offsetX, object->position.vz + g_levelMap.offsetZ );
    Object_DrawLevelObjectTall( object );
    if ( object->state == 0 ) {
        if ( task->state[SWITCH_TASK_TIMER] == 0 ) {
            s16 frame;
            task->state[SWITCH_TASK_TIMER] = SWITCH_BLINK_TICKS;
            frame = task->state[SWITCH_TASK_FRAME] ^ 1;
            task->state[SWITCH_TASK_FRAME] = frame;
            LEVEL_OBJECT_SET_FRAME( object, SWITCH_FRAME_IDLE + frame );
        } else {
            task->state[SWITCH_TASK_TIMER]--;
        }
    } else {
        task->state[SWITCH_TASK_FRAME] = 0;
        task->state[SWITCH_TASK_TIMER] = SWITCH_ACTIVE_BLINK_TICKS;
        task->state[SWITCH_TASK_BLINKS] = SWITCH_ACTIVE_BLINKS;
        LEVEL_OBJECT_SET_FRAME( object, SWITCH_FRAME_ACTIVE );
        task->callback = Object_SwitchPanelActiveTask;
        Snd_Play( SOUND_SWITCH, -1, SPU_VOLUME_MAX );
    }
    g_nextTask = task->Next;
}
