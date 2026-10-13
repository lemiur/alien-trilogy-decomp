/* Particles: the particle pool (128 Particles with their position and velocity
 * vectors, see <PARTICLE.H>), the weapon shots and the spawners that use it, the smart
 * gun's target list and the particle renderers. Retail .text from 0x800246ac to at
 * least 0x80027a18. One translation unit: its statics (.sbss 0x8008d128..0x8008d138)
 * are read from particle_pool_init (0x800246c4) to the per-frame particle task at
 * 0x80027a18, so the code between is one module; the .rdata of its jump tables
 * (0x80011a54..) follows the function order. Functions not yet matched are absent.
 * Name invented.
 */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <MODEL.H>
#include <PARTICLE.H>
#include <HEAP.H>
#include <CAMERA.H>
#include <TASK.H>
#include <AUDIO.H>
#include <GAME.H>
#include <ITEMS.H>
#include <ABS.H>
#include <TRACK.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <ACTOR.H>
#include <VIEW.H>
#include <LIBGPU.H>
#include <DISPLAY.H>
#include <TEXTURE.H>
#include <DEBRIS.H>
#include <EFFECTS.H>
typedef signed short s16;
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned short u16;

/* Module statics (.sbss 0x8008d128..0x8008d138), in their retail order. */
/* name: inferred from use: particle_pool_alloc allocates the PARTICLE_COUNT records into it */
static Particle *g_particle_pool;
/* name: inferred from use: particle_free pushes and particle_alloc pops it */
static Particle *g_particle_free;
/* name: inferred from use: particle_link pushes and particle_unlink removes live particles */
static Particle *g_live_particles;

extern void gteMIMefunc(SVECTOR *otp, SVECTOR *dfp, long n, long p);
/* Explosion sprite sizes by remaining size (particle_draw_steam: width, height) and UV picks
 * (four per size step, chosen at random); particle_draw_fire's second set. names: invented */
/* name: analysis/runtime/globals.json (round 2) */
static int g_steam_lengths[] = {
    10, 10, 8, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 8, 8, 8, 8
};
/* name: analysis/runtime/globals.json (round 2) */
static int g_steam_widths[] = {
    5, 5, 5, 5, 5, 6, 6, 6, 6, 5, 5, 4, 4, 4, 4, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2
};
/* name: analysis/runtime/globals.json (round 2) */
static short g_steam_frames[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6,
    7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8
};
/* name: analysis/runtime/globals.json (round 2) */
static int g_fire_lengths[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8
};
/* name: analysis/runtime/globals.json (round 2) */
static int g_fire_widths[] = {
    8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6,
    5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3
};
/* name: analysis/runtime/globals.json (round 2) */
static short g_fire_frames[] = {
    2, 2, 2, 2, 4, 3, 4, 3, 3, 4, 3, 4,
    4, 4, 4, 4, 3, 4, 4, 5, 4, 4, 5, 4,
    4, 4, 5, 5, 5, 6, 5, 6, 6, 7, 7, 7
};

void particle_free(Particle *particle)
{
    particle->next = g_particle_free;
    g_particle_free = particle;
}

/* name: inferred from its body, which gives every pool particle its vectors and frees it */
void particle_pool_init(void)
{
    Particle *particle;
    SVECTOR *position;
    SVECTOR *velocity;
    int i;

    g_particle_free = 0;
    g_live_particles = 0;
    velocity = g_particle_velocities;
    particle = g_particle_pool;
    position = g_particle_positions;
    i = 0;
    do {
        particle->position = position;
        particle->velocity = velocity;
        particle->life = 0;
        particle->size = 0;
        particle->kind = 0;
        particle_free(particle);
        ++i;
        ++particle;
        velocity++;
        position++;
    } while (i < PARTICLE_COUNT);
}

/* name: inferred from its body, which allocates the PARTICLE_COUNT pool records */
void particle_pool_alloc(void) { g_particle_pool = (Particle *)heap_alloc(PARTICLE_COUNT * sizeof(Particle)); }

Particle *particle_alloc(void)
{
    Particle *first;

    if ((first = g_particle_free))
        g_particle_free = first->next;

    return first;
}

void particle_link(Particle *particle)
{
    Particle *old = g_live_particles;
    if (old) old->prev = particle;
    g_live_particles = particle;
    particle->next = old;
    particle->prev = 0;
}

/* name: inferred from its body, which unlinks a particle from the live list, freeing its debris panel */
void particle_unlink(Particle *particle)
{
    Particle *next;
    Particle *previous;
    if (particle->kind == PARTICLE_KIND_SHARD || particle->kind == PARTICLE_KIND_PANEL)
        panel_free(particle);
    next = particle->next;
    previous = particle->prev;
    if (next != 0)
        next->prev = previous;
    if (previous != 0)
        previous->next = next;
    else
        g_live_particles = next;
    particle->prev = 0;
    particle->next = 0;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void explosion_spawn(int x, int y, int z) {
    SVECTOR position;
    TASK *node;
    position.vx = (s16)x;
    position.vy = (s16)y;
    position.vz = (s16)z;
    sound_play_at(SOUND_EXPLOSION, &position, SOUND_AT_NEAR);
    node = TaskCreate(TASK_PRIORITY_ACTORS, explosion_task);
    if (node != 0) {
        node->state[0] = (s16)x;
        node->state[1] = (s16)y;
        node->state[2] = (s16)z;
        node->state[3] = 0;
    }
}

/* name: static evidence, analysis/runtime/functions.json */
void big_explosion_spawn(int x, int y, int z)
{
    SVECTOR position;
    TASK *node;

    position.vx = (s16)x;
    position.vy = (s16)y;
    position.vz = (s16)z;
    sound_play_at(SOUND_BIG_EXPLOSION, &position, SOUND_AT_NEAR);
    node = TaskCreate(TASK_PRIORITY_ACTORS, big_explosion_task);
    if (node != 0) {
        node->state[0] = (s16)x;
        node->state[1] = (s16)y;
        node->state[2] = (s16)z;
        node->state[3] = 0;
        g_game_flags |= GAME_FLAG_FLASH;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void impact_spray(Particle *source, short selection)
{
    SVECTOR *origin = source->position;
    u_int count;
    int speed;
    Particle *particle;
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR rotation;

    switch (source->kind) {
    case PARTICLE_KIND_SHOTGUN_SHOT:
        count = IMPACT_SPARKS_SHOTGUN;
        break;
    case PARTICLE_KIND_PULSE_RIFLE_SHOT:
    case PARTICLE_KIND_SMART_GUN_SHOT:
        count = IMPACT_SPARKS_RAPID;
        break;
    case PARTICLE_KIND_PISTOL_SHOT:
    case PARTICLE_KIND_FLAME:
    case PARTICLE_KIND_ENEMY_SHOT:
    default:
        count = IMPACT_SPARKS;
        break;
    }
    if (selection == SPARK_YELLOW)
        speed = SPARK_SPEED;
    else
        speed = SPARK_SPEED_COLOURED;
    while (count-- > 0) {
        particle = particle_alloc();
        if (particle == 0)
            break;
        rotation.vx = 0;
        rotation.vy = random() & ANGLE_MASK;
        rotation.vz = 0;
        particle->size = SPARK_SIZE;
        particle->kind = PARTICLE_KIND_SPARK;
        particle->state = selection;
        position = particle->position;
        copyVector(position, origin);
        particle->life = (random() & SPARK_LIFE_MASK) + SPARK_LIFE;
        velocity = particle->velocity;
        velocity->vx = 0;
        velocity->vy = (random() & SPARK_LIFT_MASK) + SPARK_LIFT;
        velocity->vz = speed + (random() & SPARK_SPEED_MASK);
        PushMatrix();
        rotate_vector(&rotation, velocity, velocity);
        PopMatrix();
        particle_link(particle);
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void shot_impact(Particle *particle)
{
    u8 type = particle->cell->type;
    if (type == CELL_TYPE_LIQUID_DEEP) impact_spray(particle, SPARK_BLUE);
    else if (type == CELL_TYPE_LIQUID_SHALLOW) impact_spray(particle, SPARK_GREEN);
    else impact_spray(particle, SPARK_YELLOW);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void blood_splash(Particle *source)
{
    TASK *node;
    if (source->kind == PARTICLE_KIND_ACID_BLOOD)
        node = TaskCreate(TASK_PRIORITY_ACTORS, acid_splash_task);
    else if (source->kind == PARTICLE_KIND_BLOOD)
        node = TaskCreate(TASK_PRIORITY_ACTORS, blood_splash_task);
    else
        node = TaskCreate(TASK_PRIORITY_ACTORS, synth_splash_task);
    if (node != 0) {
        node->state[0] = source->position->vx;
        node->state[1] = source->position->vy;
        node->state[2] = source->position->vz;
        node->state[3] = 0;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void blood_spray(Particle *source, int kind)
{
    SVECTOR *origin = source->position;
    u_int count;
    int large;
    Particle *particle;
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR rotation;

    if (source->kind == PARTICLE_KIND_EXPLOSIVE || source->kind == PARTICLE_KIND_PULSE_GRENADE || source->kind == PARTICLE_KIND_GRENADE) {
        count = BLOOD_DROPS_BURST;
        large = 1;
    } else {
        count = BLOOD_DROPS;
        large = 0;
    }
    while (count-- > 0) {
        particle = particle_alloc();
        if (particle == 0)
            break;
        rotation.vx = 0;
        rotation.vy = random() & ANGLE_MASK;
        rotation.vz = 0;
        particle->size = (random() & BLOOD_SIZE_MASK) + BLOOD_SIZE;
        particle->kind = kind;
        particle->state = 1;
        particle->large = large;
        position = particle->position;
        copyVector(position, origin);
        particle->life = (random() & BLOOD_LIFE_MASK) + BLOOD_LIFE;
        velocity = particle->velocity;
        velocity->vx = 0;
        velocity->vy = (random() & BLOOD_LIFT_MASK) + BLOOD_LIFT;
        velocity->vz = (random() & BLOOD_SPEED_MASK) + BLOOD_SPEED;
        PushMatrix();
        rotate_vector(&rotation, velocity, velocity);
        PopMatrix();
        particle_link(particle);
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_bleed(Actor *actor, Particle *source)
{
    switch (actor->kind) {
    case ACTOR_KIND_EGG: case ACTOR_KIND_FACEHUGGER: case ACTOR_KIND_CHESTBURSTER: case ACTOR_KIND_BAMBI: case ACTOR_KIND_DOG: case ACTOR_KIND_WARRIOR: case ACTOR_KIND_BOSS: case ACTOR_KIND_WARRIOR_CEILING: case ACTOR_KIND_DOG_CEILING:
        blood_spray(source, PARTICLE_KIND_ACID_BLOOD);
        break;
    case ACTOR_KIND_COLONIST: case ACTOR_KIND_GUARD: case ACTOR_KIND_SOLDIER: case ACTOR_KIND_HANDLER:
        blood_spray(source, PARTICLE_KIND_BLOOD);
        break;
    case ACTOR_KIND_SYNTHETIC:
        blood_spray(source, PARTICLE_KIND_SYNTH_BLOOD);
        break;
    default:
        break;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_fire_bullet(Actor *actor)
{
    Particle *particle = particle_alloc();
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR aim;
    SVECTOR turn;

    if (particle == 0)
        return;
    PushMatrix();
    velocity = particle->velocity;
    position = particle->position;
    velocity->vx = 0;
    velocity->vy = 0;
    velocity->vz = actor->shot_speed + ENEMY_SHOT_SPEED;
    switch (actor->kind) {
    case ACTOR_KIND_GUARD:
        position->vx = actor->position.vx;
        position->vz = actor->position.vz;
        position->vy = actor->position.vy + GUARD_GUN_HEIGHT;
        break;
    case ACTOR_KIND_SOLDIER:
        position->vx = actor->position.vx;
        position->vz = actor->position.vz;
        position->vy = actor->position.vy + ACTOR_GUN_HEIGHT;
        break;
    case ACTOR_KIND_SYNTHETIC:
    default:
        position->vx = actor->position.vx;
        position->vz = actor->position.vz;
        position->vy = actor->position.vy + ACTOR_GUN_HEIGHT;
        break;
    }
    aim.vz = point_distance_xz(position, &g_player.position);
    aim.vx = (short)(position->vy - ENEMY_SHOT_AIM_DROP) - g_player.position.vy;
    aim.vx = point_heading_xz(&g_zero_vector, &aim) - ANGLE_180;
    aim.vy = 0;
    aim.vz = 0;
    turn.vx = 0;
    turn.vy = (actor->angle + ANGLE_180) & ANGLE_MASK;
    turn.vz = ANGLE_180;
    rotate_vector(&aim, velocity, velocity);
    rotate_vector(&turn, velocity, velocity);
    PopMatrix();
    particle->size = ENEMY_SHOT_SIZE;
    particle->kind = PARTICLE_KIND_ENEMY_SHOT;
    particle->state = 1;
    particle->rotation.vx = 0;
    particle->rotation.vy = turn.vy;
    particle->rotation.vz = 0;
    particle_link(particle);
}

/* name: static evidence, analysis/runtime/functions.json */
Particle *jet_spray(SVECTOR *origin, short speed, short heading, short kind)
{
    Particle *particle = particle_alloc();
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR rotation;
    short direction;

    if (particle == 0)
        return 0;
    direction = kind >= 0 ? 1 : -1;
    position = particle->position;
    velocity = particle->velocity;
    rotation.vx = 0;
    rotation.vy = heading & ANGLE_MASK;
    rotation.vz = 0;
    copyVector(position, origin);
    kind = ABS(kind);
    switch (kind) {
    case PARTICLE_KIND_FIRE_VERTICAL:
    case PARTICLE_KIND_STEAM_VERTICAL:
        velocity->vx = 0;
        if (direction == -1)
            velocity->vy = -speed;
        else
            velocity->vy = speed;
        velocity->vz = 0;
        particle->size = JET_PUFF_SIZE;
        particle->state = PARTICLE_DAMAGE_JET;
        break;
    case PARTICLE_KIND_FIRE:
        velocity->vx = 0;
        velocity->vy = 0;
        velocity->vz = speed;
        particle->size = JET_PUFF_SIZE;
        particle->state = PARTICLE_DAMAGE_FLAME_JET;
        break;
    case PARTICLE_KIND_PANEL: /* no jet passes it; retail's jump table has the entry */
    default:
        velocity->vx = 0;
        velocity->vy = 0;
        velocity->vz = speed;
        particle->size = JET_PUFF_SIZE;
        particle->state = PARTICLE_DAMAGE_JET;
        break;
    }
    PushMatrix();
    rotate_vector(&rotation, velocity, velocity);
    PopMatrix();
    particle->large = particle->size;
    particle->kind = kind;
    particle_link(particle);
    return particle;
}

/* name: static evidence, analysis/runtime/functions.json */
void actor_lob_grenade(Actor *actor)
{
    Particle *particle = particle_alloc();
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR rotation;

    if (particle == 0)
        return;
    position = particle->position;
    velocity = particle->velocity;
    position->vx = actor->position.vx;
    position->vy = actor->position.vy + ENEMY_GRENADE_HEIGHT;
    position->vz = actor->position.vz;
    velocity->vx = 0;
    velocity->vy = 0;
    velocity->vz = actor->shot_speed + ENEMY_SHOT_SPEED;
    if (g_player.position.vy > actor->position.vy)
        rotation.vx = (ANGLE_90 / 2);
    else if (g_player.position.vy < actor->position.vy)
        rotation.vx = -(ANGLE_90 / 2);
    else
        rotation.vx = 0;
    rotation.vy = 0;
    rotation.vz = 0;
    PushMatrix();
    rotate_vector(&rotation, velocity, velocity);
    rotation.vx = 0;
    rotation.vy = (actor->angle + ANGLE_180) & ANGLE_MASK;
    rotate_vector(&rotation, velocity, velocity);
    PopMatrix();
    velocity->vy = ENEMY_GRENADE_LIFT;
    particle->model = g_object_models[PICKUP_GRENADES];
    particle_launch(particle, 0, -actor->angle & ANGLE_MASK, 0, ENEMY_GRENADE_LIFE, ENEMY_GRENADE_FUSE, PARTICLE_KIND_ENEMY_GRENADE, PARTICLE_DAMAGE_ENEMY_GRENADE);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void eject_shell(short weapon)
{
    Particle *particle = particle_alloc();
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR offset;
    SVECTOR rotation;
    DispatchRow *model;
    int kind;

    if (particle == 0)
        return;
    position = particle->position;
    velocity = particle->velocity;
    rotation.vx = -g_view_pitch.vx & ANGLE_MASK;
    rotation.vy = 0;
    rotation.vz = 0;
    copyVector(position, &g_view_position);
    offset.vx = 0;
    offset.vy = SHELL_EJECT_OFFSET_Y;
    offset.vz = SHELL_EJECT_OFFSET_Z;
    rotate_vector(&rotation, &offset, &offset);
    rotation.vx = 0;
    rotation.vy = (g_view_rotation.vy + SHELL_EJECT_ANGLE) & ANGLE_MASK;
    rotation.vz = g_view_rotation.vz;
    rotate_vector_offset(&rotation, &offset, position, position);
    rotation.vx = -g_view_pitch.vx & ANGLE_MASK;
    rotation.vy = 0;
    rotation.vz = 0;
    velocity->vx = 0;
    velocity->vy = -SHELL_EJECT_LIFT;
    velocity->vz = g_player.movement.vz + SHELL_EJECT_SPEED;
    rotate_vector(&rotation, velocity, velocity);
    rotation.vx = 0;
    rotation.vy = (g_view_rotation.vy + (short)((random() & SHELL_EJECT_HEADING_MASK) + SHELL_EJECT_HEADING)) & ANGLE_MASK;
    rotation.vz = g_view_rotation.vz;
    rotate_vector(&rotation, velocity, velocity);
    if (weapon == WEAPON_PISTOL) {
        model = g_object_models[PICKUP_PISTOL_SHELL];
        kind = PARTICLE_KIND_PISTOL_SHELL;
    } else {
        model = g_object_models[PICKUP_SHOTGUN_SHELL];
        kind = PARTICLE_KIND_SHOTGUN_SHELL;
    }
    particle->kind = kind;
    particle->model = model;
    particle->rotation.vx = random() & ANGLE_MASK;
    particle->rotation.vy = random() & ANGLE_MASK;
    particle->rotation.vz = 0;
    particle->size = SHELL_SIZE;
    particle_link(particle);
}

/* name: runtime evidence, analysis/runtime/functions.json */
int fire_bullet(short speed, u_char kind)
{
    Particle *particle = particle_alloc();
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR rotation;
    u_int x;
    u_int z;

    if (particle == 0)
        return 0;
    particle->kind = kind;
    position = particle->position;
    velocity = particle->velocity;
    position->vx = g_view_position.vx;
    position->vy = g_view_position.vy - SHOT_DROP;
    position->vz = g_view_position.vz;
    switch (particle->kind) {
    case PARTICLE_KIND_PISTOL_SHOT:
        particle->size = PISTOL_SHOT_SIZE;
        particle->state = PARTICLE_DAMAGE_PISTOL;
        rotation.vy = PISTOL_SHOT_ANGLE;
        velocity->vx = PISTOL_MUZZLE_X;
        velocity->vy = 0;
        velocity->vz = 0;
        rotate_vector_offset(&g_view_rotation, velocity, position, position);
        break;
    case PARTICLE_KIND_SHOTGUN_SHOT:
        particle->size = SHOTGUN_SHOT_SIZE;
        particle->state = PARTICLE_DAMAGE_SHOTGUN;
        rotation.vy = PISTOL_SHOT_ANGLE;
        velocity->vx = RIFLE_MUZZLE_X;
        velocity->vy = 0;
        velocity->vz = 0;
        rotate_vector_offset(&g_view_rotation, velocity, position, position);
        break;
    case PARTICLE_KIND_PULSE_RIFLE_SHOT:
        particle->size = PULSE_RIFLE_SHOT_SIZE;
        particle->state = PARTICLE_DAMAGE_PULSE_RIFLE;
        rotation.vy = PULSE_RIFLE_SHOT_ANGLE;
        velocity->vx = RIFLE_MUZZLE_X;
        velocity->vy = 0;
        velocity->vz = 0;
        rotate_vector_offset(&g_view_rotation, velocity, position, position);
        break;
    case PARTICLE_KIND_FLAME:
        break;
    case PARTICLE_KIND_SMART_GUN_SHOT:
        position->vy -= SMART_GUN_SHOT_DROP;
        particle->size = SMART_GUN_SHOT_SIZE;
        particle->state = PARTICLE_DAMAGE_SMART_GUN;
        rotation.vy = 0;
        break;
    default:
        particle->size = PISTOL_SHOT_SIZE;
        particle->state = PARTICLE_DAMAGE_PISTOL;
        rotation.vy = PISTOL_SHOT_ANGLE;
        break;
    }
    particle->large = particle->size;
    rotation.vx = -g_view_pitch.vx & ANGLE_MASK;
    rotation.vz = 0;
    velocity->vx = 0;
    velocity->vy = 0;
    velocity->vz = speed + SHOT_SPEED;
    rotate_vector(&rotation, velocity, velocity);
    rotate_vector(&g_view_rotation, velocity, velocity);
    x = position->vx + g_level_map.offset_x;
    z = position->vz + g_level_map.offset_z;
    if (MAP_OUTSIDE(x, z)) {
        particle_free(particle);
        return 0;
    }
    particle->cell = MAP_CELL(x, z);
    if (particle_hit_wall(particle)) {
        particle_free(particle);
        return -1;
    }
    position->vx += velocity->vx;
    position->vy += velocity->vy;
    position->vz += velocity->vz;
    x = position->vx + g_level_map.offset_x;
    z = position->vz + g_level_map.offset_z;
    if (MAP_OUTSIDE(x, z)) {
        particle_free(particle);
        return 0;
    }
    particle->cell = MAP_CELL(x, z);
    if (particle_hit_wall(particle)) {
        particle_free(particle);
        return -1;
    }
    particle_link(particle);
    return -1;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void pistol_fire(void)
{
    if (fire_bullet(g_player.movement.vz, PARTICLE_KIND_PISTOL_SHOT) == 0) return;
    eject_shell(WEAPON_PISTOL);
    if (CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO)) return;
    g_player.pistol_ammo--;
}
/* name: runtime evidence, analysis/runtime/functions.json */
void shotgun_fire(void)
{
    if (fire_bullet(g_player.movement.vz, PARTICLE_KIND_SHOTGUN_SHOT) == 0) return;
    eject_shell(WEAPON_SHOTGUN);
    g_player.flash = PLAYER_FLASH_SHOTGUN;
    g_player.flash_heading = ((u_short)g_view_rotation.vy + ANGLE_180) & ANGLE_MASK;
    if (CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO)) return;
    g_player.shotgun_ammo--;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void pulse_rifle_fire(void) {
    s16 i = 0;
    while (i < PULSE_RIFLE_BURST) {
        int offset = (i << 16) >> (16 - BURST_SPEED_SHIFT);
        if (!fire_bullet(g_player.movement.vz + offset, PARTICLE_KIND_PULSE_RIFLE_SHOT)) return;
        if (!CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO)) {
            s16 remaining = (s16)g_player.pulse_rifle_ammo - 1;
            g_player.pulse_rifle_ammo = (u16)remaining;
            if (remaining == 0) break;
        }
        i++;
    }
}

/* flame_launch launches the charged shot particle and steps it `charge` times */
/* name: runtime evidence, analysis/runtime/functions.json */
int flame_launch(short charge)
{
    short steps = charge;
    Particle *particle = particle_alloc();
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR rotation;
    u_int x;
    u_int z;

    if (particle == 0)
        return 0;
    position = particle->position;
    velocity = particle->velocity;
    position->vx = g_view_position.vx;
    position->vy = g_view_position.vy - FLAME_DROP + (random() & FLAME_DROP_MASK);
    position->vz = g_view_position.vz;
    particle->kind = PARTICLE_KIND_FLAME;
    particle->size = FLAME_SIZE - steps;
    particle->large = particle->size;
    particle->state = PARTICLE_DAMAGE_FLAME;
    velocity->vx = FLAME_MUZZLE_X;
    velocity->vy = 0;
    velocity->vz = 0;
    rotate_vector_offset(&g_view_rotation, velocity, position, position);
    rotation.vx = -g_view_pitch.vx & ANGLE_MASK;
    rotation.vy = FLAME_ANGLE;
    rotation.vz = 0;
    velocity->vx = 0;
    velocity->vy = 0;
    velocity->vz = g_player.movement.vz + FLAME_SPEED + (random() & FLAME_SPEED_MASK);
    rotate_vector(&rotation, velocity, velocity);
    rotate_vector(&g_view_rotation, velocity, velocity);
    if (charge != 0) {
        do {
            position->vx += velocity->vx;
            position->vy += velocity->vy;
            position->vz += velocity->vz;
            x = position->vx + g_level_map.offset_x;
            z = position->vz + g_level_map.offset_z;
            if (MAP_OUTSIDE(x, z)) {
                particle_free(particle);
                return 0;
            }
            particle->cell = MAP_CELL(x, z);
            if (particle_hit_wall(particle)) {
                SET_VECTOR_TO_ZERO(*velocity);
                break;
            }
        } while (--steps);
    }
    particle_link(particle);
    return -1;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void flamethrower_fire(void)
{
    s16 i;
    if (flame_launch(1) == 0) return;
    if (!CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO))
        g_player.flamethrower_fuel--;
    i = 2;
    while (i < FLAME_BURST) {
        if (flame_launch(i) == 0) return;
        i++;
    }
}

/* name: inferred from its body: live actors within fov of the player's yaw, with angle and distance */
void collect_targets(s16 fov)
{
    Actor *actor = g_live_actors;
    ActorTarget *target;
    s16 delta;

    g_target_count = 0;
    target = g_targets;
    for (; actor; actor = actor->next) {
        if (actor->kind < ACTOR_KIND_INERT && actor->action < ACTOR_HURT && (actor->cell->mask & g_visible_cell_mask)
            && actor->player_distance <= SMART_GUN_RANGE) {
            delta = ((u16)(g_player.angles.vy - actor->player_angle) << ANGLE_SIGN_SHIFT) >> ANGLE_SIGN_SHIFT;
            if ((s16)ABS(delta) <= fov) {
                target->actor = actor;
                target->angle = delta;
                target->distance = actor->player_distance;
                target++;
                g_target_count++;
            }
        }
    }
}

/* smart_gun_fire fires the smart gun: up to three shots, each guided at the nearest
 * target collect_targets finds, else straight ahead */
/* name: runtime evidence, analysis/runtime/functions.json */
void smart_gun_fire(void)
{
    short shots = SMART_GUN_BURST;
    ActorTarget *target;
    ActorTarget *nearest;
    short distance;
    short i;
    Particle *particle;
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR aim;
    SVECTOR turn;
    Actor *actor;
    u_int x;
    u_int z;

    collect_targets(SMART_GUN_FOV);
    if (g_target_count) {
        for (;;) {
            distance = SMART_GUN_RANGE;
            nearest = 0;
            target = g_targets;
            for (i = 0; i < g_target_count; i++, target++) {
                if (target->distance < distance) {
                    distance = target->distance;
                    nearest = target;
                }
            }
            if (nearest == 0)
                break;
            nearest->distance = SMART_GUN_RANGE;
            particle = particle_alloc();
            if (particle == 0)
                return;
            position = particle->position;
            velocity = particle->velocity;
            position->vx = g_view_position.vx;
            position->vy = g_view_position.vy - SMART_GUN_DROP;
            position->vz = g_view_position.vz;
            velocity->vx = 0;
            velocity->vy = 0;
            velocity->vz = SMART_GUN_MUZZLE;
            rotate_vector_offset(&g_view_rotation, velocity, position, position);
            actor = nearest->actor;
            aim.vz = point_distance_xz(position, &actor->position);
            aim.vx = position->vy - (actor->position.vy + (actor->scale.vy >> 1));
            aim.vx = point_heading_xz(&g_zero_vector, &aim) - ANGLE_180;
            aim.vz = 0;
            aim.vy = 0;
            turn.vx = 0;
            turn.vy = (g_player.angles.vy - nearest->angle) & ANGLE_MASK;
            turn.vz = g_view_rotation.vz;
            velocity->vx = 0;
            velocity->vy = 0;
            velocity->vz = g_player.movement.vz + SHOT_SPEED;
            rotate_vector(&aim, velocity, velocity);
            rotate_vector(&turn, velocity, velocity);
            particle->kind = PARTICLE_KIND_SMART_GUN_SHOT;
            particle->large = particle->size = SMART_GUN_SHOT_SIZE;
            particle->state = PARTICLE_DAMAGE_SMART_GUN;
            x = position->vx + g_level_map.offset_x;
            z = position->vz + g_level_map.offset_z;
            if (MAP_OUTSIDE(x, z)) {
                particle_free(particle);
                return;
            }
            particle->cell = MAP_CELL(x, z);
            if (particle_hit_wall(particle)) {
                particle_free(particle);
                return;
            }
            particle_link(particle);
            if (!CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO) && --g_player.smart_gun_ammo == 0)
                return;
            if (--shots == 0)
                return;
        }
    }
    while (shots-- > 0U) {
        fire_bullet(g_player.movement.vz + (shots << BURST_SPEED_SHIFT), PARTICLE_KIND_SMART_GUN_SHOT);
        if (!CHEAT_IS_ACTIVE(CHEAT_FLAG_UNLIMITED_AMMO) && --g_player.smart_gun_ammo == 0)
            return;
    }
}

int f_2603c(int value)
{
    return fire_bullet((short)value, PARTICLE_KIND_PISTOL_SHOT);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void pulse_grenade_launch(void)
{
    Particle *particle = particle_alloc();
    SVECTOR *position;
    SVECTOR *velocity;
    SVECTOR rotation;

    if (particle == 0)
        return;
    g_player.flash = PLAYER_FLASH_GRENADE;
    g_player.flash_heading = (g_view_rotation.vy + ANGLE_180) & ANGLE_MASK;
    position = particle->position;
    velocity = particle->velocity;
    rotation.vx = -g_view_pitch.vx & ANGLE_MASK;
    rotation.vy = 0;
    rotation.vz = 0;
    position->vx = g_view_position.vx + g_player.shot_offset_x;
    position->vy = g_view_position.vy - PULSE_GRENADE_DROP;
    position->vz = g_view_position.vz;
    velocity->vx = 0;
    velocity->vy = -GRENADE_LIFT;
    velocity->vz = g_player.movement.vz + PULSE_GRENADE_SPEED;
    if (velocity->vz > PULSE_GRENADE_SPEED)
        velocity->vz = PULSE_GRENADE_SPEED;
    rotate_vector(&rotation, velocity, velocity);
    rotate_vector(&g_view_rotation, velocity, velocity);
    particle->model = g_object_models[PICKUP_GRENADES];
    particle_launch(particle, 0, -g_view_rotation.vy & ANGLE_MASK, 0, PULSE_GRENADE_LIFE, PULSE_GRENADE_FUSE, PARTICLE_KIND_PULSE_GRENADE, PARTICLE_DAMAGE_GRENADE);
    sound_play(SOUND_PULSE_GRENADE, -1, SPU_VOLUME_MAX / 2);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void grenade_throw(void)
{
    Particle *particle = particle_alloc();

    if (particle != 0) {
        SVECTOR *position = particle->position;
        SVECTOR *velocity = particle->velocity;
        SVECTOR rotation;

        rotation.vx = -g_view_pitch.vx & ANGLE_MASK;
        rotation.vy = 0;
        rotation.vz = 0;
        position->vx = g_view_position.vx + g_player.shot_offset_x;
        position->vy = g_view_position.vy - HAND_GRENADE_DROP;
        position->vz = g_view_position.vz;
        velocity->vx = 0;
        velocity->vy = -GRENADE_LIFT;
        velocity->vz = g_player.movement.vz + HAND_GRENADE_SPEED;
        if (velocity->vz >= HAND_GRENADE_MAX_SPEED + 1)
            velocity->vz = HAND_GRENADE_MAX_SPEED;
        rotate_vector(&rotation, velocity, velocity);
        rotate_vector(&g_view_rotation, velocity, velocity);
        particle->model = g_object_models[PICKUP_SEISMIC_CHARGES];
        particle_launch(particle, 0, 0, 0, HAND_GRENADE_LIFE, HAND_GRENADE_FUSE, PARTICLE_KIND_GRENADE, PARTICLE_DAMAGE_GRENADE);
    }
}

/* Rule exception: `unused` is never read or written; the 8-byte local is required for
 * retail's 0x30 frame (particle_draw_steam and particle_draw_fire keep their colour in the same slot). */
/* name: runtime evidence, analysis/runtime/functions.json */
void particle_draw_flame(DVECTOR *screen, Particle *particle)
{
    POLY_FT4 *poly = g_sprite_workspace;
    SpriteUV *uv;
    short half;
    CVECTOR unused;

    if (particle->size < FLAME_SMALL_SIZE)
        half = ((FLAME_SIZE - particle->size) * FLAME_GROWTH_SMALL << FLAME_SCALE_SHIFT) / g_projected_depth;
    else
        half = ((FLAME_SIZE - particle->size) * FLAME_GROWTH << FLAME_SCALE_SHIFT) / g_projected_depth;
    uv = &g_explosion_uvs[random() & FLAME_FRAME_MASK];
    *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
    *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
    *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
    *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
    poly->x0 = screen->vx - half;
    poly->y0 = screen->vy - half;
    poly->x1 = half + screen->vx;
    poly->y1 = screen->vy - half;
    poly->x2 = screen->vx - half;
    poly->y2 = half + screen->vy;
    poly->x3 = half + screen->vx;
    poly->y3 = half + screen->vy;
    *(u_long *)&poly->r0 = *(u_long *)&g_palette->colors[0];
    if (particle->size < FLAME_ADDITIVE_SIZE)
        poly->tpage = GetTPage(0, 3, EXPLOSION_TPAGE_X, EXPLOSION_TPAGE_Y);
    else
        poly->tpage = GetTPage(0, 1, EXPLOSION_TPAGE_X, EXPLOSION_TPAGE_Y);
    setPolyFT4(poly);
    setSemiTrans(poly, 1);
    if (g_projected_depth > PARTICLE_FLAME_DEPTH_BIAS)
        g_projected_depth -= PARTICLE_FLAME_DEPTH_BIAS;
    else
        g_projected_depth = PARTICLE_FLAME_MIN_DEPTH;
    addPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), poly);
    poly++;
    g_sprite_workspace = poly;
}

/* name: static evidence, analysis/runtime/functions.json */
void particle_draw_steam(DVECTOR *screen, short size, short kind)
{
    POLY_FT4 *poly = g_sprite_workspace;
    SpriteUV *uv;
    short width;
    short height;
    CVECTOR color;

    if (size < 0)
        size = 0;
    if (size >= STEAM_SIZE_MAX)
        size = STEAM_SIZE_MAX - 1;
    uv = &g_explosion_uvs[g_steam_frames[(random() & STEAM_FRAME_MASK) + (STEAM_SIZE_MAX - size)]];
    *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
    *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
    *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
    *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
    poly->clut = g_explosion_cluts[2]->clut;
    if (kind == PARTICLE_KIND_STEAM) {
        width = g_steam_lengths[size] * STEAM_SCALE / g_projected_depth;
        height = g_steam_widths[size] * STEAM_SCALE / g_projected_depth;
    } else {
        height = g_steam_lengths[size] * STEAM_SCALE / g_projected_depth;
        width = g_steam_widths[size] * STEAM_SCALE / g_projected_depth;
    }
    width -= (random() & STEAM_JITTER_MASK) - STEAM_JITTER;
    height -= (random() & STEAM_JITTER_MASK) - STEAM_JITTER;
    poly->x0 = screen->vx - width;
    poly->x1 = width + screen->vx;
    poly->y0 = screen->vy - height;
    poly->y2 = height + screen->vy;
    poly->x2 = poly->x0;
    poly->x3 = poly->x1;
    poly->y3 = poly->y2;
    poly->y1 = poly->y0;
    if (g_game_flags & GAME_FLAG_FADE)
        color.b = color.g = color.r = g_palette->colors[0].r >> 1;
    else
        color.b = color.g = color.r = size + STEAM_BRIGHTNESS;
    g_transform_result = g_projected_depth;
    if (g_transform_result < ONE) {
        *(u_long *)&poly->r0 = *(u_long *)&color;
    } else {
        g_transform_result -= ONE;
        DpqColor(&color, g_transform_result, (CVECTOR *)&poly->r0);
    }
    setPolyFT4(poly);
    setSemiTrans(poly, 1);
    addPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), poly);
    poly++;
    g_sprite_workspace = poly;
}

/* name: static evidence, analysis/runtime/functions.json */
void particle_draw_fire(DVECTOR *screen, short size, short kind)
{
    POLY_FT4 *poly = g_sprite_workspace;
    SpriteUV *uv;
    short width;
    short height;
    CVECTOR color;

    if (size < 0)
        size = 0;
    if (size >= STEAM_SIZE_MAX)
        size = STEAM_SIZE_MAX - 1;
    uv = &g_explosion_uvs[g_fire_frames[(random() & STEAM_FRAME_MASK) + (STEAM_SIZE_MAX - size)]];
    *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
    *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
    *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
    *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
    if (kind == PARTICLE_KIND_FIRE) {
        width = g_fire_lengths[size] * FIRE_LENGTH_SCALE / g_projected_depth;
        height = g_fire_widths[size] * FIRE_WIDTH_SCALE / g_projected_depth;
    } else {
        height = g_fire_lengths[size] * FIRE_LENGTH_SCALE / g_projected_depth;
        width = g_fire_widths[size] * FIRE_VERTICAL_WIDTH_SCALE / g_projected_depth;
    }
    poly->x0 = screen->vx - width;
    poly->x1 = width + screen->vx;
    poly->y0 = screen->vy - height;
    poly->y2 = height + screen->vy;
    poly->x2 = poly->x0;
    poly->x3 = poly->x1;
    poly->y3 = poly->y2;
    poly->y1 = poly->y0;
    if (g_game_flags & GAME_FLAG_FADE) {
        color.r = g_palette->colors[0].r;
        color.g = g_palette->colors[0].g;
        color.b = g_palette->colors[0].b;
    } else {
        color.b = color.g = color.r = FIRE_BRIGHTNESS;
    }
    g_transform_result = g_projected_depth;
    if (g_transform_result < ONE) {
        *(u_long *)&poly->r0 = *(u_long *)&color;
    } else {
        g_transform_result -= ONE;
        DpqColor(&color, g_transform_result, (CVECTOR *)&poly->r0);
    }
    setPolyFT4(poly);
    if (size < FIRE_OPAQUE_SIZE)
        setSemiTrans(poly, 1);
    addPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), poly);
    poly++;
    g_sprite_workspace = poly;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void particle_draw_model(Particle *particle)
{
    VECTOR scale;

    if (particle->kind == PARTICLE_KIND_PISTOL_SHELL)
        scale.vx = scale.vy = scale.vz = ONE / 2;
    g_model_uvs = g_pickup_uvs;
    LoadAverageShort12(particle->position, &g_view_position, ONE, -ONE, &g_relative_position);
    PushMatrix();
    ApplyMatrix(&g_view_matrix, &g_relative_position, &g_translation);
    if ((u_long)(g_translation.vz - (DRAW_NEAR_DEPTH + 1)) < DRAW_FAR_DEPTH - DRAW_NEAR_DEPTH - 1 && g_translation.vx < DRAW_FAR_DEPTH) {
        RotMatrix(&particle->rotation, &g_matrix_work);
        MulMatrix2(&g_view_matrix, &g_matrix_work);
        if (particle->kind == PARTICLE_KIND_PISTOL_SHELL)
            ScaleMatrix(&g_matrix_work, &scale);
        SetRotMatrix(&g_matrix_work);
        TransMatrix(&g_matrix_work, &g_translation);
        SetTransMatrix(&g_matrix_work);
        model_draw_mid_in_cell(particle->model, particle->cell);
    }
    g_model_uvs = g_sprite_uvs;
    PopMatrix();
    if (particle->kind == PARTICLE_KIND_GRENADE)
        particle->rotation.vx = (particle->rotation.vx + GRENADE_SPIN) & ANGLE_MASK;
    if (particle->kind == PARTICLE_KIND_SHOTGUN_SHELL || particle->kind == PARTICLE_KIND_PISTOL_SHELL) {
        particle->rotation.vx = (particle->rotation.vx + SHELL_SPIN_X) & ANGLE_MASK;
        particle->rotation.vy = (particle->rotation.vy + SHELL_SPIN_Y) & ANGLE_MASK;
    }
}

/* Rule exception: `unused` is never read or written; the 8-byte local is required for
 * retail's 0x40 frame (as with level_object_task's unused scale). */
/* name: static evidence, analysis/runtime/functions.json */
void particle_draw_debris(Particle *particle)
{
    VECTOR scale;
    SVECTOR unused;
    DispatchRow *row;

    scale.vx = scale.vy = scale.vz = particle->size << DEBRIS_SCALE_SHIFT;
    LoadAverageShort12(particle->position, &g_view_position, ONE, -ONE, &g_relative_position);
    PushMatrix();
    ApplyMatrix(&g_view_matrix, &g_relative_position, &g_translation);
    if ((u_long)(g_translation.vz - (DRAW_NEAR_DEPTH + 1)) < PARTICLE_MODEL_FAR_DEPTH - DRAW_NEAR_DEPTH - 1 && g_translation.vx < PARTICLE_MODEL_FAR_DEPTH) {
        RotMatrix(&particle->rotation, &g_matrix_work);
        MulMatrix2(&g_view_matrix, &g_matrix_work);
        ScaleMatrix(&g_matrix_work, &scale);
        SetRotMatrix(&g_matrix_work);
        TransMatrix(&g_matrix_work, &g_translation);
        SetTransMatrix(&g_matrix_work);
        if (particle->kind == PARTICLE_KIND_SHARD) {
            row = particle->model;
            g_far_draw_table[ROW_SELECT_QUAD_NOCULL](row, &g_palette[row->flags & ROW_PALETTE_MASK],
                                      g_debris_uvs + row->count);
        } else if (particle->kind == PARTICLE_KIND_PANEL) {
            g_far_draw_table[ROW_SELECT_QUAD_SEMITRANS](particle->model, &g_palette[particle->cell->palette],
                                       g_debris_uvs + particle->model->count);
        }
    }
    PopMatrix();
    if (particle->kind != PARTICLE_KIND_PANEL)
        particle->rotation.vz = (particle->rotation.vz + DEBRIS_SPIN) & ANGLE_MASK;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void particle_draw_spark(DVECTOR *screen, Particle *particle)
{
    POLY_F4 *poly;
    short size;

    size = (particle->size << PARTICLE_SCALE_SHIFT) / g_projected_depth;
    poly = g_sprite_workspace;
    if (size == 0)
        size++;
    poly->x0 = screen->vx;
    poly->y0 = screen->vy - size;
    poly->x1 = size + screen->vx;
    poly->y1 = screen->vy;
    poly->x2 = screen->vx - size;
    poly->y2 = screen->vy;
    poly->x3 = screen->vx;
    poly->y3 = size + screen->vy;
    switch (particle->state) {
    case SPARK_BLUE:
        setRGB0(poly, 16, 64, 200);
        break;
    case SPARK_GREEN:
        setRGB0(poly, 32, 255, 0);
        break;
    default:
        setRGB0(poly, 255, 255, 0);
        break;
    }
    if (particle->size < SPARK_FADE_SIZE)
        DpqColor((CVECTOR *)&poly->r0, ONE - (particle->size << SPARK_FADE_SHIFT), (CVECTOR *)&poly->r0);
    setPolyF4(poly);
    poly++;
    AddPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), g_sprite_workspace);
    g_sprite_workspace = poly;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void particle_draw_blood(Particle *particle, DVECTOR *screen)
{
    POLY_FT4 *poly = g_sprite_workspace;
    SpriteUV *uv;
    short size;

    if (particle->large)
        size = (particle->size << BLOOD_LARGE_SCALE_SHIFT) / g_projected_depth;
    else
        size = (particle->size << PARTICLE_SCALE_SHIFT) / g_projected_depth;
    uv = &g_explosion_uvs[BLOOD_FRAME_FIRST + ((g_frame_count >> BLOOD_FRAME_RATE_SHIFT) & BLOOD_FRAME_MASK)];
    *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
    *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
    *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
    *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
    poly->x0 = poly->x3 = screen->vx;
    poly->y1 = poly->y2 = screen->vy;
    poly->y0 = screen->vy - size;
    poly->x1 = size + screen->vx;
    poly->x2 = screen->vx - size;
    poly->y3 = size + screen->vy;
    *(u_long *)&poly->r0 = *(u_long *)&g_palette->colors[0];
    if (particle->kind == PARTICLE_KIND_ACID_BLOOD) {
        poly->r0 = 0;
        poly->b0 = 0;
    } else if (particle->kind == PARTICLE_KIND_BLOOD) {
        poly->g0 = 0;
        poly->b0 = 0;
    }
    setPolyFT4(poly);
    setSemiTrans(poly, 1);
    poly++;
    AddPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), g_sprite_workspace);
    g_sprite_workspace = poly;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void particle_draw_tracer(Particle *particle, DVECTOR *screen)
{
    PaletteEntry *palette = g_palette;
    LINE_G2 *line = g_sprite_workspace;
    CVECTOR *tail = &palette[1].colors[0];
    SVECTOR end;

    switch (particle->kind) {
    case PARTICLE_KIND_PISTOL_SHOT:
        end.vx = particle->position->vx + (particle->velocity->vx >> 1);
        end.vy = particle->position->vy + (particle->velocity->vy >> 1);
        end.vz = particle->position->vz + (particle->velocity->vz >> 1);
        setRGB0(line, palette[1].colors[0].r, palette[1].colors[0].g, 0);
        break;
    case PARTICLE_KIND_SMART_GUN_SHOT:
        end.vx = particle->position->vx + particle->velocity->vx;
        end.vy = particle->position->vy + particle->velocity->vy;
        end.vz = particle->position->vz + particle->velocity->vz;
        setRGB0(line, 0, palette[1].colors[0].g, palette[1].colors[0].b);
        break;
    case PARTICLE_KIND_PULSE_RIFLE_SHOT:
        end.vx = particle->position->vx + particle->velocity->vx;
        end.vy = particle->position->vy + particle->velocity->vy;
        end.vz = particle->position->vz + particle->velocity->vz;
        setRGB0(line, 0, palette[0].colors[0].g, palette[1].colors[0].b);
        break;
    default:
        end.vx = particle->position->vx + particle->velocity->vx;
        end.vy = particle->position->vy + particle->velocity->vy;
        end.vz = particle->position->vz + particle->velocity->vz;
        setRGB0(line, palette[0].colors[0].r, 0, 0);
        break;
    }
    RotTransPers(&end, (long *)&end, &g_gte_interpolation, &g_gte_flag);
    line->x0 = screen->vx;
    line->y0 = screen->vy;
    line->x1 = end.vx;
    line->y1 = end.vy;
    setRGB1(line, tail->r, tail->g, tail->b);
    setLineG2(line);
    line++;
    AddPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), g_sprite_workspace);
    g_sprite_workspace = line;
    if (particle->kind == PARTICLE_KIND_SMART_GUN_SHOT) {
        line->x0 = screen->vx + 1;
        line->y0 = screen->vy + 1;
        line->x1 = end.vx + 1;
        line->y1 = end.vy + 1;
        setRGB0(line, 0, tail->g, tail->b);
        setRGB1(line, tail->r, tail->g, tail->b);
        setLineG2(line);
        line++;
        AddPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), g_sprite_workspace);
        g_sprite_workspace = line;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void particle_draw(Particle *particle)
{
    DVECTOR screen;

    switch (particle->kind) {
    case PARTICLE_KIND_SHOTGUN_SHOT:
        return;
    case PARTICLE_KIND_EXPLOSIVE:
    case PARTICLE_KIND_PULSE_GRENADE:
    case PARTICLE_KIND_ENEMY_GRENADE:
    case PARTICLE_KIND_GRENADE:
    case PARTICLE_KIND_SHOTGUN_SHELL:
    case PARTICLE_KIND_PISTOL_SHELL:
        particle_draw_model(particle);
        return;
    }
    g_projected_depth = RotTransPers(particle->position, (long *)&screen, &g_gte_interpolation, &g_gte_flag) << 2;
    if (g_projected_depth > PARTICLE_FAR_DEPTH)
        return;
    if (screen.vx < 0)
        return;
    if (screen.vx > SCREEN_WIDTH)
        return;
    if (screen.vy < 0)
        return;
    if (screen.vy > SCREEN_HEIGHT)
        return;
    switch (particle->kind) {
    case PARTICLE_KIND_STEAM:
    case PARTICLE_KIND_STEAM_VERTICAL:
        if (g_projected_depth >= DRAW_NEAR_DEPTH)
            particle_draw_steam(&screen, particle->size, particle->kind);
        break;
    case PARTICLE_KIND_FIRE:
    case PARTICLE_KIND_FIRE_VERTICAL:
        if (g_projected_depth >= DRAW_NEAR_DEPTH)
            particle_draw_fire(&screen, particle->size, particle->kind);
        break;
    case PARTICLE_KIND_SHARD:
    case PARTICLE_KIND_PANEL:
        if (g_projected_depth >= DRAW_NEAR_DEPTH)
            particle_draw_debris(particle);
        break;
    case PARTICLE_KIND_PISTOL_SHOT:
    case PARTICLE_KIND_PULSE_RIFLE_SHOT:
    case PARTICLE_KIND_SMART_GUN_SHOT:
        if (get_difficulty() != 2 && g_projected_depth >= DRAW_NEAR_DEPTH)
            particle_draw_tracer(particle, &screen);
        break;
    case PARTICLE_KIND_ENEMY_SHOT:
        if (get_difficulty() == DIFFICULTY_ACID_REIGN && g_projected_depth >= DRAW_NEAR_DEPTH)
            particle_draw_tracer(particle, &screen);
        break;
    case PARTICLE_KIND_FLAME:
        if (g_projected_depth >= PARTICLE_FLAME_NEAR_DEPTH)
            particle_draw_flame(&screen, particle);
        break;
    case PARTICLE_KIND_SPARK:
        if (g_projected_depth >= DRAW_NEAR_DEPTH)
            particle_draw_spark(&screen, particle);
        break;
    case PARTICLE_KIND_BLOOD:
    case PARTICLE_KIND_ACID_BLOOD:
    case PARTICLE_KIND_SYNTH_BLOOD:
        if (g_projected_depth >= DRAW_NEAR_DEPTH)
            particle_draw_blood(particle, &screen);
        break;
    }
}

/* name: invented; four explosions scattered round the particle and one at its centre */
static __inline__ void particle_explode(Particle *particle)
{
    short dx;
    short dy;

    dx = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    dy = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    explosion_spawn(particle->position->vx + dx, particle->position->vy + dy, particle->position->vz);
    dx = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    dy = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    explosion_spawn(particle->position->vx + dx, particle->position->vy + dy, particle->position->vz);
    dx = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    dy = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    explosion_spawn(particle->position->vx + dx, particle->position->vy + dy, particle->position->vz);
    dx = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    dy = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    explosion_spawn(particle->position->vx + dx, particle->position->vy + dy, particle->position->vz);
    explosion_spawn(particle->position->vx, particle->position->vy, particle->position->vz);
}

/* name: invented; like particle_explode, but the explosions rise above the particle */
static __inline__ void particle_explode_up(Particle *particle)
{
    short dx;

    dx = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    explosion_spawn(particle->position->vx + dx, particle->position->vy + ((random() & EXPLOSION_RISE_MASK) | EXPLOSION_RISE), particle->position->vz);
    dx = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    explosion_spawn(particle->position->vx + dx, particle->position->vy + ((random() & EXPLOSION_RISE_MASK) | EXPLOSION_RISE), particle->position->vz);
    dx = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    explosion_spawn(particle->position->vx + dx, particle->position->vy + ((random() & EXPLOSION_RISE_MASK) | EXPLOSION_RISE), particle->position->vz);
    dx = (random() & EXPLOSION_SCATTER_MASK) - EXPLOSION_SCATTER;
    explosion_spawn(particle->position->vx + dx, particle->position->vy + ((random() & EXPLOSION_RISE_MASK) | EXPLOSION_RISE), particle->position->vz);
    explosion_spawn(particle->position->vx, particle->position->vy + EXPLOSION_RISE, particle->position->vz);
}

/* name: invented; a random speed of about half the vertical speed v, either way */
#define SCATTER_SPEED(v) ({ int speed = random() & SCATTER_SPEED_MASK; speed += (v) / 2; if (random() & 1) speed = -speed; speed; })

/* Per-frame particle task: moves every live particle by its velocity (gteMIMefunc), finds its
 * map cell, ages it and runs its kind's behaviour, then draws it (particle_draw) or frees it. */
/* name: runtime evidence, analysis/runtime/functions.json */
void particles_task(TASK *task)
{
    Particle *particle = g_live_particles;
    Particle *next;
    u_int x;
    u_int z;
    short floor; /* also the countdown in size: one local for both (retail's register use) */
    short sample;

    if (particle == 0)
        goto done;
    gteMIMefunc(g_particle_positions, g_particle_velocities, PARTICLE_COUNT, ONE);
    for (; particle != 0; particle = next) {
        next = particle->next;
        x = particle->position->vx + g_level_map.offset_x;
        z = particle->position->vz + g_level_map.offset_z;
        if (MAP_OUTSIDE(x, z))
            goto release;
        particle->cell = MAP_CELL(x, z);
        floor = particle->size;
        if (--floor <= 0) {
            if (particle->kind == PARTICLE_KIND_EXPLOSIVE || particle->kind == PARTICLE_KIND_PULSE_GRENADE || particle->kind == PARTICLE_KIND_ENEMY_GRENADE || particle->kind == PARTICLE_KIND_GRENADE) {
                explosion_damage(particle->position, particle->cell);
                particle_explode(particle);
            }
            goto release;
        }
        particle->size = floor;
        switch (particle->kind) {
        case PARTICLE_KIND_PISTOL_SHOT:
        case PARTICLE_KIND_SHOTGUN_SHOT:
        case PARTICLE_KIND_PULSE_RIFLE_SHOT:
        case PARTICLE_KIND_FLAME:
        case PARTICLE_KIND_SMART_GUN_SHOT:
            if (particle_hit_wall(particle)) {
                if (particle->kind == PARTICLE_KIND_FLAME)
                    goto halt;
                shot_impact(particle);
                sample = (u_int)(g_level - LEVEL_211_LIVING_QUARTERS) < LEVEL_311_TUNNELS - LEVEL_211_LIVING_QUARTERS ? SOUND_SHOT_IMPACT_ALT : SOUND_SHOT_IMPACT;
                if ((u_char)(particle->cell->type - CELL_TYPE_LIQUID_SHALLOW) < CELL_TYPE_LIQUID_DEEP - CELL_TYPE_LIQUID_SHALLOW + 1)
                    sample = SOUND_SHOT_SPLASH;
                sound_play_at(sample, particle->position, SOUND_AT_SOFT);
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            if (particle_hit_actors(particle)) {
                if (particle->kind != PARTICLE_KIND_FLAME) {
                    particle_unlink(particle);
                    particle_free(particle);
                    continue;
                }
            halt:
                particle_step_back(particle);
                particle->velocity->vx = particle->velocity->vy = particle->velocity->vz = 0;
                particle_draw(particle);
                continue;
            }
            shot_cell_trigger(particle);
            particle_draw(particle);
            break;
        case PARTICLE_KIND_EXPLOSIVE:
        case PARTICLE_KIND_PULSE_GRENADE:
        case PARTICLE_KIND_GRENADE:
            if (particle_hit_floor_or_ceiling(particle)) {
                particle_explode(particle);
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            if (particle_hit_actors(particle)) {
                particle_step_back(particle);
                particle_explode(particle);
                explosion_damage(particle->position, particle->cell);
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            shot_cell_trigger(particle);
            floor = floor_height(x, z);
            if (particle->position->vy < floor + GRENADE_BOUNCE_HEIGHT) {
                shot_impact(particle);
                if (floor - particle->position->vy > GRENADE_STEP_LIMIT) {
                    particle_step_back(particle);
                    explosion_damage(particle->position, particle->cell);
                    particle_explode_up(particle);
                    particle_unlink(particle);
                    particle_free(particle);
                    continue;
                }
                particle->position->vy = floor + GRENADE_BOUNCE_HEIGHT;
                particle->velocity->vy = particle->life;
                if (particle->life >= GRENADE_BOUNCE_MIN) {
                    particle->life -= 2;
                } else {
                    particle->velocity->vx >>= 1;
                    particle->velocity->vz >>= 1;
                }
                particle_draw(particle);
                break;
            }
            particle->velocity->vy -= 1;
            particle_draw(particle);
            break;
        case PARTICLE_KIND_ENEMY_GRENADE:
            if (particle_hit_floor_or_ceiling(particle)) {
                explosion_spawn(particle->position->vx, particle->position->vy, particle->position->vz);
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            if (particle_hit_player(particle)) {
                particle_step_back(particle);
                explosion_damage(particle->position, particle->cell);
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            enemy_shot_cell_trigger(particle);
            floor = floor_height(x, z);
            if (particle->position->vy < floor + GRENADE_BOUNCE_HEIGHT) {
                shot_impact(particle);
                if (floor - particle->position->vy > GRENADE_STEP_LIMIT) {
                    particle_step_back(particle);
                    explosion_damage(particle->position, particle->cell);
                    explosion_spawn(particle->position->vx, particle->position->vy + EXPLOSION_RISE, particle->position->vz);
                    particle_unlink(particle);
                    particle_free(particle);
                    continue;
                }
                particle->position->vy = floor + GRENADE_BOUNCE_HEIGHT;
                particle->velocity->vy = particle->life;
                if (particle->life >= GRENADE_BOUNCE_MIN) {
                    particle->life -= 2;
                } else {
                    particle->velocity->vx >>= 1;
                    particle->velocity->vz >>= 1;
                }
                particle_draw(particle);
                break;
            }
            particle->velocity->vy -= 2;
            particle_draw(particle);
            break;
        case PARTICLE_KIND_ENEMY_SHOT:
            if (particle_hit_wall(particle)) {
                shot_impact(particle);
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            if (particle_hit_player(particle)) {
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            enemy_shot_cell_trigger(particle);
            particle_draw(particle);
            break;
        case PARTICLE_KIND_FIRE_VERTICAL:
            particle->size--;
        case PARTICLE_KIND_STEAM_VERTICAL:
            if (particle_hit_player(particle)) {
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            if (particle->velocity->vy > 0 && (particle->cell->ceiling - JET_PUFF_CEILING_GAP) << MAP_HEIGHT_SHIFT < particle->position->vy) {
                particle->position->vy = (particle->cell->ceiling - JET_PUFF_CEILING_GAP) << MAP_HEIGHT_SHIFT;
                particle->velocity->vx = SCATTER_SPEED(particle->velocity->vy);
                particle->velocity->vz = SCATTER_SPEED(particle->velocity->vy);
                particle->velocity->vy = -JET_PUFF_BOUNCE;
            } else if (particle->velocity->vy < 0 && particle->position->vy < (particle->cell->value + JET_PUFF_FLOOR_GAP) << MAP_HEIGHT_SHIFT) {
                particle->position->vy = (particle->cell->value + JET_PUFF_FLOOR_GAP) << MAP_HEIGHT_SHIFT;
                particle->velocity->vx = SCATTER_SPEED(particle->velocity->vy);
                particle->velocity->vz = SCATTER_SPEED(particle->velocity->vy);
                particle->velocity->vy = JET_PUFF_BOUNCE;
            }
            particle_draw(particle);
            break;
        case PARTICLE_KIND_FIRE:
            particle->size--;
            if (particle_hit_player(particle)) {
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            if (particle->size < FIRE_FALL_SIZE) {
                particle->velocity->vy -= 2;
                particle->velocity->vz -= FIRE_DRAG;
                if (particle->velocity->vz < 0)
                    particle->velocity->vz = 0;
            }
            particle_draw(particle);
            break;
        case PARTICLE_KIND_STEAM:
            if (particle_hit_player(particle)) {
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            if (particle->size < STEAM_RISE_SIZE)
                particle->velocity->vy += STEAM_RISE;
            particle_draw(particle);
            break;
        case PARTICLE_KIND_ACID_BLOOD:
        case PARTICLE_KIND_SYNTH_BLOOD:
            if (particle_hit_player(particle)) {
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            floor = floor_height(x, z);
            particle->velocity->vy -= PARTICLE_GRAVITY;
            if (particle->position->vy < floor) {
                blood_splash(particle);
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            particle_draw(particle);
            break;
        case PARTICLE_KIND_BLOOD:
            floor = floor_height(x, z);
            particle->velocity->vy -= PARTICLE_GRAVITY;
            if (particle->position->vy < floor) {
                blood_splash(particle);
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            particle_draw(particle);
            break;
        case PARTICLE_KIND_SHARD:
        case PARTICLE_KIND_PANEL:
            floor = floor_height(x, z) + DEBRIS_REST_HEIGHT;
            if (particle->velocity->vy == 0) {
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            particle->position->vy += particle->velocity->vy;
            if (particle->position->vy < floor) {
                particle->position->vy = floor;
                particle->life -= DEBRIS_BOUNCE_LOSS;
                if (particle->kind == PARTICLE_KIND_PANEL)
                    sound_play_at(SOUND_PANEL_LAND, particle->position, SOUND_AT_NEAR);
                if (particle->life < DEBRIS_BOUNCE_MIN)
                    particle->velocity->vy = 0;
                else
                    particle->velocity->vy = particle->life;
                particle->velocity->vx = 0;
                particle->velocity->vz = 0;
            } else if (particle->velocity->vy != PARTICLE_GRAVITY) {
                particle->velocity->vy -= PARTICLE_GRAVITY;
            } else {
                particle->velocity->vy = -2;
            }
            particle_draw(particle);
            break;
        case PARTICLE_KIND_SPARK:
            floor = floor_height(x, z);
            if (particle->velocity->vy == 0) {
                particle_unlink(particle);
                particle_free(particle);
                continue;
            }
            if (particle->cell->type == CELL_TYPE_LIQUID_DEEP && particle->position->vy < floor + SPARK_BLUE_SURFACE) {
                SET_VECTOR_TO_ZERO(*particle->velocity);
            } else if ((particle->cell->type == CELL_TYPE_LIQUID_SHALLOW && particle->position->vy < floor + SPARK_GREEN_SURFACE)
                       || particle->position->vy < floor) {
                SET_VECTOR_TO_ZERO(*particle->velocity);
            } else {
                particle->velocity->vy = particle->velocity->vy == PARTICLE_GRAVITY ? -2 : particle->velocity->vy - PARTICLE_GRAVITY;
            }
            particle_draw(particle);
            break;
        release:
            particle_unlink(particle);
            particle_free(particle);
            continue;
        case PARTICLE_KIND_SHOTGUN_SHELL:
        case PARTICLE_KIND_PISTOL_SHELL:
            particle->velocity->vy -= SHELL_GRAVITY;
            particle_draw(particle);
            break;
        default:
            particle->velocity->vy -= PARTICLE_GRAVITY;
            particle_draw(particle);
            break;
        }
    }
done:
    g_next_task = task->Next;
}
