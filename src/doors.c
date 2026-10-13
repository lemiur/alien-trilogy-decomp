/* Doors: their level records (a 0x20-byte Door each, in a heap block), drawing,
 * the open/close tasks and power-up. Retail .text from at least 0x800223a0 to
 * 0x800235ac. One translation unit: its statics (.sbss 0x8008d100..0x8008d118)
 * are read by door_task_opening (0x80022b18), door_power/door_power_final, door_models_prepare and doors_alloc/doors_init
 * (0x80023580), doors_build and door_defs_load; its rotations (.sdata
 * 0x8008cfac) and strings (.rdata 0x800119ec) follow the previous modules' data.
 * Functions not yet matched are absent. Name invented.
 */
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
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;

/* Module statics (.sbss 0x8008d100..0x8008d118), in their retail order. The door table
 * (doors_alloc allocates it, doors_build fills it), the level's door definitions and their
 * count (door_defs_load), and the ticks a door takes to open (door_task_opening starts the door when
 * its counter reaches it), also the ceiling offset added to the doorway cells
 * (doors_build): 0x1e on the first two levels, else 0x2b (door_models_prepare).
 * names: runtime evidence, analysis/runtime/globals.json */
static Door *g_doors;
static DoorRecord *g_door_defs;
static short g_door_count;
static short g_door_open_ticks;


/* Messages of door_power and door_power_final. Each array's extent includes its alignment padding. */
const char s_door_activated[16] = "Door activated";
const char s_door_powered_up[16] = "Door powered up";

/* Door rotations by facing (0, 2, 4, 6); constant, never written. Four
 * objects, not one array: retail keeps them in .sdata, where -G8 admits only
 * objects of eight bytes or less.
 * names: invented from analysis/runtime/globals.json's g_door_rotations */
static SVECTOR g_door_rotation_0 = { 0, ONE / 2, 0, 0 };
static SVECTOR g_door_rotation_2 = { 0, ONE / 4, 0, 0 };
static SVECTOR g_door_rotation_4 = { 0, 0, 0, 0 };
static SVECTOR g_door_rotation_6 = { 0, -ONE / 4, 0, 0 };

/* Functions of this module referenced before their definitions. */
void door_task_open(TASK *node);
void door_task_closed(TASK *node);
void door_task_closing(TASK *node);
void door_task_opening(TASK *node);

/* name: runtime evidence, analysis/runtime/functions.json */
int door_blocked(Door *door)
{
    MapCell *a = door->cell_a;
    MapCell *b = door->cell_b;
    int stride;

    if (a->marker || b->marker)
        return -1;
    if (door->flags & DOOR_ALONG_Z) {
        if (a[-1].marker || a[1].marker || b[-1].marker || b[1].marker)
            return -1;
    } else {
        stride = g_level_map.width * sizeof(MapCell);
        if (((MapCell *)((u8 *)a - stride))->marker || ((MapCell *)((u8 *)a + stride))->marker
            || ((MapCell *)((u8 *)b - stride))->marker || ((MapCell *)((u8 *)b + stride))->marker)
            return -1;
    }
    return 0;
}

/* name: inferred from its body: keeps the level's door definitions and copies each
 * door's map cell value into it */
void door_defs_load(DoorRecord *records, short count)
{
    MapCell *cell;
    int z;

    g_door_count = count;
    g_door_defs = records;
    while (count-- > 0U) {
        z = records->z;
        cell = g_level_map.cells + g_level_map.width * z;
        cell += records->x;
        records->value = cell->value;
        records++;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void level_doors_load(void)
{
    if (g_level_map.door_record_count)
        door_defs_load(g_level_map.door_records, g_level_map.door_record_count);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void door_draw(Door *door)
{
    SVECTOR *rotation;
    DispatchRow *model;

    if (CHEAT_IS_ACTIVE(CHEAT_FLAG_DEBUG))
        return;
    if ((door->cell_a->mask & g_visible_cell_mask) == 0)
        return;
    model = (DispatchRow *)g_door_models[door->model];
    if (model == 0)
        return;
    switch (door->facing) {
    case FACING_POS_X:
        rotation = &g_door_rotation_2;
        break;
    case FACING_POS_Z:
        rotation = &g_door_rotation_4;
        break;
    case FACING_NEG_X:
        rotation = &g_door_rotation_6;
        break;
    case FACING_NEG_Z:
    default:
        rotation = &g_door_rotation_0;
        break;
    }
    LoadAverageShort12(&door->position, &g_view_position, ONE, -ONE, &g_relative_position);
    PushMatrix();
    ApplyMatrix(&g_view_matrix, &g_relative_position, &g_translation);
    if (g_translation.vz < DOOR_CULL_DISTANCE && g_translation.vx < DOOR_CULL_DISTANCE) {
        RotMatrix(rotation, &g_matrix_work);
        MulMatrix2(&g_view_matrix, &g_matrix_work);
        SetRotMatrix(&g_matrix_work);
        TransMatrix(&g_matrix_work, &g_translation);
        SetTransMatrix(&g_matrix_work);
        if (g_translation.vz < DOOR_NEAR_DISTANCE && g_translation.vx < DOOR_NEAR_DISTANCE)
            model_draw_near(model);
        else if (g_translation.vz < MODEL_FAR_DISTANCE && g_translation.vx < MODEL_FAR_DISTANCE)
            model_draw_mid(model);
        else
            model_draw_far(model);
    }
    PopMatrix();
}

/* name: static evidence, analysis/runtime/functions.json */
void door_task_static(TASK *node)
{
    door_draw((void *)node->state[0]);
    g_next_task = node->Next;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void door_task_hold(TASK *task)
{
    Door *door = (Door *)task->state[0];
    task->state[3]--;
    if (task->state[3] == 0) {
        if (door->flags & DOOR_OPEN)
            task->callback = door_task_open;
        else
            task->callback = door_task_closed;
    }
    door_draw(door);
    g_next_task = task->Next;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void door_task_open(TASK *node)
{
    Door *door = (Door *)node->state[0];
    int distance;

    if (door->count >= door->wait) {
        if (door->flags & DOOR_ALONG_Z)
            distance = door->position.vx - g_view_position.vx;
        else
            distance = door->position.vz - g_view_position.vz;
        if (ABS(distance) > DOOR_CLOSE_DISTANCE && door_blocked(door) == 0) {
            door->flags |= DOOR_CLOSING;
            node->callback = door_task_closing;
            sound_play_at(SOUND_DOOR_MOVE, &door->position, SOUND_AT_SOFT);
        }
    }
    door_draw(door);
    g_next_task = node->Next;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void door_task_closed(TASK *node)
{
    Door *door = (Door *)node->state[0];
    if (door->count >= door->wait) {
        g_player.noise_timer = DOOR_NOISE_TIME;
        door->flags |= DOOR_OPENING;
        node->callback = door_task_opening;
        sound_play_at(SOUND_DOOR_MOVE, &door->position, SOUND_AT_NEAR);
    }
    door_draw(door);
    g_next_task = node->Next;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void door_task_closing(TASK *node)
{
    Door *door = (Door *)node->state[0];
    short height;

    node->state[2]--;
    if (door_blocked(door)) {
        door->flags = (door->flags & ~DOOR_CLOSING) | DOOR_OPENING;
        node->callback = door_task_opening;
        sound_play_at(SOUND_DOOR_MOVE, &door->position, SOUND_AT_SOFT);
    } else if (node->state[2] == 0) {
        door->flags &= ~(DOOR_CLOSING | DOOR_OPEN);
        if ((node->state[3] = (u16)door->hold) == 0) {
            door->count = 0;
            node->callback = door_task_closed;
        } else if (!(door->flags & DOOR_RESTS_OPEN)) {
            door->count = 0;
            node->callback = door_task_closed;
        } else {
            node->callback = door_task_hold;
        }
        sound_play_at(SOUND_DOOR_STOP, &door->position, SOUND_AT_SOFT);
    } else {
        height = door->cell_a->ceiling;
        if (--height < 0)
            height = 0;
        door->cell_a->ceiling = height;
        door->cell_b->ceiling = height;
        door->cell_c->ceiling = height;
        door->cell_d->ceiling = height;
        door->position.vy -= MAP_HEIGHT_STEP;
    }
    door_draw(door);
    g_next_task = node->Next;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void door_task_opening(TASK *node)
{
    Door *door = (Door *)node->state[0];
    u8 height;

    if (++node->state[2] == g_door_open_ticks) {
        sound_play_at(SOUND_DOOR_STOP, &door->position, SOUND_AT_NEAR);
        door->flags = (door->flags & ~DOOR_OPENING) | DOOR_OPEN;
        if ((node->state[3] = (u16)door->hold) == 0 || (door->flags & DOOR_RESTS_OPEN)) {
            door->count = 0;
            node->callback = door_task_open;
        } else {
            node->callback = door_task_hold;
        }
    } else {
        height = ++door->cell_a->ceiling;
        door->cell_b->ceiling = height;
        door->cell_c->ceiling = height;
        door->cell_d->ceiling = height;
        door->position.vy += MAP_HEIGHT_STEP;
    }
    door_draw(door);
    g_next_task = node->Next;
}

/* name: static evidence, analysis/runtime/functions.json */
int door_power(int step, int index)
{
    Door *door = &g_doors[(u8)index];
    u8 flags = door->flags;

    if (flags & DOOR_MOVING)
        return 0;
    if (door->count < door->wait) {
        door->count = (unsigned int)step + door->count;
        if (door->count == door->wait) {
            hud_show_message(s_door_activated);
            if (door->flags & DOOR_COUNTED)
                return -1;
            g_player.tallies[TALLY_DOORS]++;
            door->flags |= DOOR_COUNTED;
        } else {
            hud_show_message(s_door_powered_up);
        }
    } else {
        return 0;
    }
    return -1;
}

/* name: runtime evidence, analysis/runtime/functions.json */
int door_power_final(int step, int index)
{
    Door *door = &g_doors[(u8)index];

    if (door->flags & DOOR_MOVING)
        return 0;
    if (door->count < door->wait) {
        if (door->count < door->wait - 1)
            return 0;
    } else {
        return 0;
    }
    door->count = (unsigned int)step + door->count;
    if (door->count == door->wait) {
        hud_show_message(s_door_activated);
        if (door->flags & DOOR_COUNTED)
            return -1;
        g_player.tallies[TALLY_DOORS]++;
        door->flags |= DOOR_COUNTED;
    } else {
        hud_show_message(s_door_powered_up);
    }
    return -1;
}

/* doors_build builds each door from its record, marks its doorway cells and starts its task */
/* name: runtime evidence, analysis/runtime/functions.json */
void doors_build(void)
{
    Door *door = g_doors;
    DoorRecord *record = g_door_defs;
    short i;
    u_int x, z;
    u_int cx, cz; /* the first doorway cell */
    MapCell *cell;
    TASK *task;

    g_level_map.kind_counts[TALLY_DOORS] = g_door_count;
    for (i = 0; i < g_door_count; i++) {
        x = record->x;
        z = record->z;
        door->flags = record->flags;
        door->hold = record->hold << DOOR_HOLD_SHIFT;
        door->wait = record->wait;
        door->count = 0;
        door->facing = record->facing;
        door->model = record->model;
        switch (door->facing) {
        case FACING_NEG_Z:
        case FACING_POS_Z:
            door->flags |= 0; /* retail rewrites flags unchanged for facings 0 and 4 */
            break;
        case FACING_POS_X:
        case FACING_NEG_X:
            door->flags |= DOOR_ALONG_Z;
            break;
        }
        if (door->flags & DOOR_RESTS_OPEN) {
            if (door->flags & DOOR_ALONG_Z) {
                cx = x;
                cz = z - 1;
                cell = g_level_map.cells + g_level_map.width * cz + cx;
                cell->value = record->value;
                cell->ceiling = cell->value + g_door_open_ticks;
                door->cell_c = cell;
                cell->light |= CELL_BLOCKED;
                cell += g_level_map.width;
                cell->value = record->value;
                cell->ceiling = cell->value + g_door_open_ticks;
                door->cell_a = cell;
                cell += g_level_map.width;
                cell->value = record->value;
                cell->ceiling = cell->value + g_door_open_ticks;
                door->cell_b = cell;
                cell += g_level_map.width;
                cell->value = record->value;
                cell->ceiling = cell->value + g_door_open_ticks;
                door->cell_d = cell;
                cell->light |= CELL_BLOCKED;
                door->position.vx = (x << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_x;
                door->position.vz = (z << MAP_CELL_SHIFT) + MAP_CELL_SIZE - g_level_map.offset_z;
                door->position.vy = (cell->ceiling << MAP_HEIGHT_SHIFT) + MAP_HEIGHT_HALF_STEP;
            } else {
                cx = x - 1;
                cz = z;
                cell = g_level_map.cells + g_level_map.width * cz + cx;
                cell->value = record->value;
                cell->ceiling = cell->value + g_door_open_ticks;
                door->cell_c = cell;
                cell->light |= CELL_BLOCKED;
                cell++;
                cell->value = record->value;
                cell->ceiling = cell->value + g_door_open_ticks;
                door->cell_a = cell;
                cell++;
                cell->value = record->value;
                cell->ceiling = cell->value + g_door_open_ticks;
                door->cell_b = cell;
                cell++;
                cell->value = record->value;
                cell->ceiling = cell->value + g_door_open_ticks;
                door->cell_d = cell;
                cell->light |= CELL_BLOCKED;
                door->position.vx = (x << MAP_CELL_SHIFT) + MAP_CELL_SIZE - g_level_map.offset_x;
                door->position.vz = (z << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_z;
                door->position.vy = (cell->ceiling << MAP_HEIGHT_SHIFT) + MAP_HEIGHT_HALF_STEP;
            }
            task = TaskCreate(TASK_PRIORITY_DOORS, door_task_open);
            task->state[0] = (unsigned int)door;
            task->state[2] = g_door_open_ticks;
        } else {
            if (door->flags & DOOR_ALONG_Z) {
                cx = x;
                cz = z - 1;
                cell = g_level_map.cells + g_level_map.width * cz + cx;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cell_c = cell;
                cell->light |= CELL_BLOCKED;
                cell += g_level_map.width;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cell_a = cell;
                cell += g_level_map.width;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cell_b = cell;
                cell += g_level_map.width;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cell_d = cell;
                cell->light |= CELL_BLOCKED;
                door->position.vx = (x << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_x;
                door->position.vz = (z << MAP_CELL_SHIFT) + MAP_CELL_SIZE - g_level_map.offset_z;
                door->position.vy = (record->value << MAP_HEIGHT_SHIFT) + MAP_HEIGHT_HALF_STEP;
            } else {
                cx = x - 1;
                cz = z;
                cell = g_level_map.cells + g_level_map.width * cz + cx;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cell_c = cell;
                cell->light |= CELL_BLOCKED;
                cell++;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cell_a = cell;
                cell++;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cell_b = cell;
                cell++;
                cell->value = record->value;
                cell->ceiling = record->value;
                door->cell_d = cell;
                cell->light |= CELL_BLOCKED;
                door->position.vx = (x << MAP_CELL_SHIFT) + MAP_CELL_SIZE - g_level_map.offset_x;
                door->position.vz = (z << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_z;
                door->position.vy = (record->value << MAP_HEIGHT_SHIFT) + MAP_HEIGHT_HALF_STEP;
            }
            task = TaskCreate(TASK_PRIORITY_DOORS, door_task_closed);
            task->state[2] = 0;
            task->state[0] = (unsigned int)door;
        }
        door++;
        record++;
    }
}

/* The face kind is the halfword over flags and selector until this splits it.
 * name: inferred from its body: turns a door model's vertex indices into pointers and
 * each face kind into flags and a dispatch selector */
void door_model_faces_fixup(DispatchRow *face, SVECTOR *vertices, u8 flags)
{
    int index;
    short last;
    u8 value;

    do {
        index = (int)face->vertices[0];
        face->vertices[0] = vertices + index;
        index = (int)face->vertices[1];
        face->vertices[1] = vertices + index;
        index = (int)face->vertices[2];
        face->vertices[2] = vertices + index;
        index = (int)face->vertices[3];
        face->vertices[3] = vertices + index;
        last = *(u16 *)&face->flags & ROW_FLAG_LAST;
        switch ((s16)(*(u16 *)&face->flags & ~ROW_FLAG_LAST)) {
        case FACE_KIND_QUAD:
        case FACE_KIND_QUAD_OTHER:
            face->selector = ROW_SELECT_QUAD;
            break;
        case FACE_KIND_QUAD_MIRROR:
            face->selector = ROW_SELECT_QUAD_ALT;
            break;
        case FACE_KIND_TRIANGLE ... FACE_KIND_TRIANGLE_LAST:
            face->selector = ROW_SELECT_TRI;
            break;
        default:
            face->selector = ROW_SELECT_TRI;
            break;
        }
        value = flags;
        if (last)
            value |= ROW_FLAG_LAST;
        face->flags = value;
        face++;
    } while (!(value & ROW_FLAG_LAST));
}

/* name: runtime evidence, analysis/runtime/functions.json */
void door_models_prepare(void)
{
    s16 i;
    u8 *p;
    int count;

    g_door_open_ticks = g_level < LEVEL_113_AMMUNITION_DUMP ? DOOR_OPEN_STEPS_SHORT : DOOR_OPEN_STEPS;
    for (i = 0; i < g_door_model_count; i++) {
        p = g_door_models[i];
        if (p) {
            p += sizeof(((ModelBlock *)p)->tag) + sizeof(((ModelBlock *)p)->unknown_04); /* to row_count */
            count = *(int *)p;
            p += sizeof(((ModelBlock *)p)->row_count) + sizeof(((ModelBlock *)p)->vertex_count); /* to rows */
            door_model_faces_fixup((DispatchRow *)p, (SVECTOR *)(p + count * sizeof(DispatchRow)), MODEL_PALETTE);
            g_door_models[i] = p;
        }
    }
}

/* name: static evidence, analysis/runtime/functions.json */
void doors_alloc(void) { g_doors = heap_alloc(DOOR_MAX * sizeof(Door)); g_door_count = 0; }

/* name: runtime evidence, analysis/runtime/functions.json */
void doors_init(void) { if (g_door_count) doors_build(); }
