#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <MODEL.H>
#include <CAMERA.H>
#include <TASK.H>
#include <HEAP.H>
#include <AUDIO.H>
#include <TEXT.H>
#include <HUD.H>
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;


/* Module statics (.sbss 0x8008d1e8..0x8008d1f8), in their retail order. */
static Lift *g_lifts;
static LiftRecord *g_lift_records;
static s16 g_lift_record_count;
extern void lifts_create(void);
extern void lift_draw(Lift *lift);
void lift_records_prepare(LiftRecord *entry, s16 count);
void lift_set_height(Lift *lift, u8 height);
void lift_task_delay(TASK *task);
void lift_task_lower(TASK *task);
void lift_task_raise(TASK *task);
void lift_task_wait(TASK *task);

char *s_lift_activated = "Lift activated";

/* The face kind is the halfword over flags and selector until this splits it.
 * name: inferred from its body (the same function as door_model_faces_fixup): turns a
 * lift model's vertex indices into pointers and each face kind into flags and a
 * dispatch selector */
void lift_model_faces_fixup(DispatchRow *face, SVECTOR *vertices, u8 flags)
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
void lift_models_prepare(void)
{
    s16 i;
    for (i = 0; i < g_lift_model_count; i++) {
        int *p = g_lift_models[i];
        int count;
        p += (sizeof(((ModelBlock *)p)->tag) + sizeof(((ModelBlock *)p)->unknown_04)) / sizeof(*p); /* to row_count */
        count = *p;
        p += (sizeof(((ModelBlock *)p)->row_count) + sizeof(((ModelBlock *)p)->vertex_count)) / sizeof(*p); /* to rows */
        lift_model_faces_fixup((DispatchRow *)p, (SVECTOR *)((u8 *)p + count * sizeof(DispatchRow)), MODEL_PALETTE);
        g_lift_models[i] = p;
    }
}

/* name: inferred from its body, which allocates the lift table */
void lifts_alloc(void)
{
    g_lifts = heap_alloc(LIFT_MAX * sizeof(Lift));
}

/* name: inferred from its body, which raises each lift record by its map cell's value */
void lift_records_prepare(LiftRecord *entry, s16 count)
{
    g_lift_record_count = count;
    g_lift_records = entry;
    while (count--) {
        int x = entry->x;
        int z = entry->z;
        MapCell *cell = g_level_map.cells + g_level_map.width * z + x;
        entry->level = cell->value;
        entry->base += cell->value;
        entry++;
    }
}

void lift_records_init(void)
{
    lift_records_prepare(g_level_map.lift_records, g_level_map.lift_count);
}

int lift_trigger(int amount, u8 index)
{
    Lift *lift = g_lifts + index;
    if (lift->flags & (LIFT_RAISING | LIFT_LOWERING))
        return 0;
    if (lift->count >= lift->wait)
        return 0;
    lift->count = (unsigned int)amount + lift->count;
    return -1;
}

int lift_trigger_last(int amount, u8 index)
{
    Lift *lift = g_lifts + index;
    if (lift->flags & (LIFT_RAISING | LIFT_LOWERING))
        return 0;
    if (lift->count < lift->wait) {
        if (lift->count >= lift->wait - 1) {
            lift->count = (unsigned int)amount + lift->count;
            return -1;
        }
    }
    return 0;
}

void lift_set_height(Lift *lift, u8 height)
{
    MapCell *row = lift->cells;
    short z;

    for (z = 0; z < lift->depth; ++z) {
        MapCell *cell = row;
        short x;

        for (x = 0; x < lift->width; ++x) {
            cell->value = height;
            cell++;
        }
        row += g_level_map.width;
    }
}

void lift_task_delay(TASK *task)
{
    Lift *lift = (Lift *)task->state[0];
    --lift->delay;
    if (lift->delay == 0) {
        if (lift->flags & LIFT_UP) {
            lift->flags = (lift->flags & ~LIFT_UP) | LIFT_LOWERING;
            task->callback = lift_task_lower;
        } else {
            lift->flags = (lift->flags & ~LIFT_DOWN) | LIFT_RAISING;
            task->callback = lift_task_raise;
        }
        sound_play_at(SOUND_LIFT_MOVE, &lift->position, SOUND_AT_SOFT);
    }
    lift_draw(lift);
    g_next_task = task->Next;
}

void lift_task_lower(TASK *task)
{
    Lift *lift = (Lift *)task->state[0];
    short step;
    short height;
    step = 1;
    if (lift->flags & LIFT_FAST)
        step = LIFT_FAST_STEP;
    height = lift->cells->value - step;
    lift_set_height(lift, (u8)height);
    lift->position.vy -= step << MAP_HEIGHT_SHIFT;
    if (height == lift->low) {
        lift->flags = (lift->flags & ~LIFT_LOWERING) | LIFT_DOWN;
        sound_play_at(SOUND_LIFT_STOP, &lift->position, SOUND_AT_SOFT);
        if (!(lift->flags & LIFT_CYCLES)) {
            if (lift->hold == 0) {
                lift->count = 0;
                task->callback = lift_task_wait;
            } else if (!(lift->flags & LIFT_RESTS_UP)) {
                lift->count = 0;
                task->callback = lift_task_wait;
            } else {
                lift->delay = lift->hold;
                task->callback = lift_task_delay;
            }
        } else {
            lift->delay = lift->hold;
            task->callback = lift_task_delay;
        }
    }
    lift_draw(lift);
    g_next_task = task->Next;
}

void lift_task_raise(TASK *task)
{
    Lift *lift = (Lift *)task->state[0];
    short step;
    short height;
    step = 1;
    if (lift->flags & LIFT_FAST)
        step = LIFT_FAST_STEP;
    height = lift->cells->value + step;
    lift_set_height(lift, (u8)height);
    lift->position.vy += step << MAP_HEIGHT_SHIFT;
    if (height == lift->high) {
        lift->flags = (lift->flags & ~LIFT_RAISING) | LIFT_UP;
        sound_play_at(SOUND_LIFT_STOP, &lift->position, SOUND_AT_SOFT);
        if (!(lift->flags & LIFT_CYCLES)) {
            if (lift->hold == 0) {
                lift->count = 0;
                task->callback = lift_task_wait;
            } else if (lift->flags & LIFT_RESTS_UP) {
                lift->count = 0;
                task->callback = lift_task_wait;
            } else {
                lift->delay = lift->hold;
                task->callback = lift_task_delay;
            }
        } else {
            lift->delay = lift->hold;
            task->callback = lift_task_delay;
        }
    }
    lift_draw(lift);
    g_next_task = task->Next;
}

void lift_task_wait(TASK *task)
{
    Lift *lift = (Lift *)task->state[0];
    if (lift->count >= lift->wait) {
        if (!(lift->flags & LIFT_COUNTED)) {
            lift->flags |= LIFT_COUNTED;
            g_player.tallies[TALLY_LIFTS]++;
        }
        hud_show_message(s_lift_activated);
        if (lift->flags & LIFT_UP) {
            lift->flags = (lift->flags & ~LIFT_UP) | LIFT_LOWERING;
            sound_play_at(SOUND_LIFT_MOVE, &lift->position, SOUND_AT_FULL);
            task->callback = lift_task_lower;
        } else {
            lift->flags = (lift->flags & ~LIFT_DOWN) | LIFT_RAISING;
            sound_play_at(SOUND_LIFT_MOVE, &lift->position, SOUND_AT_FULL);
            task->callback = lift_task_raise;
        }
    }
    lift_draw(lift);
    g_next_task = task->Next;
}

/* Centres the lift on its cells and offsets it by half its model's height range.
 * codegen: the shifts are second assignments to width and depth; a once-set
 * temporary would be scheduled next to its add (sched1's birthing boost).
 * name: inferred from its body */
void lift_measure_model(Lift *lift)
{
    short high;
    short low;
    DispatchRow *face;
    u_int width;
    u_int depth;

    lift->offset = 0;
    low = LIFT_FACE_HEIGHT_LIMIT;
    high = -LIFT_FACE_HEIGHT_LIMIT;
    width = lift->width;
    depth = lift->depth;
    width <<= LIFT_HALF_CELL_SHIFT;
    depth <<= LIFT_HALF_CELL_SHIFT;
    face = lift->faces;
    lift->centre_x = lift->position.vx - MAP_CELL_HALF + width;
    lift->centre_z = lift->position.vz - MAP_CELL_HALF + depth;
    if (face != 0) {
        do {
            if (face->vertices[0]->vy < low) low = face->vertices[0]->vy;
            if (high < face->vertices[0]->vy) high = face->vertices[0]->vy;
            if (face->vertices[1]->vy < low) low = face->vertices[1]->vy;
            if (high < face->vertices[1]->vy) high = face->vertices[1]->vy;
            if (face->vertices[2]->vy < low) low = face->vertices[2]->vy;
            if (high < face->vertices[2]->vy) high = face->vertices[2]->vy;
            if (face->selector == ROW_SELECT_QUAD || face->selector == ROW_SELECT_QUAD_ALT) {
                if (face->vertices[3]->vy < low) low = face->vertices[3]->vy;
                if (high < face->vertices[3]->vy) high = face->vertices[3]->vy;
            }
        } while (!((face++)->flags & ROW_FLAG_LAST));
        lift->offset = -((high - low) >> 1);
    }
}

/* name: inferred from its body, which builds each lift from its record and starts its wait task */
void lifts_create(void)
{
    Lift *lift = g_lifts;
    LiftRecord *record = g_lift_records;
    short i;
    TASK *task;

    g_level_map.kind_counts[TALLY_LIFTS] = g_lift_record_count;
    for (i = 0; i < g_lift_record_count; i++) {
        u_int x = record->x;
        u_int z = record->z;

        lift->cells = g_level_map.cells + g_level_map.width * z + x;
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
        lift->position.vx = (x << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_x;
        lift->position.vz = (z << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_z;
        if (lift->delay)
            lift->flags |= LIFT_CYCLES;
        if (lift->flags & LIFT_UP) {
            lift->flags |= LIFT_RESTS_UP;
            lift->position.vy = (record->base << MAP_HEIGHT_SHIFT) + MAP_HEIGHT_HALF_STEP;
            lift_set_height(lift, record->base);
        } else {
            lift->position.vy = (record->level << MAP_HEIGHT_SHIFT) + MAP_HEIGHT_HALF_STEP;
            lift_set_height(lift, record->level);
        }
        lift->faces = g_lift_models[lift->model];
        lift_measure_model(lift);
        task = TaskCreate(TASK_PRIORITY_DOORS, lift_task_wait);
        task->state[0] = (unsigned int)lift;
        lift++;
        record++;
    }
}

/* name: inferred from its body, which creates the lifts when the level has any */
void lifts_spawn(void)
{
    if (g_level_map.lift_count)
        lifts_create();
}
