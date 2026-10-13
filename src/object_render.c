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
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;




extern LevelObject *g_level_objects;
/* invented names: ring offsets (x, z) of the 8 inner and 16 outer cells around an explosion */
static short g_blast_near_offsets[BLAST_NEAR_CELLS * 2] = {
    -1, -1,  0, -1,  1, -1,  1,  0,  1,  1,  0,  1, -1,  1, -1,  0
};
static short g_blast_far_offsets[BLAST_FAR_CELLS * 2] = {
    -2, -2, -1, -2,  0, -2,  1, -2,  2, -2,  2, -1,  2,  0,  2,  1,
     2,  2,  1,  2,  0,  2, -1,  2, -2,  2, -2,  1, -2,  0, -2, -1
};
/* invented names: the cells of those rings that an explosion reaches (explosion_damage) */
extern MapCell *g_blast_near[BLAST_NEAR_CELLS];
extern MapCell *g_blast_far[BLAST_FAR_CELLS];
#define ABS(x) ((x) >= 0 ? (x) : -(x))
extern void level_objects_create(LevelObjectRecord *record, s16 count);
/* name: invented; level_objects_create keeps the level's object records and their count */
extern LevelObjectRecord *g_level_object_records;
extern s16 g_level_object_count;
/* name: invented; level object models, filled at level load (identity-TODO: layout of the table) */
extern DispatchRow *g_level_object_models[];
extern void level_object_mark_cells(LevelObject *block);
extern void level_object_task(TASK *task);
extern void level_object_spin_task(TASK *task);
extern void level_object_hit_task(TASK *task);
extern void level_object_debris_task(TASK *task);
extern void block_object_task(TASK *task);
extern void switch_panel_task(TASK *task);
extern void switch_panel_active_task(TASK *task);

/* Fixes up a level object model's rows after loading: vertex indices become pointers into
 * vertices, the file's 16-bit face kind (with bit 7 ending the list) becomes the palette
 * flags and the dispatch selector (14 for the tall model). */
/* name: static evidence, analysis/runtime/functions.json */
void level_object_model_fixup(DispatchRow *face, SVECTOR *vertices, u8 flags, s16 tall)
{
    int index;
    short last;
    u8 value;

    if (tall)
        flags += MODEL_PALETTE_TALL_OFFSET;
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
        if (tall) {
            face->selector = ROW_SELECT_TALL;
        } else {
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
        }
        value = flags;
        if (last)
            value |= ROW_FLAG_LAST;
        face->flags = value;
        face++;
    } while (!(value & ROW_FLAG_LAST));
}

/* name: invented; loads the level object models (g_file_table[FILE_OBJ3D_BND]) into
 * g_level_object_models and allocates g_level_objects */
/* name: static evidence, analysis/runtime/functions.json */
void level_object_models_load(void)
{
    short i;
    short count;
    short index;
    u_int size;
    u_char *p;
    DispatchRow *rows;
    SVECTOR *vertices;
    u_char text[BND_COUNT_DIGITS + 1];

    for (i = 0; i < LEVEL_OBJECT_MODEL_COUNT; i++)
        g_level_object_models[i] = 0;
    load_file(&g_file_table[FILE_OBJ3D_BND]);
    p = g_file_table[FILE_OBJ3D_BND].buffer;
    p += BND_HEADER_SIZE;
    text[0] = *p++;
    text[1] = *p++;
    text[2] = *p++;
    text[3] = *p++;
    text[BND_COUNT_DIGITS] = 0;
    count = parse_decimal(text);
    for (i = 0; i < count; i++) {
        p++;
        text[0] = *p++;
        text[1] = *p++;
        text[2] = *p++;
        text[BND_INDEX_DIGITS] = 0;
        index = parse_decimal(text);
        size = read_be32(p);
        p += BND_LENGTH_SIZE;
        if (size & 1)
            size++;
        rows = ((ModelBlock *)p)->rows;
        vertices = (SVECTOR *)(rows + ((ModelBlock *)p)->row_count);
        if (index == LEVEL_OBJECT_MODEL_TALL)
            level_object_model_fixup(rows, vertices, MODEL_PALETTE, -1);
        else
            level_object_model_fixup(rows, vertices, MODEL_PALETTE, 0);
        g_level_object_models[index] = rows;
        p += size;
    }
    g_level_objects = heap_alloc(LEVEL_OBJECT_MAX * sizeof(LevelObject));
}

/* Whether an explosion at height is stopped by cell: no cell, a wall type, a height step
 * of BLAST_STEP_LIMIT or more, or a closed door (explosion_damage). */
#define BLAST_BLOCKED(cell, height) \
    ((cell) == 0 || (cell)->type >= CELL_TYPE_WALL || ABS((cell)->value - (height)) >= BLAST_STEP_LIMIT \
     || ((cell)->type == CELL_TYPE_DOOR && (cell)->ceiling - (cell)->value < BLAST_DOOR_GAP))

/* name: invented; an explosion at position in cell: gathers the two rings of cells around
 * it, drops the outer cells shielded by blocked inner ones, then hurts the player, kills
 * actors and starts the destruction timers of level objects in the remaining cells. */
/* name: static evidence, analysis/runtime/functions.json */
void explosion_damage(SVECTOR *position, MapCell *cell)
{
    u_char height = cell->value;
    u_int x = (u_int)(position->vx + g_level_map.offset_x) >> MAP_CELL_SHIFT;
    u_int z = (u_int)(position->vz + g_level_map.offset_z) >> MAP_CELL_SHIFT;
    short *offset;
    MapCell *near;
    s16 i;

    offset = g_blast_near_offsets;
    for (i = 0; i < BLAST_NEAR_CELLS; i++) {
        u_int cx = x + *offset++;
        u_int cz = z + *offset++;
        if (cx < g_level_map.width && cz < g_level_map.height)
            g_blast_near[i] = g_level_map.cells + g_level_map.width * cz + cx;
        else
            g_blast_near[i] = 0;
    }
    offset = g_blast_far_offsets;
    for (i = 0; i < BLAST_FAR_CELLS; i++) {
        u_int cx = x + *offset++;
        u_int cz = z + *offset++;
        if (cx < g_level_map.width && cz < g_level_map.height)
            g_blast_far[i] = g_level_map.cells + g_level_map.width * cz + cx;
        else
            g_blast_far[i] = 0;
    }
    near = g_blast_near[0];
    if (BLAST_BLOCKED(near, height)) {
        g_blast_near[0] = 0;
        g_blast_far[0] = 0;
        g_blast_far[1] = 0;
        g_blast_far[15] = 0;
    }
    near = g_blast_near[1];
    if (BLAST_BLOCKED(near, height)) {
        g_blast_near[1] = 0;
        g_blast_far[2] = 0;
    }
    near = g_blast_near[2];
    if (BLAST_BLOCKED(near, height)) {
        g_blast_near[2] = 0;
        g_blast_far[3] = 0;
        g_blast_far[4] = 0;
        g_blast_far[5] = 0;
    }
    near = g_blast_near[3];
    if (BLAST_BLOCKED(near, height)) {
        g_blast_near[3] = 0;
        g_blast_far[6] = 0;
    }
    near = g_blast_near[4];
    if (BLAST_BLOCKED(near, height)) {
        g_blast_near[4] = 0;
        g_blast_far[7] = 0;
        g_blast_far[8] = 0;
        g_blast_far[9] = 0;
    }
    near = g_blast_near[5];
    if (BLAST_BLOCKED(near, height)) {
        g_blast_near[5] = 0;
        g_blast_far[10] = 0;
    }
    near = g_blast_near[6];
    if (BLAST_BLOCKED(near, height)) {
        g_blast_near[6] = 0;
        g_blast_far[11] = 0;
        g_blast_far[12] = 0;
        g_blast_far[13] = 0;
    }
    near = g_blast_near[7];
    if (BLAST_BLOCKED(near, height)) {
        g_blast_near[7] = 0;
        g_blast_far[14] = 0;
    }
    for (i = 0; i < BLAST_NEAR_CELLS; i++) {
        MapCell *target = g_blast_near[i];
        if (target != 0 && target->marker != 0) {
            if (target->marker == CELL_MARKER_PLAYER) {
                player_hit(BLAST_NEAR_DAMAGE, DEATH_HAZARD);
            } else if (target->marker != 0 && target->marker < CELL_MARKER_ACTOR_LIMIT) {
                Actor *actor = &g_actor_pool[target->occupant];
                if (actor->kind != ACTOR_KIND_BOSS)
                    actor_damage(actor, actor->health, 0);
                explosion_spawn(actor->position.vx, actor->position.vy + ACTOR_BLAST_HEIGHT, actor->position.vz);
            } else {
                LevelObject *object = &g_level_objects[target->occupant];
                if (!(object->flags & LEVEL_OBJECT_ARMED)) {
                    object->flags |= LEVEL_OBJECT_ARMED;
                    object->hits = (random() & BLAST_NEAR_HITS_MASK) + BLAST_NEAR_HITS_MIN;
                }
            }
        }
    }
    for (i = 0; i < BLAST_FAR_CELLS; i++) {
        MapCell *target = g_blast_far[i];
        if (target != 0 && target->marker != 0) {
            if (target->marker == CELL_MARKER_PLAYER) {
                player_hit(BLAST_FAR_DAMAGE, DEATH_HAZARD);
            } else if (target->marker != 0 && target->marker < CELL_MARKER_ACTOR_LIMIT) {
                Actor *actor = &g_actor_pool[target->occupant];
                if (actor->kind != ACTOR_KIND_BOSS)
                    actor_damage(actor, actor->health, 0);
                explosion_spawn(actor->position.vx, actor->position.vy + ACTOR_BLAST_HEIGHT, actor->position.vz);
            } else {
                LevelObject *object = &g_level_objects[target->occupant];
                if (!(object->flags & LEVEL_OBJECT_ARMED)) {
                    object->flags |= LEVEL_OBJECT_ARMED;
                    object->hits = (random() & BLAST_FAR_HITS_MASK) + BLAST_FAR_HITS_MIN;
                }
            }
        }
    }
}

void level_object_tick(int index, int elapsed)
{
    LevelObject *entry = &g_level_objects[(u8)index];
    u16 remaining;

    if (entry->flags & LEVEL_OBJECT_ARMED)
        return;
    remaining = entry->state - elapsed;
    entry->state = remaining;
    if ((short)remaining <= 0) {
        entry->flags |= LEVEL_OBJECT_ARMED;
        entry->hits = (random() & TIMER_HITS_MASK) + 1;
    }
}

int level_object_activate(u8 index, u8 status)
{
    LevelObject *entry = &g_level_objects[index];
    if (entry->state != 0) return 0;
    if (entry->type == LEVEL_OBJECT_SWITCH_BATTERY || entry->type == LEVEL_OBJECT_SWITCH_BATTERY_ALT) {
        if (g_player.batteries == 0) {
            hud_show_message("Battery required");
            return 0;
        }
        g_player.batteries--;
    }
    sound_play(SOUND_SWITCH, -1, SPU_VOLUME_MAX);
    g_player.tallies[entry->type]++;
    entry->state = status == SWITCH_STATE_ON ? SWITCH_STATE_ON : SWITCH_STATE_REARMS;
    return -1;
}

void level_object_mark_cells(LevelObject *block)
{
 MapCell *base=block->cell;
 s16 outer=0;
 if(block->depth==0) return;
 do {
  s16 inner=0;
  MapCell *row;
  if(inner<block->width) {
   row=base;
   do {
    row->marker=block->type;
    row->occupant=block->occupant;
    row++;
    inner++;
   } while(inner<block->width);
  }
  base+=g_level_map.width;
  outer++;
 } while(outer<block->depth);
}

void level_object_clear_cells(LevelObject *block)
{
    MapCell *base = block->cell;
    s16 outer = 0;
    if (block->depth == 0) return;
    do {
        MapCell *row = base;
        s16 inner = 0;
        if (inner < block->width) {
            do {
                row->marker = 0;
                row++;
                inner++;
            } while (inner < block->width);
        }
        base += g_level_map.width;
        outer++;
    } while (outer < block->depth);
}

/* name: inferred from use (level_objects_init): builds each level object from its record and starts its task */
void level_objects_create(LevelObjectRecord *record, s16 count)
{
    LevelObject *object = g_level_objects;
    s16 i;
    u_int x, z;
    TASK *task;
    MapCell *cell;

    g_level_object_records = record;
    g_level_object_count = count;
    for (i = 0; i < count; i++, record++, object++) {
        object->type = record->type;
        object->flags = record->flags;
        object->state = record->state << LEVEL_OBJECT_STATE_SHIFT;
        object->drop[0] = record->drop[0];
        object->drop[1] = record->drop[1];
        object->width = record->width;
        object->depth = record->depth;
        object->command_base = record->command_base;
        object->command_count = record->command_count;
        object->facing = record->facing;
        object->position.vx = (record->x << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_x;
        object->position.vz = (record->z << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_z;
        object->position.vy = floor_height(object->position.vx + g_level_map.offset_x,
                                           object->position.vz + g_level_map.offset_z);
        object->rotation.vx = object->rotation.vy = object->rotation.vz = 0;
        object->occupant = i;
        object->hits = 0;
        x = object->position.vx + g_level_map.offset_x;
        z = object->position.vz + g_level_map.offset_z;
        cell = MAP_CELL(x, z);
        object->cell = cell;
        cell->marker = object->type;
        cell->occupant = object->occupant;
        switch (object->facing) {
        case FACING_NEG_Z:
            object->rotation.vy = ONE / 2;
            break;
        case FACING_POS_X_NEG_Z:
            object->rotation.vy = ONE * 5 / 8;
            break;
        case FACING_POS_X:
            object->rotation.vy = ONE / 4;
            break;
        case FACING_POS_X_POS_Z:
            object->rotation.vy = -ONE / 8;
            break;
        case FACING_POS_Z:
            object->rotation.vy = 0;
            break;
        case FACING_NEG_X_POS_Z:
            object->rotation.vy = ONE / 8;
            break;
        case FACING_NEG_X:
            object->rotation.vy = -ONE / 4;
            break;
        case FACING_NEG_X_NEG_Z:
            object->rotation.vy = ONE * 3 / 8;
            break;
        }
        if (object->type < TALLY_COUNT)
            g_level_map.kind_counts[object->type]++;
        switch (object->type) {
        case LEVEL_OBJECT_EXPLOSIVE:
            task = TaskCreate(TASK_PRIORITY_OBJECTS, level_object_debris_task);
            object->model = g_level_object_models[LEVEL_OBJECT_MODEL_EXPLOSIVE];
            break;
        case LEVEL_OBJECT_BLOCK:
        case LEVEL_OBJECT_WALL:
        case LEVEL_OBJECT_BLOCK_SMALL:
            level_object_mark_cells(object);
            task = TaskCreate(TASK_PRIORITY_OBJECTS, block_object_task);
            break;
        case LEVEL_OBJECT_SWITCH:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate(TASK_PRIORITY_OBJECTS, switch_panel_task);
            if (g_level >= LEVEL_311_TUNNELS)
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_LATE];
            else
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH];
            task->state[1] = SWITCH_BLINK_TICKS;
            task->state[3] = 0;
            object->state = 0;
            break;
        case LEVEL_OBJECT_SWITCH_ALT:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate(TASK_PRIORITY_OBJECTS, switch_panel_task);
            if (g_level >= LEVEL_311_TUNNELS)
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_ALT_LATE];
            else
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_ALT];
            task->state[1] = SWITCH_BLINK_TICKS;
            task->state[3] = 0;
            object->state = 0;
            break;
        case LEVEL_OBJECT_SWITCH_BATTERY:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate(TASK_PRIORITY_OBJECTS, switch_panel_task);
            if (g_level >= LEVEL_311_TUNNELS)
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_LATE];
            else
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_BATTERY];
            task->state[1] = SWITCH_BLINK_TICKS;
            task->state[3] = 0;
            object->state = 0;
            break;
        case LEVEL_OBJECT_SWITCH_BATTERY_ALT:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate(TASK_PRIORITY_OBJECTS, switch_panel_task);
            if (g_level >= LEVEL_311_TUNNELS)
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT_LATE];
            else
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT];
            task->state[1] = SWITCH_BLINK_TICKS;
            task->state[3] = 0;
            object->state = 0;
            break;
        case LEVEL_OBJECT_TARGET:
            task = TaskCreate(TASK_PRIORITY_OBJECTS, level_object_hit_task);
            if (g_level >= LEVEL_311_TUNNELS)
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_TARGET_LATE];
            else
                object->model = g_level_object_models[LEVEL_OBJECT_MODEL_TARGET];
            break;
        case LEVEL_OBJECT_TARGET_ALT:
            task = TaskCreate(TASK_PRIORITY_OBJECTS, level_object_hit_task);
            object->model = g_level_object_models[LEVEL_OBJECT_MODEL_TARGET_ALT];
            break;
        case LEVEL_OBJECT_TILTING:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate(TASK_PRIORITY_OBJECTS, level_object_spin_task);
            object->state = 0;
            object->model = g_level_object_models[LEVEL_OBJECT_MODEL_TALL];
            break;
        case LEVEL_OBJECT_STATIC_FIRST ... LEVEL_OBJECT_STATIC_LAST:
            object->flags |= LEVEL_OBJECT_ARMED;
            task = TaskCreate(TASK_PRIORITY_OBJECTS, level_object_task);
            object->model = g_level_object_models[object->type - LEVEL_OBJECT_STATIC_FIRST + LEVEL_OBJECT_MODEL_STATIC];
            break;
        default:
            task = TaskCreate(TASK_PRIORITY_OBJECTS, level_object_hit_task);
            object->model = g_level_object_models[LEVEL_OBJECT_MODEL_TARGET];
            break;
        }
        if (object->model == 0)
            object->model = g_level_object_models[LEVEL_OBJECT_MODEL_TARGET];
        task->state[0] = (unsigned int)object;
    }
}

/* name: inferred from its body, which creates the level's pickups */
void level_objects_init(void)
{
    s16 count = g_level_map.pickup_count;
    if (count != 0) {
        level_objects_create(g_level_map.pickups, count);
    }
}

void level_object_draw(LevelObject *object)
{
    VECTOR scale;
    if ((object->cell->mask & g_visible_cell_mask) == 0)
        return;
    if (object->type == LEVEL_OBJECT_TARGET && g_level >= LEVEL_311_TUNNELS)
        g_model_uvs = g_panel_uvs;
    else
        g_model_uvs = g_pickup_uvs;
    LoadAverageShort12(&object->position, &g_view_position, ONE, -ONE, &g_relative_position);
    PushMatrix();
    ApplyMatrix(&g_view_matrix, &g_relative_position, &g_translation);
    if ((u_long)(g_translation.vz - 1) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE) {
        RotMatrix(&object->rotation, &g_matrix_work);
        MulMatrix2(&g_view_matrix, &g_matrix_work);
        if (object->type != LEVEL_OBJECT_EXPLOSIVE) {
            scale.vx = LEVEL_OBJECT_SCALE;
            scale.vy = LEVEL_OBJECT_SCALE;
            scale.vz = LEVEL_OBJECT_SCALE;
            ScaleMatrix(&g_matrix_work, &scale);
        }
        SetRotMatrix(&g_matrix_work);
        TransMatrix(&g_matrix_work, &g_translation);
        SetTransMatrix(&g_matrix_work);
        if (g_translation.vz < MODEL_NEAR_DISTANCE)
            model_draw_near_in_cell(object->model, object->cell);
        else if (g_translation.vz < MODEL_FAR_DISTANCE)
            model_draw_mid_in_cell(object->model, object->cell);
        else
            model_draw_far_in_cell(object->model, object->cell);
    }
    PopMatrix();
    g_model_uvs = g_sprite_uvs;
}

void level_object_draw_tall(LevelObject *object)
{
    VECTOR scale;
    if ((object->cell->mask & g_visible_cell_mask) == 0)
        return;
    g_model_uvs = g_panel_uvs;
    LoadAverageShort12(&object->position, &g_view_position, ONE, -ONE, &g_relative_position);
    PushMatrix();
    ApplyMatrix(&g_view_matrix, &g_relative_position, &g_translation);
    if ((u_long)(g_translation.vz - 1) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE) {
        RotMatrix(&object->rotation, &g_matrix_work);
        MulMatrix2(&g_view_matrix, &g_matrix_work);
        scale.vx = LEVEL_OBJECT_TALL_SCALE;
        scale.vy = LEVEL_OBJECT_TALL_SCALE_Y;
        scale.vz = LEVEL_OBJECT_TALL_SCALE;
        ScaleMatrix(&g_matrix_work, &scale);
        SetRotMatrix(&g_matrix_work);
        TransMatrix(&g_matrix_work, &g_translation);
        SetTransMatrix(&g_matrix_work);
        if (g_translation.vz < MODEL_CLOSE_DISTANCE)
            model_draw_near_in_cell(object->model, object->cell);
        else if (g_translation.vz < MODEL_FAR_DISTANCE)
            model_draw_mid_in_cell(object->model, object->cell);
        else
            model_draw_far_in_cell(object->model, object->cell);
    }
    PopMatrix();
    g_model_uvs = g_sprite_uvs;
}

void level_object_draw_unscaled(LevelObject *object)
{
    if ((object->cell->mask & g_visible_cell_mask) == 0)
        return;
    LoadAverageShort12(&object->position, &g_view_position, ONE, -ONE, &g_relative_position);
    PushMatrix();
    ApplyMatrix(&g_view_matrix, &g_relative_position, &g_translation);
    if ((u_long)(g_translation.vz - 1) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE) {
        RotMatrix(&object->rotation, &g_matrix_work);
        MulMatrix2(&g_view_matrix, &g_matrix_work);
        SetRotMatrix(&g_matrix_work);
        TransMatrix(&g_matrix_work, &g_translation);
        SetTransMatrix(&g_matrix_work);
        if (g_translation.vz < MODEL_CLOSE_DISTANCE)
            model_draw_near_in_cell(object->model, object->cell);
        else if (g_translation.vz < MODEL_FAR_DISTANCE)
            model_draw_mid_in_cell(object->model, object->cell);
        else
            model_draw_far_in_cell(object->model, object->cell);
    }
    PopMatrix();
}

/* Rule exception: `scale` is unused here; the 16-byte local is required for
 * retail's 0x40 frame (body otherwise matches level_object_draw_tall without ScaleMatrix). */
void level_object_task(TASK *task)
{
    LevelObject *object = (LevelObject *)task->state[0];
    VECTOR scale;
    object->position.vy = floor_height(object->position.vx + g_level_map.offset_x, object->position.vz + g_level_map.offset_z);
    if (object->cell->mask & g_visible_cell_mask) {
        g_model_uvs = g_panel_uvs;
        LoadAverageShort12(&object->position, &g_view_position, ONE, -ONE, &g_relative_position);
        PushMatrix();
        ApplyMatrix(&g_view_matrix, &g_relative_position, &g_translation);
        if ((u_long)(g_translation.vz - 1) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE) {
            RotMatrix(&object->rotation, &g_matrix_work);
            MulMatrix2(&g_view_matrix, &g_matrix_work);
            SetRotMatrix(&g_matrix_work);
            TransMatrix(&g_matrix_work, &g_translation);
            SetTransMatrix(&g_matrix_work);
            if (g_translation.vz < MODEL_NEAR_DISTANCE)
                model_draw_near_in_cell(object->model, object->cell);
            else if (g_translation.vz < MODEL_FAR_DISTANCE)
                model_draw_mid_in_cell(object->model, object->cell);
            else
                model_draw_far_in_cell(object->model, object->cell);
        }
        PopMatrix();
        g_model_uvs = g_sprite_uvs;
    }
    g_next_task = task->Next;
}

void level_object_spin_task(TASK *task)
{
    LevelObject *object = (LevelObject *)task->state[0];
    object->position.vy = floor_height(object->position.vx + g_level_map.offset_x, object->position.vz + g_level_map.offset_z);
    level_object_draw_unscaled(object);
    if (object->state && object->rotation.vz >= LEVEL_OBJECT_TILT_LIMIT)
        object->rotation.vz -= LEVEL_OBJECT_TILT_STEP;
    g_next_task = task->Next;
}

void level_object_hit_task(TASK *task)
{
    LevelObject *object = (LevelObject *)task->state[0];
    object->position.vy = floor_height(object->position.vx + g_level_map.offset_x, object->position.vz + g_level_map.offset_z);
    if (object->flags & LEVEL_OBJECT_ARMED) {
        s16 remaining = object->hits;
        remaining--;
        if (remaining == 0) {
            MapCell *cell = object->cell;
            g_player.tallies[object->type]++;
            cell->marker = 0;
            if (object->drop[0] != LEVEL_OBJECT_NO_DROP) {
                if ((object->flags & LEVEL_OBJECT_DROPS_ACTOR)==0)
                    level_object_drop_item(object, object->drop[0]);
                else
                    actor_spawn_from_object(object, object->drop[0]);
            }
            if (object->drop[1] != LEVEL_OBJECT_NO_DROP)
                level_object_drop_item(object, object->drop[1]);
            explosion_spawn(object->position.vx, object->position.vy,
                    object->position.vz);
            panel_list_walk(object);
            TaskDie(task);
            return;
        }
        object->hits = remaining;
    }
    level_object_draw(object);
    g_next_task = task->Next;
}

void level_object_debris_task(TASK *task)
{
    LevelObject *object = (LevelObject *)task->state[0];
    object->position.vy = floor_height(object->position.vx + g_level_map.offset_x, object->position.vz + g_level_map.offset_z);
    if (object->flags & LEVEL_OBJECT_ARMED) {
        s16 remaining = object->hits;
        remaining--;
        if (remaining == 0) {
            MapCell *cell = object->cell;
            g_player.tallies[object->type]++;
            cell->marker = 0;
            big_explosion_spawn(object->position.vx, object->position.vy - EXPLOSIVE_BLAST_DROP,
                    object->position.vz);
            panel_list_walk(object);
            explosion_damage(&object->position, object->cell);
            TaskDie(task);
            return;
        }
        object->hits = remaining;
    }
    level_object_draw(object);
    g_next_task = task->Next;
}

/* Tallies a destroyed block object and bursts explosions on the side facing the player. */
/* name: static evidence, analysis/runtime/functions.json */
void block_object_explode(LevelObject *object)
{
    SVECTOR position;
    short count;
    s16 i;
    g_player.tallies[object->type]++;
    position.vx = object->position.vx + (object->width - 1) * MAP_CELL_HALF;
    position.vy = object->position.vy + BLOCK_BLAST_HEIGHT;
    position.vz = object->position.vz + (object->depth - 1) * MAP_CELL_HALF;
    count = BLOCK_BLAST_COUNT;
    if (object->type == LEVEL_OBJECT_BLOCK_SMALL)
        count = 1;
    switch (object->facing) {
    case FACING_NEG_Z:
    case FACING_POS_Z:
        if (position.vz < g_player.position.vz) {
            position.vz += MAP_CELL_SIZE;
            for (i = 0; i < count; i++) {
                s16 dx = (random() & MAP_CELL_MASK) - MAP_CELL_HALF;
                s16 dy = (random() & (BLOCK_BLAST_SPREAD_Y - 1)) - BLOCK_BLAST_SPREAD_Y / 2;
                explosion_spawn(position.vx + dx, position.vy + dy, position.vz);
            }
        } else {
            position.vz -= MAP_CELL_SIZE;
            for (i = 0; i < count; i++) {
                s16 dx = (random() & MAP_CELL_MASK) - MAP_CELL_HALF;
                s16 dy = (random() & (BLOCK_BLAST_SPREAD_Y - 1)) - BLOCK_BLAST_SPREAD_Y / 2;
                explosion_spawn(position.vx + dx, position.vy + dy, position.vz);
            }
        }
        break;
    case FACING_POS_X:
    case FACING_NEG_X:
        if (position.vx < g_player.position.vx) {
            position.vx += MAP_CELL_SIZE;
            for (i = 0; i < count; i++) {
                s16 dz = (random() & MAP_CELL_MASK) - MAP_CELL_HALF;
                s16 dy = (random() & (BLOCK_BLAST_SPREAD_Y - 1)) - BLOCK_BLAST_SPREAD_Y / 2;
                explosion_spawn(position.vx, position.vy + dy, position.vz + dz);
            }
        } else {
            position.vx -= MAP_CELL_SIZE;
            for (i = 0; i < count; i++) {
                s16 dz = (random() & MAP_CELL_MASK) - MAP_CELL_HALF;
                s16 dy = (random() & (BLOCK_BLAST_SPREAD_Y - 1)) - BLOCK_BLAST_SPREAD_Y / 2;
                explosion_spawn(position.vx, position.vy + dy, position.vz + dz);
            }
        }
        break;
    default:
        explosion_spawn(position.vx, position.vy, position.vz);
        break;
    }
}


/* Task of a destructible block object (types 0x15, 0x1c, 0x1d): when its hits run out it
 * explodes (0x1c instead runs wall_shatter with a sound), clears its cells, raises its
 * command states and drops its items. */
/* name: runtime evidence, analysis/runtime/functions.json */
void block_object_task(TASK *task)
{
    LevelObject *object = (LevelObject *)task->state[0];
    s16 i;
    if (object->flags & LEVEL_OBJECT_ARMED) {
        s16 remaining = object->hits;
        remaining--;
        if (remaining == 0) {
            if (object->type != LEVEL_OBJECT_WALL) {
                block_object_explode(object);
                level_object_clear_cells(object);
                for (i = 0; i < object->command_count; i++)
                    command_state_add(1, object->command_base + i);
                if (object->drop[0] != LEVEL_OBJECT_NO_DROP) {
                    if ((object->flags & LEVEL_OBJECT_DROPS_ACTOR) == 0)
                        level_object_drop_item(object, object->drop[0]);
                    else
                        actor_spawn_from_object(object, object->drop[0]);
                }
                if (object->drop[1] != LEVEL_OBJECT_NO_DROP)
                    level_object_drop_item(object, object->drop[1]);
            } else {
                wall_shatter(object);
                sound_play_at(SOUND_WALL_SHATTER, &object->position, SOUND_AT_FULL);
                level_object_clear_cells(object);
                for (i = 0; i < object->command_count; i++)
                    command_state_add(1, object->command_base + i);
            }
            TaskDie(task);
            return;
        }
        object->hits = remaining;
    }
    g_next_task = task->Next;
}

/* Picks frame of a switch panel's model set by its type (the _LATE sets from LEVEL_311_TUNNELS on). */
#define LEVEL_OBJECT_SET_FRAME(object, frame) \
    if ((object)->type == LEVEL_OBJECT_SWITCH) { \
        if (g_level >= LEVEL_311_TUNNELS) \
            (object)->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_LATE + (frame)]; \
        else \
            (object)->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH + (frame)]; \
    } else if ((object)->type == LEVEL_OBJECT_SWITCH_ALT) { \
        if (g_level >= LEVEL_311_TUNNELS) \
            (object)->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_ALT_LATE + (frame)]; \
        else \
            (object)->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_ALT + (frame)]; \
    } else if ((object)->type == LEVEL_OBJECT_SWITCH_BATTERY) { \
        if (g_level >= LEVEL_311_TUNNELS) \
            (object)->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_LATE + (frame)]; \
        else \
            (object)->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_BATTERY + (frame)]; \
    } else { \
        if (g_level >= LEVEL_311_TUNNELS) \
            (object)->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT_LATE + (frame)]; \
        else \
            (object)->model = g_level_object_models[LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT + (frame)]; \
    }

/* Task of an activated switch panel: blinks frames 2/3 while state[2] counts down, then
 * holds frame 3; when the object's state returns to 2 it goes back to switch_panel_task. */
/* name: static evidence, analysis/runtime/functions.json */
void switch_panel_active_task(TASK *task)
{
    LevelObject *object = (LevelObject *)task->state[0];
    object->position.vy = floor_height(object->position.vx + g_level_map.offset_x, object->position.vz + g_level_map.offset_z);
    level_object_draw_tall(object);
    if (task->state[2] != 0) {
        if (task->state[1] == 0) {
            if (--task->state[2] == 0) {
                LEVEL_OBJECT_SET_FRAME(object, SWITCH_FRAME_HELD);
                task->state[1] = SWITCH_HOLD_TICKS;
            } else {
                s16 frame;
                task->state[1] = SWITCH_ACTIVE_BLINK_TICKS;
                frame = task->state[3] ^ 1;
                task->state[3] = frame;
                LEVEL_OBJECT_SET_FRAME(object, SWITCH_FRAME_ACTIVE + frame);
            }
        } else {
            task->state[1]--;
        }
    } else if (task->state[1] != 0) {
        if (--task->state[1] == 0 && object->state == SWITCH_STATE_REARMS) {
            object->state = 0;
            task->state[1] = SWITCH_BLINK_TICKS;
            task->state[3] = 0;
            task->callback = switch_panel_task;
            LEVEL_OBJECT_SET_FRAME(object, SWITCH_FRAME_HELD);
        }
    }
    g_next_task = task->Next;
}

/* Task of an idle switch panel (types 0x16, 0x18, 0x1a, 0x1b): blinks frames 0/1; once
 * activated (state) it shows frame 2, sounds and hands over to switch_panel_active_task. */
/* name: runtime evidence, analysis/runtime/functions.json */
void switch_panel_task(TASK *task)
{
    LevelObject *object = (LevelObject *)task->state[0];
    object->position.vy = floor_height(object->position.vx + g_level_map.offset_x, object->position.vz + g_level_map.offset_z);
    level_object_draw_tall(object);
    if (object->state == 0) {
        if (task->state[1] == 0) {
            s16 frame;
            task->state[1] = SWITCH_BLINK_TICKS;
            frame = task->state[3] ^ 1;
            task->state[3] = frame;
            LEVEL_OBJECT_SET_FRAME(object, SWITCH_FRAME_IDLE + frame);
        } else {
            task->state[1]--;
        }
    } else {
        task->state[3] = 0;
        task->state[1] = SWITCH_ACTIVE_BLINK_TICKS;
        task->state[2] = SWITCH_ACTIVE_BLINKS;
        LEVEL_OBJECT_SET_FRAME(object, SWITCH_FRAME_ACTIVE);
        task->callback = switch_panel_active_task;
        sound_play(SOUND_SWITCH, -1, SPU_VOLUME_MAX);
    }
    g_next_task = task->Next;
}
