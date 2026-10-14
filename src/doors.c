/* doors.c -- doors: their level records (a 0x20-byte Door each, in a heap block), drawing, the
 * open/close tasks and power-up */
/* Retail .text from at least 0x800223a0 to 0x800235ac. One translation unit: its statics (.sbss
 * 0x8008d100..0x8008d118) are read by Door_TaskOpening (0x80022b18), Door_Power/Door_PowerFinal,
 * Door_PrepareModels and Door_Alloc/Door_Init (0x80023580), Door_Build and Door_LoadDefs; its
 * rotations (.sdata 0x8008cfac) and strings (.rdata 0x800119ec) follow the previous modules' data.
 * Name invented. */
#include <sys/types.h>
#include <LIBGTE.H>
#include <ABS.H>
#include <LEVEL.H>
#include <MODEL.H>
#include <CAMERA.H>
#include <TASK.H>
#include <HEAP.H>
#include <AUDIO.H>
#include <GAME.H>
#include <VIEW.H>
#include <TEXT.H>
#include <HUD.H>
#include <TYPES.H>

/* Module statics (.sbss 0x8008d100..0x8008d118), in their retail order. The door table
 * (Door_Alloc allocates it, Door_Build fills it), the level's door definitions and their
 * count (Door_LoadDefs), and the ticks a door takes to open (Door_TaskOpening starts the door when
 * its counter reaches it), also the ceiling offset added to the doorway cells
 * (Door_Build): 0x1e on the first two levels, else 0x2b (Door_PrepareModels).
 * names: runtime evidence, analysis/runtime/globals.json */
static door_t       *g_doors;
static doorRecord_t *g_doorDefs;
static short         g_doorCount;
static short         g_doorOpenTicks;


/* Messages of Door_Power and Door_PowerFinal. Each array's extent includes its alignment padding. */
#define DOOR_MESSAGE_SIZE 16 /* the text and its padding to a word boundary. name: invented */
const char s_doorActivated[DOOR_MESSAGE_SIZE] = "Door activated";
const char s_doorPoweredUp[DOOR_MESSAGE_SIZE] = "Door powered up";

/* Door rotations by facing (0, 2, 4, 6); constant, never written. Four
 * objects, not one array: retail keeps them in .sdata, where -G8 admits only
 * objects of eight bytes or less.
 * names: invented from analysis/runtime/globals.json's g_door_rotations */
static SVECTOR g_doorRotation0 = { 0, ONE / 2, 0, 0 };
static SVECTOR g_doorRotation2 = { 0, ONE / 4, 0, 0 };
static SVECTOR g_doorRotation4 = { 0, 0, 0, 0 };
static SVECTOR g_doorRotation6 = { 0, -ONE / 4, 0, 0 };

/* Functions of this module referenced before their definitions. */
void Door_TaskOpen( TASK *node );
void Door_TaskClosed( TASK *node );
void Door_TaskClosing( TASK *node );
void Door_TaskOpening( TASK *node );

/*
==================
Door_Blocked
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
/* MATCH-PENDING: R4, neighbour cells reached by byte-stride casts; every typed spelling (a - w/a + w, a[-w]/a[w], &a[w], int/short/u_int/u_short w, negated w, an inline neighbour helper, an inline marker accessor) emits addu (w<<4, a) or changes size where retail adds (a, stride): retail added an integer byte stride */
int Door_Blocked( door_t *door )
{
    mapCell_t *a = door->cellA;
    mapCell_t *b = door->cellB;
    int        stride;

    if ( a->marker || b->marker )
        return -1;
    if ( door->flags & DOOR_ALONG_Z ) {
        if ( a[-1].marker || a[1].marker || b[-1].marker || b[1].marker )
            return -1;
    } else {
        stride = g_levelMap.width * sizeof( mapCell_t );
        if ( ( (mapCell_t *)( (u8 *)a - stride ) )->marker || ( (mapCell_t *)( (u8 *)a + stride ) )->marker
             || ( (mapCell_t *)( (u8 *)b - stride ) )->marker || ( (mapCell_t *)( (u8 *)b + stride ) )->marker )
            return -1;
    }
    return 0;
}

/*
==================
Door_LoadDefs
==================
*/
/* name: inferred from its body: keeps the level's door definitions and copies each
 * door's map cell value into it */
void Door_LoadDefs( doorRecord_t *records, short count )
{
    mapCell_t *cell;
    int        z;

    g_doorCount = count;
    g_doorDefs = records;
    while ( count-- > 0U ) {
        z = records->z;
        cell = g_levelMap.cells + g_levelMap.width * z;
        cell += records->x;
        records->value = cell->value;
        records++;
    }
}

/*
==================
Door_LoadLevelDoors
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_LoadLevelDoors( void )
{
    if ( g_levelMap.doorRecordCount )
        Door_LoadDefs( g_levelMap.doorRecords, g_levelMap.doorRecordCount );
}

/*
==================
Door_Draw
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_Draw( door_t *door )
{
    SVECTOR       *rotation;
    dispatchRow_t *model;

    if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_DEBUG ) )
        return;
    if ( ( door->cellA->mask & g_visibleCellMask ) == 0 )
        return;
    model = (dispatchRow_t *)g_doorModels[door->model];
    if ( model == 0 )
        return;
    switch ( door->facing ) {
    case FACING_POS_X:
        rotation = &g_doorRotation2;
        break;
    case FACING_POS_Z:
        rotation = &g_doorRotation4;
        break;
    case FACING_NEG_X:
        rotation = &g_doorRotation6;
        break;
    case FACING_NEG_Z:
    default:
        rotation = &g_doorRotation0;
        break;
    }
    LoadAverageShort12( &door->position, &g_viewPosition, ONE, -ONE, &g_relativePosition );
    PushMatrix();
    ApplyMatrix( &g_viewMatrix, &g_relativePosition, &g_translation );
    if ( g_translation.vz < DOOR_CULL_DISTANCE && g_translation.vx < DOOR_CULL_DISTANCE ) {
        RotMatrix( rotation, &g_matrixWork );
        MulMatrix2( &g_viewMatrix, &g_matrixWork );
        SetRotMatrix( &g_matrixWork );
        TransMatrix( &g_matrixWork, &g_translation );
        SetTransMatrix( &g_matrixWork );
        if ( g_translation.vz < DOOR_NEAR_DISTANCE && g_translation.vx < DOOR_NEAR_DISTANCE )
            Model_DrawNear( model );
        else if ( g_translation.vz < MODEL_FAR_DISTANCE && g_translation.vx < MODEL_FAR_DISTANCE )
            Model_DrawMid( model );
        else
            Model_DrawFar( model );
    }
    PopMatrix();
}

/*
==================
Door_TaskStatic
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Door_TaskStatic( TASK *node )
{
    Door_Draw( (void *)node->state[DOOR_TASK_DOOR] );
    g_nextTask = node->Next;
}

/*
==================
Door_TaskHold
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_TaskHold( TASK *task )
{
    door_t *door = (door_t *)task->state[DOOR_TASK_DOOR];
    task->state[DOOR_TASK_HOLD]--;
    if ( task->state[DOOR_TASK_HOLD] == 0 ) {
        if ( door->flags & DOOR_OPEN )
            task->callback = Door_TaskOpen;
        else
            task->callback = Door_TaskClosed;
    }
    Door_Draw( door );
    g_nextTask = task->Next;
}

/*
==================
Door_TaskOpen
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_TaskOpen( TASK *node )
{
    door_t *door = (door_t *)node->state[DOOR_TASK_DOOR];
    int     distance;

    if ( door->count >= door->wait ) {
        if ( door->flags & DOOR_ALONG_Z )
            distance = door->position.vx - g_viewPosition.vx;
        else
            distance = door->position.vz - g_viewPosition.vz;
        if ( ABS( distance ) > DOOR_CLOSE_DISTANCE && Door_Blocked( door ) == 0 ) {
            door->flags |= DOOR_CLOSING;
            node->callback = Door_TaskClosing;
            Snd_PlayAt( SOUND_DOOR_MOVE, &door->position, SOUND_AT_SOFT );
        }
    }
    Door_Draw( door );
    g_nextTask = node->Next;
}

/*
==================
Door_TaskClosed
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_TaskClosed( TASK *node )
{
    door_t *door = (door_t *)node->state[DOOR_TASK_DOOR];
    if ( door->count >= door->wait ) {
        g_player.noiseTimer = DOOR_NOISE_TIME;
        door->flags |= DOOR_OPENING;
        node->callback = Door_TaskOpening;
        Snd_PlayAt( SOUND_DOOR_MOVE, &door->position, SOUND_AT_NEAR );
    }
    Door_Draw( door );
    g_nextTask = node->Next;
}

/*
==================
Door_TaskClosing
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_TaskClosing( TASK *node )
{
    door_t *door = (door_t *)node->state[DOOR_TASK_DOOR];
    short   height;

    node->state[DOOR_TASK_STEPS]--;
    if ( Door_Blocked( door ) ) {
        door->flags = ( door->flags & ~DOOR_CLOSING ) | DOOR_OPENING;
        node->callback = Door_TaskOpening;
        Snd_PlayAt( SOUND_DOOR_MOVE, &door->position, SOUND_AT_SOFT );
    } else if ( node->state[DOOR_TASK_STEPS] == 0 ) {
        door->flags &= ~( DOOR_CLOSING | DOOR_OPEN );
        if ( ( node->state[DOOR_TASK_HOLD] = (u16)door->hold ) == 0 ) {
            door->count = 0;
            node->callback = Door_TaskClosed;
        } else if ( !( door->flags & DOOR_RESTS_OPEN ) ) {
            door->count = 0;
            node->callback = Door_TaskClosed;
        } else {
            node->callback = Door_TaskHold;
        }
        Snd_PlayAt( SOUND_DOOR_STOP, &door->position, SOUND_AT_SOFT );
    } else {
        height = door->cellA->ceiling;
        if ( --height < 0 )
            height = 0;
        door->cellA->ceiling = height;
        door->cellB->ceiling = height;
        door->cellC->ceiling = height;
        door->cellD->ceiling = height;
        door->position.vy -= MAP_HEIGHT_STEP;
    }
    Door_Draw( door );
    g_nextTask = node->Next;
}

/*
==================
Door_TaskOpening
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_TaskOpening( TASK *node )
{
    door_t *door = (door_t *)node->state[DOOR_TASK_DOOR];
    u8      height;

    if ( ++node->state[DOOR_TASK_STEPS] == g_doorOpenTicks ) {
        Snd_PlayAt( SOUND_DOOR_STOP, &door->position, SOUND_AT_NEAR );
        door->flags = ( door->flags & ~DOOR_OPENING ) | DOOR_OPEN;
        if ( ( node->state[DOOR_TASK_HOLD] = (u16)door->hold ) == 0 || ( door->flags & DOOR_RESTS_OPEN ) ) {
            door->count = 0;
            node->callback = Door_TaskOpen;
        } else {
            node->callback = Door_TaskHold;
        }
    } else {
        height = ++door->cellA->ceiling;
        door->cellB->ceiling = height;
        door->cellC->ceiling = height;
        door->cellD->ceiling = height;
        door->position.vy += MAP_HEIGHT_STEP;
    }
    Door_Draw( door );
    g_nextTask = node->Next;
}

/*
==================
Door_Power
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
int Door_Power( int step, int index )
{
    door_t *door = &g_doors[(u8)index];
    u8      flags = door->flags;

    if ( flags & DOOR_MOVING )
        return 0;
    if ( door->count < door->wait ) {
        door->count = (unsigned int)step + door->count;
        if ( door->count == door->wait ) {
            Hud_ShowMessage( s_doorActivated );
            if ( door->flags & DOOR_COUNTED )
                return -1;
            g_player.tallies[TALLY_DOORS]++;
            door->flags |= DOOR_COUNTED;
        } else {
            Hud_ShowMessage( s_doorPoweredUp );
        }
    } else {
        return 0;
    }
    return -1;
}

/*
==================
Door_PowerFinal
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Door_PowerFinal( int step, int index )
{
    door_t *door = &g_doors[(u8)index];

    if ( door->flags & DOOR_MOVING )
        return 0;
    if ( door->count < door->wait ) {
        if ( door->count < door->wait - 1 )
            return 0;
    } else {
        return 0;
    }
    door->count = (unsigned int)step + door->count;
    if ( door->count == door->wait ) {
        Hud_ShowMessage( s_doorActivated );
        if ( door->flags & DOOR_COUNTED )
            return -1;
        g_player.tallies[TALLY_DOORS]++;
        door->flags |= DOOR_COUNTED;
    } else {
        Hud_ShowMessage( s_doorPoweredUp );
    }
    return -1;
}

/*
==================
Door_Build

Door_Build builds each door from its record, marks its doorway cells and starts its task
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_Build( void )
{
    door_t       *door = g_doors;
    doorRecord_t *record = g_doorDefs;
    short         i;
    u_int         x, z;
    u_int         cx, cz; /* the first doorway cell */
    mapCell_t    *cell;
    TASK         *task;

    g_levelMap.kindCounts[TALLY_DOORS] = g_doorCount;
    for ( i = 0; i < g_doorCount; i++ ) {
        x = record->x;
        z = record->z;
        door->flags = record->flags;
        door->hold = record->hold << DOOR_HOLD_SHIFT;
        door->wait = record->wait;
        door->count = 0;
        door->facing = record->facing;
        door->model = record->model;
        switch ( door->facing ) {
        case FACING_NEG_Z:
        case FACING_POS_Z:
            door->flags |= DOOR_ALONG_X;
            break;
        case FACING_POS_X:
        case FACING_NEG_X:
            door->flags |= DOOR_ALONG_Z;
            break;
        }
        if ( door->flags & DOOR_RESTS_OPEN ) {
            if ( door->flags & DOOR_ALONG_Z ) {
                cx = x;
                cz = z - 1;
                cell = g_levelMap.cells + g_levelMap.width * cz + cx;
                cell->value = record->value;
                cell->ceiling = cell->value + g_doorOpenTicks;
                door->cellC = cell;
                cell->light |= CELL_BLOCKED;
                cell += g_levelMap.width;
                cell->value = record->value;
                cell->ceiling = cell->value + g_doorOpenTicks;
                door->cellA = cell;
                cell += g_levelMap.width;
                cell->value = record->value;
                cell->ceiling = cell->value + g_doorOpenTicks;
                door->cellB = cell;
                cell += g_levelMap.width;
                cell->value = record->value;
                cell->ceiling = cell->value + g_doorOpenTicks;
                door->cellD = cell;
                cell->light |= CELL_BLOCKED;
                door->position.vx = ( x << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetX;
                door->position.vz = ( z << MAP_CELL_SHIFT ) + MAP_CELL_SIZE - g_levelMap.offsetZ;
                door->position.vy = ( cell->ceiling << MAP_HEIGHT_SHIFT ) + MAP_HEIGHT_HALF_STEP;
            } else {
                cx = x - 1;
                cz = z;
                cell = g_levelMap.cells + g_levelMap.width * cz + cx;
                cell->value = record->value;
                cell->ceiling = cell->value + g_doorOpenTicks;
                door->cellC = cell;
                cell->light |= CELL_BLOCKED;
                cell++;
                cell->value = record->value;
                cell->ceiling = cell->value + g_doorOpenTicks;
                door->cellA = cell;
                cell++;
                cell->value = record->value;
                cell->ceiling = cell->value + g_doorOpenTicks;
                door->cellB = cell;
                cell++;
                cell->value = record->value;
                cell->ceiling = cell->value + g_doorOpenTicks;
                door->cellD = cell;
                cell->light |= CELL_BLOCKED;
                door->position.vx = ( x << MAP_CELL_SHIFT ) + MAP_CELL_SIZE - g_levelMap.offsetX;
                door->position.vz = ( z << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetZ;
                door->position.vy = ( cell->ceiling << MAP_HEIGHT_SHIFT ) + MAP_HEIGHT_HALF_STEP;
            }
            task = TaskCreate( TASK_PRIORITY_DOORS, Door_TaskOpen );
            task->state[DOOR_TASK_DOOR] = (unsigned int)door;
            task->state[DOOR_TASK_STEPS] = g_doorOpenTicks;
        } else {
            if ( door->flags & DOOR_ALONG_Z ) {
                cx = x;
                cz = z - 1;
                cell = g_levelMap.cells + g_levelMap.width * cz + cx;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cellC = cell;
                cell->light |= CELL_BLOCKED;
                cell += g_levelMap.width;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cellA = cell;
                cell += g_levelMap.width;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cellB = cell;
                cell += g_levelMap.width;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cellD = cell;
                cell->light |= CELL_BLOCKED;
                door->position.vx = ( x << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetX;
                door->position.vz = ( z << MAP_CELL_SHIFT ) + MAP_CELL_SIZE - g_levelMap.offsetZ;
                door->position.vy = ( record->value << MAP_HEIGHT_SHIFT ) + MAP_HEIGHT_HALF_STEP;
            } else {
                cx = x - 1;
                cz = z;
                cell = g_levelMap.cells + g_levelMap.width * cz + cx;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cellC = cell;
                cell->light |= CELL_BLOCKED;
                cell++;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cellA = cell;
                cell++;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cellB = cell;
                cell++;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cellD = cell;
                cell->light |= CELL_BLOCKED;
                door->position.vx = ( x << MAP_CELL_SHIFT ) + MAP_CELL_SIZE - g_levelMap.offsetX;
                door->position.vz = ( z << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetZ;
                door->position.vy = ( record->value << MAP_HEIGHT_SHIFT ) + MAP_HEIGHT_HALF_STEP;
            }
            task = TaskCreate( TASK_PRIORITY_DOORS, Door_TaskClosed );
            task->state[DOOR_TASK_STEPS] = 0;
            task->state[DOOR_TASK_DOOR] = (unsigned int)door;
        }
        door++;
        record++;
    }
}

/*
==================
Door_FixupModelFaces

The face kind is the halfword over flags and selector until this splits it.
==================
*/
/* name: inferred from its body: turns a door model's vertex indices into pointers and
 * each face kind into flags and a dispatch selector */
void Door_FixupModelFaces( modelRow_t *face, SVECTOR *vertices, u8 flags )
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
Door_PrepareModels
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
/* MATCH-PENDING: R4, header fields walked through a u8 cursor stepped by member sizes and read via *(int *)p; block->rowCount/block->rows change the function size */
void Door_PrepareModels( void )
{
    s16         i;
    u8         *p;
    modelRow_t *rows;
    int         count;

    g_doorOpenTicks = g_level < LEVEL_113_AMMUNITION_DUMP ? DOOR_OPEN_STEPS_SHORT : DOOR_OPEN_STEPS;
    for ( i = 0; i < g_doorModelCount; i++ ) {
        p = g_doorModels[i];
        if ( p ) {
            p += sizeof( ( (modelBlock_t *)p )->tag ) + sizeof( ( (modelBlock_t *)p )->unknown_04 ); /* to rowCount */
            count = *(int *)p;
            p += sizeof( ( (modelBlock_t *)p )->rowCount ) + sizeof( ( (modelBlock_t *)p )->vertexCount ); /* to rows */
            rows = (modelRow_t *)p;
            Door_FixupModelFaces( rows, (SVECTOR *)( rows + count ), MODEL_PALETTE );
            g_doorModels[i] = p;
        }
    }
}

/*
==================
Door_Alloc
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Door_Alloc( void )
{
    g_doors = Heap_Alloc( DOOR_MAX * sizeof( door_t ) );
    g_doorCount = 0;
}

/*
==================
Door_Init
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Door_Init( void )
{
    if ( g_doorCount )
        Door_Build();
}
