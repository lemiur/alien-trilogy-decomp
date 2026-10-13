/*
 * Particle pool node (src/particle.c: allocated by particle_alloc, started by
 * particle_link) and the inline launch helper whose stores go through particle_link's
 * argument register.
 * Requires <sys/types.h>, <LIBGTE.H>, <LEVEL.H> and <MODEL.H>.
 */
#ifndef PARTICLE_H
#define PARTICLE_H

typedef struct Particle {
    SVECTOR *position;
    SVECTOR *velocity;
    DispatchRow *model; /* invented name: drawn by the particle renderer (particle_draw_debris) */
    MapCell *cell; /* map cell under position (particle_step_back) */
    SVECTOR rotation;
    short life; /* signed: particles_task bounces while it is >= 5 */
    short state;
    u_char size;
    u_char large;
    u_char kind;
    u_char last_trigger; /* invented name: trigger number (MapCell.trigger) of the cell the particle was in last (level_triggers.c), 0 for none */
    struct Particle *next;
    struct Particle *prev;
} Particle;

#define PARTICLE_COUNT 128

/* Particle.kind values. names: analysis/runtime/functions.json (the spawners and the
 * particle_draw_* renderers of each kind) unless marked. */
enum {
    PARTICLE_KIND_PISTOL_SHOT = 0, /* pistol_fire */
    PARTICLE_KIND_SHOTGUN_SHOT = 1, /* shotgun_fire; with a doubled hit box, passes through the actors it hits */
    PARTICLE_KIND_PULSE_RIFLE_SHOT = 2, /* pulse_rifle_fire */
    PARTICLE_KIND_FLAME = 3, /* flame_launch (flamethrower) */
    PARTICLE_KIND_SMART_GUN_SHOT = 4, /* smart_gun_fire; passes through the actors it hits */
    PARTICLE_KIND_ENEMY_SHOT = 5, /* actor_fire_bullet */
    PARTICLE_KIND_SHARD = 6, /* a shard of a shattered object's model row (shard_split) */
    PARTICLE_KIND_SPARK = 7, /* impact_spray */
    PARTICLE_KIND_BLOOD = 8, /* red blood drop (actor_bleed) */
    PARTICLE_KIND_ACID_BLOOD = 9, /* green blood drop */
    PARTICLE_KIND_SYNTH_BLOOD = 10, /* white blood drop of synthetics */
    PARTICLE_KIND_EXPLOSIVE = 11, /* invented name: explodes like the grenades; no matched spawner, never created at runtime (identity-TODO) */
    PARTICLE_KIND_PULSE_GRENADE = 12, /* pulse_grenade_launch */
    PARTICLE_KIND_ENEMY_GRENADE = 13, /* actor_lob_grenade */
    PARTICLE_KIND_STEAM = 14, /* horizontal steam jet puff */
    PARTICLE_KIND_PANEL = 15, /* a wall debris panel (wall_shatter) */
    PARTICLE_KIND_FIRE = 16, /* horizontal flame jet */
    PARTICLE_KIND_FIRE_VERTICAL = 17, /* vertical flame jet */
    PARTICLE_KIND_STEAM_VERTICAL = 18, /* vertical steam jet puff */
    PARTICLE_KIND_GRENADE = 19, /* grenade_throw (hand grenade) */
    PARTICLE_KIND_SHOTGUN_SHELL = 20, /* eject_shell */
    PARTICLE_KIND_PISTOL_SHELL = 21 /* eject_shell */
};

/* Hit box of particle_hit_player around the player's position. names: invented */
#define PARTICLE_PLAYER_HIT_RADIUS 0x100 /* on x and z */
#define PARTICLE_PLAYER_HIT_HEIGHT 0x400 /* above the player's position */

/* Damage a particle deals (Particle.state of the shots and grenades; particle_hit_actors
 * doubles it while the particle is less than half its launch size). names: invented */
#define PARTICLE_DAMAGE_PISTOL 0x10
#define PARTICLE_DAMAGE_SHOTGUN 0x50
#define PARTICLE_DAMAGE_PULSE_RIFLE 0x60
#define PARTICLE_DAMAGE_FLAME 0x50
#define PARTICLE_DAMAGE_SMART_GUN 0xa0
#define PARTICLE_DAMAGE_GRENADE 0x140 /* hand and pulse rifle grenades */
#define PARTICLE_DAMAGE_ENEMY_GRENADE 0x19

/* The smart gun aims at actors within SMART_GUN_RANGE and +-SMART_GUN_FOV of the player's
 * heading (collect_targets). names: invented */
#define SMART_GUN_RANGE 0x1c00
#define SMART_GUN_FOV 0x80

/* particle_draw skips particles beyond this depth; model particles (particle_draw_debris)
 * draw out to PARTICLE_MODEL_FAR_DEPTH. names: invented */
#define PARTICLE_FAR_DEPTH 0x1800
#define PARTICLE_MODEL_FAR_DEPTH 0x2000
/* particle_draw_flame draws a flame this much nearer in the ordering table. name: invented */
#define PARTICLE_FLAME_DEPTH_BIAS 0x200

/* A spark's colour: impact_spray's selection and the spark's Particle.state
 * (particle_draw_spark); shot_impact picks it by the struck cell's type. names: invented */
enum {
    SPARK_YELLOW = 0,
    SPARK_BLUE = 1,
    SPARK_GREEN = 2
};

/* Particle.state of the jets' puffs: damage to the player (jet_spray). names: invented */
#define PARTICLE_DAMAGE_JET 5
#define PARTICLE_DAMAGE_FLAME_JET 10

/* Spawn tuning. A random value is base + (random() & mask); a grenade's Particle.size is
 * its fuse (particles_task explodes it at 0) and its life the bounce speed. names: invented */
#define IMPACT_SPARKS_SHOTGUN 32 /* impact_spray: sparks per hit */
#define IMPACT_SPARKS_RAPID 16 /* pulse rifle and smart gun */
#define IMPACT_SPARKS 4
#define SPARK_SPEED 0x40 /* SPARK_YELLOW */
#define SPARK_SPEED_COLOURED 0x10
#define SPARK_SPEED_MASK 0x1f
#define SPARK_SIZE 0x10
#define SPARK_LIFE 0x10
#define SPARK_LIFE_MASK 0x1f
#define SPARK_LIFT 0x10
#define SPARK_LIFT_MASK 0xf
#define BLOOD_DROPS_BURST 64 /* blood_spray, for an explosive */
#define BLOOD_DROPS 4
#define BLOOD_SIZE 0x20
#define BLOOD_SIZE_MASK 0xf
#define BLOOD_LIFE 0x30
#define BLOOD_LIFE_MASK 0x1f
#define BLOOD_LIFT 0x10
#define BLOOD_LIFT_MASK 0xf
#define BLOOD_SPEED 0x10
#define BLOOD_SPEED_MASK 0x1f
#define ENEMY_SHOT_SPEED 0x80 /* plus Actor.shot_speed (actor_fire_bullet, actor_lob_grenade) */
#define ENEMY_SHOT_SIZE 0x40
#define GUARD_GUN_HEIGHT 0x2c0 /* above the actor's position */
#define ACTOR_GUN_HEIGHT 0x240 /* the soldier's and the synthetic's */
#define ENEMY_SHOT_AIM_DROP 0x280
#define JET_PUFF_SIZE 0x20 /* jet_spray */
#define ENEMY_GRENADE_HEIGHT 0x180 /* actor_lob_grenade */
#define ENEMY_GRENADE_LIFT 0x20
#define ENEMY_GRENADE_LIFE 0x10
#define ENEMY_GRENADE_FUSE 0x40
#define SHELL_EJECT_OFFSET_Y 200 /* eject_shell: from the view */
#define SHELL_EJECT_OFFSET_Z 0x280
#define SHELL_EJECT_ANGLE 0x40
#define SHELL_EJECT_LIFT 0x30
#define SHELL_EJECT_SPEED 8
#define SHELL_EJECT_HEADING 0x80
#define SHELL_EJECT_HEADING_MASK 0x7f
#define SHELL_SIZE 0x20
#define SHOT_DROP 0x60 /* fire_bullet: below the view */
#define SHOT_SPEED 0x100 /* plus the player's speed */
#define SMART_GUN_SHOT_DROP 0xa0 /* further below for the smart gun */
#define PISTOL_SHOT_SIZE 20
#define SHOTGUN_SHOT_SIZE 0x10
#define PULSE_RIFLE_SHOT_SIZE 28
#define SMART_GUN_SHOT_SIZE 28
#define PISTOL_SHOT_ANGLE 40 /* fire_bullet's muzzle: angle and side offset */
#define PULSE_RIFLE_SHOT_ANGLE 0x30
#define PISTOL_MUZZLE_X 40
#define RIFLE_MUZZLE_X 0x20 /* shotgun and pulse rifle */
#define PULSE_RIFLE_BURST 3 /* shots per pulse_rifle_fire */
#define SMART_GUN_BURST 3
#define BURST_SPEED_SHIFT 7 /* each shot of a burst flies 1 << it faster */
#define FLAME_BURST 4 /* flamethrower_fire launches charges 1 to FLAME_BURST - 1 */
#define FLAME_DROP 0x80 /* flame_launch */
#define FLAME_DROP_MASK 0x1f
#define FLAME_SIZE 0x10 /* less the charge; particle_draw_flame grows it as it shrinks */
#define FLAME_MUZZLE_X 0x38
#define FLAME_ANGLE 0x38
#define FLAME_SPEED 0x80
#define FLAME_SPEED_MASK 0x3f
#define SMART_GUN_DROP 0x100 /* smart_gun_fire */
#define SMART_GUN_MUZZLE 400
#define GRENADE_LIFT 24 /* pulse_grenade_launch, grenade_throw */
#define PULSE_GRENADE_DROP 0x80
#define PULSE_GRENADE_SPEED 0x100 /* plus the player's speed, at most this */
#define PULSE_GRENADE_LIFE 0x10
#define PULSE_GRENADE_FUSE 0x30
#define HAND_GRENADE_DROP 0x100
#define HAND_GRENADE_SPEED 0x40
#define HAND_GRENADE_MAX_SPEED 0x80
#define HAND_GRENADE_LIFE 8
#define HAND_GRENADE_FUSE 0x40

/* Drawing (particle_draw and its helpers). names: invented */
#define PARTICLE_FLAME_NEAR_DEPTH 0x10 /* a flame is drawn from this depth on */
#define PARTICLE_FLAME_MIN_DEPTH 0x40 /* PARTICLE_FLAME_DEPTH_BIAS stops here */
#define FLAME_SMALL_SIZE 4 /* below it a flame grows faster */
#define FLAME_GROWTH_SMALL 5
#define FLAME_GROWTH 3
#define FLAME_SCALE_SHIFT 9
#define FLAME_FRAME_MASK 7
#define FLAME_ADDITIVE_SIZE 8 /* below it the flame's tpage blends additively at a quarter */
#define STEAM_SIZE_MAX 0x20 /* the size tables' length */
#define STEAM_FRAME_MASK 3
#define STEAM_SCALE 15000
#define STEAM_JITTER_MASK 7
#define STEAM_JITTER 3
#define STEAM_BRIGHTNESS 65 /* plus the size */
#define FIRE_LENGTH_SCALE 16000
#define FIRE_WIDTH_SCALE 8000
#define FIRE_VERTICAL_WIDTH_SCALE 12000
#define FIRE_BRIGHTNESS 105
#define FIRE_OPAQUE_SIZE 28 /* from it on fire is drawn opaque */
#define GRENADE_SPIN 0x20 /* rotation per tick (particle_draw_model) */
#define SHELL_SPIN_X 0x40
#define SHELL_SPIN_Y 0x60
#define DEBRIS_SPIN 0x40
#define DEBRIS_SCALE_SHIFT 6 /* Particle.size << it is the model scale */
#define PARTICLE_SCALE_SHIFT 8 /* spark and blood size on screen: (size << it) / depth */
#define BLOOD_LARGE_SCALE_SHIFT 10
#define SPARK_FADE_SIZE 9 /* smaller sparks are depth-cued by size << SPARK_FADE_SHIFT */
#define SPARK_FADE_SHIFT 9
#define BLOOD_FRAME_FIRST 22 /* g_explosion_uvs: four blood frames */
#define BLOOD_FRAME_MASK 3
#define BLOOD_FRAME_RATE_SHIFT 2

/* Movement (particles_task). Explosions scatter by up to EXPLOSION_SCATTER either way and
 * rise EXPLOSION_RISE (plus random() & EXPLOSION_RISE_MASK) when blown off a step.
 * names: invented */
#define EXPLOSION_SCATTER 0x100
#define EXPLOSION_SCATTER_MASK 0x1ff
#define EXPLOSION_RISE 0x200
#define EXPLOSION_RISE_MASK 0x7f
#define SCATTER_SPEED_MASK 0xf
#define PARTICLE_GRAVITY 4
#define SHELL_GRAVITY 6
#define GRENADE_BOUNCE_HEIGHT 0x20 /* a grenade bounces this far above the floor */
#define GRENADE_STEP_LIMIT 0x40 /* deeper below the floor it explodes */
#define GRENADE_BOUNCE_MIN 5 /* from this life on a bounce costs 2; below it the grenade slows */
#define JET_PUFF_CEILING_GAP 0x10 /* in cell height units: a puff turns this far below the ceiling */
#define JET_PUFF_FLOOR_GAP 4 /* or above the floor */
#define JET_PUFF_BOUNCE 10
#define FIRE_FALL_SIZE 20 /* from below it a fire puff sinks and slows */
#define FIRE_DRAG 0xc
#define STEAM_RISE_SIZE 0xc
#define STEAM_RISE 10
#define DEBRIS_REST_HEIGHT 0x40 /* above the floor */
#define DEBRIS_BOUNCE_LOSS 8
#define DEBRIS_BOUNCE_MIN 4
#define SPARK_BLUE_SURFACE 0x180 /* sparks stop this far above the floor of the cell types behind SPARK_BLUE and SPARK_GREEN */
#define SPARK_GREEN_SURFACE 0x100
#define PARTICLE_CEILING_GAP 0x100 /* particle_hit_floor_or_ceiling puts a particle this far below the ceiling */

/* name: inferred from use: particle_pool_init points each particle's position at one of them */
extern SVECTOR g_particle_positions[PARTICLE_COUNT];
/* name: inferred from use: particle_pool_init points each particle's velocity at one of them */
extern SVECTOR g_particle_velocities[PARTICLE_COUNT];

extern void particle_free(Particle *particle);
extern void particle_pool_init(void);
extern void particle_pool_alloc(void);
extern Particle *particle_alloc(void);
extern void particle_link(Particle *particle);
extern void particle_unlink(Particle *particle);

/* Particle hit tests (src/particle_hits.c): particle_step_back steps a particle back by its
 * velocity; the others return -1 on a hit. */
extern void particle_step_back(Particle *particle);
extern int particle_hit_floor_or_ceiling(Particle *particle);
extern int particle_hit_actors(Particle *particle);
extern int particle_hit_player(Particle *particle);

/* Particle helpers and spawners of the weapon code (src/particle.c). */
extern int particle_hit_wall(Particle *particle);
extern void impact_spray(Particle *source, short selection);
extern void eject_shell(short weapon);
extern int fire_bullet(short speed, u_char kind);

static __inline__ void particle_launch(Particle *particle, short rx, short ry, short rz,
                                       int life, int size, int kind, int state)
{
    particle->rotation.vx = rx;
    particle->rotation.vy = ry;
    particle->rotation.vz = rz;
    particle->life = life;
    particle->size = size;
    particle->kind = kind;
    particle->state = state;
    particle_link(particle);
}

/* src/particle.c: spawns a particle burst at a map position */
extern void explosion_spawn(int x, int y, int z);

struct Actor;
struct TASK;

/* src/particle.c */
extern Particle *jet_spray(SVECTOR *origin, short speed, short heading, short kind);
extern void actor_fire_bullet(struct Actor *actor);
extern void actor_lob_grenade(struct Actor *actor);
extern void actor_bleed(struct Actor *actor, Particle *source);
extern void particles_task(struct TASK *task);
extern void big_explosion_spawn(int x, int y, int z);
extern void pistol_fire(void);
extern void shotgun_fire(void);
extern void pulse_rifle_fire(void);
extern void flamethrower_fire(void);
extern void smart_gun_fire(void);
extern void pulse_grenade_launch(void);
extern void grenade_throw(void);

#endif
