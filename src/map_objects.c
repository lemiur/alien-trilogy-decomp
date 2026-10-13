#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LEVEL.H>
#include <MODEL.H>
#include <CAMERA.H>
#include <VIEW.H>
#include <SPRITEUV.H>
#include <TEXTURE.H>
#include <TASK.H>
#include <AUDIO.H>
#include <HEIGHT.H>
#include <LIBCD.H>
#include <HEAPFILE.H>
#include <HEAP.H>
#include <TEXT.H>
#include <HUD.H>
#include <TRACK.H>
#include <SPRITE.H>
#include <ACTOR.H>
#include <GAME.H>
#include <ITEMS.H>

extern MapObject *g_map_object_pool;
extern MapObject *g_map_object_free;
extern MapObject *g_map_objects;
/* ScaleMatrix scale of each object kind (map_object_draw). name: invented */
static VECTOR g_map_object_scales[] = {
    { 0x1000, 0x1000, 0x1000, 0 }, { 0x1000, 0x1000, 0x1000, 0 }, { 0x1000, 0x1000, 0x1000, 0 },
    { 0x1000, 0x1000, 0x1000, 0 }, { 0x1400, 0x1400, 0x1400, 0 }, { 0x400, 0x400, 0x400, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0xc00, 0xc00, 0xc00, 0 },
    { 0x2c00, 0x2c00, 0x2c00, 0 }, { 0x2c00, 0x2c00, 0x2c00, 0 }, { 0x2800, 0x2800, 0x2800, 0 },
    { 0x2000, 0x2000, 0x2000, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 }, { 0x1c00, 0x1c00, 0x1c00, 0 },
    { 0x1c00, 0x1c00, 0x1c00, 0 }
};
/* name: invented; map_objects_spawn keeps the level's object records for map_object_trigger */
extern ObjectRecord *g_object_records;
/* name: invented; the number of g_object_records */
extern short g_object_record_count;
extern void map_object_move(MapObject *object);
/* name: inferred from use: the HUD message per pickup kind ("9mm automatic", ...) */
/* name: analysis/runtime/globals.json (round 2) */
static char *g_pickup_messages[] = {
    "9mm automatic", "Shotgun", "Pulse rifle", "Flamethrower",
    "Smart Gun", "NOT USED", "Seismic charges", "Battery",
    "Night vision goggles", "9mm ammunition clip", "Shotgun cartridges", "Pulse rifle clip",
    "Grenades", "Flamethower fuel", "Smartgun ammunition", "Identity tag",
    "Auto mapper", "Hypo Pack", "Acid vest", "Body suit",
    "MediKit", "DermPatch", "Protective boots", "Adreneline burst",
    "NOT USED", "Shoulder lamp", "NOT USED", "NOT USED"
};
/* name: invented; height of each object kind above the floor */
static u_short g_object_heights[] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 96, 64, 64, 64, 64, 64, 64, 64
};
extern void map_object_unlink(MapObject *node);
extern int map_object_near_player(MapObject *object);
extern void map_object_draw(MapObject *object);

/* name: inferred from its body, which pushes an object onto the free list */
void map_object_free(MapObject *node)
{
    node->next = g_map_object_free;
    g_map_object_free = node;
}

/* name: inferred from its body, which threads the 128-object pool onto the free list */
void map_objects_reset(void)
{
    MapObject *node;
    int i;

    g_map_object_free = 0;
    g_map_objects = 0;
    node = g_map_object_pool;
    for (i = 0; i < MAP_OBJECT_MAX; ++i) {
        map_object_free(node);
        ++node;
    }
}

/* The face kind is the halfword over flags and selector until this splits it.
 * name: inferred from its body (door_model_faces_fixup with palette entry 10): turns an
 * object model's vertex indices into pointers and each face kind into flags and a
 * dispatch selector */
void object_model_faces_fixup(DispatchRow *face, SVECTOR *vertices)
{
    int index;
    short last;

    do {
        index = (int)face->vertices[0];
        face->vertices[0] = vertices + index;
        index = (int)face->vertices[1];
        face->vertices[1] = vertices + index;
        index = (int)face->vertices[2];
        face->vertices[2] = vertices + index;
        index = (int)face->vertices[3];
        face->vertices[3] = vertices + index;
        last = *(u_short *)&face->flags & ROW_FLAG_LAST;
        switch ((short)(*(u_short *)&face->flags & ~ROW_FLAG_LAST)) {
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
        face->flags = last ? ROW_FLAG_LAST | MODEL_PALETTE : MODEL_PALETTE;
    } while (!(face++->flags & ROW_FLAG_LAST));
}

/* name: invented; reads one of the BND file's decimal fields */
static inline short read_number(u_char *text)
{
    return parse_decimal(text);
}

/* name: inferred from its body, which loads \GFX\PICKMOD.BND (g_file_table[FILE_PICKMOD_BND]) into g_object_models */
void pickup_models_load(void)
{
    short i;
    short count;
    short index;
    u_int size;
    u_char *p;
    DispatchRow *rows;
    u_char text[BND_COUNT_DIGITS + 1];

    g_map_object_pool = heap_alloc(MAP_OBJECT_MAX * sizeof(MapObject));
    for (i = 0; i < PICKUP_KIND_COUNT; i++)
        g_object_models[i] = 0;
    load_file(&g_file_table[FILE_PICKMOD_BND]);
    p = g_file_table[FILE_PICKMOD_BND].buffer;
    p += BND_HEADER_SIZE;
    text[0] = *p++;
    text[1] = *p++;
    text[2] = *p++;
    text[3] = *p++;
    text[BND_COUNT_DIGITS] = 0;
    count = read_number(text);
    for (i = 0; i < count; i++) {
        p++;
        text[0] = *p++;
        text[1] = *p++;
        text[2] = *p++;
        text[BND_INDEX_DIGITS] = 0;
        index = read_number(text);
        size = read_be32(p);
        p += BND_LENGTH_SIZE;
        if (size & 1)
            size++;
        rows = ((ModelBlock *)p)->rows;
        object_model_faces_fixup(rows, (SVECTOR *)(rows + ((ModelBlock *)p)->row_count));
        p += size;
        g_object_models[index] = rows;
    }
}

MapObject *map_object_alloc(void)
{
    MapObject *first;

    if ((first = g_map_object_free))
        g_map_object_free = first->next;

    return first;
}

void map_object_link(MapObject *node)
{
    MapObject *old = g_map_objects;
    if (old) old->prev = node;
    g_map_objects = node;
    node->next = old;
    node->prev = 0;
}

/* Multi-statement macro (name invented): takes a pool object and places a record's object on
 * its floor cell. A macro, not an inline function: an inline's integrated RTL substitutes the
 * constant &g_level_map.offset_x and loses retail's pointer register. */
#define MAP_OBJECT_SPAWN(record) \
    { \
        MapObject *object = map_object_alloc(); \
        u_short *offset_x; \
        u_int x, z; /* map position of the object, for MAP_CELL */ \
        object->kind = (record)->kind; \
        if (object->kind == PICKUP_UNUSED_24) \
            object->kind = PICKUP_DERM_PATCH; \
        object->amount = (record)->amount; \
        object->clips = (record)->clips; \
        offset_x = &g_level_map.offset_x; \
        object->position.vx = ((record)->x << MAP_CELL_SHIFT) + MAP_CELL_HALF - *offset_x; \
        object->position.vz = ((record)->z << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_z; \
        object->position.vy = floor_height(object->position.vx + *offset_x, \
                                           object->position.vz + g_level_map.offset_z) \
                              + g_object_heights[object->kind]; \
        x = object->position.vx + *offset_x; \
        z = object->position.vz + g_level_map.offset_z; \
        object->cell = MAP_CELL(x, z); \
        object->rotation.vx = 0; \
        object->rotation.vy = 0; \
        object->rotation.vz = 0; \
        object->velocity.vx = 0; \
        object->velocity.vy = 0; \
        object->velocity.vz = 0; \
        object->model = g_object_models[object->kind]; \
        map_object_link(object); \
    }

/* name: inferred from its body, which keeps the level's object records and spawns the ones needing no trigger */
void map_objects_spawn(ObjectRecord *records, short count)
{
    g_object_records = records;
    g_object_record_count = count;
    while (count-- > 0U) {
        records->triggers = 0;
        if (records->kind == PICKUP_IDENTITY_TAG)
            g_level_map.kind_counts[TALLY_IDENTITY_TAGS]++;
        if (records->required == 0)
            MAP_OBJECT_SPAWN(records);
        records++;
    }
}

/* name: inferred from its body, which resets the map object pool and spawns the level's objects */
void map_objects_init(void)
{
    map_objects_reset();
    if (g_level_map.object_record_count)
        map_objects_spawn(g_level_map.object_records, g_level_map.object_record_count);
}

/* name: inferred from its body, which raises a record's trigger count and spawns its object when it is reached */
void map_object_trigger(int amount, u_char index)
{
    ObjectRecord *record = &g_object_records[index];
    record->triggers += amount;
    if (record->triggers > record->required)
        record->triggers = record->required;
    if (record->triggers == record->required)
        MAP_OBJECT_SPAWN(record);
}

/* actor_drop_item raises the trigger count of the actor's object record and throws its object out of the actor */
/* name: static evidence, analysis/runtime/functions.json */
void actor_drop_item(Actor *actor)
{
    ObjectRecord *record;
    MapObject *object;
    u_short *offset_x;
    u_int x, z;

    if (g_object_record_count == 0)
        return;
    record = &g_object_records[actor->attached_index];
    record->triggers += 1;
    if (record->triggers > record->required)
        record->triggers = record->required;
    if (record->triggers != record->required)
        return;
    object = map_object_alloc();
    object->kind = record->kind;
    if (object->kind == PICKUP_UNUSED_24)
        object->kind = PICKUP_DERM_PATCH;
    object->amount = record->amount;
    object->clips = record->clips;
    object->position.vx = actor->position.vx;
    object->position.vz = actor->position.vz;
    offset_x = &g_level_map.offset_x;
    object->position.vy = floor_height(object->position.vx + *offset_x,
                                       object->position.vz + g_level_map.offset_z) + THROW_DROP_HEIGHT;
    x = object->position.vx + *offset_x;
    z = object->position.vz + g_level_map.offset_z;
    object->cell = MAP_CELL(x, z);
    switch (actor->direction) {
    case FACING_NEG_Z:
        object->velocity.vx = 0;
        object->velocity.vz = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        break;
    case FACING_POS_X_NEG_Z:
        object->velocity.vx = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        object->velocity.vz = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        break;
    case FACING_POS_X:
        object->velocity.vx = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        object->velocity.vz = 0;
        break;
    case FACING_POS_X_POS_Z:
        object->velocity.vx = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        object->velocity.vz = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        break;
    case FACING_POS_Z:
        object->velocity.vx = 0;
        object->velocity.vz = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        break;
    case FACING_NEG_X_POS_Z:
        object->velocity.vx = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        object->velocity.vz = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        break;
    case FACING_NEG_X:
        object->velocity.vx = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        object->velocity.vz = 0;
        break;
    case FACING_NEG_X_NEG_Z:
        object->velocity.vx = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        object->velocity.vz = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        break;
    }
    /* (u_short): retail adds the jitter through the constant -THROW_JITTER_BIAS as a halfword */
    object->velocity.vx += ((u_short)random() & THROW_JITTER_MASK) - THROW_JITTER_BIAS;
    object->velocity.vz += ((u_short)random() & THROW_JITTER_MASK) - THROW_JITTER_BIAS;
    object->velocity.vy = THROW_LIFT_SPEED;
    object->bounce_speed = THROW_BOUNCE_SPEED;
    SET_VECTOR_TO_ZERO(object->rotation);
    object->model = g_object_models[object->kind];
    map_object_link(object);
}

/* level_object_drop_item raises the trigger count of a destroyed level object's drop record and throws its object out */
/* name: static evidence, analysis/runtime/functions.json */
void level_object_drop_item(LevelObject *source, u_char index)
{
    ObjectRecord *record;
    MapObject *object;
    u_short *offset_x;
    u_int x, z;

    if (g_object_record_count == 0)
        return;
    record = &g_object_records[index];
    record->triggers += 1;
    if (record->triggers > record->required)
        record->triggers = record->required;
    if (record->triggers != record->required)
        return;
    object = map_object_alloc();
    object->kind = record->kind;
    if (object->kind == PICKUP_UNUSED_24)
        object->kind = PICKUP_DERM_PATCH;
    object->amount = record->amount;
    object->clips = record->clips;
    object->position.vx = source->position.vx;
    object->position.vz = source->position.vz;
    offset_x = &g_level_map.offset_x;
    object->position.vy = floor_height(object->position.vx + *offset_x,
                                       object->position.vz + g_level_map.offset_z) + g_object_heights[object->kind];
    x = object->position.vx + *offset_x;
    z = object->position.vz + g_level_map.offset_z;
    object->cell = MAP_CELL(x, z);
    switch (source->facing) {
    case FACING_NEG_Z:
        object->velocity.vx = 0;
        object->velocity.vz = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        break;
    case FACING_POS_X_NEG_Z:
        object->velocity.vx = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        object->velocity.vz = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        break;
    case FACING_POS_X:
        object->velocity.vx = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        object->velocity.vz = 0;
        break;
    case FACING_POS_X_POS_Z:
        object->velocity.vx = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        object->velocity.vz = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        break;
    case FACING_POS_Z:
        object->velocity.vx = 0;
        object->velocity.vz = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        break;
    case FACING_NEG_X_POS_Z:
        object->velocity.vx = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        object->velocity.vz = (random() & THROW_SPEED_MASK) + THROW_SPEED_MIN;
        break;
    case FACING_NEG_X:
        object->velocity.vx = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        object->velocity.vz = 0;
        break;
    case FACING_NEG_X_NEG_Z:
        object->velocity.vx = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        object->velocity.vz = -((random() & THROW_SPEED_MASK) + THROW_SPEED_MIN);
        break;
    }
    /* (u_short): retail adds the jitter through the constant -THROW_JITTER_BIAS as a halfword */
    object->velocity.vx += ((u_short)random() & THROW_JITTER_MASK) - THROW_JITTER_BIAS;
    object->velocity.vz += ((u_short)random() & THROW_JITTER_MASK) - THROW_JITTER_BIAS;
    object->velocity.vy = THROW_LIFT_SPEED;
    object->bounce_speed = THROW_BOUNCE_SPEED;
    SET_VECTOR_TO_ZERO(object->rotation);
    object->model = g_object_models[object->kind];
    map_object_link(object);
}

void map_object_unlink(MapObject *node)
{
    MapObject *next = node->next;
    MapObject *prev = node->prev;
    if (next) next->prev = prev;
    if (prev) prev->next = next;
    else g_map_objects = next;
    node->prev = 0;
    node->next = 0;
}

int map_object_near_player(MapObject *object)
{
    int delta[3];
    delta[0] = object->position.vx - g_player.position.vx;
    delta[1] = object->position.vy - g_player.position.vy;
    delta[2] = object->position.vz - g_player.position.vz;
    if (delta[0] < 0) delta[0] = -delta[0];
    if (delta[1] < 0) delta[1] = -delta[1];
    if (delta[2] < 0) delta[2] = -delta[2];
    return delta[0] < PICKUP_REACH && delta[1] < PICKUP_REACH_Y && delta[2] < PICKUP_REACH;
}

void map_object_draw(MapObject *object)
{
    if (object->model == 0)
        return;
    if ((object->cell->mask & g_visible_cell_mask) == 0)
        return;
    g_model_uvs = g_pickup_uvs;
    LoadAverageShort12(&object->position, &g_view_position, ONE, -ONE, &g_relative_position);
    PushMatrix();
    ApplyMatrix(&g_view_matrix, &g_relative_position, &g_translation);
    if ((u_long)(g_translation.vz - 1) < MODEL_CULL_DISTANCE - 1 && g_translation.vx < MODEL_CULL_DISTANCE) {
        RotMatrix(&object->rotation, &g_matrix_work);
        MulMatrix2(&g_view_matrix, &g_matrix_work);
        ScaleMatrix(&g_matrix_work, &g_map_object_scales[object->kind]);
        SetRotMatrix(&g_matrix_work);
        TransMatrix(&g_matrix_work, &g_translation);
        SetTransMatrix(&g_matrix_work);
        if (g_translation.vz < MODEL_FAR_DISTANCE)
            model_draw_mid_in_cell(object->model, object->cell);
        else
            model_draw_far_in_cell(object->model, object->cell);
    }
    g_model_uvs = g_sprite_uvs;
    object->rotation.vy = (object->rotation.vy + MAP_OBJECT_SPIN_STEP) & (ONE - 1);
    PopMatrix();
}

/* Multi-statement macro (name invented): takes an object off the live list and returns it to the pool.
 * Its do { } while (0) block gives retail's register order (object in s1, sound in s2). */
#define MAP_OBJECT_REMOVE(object) do { \
    map_object_unlink(object); \
    map_object_free(object); \
} while (0)

/* Rule exception (config/exceptions.json): the loop is entered at its test through a goto, as
 * retail is (no copy of the test before the first object).
 * name: inferred from its body: the pickup task; an object the player touches and can use is
 * announced, sounded, removed and handed to player_give_pickup, every other object is drawn */
void pickups_task(TASK *task)
{
    MapObject *object;
    MapObject *next;
    short taken;
    short sound = SOUND_PICKUP;

    object = g_map_objects;
    goto next_object;
object_loop:
    map_object_move(object);
    next = object->next;
    taken = 0;
    if (map_object_near_player(object)) {
        switch (object->kind) {
        case PICKUP_PISTOL:
            taken = -1;
            sound = SOUND_PISTOL_PICKUP;
            break;
        case PICKUP_SHOTGUN:
            taken = -1;
            sound = SOUND_SHOTGUN_PUMP;
            break;
        case PICKUP_PULSE_RIFLE:
            taken = -1;
            sound = SOUND_PULSE_RIFLE_PICKUP;
            break;
        case PICKUP_FLAMETHROWER:
            taken = -1;
            sound = SOUND_FLAMETHROWER_PICKUP;
            break;
        case PICKUP_SMART_GUN:
            taken = -1;
            sound = SOUND_SMART_GUN_PICKUP;
            break;
        case PICKUP_AUTO_MAPPER:
            taken = !(g_player.flags & PLAYER_HAS_AUTO_MAPPER);
            break;
        case PICKUP_ACID_VEST:
        case PICKUP_BODY_SUIT:
            taken = object->amount > g_player.armour;
            break;
        case PICKUP_MEDIKIT:
            taken = g_player.health < PLAYER_HEALTH_MAX;
            break;
        case PICKUP_ADRENALINE_BURST:
            taken = g_player.health < PLAYER_HEALTH_ADRENALINE;
            break;
        case PICKUP_PISTOL_CLIP:
            taken = g_player.pistol_ammo < PISTOL_CLIP_ROUNDS || g_player.pistol_clips < CLIPS_MAX;
            sound = SOUND_PISTOL_PICKUP;
            break;
        case PICKUP_SHOTGUN_CARTRIDGES:
            taken = g_player.shotgun_ammo < SHOTGUN_AMMO_MAX;
            sound = SOUND_SHOTGUN_PUMP;
            break;
        case PICKUP_PULSE_RIFLE_CLIP:
            taken = g_player.pulse_rifle_ammo < PULSE_RIFLE_CLIP_ROUNDS || g_player.pulse_rifle_clips < CLIPS_MAX;
            sound = SOUND_PULSE_RIFLE_PICKUP;
            break;
        case PICKUP_FLAMETHROWER_FUEL:
            taken = g_player.flamethrower_fuel < FLAMETHROWER_FUEL_MAX;
            sound = SOUND_FLAMETHROWER_PICKUP;
            break;
        case PICKUP_SMART_GUN_AMMO:
            taken = g_player.smart_gun_ammo < SMART_GUN_CLIP_ROUNDS || g_player.smart_gun_clips < CLIPS_MAX;
            sound = SOUND_SMART_GUN_PICKUP;
            break;
        case PICKUP_GRENADES:
            taken = g_player.pulse_rifle_grenades < PULSE_RIFLE_GRENADES_MAX;
            sound = SOUND_GRENADES_PICKUP;
            break;
        case PICKUP_SEISMIC_CHARGES:
        case PICKUP_BATTERY:
        case PICKUP_NIGHT_VISION:
        case PICKUP_IDENTITY_TAG:
        case PICKUP_HYPO_PACK:
        case PICKUP_DERM_PATCH:
        case PICKUP_PROTECTIVE_BOOTS:
        case PICKUP_SHOULDER_LAMP:
            taken = -1;
            break;
        case PICKUP_UNUSED_5:
        case PICKUP_UNUSED_24:
            break;
        }
    }
    if (taken) {
        hud_show_message(g_pickup_messages[object->kind]);
        sound_play(sound, -1, SPU_VOLUME_MAX);
        MAP_OBJECT_REMOVE(object);
        player_give_pickup(object->kind, object->amount, object->clips);
    } else {
        map_object_draw(object);
    }
    object = next;
next_object:
    if (object)
        goto object_loop;
    g_next_task = task->Next;
}
