/* lift.c -- lifts built from level lift records: models, records, triggers and tasks */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <MODEL.H>
#include <VIEW.H>
#include <CAMERA.H>
#include <TASK.H>
#include <HEAP.H>
#include <AUDIO.H>
#include <TEXT.H>
#include <HUD.H>
#include <TYPES.H>

/* Module statics (.sbss 0x8008d1e8..0x8008d1f8), in their retail order. */
static lift_t       *g_lifts;
static liftRecord_t *g_liftRecords;
static s16           g_liftRecordCount;
extern void Lift_Create( void );
extern void Lift_Draw( lift_t *lift );
void Lift_PrepareRecords( liftRecord_t *entry, s16 count );
void Lift_SetHeight( lift_t *lift, u8 height );
void Lift_TaskDelay( TASK *task );
void Lift_TaskLower( TASK *task );
void Lift_TaskRaise( TASK *task );
void Lift_TaskWait( TASK *task );

char *s_liftActivated = "Lift activated";

/*
==================
Lift_FixupModelFaces

The face kind is the halfword over flags and selector until this splits it.
==================
*/
/* name: inferred from its body (the same function as Door_FixupModelFaces): turns a
 * lift model's vertex indices into pointers and each face kind into flags and a
 * dispatch selector */
void Lift_FixupModelFaces( modelRow_t *face, SVECTOR *vertices, u8 flags )
{
    int   index;
    short last;
    u8    value;

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
        value = flags;
        if ( last )
            value |= ROW_FLAG_LAST;
        face->row.flags = value;
        face++;
    } while ( !( value & ROW_FLAG_LAST ) );
}

/*
==================
Lift_PrepareModels
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
/* MATCH-PENDING: R4, header fields walked through an int cursor stepped by member sizes and read via *p; block->rowCount/block->rows change the function size */
void Lift_PrepareModels( void )
{
    s16 i;
    for ( i = 0; i < g_liftModelCount; i++ ) {
        int        *p = g_liftModels[i];
        modelRow_t *rows;
        int         count;
        p += ( sizeof( ( (modelBlock_t *)p )->tag ) + sizeof( ( (modelBlock_t *)p )->unknown_04 ) ) / sizeof( *p ); /* to rowCount */
        count = *p;
        p += ( sizeof( ( (modelBlock_t *)p )->rowCount ) + sizeof( ( (modelBlock_t *)p )->vertexCount ) ) / sizeof( *p ); /* to rows */
        rows = (modelRow_t *)p;
        Lift_FixupModelFaces( rows, (SVECTOR *)( rows + count ), MODEL_PALETTE );
        g_liftModels[i] = rows;
    }
}

/*
==================
Lift_Alloc
==================
*/
/* name: inferred from its body, which allocates the lift table */
void Lift_Alloc( void )
{
    g_lifts = Heap_Alloc( LIFT_MAX * sizeof( lift_t ) );
}

/*
==================
Lift_PrepareRecords
==================
*/
/* name: inferred from its body, which raises each lift record by its map cell's value */
void Lift_PrepareRecords( liftRecord_t *entry, s16 count )
{
    g_liftRecordCount = count;
    g_liftRecords = entry;
    while ( count-- ) {
        int        x = entry->x;
        int        z = entry->z;
        mapCell_t *cell = g_levelMap.cells + g_levelMap.width * z + x;
        entry->level = cell->value;
        entry->base += cell->value;
        entry++;
    }
}

/*
==================
Lift_InitRecords
==================
*/
void Lift_InitRecords( void )
{
    Lift_PrepareRecords( g_levelMap.liftRecords, g_levelMap.liftCount );
}

/*
==================
Lift_Trigger
==================
*/
int Lift_Trigger( int amount, u8 index )
{
    lift_t *lift = g_lifts + index;
    if ( lift->flags & ( LIFT_RAISING | LIFT_LOWERING ) )
        return 0;
    if ( lift->count >= lift->wait )
        return 0;
    lift->count = (unsigned int)amount + lift->count;
    return -1;
}

/*
==================
Lift_TriggerLast
==================
*/
int Lift_TriggerLast( int amount, u8 index )
{
    lift_t *lift = g_lifts + index;
    if ( lift->flags & ( LIFT_RAISING | LIFT_LOWERING ) )
        return 0;
    if ( lift->count < lift->wait ) {
        if ( lift->count >= lift->wait - 1 ) {
            lift->count = (unsigned int)amount + lift->count;
            return -1;
        }
    }
    return 0;
}

/*
==================
Lift_SetHeight
==================
*/
void Lift_SetHeight( lift_t *lift, u8 height )
{
    mapCell_t *row = lift->cells;
    short      z;

    for ( z = 0; z < lift->depth; ++z ) {
        mapCell_t *cell = row;
        short      x;

        for ( x = 0; x < lift->width; ++x ) {
            cell->value = height;
            cell++;
        }
        row += g_levelMap.width;
    }
}

/*
==================
Lift_TaskDelay
==================
*/
void Lift_TaskDelay( TASK *task )
{
    lift_t *lift = (lift_t *)task->state[0];
    --lift->delay;
    if ( lift->delay == 0 ) {
        if ( lift->flags & LIFT_UP ) {
            lift->flags = ( lift->flags & ~LIFT_UP ) | LIFT_LOWERING;
            task->callback = Lift_TaskLower;
        } else {
            lift->flags = ( lift->flags & ~LIFT_DOWN ) | LIFT_RAISING;
            task->callback = Lift_TaskRaise;
        }
        Snd_PlayAt( SOUND_LIFT_MOVE, &lift->position, SOUND_AT_SOFT );
    }
    Lift_Draw( lift );
    g_nextTask = task->Next;
}

/*
==================
Lift_TaskLower
==================
*/
void Lift_TaskLower( TASK *task )
{
    lift_t *lift = (lift_t *)task->state[0];
    short   step;
    short   height;
    step = 1;
    if ( lift->flags & LIFT_FAST )
        step = LIFT_FAST_STEP;
    height = lift->cells->value - step;
    Lift_SetHeight( lift, (u8)height );
    lift->position.vy -= step << MAP_HEIGHT_SHIFT;
    if ( height == lift->low ) {
        lift->flags = ( lift->flags & ~LIFT_LOWERING ) | LIFT_DOWN;
        Snd_PlayAt( SOUND_LIFT_STOP, &lift->position, SOUND_AT_SOFT );
        if ( !( lift->flags & LIFT_CYCLES ) ) {
            if ( lift->hold == 0 ) {
                lift->count = 0;
                task->callback = Lift_TaskWait;
            } else if ( !( lift->flags & LIFT_RESTS_UP ) ) {
                lift->count = 0;
                task->callback = Lift_TaskWait;
            } else {
                lift->delay = lift->hold;
                task->callback = Lift_TaskDelay;
            }
        } else {
            lift->delay = lift->hold;
            task->callback = Lift_TaskDelay;
        }
    }
    Lift_Draw( lift );
    g_nextTask = task->Next;
}

/*
==================
Lift_TaskRaise
==================
*/
void Lift_TaskRaise( TASK *task )
{
    lift_t *lift = (lift_t *)task->state[0];
    short   step;
    short   height;
    step = 1;
    if ( lift->flags & LIFT_FAST )
        step = LIFT_FAST_STEP;
    height = lift->cells->value + step;
    Lift_SetHeight( lift, (u8)height );
    lift->position.vy += step << MAP_HEIGHT_SHIFT;
    if ( height == lift->high ) {
        lift->flags = ( lift->flags & ~LIFT_RAISING ) | LIFT_UP;
        Snd_PlayAt( SOUND_LIFT_STOP, &lift->position, SOUND_AT_SOFT );
        if ( !( lift->flags & LIFT_CYCLES ) ) {
            if ( lift->hold == 0 ) {
                lift->count = 0;
                task->callback = Lift_TaskWait;
            } else if ( lift->flags & LIFT_RESTS_UP ) {
                lift->count = 0;
                task->callback = Lift_TaskWait;
            } else {
                lift->delay = lift->hold;
                task->callback = Lift_TaskDelay;
            }
        } else {
            lift->delay = lift->hold;
            task->callback = Lift_TaskDelay;
        }
    }
    Lift_Draw( lift );
    g_nextTask = task->Next;
}

/*
==================
Lift_TaskWait
==================
*/
void Lift_TaskWait( TASK *task )
{
    lift_t *lift = (lift_t *)task->state[0];
    if ( lift->count >= lift->wait ) {
        if ( !( lift->flags & LIFT_COUNTED ) ) {
            lift->flags |= LIFT_COUNTED;
            g_player.tallies[TALLY_LIFTS]++;
        }
        Hud_ShowMessage( s_liftActivated );
        if ( lift->flags & LIFT_UP ) {
            lift->flags = ( lift->flags & ~LIFT_UP ) | LIFT_LOWERING;
            Snd_PlayAt( SOUND_LIFT_MOVE, &lift->position, SOUND_AT_FULL );
            task->callback = Lift_TaskLower;
        } else {
            lift->flags = ( lift->flags & ~LIFT_DOWN ) | LIFT_RAISING;
            Snd_PlayAt( SOUND_LIFT_MOVE, &lift->position, SOUND_AT_FULL );
            task->callback = Lift_TaskRaise;
        }
    }
    Lift_Draw( lift );
    g_nextTask = task->Next;
}

/*
==================
Lift_MeasureModel

Centres the lift on its cells and offsets it by half its model's height range.
codegen: the shifts are second assignments to width and depth; a once-set
temporary would be scheduled next to its add (sched1's birthing boost).
==================
*/
/* name: inferred from its body */
void Lift_MeasureModel( lift_t *lift )
{
    short          high;
    short          low;
    dispatchRow_t *face;
    u_int          width;
    u_int          depth;

    lift->offset = 0;
    low = LIFT_FACE_HEIGHT_LIMIT;
    high = -LIFT_FACE_HEIGHT_LIMIT;
    width = lift->width;
    depth = lift->depth;
    width <<= LIFT_HALF_CELL_SHIFT;
    depth <<= LIFT_HALF_CELL_SHIFT;
    face = lift->faces;
    lift->centreX = lift->position.vx - MAP_CELL_HALF + width;
    lift->centreZ = lift->position.vz - MAP_CELL_HALF + depth;
    if ( face != 0 ) {
        do {
            if ( face->vertices[QUAD_VERTEX_TOP_LEFT]->vy < low )
                low = face->vertices[QUAD_VERTEX_TOP_LEFT]->vy;
            if ( high < face->vertices[QUAD_VERTEX_TOP_LEFT]->vy )
                high = face->vertices[QUAD_VERTEX_TOP_LEFT]->vy;
            if ( face->vertices[QUAD_VERTEX_TOP_RIGHT]->vy < low )
                low = face->vertices[QUAD_VERTEX_TOP_RIGHT]->vy;
            if ( high < face->vertices[QUAD_VERTEX_TOP_RIGHT]->vy )
                high = face->vertices[QUAD_VERTEX_TOP_RIGHT]->vy;
            if ( face->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vy < low )
                low = face->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vy;
            if ( high < face->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vy )
                high = face->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vy;
            if ( face->selector == ROW_SELECT_QUAD || face->selector == ROW_SELECT_QUAD_ALT ) {
                if ( face->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vy < low )
                    low = face->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vy;
                if ( high < face->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vy )
                    high = face->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vy;
            }
        } while ( !( ( face++ )->flags & ROW_FLAG_LAST ) );
        lift->offset = -( ( high - low ) >> 1 );
    }
}

/*
==================
Lift_Create
==================
*/
/* name: inferred from its body, which builds each lift from its record and starts its wait task */
void Lift_Create( void )
{
    lift_t       *lift = g_lifts;
    liftRecord_t *record = g_liftRecords;
    short         i;
    TASK         *task;

    g_levelMap.kindCounts[TALLY_LIFTS] = g_liftRecordCount;
    for ( i = 0; i < g_liftRecordCount; i++ ) {
        u_int x = record->x;
        u_int z = record->z;

        lift->cells = g_levelMap.cells + g_levelMap.width * z + x;
        lift->width = record->width;
        lift->depth = record->depth;
        lift->low = record->level;
        lift->high = record->base;
        lift->unknown_14 = record->unknown_06;
        lift->flags = record->flags;
        lift->delay = record->delay;
        lift->hold = record->hold;
        lift->wait = record->wait;
        lift->count = record->count;
        lift->model = record->model;
        lift->position.vx = ( x << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetX;
        lift->position.vz = ( z << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetZ;
        if ( lift->delay )
            lift->flags |= LIFT_CYCLES;
        if ( lift->flags & LIFT_UP ) {
            lift->flags |= LIFT_RESTS_UP;
            lift->position.vy = ( record->base << MAP_HEIGHT_SHIFT ) + MAP_HEIGHT_HALF_STEP;
            Lift_SetHeight( lift, record->base );
        } else {
            lift->position.vy = ( record->level << MAP_HEIGHT_SHIFT ) + MAP_HEIGHT_HALF_STEP;
            Lift_SetHeight( lift, record->level );
        }
        lift->faces = g_liftModels[lift->model];
        Lift_MeasureModel( lift );
        task = TaskCreate( TASK_PRIORITY_DOORS, Lift_TaskWait );
        task->state[0] = (unsigned int)lift;
        lift++;
        record++;
    }
}

/*
==================
Lift_Spawn
==================
*/
/* name: inferred from its body, which creates the lifts when the level has any */
void Lift_Spawn( void )
{
    if ( g_levelMap.liftCount )
        Lift_Create();
}
