/*
 * The loaded level map at 0x80096060 (g_level_map; name invented: level_parse_map fills it from the
 * level's .MAP file with the cell grid, faces, vertices, actor slots, object/door/lift records, triggers,
 * script and lights, and the player's start cell). Requires <sys/types.h> and <LIBGTE.H>.
 * Retail forms &offset_x as 0x800960a6 + 0x26 and reaches width (-4), offset_z (+2)
 * and cells (+0xc) from that base, so 0x80096060..0x8009610f is one object. The word at
 * 0x80096110 continues the count/pointer pairing (state_count, command_count, light_count at
 * +0x70..+0x74 with command_starts, commands, lights at +0xa8..+0xb0), and lights_load passes it
 * together with light_count, so the object ends at 0x80096113.
 * Names without evidence are marked invented; unidentified members are identity-TODO.
 */
#ifndef LEVEL_H
#define LEVEL_H

typedef struct MapCell {
    struct DispatchRow *face; /* see <MODEL.H> */
    u_char occupant; /* actor index of the actor in the cell (actor_move, actor_line_of_sight) or a level object's occupant value; particles hit it (level_object_tick) */
    u_char secret_index;
    u_char layout_index; /* invented name: copied to Actor.layout_index when an actor enters the cell (actor_move) */
    u_char light;
    u_short mask;
    u_char type;
    u_char value;
    u_char marker;
    u_char ceiling; /* invented name: door code moves it, level_fixup_pointers defaults it to value + 0x30 */
    u_char palette; /* invented name: 16-byte palette entry the cell's models are lit with (model_draw_far_in_cell, particle_draw_debris) */
    u_char trigger; /* invented name: the cell's g_cell_triggers record, numbered from 1; 0 for none (player_cell_trigger, actor_cell_trigger, shot_cell_trigger, enemy_shot_cell_trigger) */
} MapCell;

/* Map geometry. A cell is 0x200 map units square (map position >> 9 is the cell column or
 * row); heights are MapCell.value steps of 0x20 units (floor_height, ceiling_height).
 * names: invented */
#define MAP_CELL_SHIFT 9
#define MAP_CELL_SIZE 0x200
#define MAP_CELL_HALF 0x100 /* a cell's centre from its corner */
#define MAP_CELL_MASK 0x1ff /* position within its cell */
#define MAP_HEIGHT_SHIFT 5
#define MAP_HEIGHT_STEP 0x20
#define MAP_HEIGHT_HALF_STEP 0x10 /* doors, lifts: centre of a height step */
#define MAP_DEFAULT_CEILING 0x30 /* steps above value of a cell whose ceiling is 0 (level_fixup_pointers) */
/* Slope cells (floor_height): a ramp climbs MAP_SLOPE_RISE height steps across its cell, one
 * height unit per 1 << MAP_SLOPE_SHIFT map units; a step cell's raised half is MAP_SLOPE_RISE
 * steps up. names: invented */
#define MAP_SLOPE_RISE 4
#define MAP_SLOPE_SHIFT 2

/* Octant facings (LevelObject.facing, DoorRecord.facing, ActorSlot.direction, Actor.direction
 * and, relative to the view, Actor.shown_direction): the octant of an angle, named by the
 * direction actor_drop_item and level_object_drop_item throw an object for each. Doors use
 * the four axis facings. names: invented */
enum {
    FACING_NEG_Z = 0,
    FACING_POS_X_NEG_Z = 1,
    FACING_POS_X = 2,
    FACING_POS_X_POS_Z = 3,
    FACING_POS_Z = 4,
    FACING_NEG_X_POS_Z = 5,
    FACING_NEG_X = 6,
    FACING_NEG_X_NEG_Z = 7
};
#define FACING_MASK 7 /* an octant: (angle >> ANGLE_OCTANT_SHIFT) & FACING_MASK */

/* MapCell.light bits (the member's name predates them). names: invented, from use */
#define CELL_VISITED 0x01 /* the view was drawn from the cell (view_render_task); the automap shows it */
#define CELL_BLOCKED 0x80 /* walls and doorway side cells: the player cannot enter (player movement, doors_build, level_cells_reset) */

/* MapCell.marker values: 0 empty, an actor below CELL_MARKER_ACTOR_LIMIT, the player, else
 * a level object type (LevelObjectType). names: invented, from explosion_damage */
#define CELL_MARKER_ACTOR_LIMIT 16
#define CELL_MARKER_PLAYER 0xff

/* g_secrets_found entry of a found secret. name: invented */
#define SECRET_FOUND 0xff

typedef struct LayoutRecord {
    u_char x, y;
    u_char flow; /* invented name: breadth-first value written by flow_field_fill, climbed by actor_target_toward_player */
    u_char flags;
    u_char links[4];
} LayoutRecord;

/* LayoutRecord.links entry past the last link, and the flow value flow_field_fill starts from.
 * names: invented */
#define LAYOUT_NO_LINK 0xff
#define FLOW_FIELD_START 0xff

/* 8-byte door placement record (src/doors.c: door_defs_load, doors_build).
 * Names invented from the Door fields they fill. */
typedef struct DoorRecord {
    u_char x;
    u_char z;
    u_char flags;
    u_char hold; /* invented name: hold time in units of 1 << DOOR_HOLD_SHIFT frames (Door.hold) */
    u_char wait;
    u_char value; /* map cell value and ceiling under the door */
    u_char facing;
    u_char model;
} DoorRecord;

/* Door flags (DoorRecord.flags, the door's flags in src/doors.c). names: invented, from
 * doors_build, door_power and the door tasks; parallel to the Lift flags. */
enum {
    DOOR_OPEN = 0x01,
    DOOR_ALONG_Z = 0x02,   /* facing 2 or 6: the doorway's cells run along z */
    DOOR_OPENING = 0x08,
    DOOR_CLOSING = 0x10,
    DOOR_RESTS_OPEN = 0x40, /* built open; stays open after opening, reopens after hold */
    DOOR_COUNTED = 0x80    /* already counted in tallies[TALLY_DOORS] */
};
#define DOOR_MOVING (DOOR_OPENING | DOOR_CLOSING)

/* A door built from a DoorRecord: its heap table entry (doors_alloc, doors_build), drawn by
 * door_draw and moved by the door tasks (src/doors.c). name: invented */
typedef struct Door {
    SVECTOR position;
    MapCell *cell_a;
    MapCell *cell_b;
    MapCell *cell_c; /* invented name: the cell before cell_a, lit by doors_build */
    MapCell *cell_d; /* invented name: the cell after cell_b, lit by doors_build */
    u_char flags;
    u_char facing; /* invented name: picks the door's rotation in door_draw */
    u_char wait; /* invented name: DoorRecord.wait; door_task_open and door_task_closed wait for count to reach it */
    u_char count;
    short hold; /* invented name: DoorRecord.hold << DOOR_HOLD_SHIFT (doors_build); frames an opened door holds before closing; 0 for none (door_task_closing, door_task_opening) */
    u_short model; /* invented name: index of the door's 'D' chunk in g_door_models */
} Door;

#define DOOR_MAX 64 /* doors_alloc's table. name: invented */
/* DoorRecord.hold is the door's hold time in units of 1 << DOOR_HOLD_SHIFT frames
 * (doors_build; door_task_hold counts it down). name: invented */
#define DOOR_HOLD_SHIFT 2
/* Height steps a door opens by, which is also its opening time in frames (door_models_prepare):
 * short on the first two levels. names: invented */
#define DOOR_OPEN_STEPS_SHORT 0x1e
#define DOOR_OPEN_STEPS 0x2b
/* door_draw: view depth and width beyond which a door is not drawn, and below which it uses the
 * near rows. names: invented */
#define DOOR_CULL_DISTANCE 0x1e00
#define DOOR_NEAR_DISTANCE 0x600
/* Distance along its axis the viewer must be from an open door before it closes (door_task_open).
 * name: invented */
#define DOOR_CLOSE_DISTANCE 0x400
/* Player.noise_timer a door starting to open sets (door_task_closed). name: invented */
#define DOOR_NOISE_TIME 0x200
/* A door cell whose ceiling is less than this many height steps above its floor is shut on
 * whatever lies in it: actor_task removes a dead actor there. name: invented */
#define DOOR_SHUT_GAP 4

/* Level tallies (Player.tallies, LevelMap.kind_counts) past the actor kinds and level object types.
 * names: invented, from doors_build, lifts_create, map_objects_spawn and compute_level_results */
enum {
    TALLY_DOORS = 33,
    TALLY_LIFTS = 34,
    TALLY_IDENTITY_TAGS = 35,
    TALLY_TOTAL = 36,
    TALLY_COUNT = 37
};

/* 8-byte object placement record (map_objects_spawn, map_object_trigger). */
typedef struct ObjectRecord {
    u_char x;
    u_char z;
    u_char kind;
    u_char amount; /* invented name: MapObject.amount, the rounds (armour for a vest or suit) the pickup carries */
    u_char clips; /* invented name: MapObject.clips, the clips the pickup carries */
    u_char triggers; /* invented name: raised by map_object_trigger, the object spawns when it reaches required */
    u_char required; /* invented name: 0 spawns the object at level start (map_objects_spawn) */
    u_char default_amount; /* name: inferred from the level data: a constant per kind, set only on the ammunition kinds (pistol clip 15, shotgun cartridges 5, pulse rifle clip 45, grenades 3, flamethrower fuel 15, smart gun ammo 130, seismic charges 1) and 0 elsewhere, independent of amount; no runtime reader (analysis/runtime, round 5) */
} ObjectRecord;

/* Map object (pickup) kinds: ObjectRecord.kind, MapObject.kind, the index of g_object_models.
 * names: inferred from the HUD message of each kind (g_pickup_messages, src/map_objects.c);
 * kinds 5, 24, 26 and 27 read "NOT USED", and kind 24 spawns as PICKUP_DERM_PATCH. */
enum {
    PICKUP_PISTOL = 0,
    PICKUP_SHOTGUN = 1,
    PICKUP_PULSE_RIFLE = 2,
    PICKUP_FLAMETHROWER = 3,
    PICKUP_SMART_GUN = 4,
    PICKUP_UNUSED_5 = 5,
    PICKUP_SEISMIC_CHARGES = 6,
    PICKUP_BATTERY = 7,
    PICKUP_NIGHT_VISION = 8,
    PICKUP_PISTOL_CLIP = 9,
    PICKUP_SHOTGUN_CARTRIDGES = 10,
    PICKUP_PULSE_RIFLE_CLIP = 11,
    PICKUP_GRENADES = 12,
    PICKUP_FLAMETHROWER_FUEL = 13,
    PICKUP_SMART_GUN_AMMO = 14,
    PICKUP_IDENTITY_TAG = 15,
    PICKUP_AUTO_MAPPER = 16,
    PICKUP_HYPO_PACK = 17,
    PICKUP_ACID_VEST = 18,
    PICKUP_BODY_SUIT = 19,
    PICKUP_MEDIKIT = 20,
    PICKUP_DERM_PATCH = 21,
    PICKUP_PROTECTIVE_BOOTS = 22,
    PICKUP_ADRENALINE_BURST = 23,
    PICKUP_UNUSED_24 = 24,
    PICKUP_SHOULDER_LAMP = 25,
    PICKUP_SHOTGUN_SHELL = 26, /* never placed: the model of an ejected shotgun shell (eject_shell) */
    PICKUP_PISTOL_SHELL = 27,  /* never placed: the model of an ejected pistol casing (eject_shell) */
    PICKUP_KIND_COUNT = 28
};

#define MAP_OBJECT_MAX 128 /* the map object pool (pickup_models_load). name: invented */
/* map_object_near_player: reach of the player on x and z, and on y. names: invented */
#define PICKUP_REACH 0x300
#define PICKUP_REACH_Y 0x400
/* A thrown object (actor_drop_item, level_object_drop_item): its height above the actor's floor,
 * its speed away from the thrower (THROW_SPEED_MIN plus up to THROW_SPEED_MASK), the jitter
 * added to it, its upward speed and its first bounce speed. names: invented */
#define THROW_DROP_HEIGHT 0x200
#define THROW_SPEED_MIN 0x10
#define THROW_SPEED_MASK 0xf
#define THROW_JITTER_MASK 0xf
#define THROW_JITTER_BIAS 8
#define THROW_LIFT_SPEED 0x40
#define THROW_BOUNCE_SPEED 0x20 /* MapObject.bounce_speed */
/* map_object_draw spins an object by this much a frame. name: invented */
#define MAP_OBJECT_SPIN_STEP 16

/* name: invented; the model of each map object kind (map_objects_spawn, the pause menu's item
 * display, the particle spawners). 28 entries: map_objects clears 28, and g_secrets_found follows. */
extern struct DispatchRow *g_object_models[PICKUP_KIND_COUNT];
/* Raises a record's trigger count and spawns its object when it is reached (src/map_objects.c). */
extern void map_object_trigger(int amount, u_char index);

/* Pool node allocated by map_object_alloc and linked by map_object_link. */
typedef struct MapObject {
    SVECTOR position;
    SVECTOR velocity; /* invented name: a thrown object's motion per frame (actor_drop_item, level_object_drop_item); map_object_move (unmatched, 0x80038d44) adds it and lets vy fall by 4 a frame */
    SVECTOR rotation; /* invented name: RotMatrix input of map_object_draw, which spins vy by 16 per frame */
    struct DispatchRow *model; /* invented name: g_object_models[kind] */
    MapCell *cell;
    u_char kind;
    u_char amount; /* invented name: ObjectRecord.amount, handed to player_give_pickup (pickups_task); a vest or suit is taken when it exceeds Player.armour */
    u_char clips; /* invented name: ObjectRecord.clips, handed to player_give_pickup as its extra clips (pickups_task) */
    short bounce_speed; /* invented name: velocity.vy a thrown object bounces up with when it lands; map_object_move lowers it by 8 a landing and stops the object below 4 */
    struct MapObject *next;
    struct MapObject *prev;
} MapObject;

/* 40-byte level object: an entry of g_level_map.pickups (level_objects_create), the block of
 * map cells it occupies, and the model drawn for it. name: inferred from the
 * pickup table, the cell marks of level_object_mark_cells/level_object_clear_cells and the renderers
 * in src/object_render.c. */
typedef struct LevelObject {
    SVECTOR position;
    SVECTOR rotation;
    struct DispatchRow *model;
    MapCell *cell;   /* cell under the object; top-left of its block */
    u_char type;         /* written to the block's cells; tallies index */
    u_char flags;
    short state;
    u_char drop[2];      /* object kinds spawned when it is destroyed, 0xff for none */
    u_char occupant;     /* written to the block's cells' MapCell.occupant */
    u_char hits;         /* hits left (level_object_hit_task); rerolled 1..8 when the timer in state runs out (level_object_tick) */
    u_char width;
    u_char depth;
    short facing; /* invented name: octant an actor spawned by actor_spawn_from_object faces; wall_shatter picks a wall's panel layout by it (0 and 4, 2 and 6) */
    short command_base; /* invented name: first command state block_object_task raises (command_state_add) when the object is destroyed; from LevelObjectRecord.command_base */
    short command_count; /* invented name: number of command states raised from command_base; from LevelObjectRecord.command_count */
} LevelObject;

/* Level object types (LevelObject.type, also its cells' MapCell.marker and its tallies index).
 * names: invented, from the task level_objects_create starts for each type, the renderers and
 * the particle hits (particle_hits.c); identity-TODO: what each object depicts. */
enum {
    LEVEL_OBJECT_TARGET = 0x14,       /* level_object_hit_task; the panel model from level 22 */
    LEVEL_OBJECT_BLOCK = 0x15,        /* block_object_task, six explosions; runtime: hazard-topped drums */
    LEVEL_OBJECT_SWITCH_BATTERY = 0x16, /* switch panel needing a battery */
    LEVEL_OBJECT_EXPLOSIVE = 0x17,    /* level_object_debris_task: a big explosion and explosion_damage */
    LEVEL_OBJECT_SWITCH = 0x18,
    LEVEL_OBJECT_TARGET_ALT = 0x19,   /* level_object_hit_task with model 2 */
    LEVEL_OBJECT_SWITCH_BATTERY_ALT = 0x1a,
    LEVEL_OBJECT_SWITCH_ALT = 0x1b,
    LEVEL_OBJECT_WALL = 0x1c,         /* block_object_task; shatters (wall_shatter); runtime: one pistol bullet uses up its state of 16 */
    LEVEL_OBJECT_BLOCK_SMALL = 0x1d,  /* block_object_task, one explosion */
    LEVEL_OBJECT_TILTING = 0x20,      /* level_object_spin_task, the tall model, which it tilts (rotation.vz down to LEVEL_OBJECT_TILT_LIMIT) once state is set; never spins. runtime: levels 4 and 12 only; a long low slab, 1344 x 416 x 576 units (name: invented) */
    LEVEL_OBJECT_STATIC_FIRST = 0x21, /* 0x21..0x25: level_object_task, model type + 2; runtime: 0x21 on levels 2, 6, 12, 13, 18 is a hazard-striped drum; 0x22..0x25 on none */
    LEVEL_OBJECT_STATIC_LAST = 0x25
};

/* LevelObject.flags. names: invented, from use */
#define LEVEL_OBJECT_ARMED 0x01        /* hits count down to its destruction; explosion_damage and level_object_tick arm it */
#define LEVEL_OBJECT_DROPS_ACTOR 0x02  /* drop[0] is an actor slot group (actor_spawn_from_object), not a map object record */
/* LevelObject.drop entry for none. name: invented */
#define LEVEL_OBJECT_NO_DROP 0xff

#define LEVEL_OBJECT_MAX 128 /* level_object_models_load's table. name: invented */
#define LEVEL_OBJECT_MODEL_COUNT 42 /* g_level_object_models entries cleared at load. name: invented */
#define LEVEL_OBJECT_MODEL_TALL 40 /* the model level_object_model_fixup gives the tall selector. name: invented */
/* g_level_object_models entries (OBJ3D.BND model numbers) level_objects_create gives each type;
 * the _LATE models replace their sets from LEVEL_311_TUNNELS on. A switch panel set is
 * SWITCH_FRAME_COUNT frames. LEVEL_OBJECT_MODEL_STATIC is the model of
 * LEVEL_OBJECT_STATIC_FIRST, each later static type the next one. names: invented */
enum {
    LEVEL_OBJECT_MODEL_EXPLOSIVE = 0,
    LEVEL_OBJECT_MODEL_TARGET = 1,     /* also the default */
    LEVEL_OBJECT_MODEL_TARGET_ALT = 2,
    LEVEL_OBJECT_MODEL_SWITCH = 3,
    LEVEL_OBJECT_MODEL_SWITCH_ALT = 7,
    LEVEL_OBJECT_MODEL_SWITCH_BATTERY = 11,
    LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT = 15,
    LEVEL_OBJECT_MODEL_SWITCH_LATE = 19,
    LEVEL_OBJECT_MODEL_SWITCH_ALT_LATE = 23,
    LEVEL_OBJECT_MODEL_SWITCH_BATTERY_LATE = 27,
    LEVEL_OBJECT_MODEL_SWITCH_BATTERY_ALT_LATE = 31,
    LEVEL_OBJECT_MODEL_STATIC = 35,
    LEVEL_OBJECT_MODEL_TARGET_LATE = 41
};
/* Frames of a switch panel's model set: the idle panel blinks between SWITCH_FRAME_IDLE and the
 * next frame, an activated one between SWITCH_FRAME_ACTIVE and SWITCH_FRAME_HELD, which it then
 * holds (switch_panel_task, switch_panel_active_task). names: invented */
enum {
    SWITCH_FRAME_IDLE = 0,
    SWITCH_FRAME_ACTIVE = 2,
    SWITCH_FRAME_HELD = 3,
    SWITCH_FRAME_COUNT = 4
};
/* LevelObject.state of a switch panel once activated (level_object_activate): it stays on when
 * activated with its last charge, else it returns to idle after SWITCH_HOLD_TICKS. Activated, it
 * blinks SWITCH_ACTIVE_BLINKS times every SWITCH_ACTIVE_BLINK_TICKS frames. names: invented */
#define SWITCH_STATE_ON 1
#define SWITCH_STATE_REARMS 2
#define SWITCH_ACTIVE_BLINK_TICKS 4
#define SWITCH_ACTIVE_BLINKS 8
#define SWITCH_HOLD_TICKS 32
/* LevelObjectRecord.state is in units of 16 frames (level_objects_create). name: invented */
#define LEVEL_OBJECT_STATE_SHIFT 4
/* Scale of a level object model (level_object_draw), and of a tall one (level_object_draw_tall).
 * names: invented */
#define LEVEL_OBJECT_SCALE 0xe00
#define LEVEL_OBJECT_TALL_SCALE 0xc00
#define LEVEL_OBJECT_TALL_SCALE_Y 0xd98
/* level_object_spin_task tilts an activated spinner by this much a frame down to the limit.
 * names: invented */
#define LEVEL_OBJECT_TILT_STEP 8
#define LEVEL_OBJECT_TILT_LIMIT (-0xff)
/* Frames between switch panel blinks before activation, and after it. names: invented */
#define SWITCH_BLINK_TICKS 0x10
/* block_object_explode: explosions start a cell beyond the block, spread over a cell
 * across and BLOCK_BLAST_SPREAD_Y in height. names: invented */
#define BLOCK_BLAST_SPREAD_Y 0x100
/* explosion_damage: height steps at which a cell shields the next ring. name: invented */
#define BLAST_STEP_LIMIT 13
/* explosion_damage: the inner ring of 8 and outer ring of 16 cells around the blast; a door cell
 * (CELL_TYPE_DOOR) open by fewer than BLAST_DOOR_GAP height steps shields the ring behind it;
 * the player takes BLAST_NEAR_DAMAGE in the inner ring, BLAST_FAR_DAMAGE in the outer. names: invented */
#define BLAST_NEAR_CELLS 8
#define BLAST_FAR_CELLS 16
#define BLAST_DOOR_GAP 8
#define BLAST_NEAR_DAMAGE 50
#define BLAST_FAR_DAMAGE 25
/* explosion_damage: an actor in reach explodes this far above its position; an armed level
 * object in the inner ring gets BLAST_NEAR_HITS_MIN plus up to BLAST_NEAR_HITS_MASK hits, one in
 * the far ring BLAST_FAR_HITS_MIN plus up to BLAST_FAR_HITS_MASK. names: invented */
#define ACTOR_BLAST_HEIGHT 0x100
#define BLAST_NEAR_HITS_MIN 4
#define BLAST_NEAR_HITS_MASK 7
#define BLAST_FAR_HITS_MIN 8
#define BLAST_FAR_HITS_MASK 0xf
/* level_object_tick: an object whose timer runs out gets 1 plus up to this many hits. name: invented */
#define TIMER_HITS_MASK 7
/* block_object_explode: explosions of a destroyed block (LEVEL_OBJECT_BLOCK_SMALL has one). name: invented */
#define BLOCK_BLAST_COUNT 6
/* level_object_debris_task: the big explosion is this far below the object. name: invented */
#define EXPLOSIVE_BLAST_DROP 0x30
/* block_object_explode: height of the explosions above the block. name: invented */
#define BLOCK_BLAST_HEIGHT 0x200

/* 16-byte level object placement record: an entry of g_level_map.pickups, copied into a
 * LevelObject by level_objects_create. */
typedef struct LevelObjectRecord {
    u_char x;
    u_char z;
    u_char type;
    u_char flags;
    u_char state; /* LevelObject.state >> 4 */
    u_char unknown_05; /* identity-TODO: level_objects_create skips it; no runtime reader at level load or in play (analysis/runtime, round 5); 10 on types 0x14, 0x19, 0x1d and 0x20, 0 on every other type in all 35 levels */
    u_char drop[2];
    u_char width;
    u_char depth;
    u_short command_base; /* LevelObject.command_base */
    u_short command_count; /* LevelObject.command_count */
    u_short facing;
} LevelObjectRecord;

/* Raises an actor slot's group count for a destroyed object and spawns the group when full (src/game_20000_3.c). */
extern void actor_spawn_from_object(LevelObject *object, u_char index);
/* Explosion at a map position and its cell (src/object_render.c): destroyed level objects and particles hitting cells call it. */
extern void explosion_damage(SVECTOR *position, MapCell *cell);

/* 16-byte lift placement record (lift_records_prepare). */
typedef struct LiftRecord {
    u_char x;
    u_char z;
    u_char width; /* Lift.width: cells the lift covers along x (lifts_create) */
    u_char depth; /* Lift.depth: cells along z */
    u_char level; /* invented name: set to the cell's value by lift_records_prepare */
    u_char base; /* invented name: raised by the cell's value in lift_records_prepare */
    u_char unknown_06; /* identity-TODO: copied to Lift.unknown_14 (lifts_create, its one runtime reader); 1 in all 126 lift records */
    u_char flags;  /* Lift.flags */
    u_char delay;  /* Lift.delay */
    u_char hold;   /* Lift.hold */
    u_char wait;   /* Lift.wait: TRIGGER_LIFT counts it moves after */
    u_char count;  /* Lift.count: TRIGGER_LIFT counts so far */
    u_char model;  /* Lift.model */
    u_char unknown_0d[3]; /* identity-TODO: no runtime reader at level load or in play (analysis/runtime, round 5); three equal bytes, 0..8, in every lift record */
} LiftRecord;

/* A lift's position is its first cell's centre, MAP_CELL_HALF in from its corner
 * (lifts_create). invented name */
#define LIFT_HALF_CELL_SHIFT 8 /* log2(MAP_CELL_HALF) */
#define LIFT_FACE_HEIGHT_LIMIT 0x2000 /* bound on a lift model's vertex heights (lift_measure_model) */
#define LIFT_MAX 32 /* lifts_alloc's table. name: invented */

/* A lift built from a LiftRecord (src/lift.c; the unmatched lift_draw takes one too). */
typedef struct Lift {
    SVECTOR position; /* invented name: the lift's sound position (sound_play_at); vy is its height */
    MapCell *cells;
    void *faces;
    u_char width;
    u_char depth;
    u_char low;
    u_char high;
    u_char unknown_14; /* identity-TODO: LiftRecord.unknown_06; no runtime reader with the lifts idle or moving (level 14) */
    u_char flags;
    u_char delay;
    u_char hold;
    u_char wait;
    u_char count;
    u_short model; /* invented name: index into g_lift_models for faces (lifts_create) */
    short centre_x;
    short offset;
    short centre_z;
} Lift;

/* Lift.flags. name: inferred from lift_task_delay/lower/raise/wait. */
enum {
    LIFT_UP = 0x01,        /* at its high position */
    LIFT_RAISING = 0x02,
    LIFT_DOWN = 0x04,      /* at its low position */
    LIFT_LOWERING = 0x08,
    LIFT_FAST = 0x10,      /* moves two height steps per frame */
    LIFT_RESTS_UP = 0x20,  /* waits for triggers when up, returns after hold when down */
    LIFT_CYCLES = 0x40,    /* always returns after hold */
    LIFT_COUNTED = 0x80    /* already counted in tallies[34] */
};
#define LIFT_FAST_STEP 2 /* height steps a LIFT_FAST lift moves per frame. name: invented */

typedef struct ActorSlot {
    u_char kind;
    u_char grid_x;
    u_char grid_z;
    u_char unknown_03; /* identity-TODO: no runtime reader at level load or in play (analysis/runtime, round 5); 0xff in 1508 of the 1515 slots, else 6..65 on six face-hugger slots of level 15 and 1 on one warrior of level 4 */
    u_char direction; /* invented name: copied to Actor.direction (actor_spawn); a FACING_* octant */
    u_char strength; /* invented name: hit points before the difficulty scaling (actor_slot_health) */
    u_char attached_index; /* invented name: copied to Actor.attached_index (actor_spawn) */
    u_char group; /* invented name */
    u_char group_size; /* invented name */
    u_char unknown_09; /* identity-TODO: no runtime reader at level load or in play (analysis/runtime, round 5); 0 except 2 or 4 on 21 eggs (all with a face-hugger as attached slot) and 2 on 15 cocooned colonists */
    short view_cone; /* invented name: copied to Actor.view_cone (actor_spawn) */
    short reach; /* invented name: copied to Actor.reach (actor_spawn) */
    short speed; /* invented name: copied to Actor.speed (actor_spawn) */
    short far_range; /* invented name: copied to Actor.far_range (actor_spawn) */
    short near_range; /* invented name: copied to Actor.near_range (actor_spawn) */
} ActorSlot;

/* Level script: a command is a value and an opcode (bit 15 continues the step). */
typedef struct Command {
    u_short value;
    u_short opcode;
} Command;

/* Start of one script state: the label it begins at and two parameters. */
typedef struct CommandStart {
    short label;
    u_char param[2];
} CommandStart;

typedef struct LightRecord {
    CVECTOR colors[4]; /* only r, g and b are copied (lights_init) */
    short timer; /* invented name: frames left in the current phase; the light update (0x80023890, unmatched) counts it down and reloads it from flash_time or rest_time */
    short flash_count; /* invented name: mode 1 flashes left; the light update counts it down per flash */
    short flash_time; /* invented name: frames spent with flashing set (modes 1 and 2) */
    short rest_time; /* invented name: frames spent with flashing clear (modes 1 and 2) */
    u_char mode; /* invented name: 4 fills a whole palette entry from colors (lights_init); in the light update 1 flashes flash_count times, 2 blinks, 3 and 5 flicker for random times, 0 stays */
    u_char flashing; /* invented name: the phase; 1 shows colors[1], 0 colors[0] (lights_init, the light update). Until level reaches max_level the update reloads it, timer and flash_count from the level's record */
    u_char level; /* invented name: raised by light_raise up to max_level */
    u_char max_level; /* invented name */
} LightRecord;

/* LightRecord.mode that fills a whole palette entry from colors (lights_init). name: invented */
#define LIGHT_MODE_FULL_PALETTE 4

typedef struct LevelMap {
    u_short kind_counts[TALLY_COUNT];
    short unknown_4a; /* identity-TODO: level_parse_map neither clears nor fills it (its clear loop covers kind_counts only); no absolute reference to 0x800960aa in retail and no runtime access in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime, round 5) */
    short vertex_count; /* invented name: the first halfword of the map chunk; vertices span vertex_count SVECTORs (level_parse_map) */
    short face_count;
    short layout_record_count;
    short actor_slot_count;
    short object_record_count; /* invented name: count passed with object_records to map_objects_spawn */
    short pickup_count; /* invented name: entry count handed to level_objects_create with pickups */
    short door_record_count; /* invented name */
    short lift_count; /* invented name: lift record count (lift_records_init, lifts_spawn) */
    u_short start_x; /* invented name: the player's starting cell column (player_task_start) */
    u_short start_z; /* invented name: the player's starting cell row (player_task_start) */
    short start_facing; /* invented name: the player's starting facing (FACING_*); player_task_start turns the view by it */
    u_char cell_trigger_count; /* invented name: records at cell_triggers, the first unused (cell_triggers_load); level_parse_map caps it at 64 */
    u_char trigger_command_count; /* invented name: 4-byte records at trigger_commands (level_parse_map places lights after them, caps it at 64) */
    u_int resources; /* invented name: resource-set flag bits, one sprite file each (level_sprites_load) */
    u_short width;
    u_short height;
    u_short offset_x;
    u_short offset_z;
    short state_count; /* invented name: script states set up by command_states_init (via command_sequencer_start) */
    short command_count; /* invented name: script commands scanned for labels by command_states_init */
    short light_count; /* invented name: records at lights, loaded by lights_init */
    u_short secret_count;
    MapCell *cells;
    struct DispatchRow *faces; /* see <MODEL.H> */
    ActorSlot *actor_slots;
    SVECTOR *vertices;
    LayoutRecord *layout_records;
    ObjectRecord *object_records; /* invented name */
    struct LevelObjectRecord *pickups; /* invented name: entry table handed to level_objects_create, whose entries level_object_activate collects */
    DoorRecord *door_records; /* invented name: handed to door_defs_load with door_record_count (level_doors_load) */
    LiftRecord *lift_records; /* invented name */
    u_char *cell_triggers; /* invented name: 4-byte cell trigger records copied by cell_triggers_load into g_cell_triggers */
    u_long trigger_commands; /* invented name: 4-byte command table run by level_commands_run (stored by level_triggers_load) */
    u_char *map_chunk;
    CommandStart *command_starts; /* invented name */
    Command *commands; /* invented name */
    LightRecord *lights; /* invented name: read at 0x80096110 with light_count (lights_load) */
} LevelMap;

extern LevelMap g_level_map;

/* Screen position of the render time view_render_task shows in debug mode. names: invented */
#define RENDER_TIME_X 0xa0
#define RENDER_TIME_Y 0x78

/* Model chunks of the level file, collected by level_map_file_load (src/game_20000_3.c): 'D'
 * chunks (door models, g_door_model_count of them) and 'L' chunks (lift models, g_lift_model_count).
 * names: runtime evidence, analysis/runtime/globals.json (level 1: 2 door models, no
 * lifts; level 14: 3 door models, 5 lifts) */
#define DOOR_MODEL_MAX 8
#define LIFT_MODEL_MAX 32
/* The level map file: a header, then chunks of a MAP_CHUNK_ID_SIZE-character id, a 32-bit
 * big-endian length and the data (level_map_file_load). names: invented */
#define MAP_FILE_HEADER_SIZE 12
#define MAP_CHUNK_ID_SIZE 4
extern u_char *g_door_models[DOOR_MODEL_MAX];
extern short g_door_model_count;
extern void *g_lift_models[LIFT_MODEL_MAX];
extern short g_lift_model_count;
/* name: inferred from use: MapCell.mask bits of the cells in view; actors and objects outside it are skipped */
extern u_short g_visible_cell_mask;

/* name: invented; the map cell under map position x, z (world position plus offset_x, offset_z) */
#define MAP_CELL(x, z) (g_level_map.cells + g_level_map.width * ((z) >> MAP_CELL_SHIFT) + ((x) >> MAP_CELL_SHIFT))
/* name: invented; map position x, z lies beyond the last cell column or row */
#define MAP_OUTSIDE(x, z) ((x) >> MAP_CELL_SHIFT > g_level_map.width || (z) >> MAP_CELL_SHIFT > g_level_map.height)

/* Secret found flags: [0] is set at level start, view_render_task sets the entry of the current cell's
 * secret; the results screen counts [1..255]. name: runtime evidence (medium; no secret
 * entered at runtime), analysis/runtime/globals.json */
extern unsigned char g_secrets_found[256];

/* Level trigger commands: the first byte of a 4-byte g_level_commands record {code, next,
 * operand, operand}; next is TRIGGER_CHAIN_END after the last command of a chain.
 * names: inferred from the function each code calls (src/level_triggers.c) */
enum {
    TRIGGER_LIGHT_RAISE = 0,       /* light_raise */
    TRIGGER_DOOR_POWER = 1,        /* door_power */
    TRIGGER_MAP_OBJECT = 2,        /* map_object_trigger */
    TRIGGER_ACTOR_GROUP = 3,       /* actor_group_add */
    TRIGGER_SWITCH = 4,            /* level_object_activate; the player's use button */
    TRIGGER_LIFT_LAST = 5,         /* lift_trigger_last */
    TRIGGER_DOOR_POWER_FINAL = 6,  /* door_power_final; the player's use button */
    TRIGGER_LIFT = 7,              /* lift_trigger */
    TRIGGER_LEVEL_END = 8,         /* sets GAME_FLAG_LEVEL_COMPLETE */
    TRIGGER_COMMAND_STATE = 9      /* command_state_add */
};
#define TRIGGER_CHAIN_END 0xff
/* Bytes of a g_level_commands record: the code, the next command, and the two operands every
 * handler takes (an amount and the index of the door, lift, light, record or group it acts on).
 * names: invented, from the handlers' parameters */
enum {
    LEVEL_COMMAND_CODE = 0,
    LEVEL_COMMAND_NEXT = 1,
    LEVEL_COMMAND_AMOUNT = 2,
    LEVEL_COMMAND_INDEX = 3,
    LEVEL_COMMAND_SIZE = 4
};
/* Bytes of a g_cell_triggers record (MapCell.trigger numbers them from 1). names: invented */
enum {
    CELL_TRIGGER_FLAGS = 0,
    CELL_TRIGGER_COMMAND = 1,
    CELL_TRIGGER_CHARGES = 2,
    CELL_TRIGGER_SIZE = 4
};
/* Cell trigger record {flags, first command, charges, -} (g_cell_triggers): who sets it off,
 * and the charge count that never runs out. names: inferred from the four callers */
#define TRIGGER_BY_PLAYER 0x01     /* player_cell_trigger */
#define TRIGGER_BY_SHOT 0x02       /* shot_cell_trigger */
#define TRIGGER_BY_ACTOR 0x04      /* actor_cell_trigger */
#define TRIGGER_BY_ENEMY_SHOT 0x08 /* enemy_shot_cell_trigger */
#define TRIGGER_ENDLESS 0xff

/* src/level_triggers.c: runs a level command */
extern void level_commands_run(u_char index);

/* src/map_objects.c: throws an object from a level object */
extern void level_object_drop_item(LevelObject *source, u_char index);

struct TASK;
struct Particle;
struct Actor;

/* src/doors.c */
extern int door_power(int step, int index);
extern int door_power_final(int step, int index);
extern void door_models_prepare(void);
extern void doors_alloc(void);
extern void doors_init(void);

/* src/game_20000_3.c */
extern void view_render_task(struct TASK *task);
extern void level_map_file_load(LevelMap *level);
extern void flow_field_clear(void);
extern void level_cells_reset(LevelMap *level);
extern void flow_field_fill(LayoutRecord *start, short index);

/* src/level_triggers.c */
extern void level_triggers_load(void);
/* src/level_triggers.c: runs the triggers of the player's cell; buttons are the pressed use buttons */
extern void player_cell_trigger(int buttons);
extern void actor_cell_trigger(struct Actor *actor);
extern void shot_cell_trigger(struct Particle *particle);
extern void enemy_shot_cell_trigger(struct Particle *particle);

/* src/lift.c */
extern int lift_trigger(int amount, u_char index);
extern int lift_trigger_last(int amount, u_char index);
extern void lift_models_prepare(void);
extern void lifts_alloc(void);
extern void lifts_spawn(void);

/* src/lights.c */
extern void light_raise(int amount, int index);
extern void lights_load(void);

/* src/map_objects.c */
extern void actor_drop_item(struct Actor *actor);
extern void map_objects_init(void);
extern void pickup_models_load(void);
extern void pickups_task(struct TASK *task);

/* src/object_render.c */
extern int level_object_activate(u_char index, u_char status);
extern void level_object_models_load(void);
extern void level_objects_init(void);
extern void level_object_tick(int index, int elapsed);

/* MapCell.type: slopes are 45..56, three bands of four facings; player_controls looks up
 * or down by facing. names: invented */
#define CELL_TYPE_SLOPE 0x2d
#define CELL_SLOPE_BAND 4
/* The slope bands: a step (the half toward its facing is MAP_SLOPE_RISE steps up) and two ramps
 * with the same floor (floor_height); and the facing within a band, the side the cell rises
 * toward. CELL_TYPE_SLOPE_AT names one slope type. names: invented */
enum {
    SLOPE_BAND_STEP = 0,
    SLOPE_BAND_RAMP = 1,
    SLOPE_BAND_RAMP_ALT = 2
};
enum {
    SLOPE_RISES_POS_Z = 0,
    SLOPE_RISES_NEG_X = 1,
    SLOPE_RISES_POS_X = 2,
    SLOPE_RISES_NEG_Z = 3
};
#define CELL_TYPE_SLOPE_AT(band, facing) (CELL_TYPE_SLOPE + (band) * CELL_SLOPE_BAND + (facing))

/* MapCell.type values below the slopes. level_cells_reset blocks CELL_TYPE_SOLID and the walls
 * (CELL_TYPE_WALL up to the diagonals); particles explode on CELL_TYPE_WALL
 * (particle_hit_floor_or_ceiling) and explosions stop at every type from it (explosion_damage).
 * A door cell's ceiling moves with its door (src/doors.c); particles, explosions and the automap
 * treat it apart. diagonal_clip_x/_z keep positions off the solid half of a diagonal cell,
 * named by the corner (low or high x and z within the cell) that half covers.
 * CELL_TYPE_SLOW and CELL_TYPE_LIQUID_DEEP halve the player's acceleration and top speed unless
 * protective boots are on (player_accelerate); shots hitting a wall from a liquid cell splash
 * (SOUND_SHOT_SPLASH), and falling particles stop 256 (shallow) or 384 (deep) units above its
 * floor (particle.c). names: invented, from use (the slow and liquid ones inferred). Runtime: type 9
 * is a water-filled channel or sunken pool (levels 3 and 17); identity-TODO: type 5 looks like
 * ordinary floor in the level 1 cells viewed */
enum {
    CELL_TYPE_FLOOR = 0,
    CELL_TYPE_SOLID = 1,
    CELL_TYPE_SLOW = 5,
    CELL_TYPE_DOOR = 6,
    CELL_TYPE_LIQUID_SHALLOW = 8,
    CELL_TYPE_LIQUID_DEEP = 9,
    CELL_TYPE_REMAINS = 12, /* a dead alien leaves it on the CELL_TYPE_FLOOR cell it dies on (not on DIFFICULTY_ACID_REIGN); identity-TODO: no matched reader */
    CELL_TYPE_WALL = 20,
    CELL_TYPE_DIAGONAL_LOW_X_LOW_Z = 25,
    CELL_TYPE_DIAGONAL_HIGH_X_LOW_Z = 26,
    CELL_TYPE_DIAGONAL_LOW_X_HIGH_Z = 27,
    CELL_TYPE_DIAGONAL_HIGH_X_HIGH_Z = 28
};
#define CELL_TYPE_DIAGONAL_FIRST CELL_TYPE_DIAGONAL_LOW_X_LOW_Z /* also the end of the walls */

#endif
