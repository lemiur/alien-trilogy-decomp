/*
 * Actor: one enemy (or other animated map object) of the actor pool.
 * Requires <sys/types.h>, <LIBGTE.H>, <LEVEL.H>, <TRACK.H>, <SPRITEUV.H> and <SPRITE.H>.
 *
 * Pool: actor_pool_alloc allocates 0x6400 bytes (ACTOR_COUNT records of 0xc8) into g_actor_pool;
 * actor_pool_init stores each record's index at +0x70 and threads them onto the free list
 * g_actor_free (actor_free pushes, actor_alloc pops). Live actors are linked through next/prev
 * on g_live_actors (actor_link links, actor_unlink unlinks). actor_project appends visible
 * actors to g_visible_actors.
 * Names without evidence are marked invented; unidentified members are identity-TODO.
 */
#ifndef ACTOR_H
#define ACTOR_H

#define ACTOR_COUNT 128

/* Actor.kind (and ActorSlot.kind) values, also the index of the kill tally g_player.tallies.
 * names: analysis/runtime/functions.json and globals.json (the sprite resource actor_spawn
 * gives each kind and the update task it starts) unless marked. */
enum {
    ACTOR_KIND_EGG = 1, /* egg_task */
    ACTOR_KIND_FACEHUGGER = 2, /* hugger animations; leaps on spawn */
    ACTOR_KIND_CHESTBURSTER = 3, /* BURSTER.BND */
    ACTOR_KIND_BAMBI = 4, /* name: from its sprite resource BAMBI.BND */
    ACTOR_KIND_DOG = 5, /* DOG.BND; floor form of ACTOR_KIND_DOG_CEILING */
    ACTOR_KIND_WARRIOR = 6, /* the adult alien; floor form of ACTOR_KIND_WARRIOR_CEILING */
    ACTOR_KIND_BOSS = 7, /* the Queen (QUEEN.BND, queen_task); invented name: the level boss; boss_sprite_update and g_boss_unpack_buffer serve it alone */
    ACTOR_KIND_WARRIOR_CEILING = 8, /* actor_to_ceiling form of ACTOR_KIND_WARRIOR */
    ACTOR_KIND_DOG_CEILING = 9, /* DOGCEIL.BND; actor_to_ceiling form of ACTOR_KIND_DOG */
    ACTOR_KIND_COLONIST = 10, /* cocooned colonist (COLONIST.BND, colonist_task) */
    ACTOR_KIND_GUARD = 11, /* GUARD.BND */
    ACTOR_KIND_SOLDIER = 12, /* SOLDIER.BND */
    ACTOR_KIND_SYNTHETIC = 13, /* bleeds white (actor_bleed) */
    ACTOR_KIND_HANDLER = 14, /* HANDLER.BND */
    ACTOR_KIND_INERT = 15, /* invented name: actor_spawn starts no task for it; collect_targets skips it and every later kind; no level uses it at runtime (identity-TODO) */
    ACTOR_KIND_STEAM_JET = 16, /* steam_jet_task */
    ACTOR_KIND_FLAME_JET = 17, /* flame_jet_task */
    ACTOR_KIND_STEAM_JET_VERTICAL = 18, /* steam_jet_vertical_task */
    ACTOR_KIND_FLAME_JET_VERTICAL = 19 /* flame_jet_vertical_task */
};

/* Actor.flags bits. names: invented, from their setters and testers (creature_task unless noted) */
#define ACTOR_FLAG_GRENADE 0x1 /* the next shot is lobbed (actor_lob_grenade) instead of fired (actor_fire_bullet) */
#define ACTOR_FLAG_ALERTED 0x2 /* knows of the player: turns to and chases it instead of listening and looking */
#define ACTOR_FLAG_RETREAT 0x4 /* after its own bite or a hit: picks a flee target first */
#define ACTOR_FLAG_ON_CEILING 0x8 /* invented name: set by actor_to_ceiling, cleared by actor_to_floor; moves and collides against the ceiling */
#define ACTOR_FLAG_SHUT_DOWN 0x10 /* a jet whose slot group was emptied, tallied once (jet_update) */
#define ACTOR_FLAG_NO_CORPSE 0x20 /* invented name: removed the moment it dies (creature_task) and never drawn dead (actors_draw_task) */

/* "No actor" in the byte-sized actor and slot indices (Actor.attached_index, a colonist's
 * Actor.speed). name: invented */
#define ACTOR_INDEX_NONE 0xff
/* Actor.shown_direction value that makes the next facing change resend its animation.
 * name: invented */
#define ACTOR_DIRECTION_NONE 0xff

/* Heights and ranges of actor movement and sensing (src/actor.c). names: invented */
#define ACTOR_FLOOR_CLEARANCE 0x20 /* an actor stands this far above its floor, or below its ceiling */
#define ACTOR_GRAVITY 0xc /* taken from a leaping actor's vertical speed each tick (actor_move) */
#define ACTOR_CLOSE_RANGE 0x400 /* distance class 0 below it (actor_measure_player, actor_measure_target) */
#define DISTANCE_XZ_MAX 0x4000 /* distance_xz caps at it; distance class 4 from it */
#define ACTOR_PATH_RANGE 0x2000 /* actor_path_clear gives up beyond it */
#define ACTOR_SIGHT_RANGE 0x1556 /* actor_line_of_sight gives up from it */
#define ACTOR_BOSS_HEADROOM 0xc00 /* the Queen does not walk under a lower ceiling */
#define JET_ACTIVE_RANGE 0x1e00 /* a jet idles while the player is farther (jet_update) */
#define ACTOR_ATTACK_HEIGHT 0x600 /* creature_task attacks a close player at most this far above or below */
#define ACTOR_BITE_HEIGHT 0x400 /* the same for a melee bite */

/* Actor.direction (and the slot's facing) is an octant facing, LEVEL.H's FACING_*;
 * Actor.shown_direction holds one relative to the view (actor_face_view). */

/* Actor.player_class and target_class: distance classes (actor_measure_player,
 * actor_measure_target). names: invented */
enum {
    ACTOR_RANGE_CLOSE = 0, /* below ACTOR_CLOSE_RANGE */
    ACTOR_RANGE_NEAR = 1, /* below Actor.near_range */
    ACTOR_RANGE_MID = 2, /* below Actor.far_range */
    ACTOR_RANGE_FAR = 3, /* below DISTANCE_XZ_MAX */
    ACTOR_RANGE_OUT = 4 /* the update tasks stop the actor */
};

/* Actor.action 0..3 of creature_task (also the Queen's, queen_task). names: invented */
enum {
    CREATURE_IDLE = 0, /* waits for idle_timer and CHEAT_FLAG_ACTORS_WAKE */
    CREATURE_ALERT = 1, /* listens and looks for the player, or picks a target */
    CREATURE_MOVE = 2, /* walks to Actor.target */
    CREATURE_ENGAGE = 3 /* turns to the player and attacks (actor_start_attack) */
};
/* Actor.action 0..3 of egg_task. names: invented */
enum {
    EGG_CLOSED = 0,
    EGG_WAKING = 1,
    EGG_OPENING = 2,
    EGG_OPEN = 3 /* releases its face-hugger (egg_release_hugger) */
};
/* Actor.action 0..3 of colonist_task. names: invented */
enum {
    COLONIST_IDLE = 0,
    COLONIST_REACHING = 1,
    COLONIST_BURSTING = 2, /* COLONIST_ANIM_BURST plays, then colonist_release_burster */
    COLONIST_EMPTY = 3 /* the chest-burster has left; damage no longer reaches it */
};
/* A jet's Actor.action besides ACTOR_HURT, ACTOR_DYING and ACTOR_DEAD (jet_update). name: invented */
#define JET_IDLE 0
/* The view-relative octant (colonist_task) from which a colonist reaches for the player or
 * bursts. name: invented */
#define COLONIST_REACH_OCTANT 4

/* actor_spawn tuning. names: invented */
#define ACTOR_WAKE_DELAY 32 /* idle_timer at spawn, and on a Xenomania revival (creature_task) */
#define ACTOR_SPAWN_STUN 64 /* stun_timer at spawn */
#define ACTOR_HURT_STUN 16 /* stun_timer while hurt (actor_hurt) */
#define QUEEN_HURT_STUN 64 /* stun_timer of a hit in attack phase 2 (actor_hurt) */
#define ACTOR_VIEW_CONE_MIN 0x100 /* the slot's view_cone is raised to it */
#define ACTOR_VIEW_CONE_MAX 0x800 /* a wider view_cone becomes ACTOR_VIEW_CONE_WIDE */
#define ACTOR_VIEW_CONE_WIDE 0x700
#define ACTOR_NEAR_RANGE_MIN 0x600 /* the slot's near_range is raised to it */
#define EGG_FAR_RANGE 0xc00
#define EGG_NEAR_RANGE 0x800
#define COLONIST_HANG_HEIGHT 0x40 /* a colonist's cocoon, and its chest-burster, start this far above the floor */
#define COLONIST_START_PHASE_MASK 5 /* random() & it: colonist_task's state[1] at spawn */
#define JET_HEIGHT_STEP_SHIFT 5 /* a jet's height step in units of 1 << it */
#define JET_CEILING_STEP_BIAS 10 /* added to a negative height step, measured from the ceiling */

/* Actor.scale (its size: scale.vy is the headroom actor_path_clear needs) in units of
 * ACTOR_SCALE_UNIT, and each kind's Sprite.scale (actor_spawn). names: invented */
#define ACTOR_SCALE_UNIT 0x100
#define EGG_SPRITE_SCALE 0xa00
#define HUGGER_SPRITE_SCALE 0x600
#define BURSTER_SPRITE_SCALE 0x600
#define BAMBI_SPRITE_SCALE 0x800
#define DOG_SPRITE_SCALE 0xa00
#define WARRIOR_SPRITE_SCALE 0xa00
#define QUEEN_SPRITE_SCALE 0xb00
#define COLONIST_SPRITE_SCALE 2100
#define GUARD_SPRITE_SCALE 0x800
#define SOLDIER_SPRITE_SCALE 0x880
#define SYNTH_SPRITE_SCALE 0x900
#define HANDLER_SPRITE_SCALE 0x900

/* The ranges and speed actor_slots_init gives a slot whose kind g_actor_kind_map changed:
 * base + (random() & mask). names: invented */
#define REMAPPED_FAR_RANGE 1000
#define REMAPPED_NEAR_RANGE 0x300
#define REMAPPED_HUGGER_FAR_RANGE 1200
#define REMAPPED_HUGGER_FAR_RANGE_MASK 0xff
#define REMAPPED_WARRIOR_SPEED 100
#define REMAPPED_SPEED 95
#define REMAPPED_SPEED_MASK 0xf

/* Leaps: actor_set_velocity speed and upward velocity.vy. names: invented */
#define ACTOR_SPAWN_LEAP_SPEED 0x40 /* actor_spawn_from_object, actor_spawn_leaping */
#define ACTOR_SPAWN_LEAP_LIFT 0x60
#define HUGGER_LEAP_SPEED 0x60 /* actor_start_attack */
#define BURSTER_LEAP_SPEED 0x40
#define ACTOR_LEAP_LIFT 0x80 /* actor_start_attack, when not already rising */
#define BURSTER_EMERGE_SPEED 0x60 /* colonist_release_burster */
#define BURSTER_EMERGE_LIFT 120

/* actor_melee_hit: damage to the player in distance class ACTOR_RANGE_CLOSE or NEAR, and the
 * smaller hit in ACTOR_RANGE_MID. names: invented */
#define HUGGER_BITE_DAMAGE 5 /* also the chest-burster's */
#define HUGGER_BITE_DAMAGE_FAR 1
#define DOG_BITE_DAMAGE 10 /* also bambi's */
#define DOG_BITE_DAMAGE_FAR 5
#define WARRIOR_CLAW_DAMAGE 15
#define WARRIOR_CLAW_DAMAGE_FAR 10
#define QUEEN_STRIKE_DAMAGE 25
#define QUEEN_STRIKE_DAMAGE_FAR 15

/* creature_task timers: Actor.engage_cooldown holds off the next charge; idle_timer pauses.
 * names: invented */
#define ACTOR_CHARGE_COOLDOWN 60
#define ACTOR_RETARGET_COOLDOWN 30
#define ACTOR_PAUSE 8
#define ACTOR_PAUSE_SHORT 4

/* queen_task's TASK.state[1]: the Queen's waking phase (actor_hurt, actor_draw read it).
 * names: invented */
enum {
    QUEEN_DORMANT = 0,
    QUEEN_WAKING = 1, /* QUEEN_ANIM_WAKE plays */
    QUEEN_RISING = 2, /* QUEEN_ANIM_RISE plays */
    QUEEN_AWAKE = 3 /* fights; drawn with the shadow when near */
};
/* egg_task's TASK.state[1]: how far the egg has opened (actor_hurt reads it). names: invented */
enum {
    EGG_SHELL_CLOSED = 0,
    EGG_SHELL_OPENING = 1,
    EGG_SHELL_OPEN = 2,
    EGG_SHELL_HATCHED = 3
};

/* Wait times (idle_timer) and the hugger's cling time. Random waits are base + (random() & mask).
 * names: invented */
#define QUEEN_DORMANT_DELAY 0x80 /* back to sleep when the player stays away */
#define QUEEN_RECOVER_DELAY 0x40 /* hit before it has risen */
#define EGG_CLOSE_DELAY 0x40
#define EGG_CLOSE_DELAY_MASK 0x7f
#define COLONIST_BURST_DELAY 40
#define COLONIST_REACH_DELAY 120
#define COLONIST_DELAY_MASK 0x7f
#define HUGGER_CLING_TIME 0x40 /* ticks hugger_cling_task damages the player before the drop */

/* Movement probes. actor_path_clear walks two lines ACTOR_RADIUS either side of the heading in
 * ACTOR_PATH_STEP steps (distance >> ACTOR_PATH_STEP_SHIFT of them) and fails on a climb or
 * drop beyond the limits (larger on the ceiling); actor_clip_x/_z keep ACTOR_RADIUS from a
 * cell's walls. actor_line_of_sight starts at ACTOR_EYE_HEIGHT above the actor, aims
 * ACTOR_SIGHT_AIM_DROP lower and passes over the level objects it may see across.
 * names: invented */
#define ACTOR_RADIUS 200
#define ACTOR_PATH_STEP 0x100
#define ACTOR_PATH_STEP_SHIFT 8
#define ACTOR_MAX_CLIMB 0x100
#define ACTOR_MAX_DROP 0x400
#define ACTOR_CEILING_MAX_CLIMB 0x200
#define ACTOR_CEILING_MAX_DROP 0x800
#define ACTOR_EYE_HEIGHT 0x240
#define ACTOR_SIGHT_AIM_DROP 0x400
/* A level object's height over its floor: sight passes above it (actor_line_of_sight), a shot
 * below it hits it (particle_hit_floor_or_ceiling). names: invented */
#define OBJECT_HIT_HEIGHT 0x200
#define OBJECT_TALL_HIT_HEIGHT 0x400 /* LEVEL_OBJECT_TARGET_ALT */
/* A 12-bit angle difference sign-extends through << ANGLE_SIGN_SHIFT >> ANGLE_SIGN_SHIFT
 * (actor_turn_toward, actor_sees_player). name: invented */
#define ANGLE_SIGN_SHIFT 20

/* Target picking (Actor.target): ACTOR_TARGET_TRIES random headings, at a distance base +
 * (random() & ACTOR_TARGET_SPREAD_MASK). names: invented */
#define ACTOR_TARGET_TRIES 4
#define ACTOR_TARGET_SPREAD_MASK 0x1ff
#define TARGET_NEAR_PLAYER_DISTANCE 0x400 /* actor_pick_target_near_player */
#define TARGET_STEP_DISTANCE 0x200 /* actor_pick_chase_target, actor_pick_flee_target */
#define TARGET_BEHIND_DISTANCE 0x400 /* actor_pick_target_behind */

/* actor_draw_shadowed scales its clipped polygons in 1 / (1 << ACTOR_CLIP_FRACTION_SHIFT)
 * steps. name: invented */
#define ACTOR_CLIP_FRACTION_SHIFT 4

/* The face-hugger overlay draws its frame offsets in units of HUGGER_OVERLAY_FRAME_SCALE
 * pixels, shaken by g_frame_count & HUGGER_OVERLAY_SHAKE_MASK, rising HUGGER_OVERLAY_RISE_STEP
 * a tick; the dropped hugger lands within HUGGER_DROP_SCATTER of the player, at most
 * HUGGER_DROP_MAX_STEP above or below it (hugger_cling_task). names: invented */
#define HUGGER_OVERLAY_FRAME_SCALE 4
#define HUGGER_OVERLAY_SHAKE_MASK 0xf
#define HUGGER_OVERLAY_RISE_STEP 8
#define HUGGER_DROP_SCATTER 0x200
#define HUGGER_DROP_SCATTER_MASK 0x3ff
#define HUGGER_DROP_MAX_STEP 0x100

/* The Queen picks one of four strikes (random() & QUEEN_STRIKE_MASK); a handler lobs a
 * grenade unless random() & HANDLER_ATTACK_MASK is 0..2 (actor_start_attack). names: invented */
#define QUEEN_STRIKE_MASK 3
#define HANDLER_ATTACK_MASK 7

/* jet_update arguments per jet task: particle period (ticks) and speed. names: invented */
#define STEAM_JET_PERIOD 3
#define STEAM_JET_SPEED 60
#define JET_PERIOD 2 /* the other three jets */
#define FLAME_JET_SPEED 120
#define FLAME_JET_VERTICAL_SPEED 90
#define STEAM_JET_VERTICAL_SPEED 65

/* actor_slot_health: ActorSlot.strength (plus a difficulty bonus) shifted into hit points,
 * by more for the Queen; a non-zero result is at least ACTOR_MIN_HEALTH. names: invented */
#define ACTOR_HEALTH_SHIFT 4
#define ACTOR_BOSS_HEALTH_SHIFT 6
#define ACTOR_MIN_HEALTH 16

/* A signed byte kept in a u_char (a jet's height step in Actor.attached_index, the amount of
 * actor_group_add): values from BYTE_SIGN_BIT on are negative and widen with
 * BYTE_SIGN_EXTENSION. names: invented */
#define BYTE_MASK 0xff
#define BYTE_SIGN_BIT 0x80
#define BYTE_SIGN_EXTENSION 0xff00

/* actor_project keeps an actor between ACTOR_NEAR_DEPTH and DRAW_FAR_DEPTH whose screen
 * position is within the clip box (the y bounds only from ACTOR_CLIP_Y_DEPTH on).
 * actor_draw shades actors nearer than the shadow depths. names: invented */
#define ACTOR_NEAR_DEPTH 0x80
#define ACTOR_CLIP_MIN_X (-0x7f)
#define ACTOR_CLIP_MAX_X 0x1bf
#define ACTOR_CLIP_MIN_Y (-0x3f)
#define ACTOR_CLIP_MAX_Y 0x16f
#define ACTOR_CLIP_Y_DEPTH 0x400
#define ACTOR_SHADOW_DEPTH 0x1000
#define ACTOR_BOSS_SHADOW_DEPTH 0x1800

/* The face-hugger overlay (hugger_overlay_start, hugger_overlay_step): centred, rising from
 * below the screen to its top row, dropping away when the hugger lets go. names: invented */
#define HUGGER_OVERLAY_X 0xa0
#define HUGGER_OVERLAY_HIDDEN_Y 0x154
#define HUGGER_OVERLAY_TOP_Y 0x78
#define HUGGER_OVERLAY_DROP_STEP 0x10

/* Actor.action values shared by every kind; 0..3 are per-kind behaviour states. */
enum {
    ACTOR_LEAP = 4, /* invented name: set with an upward velocity by actor_start_attack, actor_spawn_from_object and actor_spawn_leaping */
    ACTOR_HURT = 5, /* invented name: set by actor_hurt while stun_timer runs */
    ACTOR_DYING = 6, /* invented name: set by actor_die */
    ACTOR_DEAD = 7 /* invented name: the death animation has finished (egg_task) */
};

typedef struct Actor {
    SVECTOR position; /* RotTransPers input (actor_project) */
    SVECTOR scale; /* invented name: 0x100 per axis when actor_to_ceiling changes kind */
    VECTOR player_delta; /* invented name: |player - position| per axis (actor_measure_player) */
    VECTOR target_delta; /* invented name: |target - position| per axis (actor_measure_target) */
    SVECTOR velocity; /* rotated by heading (actor_set_velocity) */
    SVECTOR target; /* invented name: waypoint, set by actor_pick_target_near_player and the layout walk actor_target_toward_player */
    short view_cone; /* invented name: the player is seen within +-view_cone of angle (actor_sees_player); ActorSlot.view_cone */
    short angle; /* heading, stepped toward desired_angle by turn_rate (actor_turn_toward) */
    short desired_angle; /* invented name */
    short turn_rate; /* invented name */
    short far_range; /* invented name: distance class 2/3 boundary (actor_measure_player) */
    short near_range; /* invented name: distance class 1/2 boundary (actor_measure_player) */
    short shot_speed; /* invented name: added to the speed of spat projectiles (actor_fire_bullet, actor_lob_grenade), 0 at spawn (actor_spawn) */
    short stun_timer; /* invented name: counts down; blocks damage while nonzero (egg_task, actor_damage) */
    short reach; /* invented name: actor_hears_player attacks within reach / 2, or reach with noise; ActorSlot.reach */
    short idle_timer; /* invented name: counts down; idle action waits for it (egg_task) */
    short player_angle; /* invented name: heading from position to the player (egg_task) */
    short target_angle; /* invented name: heading from position to target (actor_pick_target_near_player) */
    short health; /* invented name: reduced by damage, killed at 0 (actor_damage) */
    short height; /* invented name: spawn height offset (colonist_release_burster) */
    short ceiling; /* invented name: ceiling_height ceiling height under the actor (actor_move) */
    short speed; /* invented name: ActorSlot.speed at spawn (actor_spawn); actor_hurt knocks back at speed >> 2; colonist_release_burster reads it as a slot index (identity-TODO) */
    u_int player_distance; /* invented name: distance_xz distance to the player (actor_measure_player) */
    u_int target_distance; /* invented name: distance_xz distance to target (actor_measure_target) */
    int engage_cooldown; /* invented name: frames before creature_task may engage again; set to ACTOR_CHARGE_COOLDOWN or ACTOR_RETARGET_COOLDOWN, counted down each frame, cleared at spawn (actor_spawn) */
    u_char flags;
    u_char direction; /* invented name: octant of angle */
    u_char shown_direction; /* invented name: octant last sent to the animation (actor_face_view), 0xff forces a resend */
    u_char action; /* invented name: behaviour state */
    u_char index; /* pool index (actor_pool_init) */
    u_char last_trigger; /* invented name: trigger number (MapCell.trigger) of the cell the actor stood in last (level_triggers.c), 0 for none; cleared at spawn (actor_spawn) */
    u_char attached_index; /* invented name: actor slot spawned by egg_release_hugger, 0xff for none */
    u_char player_class; /* invented name: distance class 0..4 to the player (actor_measure_player) */
    u_char kind;
    u_char target_class; /* invented name: distance class 0..4 to target (actor_measure_target) */
    u_char layout_index; /* invented name: layout record the actor walks from (actor_target_toward_player) */
    u_char slot_index; /* invented name: ActorSlot the actor was spawned from (actor_spawn) */
    MapCell *cell; /* invented name: its mask is tested against g_visible_cell_mask for visibility */
    TrackEntry *animations; /* invented name: animation table indexed by actor_play_animation */
    Sprite sprite;
    struct TASK *task; /* invented name: the actor's update task; its state[1] is the attack phase */
    struct Actor *next;
    struct Actor *prev;
} Actor;

/* name: inferred from use: actor_pool_alloc allocates the ACTOR_COUNT records into it */
extern Actor *g_actor_pool;
/* name: inferred from use: actors are linked through next/prev from it */
extern Actor *g_live_actors;
/* name: inferred from use: actor_project appends each visible actor */
extern Actor *g_visible_actors[ACTOR_COUNT];

/* The smart gun's target list (src/particle.c: collect_targets, smart_gun_fire). */
typedef struct ActorTarget {
    Actor *actor;
    short distance;
    short angle;
} ActorTarget;
extern ActorTarget g_targets[]; /* name: inferred from collect_targets */
extern short g_target_count; /* name: inferred from collect_targets */

extern void actor_free(Actor *actor);
extern void actor_pool_init(void);
extern void actor_pool_alloc(void);
extern int actor_passed_target(Actor *actor);
extern Actor *actor_alloc(void);
extern void actor_link(Actor *actor);
extern void actor_unlink(Actor *actor);

/* Actor behaviour (src/actor.c). */
extern short actor_slot_health(ActorSlot *slot);
extern Actor *actor_spawn(int slot);
extern void actor_play_animation(Actor *actor, short index);
extern void actor_to_ceiling(Actor *actor, short index);
extern void actor_to_floor(Actor *actor, short index);
extern void actor_project(Actor *actor);
extern void actor_die(Actor *actor);
extern void actor_move(Actor *actor);
extern void actor_measure_player(Actor *actor);
extern void actor_measure_target(Actor *actor);
extern int actor_path_clear(Actor *actor, short angle, u_int distance);
extern int actor_line_of_sight(Actor *actor, short angle, u_int distance);
extern void actor_turn_toward(Actor *actor, int angle);
extern void actor_stop(Actor *actor);
extern void actor_set_velocity(Actor *actor, short angle, short speed);
extern int actor_sees_player(Actor *actor);

/* src/actor.c */
extern void actor_group_add(int amount, int index);
extern void actors_init(void);
extern void actor_damage(Actor *actor, short amount, short set_flag);
extern void actors_draw_task(struct TASK *task);

#endif
