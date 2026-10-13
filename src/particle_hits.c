/* Particle hit tests (retail .text 0x80023ef4..0x800246ac, just below particle.c's
 * 0x800246ac): stepping a particle back out of what it hit, and hits on level objects,
 * the floor and ceiling, actors and the player. Every caller is in particle.c's code;
 * no static or .rdata ties these functions to particle.c's translation unit or keeps
 * them apart, so they stay a unit of their own. Functions not yet matched (0x80024140)
 * are absent. Name invented.
 */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <MODEL.H>
#include <PARTICLE.H>
#include <HEIGHT.H>
#include <TRACK.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <ACTOR.H>
#include <CAMERA.H>
#include <GAME.H>


/* name: runtime evidence, analysis/runtime/functions.json */
void particle_step_back(Particle *particle)
{
    u_int x, z;

    particle->position->vx -= particle->velocity->vx;
    particle->position->vy -= particle->velocity->vy;
    particle->position->vz -= particle->velocity->vz;
    z = particle->position->vz + g_level_map.offset_z;
    x = particle->position->vx + g_level_map.offset_x;
    particle->cell = MAP_CELL(x, z);
}


/* name: inferred from its body: a particle that reaches a level object, the floor or the ceiling stops there */
int particle_hit_floor_or_ceiling(Particle *particle)
{
    MapCell *cell = particle->cell;
    u_int x = particle->position->vx + g_level_map.offset_x;
    u_int z = particle->position->vz + g_level_map.offset_z;
    u_int height;
    int clearance;

    if (cell->type == CELL_TYPE_WALL) {
        particle_step_back(particle);
        explosion_damage(particle->position, particle->cell);
        return -1;
    }
    if (cell->marker == LEVEL_OBJECT_BLOCK || cell->marker == LEVEL_OBJECT_WALL || cell->marker == LEVEL_OBJECT_BLOCK_SMALL) {
        level_object_tick(cell->occupant, particle->state);
        if (cell->marker == LEVEL_OBJECT_BLOCK) {
            particle_step_back(particle);
            explosion_damage(particle->position, particle->cell);
        }
        return -1;
    }
    if ((u_char)(cell->marker - LEVEL_OBJECT_TARGET) < CELL_MARKER_PLAYER - LEVEL_OBJECT_TARGET) {
        height = floor_height(x, z);
        do {
            if (cell->marker == LEVEL_OBJECT_TARGET_ALT)
                clearance = OBJECT_TALL_HIT_HEIGHT;
            else
                clearance = OBJECT_HIT_HEIGHT;
        } while (0);
        if ((u_int)particle->position->vy >= height && height + clearance >= (u_int)particle->position->vy) {
            level_object_tick(cell->occupant, particle->state);
            particle_step_back(particle);
            explosion_damage(particle->position, particle->cell);
            return -1;
        }
    }
    height = ceiling_height(x, z);
    if (height < (u_int)particle->position->vy) {
        if (cell->type != CELL_TYPE_DOOR)
            particle->position->vy = height - PARTICLE_CEILING_GAP;
        return -1;
    }
    return 0;
}


/* name: inferred from its body: damages each visible live actor whose box (scale, doubled
 * for kind 1) holds the particle; returns -1 on a hit. int: particle.c tests the full
 * register (retail's callee sign-extends the short result itself). */
int particle_hit_actors(Particle *particle)
{
    u_short mask = particle->cell->mask;
    int damage;
    SVECTOR *position;
    Actor *actor;
    short hit;
    int delta[3];
    int rx, ry, rz;

    if (particle->size < particle->large / 2)
        damage = particle->state;
    else
        damage = particle->state * 2;
    actor = g_live_actors;
    position = particle->position;
    hit = 0;
    for (; actor; actor = actor->next) {
        if (actor->action < ACTOR_HURT && (mask & actor->cell->mask)) {
            delta[0] = position->vx - actor->position.vx;
            if (delta[0] < 0)
                delta[0] = -delta[0];
            delta[1] = position->vy - actor->position.vy;
            if (delta[1] < 0)
                delta[1] = -delta[1];
            delta[2] = position->vz - actor->position.vz;
            if (delta[2] < 0)
                delta[2] = -delta[2];
            if (particle->kind == PARTICLE_KIND_SHOTGUN_SHOT) {
                rx = actor->scale.vx * 2;
                ry = actor->scale.vy * 2;
                rz = actor->scale.vz * 2;
            } else {
                rx = actor->scale.vx;
                ry = actor->scale.vy;
                rz = actor->scale.vz;
            }
            if (delta[0] < rx && delta[1] < ry && delta[2] < rz) {
                if (particle->kind == PARTICLE_KIND_EXPLOSIVE || particle->kind == PARTICLE_KIND_PULSE_GRENADE || particle->kind == PARTICLE_KIND_GRENADE) {
                    if (actor->kind != ACTOR_KIND_BOSS)
                        actor_damage(actor, actor->health, -1);
                    else
                        actor_damage(actor, damage, -1);
                } else
                    actor_damage(actor, damage, 0);
                actor_bleed(actor, particle);
                hit = -1;
                if (particle->kind != PARTICLE_KIND_SHOTGUN_SHOT && particle->kind != PARTICLE_KIND_SMART_GUN_SHOT)
                    return -1;
            }
        }
    }
    return hit;
}

/* Hits the player with a particle within 0x100 on x and z and 0..0x400 above the player:
 * kind 5 damages through player_hit_from with the particle's heading, every
 * other kind through player_hit. Returns -1 on a hit. */
/* name: runtime evidence, analysis/runtime/functions.json */
int particle_hit_player(Particle *particle)
{
    SVECTOR *position = particle->position;
    int delta[3];
    delta[1] = position->vy - g_player.position.vy;
    if (delta[1] >= 0) {
        if (delta[1] < PARTICLE_PLAYER_HIT_HEIGHT + 1) {
            delta[0] = position->vx - g_player.position.vx;
            delta[2] = position->vz - g_player.position.vz;
            if (delta[0] < 0) delta[0] = -delta[0];
            if (delta[2] < 0) delta[2] = -delta[2];
            if (delta[0] < PARTICLE_PLAYER_HIT_RADIUS) {
                if (delta[2] < PARTICLE_PLAYER_HIT_RADIUS) {
                    if (particle->kind == PARTICLE_KIND_ENEMY_SHOT)
                        player_hit_from(particle->state, particle->rotation.vy, DEATH_SHOT);
                    else
                        player_hit(particle->state, DEATH_HAZARD);
                    return -1;
                }
            }
        }
    }
    return 0;
}
