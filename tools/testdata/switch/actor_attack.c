/* Frozen copy of actor_attack from main 36b6c838 with project headers inlined, so the
   switch-table tests do not depend on how live units and headers change. */
#include <sys/types.h>
#include <LIBGTE.H>
/* ---- LEVEL.H (frozen copy) ---- */
/*
 * Level state at 0x80096060 (g_screen). Requires <sys/types.h> and <LIBGTE.H>.
 * Retail forms &offset_x as 0x800960a6 + 0x26 and reaches width (-4), offset_z (+2)
 * and cells (+0xc) from that base, so 0x80096060..0x8009610f is one object. The word at
 * 0x80096110 continues the count/pointer pairing (state_count, command_count, light_count at
 * +0x70..+0x74 with command_starts, commands, lights at +0xa8..+0xb0), and f_23800 passes it
 * together with light_count, so the object ends at 0x80096113.
 * Names without evidence are marked invented; unidentified members are identity-TODO.
 */
#ifndef LEVEL_H
#define LEVEL_H

typedef struct MapCell {
    struct DispatchRow *face; /* see <MODEL.H> */
    u_char occupant; /* actor index of the actor in the cell (f_2cb38, actor_line_of_sight) or a level object's occupant value; particles hit it (f_319bc) */
    u_char secret_index;
    u_char layout_index; /* invented name: copied to Actor.layout_index when an actor enters the cell (f_2cb38) */
    u_char light;
    u_short mask;
    u_char type;
    u_char value;
    u_char marker;
    u_char ceiling; /* invented name: door code moves it, f_20acc defaults it to value + 0x30 */
    u_char palette; /* invented name: 16-byte palette entry the cell's models are lit with (f_1f820, f_26edc) */
    u_char unknown_0f; /* identity-TODO */
} MapCell;

typedef struct LayoutRecord {
    u_char x, y;
    u_char flow; /* invented name: breadth-first value written by f_2152c, climbed by f_2de24 */
    u_char flags;
    u_char links[4];
} LayoutRecord;

typedef struct MapProbe {
    u_char x;
    u_char y;
    u_char unknown_02[3]; /* identity-TODO */
    u_char value;
    u_char unknown_06[2]; /* identity-TODO */
} MapProbe;

/* 8-byte object placement record (map_objects_spawn, map_object_trigger). */
typedef struct ObjectRecord {
    u_char x;
    u_char z;
    u_char kind;
    u_char unknown_03; /* identity-TODO */
    u_char unknown_04; /* identity-TODO */
    u_char triggers; /* invented name: raised by map_object_trigger, the object spawns when it reaches required */
    u_char required; /* invented name: 0 spawns the object at level start (map_objects_spawn) */
    u_char unknown_07; /* identity-TODO */
} ObjectRecord;

/* name: invented; the model of each map object kind (map_objects_spawn, the pause menu's item display).
 * identity-TODO: game_20000_9 views 0x80096bfc, inside it, as u_long[22]. */
extern struct DispatchRow *g_object_models[];
/* Raises a record's trigger count and spawns its object when it is reached (src/map_objects.c). */
extern void map_object_trigger(int amount, u_char index);

/* Pool node allocated by map_object_alloc and linked by map_object_link. */
typedef struct MapObject {
    SVECTOR position;
    SVECTOR unknown_08; /* identity-TODO */
    SVECTOR rotation; /* invented name: RotMatrix input of map_object_draw, which spins vy by 16 per frame */
    struct DispatchRow *model; /* invented name: g_object_models[kind] */
    MapCell *cell;
    u_char kind;
    u_char unknown_21; /* identity-TODO */
    u_char unknown_22; /* identity-TODO */
    u_char unknown_23[5]; /* identity-TODO */
    struct MapObject *next;
    struct MapObject *prev;
} MapObject;

/* 40-byte level object: an entry of g_screen.pickups (level_objects_create), the block of
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
    u_short facing; /* invented name: octant an actor spawned by f_29734 faces */
    u_char unknown_24[4]; /* identity-TODO */
} LevelObject;

/* Raises an actor slot's group count for a destroyed object and spawns the group when full (src/game_20000_3.c). */
extern void f_29734(LevelObject *object, u_char index);

/* 16-byte lift placement record (lift_records_prepare). */
typedef struct LiftRecord {
    u_char x;
    u_char z;
    u_char unknown_02[2]; /* identity-TODO */
    u_char level; /* invented name: set to the cell's value by lift_records_prepare */
    u_char base; /* invented name: raised by the cell's value in lift_records_prepare */
    u_char unknown_06[10]; /* identity-TODO */
} LiftRecord;

typedef struct ActorSlot {
    u_char kind;
    u_char grid_x;
    u_char grid_z;
    u_char unknown_03[2]; /* identity-TODO */
    u_char strength; /* invented name: hit points before the difficulty scaling (actor_slot_health) */
    u_char unknown_06; /* identity-TODO */
    u_char group; /* invented name */
    u_char group_size; /* invented name */
    u_char unknown_09; /* identity-TODO */
    short view_cone; /* invented name: copied to Actor.view_cone (f_28a30) */
    short reach; /* invented name: copied to Actor.reach (f_28a30) */
    short speed; /* invented name: copied to Actor.speed (f_28a30) */
    short far_range; /* invented name: copied to Actor.far_range (f_28a30) */
    short near_range; /* invented name: copied to Actor.near_range (f_28a30) */
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
    CVECTOR colors[4]; /* only r, g and b are copied (f_235ac) */
    short unknown_10; /* identity-TODO */
    short unknown_12; /* identity-TODO */
    short unknown_14; /* identity-TODO */
    short unknown_16; /* identity-TODO */
    u_char mode; /* invented name: 4 fills a whole palette entry from colors (f_235ac) */
    u_char unknown_19; /* identity-TODO: 1 selects colors[1] for the palette entry */
    u_char level; /* invented name: raised by light_raise up to max_level */
    u_char max_level; /* invented name */
} LightRecord;

typedef struct Level {
    u_short kind_counts[0x27];
    short face_count;
    short layout_record_count;
    short actor_slot_count;
    short object_record_count; /* invented name: count passed with object_records to map_objects_spawn */
    short pickup_count; /* invented name: entry count handed to level_objects_create with pickups */
    short probe_count;
    short lift_count; /* invented name: lift record count (lift_records_init, lifts_spawn) */
    u_short unknown_5c; /* identity-TODO */
    u_short unknown_5e; /* identity-TODO */
    short unknown_60; /* identity-TODO */
    u_char unknown_62; /* identity-TODO: count passed with unknown_9c to f_3d860 */
    u_char unknown_63; /* identity-TODO */
    u_int resources; /* invented name: resource-set flag bits, one sprite file each (f_1d114) */
    u_short width;
    u_short height;
    u_short offset_x;
    u_short offset_z;
    short state_count; /* invented name: script states set up by command_states_init (via command_sequencer_start) */
    short command_count; /* invented name: script commands scanned for labels by command_states_init */
    short light_count; /* invented name: records at lights, loaded by f_235ac */
    u_short secret_count;
    MapCell *cells;
    struct DispatchRow *faces; /* see <MODEL.H> */
    ActorSlot *actor_slots;
    SVECTOR *vertices;
    LayoutRecord *layout_records;
    ObjectRecord *object_records; /* invented name */
    u_long pickups; /* invented name: entry table handed to level_objects_create, whose entries level_object_activate collects */
    MapProbe *probes;
    LiftRecord *lift_records; /* invented name */
    u_char *unknown_9c; /* identity-TODO: 4-byte records copied by f_3d860 */
    u_long trigger_commands; /* invented name: 4-byte command table run by level_commands_run (stored by level_triggers_load) */
    u_char *map_chunk;
    CommandStart *command_starts; /* invented name */
    Command *commands; /* invented name */
    LightRecord *lights; /* invented name: read at 0x80096110 with light_count (f_23800) */
} Level;

extern Level g_screen;
/* name: inferred from use: MapCell.mask bits of the cells in view; actors and objects outside it are skipped */
extern u_short g_visible_cell_mask;

/* name: invented; the map cell under map position x, z (world position plus offset_x, offset_z) */
#define MAP_CELL(x, z) (g_screen.cells + g_screen.width * ((z) >> 9) + ((x) >> 9))
/* name: invented; map position x, z lies beyond the last cell column or row */
#define MAP_OUTSIDE(x, z) ((x) >> 9 > g_screen.width || (z) >> 9 > g_screen.height)

#endif
/* ---- TRACK.H (frozen copy) ---- */
/*
 * Animation track entry: a sprite frame list and its command stream. Requires <sys/types.h>.
 * Indexed by the weapon view (f_3c8ec) and by actors (actor_play_animation), 8 bytes each.
 */
#ifndef TRACK_H
#define TRACK_H

typedef struct TrackEntry {
    struct SpriteFrame *frames; /* frame list the script indexes; becomes Sprite.frames (actor_play_animation) */
    u_short *stream; /* invented name: first word is the duration, commands follow */
} TrackEntry;

#endif
/* ---- SPRITEUV.H (frozen copy) ---- */
/*
 * The texture half of a POLY_FT4: four uv pairs with clut and tpage, copied into
 * packets word by word. Requires <sys/types.h>.
 */
#ifndef SPRITEUV_H
#define SPRITEUV_H

typedef struct {
    u_char u0, v0;
    u_short clut;
    u_char u1, v1;
    u_short tpage;
    u_char u2, v2;
    u_short pad1;
    u_char u3, v3;
    u_short pad2;
} SpriteUV;

#endif
/* ---- SPRITE.H (frozen copy) ---- */
/*
 * Animated sprite: texture block, current frame, animation script and VRAM slot.
 * Actors embed one at +0x80. sprite_step steps the script, f_21ef4 runs its commands,
 * f_21a80 uploads the frame's pixels to the VRAM slot and the bind code at
 * 0x8002a714..0x8002aa54 points the texture block at that slot.
 * Requires <sys/types.h>, <LIBGTE.H> and <SPRITEUV.H>. Names without evidence are
 * marked invented; unidentified members are identity-TODO.
 */
#ifndef SPRITE_H
#define SPRITE_H

/* 12-byte animation frame; frame lists are indexed by script commands (sprite_step). */
typedef struct SpriteFrame {
    short x; /* offset from the sprite's screen position (f_22158) */
    short y;
    u_short w;
    u_short h;
    u_long *pixels;
} SpriteFrame;

/* VRAM slot a frame is uploaded to (lists at 0x8006a5a0 and 0x8006a5dc). */
typedef struct VramSlot {
    u_short tpage;
    u_short x;
    u_short y;
    u_char locked; /* invented name: set while hugger_overlay_start holds the slot, cleared by hugger_cling_task */
    u_char users;
    u_long *pixels; /* frame pixels the slot currently holds */
} VramSlot;

typedef struct Sprite {
    SpriteUV uv;
    SpriteFrame *frame;
    SpriteFrame *frames;
    short *script; /* invented name: animation command stream (f_21ef4) */
    short *loop; /* invented name: command f_21ef4 jumps back to */
    u_short *clut;
    VramSlot *slot;
    DVECTOR screen; /* RotTransPers output (actor_project) */
    short depth;
    short unknown_2e; /* identity-TODO */
    u_short remaining; /* invented name: ticks left on the current command (sprite_step) */
    u_short duration; /* invented name */
    u_char unknown_34[2]; /* identity-TODO */
    u_short flags;
    short sound; /* invented name: sound id set by script command 0x1xx (f_21ef4), played with flag 0x10 (f_2fc74) */
    short repeat; /* invented name: loop counter of command 0x02xx (f_21ef4) */
} Sprite;

/* The two VRAM slot lists (0x8006a5a0, 0x8006a5dc) and their lengths. */
/* name: inferred from use: frames are uploaded to its slots; vram_slots_reserve locks four of them on the boss levels */
extern VramSlot *g_vram_slots_a;
/* name: inferred from use: the second list game_20000_6 picks slots from; vram_slots_release frees all of it */
extern VramSlot *g_vram_slots_b;
extern short g_vram_slot_count_a;
extern short g_vram_slot_count_b;

/*
 * name: inferred from actor_project and f_277cc: RotTransPers depth << 2 of the last projected
 * sprite or particle; sprites keep it as their depth, >> 5 is the ordering-table index.
 */
extern int g_projected_depth;

#endif
/* ---- CAMERA.H (frozen copy) ---- */
#ifndef CAMERA_H
#define CAMERA_H


/*
 * Requires <sys/types.h> and <LIBGTE.H>.
 * Player view object at 0x80099fd4..0x8009a0db. Retail forms 0x80099fd4 and 0x80099fe4
 * from the 0x80099fec base (0x8003ac70) and 0x80099fd4/0x8009a0d0 from the 0x80099fdc
 * base (0x8003986c), so the whole span is one object. pitch, yaw and roll are copied
 * into the view rotation by the code at 0x800398ec.
 */
typedef struct ViewCamera {
    SVECTOR position;
    short pitch;
    u_short yaw;
    u_short roll;
    short unknown_0e; /* identity-TODO */
    SVECTOR rotation;
    SVECTOR movement; /* invented name: rotated by rotation at 0x8003ac70 */
    Sprite weapon_sprite; /* invented name: the weapon view; f_3c8ec steps it with sprite_step, weapon_select picks its clut and clears its flags */
    TrackEntry *weapon_sequence; /* invented name: animation table of the selected weapon (weapon_select), 0 for none */
    short weapon_animation; /* invented name: sequence entry the weapon view plays; 3 when firing (f_3a118), 0 on selection (weapon_select) */
    short weapon; /* invented name: current weapon 0..4, stored by weapon_select, the argument of weapon_select_slot in the pause menu */
    struct MapCell *cell; /* invented name: cell under the player; its type (5, 9) slows movement at 0x80039b4c */
    int item_flags; /* invented name: pickup bits; the pause menu tests 0x80 auto mapper, 8, 0x10, 0x20, 0x40 */
    short health; /* invented name: reduced by damage (player_damage_health), set to 100 by player_reset, death flag at 0 */
    short armour; /* invented name: absorbs damage before health (player_damage) */
    short unknown_70; /* identity-TODO */
    short pistol_ammo; /* invented name: decremented per pistol shot unless g_cheat_flags & 0x20 (f_2576c); the pause menu counts it plus 15 per clip */
    short pistol_clips; /* invented name: the pause menu shows pistol_ammo + 15 x pistol_clips; the password stores it */
    short shotgun_ammo; /* invented name: decremented per shotgun shot unless g_cheat_flags & 0x20 (f_257d4) */
    short pulse_rifle_ammo; /* invented name: decremented per pulse rifle shot unless g_cheat_flags & 0x20 (f_2585c) */
    short pulse_rifle_clips; /* invented name: the pause menu shows pulse_rifle_ammo + 100 x pulse_rifle_clips */
    short grenades; /* invented name: spent by f_3a118; also enables the pause menu's pulse rifle option */
    short flamethrower_fuel; /* invented name: spent by f_25b44, the pause menu's flamethrower count */
    short smart_gun_ammo; /* invented name: the pause menu's smart gun count with smart_gun_clips */
    short smart_gun_clips; /* invented name: the pause menu shows smart_gun_ammo + 100 x smart_gun_clips */
    short unknown_84; /* identity-TODO */
    short batteries; /* invented name: spent by entries of type 0x16/0x1a, "Battery required" when 0 (level_object_activate) */
    short lamp_charges; /* invented name: decremented when the pause menu lights the shoulder lamp */
    short lamp_timer; /* invented name: set to 1800 when the lamp is lit (pause_menu_select) */
    short night_vision_charges; /* invented name: decremented when the pause menu starts night vision */
    short night_vision_timer; /* invented name: set to 2700 when night vision starts (pause_menu_select) */
    short noise_timer; /* invented name: set to 0x200 when a door opens (f_22920), tested by f_2e8a0 */
    short flash; /* invented name: screen flash; 0x20/0x40 when the player fires (f_26064, f_257d4), 0x80 when hit (f_3b350) */
    short flash_heading; /* invented name: direction of the flash: yaw + 0x800 when firing, the hit direction when hit */
    short unknown_96; /* identity-TODO */
    short invulnerable_timer; /* invented name: blocks damage while nonzero (player_damage), ticked by player_tick */
    /*
     * invented name: level tallies, one array (f_1b408 sums [1..14] and indexes it by a
     * per-level class table, parallel to Level.kind_counts). [0..32] kills by actor kind;
     * [33] switches completed once (f_22c24, f_22d08); [34] lifts activated once (lift_task_wait);
     * [35] copy of unknown_96 and [36] the kill total, both stored by f_1b408.
     * Constant-index accesses compile to the same absolute address as named members.
     */
    u_short tallies[37];
    short damage_timer; /* invented name: set to 30 after damage, blocks damage while nonzero, tints the HUD */
    short turn_speed_max; /* invented name: caps turn_speed (player_yaw_decrease) */
    short turn_speed; /* invented name: grows by 4 per frame, added to yaw (player_yaw_decrease) */
    short pitch_speed_max; /* invented name: caps pitch_speed (player_pitch_decrease) */
    short pitch_speed; /* invented name: grows by 8 per frame, added to pitch (player_pitch_decrease) */
    short pitch_min; /* invented name: lower pitch clamp (player_pitch_decrease) */
    short pitch_max; /* invented name: upper pitch clamp (player_pitch_increase) */
    short pitch_rest; /* invented name: pitch returns to it by 8 per frame (player_pitch_recentre) */
    short unknown_f4; /* identity-TODO */
    short speed_max; /* invented name: caps movement.vz (player_accelerate) */
    short acceleration; /* invented name: added to movement.vz per frame (player_accelerate) */
    short deceleration; /* invented name: subtracted from movement.vz per frame (player_decelerate) */
    short view_height; /* invented name: added to position.vy for the view at 0x8003993c */
    short shot_offset_x; /* invented name: added to position.vx for shots */
    short bob; /* invented name: view bob height, min(sin, cos) of bob_phase >> 6 (player_view_bob) */
    short unknown_102; /* identity-TODO */
    u_short bob_phase; /* invented name: advanced by 1.5 x movement.vz; footstep sound when it wraps (player_view_bob) */
    u_char unknown_106; /* identity-TODO */
    u_char unknown_107; /* identity-TODO */
} ViewCamera;

extern ViewCamera g_reference_position;

#endif
/* ---- ACTOR.H (frozen copy) ---- */
/*
 * Actor: one enemy (or other animated map object) of the actor pool.
 * Requires <sys/types.h>, <LIBGTE.H>, <LEVEL.H>, <TRACK.H>, <SPRITEUV.H> and <SPRITE.H>.
 *
 * Pool: actor_pool_alloc allocates 0x6400 bytes (ACTOR_COUNT records of 0xc8) into g_actor_pool;
 * actor_pool_init stores each record's index at +0x70 and threads them onto the free list
 * g_actor_free (actor_free pushes, f_288c4 pops). Live actors are linked through next/prev
 * on g_live_actors (f_288f0 links, actor_unlink unlinks). actor_project appends visible
 * actors to g_visible_actors.
 * Names without evidence are marked invented; unidentified members are identity-TODO.
 */
#ifndef ACTOR_H
#define ACTOR_H

#define ACTOR_COUNT 128

/* Actor.action values shared by every kind; 0..3 are per-kind behaviour states. */
enum {
    ACTOR_LEAP = 4, /* invented name: set with an upward velocity by f_2eac0, f_29734 and f_2facc */
    ACTOR_HURT = 5, /* invented name: set by actor_hurt while stun_timer runs */
    ACTOR_DYING = 6, /* invented name: set by actor_die */
    ACTOR_DEAD = 7 /* invented name: the death animation has finished (f_2fc74) */
};

typedef struct Actor {
    SVECTOR position; /* RotTransPers input (actor_project) */
    SVECTOR scale; /* invented name: 0x100 per axis when actor_to_ceiling changes kind */
    VECTOR player_delta; /* invented name: |player - position| per axis (actor_measure_player) */
    VECTOR target_delta; /* invented name: |target - position| per axis (actor_measure_target) */
    SVECTOR velocity; /* rotated by heading (actor_set_velocity) */
    SVECTOR target; /* invented name: waypoint, set by f_2dc70 and the layout walk f_2de24 */
    short view_cone; /* invented name: the player is seen within +-view_cone of angle (actor_sees_player); ActorSlot.view_cone */
    short angle; /* heading, stepped toward desired_angle by turn_rate (actor_turn_toward) */
    short desired_angle; /* invented name */
    short turn_rate; /* invented name */
    short far_range; /* invented name: distance class 2/3 boundary (actor_measure_player) */
    short near_range; /* invented name: distance class 1/2 boundary (actor_measure_player) */
    short shot_speed; /* invented name: added to the speed of spat projectiles (f_24e00, f_25120), 0 at spawn (f_28a30) */
    short stun_timer; /* invented name: counts down; blocks damage while nonzero (f_2fc74, actor_damage) */
    short reach; /* invented name: f_2e8a0 attacks within reach / 2, or reach with noise; ActorSlot.reach */
    short idle_timer; /* invented name: counts down; idle action waits for it (f_2fc74) */
    short player_angle; /* invented name: heading from position to the player (f_2fc74) */
    short target_angle; /* invented name: heading from position to target (f_2dc70) */
    short health; /* invented name: reduced by damage, killed at 0 (actor_damage) */
    short height; /* invented name: spawn height offset (f_2fc04) */
    short ceiling; /* invented name: ceiling_height ceiling height under the actor (f_2cb38) */
    short speed; /* invented name: ActorSlot.speed at spawn (f_28a30); actor_hurt knocks back at speed >> 2; f_2fc04 reads it as a slot index (identity-TODO) */
    u_int player_distance; /* invented name: distance_xz distance to the player (actor_measure_player) */
    u_int target_distance; /* invented name: distance_xz distance to target (actor_measure_target) */
    int unknown_68; /* identity-TODO: cleared at spawn (f_28a30) */
    u_char flags;
    u_char direction; /* invented name: octant of angle */
    u_char shown_direction; /* invented name: octant last sent to the animation (f_2bfc0), 0xff forces a resend */
    u_char action; /* invented name: behaviour state */
    u_char index; /* pool index (actor_pool_init) */
    u_char unknown_71; /* identity-TODO */
    u_char attached_index; /* invented name: actor slot spawned by f_2fbbc, 0xff for none */
    u_char player_class; /* invented name: distance class 0..4 to the player (actor_measure_player) */
    u_char kind;
    u_char target_class; /* invented name: distance class 0..4 to target (actor_measure_target) */
    u_char layout_index; /* invented name: layout record the actor walks from (f_2de24) */
    u_char slot_index; /* invented name: ActorSlot the actor was spawned from (f_28a30) */
    MapCell *cell; /* invented name: its mask is tested against g_visible_cell_mask for visibility */
    TrackEntry *animations; /* invented name: animation table indexed by actor_play_animation */
    Sprite sprite;
    struct TASK *task; /* invented name: the actor's update task; its state[1] is the attack phase */
    struct Actor *next;
    struct Actor *prev;
} Actor;

/* name: inferred from use: the rotation actor_path_clear and actor_set_velocity turn a step or the velocity by */
extern SVECTOR g_velocity_rotation;

/* name: inferred from use: actor_pool_alloc allocates the ACTOR_COUNT records into it */
extern Actor *g_actor_pool;
/* name: inferred from use: actor_free pushes and actor_alloc pops it */
extern Actor *g_actor_free;
/* name: inferred from use: actors are linked through next/prev from it */
extern Actor *g_live_actors;
/* name: inferred from use: actor_project appends each visible actor */
extern Actor *g_visible_actors[ACTOR_COUNT];
/* name: inferred from use: actor_project counts the entries of g_visible_actors, actor_pool_init clears it */
extern short g_visible_actor_count;

extern void actor_free(Actor *actor);
extern void actor_pool_init(void);
extern void actor_pool_alloc(void);
extern Actor *actor_alloc(void);
extern void actor_link(Actor *actor);
extern void actor_unlink(Actor *actor);

/* Actor behaviour shared across the actor units (src/actor_*.c, src/recovery_s2*.c, src/game_helpers6.c, src/game_20000_1.c). */
extern short actor_slot_health(ActorSlot *slot);
extern Actor *f_28a30(int slot);
extern void actor_play_animation(Actor *actor, short index);
extern void actor_to_ceiling(Actor *actor, short index);
extern void actor_to_floor(Actor *actor, short index);
extern void actor_project(Actor *actor);
extern void actor_die(Actor *actor);
extern void f_2cb38(Actor *actor);
extern void actor_measure_player(Actor *actor);
extern void actor_measure_target(Actor *actor);
extern int actor_path_clear(Actor *actor, short angle, u_int distance);
extern int actor_line_of_sight(Actor *actor, short angle, u_int distance);
extern void actor_turn_toward(Actor *actor, int angle);
extern void actor_stop(Actor *actor);
extern void actor_set_velocity(Actor *actor, short angle, short speed);
extern int actor_sees_player(Actor *actor);

#endif
/* ---- TASK.H (frozen copy) ---- */
/*
 * The tasker (src/tasker.c): a pool of TASKs on a free
 * list and an in-use list kept in priority order. TaskCreate takes an entry from the pool,
 * AddTASKToInUse links it by priority and TaskDispatch calls each callback with its own
 * entry; state[] is the callback's private storage.
 * Names: lineage: Forsaken PC Tasker.c (Probe, "The X Men", 1996) for the type, the
 * Next/Prev/Priority members, the tasker globals and functions; the rest invented.
 * Forsaken's tasker runs setjmp coroutines, this one plain callbacks.
 */
#ifndef TASK_H
#define TASK_H

/* name: lineage (Tasker.c) */
#define MAXTASKS 256

typedef struct TASK TASK;
struct TASK {
    int Priority;
    void (*callback)(TASK *); /* name: invented */
    unsigned int state[4]; /* name: invented */
    TASK *Next;
    TASK *Prev;
};

/* name: inferred from use: TaskDispatch continues with it; a task sets it to its successor.
 * TaskerInit clears it where Tasker.c clears TaskCrnt, but it holds the next TASK, not the current one. */
extern TASK *g_next_task;

extern TASK *TaskCreate(int priority, void (*callback)(TASK *));
extern void TaskDie(TASK *task);
extern void TaskDispatch(void);
extern void TaskerInit(void);
extern void TaskerAlloc(void);

#endif
/* ---- AUDIO.H (frozen copy) ---- */
/*
 * Sound, in two layers, and CD music.
 *  - src/audio.c: the SPU voice layer. A playing sound is a handle (voice in the low
 *    byte, the voice's play serial above it); voices expire by g_sound_clock.
 *  - src/audio_control.c: the game's sound API. Every call is a no-op unless
 *    audio_enable has run; samples come from sample banks loaded per level.
 *  - src/music_control.c: CD-DA music.
 * Requires <sys/types.h> and <LIBGTE.H>. Names without evidence are marked invented.
 */
#ifndef AUDIO_H
#define AUDIO_H

/* One request to voice_play. */
typedef struct PlayRequest {
    int sample;
    int volume;
    int balance;
    int pitch;
    int reverb;
} PlayRequest;

/* One 14-word sample header of a bank file. Field names inferred from the
 * SampleSlot members sample_bank_load copies them to. */
typedef struct SampleSource {
    long priority;
    long duration;
    long offset;
    long flags;
    long volume;
    long pitch;
    long ar;
    long a_mode;
    long dr;
    long sr;
    long s_mode;
    long sl;
    long rr;
    long r_mode;
} SampleSource;

typedef struct SampleBank {
    long count;
    long size;
    SampleSource sources[1];
} SampleBank;

/* SPU voice layer (src/audio.c) */
/* name: inferred from use: vblank_handler and sound_clock_tick advance it; voice expiry times are taken from it */
extern volatile unsigned short g_sound_clock;
/* name: inferred from use: SCALE_BY_SFX_LEVEL scales voice volumes by it; sound_set_volume stores the menu value / 10 */
extern int g_sfx_level;
extern unsigned short sound_clock_tick(void);
extern void voices_reset(void);
extern int voice_play(PlayRequest *request);
extern void spu_set_master_volume(int volume, int balance);
extern void voices_mute(void);
extern void voices_restore(int unused);
extern void spu_set_cd_volume(int volume, int balance);
extern int voice_set_volume(int handle, int volume, int balance);
extern int voice_set_pitch(int handle, int pitch);
extern int voice_set_note(int handle, int note);
extern int voice_stop(int handle);
extern int voice_of_handle(int handle);
extern int voice_reverb_on(int handle);
extern int voice_reverb_off(int handle);
extern unsigned long sample_bank_load(int first, SampleBank *data);
extern void sample_bank_free(unsigned long address);
extern void spu_init(void);
extern void voices_stop_all(void);
extern void sample_priorities_init(void);

/* Game sound API (src/audio_control.c) */
extern int audio_load_bank(short index, short argument);
extern short audio_free_bank(short key);
extern short audio_select_level_bank(short index);
extern int audio_init(void);
extern void audio_enable(void);
extern int audio_disable(void);
extern void sound_stop_all(void);
extern int sound_play_request(PlayRequest *request);
extern void sound_mute_all(void);
extern void sound_unmute_all(void);
extern int sound_stop(int handle);
extern int sound_adjust(int handle, int volume, int balance);
extern int sound_play(short sample, int pitch, int volume);
extern void sound_set_volume(int value);
/* identity-TODO: positional play at 0x8003d66c, not matched yet */
extern int sound_play_at(short sample, const SVECTOR *position, short mode);
extern int sound_play_at_position(int sample, const SVECTOR *position);
extern int sound_play_at_xz(int sample, short x, short z);
/* src/additional.c: plays a menu sample through its own PlayRequest */
extern void play_menu_sound(int sample);

/* CD music (src/music_control.c) */
extern void music_resume(void);
extern int music_init(void);
extern short music_play_track(int track);
extern int music_stop(void);
extern int music_pause(void);
extern int music_unpause(void);
extern void music_disable(void);
extern void music_enable(void);
extern void music_set_volume(int volume);
extern int music_poll(void);

#endif
/* ---- GAME.H (frozen copy) ---- */
/*
 * Game-wide state shared by the front end, the level loop and the game code.
 * Names invented; each is marked with the use it is inferred from.
 */
#ifndef GAME_H
#define GAME_H

/* name: inferred from use: main loads, plays and advances levels by it; the password and saves record it */
extern int g_level;
/* name: inferred from use: 0x20 player dead, 0x40 death finished, 4 level end, 0x48 ends main's level loop, 0x20000 demo */
extern unsigned int g_game_flags;
/* name: inferred from use: check_password sets its bits from the cheat codes; shots skip ammo use under 0x20, countdown_start does nothing under 4 */
extern unsigned int g_cheat_flags;
/* name: inferred from use: vblank_handler increments it every vertical blank */
extern volatile int g_frame_count;
/* name: inferred from use: set before drawing, cleared by vblank_handler; the buffer swap waits for 0 */
extern volatile int g_graphics_busy;
/* name: inferred from use: set_death_type stores it and play_death_movie switches on it */
extern short g_death_type;

/* Pad input (src/recovery_s10_02.c pad_poll, src/helpers.c pad_reset). */
/* name: inferred from use: pad_poll stores the buttons newly down this frame */
extern unsigned int g_pad_pressed;
/* name: inferred from use: pad_poll stores the PadRead result */
extern unsigned int g_pad_held;
/* name: inferred from use: pad_poll keeps the previous frame's held buttons */
extern unsigned int g_pad_previous;
/* name: inferred from use: pad_poll stores pressed buttons, then held buttons every 11 frames after 20 */
extern unsigned int g_pad_repeat;
/* name: inferred from use: pad_poll counts frames the buttons stay unchanged, up to 20 */
extern short g_pad_hold_frames;
/* name: inferred from use: pad_poll counts frames between auto-repeats */
extern short g_pad_repeat_clock;

extern void pad_poll(void);
extern void pad_reset(void);

#endif
/* ---- VIEW.H (frozen copy) ---- */
/*
 * The view transform: the camera position and angles, the GTE matrices built from
 * them each frame (view_transform_setup, src/strict12_view.c) and the vector
 * rotation helpers (src/game_round7_04.c) and the x-z
 * heading/distance helpers (src/recovery_s9_03.c, src/game_helpers14.c, src/recovery_s20_04.c). Requires <sys/types.h> and <LIBGTE.H>.
 */
#ifndef VIEW_H
#define VIEW_H

/* name: inferred from use: view_transform_setup builds the view transform from its negation; shots and particles start from it */
extern SVECTOR g_view_position;
/* name: inferred from use: view_transform_setup builds g_view_matrix from it; shots take its yaw and roll */
extern SVECTOR g_view_rotation;
/* name: inferred from use: view_transform_setup multiplies its matrix into the view; shots take -vx as their pitch */
extern SVECTOR g_view_pitch;
/* name: inferred from use: LoadAverageShort12 leaves position - g_view_position in it before ApplyMatrix */
extern SVECTOR g_relative_position;
extern MATRIX g_view_matrix;
extern MATRIX g_matrix_work;
extern VECTOR g_translation;
/* names: inferred from use: the p (depth-cue interpolation) and flag outputs of RotTransPers and RotAverageNclip4 */
extern long g_gte_interpolation;
extern long g_gte_flag;
/* name: inferred from use: the depth value of the last transform, fed to DpqColor */
extern long g_transform_result;

extern void view_transform_setup(void);
/* identity-TODO: builds a rotation matrix from three angles (0x80020178, not matched yet) */
extern int rotation_matrix(const SVECTOR *rotation, MATRIX *matrix);
extern void rotate_vector(const SVECTOR *rotation, SVECTOR *point, SVECTOR *result);
extern short point_heading_xz(SVECTOR *left, SVECTOR *right);
extern int point_distance_xz(SVECTOR *left, SVECTOR *right);
extern void aim_rotation(SVECTOR *left, SVECTOR *right, SVECTOR *out);
extern void rotate_vector_offset(const SVECTOR *rotation, SVECTOR *point, const SVECTOR *origin, SVECTOR *result);

#endif


extern int f_2e8a0(Actor *actor);
extern unsigned int random(void);
extern short hugger_overlay_start(Actor *actor);
extern void hugger_cling_task(TASK *node);
extern ActorSlot *g_actor_slots;
extern short get_difficulty(void);
extern void sprite_step(Sprite *sprite);
extern void f_24e00(Actor *actor);
extern void f_25120(Actor *actor);
extern void f_2bfc0(Actor *actor);
extern void f_2c0b4(Actor *actor);
extern void f_2c0fc(Actor *actor);
extern int f_2cfd0(Actor *actor);
extern int f_2dc70(Actor *actor);
extern int f_2e128(Actor *actor);
extern int f_2e5c0(Actor *actor);
extern int f_2e920(Actor *actor);
extern void f_2eac0(TASK *node, Actor *actor);
extern void f_3b324(int amount, int mode);
extern void f_3b350(int amount, int angle, int mode);

void f_2bfc0(Actor *actor)
{
    short direction = (actor->direction -
        (((actor->player_angle + 2304) >> 9) & 7)) & 7;
    if (actor->shown_direction != direction) {
        actor->shown_direction = direction;
        switch (actor->kind) {
        case 1:
            break;
        case 7:
            switch (direction) {
            case 5: case 6: case 7:
                actor->sprite.flags |= 32;
                break;
            default:
                actor->sprite.flags &= ~32;
                break;
            }
            actor_play_animation(actor, direction);
            break;
        case 2: case 3: case 4: case 5: case 6:
        case 8: case 9: case 11: case 12: case 14:
            switch (direction) {
            case 5: case 6: case 7:
                actor->sprite.flags |= 32;
                break;
            default:
                actor->sprite.flags &= ~32;
                break;
            }
            actor_play_animation(actor, direction);
            break;
        case 13:
            actor_play_animation(actor, direction);
            break;
        default:
            actor_play_animation(actor, direction);
            break;
        }
    }
}

void f_2c0b4(Actor *actor)
{
    short angle = actor->player_angle;
    actor->shown_direction = 255;
    actor->desired_angle = angle;
    actor->angle = angle;
    actor->direction = ((angle + 256) >> 9) & 7;
    f_2bfc0(actor);
}

void f_2c0fc(Actor *actor)
{
    short angle = actor->target_angle;
    actor->shown_direction = 255;
    actor->desired_angle = angle;
    actor->angle = angle;
    actor->direction = ((angle + 256) >> 9) & 7;
    f_2bfc0(actor);
}

unsigned int f_2c144(unsigned int position, unsigned int boundary,
                    short positive, short *offset)
{
    int beyond;
    unsigned int result;
    if (positive) {
        boundary = (boundary & ~511) + 712;
        beyond = boundary < position + *offset;
    } else {
        boundary = (boundary & ~511) - 201;
        beyond = boundary > position + *offset;
    }
    result = boundary;
    if (beyond)
        result = position + *offset;
    return result;
}

unsigned int f_2c650(unsigned int position, unsigned int boundary,
                    short positive, short *offset)
{
    int beyond;
    unsigned int result;
    if (positive) {
        boundary = (boundary & ~511) + 712;
        beyond = boundary < position + offset[2];
    } else {
        boundary = (boundary & ~511) - 201;
        beyond = boundary > position + offset[2];
    }
    result = boundary;
    if (beyond)
        result = position + offset[2];
    return result;
}

void f_2eac0(TASK *node, Actor *actor)
{
    actor->unknown_68 = 0x3c;
    actor->action = 4;
    if (actor->player_class < 2) {
        switch (actor->kind) {
        case 2:
            if (actor->player_class == 0 && actor->player_delta.vy < 0x601
                && !(g_reference_position.item_flags & 0x100) && hugger_overlay_start(actor)) {
                g_reference_position.item_flags |= 0x100;
                node->callback = hugger_cling_task;
                node->state[1] = 0x40;
                node->state[2] = 0;
                actor->cell->marker = 0;
                break;
            }
            actor_set_velocity(actor, actor->angle, 0x60);
            actor_play_animation(actor, 10);
            if (actor->velocity.vy == 0)
                actor->velocity.vy = 0x80;
            break;
        case 9:
            actor_to_floor(actor, 8);
            break;
        case 3:
            actor_set_velocity(actor, actor->angle, 0x40);
            actor_play_animation(actor, 12);
            if (actor->velocity.vy == 0)
                actor->velocity.vy = 0x80;
            break;
        case 8:
            actor_to_floor(actor, 9);
            break;
        case 7:
            goto random_strike;
        case 4:
            actor_play_animation(actor, 12);
            break;
        case 11:
        case 12:
            actor_play_animation(actor, 9);
            break;
        case 5:
        case 13:
            actor_play_animation(actor, 10);
            break;
        case 6:
        case 14:
            actor_play_animation(actor, 11);
            break;
        case 1:
        case 19:
            break;
        }
    } else {
        switch (actor->kind) {
        case 2:
            actor_set_velocity(actor, actor->angle, 0x60);
            actor_play_animation(actor, 10);
            if (actor->velocity.vy == 0)
                actor->velocity.vy = 0x80;
            break;
        case 3:
            actor_set_velocity(actor, actor->angle, 0x40);
            actor_play_animation(actor, 12);
            if (actor->velocity.vy == 0)
                actor->velocity.vy = 0x80;
            break;
        case 8:
            actor_to_floor(actor, 9);
            break;
        case 7:
        random_strike:
            switch ((short)(random() & 3)) {
            case 1:
                actor_play_animation(actor, 11);
                break;
            case 2:
                actor_play_animation(actor, 14);
                break;
            case 3:
                actor_play_animation(actor, 14);
                actor->sprite.flags |= 0x20;
                break;
            default:
                actor_play_animation(actor, 12);
                break;
            case 0:
                actor_play_animation(actor, 12);
                break;
            }
            break;
        case 4:
        case 6:
            actor_play_animation(actor, 12);
            break;
        case 11:
        case 12:
            actor_play_animation(actor, 9);
            break;
        case 13:
            actor_play_animation(actor, 10);
            break;
        case 5:
            actor_play_animation(actor, 11);
            break;
        case 14:
            switch ((short)(random() & 7)) {
            case 0:
            case 1:
            case 2:
                actor_play_animation(actor, 11);
                break;
            default:
                actor->flags |= 1;
                actor_play_animation(actor, 10);
                break;
            }
            break;
        case 1:
        case 19:
            break;
        }
    }
}

void f_2ed94(Actor *actor)
{
    if (actor->player_class < 2) {
        switch (actor->kind) {
        case 2:
        case 3:
            if (actor_sees_player(actor) && (actor->player_class == 0 || actor_path_clear(actor, actor->player_angle, actor->player_distance)))
                f_3b324(5, 4);
            break;
        case 4:
        case 5:
            if (actor_sees_player(actor) && (actor->player_class == 0 || actor_path_clear(actor, actor->player_angle, actor->player_distance)))
                f_3b350(10, (actor->angle + 0x800) & 0xfff, 1);
            break;
        case 6:
            if (actor_sees_player(actor) && (actor->player_class == 0 || actor_path_clear(actor, actor->player_angle, actor->player_distance)))
                f_3b350(15, (actor->angle + 0x800) & 0xfff, 0);
            break;
        case 7:
            if (actor_sees_player(actor) && (actor->player_class == 0 || actor_path_clear(actor, actor->player_angle, actor->player_distance)))
                f_3b350(25, (actor->angle + 0x800) & 0xfff, 2);
            break;
        case 1:
        case 19:
            break;
        }
    } else if (actor->player_class == 2) {
        switch (actor->kind) {
        case 2:
        case 3:
            if (actor_sees_player(actor) && actor_path_clear(actor, actor->player_angle, actor->player_distance))
                f_3b324(1, 4);
            break;
        case 4:
        case 5:
            if (actor_sees_player(actor) && actor_path_clear(actor, actor->player_angle, actor->player_distance))
                f_3b350(5, (actor->angle + 0x800) & 0xfff, 1);
            break;
        case 6:
            if (actor_sees_player(actor) && actor_path_clear(actor, actor->player_angle, actor->player_distance))
                f_3b350(10, (actor->angle + 0x800) & 0xfff, 0);
            break;
        case 7:
            if (actor_sees_player(actor) && actor_path_clear(actor, actor->player_angle, actor->player_distance))
                f_3b350(15, (actor->angle + 0x800) & 0xfff, 2);
            break;
        case 1:
        case 19:
            break;
        }
    }
}

void f_2f028(TASK *task)
{
    Actor *actor = (Actor *)task->state[0];
    int clear;

    if (actor->stun_timer != 0)
        actor->stun_timer--;
    if (actor->idle_timer != 0)
        actor->idle_timer--;
    if (actor->unknown_68 != 0)
        actor->unknown_68--;
    f_2cb38(actor);
    actor_measure_player(actor);
    if (actor->player_class == 4) {
        if ((g_game_flags & 0x8000) && actor->action == ACTOR_DEAD) {
            actor->health = actor_slot_health(&g_actor_slots[actor->slot_index]);
            actor->flags = 0;
            actor->action = 0;
            actor->idle_timer = 0x20;
        }
        actor_stop(actor);
        g_next_task = task->Next;
        return;
    }
    if (actor->action != ACTOR_DEAD)
        actor->player_angle = point_heading_xz(&actor->position, &g_reference_position.position) & 0xfff;
    switch (actor->action) {
    case 0:
        if (actor->idle_timer == 0 && (g_cheat_flags & 0x100))
            actor->action = 1;
        else if (actor->flags & 2)
            actor_turn_toward(actor, actor->player_angle);
        f_2bfc0(actor);
        break;
    case 1:
        if (actor->flags & 4) {
            if (f_2e5c0(actor)) {
                actor->action = 2;
                break;
            }
            actor->flags &= ~4;
        }
        if (actor->flags & 2) {
            if (f_2dc70(actor) || f_2e128(actor) || f_2e920(actor)) {
                actor->action = 2;
                break;
            }
            if (actor->kind >= 11 && actor->kind < 15 && actor->player_delta.vy < 0x601
                && (actor->player_class == 0
                    || actor_line_of_sight(actor, actor->player_angle, actor->player_distance)))
                actor->action = 3;
            break;
        }
        if (f_2e8a0(actor)) {
            actor->flags |= 2;
            break;
        }
        if (!actor_sees_player(actor))
            break;
        if (actor->player_class == 0 && actor->player_delta.vy < 0x601
            && actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
            actor->action = 3;
            break;
        }
        if (actor_line_of_sight(actor, actor->player_angle, actor->player_distance)
            || actor_path_clear(actor, actor->player_angle, actor->player_distance))
            actor->flags |= 2;
        break;
    case 2:
        actor_measure_target(actor);
        if (f_2cfd0(actor)) {
            actor_stop(actor);
            actor->action = 3;
            break;
        }
        if (!(actor->flags & 4) && actor->unknown_68 == 0) {
            if (actor->player_class == 0 && actor->player_delta.vy < 0x601) {
                actor->unknown_68 = 0x3c;
                actor_stop(actor);
                actor->action = 3;
                break;
            }
            if (actor->unknown_68 == 0 && actor->player_class != 0 && actor->player_class < 3) {
                if (actor->kind >= 11 && actor->kind < 15)
                    clear = actor_line_of_sight(actor, actor->player_angle, actor->player_distance);
                else
                    clear = actor_path_clear(actor, actor->player_angle, actor->player_distance);
                if (clear) {
                    actor->unknown_68 = 0x3c;
                    actor_stop(actor);
                    actor->action = 3;
                    break;
                }
            }
        }
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & 0xfff;
        actor_turn_toward(actor, actor->target_angle);
        f_2bfc0(actor);
        if (actor->angle != actor->desired_angle)
            break;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance)) {
            actor_set_velocity(actor, actor->angle, actor->speed);
        } else {
            actor_stop(actor);
            actor->action = 0;
            actor->idle_timer = 8;
        }
        break;
    case 3:
        if (actor->flags & 4) {
            actor->action = 1;
            break;
        }
        if (actor->player_class >= 3) {
            actor->action = 1;
            break;
        }
        if (actor->player_class == 0 && actor->player_delta.vy < 0x401
            && actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
            actor_turn_toward(actor, actor->player_angle);
            f_2bfc0(actor);
            if (actor->angle == actor->desired_angle)
                f_2eac0(task, actor);
            break;
        }
        if (actor->kind >= 11 && actor->kind < 15) {
            if (actor_line_of_sight(actor, actor->player_angle, actor->player_distance)) {
                actor_turn_toward(actor, actor->player_angle);
                f_2bfc0(actor);
                if (actor->angle == actor->desired_angle)
                    f_2eac0(task, actor);
                break;
            }
        } else if (actor->player_delta.vy < 0x401
                   && actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
            actor_turn_toward(actor, actor->player_angle);
            f_2bfc0(actor);
            if (actor->angle == actor->desired_angle)
                f_2eac0(task, actor);
            break;
        }
        actor->action = 1;
        break;
    case ACTOR_LEAP:
        actor_turn_toward(actor, actor->player_angle);
        if (actor->sprite.flags & 8) {
            if (actor->flags & 1)
                f_25120(actor);
            else
                f_24e00(actor);
            actor->flags &= ~1;
            actor->sprite.flags &= ~8;
        }
        if (!(actor->sprite.flags & 2))
            break;
        actor_stop(actor);
        f_2ed94(actor);
        if (actor->kind == 2) {
            actor->flags |= 4;
            f_2c0b4(actor);
            actor->action = 1;
            break;
        }
        if (actor->player_class < 3) {
            if (actor->player_class == 0 && actor->player_delta.vy < 0x601
                && actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                f_2c0b4(actor);
                f_2eac0(task, actor);
                break;
            }
            if (actor->kind >= 11 && actor->kind < 15) {
                if (actor->player_class == 1
                    && actor_line_of_sight(actor, actor->player_angle, actor->player_distance)) {
                    f_2c0b4(actor);
                    f_2eac0(task, actor);
                    break;
                }
                if (f_2dc70(actor)) {
                    actor->unknown_68 = 0x1e;
                    f_2c0fc(actor);
                    actor->action = 2;
                    break;
                }
                if (actor_line_of_sight(actor, actor->player_angle, actor->player_distance)) {
                    f_2c0b4(actor);
                    f_2eac0(task, actor);
                    break;
                }
            } else {
                if (actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                    f_2c0b4(actor);
                    f_2eac0(task, actor);
                    break;
                }
                if (f_2dc70(actor)) {
                    actor->unknown_68 = 0x1e;
                    f_2c0fc(actor);
                    actor->action = 2;
                    break;
                }
            }
        } else if (f_2dc70(actor)) {
            f_2c0fc(actor);
            actor->action = 2;
            break;
        }
        f_2c0b4(actor);
        actor->action = 0;
        actor->idle_timer = 4;
        break;
    case ACTOR_HURT:
        if (!(actor->sprite.flags & 2))
            break;
        actor_stop(actor);
        if (actor->kind == 3) {
            actor->flags |= 4;
            f_2c0b4(actor);
            actor->action = 1;
            break;
        }
        if (actor->player_class < 3) {
            if (actor->player_class != 0 || actor->player_delta.vy >= 0x601) {
                if (actor->kind >= 11 && actor->kind < 15) {
                    if (!actor_line_of_sight(actor, actor->player_angle, actor->player_distance)) {
                    wander:
                        if (f_2e920(actor)) {
                            actor->unknown_68 = 0x3c;
                            f_2c0fc(actor);
                            actor->action = 2;
                            break;
                        }
                        f_2c0b4(actor);
                        actor->action = 0;
                        actor->idle_timer = 8;
                        break;
                    }
                } else if (!actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                    goto wander;
                }
            }
            f_2c0b4(actor);
            f_2eac0(task, actor);
            break;
        }
        if (f_2e920(actor)) {
            actor->unknown_68 = 0x3c;
            f_2c0fc(actor);
            actor->action = 2;
            break;
        }
        f_2c0b4(actor);
        actor->action = 0;
        actor->idle_timer = 8;
        break;
    case ACTOR_DYING:
        if (actor->flags & 0x20)
            goto remove;
        if (!(actor->sprite.flags & 4))
            break;
        actor->action = ACTOR_DEAD;
        actor_stop(actor);
        if (actor->cell->type >= 45) {
            switch (actor->cell->type) {
            case 45:
            case 49:
                actor->velocity.vx = 0;
                actor->velocity.vz = -actor->speed;
                break;
            case 46:
            case 50:
                actor->velocity.vz = 0;
                actor->velocity.vx = actor->speed;
                break;
            case 47:
            case 51:
                actor->velocity.vz = 0;
                actor->velocity.vx = -actor->speed;
                break;
            case 48:
            case 52:
                actor->velocity.vx = 0;
                actor->velocity.vz = actor->speed;
                break;
            }
        }
        break;
    case ACTOR_DEAD:
        if (actor->cell->type == 6 && actor->cell->ceiling - actor->cell->value < 4) {
        remove:
            actor_unlink(actor);
            actor_free(actor);
            TaskDie(task);
            return;
        }
        if (actor->cell->type >= 45)
            break;
        actor_stop(actor);
        if (get_difficulty() == 0)
            break;
        switch (actor->kind) {
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            if (actor->cell->type == 0)
                actor->cell->type = 12;
            break;
        }
        break;
    }
    if (!(actor->sprite.flags & 6) && actor->action >= 2) {
        sprite_step(&actor->sprite);
        if (actor->sprite.flags & 0x10)
            sound_play_at_position(actor->sprite.sound, &actor->position);
    }
    actor_project(actor);
    g_next_task = task->Next;
}

Actor *f_2facc(Actor *actor, short index)
{
    Actor *other;
    ActorSlot *slot;
    unsigned int x, z;
    short angle;
    if (index == 255)
        return 0;
    x = ((unsigned int)(actor->position.vx + g_screen.offset_x)) >> 9;
    slot = &g_actor_slots[index];
    z = ((unsigned int)(actor->position.vz + g_screen.offset_z)) >> 9;
    slot->grid_x = x;
    slot->grid_z = z;
    other = f_28a30(index);
    if (!other)
        return 0;
    angle = actor->player_angle;
    other->idle_timer = 0;
    other->desired_angle = angle;
    other->angle = angle;
    other->direction = ((angle + 256) >> 9) & 7;
    other->shown_direction = other->direction;
    other->flags |= 2;
    actor_set_velocity(other, other->angle, 64);
    other->velocity.vy = 96;
    other->action = 4;
    return other;
}

int f_2fbbc(Actor *actor)
{
    Actor *other = f_2facc(actor, actor->attached_index);
    actor->attached_index = 255;
    if (other)
        actor_play_animation(other, 10);
    return -1;
}

int f_2fc04(Actor *actor)
{
    Actor *other = f_2facc(actor, actor->speed);
    actor->speed = 255;
    if (other) {
        actor_play_animation(other, 13);
        other->position.vy = other->height + 64;
        actor_set_velocity(other, other->angle, 96);
        other->velocity.vy = 120;
    }
    return -1;
}
