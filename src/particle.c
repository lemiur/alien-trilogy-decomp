/* particle.c -- particles: the particle pool (128 Particles with their position and velocity
 * vectors, see <PARTICLE.H>), the weapon shots and the spawners that use it, the smart gun's target
 * list and the particle renderers */
/* Retail .text from 0x800246ac to at least 0x80027a18. One translation unit: its statics (.sbss
 * 0x8008d128..0x8008d138) are read from Particle_InitPool (0x800246c4) to the per-frame particle
 * task at 0x80027a18, so the code between is one module; the .rdata of its jump tables
 * (0x80011a54..) follows the function order. Name invented. */
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
#include <TYPES.H>

/* Module statics (.sbss 0x8008d128..0x8008d138), in their retail order. */
/* name: inferred from use: Particle_PoolAlloc allocates the PARTICLE_COUNT records into it */
static particle_t *g_particlePool;
/* name: inferred from use: Particle_Free pushes and Particle_Alloc pops it */
static particle_t *g_particleFree;
/* name: inferred from use: Particle_Link pushes and Particle_Unlink removes live particles */
static particle_t *g_liveParticles;

/* Explosion sprite sizes by remaining size (Particle_DrawSteam: width, height) and UV picks
 * (four per size step, chosen at random); Particle_DrawFire's second set. names: invented */
/* name: analysis/runtime/globals.json */
static int g_steamLengths[] = { 10, 10, 8, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 5, 5,
                                5,  5,  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 8, 8, 8, 8 };
/* name: analysis/runtime/globals.json */
static int g_steamWidths[] = { 5, 5, 5, 5, 5, 6, 6, 6, 6, 5, 5, 4, 4, 4, 4, 3,
                               3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2 };
/* name: analysis/runtime/globals.json */
static short g_steamFrames[] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5,
                                 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8 };
/* name: analysis/runtime/globals.json */
static int g_fireLengths[] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
                               5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8 };
/* name: analysis/runtime/globals.json */
static int g_fireWidths[] = { 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6,
                              5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3 };
/* name: analysis/runtime/globals.json */
static short g_fireFrames[] = { 2, 2, 2, 2, 4, 3, 4, 3, 3, 4, 3, 4, 4, 4, 4, 4, 3, 4,
                                4, 5, 4, 4, 5, 4, 4, 4, 5, 5, 5, 6, 5, 6, 6, 7, 7, 7 };

/*
==================
Particle_Free
==================
*/
void Particle_Free( particle_t *particle )
{
    particle->next = g_particleFree;
    g_particleFree = particle;
}

/*
==================
Particle_InitPool
==================
*/
/* name: inferred from its body, which gives every pool particle its vectors and frees it */
void Particle_InitPool( void )
{
    particle_t *particle;
    SVECTOR    *position;
    SVECTOR    *velocity;
    int         i;

    g_particleFree = 0;
    g_liveParticles = 0;
    velocity = g_particleVelocities;
    particle = g_particlePool;
    position = g_particlePositions;
    i = 0;
    do {
        particle->position = position;
        particle->velocity = velocity;
        particle->life = 0;
        particle->size = 0;
        particle->kind = 0;
        Particle_Free( particle );
        ++i;
        ++particle;
        velocity++;
        position++;
    } while ( i < PARTICLE_COUNT );
}

/*
==================
Particle_PoolAlloc
==================
*/
/* name: inferred from its body, which allocates the PARTICLE_COUNT pool records */
void Particle_PoolAlloc( void )
{
    g_particlePool = (particle_t *)Heap_Alloc( PARTICLE_COUNT * sizeof( particle_t ) );
}

/*
==================
Particle_Alloc
==================
*/
particle_t *Particle_Alloc( void )
{
    particle_t *first;

    if ( ( first = g_particleFree ) )
        g_particleFree = first->next;

    return first;
}

/*
==================
Particle_Link
==================
*/
void Particle_Link( particle_t *particle )
{
    particle_t *old = g_liveParticles;
    if ( old )
        old->prev = particle;
    g_liveParticles = particle;
    particle->next = old;
    particle->prev = 0;
}

/*
==================
Particle_Unlink
==================
*/
/* name: inferred from its body, which unlinks a particle from the live list, freeing its debris panel */
void Particle_Unlink( particle_t *particle )
{
    particle_t *next;
    particle_t *previous;
    if ( particle->kind == PARTICLE_KIND_SHARD || particle->kind == PARTICLE_KIND_PANEL )
        Debris_FreePanel( particle );
    next = particle->next;
    previous = particle->prev;
    if ( next != 0 )
        next->prev = previous;
    if ( previous != 0 )
        previous->next = next;
    else
        g_liveParticles = next;
    particle->prev = 0;
    particle->next = 0;
}

/*
==================
Particle_SpawnExplosion
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_SpawnExplosion( int x, int y, int z )
{
    SVECTOR position;
    TASK   *node;
    position.vx = (s16)x;
    position.vy = (s16)y;
    position.vz = (s16)z;
    Snd_PlayAt( SOUND_EXPLOSION, &position, SOUND_AT_NEAR );
    node = TaskCreate( TASK_PRIORITY_ACTORS, Fx_ExplosionTask );
    if ( node != 0 ) {
        node->state[0] = (s16)x;
        node->state[1] = (s16)y;
        node->state[2] = (s16)z;
        node->state[EFFECT_STATE_FRAME] = 0;
    }
}

/*
==================
Particle_SpawnBigExplosion
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Particle_SpawnBigExplosion( int x, int y, int z )
{
    SVECTOR position;
    TASK   *node;

    position.vx = (s16)x;
    position.vy = (s16)y;
    position.vz = (s16)z;
    Snd_PlayAt( SOUND_BIG_EXPLOSION, &position, SOUND_AT_NEAR );
    node = TaskCreate( TASK_PRIORITY_ACTORS, Fx_BigExplosionTask );
    if ( node != 0 ) {
        node->state[0] = (s16)x;
        node->state[1] = (s16)y;
        node->state[2] = (s16)z;
        node->state[EFFECT_STATE_FRAME] = 0;
        g_gameFlags |= GAME_FLAG_FLASH;
    }
}

/*
==================
Particle_ImpactSpray
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_ImpactSpray( particle_t *source, short selection )
{
    SVECTOR    *origin = source->position;
    u_int       count;
    int         speed;
    particle_t *particle;
    SVECTOR    *position;
    SVECTOR    *velocity;
    SVECTOR     rotation;

    switch ( source->kind ) {
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
    if ( selection == SPARK_YELLOW )
        speed = SPARK_SPEED;
    else
        speed = SPARK_SPEED_COLOURED;
    while ( count-- > 0 ) {
        particle = Particle_Alloc();
        if ( particle == 0 )
            break;
        rotation.vx = 0;
        rotation.vy = Sys_Random() & ANGLE_MASK;
        rotation.vz = 0;
        particle->size = SPARK_SIZE;
        particle->kind = PARTICLE_KIND_SPARK;
        particle->state = selection;
        position = particle->position;
        copyVector( position, origin );
        particle->life = ( Sys_Random() & SPARK_LIFE_MASK ) + SPARK_LIFE;
        velocity = particle->velocity;
        velocity->vx = 0;
        velocity->vy = ( Sys_Random() & SPARK_LIFT_MASK ) + SPARK_LIFT;
        velocity->vz = speed + ( Sys_Random() & SPARK_SPEED_MASK );
        PushMatrix();
        Geo_RotateVector( &rotation, velocity, velocity );
        PopMatrix();
        Particle_Link( particle );
    }
}

/*
==================
Particle_ShotImpact
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_ShotImpact( particle_t *particle )
{
    u8 type = particle->cell->type;
    if ( type == CELL_TYPE_LIQUID_DEEP )
        Particle_ImpactSpray( particle, SPARK_BLUE );
    else if ( type == CELL_TYPE_LIQUID_SHALLOW )
        Particle_ImpactSpray( particle, SPARK_GREEN );
    else
        Particle_ImpactSpray( particle, SPARK_YELLOW );
}

/*
==================
Particle_BloodSplash
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_BloodSplash( particle_t *source )
{
    TASK *node;
    if ( source->kind == PARTICLE_KIND_ACID_BLOOD )
        node = TaskCreate( TASK_PRIORITY_ACTORS, Fx_AcidSplashTask );
    else if ( source->kind == PARTICLE_KIND_BLOOD )
        node = TaskCreate( TASK_PRIORITY_ACTORS, Fx_BloodSplashTask );
    else
        node = TaskCreate( TASK_PRIORITY_ACTORS, Fx_SynthSplashTask );
    if ( node != 0 ) {
        node->state[0] = source->position->vx;
        node->state[1] = source->position->vy;
        node->state[2] = source->position->vz;
        node->state[EFFECT_STATE_FRAME] = 0;
    }
}

/*
==================
Particle_BloodSpray
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_BloodSpray( particle_t *source, int kind )
{
    SVECTOR    *origin = source->position;
    u_int       count;
    int         large;
    particle_t *particle;
    SVECTOR    *position;
    SVECTOR    *velocity;
    SVECTOR     rotation;

    if ( source->kind == PARTICLE_KIND_EXPLOSIVE || source->kind == PARTICLE_KIND_PULSE_GRENADE || source->kind == PARTICLE_KIND_GRENADE ) {
        count = BLOOD_DROPS_BURST;
        large = 1;
    } else {
        count = BLOOD_DROPS;
        large = 0;
    }
    while ( count-- > 0 ) {
        particle = Particle_Alloc();
        if ( particle == 0 )
            break;
        rotation.vx = 0;
        rotation.vy = Sys_Random() & ANGLE_MASK;
        rotation.vz = 0;
        particle->size = ( Sys_Random() & BLOOD_SIZE_MASK ) + BLOOD_SIZE;
        particle->kind = kind;
        particle->state = 1;
        particle->large = large;
        position = particle->position;
        copyVector( position, origin );
        particle->life = ( Sys_Random() & BLOOD_LIFE_MASK ) + BLOOD_LIFE;
        velocity = particle->velocity;
        velocity->vx = 0;
        velocity->vy = ( Sys_Random() & BLOOD_LIFT_MASK ) + BLOOD_LIFT;
        velocity->vz = ( Sys_Random() & BLOOD_SPEED_MASK ) + BLOOD_SPEED;
        PushMatrix();
        Geo_RotateVector( &rotation, velocity, velocity );
        PopMatrix();
        Particle_Link( particle );
    }
}

/*
==================
Particle_ActorBleed
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_ActorBleed( actor_t *actor, particle_t *source )
{
    switch ( actor->kind ) {
    case ACTOR_KIND_EGG:
    case ACTOR_KIND_FACEHUGGER:
    case ACTOR_KIND_CHESTBURSTER:
    case ACTOR_KIND_BAMBI:
    case ACTOR_KIND_DOG:
    case ACTOR_KIND_WARRIOR:
    case ACTOR_KIND_BOSS:
    case ACTOR_KIND_WARRIOR_CEILING:
    case ACTOR_KIND_DOG_CEILING:
        Particle_BloodSpray( source, PARTICLE_KIND_ACID_BLOOD );
        break;
    case ACTOR_KIND_COLONIST:
    case ACTOR_KIND_GUARD:
    case ACTOR_KIND_SOLDIER:
    case ACTOR_KIND_HANDLER:
        Particle_BloodSpray( source, PARTICLE_KIND_BLOOD );
        break;
    case ACTOR_KIND_SYNTHETIC:
        Particle_BloodSpray( source, PARTICLE_KIND_SYNTH_BLOOD );
        break;
    default:
        break;
    }
}

/*
==================
Particle_ActorFireBullet
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_ActorFireBullet( actor_t *actor )
{
    particle_t *particle = Particle_Alloc();
    SVECTOR    *position;
    SVECTOR    *velocity;
    SVECTOR     aim;
    SVECTOR     turn;

    if ( particle == 0 )
        return;
    PushMatrix();
    velocity = particle->velocity;
    position = particle->position;
    velocity->vx = 0;
    velocity->vy = 0;
    velocity->vz = actor->shotSpeed + ENEMY_SHOT_SPEED;
    switch ( actor->kind ) {
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
    aim.vz = Geo_PointDistanceXZ( position, &g_player.position );
    aim.vx = (short)( position->vy - ENEMY_SHOT_AIM_DROP ) - g_player.position.vy;
    aim.vx = Geo_PointHeadingXZ( &g_zeroVector, &aim ) - ANGLE_180;
    aim.vy = 0;
    aim.vz = 0;
    turn.vx = 0;
    turn.vy = ( actor->angle + ANGLE_180 ) & ANGLE_MASK;
    turn.vz = ANGLE_180;
    Geo_RotateVector( &aim, velocity, velocity );
    Geo_RotateVector( &turn, velocity, velocity );
    PopMatrix();
    particle->size = ENEMY_SHOT_SIZE;
    particle->kind = PARTICLE_KIND_ENEMY_SHOT;
    particle->state = 1;
    particle->rotation.vx = 0;
    particle->rotation.vy = turn.vy;
    particle->rotation.vz = 0;
    Particle_Link( particle );
}

/*
==================
Particle_JetSpray
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
particle_t *Particle_JetSpray( SVECTOR *origin, short speed, short heading, short kind )
{
    particle_t *particle = Particle_Alloc();
    SVECTOR    *position;
    SVECTOR    *velocity;
    SVECTOR     rotation;
    short       direction;

    if ( particle == 0 )
        return 0;
    direction = kind >= 0 ? 1 : -1;
    position = particle->position;
    velocity = particle->velocity;
    rotation.vx = 0;
    rotation.vy = heading & ANGLE_MASK;
    rotation.vz = 0;
    copyVector( position, origin );
    kind = ABS( kind );
    switch ( kind ) {
    case PARTICLE_KIND_FIRE_VERTICAL:
    case PARTICLE_KIND_STEAM_VERTICAL:
        velocity->vx = 0;
        if ( direction == -1 )
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
    Geo_RotateVector( &rotation, velocity, velocity );
    PopMatrix();
    particle->large = particle->size;
    particle->kind = kind;
    Particle_Link( particle );
    return particle;
}

/*
==================
Particle_ActorLobGrenade
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Particle_ActorLobGrenade( actor_t *actor )
{
    particle_t *particle = Particle_Alloc();
    SVECTOR    *position;
    SVECTOR    *velocity;
    SVECTOR     rotation;

    if ( particle == 0 )
        return;
    position = particle->position;
    velocity = particle->velocity;
    position->vx = actor->position.vx;
    position->vy = actor->position.vy + ENEMY_GRENADE_HEIGHT;
    position->vz = actor->position.vz;
    velocity->vx = 0;
    velocity->vy = 0;
    velocity->vz = actor->shotSpeed + ENEMY_SHOT_SPEED;
    if ( g_player.position.vy > actor->position.vy )
        rotation.vx = ( ANGLE_90 / 2 );
    else if ( g_player.position.vy < actor->position.vy )
        rotation.vx = -( ANGLE_90 / 2 );
    else
        rotation.vx = 0;
    rotation.vy = 0;
    rotation.vz = 0;
    PushMatrix();
    Geo_RotateVector( &rotation, velocity, velocity );
    rotation.vx = 0;
    rotation.vy = ( actor->angle + ANGLE_180 ) & ANGLE_MASK;
    Geo_RotateVector( &rotation, velocity, velocity );
    PopMatrix();
    velocity->vy = ENEMY_GRENADE_LIFT;
    particle->model = g_objectModels[PICKUP_GRENADES];
    Particle_Launch( particle, 0, -actor->angle & ANGLE_MASK, 0, ENEMY_GRENADE_LIFE, ENEMY_GRENADE_FUSE,
                     PARTICLE_KIND_ENEMY_GRENADE, PARTICLE_DAMAGE_ENEMY_GRENADE );
}

/*
==================
Particle_EjectShell
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_EjectShell( short weapon )
{
    particle_t    *particle = Particle_Alloc();
    SVECTOR       *position;
    SVECTOR       *velocity;
    SVECTOR        offset;
    SVECTOR        rotation;
    dispatchRow_t *model;
    int            kind;

    if ( particle == 0 )
        return;
    position = particle->position;
    velocity = particle->velocity;
    rotation.vx = -g_viewPitch.vx & ANGLE_MASK;
    rotation.vy = 0;
    rotation.vz = 0;
    copyVector( position, &g_viewPosition );
    offset.vx = 0;
    offset.vy = SHELL_EJECT_OFFSET_Y;
    offset.vz = SHELL_EJECT_OFFSET_Z;
    Geo_RotateVector( &rotation, &offset, &offset );
    rotation.vx = 0;
    rotation.vy = ( g_viewRotation.vy + SHELL_EJECT_ANGLE ) & ANGLE_MASK;
    rotation.vz = g_viewRotation.vz;
    Geo_RotateVectorOffset( &rotation, &offset, position, position );
    rotation.vx = -g_viewPitch.vx & ANGLE_MASK;
    rotation.vy = 0;
    rotation.vz = 0;
    velocity->vx = 0;
    velocity->vy = -SHELL_EJECT_LIFT;
    velocity->vz = g_player.movement.vz + SHELL_EJECT_SPEED;
    Geo_RotateVector( &rotation, velocity, velocity );
    rotation.vx = 0;
    rotation.vy = ( g_viewRotation.vy + (short)( ( Sys_Random() & SHELL_EJECT_HEADING_MASK ) + SHELL_EJECT_HEADING ) ) & ANGLE_MASK;
    rotation.vz = g_viewRotation.vz;
    Geo_RotateVector( &rotation, velocity, velocity );
    if ( weapon == WEAPON_PISTOL ) {
        model = g_objectModels[PICKUP_PISTOL_SHELL];
        kind = PARTICLE_KIND_PISTOL_SHELL;
    } else {
        model = g_objectModels[PICKUP_SHOTGUN_SHELL];
        kind = PARTICLE_KIND_SHOTGUN_SHELL;
    }
    particle->kind = kind;
    particle->model = model;
    particle->rotation.vx = Sys_Random() & ANGLE_MASK;
    particle->rotation.vy = Sys_Random() & ANGLE_MASK;
    particle->rotation.vz = 0;
    particle->size = SHELL_SIZE;
    Particle_Link( particle );
}

/*
==================
Particle_FireBullet
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Particle_FireBullet( short speed, u_char kind )
{
    particle_t *particle = Particle_Alloc();
    SVECTOR    *position;
    SVECTOR    *velocity;
    SVECTOR     rotation;
    u_int       x;
    u_int       z;

    if ( particle == 0 )
        return 0;
    particle->kind = kind;
    position = particle->position;
    velocity = particle->velocity;
    position->vx = g_viewPosition.vx;
    position->vy = g_viewPosition.vy - SHOT_DROP;
    position->vz = g_viewPosition.vz;
    switch ( particle->kind ) {
    case PARTICLE_KIND_PISTOL_SHOT:
        particle->size = PISTOL_SHOT_SIZE;
        particle->state = PARTICLE_DAMAGE_PISTOL;
        rotation.vy = PISTOL_SHOT_ANGLE;
        velocity->vx = PISTOL_MUZZLE_X;
        velocity->vy = 0;
        velocity->vz = 0;
        Geo_RotateVectorOffset( &g_viewRotation, velocity, position, position );
        break;
    case PARTICLE_KIND_SHOTGUN_SHOT:
        particle->size = SHOTGUN_SHOT_SIZE;
        particle->state = PARTICLE_DAMAGE_SHOTGUN;
        rotation.vy = PISTOL_SHOT_ANGLE;
        velocity->vx = RIFLE_MUZZLE_X;
        velocity->vy = 0;
        velocity->vz = 0;
        Geo_RotateVectorOffset( &g_viewRotation, velocity, position, position );
        break;
    case PARTICLE_KIND_PULSE_RIFLE_SHOT:
        particle->size = PULSE_RIFLE_SHOT_SIZE;
        particle->state = PARTICLE_DAMAGE_PULSE_RIFLE;
        rotation.vy = PULSE_RIFLE_SHOT_ANGLE;
        velocity->vx = RIFLE_MUZZLE_X;
        velocity->vy = 0;
        velocity->vz = 0;
        Geo_RotateVectorOffset( &g_viewRotation, velocity, position, position );
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
    rotation.vx = -g_viewPitch.vx & ANGLE_MASK;
    rotation.vz = 0;
    velocity->vx = 0;
    velocity->vy = 0;
    velocity->vz = speed + SHOT_SPEED;
    Geo_RotateVector( &rotation, velocity, velocity );
    Geo_RotateVector( &g_viewRotation, velocity, velocity );
    x = position->vx + g_levelMap.offsetX;
    z = position->vz + g_levelMap.offsetZ;
    if ( MAP_OUTSIDE( x, z ) ) {
        Particle_Free( particle );
        return 0;
    }
    particle->cell = MAP_CELL( x, z );
    if ( Particle_HitWall( particle ) ) {
        Particle_Free( particle );
        return -1;
    }
    position->vx += velocity->vx;
    position->vy += velocity->vy;
    position->vz += velocity->vz;
    x = position->vx + g_levelMap.offsetX;
    z = position->vz + g_levelMap.offsetZ;
    if ( MAP_OUTSIDE( x, z ) ) {
        Particle_Free( particle );
        return 0;
    }
    particle->cell = MAP_CELL( x, z );
    if ( Particle_HitWall( particle ) ) {
        Particle_Free( particle );
        return -1;
    }
    Particle_Link( particle );
    return -1;
}

/*
==================
Particle_FirePistol
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_FirePistol( void )
{
    if ( Particle_FireBullet( g_player.movement.vz, PARTICLE_KIND_PISTOL_SHOT ) == 0 )
        return;
    Particle_EjectShell( WEAPON_PISTOL );
    if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_UNLIMITED_AMMO ) )
        return;
    g_player.pistolAmmo--;
}
/*
==================
Particle_FireShotgun
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_FireShotgun( void )
{
    if ( Particle_FireBullet( g_player.movement.vz, PARTICLE_KIND_SHOTGUN_SHOT ) == 0 )
        return;
    Particle_EjectShell( WEAPON_SHOTGUN );
    g_player.flash = PLAYER_FLASH_SHOTGUN;
    g_player.flashHeading = ( (u_short)g_viewRotation.vy + ANGLE_180 ) & ANGLE_MASK;
    if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_UNLIMITED_AMMO ) )
        return;
    g_player.shotgunAmmo--;
}

/*
==================
Particle_FirePulseRifle
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_FirePulseRifle( void )
{
    s16 i = 0;
    while ( i < PULSE_RIFLE_BURST ) {
        int offset = ( i << HALFWORD_SHIFT ) >> ( HALFWORD_SHIFT - BURST_SPEED_SHIFT );
        if ( !Particle_FireBullet( g_player.movement.vz + offset, PARTICLE_KIND_PULSE_RIFLE_SHOT ) )
            return;
        if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_UNLIMITED_AMMO ) ) {
            s16 remaining = (s16)g_player.pulseRifleAmmo - 1;
            g_player.pulseRifleAmmo = (u16)remaining;
            if ( remaining == 0 )
                break;
        }
        i++;
    }
}

/*
==================
Particle_LaunchFlame

Particle_LaunchFlame launches the charged shot particle and steps it `charge` times
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Particle_LaunchFlame( short charge )
{
    short       steps = charge;
    particle_t *particle = Particle_Alloc();
    SVECTOR    *position;
    SVECTOR    *velocity;
    SVECTOR     rotation;
    u_int       x;
    u_int       z;

    if ( particle == 0 )
        return 0;
    position = particle->position;
    velocity = particle->velocity;
    position->vx = g_viewPosition.vx;
    position->vy = g_viewPosition.vy - FLAME_DROP + ( Sys_Random() & FLAME_DROP_MASK );
    position->vz = g_viewPosition.vz;
    particle->kind = PARTICLE_KIND_FLAME;
    particle->size = FLAME_SIZE - steps;
    particle->large = particle->size;
    particle->state = PARTICLE_DAMAGE_FLAME;
    velocity->vx = FLAME_MUZZLE_X;
    velocity->vy = 0;
    velocity->vz = 0;
    Geo_RotateVectorOffset( &g_viewRotation, velocity, position, position );
    rotation.vx = -g_viewPitch.vx & ANGLE_MASK;
    rotation.vy = FLAME_ANGLE;
    rotation.vz = 0;
    velocity->vx = 0;
    velocity->vy = 0;
    velocity->vz = g_player.movement.vz + FLAME_SPEED + ( Sys_Random() & FLAME_SPEED_MASK );
    Geo_RotateVector( &rotation, velocity, velocity );
    Geo_RotateVector( &g_viewRotation, velocity, velocity );
    if ( charge != 0 ) {
        do {
            position->vx += velocity->vx;
            position->vy += velocity->vy;
            position->vz += velocity->vz;
            x = position->vx + g_levelMap.offsetX;
            z = position->vz + g_levelMap.offsetZ;
            if ( MAP_OUTSIDE( x, z ) ) {
                Particle_Free( particle );
                return 0;
            }
            particle->cell = MAP_CELL( x, z );
            if ( Particle_HitWall( particle ) ) {
                SET_VECTOR_TO_ZERO( *velocity );
                break;
            }
        } while ( --steps );
    }
    Particle_Link( particle );
    return -1;
}

/*
==================
Particle_FireFlamethrower
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_FireFlamethrower( void )
{
    s16 i;
    if ( Particle_LaunchFlame( 1 ) == 0 )
        return;
    if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_UNLIMITED_AMMO ) )
        g_player.flamethrowerFuel--;
    i = 2;
    while ( i < FLAME_BURST ) {
        if ( Particle_LaunchFlame( i ) == 0 )
            return;
        i++;
    }
}

/*
==================
Particle_CollectTargets
==================
*/
/* name: inferred from its body: live actors within fov of the player's yaw, with angle and distance */
void Particle_CollectTargets( s16 fov )
{
    actor_t       *actor = g_liveActors;
    actorTarget_t *target;
    s16            delta;

    g_targetCount = 0;
    target = g_targets;
    for ( ; actor; actor = actor->next ) {
        if ( actor->kind < ACTOR_KIND_INERT && actor->action < ACTOR_HURT && ( actor->cell->mask & g_visibleCellMask )
             && actor->playerDistance <= SMART_GUN_RANGE ) {
            delta = ( (u16)( g_player.angles.vy - actor->playerAngle ) << ANGLE_SIGN_SHIFT ) >> ANGLE_SIGN_SHIFT;
            if ( (s16)ABS( delta ) <= fov ) {
                target->actor = actor;
                target->angle = delta;
                target->distance = actor->playerDistance;
                target++;
                g_targetCount++;
            }
        }
    }
}

/*
==================
Particle_FireSmartGun

Particle_FireSmartGun fires the smart gun: up to three shots, each guided at the nearest
target Particle_CollectTargets finds, else straight ahead
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_FireSmartGun( void )
{
    short          shots = SMART_GUN_BURST;
    actorTarget_t *target;
    actorTarget_t *nearest;
    short          distance;
    short          i;
    particle_t    *particle;
    SVECTOR       *position;
    SVECTOR       *velocity;
    SVECTOR        aim;
    SVECTOR        turn;
    actor_t       *actor;
    u_int          x;
    u_int          z;

    Particle_CollectTargets( SMART_GUN_FOV );
    if ( g_targetCount ) {
        for ( ;; ) {
            distance = SMART_GUN_RANGE;
            nearest = 0;
            target = g_targets;
            for ( i = 0; i < g_targetCount; i++, target++ ) {
                if ( target->distance < distance ) {
                    distance = target->distance;
                    nearest = target;
                }
            }
            if ( nearest == 0 )
                break;
            nearest->distance = SMART_GUN_RANGE;
            particle = Particle_Alloc();
            if ( particle == 0 )
                return;
            position = particle->position;
            velocity = particle->velocity;
            position->vx = g_viewPosition.vx;
            position->vy = g_viewPosition.vy - SMART_GUN_DROP;
            position->vz = g_viewPosition.vz;
            velocity->vx = 0;
            velocity->vy = 0;
            velocity->vz = SMART_GUN_MUZZLE;
            Geo_RotateVectorOffset( &g_viewRotation, velocity, position, position );
            actor = nearest->actor;
            aim.vz = Geo_PointDistanceXZ( position, &actor->position );
            aim.vx = position->vy - ( actor->position.vy + ( actor->scale.vy >> 1 ) );
            aim.vx = Geo_PointHeadingXZ( &g_zeroVector, &aim ) - ANGLE_180;
            aim.vz = 0;
            aim.vy = 0;
            turn.vx = 0;
            turn.vy = ( g_player.angles.vy - nearest->angle ) & ANGLE_MASK;
            turn.vz = g_viewRotation.vz;
            velocity->vx = 0;
            velocity->vy = 0;
            velocity->vz = g_player.movement.vz + SHOT_SPEED;
            Geo_RotateVector( &aim, velocity, velocity );
            Geo_RotateVector( &turn, velocity, velocity );
            particle->kind = PARTICLE_KIND_SMART_GUN_SHOT;
            particle->large = particle->size = SMART_GUN_SHOT_SIZE;
            particle->state = PARTICLE_DAMAGE_SMART_GUN;
            x = position->vx + g_levelMap.offsetX;
            z = position->vz + g_levelMap.offsetZ;
            if ( MAP_OUTSIDE( x, z ) ) {
                Particle_Free( particle );
                return;
            }
            particle->cell = MAP_CELL( x, z );
            if ( Particle_HitWall( particle ) ) {
                Particle_Free( particle );
                return;
            }
            Particle_Link( particle );
            if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_UNLIMITED_AMMO ) && --g_player.smartGunAmmo == 0 )
                return;
            if ( --shots == 0 )
                return;
        }
    }
    while ( shots-- > 0U ) {
        Particle_FireBullet( g_player.movement.vz + ( shots << BURST_SPEED_SHIFT ), PARTICLE_KIND_SMART_GUN_SHOT );
        if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_UNLIMITED_AMMO ) && --g_player.smartGunAmmo == 0 )
            return;
    }
}

/*
==================
f_2603c
==================
*/
int f_2603c( int value )
{
    return Particle_FireBullet( (short)value, PARTICLE_KIND_PISTOL_SHOT );
}

/*
==================
Particle_LaunchPulseGrenade
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_LaunchPulseGrenade( void )
{
    particle_t *particle = Particle_Alloc();
    SVECTOR    *position;
    SVECTOR    *velocity;
    SVECTOR     rotation;

    if ( particle == 0 )
        return;
    g_player.flash = PLAYER_FLASH_GRENADE;
    g_player.flashHeading = ( g_viewRotation.vy + ANGLE_180 ) & ANGLE_MASK;
    position = particle->position;
    velocity = particle->velocity;
    rotation.vx = -g_viewPitch.vx & ANGLE_MASK;
    rotation.vy = 0;
    rotation.vz = 0;
    position->vx = g_viewPosition.vx + g_player.shotOffsetX;
    position->vy = g_viewPosition.vy - PULSE_GRENADE_DROP;
    position->vz = g_viewPosition.vz;
    velocity->vx = 0;
    velocity->vy = -GRENADE_LIFT;
    velocity->vz = g_player.movement.vz + PULSE_GRENADE_SPEED;
    if ( velocity->vz > PULSE_GRENADE_SPEED )
        velocity->vz = PULSE_GRENADE_SPEED;
    Geo_RotateVector( &rotation, velocity, velocity );
    Geo_RotateVector( &g_viewRotation, velocity, velocity );
    particle->model = g_objectModels[PICKUP_GRENADES];
    Particle_Launch( particle, 0, -g_viewRotation.vy & ANGLE_MASK, 0, PULSE_GRENADE_LIFE, PULSE_GRENADE_FUSE,
                     PARTICLE_KIND_PULSE_GRENADE, PARTICLE_DAMAGE_GRENADE );
    Snd_Play( SOUND_PULSE_GRENADE, -1, SPU_VOLUME_MAX / 2 );
}

/*
==================
Particle_ThrowGrenade
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_ThrowGrenade( void )
{
    particle_t *particle = Particle_Alloc();

    if ( particle != 0 ) {
        SVECTOR *position = particle->position;
        SVECTOR *velocity = particle->velocity;
        SVECTOR  rotation;

        rotation.vx = -g_viewPitch.vx & ANGLE_MASK;
        rotation.vy = 0;
        rotation.vz = 0;
        position->vx = g_viewPosition.vx + g_player.shotOffsetX;
        position->vy = g_viewPosition.vy - HAND_GRENADE_DROP;
        position->vz = g_viewPosition.vz;
        velocity->vx = 0;
        velocity->vy = -GRENADE_LIFT;
        velocity->vz = g_player.movement.vz + HAND_GRENADE_SPEED;
        if ( velocity->vz >= HAND_GRENADE_MAX_SPEED + 1 )
            velocity->vz = HAND_GRENADE_MAX_SPEED;
        Geo_RotateVector( &rotation, velocity, velocity );
        Geo_RotateVector( &g_viewRotation, velocity, velocity );
        particle->model = g_objectModels[PICKUP_SEISMIC_CHARGES];
        Particle_Launch( particle, 0, 0, 0, HAND_GRENADE_LIFE, HAND_GRENADE_FUSE, PARTICLE_KIND_GRENADE, PARTICLE_DAMAGE_GRENADE );
    }
}

/*
==================
Particle_DrawFlame
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
/* RULE-EXCEPTION: R17-dead-local, CVECTOR color: type, name and size from the sibling sprite drawers
 * Particle_DrawSteam and Particle_DrawFire, which keep CVECTOR color in the same 0x10(sp) slot of the
 * same 0x30 frame (same saved s0-s3); retail never touches the slot. Ruled out: an inlined callee (calls
 * are Sys_Random and GetTPage only), an aggregate temporary (no struct copy or return), an SDK macro local
 * (GetTPage/setPolyFT4/setSemiTrans/addPrim are expressions), and a live colour copied through color
 * (A/B: 20 bytes larger, with stack stores retail lacks). */
void Particle_DrawFlame( DVECTOR *screen, particle_t *particle )
{
    POLY_FT4   *poly = g_spriteWorkspace;
    spriteUV_t *uv;
    short       half;
    CVECTOR     color;

    if ( particle->size < FLAME_SMALL_SIZE )
        half = ( ( FLAME_SIZE - particle->size ) * FLAME_GROWTH_SMALL << FLAME_SCALE_SHIFT ) / g_projectedDepth;
    else
        half = ( ( FLAME_SIZE - particle->size ) * FLAME_GROWTH << FLAME_SCALE_SHIFT ) / g_projectedDepth;
    uv = &g_explosionUvs[Sys_Random() & FLAME_FRAME_MASK];
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
    if ( particle->size < FLAME_ADDITIVE_SIZE )
        poly->tpage = GetTPage( TPAGE_MODE_4BIT, TPAGE_ABR_ADD_QUARTER, EXPLOSION_TPAGE_X, EXPLOSION_TPAGE_Y );
    else
        poly->tpage = GetTPage( TPAGE_MODE_4BIT, TPAGE_ABR_ADD, EXPLOSION_TPAGE_X, EXPLOSION_TPAGE_Y );
    setPolyFT4( poly );
    setSemiTrans( poly, 1 );
    if ( g_projectedDepth > PARTICLE_FLAME_DEPTH_BIAS )
        g_projectedDepth -= PARTICLE_FLAME_DEPTH_BIAS;
    else
        g_projectedDepth = PARTICLE_FLAME_MIN_DEPTH;
    addPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), poly );
    poly++;
    g_spriteWorkspace = poly;
}

/*
==================
Particle_DrawSteam
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Particle_DrawSteam( DVECTOR *screen, short size, short kind )
{
    POLY_FT4   *poly = g_spriteWorkspace;
    spriteUV_t *uv;
    short       width;
    short       height;
    CVECTOR     color;

    if ( size < 0 )
        size = 0;
    if ( size >= STEAM_SIZE_MAX )
        size = STEAM_SIZE_MAX - 1;
    uv = &g_explosionUvs[g_steamFrames[( Sys_Random() & STEAM_FRAME_MASK ) + ( STEAM_SIZE_MAX - size )]];
    *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
    *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
    *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
    *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
    poly->clut = g_explosionCluts[2]->clut;
    if ( kind == PARTICLE_KIND_STEAM ) {
        width = g_steamLengths[size] * STEAM_SCALE / g_projectedDepth;
        height = g_steamWidths[size] * STEAM_SCALE / g_projectedDepth;
    } else {
        height = g_steamLengths[size] * STEAM_SCALE / g_projectedDepth;
        width = g_steamWidths[size] * STEAM_SCALE / g_projectedDepth;
    }
    width -= ( Sys_Random() & STEAM_JITTER_MASK ) - STEAM_JITTER;
    height -= ( Sys_Random() & STEAM_JITTER_MASK ) - STEAM_JITTER;
    poly->x0 = screen->vx - width;
    poly->x1 = width + screen->vx;
    poly->y0 = screen->vy - height;
    poly->y2 = height + screen->vy;
    poly->x2 = poly->x0;
    poly->x3 = poly->x1;
    poly->y3 = poly->y2;
    poly->y1 = poly->y0;
    if ( g_gameFlags & GAME_FLAG_FADE )
        color.b = color.g = color.r = g_palette->colors[0].r >> 1;
    else
        color.b = color.g = color.r = size + STEAM_BRIGHTNESS;
    g_transformResult = g_projectedDepth;
    if ( g_transformResult < ONE ) {
        *(u_long *)&poly->r0 = *(u_long *)&color;
    } else {
        g_transformResult -= ONE;
        DpqColor( &color, g_transformResult, (CVECTOR *)&poly->r0 );
    }
    setPolyFT4( poly );
    setSemiTrans( poly, 1 );
    addPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), poly );
    poly++;
    g_spriteWorkspace = poly;
}

/*
==================
Particle_DrawFire
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Particle_DrawFire( DVECTOR *screen, short size, short kind )
{
    POLY_FT4   *poly = g_spriteWorkspace;
    spriteUV_t *uv;
    short       width;
    short       height;
    CVECTOR     color;

    if ( size < 0 )
        size = 0;
    if ( size >= STEAM_SIZE_MAX )
        size = STEAM_SIZE_MAX - 1;
    uv = &g_explosionUvs[g_fireFrames[( Sys_Random() & STEAM_FRAME_MASK ) + ( STEAM_SIZE_MAX - size )]];
    *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
    *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
    *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
    *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
    if ( kind == PARTICLE_KIND_FIRE ) {
        width = g_fireLengths[size] * FIRE_LENGTH_SCALE / g_projectedDepth;
        height = g_fireWidths[size] * FIRE_WIDTH_SCALE / g_projectedDepth;
    } else {
        height = g_fireLengths[size] * FIRE_LENGTH_SCALE / g_projectedDepth;
        width = g_fireWidths[size] * FIRE_VERTICAL_WIDTH_SCALE / g_projectedDepth;
    }
    poly->x0 = screen->vx - width;
    poly->x1 = width + screen->vx;
    poly->y0 = screen->vy - height;
    poly->y2 = height + screen->vy;
    poly->x2 = poly->x0;
    poly->x3 = poly->x1;
    poly->y3 = poly->y2;
    poly->y1 = poly->y0;
    if ( g_gameFlags & GAME_FLAG_FADE ) {
        color.r = g_palette->colors[0].r;
        color.g = g_palette->colors[0].g;
        color.b = g_palette->colors[0].b;
    } else {
        color.b = color.g = color.r = FIRE_BRIGHTNESS;
    }
    g_transformResult = g_projectedDepth;
    if ( g_transformResult < ONE ) {
        *(u_long *)&poly->r0 = *(u_long *)&color;
    } else {
        g_transformResult -= ONE;
        DpqColor( &color, g_transformResult, (CVECTOR *)&poly->r0 );
    }
    setPolyFT4( poly );
    if ( size < FIRE_OPAQUE_SIZE )
        setSemiTrans( poly, 1 );
    addPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), poly );
    poly++;
    g_spriteWorkspace = poly;
}

/*
==================
Particle_DrawModel
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_DrawModel( particle_t *particle )
{
    VECTOR scale;

    if ( particle->kind == PARTICLE_KIND_PISTOL_SHELL )
        scale.vx = scale.vy = scale.vz = ONE / 2;
    g_modelUvs = g_pickupUvs;
    LoadAverageShort12( particle->position, &g_viewPosition, ONE, -ONE, &g_relativePosition );
    PushMatrix();
    ApplyMatrix( &g_viewMatrix, &g_relativePosition, &g_translation );
    if ( (u_long)( g_translation.vz - ( DRAW_NEAR_DEPTH + 1 ) ) < DRAW_FAR_DEPTH - DRAW_NEAR_DEPTH - 1
         && g_translation.vx < DRAW_FAR_DEPTH ) {
        RotMatrix( &particle->rotation, &g_matrixWork );
        MulMatrix2( &g_viewMatrix, &g_matrixWork );
        if ( particle->kind == PARTICLE_KIND_PISTOL_SHELL )
            ScaleMatrix( &g_matrixWork, &scale );
        SetRotMatrix( &g_matrixWork );
        TransMatrix( &g_matrixWork, &g_translation );
        SetTransMatrix( &g_matrixWork );
        Model_DrawMidInCell( particle->model, particle->cell );
    }
    g_modelUvs = g_spriteUvs;
    PopMatrix();
    if ( particle->kind == PARTICLE_KIND_GRENADE )
        particle->rotation.vx = ( particle->rotation.vx + GRENADE_SPIN ) & ANGLE_MASK;
    if ( particle->kind == PARTICLE_KIND_SHOTGUN_SHELL || particle->kind == PARTICLE_KIND_PISTOL_SHELL ) {
        particle->rotation.vx = ( particle->rotation.vx + SHELL_SPIN_X ) & ANGLE_MASK;
        particle->rotation.vy = ( particle->rotation.vy + SHELL_SPIN_Y ) & ANGLE_MASK;
    }
}

/*
==================
Particle_DrawDebris
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
/* MATCH-PENDING: R17, unused 8-byte local for retail's untouched 0x28(sp) slot after scale; no sibling, callee or lineage source evidences its type (Particle_DrawModel has scale only); no inlined callee, aggregate temporary or SDK macro local explains it */
void Particle_DrawDebris( particle_t *particle )
{
    VECTOR         scale;
    SVECTOR        unused;
    dispatchRow_t *row;

    scale.vx = scale.vy = scale.vz = particle->size << DEBRIS_SCALE_SHIFT;
    LoadAverageShort12( particle->position, &g_viewPosition, ONE, -ONE, &g_relativePosition );
    PushMatrix();
    ApplyMatrix( &g_viewMatrix, &g_relativePosition, &g_translation );
    if ( (u_long)( g_translation.vz - ( DRAW_NEAR_DEPTH + 1 ) ) < PARTICLE_MODEL_FAR_DEPTH - DRAW_NEAR_DEPTH - 1
         && g_translation.vx < PARTICLE_MODEL_FAR_DEPTH ) {
        RotMatrix( &particle->rotation, &g_matrixWork );
        MulMatrix2( &g_viewMatrix, &g_matrixWork );
        ScaleMatrix( &g_matrixWork, &scale );
        SetRotMatrix( &g_matrixWork );
        TransMatrix( &g_matrixWork, &g_translation );
        SetTransMatrix( &g_matrixWork );
        if ( particle->kind == PARTICLE_KIND_SHARD ) {
            row = particle->model;
            g_farDrawTable[ROW_SELECT_QUAD_NOCULL]( row, &g_palette[row->flags & ROW_PALETTE_MASK], g_debrisUvs + row->count );
        } else if ( particle->kind == PARTICLE_KIND_PANEL ) {
            g_farDrawTable[ROW_SELECT_QUAD_SEMITRANS]( particle->model, &g_palette[particle->cell->palette],
                                                       g_debrisUvs + particle->model->count );
        }
    }
    PopMatrix();
    if ( particle->kind != PARTICLE_KIND_PANEL )
        particle->rotation.vz = ( particle->rotation.vz + DEBRIS_SPIN ) & ANGLE_MASK;
}

/*
==================
Particle_DrawSpark
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_DrawSpark( DVECTOR *screen, particle_t *particle )
{
    POLY_F4 *poly;
    short    size;

    size = ( particle->size << PARTICLE_SCALE_SHIFT ) / g_projectedDepth;
    poly = g_spriteWorkspace;
    if ( size == 0 )
        size++;
    poly->x0 = screen->vx;
    poly->y0 = screen->vy - size;
    poly->x1 = size + screen->vx;
    poly->y1 = screen->vy;
    poly->x2 = screen->vx - size;
    poly->y2 = screen->vy;
    poly->x3 = screen->vx;
    poly->y3 = size + screen->vy;
    switch ( particle->state ) {
    case SPARK_BLUE:
        setRGB0( poly, SPARK_BLUE_RED, SPARK_BLUE_GREEN, SPARK_BLUE_BLUE );
        break;
    case SPARK_GREEN:
        setRGB0( poly, SPARK_GREEN_RED, SPARK_GREEN_GREEN, SPARK_GREEN_BLUE );
        break;
    default:
        setRGB0( poly, SPARK_YELLOW_RED, SPARK_YELLOW_GREEN, SPARK_YELLOW_BLUE );
        break;
    }
    if ( particle->size < SPARK_FADE_SIZE )
        DpqColor( (CVECTOR *)&poly->r0, ONE - ( particle->size << SPARK_FADE_SHIFT ), (CVECTOR *)&poly->r0 );
    setPolyF4( poly );
    poly++;
    AddPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), g_spriteWorkspace );
    g_spriteWorkspace = poly;
}

/*
==================
Particle_DrawBlood
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_DrawBlood( particle_t *particle, DVECTOR *screen )
{
    POLY_FT4   *poly = g_spriteWorkspace;
    spriteUV_t *uv;
    short       size;

    if ( particle->large )
        size = ( particle->size << BLOOD_LARGE_SCALE_SHIFT ) / g_projectedDepth;
    else
        size = ( particle->size << PARTICLE_SCALE_SHIFT ) / g_projectedDepth;
    uv = &g_explosionUvs[BLOOD_FRAME_FIRST + ( ( g_frameCount >> BLOOD_FRAME_RATE_SHIFT ) & BLOOD_FRAME_MASK )];
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
    if ( particle->kind == PARTICLE_KIND_ACID_BLOOD ) {
        poly->r0 = 0;
        poly->b0 = 0;
    } else if ( particle->kind == PARTICLE_KIND_BLOOD ) {
        poly->g0 = 0;
        poly->b0 = 0;
    }
    setPolyFT4( poly );
    setSemiTrans( poly, 1 );
    poly++;
    AddPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), g_spriteWorkspace );
    g_spriteWorkspace = poly;
}

/*
==================
Particle_DrawTracer
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_DrawTracer( particle_t *particle, DVECTOR *screen )
{
    paletteEntry_t *palette = g_palette;
    LINE_G2        *line = g_spriteWorkspace;
    CVECTOR        *tail = &palette[1].colors[0];
    SVECTOR         end;

    switch ( particle->kind ) {
    case PARTICLE_KIND_PISTOL_SHOT:
        end.vx = particle->position->vx + ( particle->velocity->vx >> 1 );
        end.vy = particle->position->vy + ( particle->velocity->vy >> 1 );
        end.vz = particle->position->vz + ( particle->velocity->vz >> 1 );
        setRGB0( line, palette[1].colors[0].r, palette[1].colors[0].g, 0 );
        break;
    case PARTICLE_KIND_SMART_GUN_SHOT:
        end.vx = particle->position->vx + particle->velocity->vx;
        end.vy = particle->position->vy + particle->velocity->vy;
        end.vz = particle->position->vz + particle->velocity->vz;
        setRGB0( line, 0, palette[1].colors[0].g, palette[1].colors[0].b );
        break;
    case PARTICLE_KIND_PULSE_RIFLE_SHOT:
        end.vx = particle->position->vx + particle->velocity->vx;
        end.vy = particle->position->vy + particle->velocity->vy;
        end.vz = particle->position->vz + particle->velocity->vz;
        setRGB0( line, 0, palette[0].colors[0].g, palette[1].colors[0].b );
        break;
    default:
        end.vx = particle->position->vx + particle->velocity->vx;
        end.vy = particle->position->vy + particle->velocity->vy;
        end.vz = particle->position->vz + particle->velocity->vz;
        setRGB0( line, palette[0].colors[0].r, 0, 0 );
        break;
    }
    RotTransPers( &end, (long *)&end, &g_gteInterpolation, &g_gteFlag );
    line->x0 = screen->vx;
    line->y0 = screen->vy;
    line->x1 = end.vx;
    line->y1 = end.vy;
    setRGB1( line, tail->r, tail->g, tail->b );
    setLineG2( line );
    line++;
    AddPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), g_spriteWorkspace );
    g_spriteWorkspace = line;
    if ( particle->kind == PARTICLE_KIND_SMART_GUN_SHOT ) {
        line->x0 = screen->vx + 1;
        line->y0 = screen->vy + 1;
        line->x1 = end.vx + 1;
        line->y1 = end.vy + 1;
        setRGB0( line, 0, tail->g, tail->b );
        setRGB1( line, tail->r, tail->g, tail->b );
        setLineG2( line );
        line++;
        AddPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), g_spriteWorkspace );
        g_spriteWorkspace = line;
    }
}

/*
==================
Particle_Draw
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_Draw( particle_t *particle )
{
    DVECTOR screen;

    switch ( particle->kind ) {
    case PARTICLE_KIND_SHOTGUN_SHOT:
        return;
    case PARTICLE_KIND_EXPLOSIVE:
    case PARTICLE_KIND_PULSE_GRENADE:
    case PARTICLE_KIND_ENEMY_GRENADE:
    case PARTICLE_KIND_GRENADE:
    case PARTICLE_KIND_SHOTGUN_SHELL:
    case PARTICLE_KIND_PISTOL_SHELL:
        Particle_DrawModel( particle );
        return;
    }
    g_projectedDepth = RotTransPers( particle->position, (long *)&screen, &g_gteInterpolation, &g_gteFlag ) << 2;
    if ( g_projectedDepth > PARTICLE_FAR_DEPTH )
        return;
    if ( screen.vx < 0 )
        return;
    if ( screen.vx > SCREEN_WIDTH )
        return;
    if ( screen.vy < 0 )
        return;
    if ( screen.vy > SCREEN_HEIGHT )
        return;
    switch ( particle->kind ) {
    case PARTICLE_KIND_STEAM:
    case PARTICLE_KIND_STEAM_VERTICAL:
        if ( g_projectedDepth >= DRAW_NEAR_DEPTH )
            Particle_DrawSteam( &screen, particle->size, particle->kind );
        break;
    case PARTICLE_KIND_FIRE:
    case PARTICLE_KIND_FIRE_VERTICAL:
        if ( g_projectedDepth >= DRAW_NEAR_DEPTH )
            Particle_DrawFire( &screen, particle->size, particle->kind );
        break;
    case PARTICLE_KIND_SHARD:
    case PARTICLE_KIND_PANEL:
        if ( g_projectedDepth >= DRAW_NEAR_DEPTH )
            Particle_DrawDebris( particle );
        break;
    case PARTICLE_KIND_PISTOL_SHOT:
    case PARTICLE_KIND_PULSE_RIFLE_SHOT:
    case PARTICLE_KIND_SMART_GUN_SHOT:
        if ( Menu_GetDifficulty() != 2 && g_projectedDepth >= DRAW_NEAR_DEPTH )
            Particle_DrawTracer( particle, &screen );
        break;
    case PARTICLE_KIND_ENEMY_SHOT:
        if ( Menu_GetDifficulty() == DIFFICULTY_ACID_REIGN && g_projectedDepth >= DRAW_NEAR_DEPTH )
            Particle_DrawTracer( particle, &screen );
        break;
    case PARTICLE_KIND_FLAME:
        if ( g_projectedDepth >= PARTICLE_FLAME_NEAR_DEPTH )
            Particle_DrawFlame( &screen, particle );
        break;
    case PARTICLE_KIND_SPARK:
        if ( g_projectedDepth >= DRAW_NEAR_DEPTH )
            Particle_DrawSpark( &screen, particle );
        break;
    case PARTICLE_KIND_BLOOD:
    case PARTICLE_KIND_ACID_BLOOD:
    case PARTICLE_KIND_SYNTH_BLOOD:
        if ( g_projectedDepth >= DRAW_NEAR_DEPTH )
            Particle_DrawBlood( particle, &screen );
        break;
    }
}

/*
==================
Particle_Explode
==================
*/
/* name: invented; four explosions scattered round the particle and one at its centre */
static __inline__ void Particle_Explode( particle_t *particle )
{
    short dx;
    short dy;

    dx = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    dy = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    Particle_SpawnExplosion( particle->position->vx + dx, particle->position->vy + dy, particle->position->vz );
    dx = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    dy = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    Particle_SpawnExplosion( particle->position->vx + dx, particle->position->vy + dy, particle->position->vz );
    dx = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    dy = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    Particle_SpawnExplosion( particle->position->vx + dx, particle->position->vy + dy, particle->position->vz );
    dx = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    dy = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    Particle_SpawnExplosion( particle->position->vx + dx, particle->position->vy + dy, particle->position->vz );
    Particle_SpawnExplosion( particle->position->vx, particle->position->vy, particle->position->vz );
}

/*
==================
Particle_ExplodeUp
==================
*/
/* name: invented; like Particle_Explode, but the explosions rise above the particle */
static __inline__ void Particle_ExplodeUp( particle_t *particle )
{
    short dx;

    dx = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    Particle_SpawnExplosion( particle->position->vx + dx,
                             particle->position->vy + ( ( Sys_Random() & EXPLOSION_RISE_MASK ) | EXPLOSION_RISE ),
                             particle->position->vz );
    dx = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    Particle_SpawnExplosion( particle->position->vx + dx,
                             particle->position->vy + ( ( Sys_Random() & EXPLOSION_RISE_MASK ) | EXPLOSION_RISE ),
                             particle->position->vz );
    dx = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    Particle_SpawnExplosion( particle->position->vx + dx,
                             particle->position->vy + ( ( Sys_Random() & EXPLOSION_RISE_MASK ) | EXPLOSION_RISE ),
                             particle->position->vz );
    dx = ( Sys_Random() & EXPLOSION_SCATTER_MASK ) - EXPLOSION_SCATTER;
    Particle_SpawnExplosion( particle->position->vx + dx,
                             particle->position->vy + ( ( Sys_Random() & EXPLOSION_RISE_MASK ) | EXPLOSION_RISE ),
                             particle->position->vz );
    Particle_SpawnExplosion( particle->position->vx, particle->position->vy + EXPLOSION_RISE, particle->position->vz );
}

/* name: invented; a random speed of about half the vertical speed v, either way */
#define SCATTER_SPEED( v )                             \
    ( {                                                \
        int speed = Sys_Random() & SCATTER_SPEED_MASK; \
        speed += ( v ) / 2;                            \
        if ( Sys_Random() & 1 )                        \
            speed = -speed;                            \
        speed;                                         \
    } )

/*
==================
Particle_Task

Per-frame particle task: moves every live particle by its velocity (gteMIMefunc), finds its
map cell, ages it and runs its kind's behaviour, then draws it (Particle_Draw) or frees it.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Particle_Task( TASK *task )
{
    particle_t *particle = g_liveParticles;
    particle_t *next;
    u_int       x;
    u_int       z;
    /* RULE-EXCEPTION: R30, floor also holds the size countdown; a separate short local swaps $a1/$a2 in
     * the shard/panel and spark cases */
    short floor;
    short sample;

    if ( particle != 0 ) {
        gteMIMefunc( g_particlePositions, g_particleVelocities, PARTICLE_COUNT, ONE );
        for ( ; particle != 0; particle = next ) {
            next = particle->next;
            x = particle->position->vx + g_levelMap.offsetX;
            z = particle->position->vz + g_levelMap.offsetZ;
            if ( MAP_OUTSIDE( x, z ) ) {
                Particle_Unlink( particle );
                Particle_Free( particle );
                continue;
            }
            particle->cell = MAP_CELL( x, z );
            floor = particle->size;
            if ( --floor <= 0 ) {
                if ( particle->kind == PARTICLE_KIND_EXPLOSIVE || particle->kind == PARTICLE_KIND_PULSE_GRENADE
                     || particle->kind == PARTICLE_KIND_ENEMY_GRENADE || particle->kind == PARTICLE_KIND_GRENADE ) {
                    Object_ExplosionDamage( particle->position, particle->cell );
                    Particle_Explode( particle );
                }
                Particle_Unlink( particle );
                Particle_Free( particle );
                continue;
            }
            particle->size = floor;
            switch ( particle->kind ) {
            case PARTICLE_KIND_PISTOL_SHOT:
            case PARTICLE_KIND_SHOTGUN_SHOT:
            case PARTICLE_KIND_PULSE_RIFLE_SHOT:
            case PARTICLE_KIND_FLAME:
            case PARTICLE_KIND_SMART_GUN_SHOT:
                if ( Particle_HitWall( particle ) ) {
                    if ( particle->kind == PARTICLE_KIND_FLAME ) {
                        Particle_StepBack( particle );
                        particle->velocity->vx = particle->velocity->vy = particle->velocity->vz = 0;
                        Particle_Draw( particle );
                        continue;
                    }
                    Particle_ShotImpact( particle );
                    sample = (u_int)( g_level - LEVEL_211_LIVING_QUARTERS ) < LEVEL_311_TUNNELS - LEVEL_211_LIVING_QUARTERS
                                 ? SOUND_SHOT_IMPACT_ALT
                                 : SOUND_SHOT_IMPACT;
                    if ( (u_char)( particle->cell->type - CELL_TYPE_LIQUID_SHALLOW )
                         < CELL_TYPE_LIQUID_DEEP - CELL_TYPE_LIQUID_SHALLOW + 1 )
                        sample = SOUND_SHOT_SPLASH;
                    Snd_PlayAt( sample, particle->position, SOUND_AT_SOFT );
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                if ( Particle_HitActors( particle ) ) {
                    if ( particle->kind != PARTICLE_KIND_FLAME ) {
                        Particle_Unlink( particle );
                        Particle_Free( particle );
                        continue;
                    }
                    Particle_StepBack( particle );
                    particle->velocity->vx = particle->velocity->vy = particle->velocity->vz = 0;
                    Particle_Draw( particle );
                    continue;
                }
                Trigger_ShotCell( particle );
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_EXPLOSIVE:
            case PARTICLE_KIND_PULSE_GRENADE:
            case PARTICLE_KIND_GRENADE:
                if ( Particle_HitFloorOrCeiling( particle ) ) {
                    Particle_Explode( particle );
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                if ( Particle_HitActors( particle ) ) {
                    Particle_StepBack( particle );
                    Particle_Explode( particle );
                    Object_ExplosionDamage( particle->position, particle->cell );
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                Trigger_ShotCell( particle );
                floor = FloorHeight( x, z );
                if ( particle->position->vy < floor + GRENADE_BOUNCE_HEIGHT ) {
                    Particle_ShotImpact( particle );
                    if ( floor - particle->position->vy > GRENADE_STEP_LIMIT ) {
                        Particle_StepBack( particle );
                        Object_ExplosionDamage( particle->position, particle->cell );
                        Particle_ExplodeUp( particle );
                        Particle_Unlink( particle );
                        Particle_Free( particle );
                        continue;
                    }
                    particle->position->vy = floor + GRENADE_BOUNCE_HEIGHT;
                    particle->velocity->vy = particle->life;
                    if ( particle->life >= GRENADE_BOUNCE_MIN ) {
                        particle->life -= 2;
                    } else {
                        particle->velocity->vx >>= 1;
                        particle->velocity->vz >>= 1;
                    }
                    Particle_Draw( particle );
                    break;
                }
                particle->velocity->vy -= 1;
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_ENEMY_GRENADE:
                if ( Particle_HitFloorOrCeiling( particle ) ) {
                    Particle_SpawnExplosion( particle->position->vx, particle->position->vy, particle->position->vz );
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                if ( Particle_HitPlayer( particle ) ) {
                    Particle_StepBack( particle );
                    Object_ExplosionDamage( particle->position, particle->cell );
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                Trigger_EnemyShotCell( particle );
                floor = FloorHeight( x, z );
                if ( particle->position->vy < floor + GRENADE_BOUNCE_HEIGHT ) {
                    Particle_ShotImpact( particle );
                    if ( floor - particle->position->vy > GRENADE_STEP_LIMIT ) {
                        Particle_StepBack( particle );
                        Object_ExplosionDamage( particle->position, particle->cell );
                        Particle_SpawnExplosion( particle->position->vx, particle->position->vy + EXPLOSION_RISE,
                                                 particle->position->vz );
                        Particle_Unlink( particle );
                        Particle_Free( particle );
                        continue;
                    }
                    particle->position->vy = floor + GRENADE_BOUNCE_HEIGHT;
                    particle->velocity->vy = particle->life;
                    if ( particle->life >= GRENADE_BOUNCE_MIN ) {
                        particle->life -= 2;
                    } else {
                        particle->velocity->vx >>= 1;
                        particle->velocity->vz >>= 1;
                    }
                    Particle_Draw( particle );
                    break;
                }
                particle->velocity->vy -= 2;
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_ENEMY_SHOT:
                if ( Particle_HitWall( particle ) ) {
                    Particle_ShotImpact( particle );
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                if ( Particle_HitPlayer( particle ) ) {
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                Trigger_EnemyShotCell( particle );
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_FIRE_VERTICAL:
                particle->size--;
            case PARTICLE_KIND_STEAM_VERTICAL:
                if ( Particle_HitPlayer( particle ) ) {
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                if ( particle->velocity->vy > 0
                     && ( particle->cell->ceiling - JET_PUFF_CEILING_GAP ) << MAP_HEIGHT_SHIFT < particle->position->vy ) {
                    particle->position->vy = ( particle->cell->ceiling - JET_PUFF_CEILING_GAP ) << MAP_HEIGHT_SHIFT;
                    particle->velocity->vx = SCATTER_SPEED( particle->velocity->vy );
                    particle->velocity->vz = SCATTER_SPEED( particle->velocity->vy );
                    particle->velocity->vy = -JET_PUFF_BOUNCE;
                } else if ( particle->velocity->vy < 0
                            && particle->position->vy < ( particle->cell->value + JET_PUFF_FLOOR_GAP ) << MAP_HEIGHT_SHIFT ) {
                    particle->position->vy = ( particle->cell->value + JET_PUFF_FLOOR_GAP ) << MAP_HEIGHT_SHIFT;
                    particle->velocity->vx = SCATTER_SPEED( particle->velocity->vy );
                    particle->velocity->vz = SCATTER_SPEED( particle->velocity->vy );
                    particle->velocity->vy = JET_PUFF_BOUNCE;
                }
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_FIRE:
                particle->size--;
                if ( Particle_HitPlayer( particle ) ) {
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                if ( particle->size < FIRE_FALL_SIZE ) {
                    particle->velocity->vy -= 2;
                    particle->velocity->vz -= FIRE_DRAG;
                    if ( particle->velocity->vz < 0 )
                        particle->velocity->vz = 0;
                }
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_STEAM:
                if ( Particle_HitPlayer( particle ) ) {
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                if ( particle->size < STEAM_RISE_SIZE )
                    particle->velocity->vy += STEAM_RISE;
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_ACID_BLOOD:
            case PARTICLE_KIND_SYNTH_BLOOD:
                if ( Particle_HitPlayer( particle ) ) {
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                floor = FloorHeight( x, z );
                particle->velocity->vy -= PARTICLE_GRAVITY;
                if ( particle->position->vy < floor ) {
                    Particle_BloodSplash( particle );
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_BLOOD:
                floor = FloorHeight( x, z );
                particle->velocity->vy -= PARTICLE_GRAVITY;
                if ( particle->position->vy < floor ) {
                    Particle_BloodSplash( particle );
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_SHARD:
            case PARTICLE_KIND_PANEL:
                floor = FloorHeight( x, z ) + DEBRIS_REST_HEIGHT;
                if ( particle->velocity->vy == 0 ) {
                    Particle_Unlink( particle );
                    Particle_Free( particle );
                    continue;
                }
                particle->position->vy += particle->velocity->vy;
                if ( particle->position->vy < floor ) {
                    particle->position->vy = floor;
                    particle->life -= DEBRIS_BOUNCE_LOSS;
                    if ( particle->kind == PARTICLE_KIND_PANEL )
                        Snd_PlayAt( SOUND_PANEL_LAND, particle->position, SOUND_AT_NEAR );
                    if ( particle->life < DEBRIS_BOUNCE_MIN )
                        particle->velocity->vy = 0;
                    else
                        particle->velocity->vy = particle->life;
                    particle->velocity->vx = 0;
                    particle->velocity->vz = 0;
                } else if ( particle->velocity->vy != PARTICLE_GRAVITY ) {
                    particle->velocity->vy -= PARTICLE_GRAVITY;
                } else {
                    particle->velocity->vy = -2;
                }
                Particle_Draw( particle );
                break;
            case PARTICLE_KIND_SPARK:
                floor = FloorHeight( x, z );
                if ( particle->velocity->vy != 0 ) {
                    if ( particle->cell->type == CELL_TYPE_LIQUID_DEEP && particle->position->vy < floor + SPARK_BLUE_SURFACE ) {
                        SET_VECTOR_TO_ZERO( *particle->velocity );
                    } else if ( ( particle->cell->type == CELL_TYPE_LIQUID_SHALLOW && particle->position->vy < floor + SPARK_GREEN_SURFACE )
                                || particle->position->vy < floor ) {
                        SET_VECTOR_TO_ZERO( *particle->velocity );
                    } else {
                        particle->velocity->vy =
                            particle->velocity->vy == PARTICLE_GRAVITY ? -2 : particle->velocity->vy - PARTICLE_GRAVITY;
                    }
                    Particle_Draw( particle );
                    break;
                }
                Particle_Unlink( particle );
                Particle_Free( particle );
                continue;
            case PARTICLE_KIND_SHOTGUN_SHELL:
            case PARTICLE_KIND_PISTOL_SHELL:
                particle->velocity->vy -= SHELL_GRAVITY;
                Particle_Draw( particle );
                break;
            default:
                particle->velocity->vy -= PARTICLE_GRAVITY;
                Particle_Draw( particle );
                break;
            }
        }
    }
    g_nextTask = task->Next;
}
