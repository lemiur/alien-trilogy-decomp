/*
 * actor_t: one enemy (or other animated map object) of the actor pool.
 * Requires <sys/types.h>, <LIBGTE.H>, <LEVEL.H>, <TRACK.H>, <SPRITEUV.H> and <SPRITE.H>.
 *
 * Pool: Actor_PoolAlloc allocates 0x6400 bytes (ACTOR_COUNT records of 0xc8) into g_actorPool;
 * Actor_InitPool stores each record's index at +0x70 and threads them onto the free list
 * g_actorFree (Actor_Free pushes, Actor_Alloc pops). Live actors are linked through next/prev
 * on g_liveActors (Actor_Link links, Actor_Unlink unlinks). Actor_Project appends visible
 * actors to g_visibleActors.
 * Names without evidence are marked invented; unidentified members are identity-TODO.
 */
#ifndef ACTOR_H
#define ACTOR_H

#define ACTOR_COUNT 128

/* actor_t.kind (and actorSlot_t.kind) values, also the index of the kill tally g_player.tallies.
 * names: analysis/runtime/functions.json and globals.json (the sprite resource Actor_Spawn
 * gives each kind and the update task it starts) unless marked. */
enum {
    ACTOR_KIND_EGG = 1,          /* Actor_EggTask */
    ACTOR_KIND_FACEHUGGER = 2,   /* hugger animations; leaps on spawn */
    ACTOR_KIND_CHESTBURSTER = 3, /* BURSTER.BND */
    ACTOR_KIND_BAMBI = 4,        /* name: from its sprite resource BAMBI.BND */
    ACTOR_KIND_DOG = 5,          /* DOG.BND; floor form of ACTOR_KIND_DOG_CEILING */
    ACTOR_KIND_WARRIOR = 6,      /* the adult alien; floor form of ACTOR_KIND_WARRIOR_CEILING */
    ACTOR_KIND_BOSS = 7, /* the Queen (QUEEN.BND, Actor_QueenTask); invented name: the level boss; Actor_UpdateBossSprite and g_bossUnpackBuffer serve it alone */
    ACTOR_KIND_WARRIOR_CEILING = 8, /* Actor_ToCeiling form of ACTOR_KIND_WARRIOR */
    ACTOR_KIND_DOG_CEILING = 9,     /* DOGCEIL.BND; Actor_ToCeiling form of ACTOR_KIND_DOG */
    ACTOR_KIND_COLONIST = 10,       /* cocooned colonist (COLONIST.BND, Actor_ColonistTask) */
    ACTOR_KIND_GUARD = 11,          /* GUARD.BND */
    ACTOR_KIND_SOLDIER = 12,        /* SOLDIER.BND */
    ACTOR_KIND_SYNTHETIC = 13,      /* bleeds white (Particle_ActorBleed) */
    ACTOR_KIND_HANDLER = 14,        /* HANDLER.BND */
    ACTOR_KIND_INERT = 15, /* invented name: Actor_Spawn starts no task for it; Particle_CollectTargets skips it and every later kind; no level uses it at runtime (identity-TODO) */
    ACTOR_KIND_STEAM_JET = 16,          /* Actor_SteamJetTask */
    ACTOR_KIND_FLAME_JET = 17,          /* Actor_FlameJetTask */
    ACTOR_KIND_STEAM_JET_VERTICAL = 18, /* Actor_SteamJetVerticalTask */
    ACTOR_KIND_FLAME_JET_VERTICAL = 19  /* Actor_FlameJetVerticalTask */
};

/* actor_t.flags bits. names: invented, from their setters and testers (Actor_CreatureTask unless noted) */
#define ACTOR_FLAG_GRENADE \
    0x1 /* the next shot is lobbed (Particle_ActorLobGrenade) instead of fired (Particle_ActorFireBullet) */
#define ACTOR_FLAG_ALERTED 0x2 /* knows of the player: turns to and chases it instead of listening and looking */
#define ACTOR_FLAG_RETREAT 0x4 /* after its own bite or a hit: picks a flee target first */
#define ACTOR_FLAG_ON_CEILING \
    0x8 /* invented name: set by Actor_ToCeiling, cleared by Actor_ToFloor; moves and collides against the ceiling */
#define ACTOR_FLAG_SHUT_DOWN 0x10 /* a jet whose slot group was emptied, tallied once (Actor_UpdateJet) */
#define ACTOR_FLAG_NO_CORPSE \
    0x20 /* invented name: removed the moment it dies (Actor_CreatureTask) and never drawn dead (Actor_DrawTask) */

/* "No actor" in the byte-sized actor and slot indices (actor_t.attachedIndex, a colonist's
 * actor_t.speed). name: invented */
#define ACTOR_INDEX_NONE 0xff
/* actor_t.shownDirection value that makes the next facing change resend its animation.
 * name: invented */
#define ACTOR_DIRECTION_NONE 0xff

/* Heights and ranges of actor movement and sensing (src/actor.c). names: invented */
#define ACTOR_FLOOR_CLEARANCE 0x20 /* an actor stands this far above its floor, or below its ceiling */
#define ACTOR_GRAVITY 0xc          /* taken from a leaping actor's vertical speed each tick (Actor_Move) */
#define ACTOR_CLOSE_RANGE 0x400    /* distance class 0 below it (Actor_MeasurePlayer, Actor_MeasureTarget) */
#define DISTANCE_XZ_MAX 0x4000     /* Actor_DistanceXZ caps at it; distance class 4 from it */
#define ACTOR_PATH_RANGE 0x2000    /* Actor_PathClear gives up beyond it */
#define ACTOR_SIGHT_RANGE 0x1556   /* Actor_LineOfSight gives up from it */
#define ACTOR_BOSS_HEADROOM 0xc00  /* the Queen does not walk under a lower ceiling */
#define JET_ACTIVE_RANGE 0x1e00    /* a jet idles while the player is farther (Actor_UpdateJet) */
#define ACTOR_ATTACK_HEIGHT 0x600  /* Actor_CreatureTask attacks a close player at most this far above or below */
#define ACTOR_BITE_HEIGHT 0x400    /* the same for a melee bite */

/* actor_t.direction (and the slot's facing) is an octant facing, LEVEL.H's FACING_*;
 * actor_t.shownDirection holds one relative to the view (Actor_FaceView). */

/* actor_t.playerClass and targetClass: distance classes (Actor_MeasurePlayer,
 * Actor_MeasureTarget). names: invented */
enum {
    ACTOR_RANGE_CLOSE = 0, /* below ACTOR_CLOSE_RANGE */
    ACTOR_RANGE_NEAR = 1,  /* below actor_t.nearRange */
    ACTOR_RANGE_MID = 2,   /* below actor_t.farRange */
    ACTOR_RANGE_FAR = 3,   /* below DISTANCE_XZ_MAX */
    ACTOR_RANGE_OUT = 4    /* the update tasks stop the actor */
};

/* actor_t.action 0..3 of Actor_CreatureTask (also the Queen's, Actor_QueenTask). names: invented */
enum {
    CREATURE_IDLE = 0,  /* waits for idleTimer and CHEAT_FLAG_ACTORS_WAKE */
    CREATURE_ALERT = 1, /* listens and looks for the player, or picks a target */
    CREATURE_MOVE = 2,  /* walks to actor_t.target */
    CREATURE_ENGAGE = 3 /* turns to the player and attacks (Actor_StartAttack) */
};
/* actor_t.action 0..3 of Actor_EggTask. names: invented */
enum {
    EGG_CLOSED = 0,
    EGG_WAKING = 1,
    EGG_OPENING = 2,
    EGG_OPEN = 3 /* releases its face-hugger (Actor_EggReleaseHugger) */
};
/* actor_t.action 0..3 of Actor_ColonistTask. names: invented */
enum {
    COLONIST_IDLE = 0,
    COLONIST_REACHING = 1,
    COLONIST_BURSTING = 2, /* COLONIST_ANIM_BURST plays, then Actor_ColonistReleaseBurster */
    COLONIST_EMPTY = 3     /* the chest-burster has left; damage no longer reaches it */
};
/* A jet's actor_t.action besides ACTOR_HURT, ACTOR_DYING and ACTOR_DEAD (Actor_UpdateJet). name: invented */
#define JET_IDLE 0
/* The view-relative octant (Actor_ColonistTask) from which a colonist reaches for the player or
 * bursts. name: invented */
#define COLONIST_REACH_OCTANT 4

/* Actor_Spawn tuning. names: invented */
#define ACTOR_WAKE_DELAY 32       /* idleTimer at spawn, and on a Xenomania revival (Actor_CreatureTask) */
#define ACTOR_SPAWN_STUN 64       /* stunTimer at spawn */
#define ACTOR_HURT_STUN 16        /* stunTimer while hurt (Actor_Hurt) */
#define QUEEN_HURT_STUN 64        /* stunTimer of a hit in attack phase 2 (Actor_Hurt) */
#define ACTOR_VIEW_CONE_MIN 0x100 /* the slot's viewCone is raised to it */
#define ACTOR_VIEW_CONE_MAX 0x800 /* a wider viewCone becomes ACTOR_VIEW_CONE_WIDE */
#define ACTOR_VIEW_CONE_WIDE 0x700
#define ACTOR_NEAR_RANGE_MIN 0x600 /* the slot's nearRange is raised to it */
#define EGG_FAR_RANGE 0xc00
#define EGG_NEAR_RANGE 0x800
#define COLONIST_HANG_HEIGHT 0x40   /* a colonist's cocoon, and its chest-burster, start this far above the floor */
#define COLONIST_START_PHASE_MASK 5 /* Sys_Random() & it: Actor_ColonistTask's state[1] at spawn */
#define JET_HEIGHT_STEP_SHIFT 5     /* a jet's height step in units of 1 << it */
#define JET_CEILING_STEP_BIAS 10    /* added to a negative height step, measured from the ceiling */

/* actor_t.scale (its size: scale.vy is the headroom Actor_PathClear needs) in units of
 * ACTOR_SCALE_UNIT, and each kind's sprite_t.scale (Actor_Spawn). names: invented */
#define ACTOR_SCALE_UNIT 0x100
#define ACTOR_SCALE_TALL ( ACTOR_SCALE_UNIT * 3 )      /* actor_t.scale.vy of the tall kinds and the jets */
#define QUEEN_SCALE_GIRTH ( ACTOR_SCALE_UNIT * 3 / 2 ) /* the Queen's scale.vx and scale.vz */
#define QUEEN_SCALE_HEIGHT ( ACTOR_SCALE_UNIT * 6 )
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

/* The ranges and speed Actor_InitSlots gives a slot whose kind g_actorKindMap changed:
 * base + (Sys_Random() & mask). names: invented */
#define REMAPPED_FAR_RANGE 1000
#define REMAPPED_NEAR_RANGE 0x300
#define REMAPPED_HUGGER_FAR_RANGE 1200
#define REMAPPED_HUGGER_FAR_RANGE_MASK 0xff
#define REMAPPED_WARRIOR_SPEED 100
#define REMAPPED_SPEED 95
#define REMAPPED_SPEED_MASK 0xf

/* Leaps: Actor_SetVelocity speed and upward velocity.vy. names: invented */
#define ACTOR_SPAWN_LEAP_SPEED 0x40 /* Actor_SpawnFromObject, Actor_SpawnLeaping */
#define ACTOR_SPAWN_LEAP_LIFT 0x60
#define HUGGER_LEAP_SPEED 0x60 /* Actor_StartAttack */
#define BURSTER_LEAP_SPEED 0x40
#define ACTOR_LEAP_LIFT 0x80      /* Actor_StartAttack, when not already rising */
#define BURSTER_EMERGE_SPEED 0x60 /* Actor_ColonistReleaseBurster */
#define BURSTER_EMERGE_LIFT 120

/* Actor_MeleeHit: damage to the player in distance class ACTOR_RANGE_CLOSE or NEAR, and the
 * smaller hit in ACTOR_RANGE_MID. names: invented */
#define HUGGER_BITE_DAMAGE 5 /* also the chest-burster's */
#define HUGGER_BITE_DAMAGE_FAR 1
#define DOG_BITE_DAMAGE 10 /* also bambi's */
#define DOG_BITE_DAMAGE_FAR 5
#define WARRIOR_CLAW_DAMAGE 15
#define WARRIOR_CLAW_DAMAGE_FAR 10
#define QUEEN_STRIKE_DAMAGE 25
#define QUEEN_STRIKE_DAMAGE_FAR 15

/* Actor_CreatureTask timers: actor_t.engageCooldown holds off the next charge; idleTimer pauses.
 * names: invented */
#define ACTOR_CHARGE_COOLDOWN 60
#define ACTOR_RETARGET_COOLDOWN 30
#define ACTOR_PAUSE 8
#define ACTOR_PAUSE_SHORT 4

/* Actor_QueenTask's TASK.state[1]: the Queen's waking phase (Actor_Hurt, Actor_Draw read it).
 * names: invented */
enum {
    QUEEN_DORMANT = 0,
    QUEEN_WAKING = 1, /* QUEEN_ANIM_WAKE plays */
    QUEEN_RISING = 2, /* QUEEN_ANIM_RISE plays */
    QUEEN_AWAKE = 3   /* fights; drawn with the shadow when near */
};
/* Actor_EggTask's TASK.state[1]: how far the egg has opened (Actor_Hurt reads it). names: invented */
enum {
    EGG_SHELL_CLOSED = 0,
    EGG_SHELL_OPENING = 1,
    EGG_SHELL_OPEN = 2,
    EGG_SHELL_HATCHED = 3
};

/* Wait times (idleTimer) and the hugger's cling time. Random waits are base + (Sys_Random() & mask).
 * names: invented */
#define QUEEN_DORMANT_DELAY 0x80 /* back to sleep when the player stays away */
#define QUEEN_RECOVER_DELAY 0x40 /* hit before it has risen */
#define EGG_CLOSE_DELAY 0x40
#define EGG_CLOSE_DELAY_MASK 0x7f
#define COLONIST_BURST_DELAY 40
#define COLONIST_REACH_DELAY 120
#define COLONIST_DELAY_MASK 0x7f
#define HUGGER_CLING_TIME 0x40 /* ticks Actor_HuggerClingTask damages the player before the drop */

/* Movement probes. Actor_PathClear walks two lines ACTOR_RADIUS either side of the heading in
 * ACTOR_PATH_STEP steps (distance >> ACTOR_PATH_STEP_SHIFT of them) and fails on a climb or
 * drop beyond the limits (larger on the ceiling); Actor_ClipX/_z keep ACTOR_RADIUS from a
 * cell's walls. Actor_LineOfSight starts at ACTOR_EYE_HEIGHT above the actor, aims
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
/* A level object's height over its floor: sight passes above it (Actor_LineOfSight), a shot
 * below it hits it (Particle_HitFloorOrCeiling). names: invented */
#define OBJECT_HIT_HEIGHT 0x200
#define OBJECT_TALL_HIT_HEIGHT 0x400 /* LEVEL_OBJECT_TARGET_ALT */
/* A 12-bit angle difference sign-extends through << ANGLE_SIGN_SHIFT >> ANGLE_SIGN_SHIFT
 * (Actor_TurnToward, Actor_SeesPlayer). name: invented */
#define ANGLE_SIGN_SHIFT 20

/* Target picking (actor_t.target): ACTOR_TARGET_TRIES random headings, at a distance base +
 * (Sys_Random() & ACTOR_TARGET_SPREAD_MASK). names: invented */
#define ACTOR_TARGET_TRIES 4
#define ACTOR_TARGET_SPREAD_MASK 0x1ff
#define TARGET_NEAR_PLAYER_DISTANCE 0x400 /* Actor_PickTargetNearPlayer */
#define TARGET_STEP_DISTANCE 0x200        /* Actor_PickChaseTarget, Actor_PickFleeTarget */
#define TARGET_BEHIND_DISTANCE 0x400      /* Actor_PickTargetBehind */

/* Actor_DrawShadowed scales its clipped polygons in 1 / (1 << ACTOR_CLIP_FRACTION_SHIFT)
 * steps. name: invented */
#define ACTOR_CLIP_FRACTION_SHIFT 4

/* The face-hugger overlay draws its frame offsets in units of HUGGER_OVERLAY_FRAME_SCALE
 * pixels, shaken by g_frameCount & HUGGER_OVERLAY_SHAKE_MASK, rising HUGGER_OVERLAY_RISE_STEP
 * a tick; the dropped hugger lands within HUGGER_DROP_SCATTER of the player, at most
 * HUGGER_DROP_MAX_STEP above or below it (Actor_HuggerClingTask). names: invented */
#define HUGGER_OVERLAY_FRAME_SCALE 4
#define HUGGER_OVERLAY_SHAKE_MASK 0xf
#define HUGGER_OVERLAY_RISE_STEP 8
#define HUGGER_DROP_SCATTER 0x200
#define HUGGER_DROP_SCATTER_MASK 0x3ff
#define HUGGER_DROP_MAX_STEP 0x100

/* The Queen picks one of four strikes (Sys_Random() & QUEEN_STRIKE_MASK); a handler lobs a
 * grenade unless Sys_Random() & HANDLER_ATTACK_MASK is 0..2 (Actor_StartAttack). names: invented */
#define QUEEN_STRIKE_MASK 3
enum {
    QUEEN_STRIKE_BITE = 0,
    QUEEN_STRIKE_STAB = 1,          /* QUEEN_ANIM_STRIKE */
    QUEEN_STRIKE_SWIPE = 2,         /* QUEEN_ANIM_SWIPE */
    QUEEN_STRIKE_SWIPE_MIRRORED = 3 /* QUEEN_ANIM_SWIPE with SPRITE_FLAG_MIRRORED */
};
/* The TASK.state[] slot the actor update tasks use as a tick counter: Actor_UpdateJet counts it and
 * Actor_Spawn clears it. name: invented */
#define ACTOR_STATE_COUNTER 3
#define HANDLER_ATTACK_MASK 7

/* Actor_UpdateJet arguments per jet task: particle period (ticks) and speed. names: invented */
#define STEAM_JET_PERIOD 3
#define STEAM_JET_SPEED 60
#define JET_PERIOD 2 /* the other three jets */
#define FLAME_JET_SPEED 120
#define FLAME_JET_VERTICAL_SPEED 90
#define STEAM_JET_VERTICAL_SPEED 65

/* Actor_SlotHealth: actorSlot_t.strength (plus a difficulty bonus) shifted into hit points,
 * by more for the Queen; a non-zero result is at least ACTOR_MIN_HEALTH. names: invented */
#define ACTOR_HEALTH_SHIFT 4
#define ACTOR_BOSS_HEALTH_SHIFT 6
#define ACTOR_MIN_HEALTH 16

/* A signed byte kept in a u_char (a jet's height step in actor_t.attachedIndex, the amount of
 * Actor_GroupAdd): values from BYTE_SIGN_BIT on are negative and widen with
 * BYTE_SIGN_EXTENSION. names: invented */
#define BYTE_MASK 0xff
#define BYTE_SIGN_BIT 0x80
#define BYTE_SIGN_EXTENSION 0xff00

/* Actor_Project keeps an actor between ACTOR_NEAR_DEPTH and DRAW_FAR_DEPTH whose screen
 * position is within the clip box (the y bounds only from ACTOR_CLIP_Y_DEPTH on).
 * Actor_Draw shades actors nearer than the shadow depths. names: invented */
#define ACTOR_NEAR_DEPTH 0x80
#define ACTOR_CLIP_MIN_X ( -0x7f )
#define ACTOR_CLIP_MAX_X 0x1bf
#define ACTOR_CLIP_MIN_Y ( -0x3f )
#define ACTOR_CLIP_MAX_Y 0x16f
#define ACTOR_CLIP_Y_DEPTH 0x400
#define ACTOR_SHADOW_DEPTH 0x1000
#define ACTOR_BOSS_SHADOW_DEPTH 0x1800

/* The face-hugger overlay (Actor_StartHuggerOverlay, Actor_StepHuggerOverlay): centred, rising from
 * below the screen to its top row, dropping away when the hugger lets go. names: invented */
#define HUGGER_OVERLAY_X 0xa0
#define HUGGER_OVERLAY_HIDDEN_Y 0x154
#define HUGGER_OVERLAY_TOP_Y 0x78
#define HUGGER_OVERLAY_DROP_STEP 0x10

/* actor_t.action values shared by every kind; 0..3 are per-kind behaviour states. */
enum {
    ACTOR_LEAP = 4, /* invented name: set with an upward velocity by Actor_StartAttack, Actor_SpawnFromObject and Actor_SpawnLeaping */
    ACTOR_HURT = 5,  /* invented name: set by Actor_Hurt while stunTimer runs */
    ACTOR_DYING = 6, /* invented name: set by Actor_Die */
    ACTOR_DEAD = 7   /* invented name: the death animation has finished (Actor_EggTask) */
};

typedef struct actor_s {
    SVECTOR position;    /* RotTransPers input (Actor_Project) */
    SVECTOR scale;       /* invented name: 0x100 per axis when Actor_ToCeiling changes kind */
    VECTOR  playerDelta; /* invented name: |player - position| per axis (Actor_MeasurePlayer) */
    VECTOR  targetDelta; /* invented name: |target - position| per axis (Actor_MeasureTarget) */
    SVECTOR velocity;    /* rotated by heading (Actor_SetVelocity) */
    SVECTOR target; /* invented name: waypoint, set by Actor_PickTargetNearPlayer and the layout walk Actor_TargetTowardPlayer */
    short viewCone; /* invented name: the player is seen within +-viewCone of angle (Actor_SeesPlayer); actorSlot_t.viewCone */
    short angle;        /* heading, stepped toward desiredAngle by turnRate (Actor_TurnToward) */
    short desiredAngle; /* invented name */
    short turnRate;     /* invented name */
    short farRange;     /* invented name: distance class 2/3 boundary (Actor_MeasurePlayer) */
    short nearRange;    /* invented name: distance class 1/2 boundary (Actor_MeasurePlayer) */
    short shotSpeed; /* invented name: added to the speed of spat projectiles (Particle_ActorFireBullet, Particle_ActorLobGrenade), 0 at spawn (Actor_Spawn) */
    short stunTimer; /* invented name: counts down; blocks damage while nonzero (Actor_EggTask, Actor_Damage) */
    short reach; /* invented name: Actor_HearsPlayer attacks within reach / 2, or reach with noise; actorSlot_t.reach */
    short idleTimer;   /* invented name: counts down; idle action waits for it (Actor_EggTask) */
    short playerAngle; /* invented name: heading from position to the player (Actor_EggTask) */
    short targetAngle; /* invented name: heading from position to target (Actor_PickTargetNearPlayer) */
    short health;      /* invented name: reduced by damage, killed at 0 (Actor_Damage) */
    short height;      /* invented name: spawn height offset (Actor_ColonistReleaseBurster) */
    short ceiling;     /* invented name: CeilingHeight ceiling height under the actor (Actor_Move) */
    short speed; /* invented name: actorSlot_t.speed at spawn (Actor_Spawn); Actor_Hurt knocks back at speed >> 2; Actor_ColonistReleaseBurster reads it as a slot index (identity-TODO) */
    u_int playerDistance; /* invented name: Actor_DistanceXZ distance to the player (Actor_MeasurePlayer) */
    u_int targetDistance; /* invented name: Actor_DistanceXZ distance to target (Actor_MeasureTarget) */
    int engageCooldown; /* invented name: frames before Actor_CreatureTask may engage again; set to ACTOR_CHARGE_COOLDOWN or ACTOR_RETARGET_COOLDOWN, counted down each frame, cleared at spawn (Actor_Spawn) */
    u_char flags;
    u_char direction;      /* invented name: octant of angle */
    u_char shownDirection; /* invented name: octant last sent to the animation (Actor_FaceView), 0xff forces a resend */
    u_char action;         /* invented name: behaviour state */
    u_char index;          /* pool index (Actor_InitPool) */
    u_char lastTrigger; /* invented name: trigger number (mapCell_t.trigger) of the cell the actor stood in last (level_triggers.c), 0 for none; cleared at spawn (Actor_Spawn) */
    u_char          attachedIndex; /* invented name: actor slot spawned by Actor_EggReleaseHugger, 0xff for none */
    u_char          playerClass;   /* invented name: distance class 0..4 to the player (Actor_MeasurePlayer) */
    u_char          kind;
    u_char          targetClass; /* invented name: distance class 0..4 to target (Actor_MeasureTarget) */
    u_char          layoutIndex; /* invented name: layout record the actor walks from (Actor_TargetTowardPlayer) */
    u_char          slotIndex;   /* invented name: actorSlot_t the actor was spawned from (Actor_Spawn) */
    mapCell_t      *cell;        /* invented name: its mask is tested against g_visibleCellMask for visibility */
    trackEntry_t   *animations;  /* invented name: animation table indexed by Actor_PlayAnimation */
    sprite_t        sprite;
    struct TASK    *task; /* invented name: the actor's update task; its state[1] is the attack phase */
    struct actor_s *next;
    struct actor_s *prev;
} actor_t;

/* name: inferred from use: Actor_PoolAlloc allocates the ACTOR_COUNT records into it */
extern actor_t *g_actorPool;
/* name: inferred from use: actors are linked through next/prev from it */
extern actor_t *g_liveActors;
/* name: inferred from use: Actor_Project appends each visible actor */
extern actor_t *g_visibleActors[ACTOR_COUNT];

/* The smart gun's target list (src/particle.c: Particle_CollectTargets, Particle_FireSmartGun). */
typedef struct actorTarget_s {
    actor_t *actor;
    short    distance;
    short    angle;
} actorTarget_t;
extern actorTarget_t g_targets[];   /* name: inferred from Particle_CollectTargets */
extern short         g_targetCount; /* name: inferred from Particle_CollectTargets */

extern void Actor_Free( actor_t *actor );
extern void Actor_InitPool( void );
extern void Actor_PoolAlloc( void );
extern int Actor_PassedTarget( actor_t *actor );
extern actor_t *Actor_Alloc( void );
extern void Actor_Link( actor_t *actor );
extern void Actor_Unlink( actor_t *actor );

/* Actor behaviour (src/actor.c). */
extern short Actor_SlotHealth( actorSlot_t *slot );
extern actor_t *Actor_Spawn( int slot );
extern void Actor_PlayAnimation( actor_t *actor, short index );
extern void Actor_ToCeiling( actor_t *actor, short index );
extern void Actor_ToFloor( actor_t *actor, short index );
extern void Actor_Project( actor_t *actor );
extern void Actor_Die( actor_t *actor );
extern void Actor_Move( actor_t *actor );
extern void Actor_MeasurePlayer( actor_t *actor );
extern void Actor_MeasureTarget( actor_t *actor );
extern int Actor_PathClear( actor_t *actor, short angle, u_int distance );
extern int Actor_LineOfSight( actor_t *actor, short angle, u_int distance );
extern void Actor_TurnToward( actor_t *actor, int angle );
extern void Actor_Stop( actor_t *actor );
extern void Actor_SetVelocity( actor_t *actor, short angle, short speed );
extern int Actor_SeesPlayer( actor_t *actor );

/* src/actor.c */
extern void Actor_GroupAdd( u_char amount, u_char index );
extern void Actor_Init( void );
extern void Actor_Damage( actor_t *actor, short amount, short setFlag );
extern void Actor_DrawTask( struct TASK *task );

#endif
