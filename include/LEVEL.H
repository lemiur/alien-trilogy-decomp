/*
 * The loaded level map at 0x80096060 (g_levelMap; name invented: Sys_LevelParseMap fills it from the
 * level's .MAP file with the cell grid, faces, vertices, actor slots, object/door/lift records, triggers,
 * script and lights, and the player's start cell). Requires <sys/types.h> and <LIBGTE.H>.
 * Retail forms &offsetX as 0x800960a6 + 0x26 and reaches width (-4), offsetZ (+2)
 * and cells (+0xc) from that base, so 0x80096060..0x8009610f is one object. The word at
 * 0x80096110 continues the count/pointer pairing (stateCount, commandCount, lightCount at
 * +0x70..+0x74 with commandStarts, commands, lights at +0xa8..+0xb0), and Light_Load passes it
 * together with lightCount, so the object ends at 0x80096113.
 * Names without evidence are marked invented; unidentified members are identity-TODO.
 */
#ifndef LEVEL_H
#define LEVEL_H

typedef struct mapCell_s {
    struct dispatchRow_s *face; /* see <MODEL.H> */
    u_char occupant; /* actor index of the actor in the cell (Actor_Move, Actor_LineOfSight) or a level object's occupant value; particles hit it (Object_TickLevelObject) */
    u_char  secretIndex;
    u_char  layoutIndex; /* invented name: copied to Actor.layoutIndex when an actor enters the cell (Actor_Move) */
    u_char  light;
    u_short mask;
    u_char  type;
    u_char  value;
    u_char  marker;
    u_char  ceiling; /* invented name: door code moves it, FixupLevelPointers defaults it to value + 0x30 */
    u_char palette; /* invented name: 16-byte palette entry the cell's models are lit with (Model_DrawFarInCell, Particle_DrawDebris) */
    u_char trigger; /* invented name: the cell's g_cellTriggers record, numbered from 1; 0 for none (Trigger_PlayerCell, Trigger_ActorCell, Trigger_ShotCell, Trigger_EnemyShotCell) */
} mapCell_t;

/* Map geometry. A cell is 0x200 map units square (map position >> 9 is the cell column or
 * row); heights are mapCell_t.value steps of 0x20 units (FloorHeight, CeilingHeight).
 * names: invented */
#define MAP_CELL_SHIFT 9
#define MAP_CELL_SIZE 0x200
#define MAP_CELL_HALF 0x100 /* a cell's centre from its corner */
#define MAP_CELL_MASK 0x1ff /* position within its cell */
#define MAP_HEIGHT_SHIFT 5
#define MAP_HEIGHT_STEP 0x20
#define MAP_HEIGHT_HALF_STEP 0x10 /* doors, lifts: centre of a height step */
#define MAP_DEFAULT_CEILING 0x30  /* steps above value of a cell whose ceiling is 0 (FixupLevelPointers) */
/* Slope cells (FloorHeight): a ramp climbs MAP_SLOPE_RISE height steps across its cell, one
 * height unit per 1 << MAP_SLOPE_SHIFT map units; a step cell's raised half is MAP_SLOPE_RISE
 * steps up. names: invented */
#define MAP_SLOPE_RISE 4
#define MAP_SLOPE_SHIFT 2

/* Octant facings (levelObject_t.facing, doorRecord_t.facing, actorSlot_t.direction, Actor.direction
 * and, relative to the view, Actor.shownDirection): the octant of an angle, named by the
 * direction Object_ActorDropItem and Object_LevelObjectDropItem throw an object for each. Doors use
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

/* mapCell_t.light bits (the member's name predates them). names: invented, from use */
#define CELL_VISITED 0x01 /* the view was drawn from the cell (ViewRenderTask); the automap shows it */
#define CELL_BLOCKED \
    0x80 /* walls and doorway side cells: the player cannot enter (player movement, Door_Build, ResetLevelCells) */

/* mapCell_t.marker values: 0 empty, an actor below CELL_MARKER_ACTOR_LIMIT, the player, else
 * a level object type (LevelObjectType). names: invented, from Object_ExplosionDamage */
#define CELL_MARKER_ACTOR_LIMIT 16
#define CELL_MARKER_PLAYER 0xff

/* g_secretsFound entry of a found secret. name: invented */
#define SECRET_FOUND 0xff

/* layoutRecord_t.links[]: up to LAYOUT_LINKS neighbour records, LAYOUT_NO_LINK after the last;
 * LAYOUT_LINK_LAST is the index of the last slot (FillFlowField, Actor_TargetTowardPlayer). names: invented */
#define LAYOUT_LINK_LAST 3
#define LAYOUT_LINKS ( LAYOUT_LINK_LAST + 1 )

typedef struct layoutRecord_s {
    u_char x, y;
    u_char flow; /* invented name: breadth-first value written by FillFlowField, climbed by Actor_TargetTowardPlayer */
    u_char flags;
    u_char links[LAYOUT_LINKS];
} layoutRecord_t;

/* layoutRecord_t.links entry past the last link, and the flow value FillFlowField starts from.
 * names: invented */
#define LAYOUT_NO_LINK 0xff
#define FLOW_FIELD_START 0xff

/* 8-byte door placement record (src/doors.c: Door_LoadDefs, Door_Build).
 * Names invented from the Door fields they fill. */
typedef struct doorRecord_s {
    u_char x;
    u_char z;
    u_char flags;
    u_char hold; /* invented name: hold time in units of 1 << DOOR_HOLD_SHIFT frames (Door.hold) */
    u_char wait;
    u_char value; /* map cell value and ceiling under the door */
    u_char facing;
    u_char model;
} doorRecord_t;

/* Door flags (doorRecord_t.flags, the door's flags in src/doors.c). names: invented, from
 * Door_Build, Door_Power and the door tasks; parallel to the Lift flags. */
enum {
    DOOR_OPEN = 0x01,
    DOOR_ALONG_X = 0x00, /* facing 0 or 4: the doorway's cells run along x (the clear DOOR_ALONG_Z bit) */
    DOOR_ALONG_Z = 0x02, /* facing 2 or 6: the doorway's cells run along z */
    DOOR_OPENING = 0x08,
    DOOR_CLOSING = 0x10,
    DOOR_RESTS_OPEN = 0x40, /* built open; stays open after opening, reopens after hold */
    DOOR_COUNTED = 0x80     /* already counted in tallies[TALLY_DOORS] */
};
#define DOOR_MOVING ( DOOR_OPENING | DOOR_CLOSING )

/* A door built from a doorRecord_t: its heap table entry (Door_Alloc, Door_Build), drawn by
 * Door_Draw and moved by the door tasks (src/doors.c). name: invented */
typedef struct door_s {
    SVECTOR    position;
    mapCell_t *cellA;
    mapCell_t *cellB;
    mapCell_t *cellC; /* invented name: the cell before cellA, lit by Door_Build */
    mapCell_t *cellD; /* invented name: the cell after cellB, lit by Door_Build */
    u_char     flags;
    u_char     facing; /* invented name: picks the door's rotation in Door_Draw */
    u_char wait; /* invented name: doorRecord_t.wait; Door_TaskOpen and Door_TaskClosed wait for count to reach it */
    u_char count;
    short hold; /* invented name: doorRecord_t.hold << DOOR_HOLD_SHIFT (Door_Build); frames an opened door holds before closing; 0 for none (Door_TaskClosing, Door_TaskOpening) */
    u_short model; /* invented name: index of the door's 'D' chunk in g_doorModels */
} door_t;

#define DOOR_MAX 64 /* Door_Alloc's table. name: invented */
/* TASK.state[] slots of the door tasks (src/doors.c): the door, the opening/closing step count
 * and the hold countdown (Door_TaskHold). names: invented */
enum {
    DOOR_TASK_DOOR = 0,
    DOOR_TASK_STEPS = 2,
    DOOR_TASK_HOLD = 3
};
/* doorRecord_t.hold is the door's hold time in units of 1 << DOOR_HOLD_SHIFT frames
 * (Door_Build; Door_TaskHold counts it down). name: invented */
#define DOOR_HOLD_SHIFT 2
/* Height steps a door opens by, which is also its opening time in frames (Door_PrepareModels):
 * short on the first two levels. names: invented */
#define DOOR_OPEN_STEPS_SHORT 0x1e
#define DOOR_OPEN_STEPS 0x2b
/* Door_Draw: view depth and width beyond which a door is not drawn, and below which it uses the
 * near rows. names: invented */
#define DOOR_CULL_DISTANCE 0x1e00
#define DOOR_NEAR_DISTANCE 0x600
/* Distance along its axis the viewer must be from an open door before it closes (Door_TaskOpen).
 * name: invented */
#define DOOR_CLOSE_DISTANCE 0x400
/* Player.noiseTimer a door starting to open sets (Door_TaskClosed). name: invented */
#define DOOR_NOISE_TIME 0x200
/* A door cell whose ceiling is less than this many height steps above its floor is shut on
 * whatever lies in it: actor_task removes a dead actor there. name: invented */
#define DOOR_SHUT_GAP 4

/* Level tallies (Player.tallies, levelMap_t.kindCounts) past the actor kinds and level object types.
 * names: invented, from Door_Build, Lift_Create, Object_Spawn and Menu_ComputeLevelResults */
enum {
    TALLY_DOORS = 33,
    TALLY_LIFTS = 34,
    TALLY_IDENTITY_TAGS = 35,
    TALLY_TOTAL = 36,
    TALLY_COUNT = 37
};

/* 8-byte object placement record (Object_Spawn, Object_Trigger). */
typedef struct objectRecord_s {
    u_char x;
    u_char z;
    u_char kind;
    u_char amount;   /* invented name: mapObject_t.amount, the rounds (armour for a vest or suit) the pickup carries */
    u_char clips;    /* invented name: mapObject_t.clips, the clips the pickup carries */
    u_char triggers; /* invented name: raised by Object_Trigger, the object spawns when it reaches required */
    u_char required; /* invented name: 0 spawns the object at level start (Object_Spawn) */
    u_char defaultAmount; /* name: inferred from the level data: a constant per kind, set only on the ammunition kinds (pistol clip 15, shotgun cartridges 5, pulse rifle clip 45, grenades 3, flamethrower fuel 15, smart gun ammo 130, seismic charges 1) and 0 elsewhere, independent of amount; no runtime reader (analysis/runtime) */
} objectRecord_t;

/* Map object (pickup) kinds: objectRecord_t.kind, mapObject_t.kind, the index of g_objectModels.
 * names: inferred from the HUD message of each kind (g_pickupMessages, src/map_objects.c);
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
    PICKUP_SHOTGUN_SHELL = 26, /* never placed: the model of an ejected shotgun shell (Particle_EjectShell) */
    PICKUP_PISTOL_SHELL = 27,  /* never placed: the model of an ejected pistol casing (Particle_EjectShell) */
    PICKUP_KIND_COUNT = 28
};

#define MAP_OBJECT_MAX 128 /* the map object pool (Object_LoadPickupModels). name: invented */
/* Object_NearPlayer: reach of the player on x and z, and on y. names: invented */
#define PICKUP_REACH 0x300
#define PICKUP_REACH_Y 0x400
/* A thrown object (Object_ActorDropItem, Object_LevelObjectDropItem): its height above the actor's floor,
 * its speed away from the thrower (THROW_SPEED_MIN plus up to THROW_SPEED_MASK), the jitter
 * added to it, its upward speed and its first bounce speed. names: invented */
#define THROW_DROP_HEIGHT 0x200
#define THROW_SPEED_MIN 0x10
#define THROW_SPEED_MASK 0xf
#define THROW_JITTER_MASK 0xf
#define THROW_JITTER_BIAS 8
#define THROW_LIFT_SPEED 0x40
#define THROW_BOUNCE_SPEED 0x20 /* mapObject_t.bounceSpeed */
/* Object_Draw spins an object by this much a frame. name: invented */
#define MAP_OBJECT_SPIN_STEP 16

/* name: invented; the model of each map object kind (Object_Spawn, the pause menu's item
 * display, the particle spawners). 28 entries: map_objects clears 28, and g_secretsFound follows. */
extern struct dispatchRow_s *g_objectModels[PICKUP_KIND_COUNT];
/* Raises a record's trigger count and spawns its object when it is reached (src/map_objects.c). */
extern void Object_Trigger( int amount, u_char index );

/* Pool node allocated by Object_Alloc and linked by Object_Link. */
typedef struct mapObject_s {
    SVECTOR position;
    SVECTOR velocity; /* invented name: a thrown object's motion per frame (Object_ActorDropItem, Object_LevelObjectDropItem); Object_Move (unmatched, 0x80038d44) adds it and lets vy fall by 4 a frame */
    SVECTOR               rotation; /* invented name: RotMatrix input of Object_Draw, which spins vy by 16 per frame */
    struct dispatchRow_s *model;    /* invented name: g_objectModels[kind] */
    mapCell_t            *cell;
    u_char                kind;
    u_char amount; /* invented name: objectRecord_t.amount, handed to Weapon_PlayerGivePickup (Object_PickupsTask); a vest or suit is taken when it exceeds Player.armour */
    u_char clips; /* invented name: objectRecord_t.clips, handed to Weapon_PlayerGivePickup as its extra clips (Object_PickupsTask) */
    short bounceSpeed; /* invented name: velocity.vy a thrown object bounces up with when it lands; Object_Move lowers it by 8 a landing and stops the object below 4 */
    struct mapObject_s *next;
    struct mapObject_s *prev;
} mapObject_t;

/* 40-byte level object: an entry of g_levelMap.pickups (Object_CreateLevelObjects), the block of
 * map cells it occupies, and the model drawn for it. name: inferred from the
 * pickup table, the cell marks of Object_MarkLevelObjectCells/Object_ClearLevelObjectCells and the renderers
 * in src/object_render.c. */
typedef struct levelObject_s {
    SVECTOR               position;
    SVECTOR               rotation;
    struct dispatchRow_s *model;
    mapCell_t            *cell; /* cell under the object; top-left of its block */
    u_char                type; /* written to the block's cells; tallies index */
    u_char                flags;
    short                 state;
    u_char                drop[2];  /* object kinds spawned when it is destroyed, 0xff for none */
    u_char                occupant; /* written to the block's cells' mapCell_t.occupant */
    u_char hits; /* hits left (Object_LevelObjectHitTask); rerolled 1..8 when the timer in state runs out (Object_TickLevelObject) */
    u_char width;
    u_char depth;
    short facing; /* invented name: octant an actor spawned by Actor_SpawnFromObject faces; Debris_ShatterWall picks a wall's panel layout by it (0 and 4, 2 and 6) */
    short commandBase; /* invented name: first command state Object_BlockObjectTask raises (Command_AddState) when the object is destroyed; from levelObjectRecord_t.commandBase */
    short commandCount; /* invented name: number of command states raised from commandBase; from levelObjectRecord_t.commandCount */
} levelObject_t;

/* Level object types (levelObject_t.type, also its cells' mapCell_t.marker and its tallies index).
 * names: invented, from the task Object_CreateLevelObjects starts for each type, the renderers and
 * the particle hits (particle_hits.c); identity-TODO: what each object depicts. */
enum {
    LEVEL_OBJECT_TARGET = 0x14,         /* Object_LevelObjectHitTask; the panel model from level 22 */
    LEVEL_OBJECT_BLOCK = 0x15,          /* Object_BlockObjectTask, six explosions; runtime: hazard-topped drums */
    LEVEL_OBJECT_SWITCH_BATTERY = 0x16, /* switch panel needing a battery */
    LEVEL_OBJECT_EXPLOSIVE = 0x17,      /* Object_LevelObjectDebrisTask: a big explosion and Object_ExplosionDamage */
    LEVEL_OBJECT_SWITCH = 0x18,
    LEVEL_OBJECT_TARGET_ALT = 0x19, /* Object_LevelObjectHitTask with model 2 */
    LEVEL_OBJECT_SWITCH_BATTERY_ALT = 0x1a,
    LEVEL_OBJECT_SWITCH_ALT = 0x1b,
    LEVEL_OBJECT_WALL = 0x1c, /* Object_BlockObjectTask; shatters (Debris_ShatterWall); runtime: one pistol bullet uses up its state of 16 */
    LEVEL_OBJECT_BLOCK_SMALL = 0x1d, /* Object_BlockObjectTask, one explosion */
    LEVEL_OBJECT_TILTING = 0x20, /* Object_LevelObjectSpinTask, the tall model, which it tilts (rotation.vz down to LEVEL_OBJECT_TILT_LIMIT) once state is set; never spins. runtime: levels 4 and 12 only; a long low slab, 1344 x 416 x 576 units (name: invented) */
    LEVEL_OBJECT_STATIC_FIRST = 0x21, /* 0x21..0x25: Object_LevelObjectTask, model type + 2; runtime: 0x21 on levels 2, 6, 12, 13, 18 is a hazard-striped drum; 0x22..0x25 on none */
    LEVEL_OBJECT_STATIC_LAST = 0x25
};

/* levelObject_t.flags. names: invented, from use */
#define LEVEL_OBJECT_ARMED \
    0x01 /* hits count down to its destruction; Object_ExplosionDamage and Object_TickLevelObject arm it */
#define LEVEL_OBJECT_DROPS_ACTOR \
    0x02 /* drop[0] is an actor slot group (Actor_SpawnFromObject), not a map object record */
/* levelObject_t.drop entry for none. name: invented */
#define LEVEL_OBJECT_NO_DROP 0xff

#define LEVEL_OBJECT_MAX 128        /* Object_LoadLevelObjectModels's table. name: invented */
#define LEVEL_OBJECT_MODEL_COUNT 42 /* g_levelObjectModels entries cleared at load. name: invented */
#define LEVEL_OBJECT_MODEL_TALL 40  /* the model Object_FixupLevelObjectModel gives the tall selector. name: invented */
/* g_levelObjectModels entries (OBJ3D.BND model numbers) Object_CreateLevelObjects gives each type;
 * the _LATE models replace their sets from LEVEL_311_TUNNELS on. A switch panel set is
 * SWITCH_FRAME_COUNT frames. LEVEL_OBJECT_MODEL_STATIC is the model of
 * LEVEL_OBJECT_STATIC_FIRST, each later static type the next one. names: invented */
enum {
    LEVEL_OBJECT_MODEL_EXPLOSIVE = 0,
    LEVEL_OBJECT_MODEL_TARGET = 1, /* also the default */
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
 * holds (Object_SwitchPanelTask, Object_SwitchPanelActiveTask). names: invented */
enum {
    SWITCH_FRAME_IDLE = 0,
    SWITCH_FRAME_ACTIVE = 2,
    SWITCH_FRAME_HELD = 3,
    SWITCH_FRAME_COUNT = 4
};
/* levelObject_t.state of a switch panel once activated (Object_ActivateLevelObject): it stays on when
 * activated with its last charge, else it returns to idle after SWITCH_HOLD_TICKS. Activated, it
 * blinks SWITCH_ACTIVE_BLINKS times every SWITCH_ACTIVE_BLINK_TICKS frames. names: invented */
#define SWITCH_STATE_ON 1
#define SWITCH_STATE_REARMS 2
#define SWITCH_ACTIVE_BLINK_TICKS 4
#define SWITCH_ACTIVE_BLINKS 8
#define SWITCH_HOLD_TICKS 32
/* levelObjectRecord_t.state is in units of 16 frames (Object_CreateLevelObjects). name: invented */
#define LEVEL_OBJECT_STATE_SHIFT 4
/* Scale of a level object model (Object_DrawLevelObject), and of a tall one (Object_DrawLevelObjectTall).
 * names: invented */
#define LEVEL_OBJECT_SCALE 0xe00
#define LEVEL_OBJECT_TALL_SCALE 0xc00
#define LEVEL_OBJECT_TALL_SCALE_Y 0xd98
/* Object_LevelObjectSpinTask tilts an activated spinner by this much a frame down to the limit.
 * names: invented */
#define LEVEL_OBJECT_TILT_STEP 8
#define LEVEL_OBJECT_TILT_LIMIT ( -0xff )
/* Frames between switch panel blinks before activation, and after it. names: invented */
#define SWITCH_BLINK_TICKS 0x10
/* TASK.state[] slots of the switch panel tasks (Object_SwitchPanelTask, Object_SwitchPanelActiveTask):
 * the object, the blink countdown, the activated blinks left and the blink frame (0 or 1). names: invented */
enum {
    SWITCH_TASK_OBJECT,
    SWITCH_TASK_TIMER,
    SWITCH_TASK_BLINKS,
    SWITCH_TASK_FRAME
};
/* Object_ExplodeBlockObject: explosions start a cell beyond the block, spread over a cell
 * across and BLOCK_BLAST_SPREAD_Y in height. names: invented */
#define BLOCK_BLAST_SPREAD_Y 0x100
/* Object_ExplosionDamage: height steps at which a cell shields the next ring. name: invented */
#define BLAST_STEP_LIMIT 13
/* Object_ExplosionDamage: the inner ring of 8 and outer ring of 16 cells around the blast; a door cell
 * (CELL_TYPE_DOOR) open by fewer than BLAST_DOOR_GAP height steps shields the ring behind it;
 * the player takes BLAST_NEAR_DAMAGE in the inner ring, BLAST_FAR_DAMAGE in the outer. names: invented */
#define BLAST_NEAR_CELLS 8
#define BLAST_FAR_CELLS 16
#define BLAST_DOOR_GAP 8
#define BLAST_NEAR_DAMAGE 50
#define BLAST_FAR_DAMAGE 25
/* Order of the inner ring (g_blastNear, its offsets in Object_ExplosionDamage): clockwise from the
 * -x -z corner. The outer ring runs the same way from its -x -z corner, so outer cell
 * BLAST_FAR_BEHIND(n) lies straight behind inner cell n, and a corner also shields the outer cells
 * either side of it. names: invented */
enum {
    BLAST_NEAR_NEG_X_NEG_Z,
    BLAST_NEAR_NEG_Z,
    BLAST_NEAR_POS_X_NEG_Z,
    BLAST_NEAR_POS_X,
    BLAST_NEAR_POS_X_POS_Z,
    BLAST_NEAR_POS_Z,
    BLAST_NEAR_NEG_X_POS_Z,
    BLAST_NEAR_NEG_X
};
#define BLAST_FAR_BEHIND( near ) ( ( near ) * 2 )
/* Object_ExplosionDamage: an actor in reach explodes this far above its position; an armed level
 * object in the inner ring gets BLAST_NEAR_HITS_MIN plus up to BLAST_NEAR_HITS_MASK hits, one in
 * the far ring BLAST_FAR_HITS_MIN plus up to BLAST_FAR_HITS_MASK. names: invented */
#define ACTOR_BLAST_HEIGHT 0x100
#define BLAST_NEAR_HITS_MIN 4
#define BLAST_NEAR_HITS_MASK 7
#define BLAST_FAR_HITS_MIN 8
#define BLAST_FAR_HITS_MASK 0xf
/* Object_TickLevelObject: an object whose timer runs out gets 1 plus up to this many hits. name: invented */
#define TIMER_HITS_MASK 7
/* Object_ExplodeBlockObject: explosions of a destroyed block (LEVEL_OBJECT_BLOCK_SMALL has one). name: invented */
#define BLOCK_BLAST_COUNT 6
/* Object_LevelObjectDebrisTask: the big explosion is this far below the object. name: invented */
#define EXPLOSIVE_BLAST_DROP 0x30
/* Object_ExplodeBlockObject: height of the explosions above the block. name: invented */
#define BLOCK_BLAST_HEIGHT 0x200

/* 16-byte level object placement record: an entry of g_levelMap.pickups, copied into a
 * levelObject_t by Object_CreateLevelObjects. */
typedef struct levelObjectRecord_s {
    u_char x;
    u_char z;
    u_char type;
    u_char flags;
    u_char state;      /* levelObject_t.state >> 4 */
    u_char unknown_05; /* identity-TODO: Object_CreateLevelObjects skips it; no runtime reader at level load or in play (analysis/runtime); 10 on types 0x14, 0x19, 0x1d and 0x20, 0 on every other type in all 35 levels */
    u_char  drop[2];
    u_char  width;
    u_char  depth;
    u_short commandBase;  /* levelObject_t.commandBase */
    u_short commandCount; /* levelObject_t.commandCount */
    u_short facing;
} levelObjectRecord_t;

/* Raises an actor slot's group count for a destroyed object and spawns the group when full (src/game_20000_3.c). */
extern void Actor_SpawnFromObject( levelObject_t *object, u_char index );
/* Explosion at a map position and its cell (src/object_render.c): destroyed level objects and particles hitting cells call it. */
extern void Object_ExplosionDamage( SVECTOR *position, mapCell_t *cell );

/* 16-byte lift placement record (Lift_PrepareRecords). */
typedef struct liftRecord_s {
    u_char x;
    u_char z;
    u_char width; /* Lift.width: cells the lift covers along x (Lift_Create) */
    u_char depth; /* Lift.depth: cells along z */
    u_char level; /* invented name: set to the cell's value by Lift_PrepareRecords */
    u_char base;  /* invented name: raised by the cell's value in Lift_PrepareRecords */
    u_char unknown_06; /* identity-TODO: copied to Lift.unknown_14 (Lift_Create, its one runtime reader); 1 in all 126 lift records */
    u_char flags;         /* Lift.flags */
    u_char delay;         /* Lift.delay */
    u_char hold;          /* Lift.hold */
    u_char wait;          /* Lift.wait: TRIGGER_LIFT counts it moves after */
    u_char count;         /* Lift.count: TRIGGER_LIFT counts so far */
    u_char model;         /* Lift.model */
    u_char unknown_0d[3]; /* identity-TODO: no runtime reader at level load or in play (analysis/runtime); three equal bytes, 0..8, in every lift record */
} liftRecord_t;

/* A lift's position is its first cell's centre, MAP_CELL_HALF in from its corner
 * (Lift_Create). invented name */
#define LIFT_HALF_CELL_SHIFT 8        /* log2(MAP_CELL_HALF) */
#define LIFT_FACE_HEIGHT_LIMIT 0x2000 /* bound on a lift model's vertex heights (Lift_MeasureModel) */
#define LIFT_MAX 32                   /* Lift_Alloc's table. name: invented */

/* A lift built from a liftRecord_t (src/lift.c; the unmatched Lift_Draw takes one too). */
typedef struct lift_s {
    SVECTOR    position; /* invented name: the lift's sound position (Snd_PlayAt); vy is its height */
    mapCell_t *cells;
    void      *faces;
    u_char     width;
    u_char     depth;
    u_char     low;
    u_char     high;
    u_char unknown_14; /* identity-TODO: liftRecord_t.unknown_06; no runtime reader with the lifts idle or moving (level 14), and the unmatched Lift_Draw (0x8003e734) loads nothing at +0x14 */
    u_char  flags;
    u_char  delay;
    u_char  hold;
    u_char  wait;
    u_char  count;
    u_short model; /* invented name: index into g_liftModels for faces (Lift_Create) */
    short   centreX;
    short   offset;
    short   centreZ;
} lift_t;

/* Lift.flags. name: inferred from Lift_TaskDelay/lower/raise/wait. */
enum {
    LIFT_UP = 0x01, /* at its high position */
    LIFT_RAISING = 0x02,
    LIFT_DOWN = 0x04, /* at its low position */
    LIFT_LOWERING = 0x08,
    LIFT_FAST = 0x10,     /* moves two height steps per frame */
    LIFT_RESTS_UP = 0x20, /* waits for triggers when up, returns after hold when down */
    LIFT_CYCLES = 0x40,   /* always returns after hold */
    LIFT_COUNTED = 0x80   /* already counted in tallies[34] */
};
#define LIFT_FAST_STEP 2 /* height steps a LIFT_FAST lift moves per frame. name: invented */

typedef struct actorSlot_s {
    u_char kind;
    u_char gridX;
    u_char gridZ;
    u_char unknown_03; /* identity-TODO: no runtime reader at level load or in play (analysis/runtime); 0xff in 1508 of the 1515 slots, else 6..65 on six face-hugger slots of level 15 and 1 on one warrior of level 4 */
    u_char direction;  /* invented name: copied to Actor.direction (Actor_Spawn); a FACING_* octant */
    u_char strength;   /* invented name: hit points before the difficulty scaling (Actor_SlotHealth) */
    u_char attachedIndex; /* invented name: copied to Actor.attachedIndex (Actor_Spawn) */
    u_char group;         /* invented name */
    u_char groupSize;     /* invented name */
    u_char unknown_09; /* identity-TODO: no runtime reader at level load or in play (analysis/runtime); 0 except 2 or 4 on 21 eggs (all with a face-hugger as attached slot) and 2 on 15 cocooned colonists */
    short viewCone;    /* invented name: copied to Actor.viewCone (Actor_Spawn) */
    short reach;       /* invented name: copied to Actor.reach (Actor_Spawn) */
    short speed;       /* invented name: copied to Actor.speed (Actor_Spawn) */
    short farRange;    /* invented name: copied to Actor.farRange (Actor_Spawn) */
    short nearRange;   /* invented name: copied to Actor.nearRange (Actor_Spawn) */
} actorSlot_t;

/* Level script: a command is a value and an opcode (bit 15 continues the step). */
typedef struct command_s {
    u_short value;
    u_short opcode;
} command_t;

/* Start of one script state: the label it begins at and two parameters. */
typedef struct commandStart_s {
    short  label;
    u_char param[2];
} commandStart_t;

/* Colours of a light record: one per colour of its palette entry (Light_Init). name: invented */
#define LIGHT_COLORS 4
/* lightRecord_t.colors[] slots: flashing 0 shows LIGHT_COLOR_REST, 1 LIGHT_COLOR_FLASH in palette
 * colour 0; LIGHT_MODE_FULL_PALETTE copies all four (Light_Init). names: invented */
enum {
    LIGHT_COLOR_REST,
    LIGHT_COLOR_FLASH,
    LIGHT_COLOR_PALETTE_2,
    LIGHT_COLOR_PALETTE_3
};

typedef struct lightRecord_s {
    CVECTOR colors[LIGHT_COLORS]; /* only r, g and b are copied (Light_Init) */
    short timer; /* invented name: frames left in the current phase; the light update (0x80023890, unmatched) counts it down and reloads it from flashTime or restTime */
    short flashCount; /* invented name: mode 1 flashes left; the light update counts it down per flash */
    short flashTime;  /* invented name: frames spent with flashing set (modes 1 and 2) */
    short restTime;   /* invented name: frames spent with flashing clear (modes 1 and 2) */
    u_char mode; /* invented name: 4 fills a whole palette entry from colors (Light_Init); in the light update 1 flashes flashCount times, 2 blinks, 3 and 5 flicker for random times, 0 stays */
    u_char flashing; /* invented name: the phase; 1 shows colors[1], 0 colors[0] (Light_Init, the light update). Until level reaches maxLevel the update reloads it, timer and flashCount from the level's record */
    u_char level;    /* invented name: raised by Light_Raise up to maxLevel */
    u_char maxLevel; /* invented name */
} lightRecord_t;

/* lightRecord_t.mode that fills a whole palette entry from colors (Light_Init). name: invented */
#define LIGHT_MODE_FULL_PALETTE 4

typedef struct levelMap_s {
    u_short kindCounts[TALLY_COUNT];
    short unknown_4a; /* identity-TODO: Sys_LevelParseMap neither clears nor fills it (its clear loop covers kindCounts only); no absolute reference to 0x800960aa in retail and no runtime access in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime) */
    short vertexCount; /* invented name: the first halfword of the map chunk; vertices span vertexCount SVECTORs (Sys_LevelParseMap) */
    short   faceCount;
    short   layoutRecordCount;
    short   actorSlotCount;
    short   objectRecordCount; /* invented name: count passed with objectRecords to Object_Spawn */
    short   pickupCount;       /* invented name: entry count handed to Object_CreateLevelObjects with pickups */
    short   doorRecordCount;   /* invented name */
    short   liftCount;         /* invented name: lift record count (Lift_InitRecords, Lift_Spawn) */
    u_short startX;            /* invented name: the player's starting cell column (Player_TaskStart) */
    u_short startZ;            /* invented name: the player's starting cell row (Player_TaskStart) */
    short startFacing; /* invented name: the player's starting facing (FACING_*); Player_TaskStart turns the view by it */
    u_char cellTriggerCount; /* invented name: records at cellTriggers, the first unused (Trigger_LoadCellTriggers); Sys_LevelParseMap caps it at 64 */
    u_char triggerCommandCount; /* invented name: 4-byte records at triggerCommands (Sys_LevelParseMap places lights after them, caps it at 64) */
    u_int      resources; /* invented name: resource-set flag bits, one sprite file each (Sprite_LoadLevelSprites) */
    u_short    width;
    u_short    height;
    u_short    offsetX;
    u_short    offsetZ;
    short      stateCount; /* invented name: script states set up by Command_InitStates (via Command_StartSequencer) */
    short      commandCount; /* invented name: script commands scanned for labels by Command_InitStates */
    short      lightCount;   /* invented name: records at lights, loaded by Light_Init */
    u_short    secretCount;
    mapCell_t *cells;
    struct dispatchRow_s *faces; /* see <MODEL.H> */
    actorSlot_t          *actorSlots;
    SVECTOR              *vertices;
    layoutRecord_t       *layoutRecords;
    objectRecord_t       *objectRecords; /* invented name */
    struct levelObjectRecord_s *pickups; /* invented name: entry table handed to Object_CreateLevelObjects, whose entries Object_ActivateLevelObject collects */
    doorRecord_t *doorRecords; /* invented name: handed to Door_LoadDefs with doorRecordCount (Door_LoadLevelDoors) */
    liftRecord_t *liftRecords; /* invented name */
    struct cellTrigger_s *cellTriggers; /* invented name: cell trigger records copied by Trigger_LoadCellTriggers into g_cellTriggers */
    struct levelCommand_s *triggerCommands; /* invented name: command table run by Trigger_RunLevelCommands (stored by Trigger_LoadLevelTriggers) */
    u_char         *mapChunk;
    commandStart_t *commandStarts; /* invented name */
    command_t      *commands;      /* invented name */
    lightRecord_t  *lights;        /* invented name: read at 0x80096110 with lightCount (Light_Load) */
} levelMap_t;

extern levelMap_t g_levelMap;

/* Screen position of the render time ViewRenderTask shows in debug mode. names: invented */
#define RENDER_TIME_X 0xa0
#define RENDER_TIME_Y 0x78

/* Model chunks of the level file, collected by LoadLevelMapFile (src/game_20000_3.c): 'D'
 * chunks (door models, g_doorModelCount of them) and 'L' chunks (lift models, g_liftModelCount).
 * names: runtime evidence, analysis/runtime/globals.json (level 1: 2 door models, no
 * lifts; level 14: 3 door models, 5 lifts) */
#define DOOR_MODEL_MAX 8
#define LIFT_MODEL_MAX 32
/* The level map file: a header, then chunks of a MAP_CHUNK_ID_SIZE-character id, a 32-bit
 * big-endian length and the data (LoadLevelMapFile). names: invented */
#define MAP_FILE_HEADER_SIZE 12
#define MAP_CHUNK_ID_SIZE 4
extern u_char *g_doorModels[DOOR_MODEL_MAX];
extern short   g_doorModelCount;
extern void   *g_liftModels[LIFT_MODEL_MAX];
extern short   g_liftModelCount;
/* name: inferred from use: mapCell_t.mask bits of the cells in view; actors and objects outside it are skipped */
extern u_short g_visibleCellMask;

/* name: invented; the map cell under map position x, z (world position plus offsetX, offsetZ) */
#define MAP_CELL( x, z ) \
    ( g_levelMap.cells + g_levelMap.width * ( ( z ) >> MAP_CELL_SHIFT ) + ( ( x ) >> MAP_CELL_SHIFT ) )
/* name: invented; map position x, z lies beyond the last cell column or row */
#define MAP_OUTSIDE( x, z ) \
    ( ( x ) >> MAP_CELL_SHIFT > g_levelMap.width || ( z ) >> MAP_CELL_SHIFT > g_levelMap.height )

/* Precomputed rays around the player's cell, read by the view-cone walker Model_DrawVisibleCells
 * (asm/view_cells.s) through g_viewCellsRays: the cell at (player x - dx, player z + dz), sorted
 * by angle (0x1000 = one turn) within each ring of increasing distance.
 * names: invented, from Model_DrawVisibleCells's use */
typedef struct viewRay_s {
    short angle;
    short dx;
    short dz;
} viewRay_t;

/* g_viewCellsBuckets: per ring of rays, the first ray of each 128-unit angle bucket. names: invented */
#define VIEW_RAY_RINGS 16
#define VIEW_RAY_BUCKETS 32
/* src/game_20000_3.c: the tables Model_DrawVisibleCells (asm/view_cells.s) walks */
extern short ( *g_viewCellsBuckets )[VIEW_RAY_BUCKETS];
extern viewRay_t *g_viewCellsRays;

/* Secret found flags: [0] is set at level start, ViewRenderTask sets the entry of the current cell's
 * secret; the results screen counts [1..255]. name: runtime evidence (medium; no secret
 * entered at runtime), analysis/runtime/globals.json */
#define SECRET_IDS 256 /* one flag per u_char secret id. name: invented */
extern unsigned char g_secretsFound[SECRET_IDS];

/* Level trigger commands: the first byte of a 4-byte g_levelCommands record {code, next,
 * operand, operand}; next is TRIGGER_CHAIN_END after the last command of a chain.
 * names: inferred from the function each code calls (src/level_triggers.c) */
enum {
    TRIGGER_LIGHT_RAISE = 0,      /* Light_Raise */
    TRIGGER_DOOR_POWER = 1,       /* Door_Power */
    TRIGGER_MAP_OBJECT = 2,       /* Object_Trigger */
    TRIGGER_ACTOR_GROUP = 3,      /* Actor_GroupAdd */
    TRIGGER_SWITCH = 4,           /* Object_ActivateLevelObject; the player's use button */
    TRIGGER_LIFT_LAST = 5,        /* Lift_TriggerLast */
    TRIGGER_DOOR_POWER_FINAL = 6, /* Door_PowerFinal; the player's use button */
    TRIGGER_LIFT = 7,             /* Lift_Trigger */
    TRIGGER_LEVEL_END = 8,        /* sets GAME_FLAG_LEVEL_COMPLETE */
    TRIGGER_COMMAND_STATE = 9     /* Command_AddState */
};
#define TRIGGER_CHAIN_END 0xff
/* Bytes of a g_levelCommands record: the code, the next command, and the two operands every
 * handler takes (an amount and the index of the door, lift, light, record or group it acts on).
 * names: invented, from the handlers' parameters */
typedef struct levelCommand_s {
    u_char code; /* TRIGGER_* handler */
    u_char next; /* next record in the chain, TRIGGER_CHAIN_END after the last */
    u_char amount;
    u_char index;
} levelCommand_t;
/* Bytes of a g_cellTriggers record (mapCell_t.trigger numbers them from 1). names: invented */
typedef struct cellTrigger_s {
    u_char flags;   /* TRIGGER_BY_* */
    u_char command; /* first levelCommand_t of its chain */
    u_char charges; /* runs left, TRIGGER_ENDLESS for no limit */
    u_char pad;     /* never read */
} cellTrigger_t;
/* Cell trigger record {flags, first command, charges, -} (g_cellTriggers): who sets it off,
 * and the charge count that never runs out. names: inferred from the four callers */
#define TRIGGER_BY_PLAYER 0x01     /* Trigger_PlayerCell */
#define TRIGGER_BY_SHOT 0x02       /* Trigger_ShotCell */
#define TRIGGER_BY_ACTOR 0x04      /* Trigger_ActorCell */
#define TRIGGER_BY_ENEMY_SHOT 0x08 /* Trigger_EnemyShotCell */
#define TRIGGER_ENDLESS 0xff

/* src/level_triggers.c: runs a level command */
extern void Trigger_RunLevelCommands( u_char index );

/* src/map_objects.c: throws an object from a level object */
extern void Object_LevelObjectDropItem( levelObject_t *source, u_char index );

struct TASK;
struct particle_s;
struct actor_s;

/* src/doors.c */
extern int Door_Power( int step, int index );
extern int Door_PowerFinal( int step, int index );
extern void Door_PrepareModels( void );
extern void Door_Alloc( void );
extern void Door_Init( void );

/* src/game_20000_3.c */
extern void ViewRenderTask( struct TASK *task );
extern void LoadLevelMapFile( levelMap_t *level );
extern void ClearFlowField( void );
extern void ResetLevelCells( levelMap_t *level );
extern void FillFlowField( layoutRecord_t *start, short index );

/* src/level_triggers.c */
extern void Trigger_LoadLevelTriggers( void );
/* src/level_triggers.c: runs the triggers of the player's cell; buttons are the pressed use buttons */
extern void Trigger_PlayerCell( int buttons );
extern void Trigger_ActorCell( struct actor_s *actor );
extern void Trigger_ShotCell( struct particle_s *particle );
extern void Trigger_EnemyShotCell( struct particle_s *particle );

/* src/lift.c */
extern int Lift_Trigger( int amount, u_char index );
extern int Lift_TriggerLast( int amount, u_char index );
extern void Lift_PrepareModels( void );
extern void Lift_Alloc( void );
extern void Lift_Spawn( void );

/* src/lights.c */
extern void Light_Raise( int amount, int index );
extern void Light_Load( void );

/* src/map_objects.c */
extern void Object_ActorDropItem( struct actor_s *actor );
extern void Object_Init( void );
extern void Object_LoadPickupModels( void );
extern void Object_PickupsTask( struct TASK *task );

/* src/object_render.c */
extern int Object_ActivateLevelObject( u_char index, u_char status );
extern void Object_LoadLevelObjectModels( void );
extern void Object_InitLevelObjects( void );
extern void Object_TickLevelObject( int index, int elapsed );

/* mapCell_t.type: slopes are 45..56, three bands of four facings; Player_Controls looks up
 * or down by facing. names: invented */
#define CELL_TYPE_SLOPE 0x2d
#define CELL_SLOPE_BAND 4
/* The slope bands: a step (the half toward its facing is MAP_SLOPE_RISE steps up) and two ramps
 * with the same floor (FloorHeight); and the facing within a band, the side the cell rises
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
#define CELL_TYPE_SLOPE_AT( band, facing ) ( CELL_TYPE_SLOPE + ( band ) * CELL_SLOPE_BAND + ( facing ) )

/* mapCell_t.type values below the slopes. ResetLevelCells blocks CELL_TYPE_SOLID and the walls
 * (CELL_TYPE_WALL up to the diagonals); particles explode on CELL_TYPE_WALL
 * (Particle_HitFloorOrCeiling) and explosions stop at every type from it (Object_ExplosionDamage).
 * A door cell's ceiling moves with its door (src/doors.c); particles, explosions and the automap
 * treat it apart. DiagonalClipX/_z keep positions off the solid half of a diagonal cell,
 * named by the corner (low or high x and z within the cell) that half covers.
 * CELL_TYPE_SLOW and CELL_TYPE_LIQUID_DEEP halve the player's acceleration and top speed unless
 * protective boots are on (Player_Accelerate); shots hitting a wall from a liquid cell splash
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
