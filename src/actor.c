/* Actors: the actor pool and its slots, spawning, animation, projection and
 * drawing, the facehugger overlay, damage and death, attacks, movement, wandering
 * and line of sight, the per-kind update tasks and the jets. Retail .text from
 * 0x80028804 to at least 0x80030b70. One translation unit: its statics (.sbss
 * 0x8008d140..0x8008d188) are read from actor_free (0x80028804) to jet_update
 * (0x8002ff08) and by nothing else, so the code between is one module; the .rdata
 * of its jump tables and strings (0x80011c4c..0x80011f00) follows the function
 * order. Functions not yet matched are absent. Name invented.
 */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LEVEL.H>
#include <TRACK.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <ANIMS.H>
#include <ACTOR.H>
#include <HEAP.H>
#include <GAME.H>
#include <KERNEL.H>
#include <LIBAPI.H>
#include <MODEL.H>
#include <TEXTURE.H>
#include <TASK.H>
#include <LIBCD.H>
#include <HEAPFILE.H>
#include <VIEW.H>
#include <TEXT.H>
#include <HUD.H>
#include <HEIGHT.H>
#include <PARTICLE.H>
#include <ABS.H>
#include <CAMERA.H>
#include <LIBGPU.H>
#include <DISPLAY.H>
#include <AUDIO.H>
#include <ITEMS.H>
typedef signed int s32;
typedef signed short s16;
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned short u16;

/* Module statics (.sbss 0x8008d140..0x8008d188), in their retail order. */
static SVECTOR g_sight_point; /* name: inferred from actor_line_of_sight, the ray position it steps */
static SVECTOR g_8d148; /* identity-TODO: actor_path_clear's second probe, (0, 0, 0xc8) turned by g_velocity_rotation */
static SVECTOR g_sight_step; /* name: inferred from actor_line_of_sight, the per-step ray offset */
/* name: inferred from use: the rotation actor_path_clear and actor_set_velocity turn a step or the velocity by */
static SVECTOR g_velocity_rotation;
static SVECTOR g_wander_offset; /* name: inferred from actor_pick_target_near_player, the random distance to the next target */
static SVECTOR g_wander_rotation; /* name: inferred from actor_pick_target_near_player, the random heading to the next target */
/* name: inferred from use: actor_free pushes and actor_alloc pops it */
static Actor *g_actor_free;
/* name: inferred from use: actor_slots_init stores the level's ActorSlot records */
static ActorSlot *g_actor_slots;
/* name: runtime evidence, analysis/runtime/globals.json: count of the ActorSlot records in g_actor_slots */
static short g_actor_slot_count;
/* name: inferred from use: actor_project counts the entries of g_visible_actors, actor_pool_init clears it */
static short g_visible_actor_count;

/* Animation tables of the actor kinds (frame lists and scripts in asm/sprite_anims.s). */
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_egg_animations[] = {
    { g_egg_anim5_frames, g_egg_anim5_script }, { g_egg_anim2_frames, g_egg_anim2_script }, { g_egg_anim4_frames, g_egg_anim4_script },
    { g_egg_anim0_frames, g_egg_anim0_script }, { g_egg_anim1_frames, g_egg_anim1_script }, { g_egg_anim3_frames, g_egg_anim3_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_hugger_animations[] = {
    { g_hugger_anim0_frames, g_hugger_anim0_script }, { g_hugger_anim2_frames, g_hugger_anim2_script }, { g_hugger_anim1_frames, g_hugger_anim1_script },
    { g_hugger_anim3_frames, g_hugger_anim3_script }, { g_hugger_anim4_frames, g_hugger_anim4_script }, { g_hugger_anim3_frames, g_hugger_anim3_script },
    { g_hugger_anim1_frames, g_hugger_anim1_script }, { g_hugger_anim2_frames, g_hugger_anim2_script }, { g_hugger_anim5_frames, g_hugger_anim5_script },
    { g_hugger_anim6_frames, g_hugger_anim6_script }, { g_hugger_anim7_frames, g_hugger_anim7_script }, { g_fingers_anim1_frames, g_fingers_anim1_script },
    { g_fingers_anim0_frames, g_fingers_anim0_script }
};
/* name: runtime evidence, analysis/runtime/globals.json: animations of every kind-6 (adult alien) actor */
static TrackEntry g_alien_animations[] = {
    { g_warrior_anim3_frames, g_warrior_anim3_script }, { g_warrior_anim4_frames, g_warrior_anim4_script }, { g_warrior_anim5_frames, g_warrior_anim5_script },
    { g_warrior_anim6_frames, g_warrior_anim6_script }, { g_warrior_anim7_frames, g_warrior_anim7_script }, { g_warrior_anim6_frames, g_warrior_anim6_script },
    { g_warrior_anim5_frames, g_warrior_anim5_script }, { g_warrior_anim4_frames, g_warrior_anim4_script }, { g_warrior_anim0_frames, g_warrior_anim0_script },
    { g_warrior_anim1_frames, g_warrior_anim1_script }, { g_warrior_anim2_frames, g_warrior_anim2_script }, { g_warrior_anim8_frames, g_warrior_anim8_script },
    { g_warrior_anim9_frames, g_warrior_anim9_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_alien_ceiling_animations[] = {
    { g_warrior_ceiling_anim2_frames, g_warrior_ceiling_anim2_script }, { g_warrior_ceiling_anim3_frames, g_warrior_ceiling_anim3_script }, { g_warrior_ceiling_anim3_frames, g_warrior_ceiling_anim3_script },
    { g_warrior_ceiling_anim2_frames, g_warrior_ceiling_anim2_script }, { g_warrior_ceiling_anim3_frames, g_warrior_ceiling_anim3_script }, { g_warrior_ceiling_anim3_frames, g_warrior_ceiling_anim3_script },
    { g_warrior_ceiling_anim3_frames, g_warrior_ceiling_anim3_script }, { g_warrior_ceiling_anim3_frames, g_warrior_ceiling_anim3_script }, { g_warrior_ceiling_anim0_frames, g_warrior_ceiling_anim0_script },
    { g_warrior_ceiling_anim1_frames, g_warrior_ceiling_anim1_script }, { g_warrior_ceiling_anim4_frames, g_warrior_ceiling_anim4_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_dog_ceiling_animations[] = {
    { g_dog_ceiling_anim0_frames, g_dog_ceiling_anim0_script }, { g_dog_ceiling_anim1_frames, g_dog_ceiling_anim1_script }, { g_dog_ceiling_anim1_frames, g_dog_ceiling_anim1_script },
    { g_dog_ceiling_anim0_frames, g_dog_ceiling_anim0_script }, { g_dog_ceiling_anim1_frames, g_dog_ceiling_anim1_script }, { g_dog_ceiling_anim1_frames, g_dog_ceiling_anim1_script },
    { g_dog_ceiling_anim1_frames, g_dog_ceiling_anim1_script }, { g_dog_ceiling_anim1_frames, g_dog_ceiling_anim1_script }, { g_dog_ceiling_anim2_frames, g_dog_ceiling_anim2_script },
    { g_dog_ceiling_anim3_frames, g_dog_ceiling_anim3_script }, { g_dog_ceiling_anim4_frames, g_dog_ceiling_anim4_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_handler_animations[] = {
    { g_handler_anim1_frames, g_handler_anim1_script }, { g_handler_anim2_frames, g_handler_anim2_script }, { g_handler_anim0_frames, g_handler_anim0_script },
    { g_handler_anim4_frames, g_handler_anim4_script }, { g_handler_anim3_frames, g_handler_anim3_script }, { g_handler_anim4_frames, g_handler_anim4_script },
    { g_handler_anim0_frames, g_handler_anim0_script }, { g_handler_anim2_frames, g_handler_anim2_script }, { g_handler_anim5_frames, g_handler_anim5_script },
    { g_handler_anim6_frames, g_handler_anim6_script }, { g_handler_anim7_frames, g_handler_anim7_script }, { g_handler_anim8_frames, g_handler_anim8_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_burster_animations[] = {
    { g_burster_anim9_frames, g_burster_anim9_script }, { g_burster_anim6_frames, g_burster_anim6_script }, { g_burster_anim8_frames, g_burster_anim8_script },
    { g_burster_anim7_frames, g_burster_anim7_script }, { g_burster_anim10_frames, g_burster_anim10_script }, { g_burster_anim7_frames, g_burster_anim7_script },
    { g_burster_anim8_frames, g_burster_anim8_script }, { g_burster_anim6_frames, g_burster_anim6_script }, { g_burster_anim2_frames, g_burster_anim2_script },
    { g_burster_anim3_frames, g_burster_anim3_script }, { g_burster_anim4_frames, g_burster_anim4_script }, { g_burster_anim5_frames, g_burster_anim5_script },
    { g_burster_anim0_frames, g_burster_anim0_script }, { g_burster_anim1_frames, g_burster_anim1_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_bambi_animations[] = {
    { g_bambi_anim4_frames, g_bambi_anim4_script }, { g_bambi_anim6_frames, g_bambi_anim6_script }, { g_bambi_anim3_frames, g_bambi_anim3_script },
    { g_bambi_anim7_frames, g_bambi_anim7_script }, { g_bambi_anim5_frames, g_bambi_anim5_script }, { g_bambi_anim7_frames, g_bambi_anim7_script },
    { g_bambi_anim3_frames, g_bambi_anim3_script }, { g_bambi_anim6_frames, g_bambi_anim6_script }, { g_bambi_anim1_frames, g_bambi_anim1_script },
    { g_bambi_anim2_frames, g_bambi_anim2_script }, { g_bambi_anim8_frames, g_bambi_anim8_script }, { g_bambi_anim9_frames, g_bambi_anim9_script },
    { g_bambi_anim0_frames, g_bambi_anim0_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_soldier_animations[] = {
    { g_soldier_anim3_frames, g_soldier_anim3_script }, { g_soldier_anim6_frames, g_soldier_anim6_script }, { g_soldier_anim4_frames, g_soldier_anim4_script },
    { g_soldier_anim7_frames, g_soldier_anim7_script }, { g_soldier_anim5_frames, g_soldier_anim5_script }, { g_soldier_anim7_frames, g_soldier_anim7_script },
    { g_soldier_anim4_frames, g_soldier_anim4_script }, { g_soldier_anim6_frames, g_soldier_anim6_script }, { g_soldier_anim0_frames, g_soldier_anim0_script },
    { g_soldier_anim1_frames, g_soldier_anim1_script }, { g_soldier_anim2_frames, g_soldier_anim2_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_guard_animations[] = {
    { g_guard_anim4_frames, g_guard_anim4_script }, { g_guard_anim7_frames, g_guard_anim7_script }, { g_guard_anim5_frames, g_guard_anim5_script },
    { g_guard_anim6_frames, g_guard_anim6_script }, { g_guard_anim3_frames, g_guard_anim3_script }, { g_guard_anim6_frames, g_guard_anim6_script },
    { g_guard_anim5_frames, g_guard_anim5_script }, { g_guard_anim7_frames, g_guard_anim7_script }, { g_guard_anim0_frames, g_guard_anim0_script },
    { g_guard_anim2_frames, g_guard_anim2_script }, { g_guard_anim1_frames, g_guard_anim1_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_synth_animations[] = {
    { g_synth_anim6_frames, g_synth_anim6_script }, { g_synth_anim11_frames, g_synth_anim11_script }, { g_synth_anim7_frames, g_synth_anim7_script },
    { g_synth_anim9_frames, g_synth_anim9_script }, { g_synth_anim5_frames, g_synth_anim5_script }, { g_synth_anim10_frames, g_synth_anim10_script },
    { g_synth_anim8_frames, g_synth_anim8_script }, { g_synth_anim12_frames, g_synth_anim12_script }, { g_synth_anim0_frames, g_synth_anim0_script },
    { g_synth_anim1_frames, g_synth_anim1_script }, { g_synth_anim4_frames, g_synth_anim4_script }, { g_synth_anim2_frames, g_synth_anim2_script },
    { g_synth_anim3_frames, g_synth_anim3_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_dog_animations[] = {
    { g_dog_anim4_frames, g_dog_anim4_script }, { g_dog_anim5_frames, g_dog_anim5_script }, { g_dog_anim6_frames, g_dog_anim6_script },
    { g_dog_anim7_frames, g_dog_anim7_script }, { g_dog_anim8_frames, g_dog_anim8_script }, { g_dog_anim7_frames, g_dog_anim7_script },
    { g_dog_anim6_frames, g_dog_anim6_script }, { g_dog_anim5_frames, g_dog_anim5_script }, { g_dog_anim0_frames, g_dog_anim0_script },
    { g_dog_anim1_frames, g_dog_anim1_script }, { g_dog_anim9_frames, g_dog_anim9_script }, { g_dog_anim10_frames, g_dog_anim10_script },
    { g_dog_anim2_frames, g_dog_anim2_script }, { g_dog_anim3_frames, g_dog_anim3_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_queen_animations[] = {
    { g_queen_anim5_frames, g_queen_anim5_script }, { g_queen_anim4_frames, g_queen_anim4_script }, { g_queen_anim4_frames, g_queen_anim4_script },
    { g_queen_anim4_frames, g_queen_anim4_script }, { g_queen_anim5_frames, g_queen_anim5_script }, { g_queen_anim4_frames, g_queen_anim4_script },
    { g_queen_anim4_frames, g_queen_anim4_script }, { g_queen_anim4_frames, g_queen_anim4_script }, { g_queen_anim0_frames, g_queen_anim0_script },
    { g_queen_anim1_frames, g_queen_anim1_script }, { g_queen_anim2_frames, g_queen_anim2_script }, { g_queen_anim3_frames, g_queen_anim3_script },
    { g_queen_anim6_frames, g_queen_anim6_script }, { g_queen_anim10_frames, g_queen_anim10_script }, { g_queen_anim11_frames, g_queen_anim11_script },
    { g_queen_anim7_frames, g_queen_anim7_script }, { g_queen_anim8_frames, g_queen_anim8_script }, { g_queen_anim9_frames, g_queen_anim9_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_colonist_animations[] = {
    { g_colonist_anim3_frames, g_colonist_anim3_script }, { g_colonist_anim3_frames, g_colonist_anim3_script }, { g_colonist_anim4_frames, g_colonist_anim4_script },
    { g_colonist_anim0_frames, g_colonist_anim0_script }, { g_colonist_anim3_frames, g_colonist_anim3_script }, { g_colonist_anim1_frames, g_colonist_anim1_script },
    { g_colonist_anim2_frames, g_colonist_anim2_script }, { g_colonist_anim3_frames, g_colonist_anim3_script }, { g_colonist_anim9_frames, g_colonist_anim9_script },
    { g_colonist_anim9_frames, g_colonist_anim9_script }, { g_colonist_anim8_frames, g_colonist_anim8_script }, { g_colonist_anim5_frames, g_colonist_anim5_script },
    { g_colonist_anim9_frames, g_colonist_anim9_script }, { g_colonist_anim6_frames, g_colonist_anim6_script }, { g_colonist_anim7_frames, g_colonist_anim7_script },
    { g_colonist_anim9_frames, g_colonist_anim9_script }, { g_colonist_anim11_frames, g_colonist_anim11_script }, { g_colonist_anim11_frames, g_colonist_anim11_script },
    { g_colonist_anim10_frames, g_colonist_anim10_script }, { g_colonist_anim12_frames, g_colonist_anim12_script }, { g_colonist_anim11_frames, g_colonist_anim11_script },
    { g_colonist_anim13_frames, g_colonist_anim13_script }, { g_colonist_anim14_frames, g_colonist_anim14_script }, { g_colonist_anim11_frames, g_colonist_anim11_script },
    { g_colonist_anim15_frames, g_colonist_anim15_script }, { g_colonist_anim16_frames, g_colonist_anim16_script }, { g_colonist_anim17_frames, g_colonist_anim17_script },
    { g_colonist_anim19_frames, g_colonist_anim19_script }, { g_colonist_anim19_frames, g_colonist_anim19_script }, { g_colonist_anim18_frames, g_colonist_anim18_script },
    { g_colonist_anim20_frames, g_colonist_anim20_script }, { g_colonist_anim19_frames, g_colonist_anim19_script }, { g_colonist_anim21_frames, g_colonist_anim21_script },
    { g_colonist_anim22_frames, g_colonist_anim22_script }, { g_colonist_anim19_frames, g_colonist_anim19_script }
};
extern void egg_task(TASK *task);
extern void queen_task(TASK *task);
extern void colonist_task(TASK *task);
extern void steam_jet_task(TASK *task);
extern void flame_jet_task(TASK *task);
extern void flame_jet_vertical_task(TASK *task);
extern void steam_jet_vertical_task(TASK *task);
extern void actor_slots_init(ActorSlot *slots, short count);
extern void actor_draw_sprite(Actor *actor);
extern void actor_draw_shadowed(Actor *actor);
extern Sprite g_hugger_upper; /* name: inferred from hugger_overlay_start, the first screen overlay sprite (animation 12) */
extern Sprite g_hugger_lower; /* name: inferred from hugger_overlay_start, the second screen overlay sprite (animation 11) */
extern void creature_task(TASK *node);
extern int actor_hears_player(Actor *actor);
extern short hugger_overlay_start(Actor *actor);
extern void hugger_cling_task(TASK *node);
extern void actor_face_view(Actor *actor);
extern void actor_face_player(Actor *actor);
extern void actor_face_target(Actor *actor);
extern int actor_pick_target_near_player(Actor *actor);
extern int actor_pick_chase_target(Actor *actor);
extern int actor_pick_flee_target(Actor *actor);
extern int actor_pick_target_behind(Actor *actor);
extern void actor_start_attack(TASK *node, Actor *actor);
extern u_int actor_move_x(Actor *actor, u_int x, short y, u_int z);
extern u_int actor_move_z(Actor *actor, u_int x, short y, u_int z);
extern int distance_xz(SVECTOR *, SVECTOR *);
extern void actor_target_toward_player(Actor *actor);
extern void actor_target_away_from_player(Actor *actor);
extern void actor_target_layout_centre(Actor *actor);
extern int egg_release_hugger(Actor *);

/* The actor pool: ACTOR_COUNT records, a free list and the live list (see <ACTOR.H>). */

void actor_free(Actor *actor)
{
    actor->next = g_actor_free;
    g_actor_free = actor;
}

/* name: inferred from its body, which numbers every pool record, frees it and clears the visible list */
void actor_pool_init(void)
{
    int i;
    Actor *actor;
    Actor **visible;
    g_visible_actor_count = 0;
    g_actor_free = 0;
    g_live_actors = 0;
    i = 0;
    visible = g_visible_actors;
    actor = g_actor_pool;
    do {
        actor->index = i;
        actor_free(actor);
        *visible = 0;
        visible++;
        i++;
        actor++;
    } while (i < ACTOR_COUNT);
}

/* name: inferred from its body, which allocates the ACTOR_COUNT pool records */
void actor_pool_alloc(void) { g_actor_pool = (Actor *)heap_alloc(ACTOR_COUNT * sizeof(Actor)); }

Actor *actor_alloc(void)
{
    Actor *first;

    if ((first = g_actor_free))
        g_actor_free = first->next;

    return first;
}

void actor_link(Actor *actor)
{
    Actor *old = g_live_actors;
    if (old) old->prev = actor;
    g_live_actors = actor;
    actor->next = old;
    actor->prev = 0;
}

void actor_unlink(Actor *actor)
{
    Actor *next = actor->next;
    Actor *prev = actor->prev;
    if (next) next->prev = prev;
    if (prev) prev->next = next;
    else g_live_actors = next;
    actor->prev = 0;
    actor->next = 0;
}

/* Multi-statement macro (name invented): a non-zero value below lo becomes lo. Its
 * do { } while (0) block gives retail's register order (health outranks slot for s0). */
#define RAISE_NONZERO(v, lo) do { \
    if ((v) != 0 && (v) < (lo)) \
        (v) = (lo); \
} while (0)

/* name: inferred from its body, which scales the slot's strength by the difficulty into hit points */
short actor_slot_health(ActorSlot *slot)
{
    short health = 0;
    int shift;

    shift = slot->kind == ACTOR_KIND_BOSS ? ACTOR_BOSS_HEALTH_SHIFT : ACTOR_HEALTH_SHIFT;
    switch (get_difficulty()) {
    case DIFFICULTY_RAGING_TERROR:
        if (slot->strength)
            health = (slot->strength + 1) << shift;
        break;
    case DIFFICULTY_XENOMANIA:
        if (slot->strength)
            health = (slot->strength + 2) << shift;
        break;
    case DIFFICULTY_ACID_REIGN:
    default:
        health = slot->strength << shift;
        break;
    }
    RAISE_NONZERO(health, ACTOR_MIN_HEALTH);
    return health;
}

/* Spawns the actor of ActorSlot index and its update task. The K&R definition (short
 * parameter under ACTOR.H's int prototype) gives retail's sign-extending prologue. */
/* name: runtime evidence, analysis/runtime/functions.json */
Actor *actor_spawn(index)
short index;
{
    ActorSlot *slot = &g_actor_slots[index];
    TASK *task = 0;
    Actor *actor = actor_alloc();
    int dx, dz;
    short height_step;

    if (actor == 0)
        return 0;
    actor->position.vx = (slot->grid_x << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_x;
    actor->position.vz = (slot->grid_z << MAP_CELL_SHIFT) + MAP_CELL_HALF - g_level_map.offset_z;
    actor->height = floor_height(actor->position.vx + g_level_map.offset_x, actor->position.vz + g_level_map.offset_z);
    actor->ceiling = ceiling_height(actor->position.vx + g_level_map.offset_x, actor->position.vz + g_level_map.offset_z);
    actor->position.vy = actor->height + ACTOR_FLOOR_CLEARANCE;
    actor->cell = MAP_CELL(actor->position.vx + g_level_map.offset_x, actor->position.vz + g_level_map.offset_z);
    actor->kind = slot->kind;
    actor->health = actor_slot_health(slot);
    if (actor->health != 0) {
        actor->cell->marker = actor->kind;
        actor->cell->occupant = actor->index;
    }
    actor->action = 0;
    actor->idle_timer = ACTOR_WAKE_DELAY;
    actor->stun_timer = ACTOR_SPAWN_STUN;
    actor->slot_index = index;
    actor->engage_cooldown = 0;
    actor->direction = slot->direction;
    actor->shown_direction = ACTOR_DIRECTION_NONE;
    actor->layout_index = actor->cell->layout_index;
    actor->attached_index = slot->attached_index;
    actor->flags = 0;
    actor->view_cone = slot->view_cone;
    actor->reach = slot->reach;
    actor->speed = slot->speed;
    actor->far_range = slot->far_range;
    actor->near_range = slot->near_range;
    actor->last_trigger = 0;
    actor->shot_speed = 0;
    actor->velocity.vx = actor->velocity.vy = actor->velocity.vz = 0;
    switch (actor->kind) {
    case ACTOR_KIND_STEAM_JET:
    case ACTOR_KIND_FLAME_JET:
        break;
    case ACTOR_KIND_STEAM_JET_VERTICAL:
    case ACTOR_KIND_FLAME_JET_VERTICAL:
        break;
    default:
        if (actor->view_cone < ACTOR_VIEW_CONE_MIN)
            actor->view_cone = ACTOR_VIEW_CONE_MIN;
        if (actor->view_cone > ACTOR_VIEW_CONE_MAX)
            actor->view_cone = ACTOR_VIEW_CONE_WIDE;
        if (actor->near_range < ACTOR_NEAR_RANGE_MIN)
            actor->near_range = ACTOR_NEAR_RANGE_MIN;
        break;
    }
    switch (actor->direction) {
    case FACING_NEG_Z:
        actor->angle = 0;
        actor->desired_angle = 0;
        break;
    case FACING_POS_X_NEG_Z:
        actor->desired_angle = actor->angle = (ANGLE_90 / 2);
        break;
    case FACING_POS_X:
        actor->desired_angle = actor->angle = ANGLE_90;
        break;
    case FACING_POS_X_POS_Z:
        actor->desired_angle = actor->angle = (ANGLE_90 / 2) * 3;
        break;
    case FACING_POS_Z:
        actor->desired_angle = actor->angle = ANGLE_180;
        break;
    case FACING_NEG_X_POS_Z:
        actor->desired_angle = actor->angle = (ANGLE_90 / 2) * 5;
        break;
    case FACING_NEG_X:
        actor->desired_angle = actor->angle = (ANGLE_90 * 3);
        break;
    case FACING_NEG_X_NEG_Z:
        actor->desired_angle = actor->angle = (ANGLE_90 / 2) * 7;
        break;
    }
    switch (actor->kind) {
    case ACTOR_KIND_EGG:
        actor->animations = g_egg_animations;
        actor->sprite.slot = 0;
        actor->sprite.flags = 0;
        actor->sprite.scale = EGG_SPRITE_SCALE;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->sprite.clut = g_egg_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, egg_task);
        task->state[0] = (unsigned int)actor;
        task->state[1] = 0;
        actor->far_range = EGG_FAR_RANGE;
        actor->near_range = EGG_NEAR_RANGE;
        actor_play_animation(actor, EGG_ANIM_CLOSED);
        break;
    case ACTOR_KIND_FACEHUGGER:
        actor->animations = g_hugger_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = HUGGER_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_facehugger_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_CHESTBURSTER:
        actor->animations = g_burster_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = BURSTER_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_burster_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_BAMBI:
        actor->animations = g_bambi_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = BAMBI_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_bambi_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_DOG:
        actor->animations = g_dog_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = DOG_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 3;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_dog_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_DOG_CEILING:
        actor->animations = g_dog_ceiling_animations;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->sprite.slot = 0;
        actor->sprite.scale = DOG_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->flags |= ACTOR_FLAG_ON_CEILING;
        actor->sprite.clut = g_dog_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_WARRIOR_CEILING:
        actor->animations = g_alien_ceiling_animations;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->sprite.slot = 0;
        actor->sprite.scale = WARRIOR_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->flags |= ACTOR_FLAG_ON_CEILING;
        actor->sprite.clut = g_alien_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_WARRIOR:
        actor->animations = g_alien_animations;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 3;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->sprite.slot = 0;
        actor->sprite.scale = WARRIOR_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_alien_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_BOSS:
        actor->sprite.clut = g_queen_clut;
        actor->animations = g_queen_animations;
        actor->sprite.slot = &g_page_sets[PAGE_SET_SPRITES].pages[1];
        actor->sprite.scale = QUEEN_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT * 3 / 2;
        actor->scale.vy = ACTOR_SCALE_UNIT * 6;
        actor->scale.vz = ACTOR_SCALE_UNIT * 3 / 2;
        actor->turn_rate = (ANGLE_90 / 2);
        task = TaskCreate(TASK_PRIORITY_ACTORS, queen_task);
        task->state[0] = (unsigned int)actor;
        task->state[1] = 0;
        actor_play_animation(actor, QUEEN_ANIM_DORMANT);
        break;
    case ACTOR_KIND_COLONIST:
        actor->animations = g_colonist_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = COLONIST_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_colonist_clut;
        switch (actor->direction) {
        case FACING_NEG_Z:
            dz = -actor->reach;
            dx = 0;
            break;
        case FACING_POS_X_NEG_Z:
            dz = -actor->reach;
            dx = actor->reach;
            break;
        case FACING_POS_X:
            dz = 0;
            dx = actor->reach;
            break;
        case FACING_POS_X_POS_Z:
            dz = actor->reach;
            dx = actor->reach;
            break;
        case FACING_POS_Z:
            dz = actor->reach;
            dx = 0;
            break;
        case FACING_NEG_X_POS_Z:
            dz = actor->reach;
            dx = -actor->reach;
            break;
        case FACING_NEG_X:
            dz = 0;
            dx = -actor->reach;
            break;
        case FACING_NEG_X_NEG_Z:
            dz = -actor->reach;
            dx = -actor->reach;
            break;
        default:
            dx = 0;
            dz = 0;
            break;
        }
        actor->position.vx += dx;
        actor->position.vz += dz;
        actor_play_animation(actor, COLONIST_ANIM_COCOONED + COLONIST_REACH_OCTANT);
        actor->position.vy = actor->height + COLONIST_HANG_HEIGHT;
        task = TaskCreate(TASK_PRIORITY_ACTORS, colonist_task);
        if (task == 0)
            break;
        task->state[0] = (unsigned int)actor;
        task->state[1] = random() & COLONIST_START_PHASE_MASK;
        task->state[2] = 0;
        task->state[3] = 0;
        break;
    case ACTOR_KIND_GUARD:
        actor->animations = g_guard_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = GUARD_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 3;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_guard_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_SOLDIER:
        actor->animations = g_soldier_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = SOLDIER_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 3;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_soldier_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_SYNTHETIC:
        actor->animations = g_synth_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = SYNTH_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 3;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_synth_clut;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_HANDLER:
        actor->animations = g_handler_animations;
        actor->sprite.slot = 0;
        actor->sprite.scale = HANDLER_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 3;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turn_rate = (ANGLE_90 / 2);
        actor->sprite.clut = g_handler_clut;
        actor->speed >>= 1;
        task = TaskCreate(TASK_PRIORITY_ACTORS, creature_task);
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_INERT:
        break;
    case ACTOR_KIND_STEAM_JET_VERTICAL:
    case ACTOR_KIND_FLAME_JET_VERTICAL:
        if (actor->kind == ACTOR_KIND_STEAM_JET_VERTICAL)
            task = TaskCreate(TASK_PRIORITY_ACTORS, steam_jet_vertical_task);
        else
            task = TaskCreate(TASK_PRIORITY_ACTORS, flame_jet_vertical_task);
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 3;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        height_step = actor->attached_index >= BYTE_SIGN_BIT ? actor->attached_index | BYTE_SIGN_EXTENSION : actor->attached_index;
        if (height_step < 0)
            actor->position.vy = actor->ceiling - ((-height_step + JET_CEILING_STEP_BIAS) << JET_HEIGHT_STEP_SHIFT);
        else
            actor->position.vy = actor->height + (height_step << JET_HEIGHT_STEP_SHIFT);
        actor->stun_timer = 0;
        actor->angle = (actor->angle + ANGLE_180) & ANGLE_MASK;
        if (task == 0)
            break;
        task->state[0] = (unsigned int)actor;
        task->state[1] = 0;
        task->state[2] = -1;
        task->state[3] = 0;
        break;
    case ACTOR_KIND_STEAM_JET:
    case ACTOR_KIND_FLAME_JET:
        if (actor->kind == ACTOR_KIND_FLAME_JET)
            task = TaskCreate(TASK_PRIORITY_ACTORS, flame_jet_task);
        else
            task = TaskCreate(TASK_PRIORITY_ACTORS, steam_jet_task);
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 3;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->position.vy += actor->attached_index << JET_HEIGHT_STEP_SHIFT;
        actor->attached_index = ACTOR_INDEX_NONE;
        actor->stun_timer = 0;
        actor->angle = (actor->angle + ANGLE_180) & ANGLE_MASK;
        if (task == 0)
            break;
        task->state[0] = (unsigned int)actor;
        task->state[1] = 0;
        task->state[2] = -1;
        task->state[3] = 0;
        break;
    }
    if (task == 0) {
        actor_free(actor);
        return 0;
    }
    actor_link(actor);
    actor->task = task;
    return actor;
}

/* Multi-statement macro (name invented) that sets the three scale axes; its
 * do { } while (0) block gives retail's register order in both kind changes. */
#define SET_SCALE(actor, x, y, z) do { \
    (actor)->scale.vx = (x); \
    (actor)->scale.vy = (y); \
    (actor)->scale.vz = (z); \
} while (0)

/* name: inferred from flags bit 3 (actor_move then follows the ceiling) and the kind 5->9, 6->8 swap */
void actor_to_ceiling(Actor *actor, short index)
{
    switch (actor->kind) {
    case ACTOR_KIND_DOG:
        actor->kind = ACTOR_KIND_DOG_CEILING;
        actor->animations = g_dog_ceiling_animations;
        break;
    case ACTOR_KIND_WARRIOR:
        actor->kind = ACTOR_KIND_WARRIOR_CEILING;
        actor->animations = g_alien_ceiling_animations;
        break;
    default:
        return;
    }
    SET_SCALE(actor, ACTOR_SCALE_UNIT, ACTOR_SCALE_UNIT, ACTOR_SCALE_UNIT);
    actor->flags |= ACTOR_FLAG_ON_CEILING;
    if (index != -1)
        actor_play_animation(actor, index);
}

/* name: inferred from it undoing actor_to_ceiling: kind 9->5, 8->6, flags bit 3 cleared */
void actor_to_floor(Actor *actor, short index)
{
    switch (actor->kind) {
    case ACTOR_KIND_DOG_CEILING:
        actor->kind = ACTOR_KIND_DOG;
        actor->animations = g_dog_animations;
        break;
    case ACTOR_KIND_WARRIOR_CEILING:
        actor->kind = ACTOR_KIND_WARRIOR;
        actor->animations = g_alien_animations;
        break;
    default:
        return;
    }
    SET_SCALE(actor, ACTOR_SCALE_UNIT, ACTOR_SCALE_UNIT * 3, ACTOR_SCALE_UNIT);
    actor->flags &= ~ACTOR_FLAG_ON_CEILING;
    if (index != -1)
        actor_play_animation(actor, index);
}

/* name: static evidence, analysis/runtime/functions.json */
void actor_group_add(int amount, int index)
{
    int delta;
    ActorSlot *slot;
    delta = amount & BYTE_MASK;
    if (delta >= BYTE_SIGN_BIT) delta |= BYTE_SIGN_EXTENSION;
    slot = &g_actor_slots[(u_char)index];
    if (((int)((unsigned int)delta << 16)) > 0 && slot->group == slot->group_size) return;
    slot->group = delta + slot->group;
    if (slot->group > slot->group_size) slot->group = slot->group_size;
    if (slot->group == slot->group_size) actor_spawn((u_char)index);
}

/* name: static evidence, analysis/runtime/functions.json */
void actor_spawn_from_object(LevelObject *object, u8 index)
{
    ActorSlot *slot = &g_actor_slots[index];
    Actor *actor;
    u32 x, z;

    slot->group++;
    if (slot->group > slot->group_size)
        slot->group = slot->group_size;
    if (slot->group != slot->group_size)
        return;
    x = (u32)(object->position.vx + g_level_map.offset_x) >> MAP_CELL_SHIFT;
    z = (u32)(object->position.vz + g_level_map.offset_z) >> MAP_CELL_SHIFT;
    slot->grid_x = x;
    slot->grid_z = z;
    actor = actor_spawn(index);
    if (actor->kind == ACTOR_KIND_FACEHUGGER) {
        actor->angle = actor->desired_angle = object->facing << ANGLE_OCTANT_SHIFT;
        actor->shown_direction = actor->direction = object->facing;
        actor->idle_timer = 0;
        actor->flags |= ACTOR_FLAG_ALERTED;
        actor_set_velocity(actor, actor->angle, ACTOR_SPAWN_LEAP_SPEED);
        actor_play_animation(actor, HUGGER_ANIM_LEAP);
        actor->velocity.vy = ACTOR_SPAWN_LEAP_LIFT;
        actor->action = ACTOR_LEAP;
    }
    if (actor->kind == ACTOR_KIND_CHESTBURSTER) {
        actor->angle = actor->desired_angle = object->facing << ANGLE_OCTANT_SHIFT;
        actor->shown_direction = actor->direction = object->facing;
        actor->idle_timer = 0;
        actor->flags |= ACTOR_FLAG_ALERTED;
        actor_set_velocity(actor, actor->angle, ACTOR_SPAWN_LEAP_SPEED);
        actor_play_animation(actor, BURSTER_ANIM_LEAP);
        actor->velocity.vy = ACTOR_SPAWN_LEAP_LIFT;
        actor->action = ACTOR_LEAP;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_slots_init(ActorSlot *slots, short count)
{
    u8 old_kind;

    g_actor_slots = slots;
    g_actor_slot_count = count;
    for (; count; count--, slots++) {
        old_kind = slots->kind;
        slots->kind = g_actor_kind_map[slots->kind];
        if (slots->kind != old_kind) {
            switch (slots->kind) {
            case ACTOR_KIND_WARRIOR:
                slots->far_range = REMAPPED_FAR_RANGE;
                slots->near_range = REMAPPED_NEAR_RANGE;
                slots->speed = (random() & REMAPPED_SPEED_MASK) + REMAPPED_WARRIOR_SPEED;
                break;
            case ACTOR_KIND_FACEHUGGER:
                slots->far_range = (random() & REMAPPED_HUGGER_FAR_RANGE_MASK) + REMAPPED_HUGGER_FAR_RANGE;
                slots->near_range = REMAPPED_NEAR_RANGE;
                slots->speed = (random() & REMAPPED_SPEED_MASK) + REMAPPED_SPEED;
                break;
            case ACTOR_KIND_DOG:
                slots->far_range = REMAPPED_FAR_RANGE;
                slots->near_range = REMAPPED_NEAR_RANGE;
                slots->speed = (random() & REMAPPED_SPEED_MASK) + REMAPPED_SPEED;
                break;
            }
        }
        g_level_map.kind_counts[slots->kind]++;
        if (slots->group == slots->group_size)
            actor_spawn(g_actor_slot_count - count);
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actors_init(void)
{
    actor_pool_init();
    actor_slots_init(g_level_map.actor_slots, g_level_map.actor_slot_count);
}

/* name: inferred from its body, which starts an animation track entry on the actor's sprite */
void actor_play_animation(Actor *actor, s16 index)
{
    TrackEntry *entry = &actor->animations[index];
    u_short *stream = entry->stream;
    volatile Sprite *sprite = &actor->sprite;
    u16 old_flags;
    u16 clut;
    SpriteFrame *first;
    u16 duration;
    sprite->uv.u2 = 0;
    sprite->uv.u0 = 0;
    old_flags = sprite->flags;
    clut = actor->sprite.clut->clut;
    sprite->flags = old_flags & (u16)~SPRITE_SCRIPT_FLAGS;
    sprite->uv.clut = clut;
    first = entry->frames;
    sprite->frame = 0;
    sprite->remaining = 1;
    sprite->frames = first;
    duration = *stream;
    stream++;
    sprite->repeat = 0;
    sprite->script = (short *)stream;
    stream++;
    sprite->loop = (short *)stream;
    sprite->duration = duration;
    sprite_step((Sprite *)sprite);
}

static inline void sprite_bind(SpriteUV *uv, Sprite *sprite, TexturePage *slot)
{
    slot->users++;
    uv->v0 = uv->v1 = slot->y;
    uv->u3 = uv->u1 = sprite->frame->w - 1;
    uv->v3 = uv->v2 = sprite->slot->y + sprite->frame->h - 1;
    uv->tpage = slot->tpage;
}

/* Draws the actor's sprite like actor_draw_sprite but clips its v range at the screen's top and bottom,
 * then draws its semi-transparent shadow quad at the floor under it. */
/* name: runtime evidence, analysis/runtime/functions.json */
void actor_draw_shadowed(Actor *actor)
{
    SpriteFrame *frame = actor->sprite.frame;
    POLY_FT4 *poly;
    POLY_FT4 *shadow;
    Sprite *sprite;
    SpriteUV *uv;
    CVECTOR *color;
    SVECTOR floor;
    int scale;
    int cut;
    u_char u;
    short bottom;
    int top;
    int height;

    g_projected_depth = actor->sprite.depth;
    poly = g_sprite_workspace;
    poly->y1 = poly->y0 = actor->sprite.screen.vy + frame->y * actor->sprite.scale / g_projected_depth;
    bottom = actor->sprite.screen.vy + (frame->y + frame->h) * actor->sprite.scale / g_projected_depth;
    poly->y3 = poly->y2 = bottom;
    sprite = &actor->sprite;
    uv = &actor->sprite.uv;
    if (poly->y0 <= SCREEN_HEIGHT && bottom >= 0) {
        if (actor->sprite.flags & SPRITE_FLAG_MIRRORED) {
            poly->x2 = poly->x0 = actor->sprite.screen.vx + (-frame->x - frame->w) * actor->sprite.scale / g_projected_depth;
            poly->x3 = poly->x1 = actor->sprite.screen.vx + -frame->x * actor->sprite.scale / g_projected_depth;
        } else {
            poly->x2 = poly->x0 = actor->sprite.screen.vx + frame->x * actor->sprite.scale / g_projected_depth;
            poly->x3 = poly->x1 = actor->sprite.screen.vx + (frame->x + frame->w) * actor->sprite.scale / g_projected_depth;
        }
        if (poly->x0 <= SCREEN_WIDTH && poly->x1 >= 0) {
            *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
            *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
            *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
            *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
            height = ABS(poly->v0 - poly->v2);
            top = poly->y0;
            scale = (ABS(top - poly->y2) << ACTOR_CLIP_FRACTION_SHIFT) / height;
            if (scale != 0) {
                if (top < 0) {
                    cut = (-top << ACTOR_CLIP_FRACTION_SHIFT) / scale;
                    poly->y0 = poly->y1 = 0;
                    poly->v0 = poly->v1 = poly->v0 + cut;
                }
                if (poly->y2 > SCREEN_HEIGHT) {
                    cut = ((poly->y2 - SCREEN_HEIGHT) << ACTOR_CLIP_FRACTION_SHIFT) / scale;
                    poly->y2 = poly->y3 = SCREEN_HEIGHT;
                    poly->v2 = poly->v3 = poly->v2 - cut;
                }
            }
            color = &g_palette[actor->cell->palette].colors[0];
            g_transform_result = g_projected_depth;
            if (g_transform_result < ONE) {
                poly->r0 = color->r;
                poly->g0 = color->g;
                poly->b0 = color->b;
            } else {
                g_transform_result -= ONE;
                DpqColor(color, g_transform_result, (CVECTOR *)&poly->r0);
            }
            if (sprite->flags & SPRITE_FLAG_MIRRORED) {
                u = poly->u0;
                poly->u0 = poly->u2 = poly->u1;
                poly->u1 = poly->u3 = u;
            }
            setPolyFT4(poly);
            addPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), poly);
            g_sprite_workspace = poly + 1;
            shadow = g_sprite_workspace;
            floor.vx = actor->position.vx;
            floor.vy = actor->height + ACTOR_FLOOR_CLEARANCE;
            floor.vz = actor->position.vz;
            RotTransPers(&floor, (long *)&floor, &g_gte_interpolation, &g_gte_flag);
            shadow->y1 = shadow->y0 = floor.vy + frame->y * (sprite->scale >> 2) / g_projected_depth;
            shadow->y2 = shadow->y3 = floor.vy + (frame->y + frame->h) * sprite->scale / g_projected_depth;
            if (shadow->y0 <= SCREEN_HEIGHT && shadow->y3 >= 0) {
                if (sprite->flags & SPRITE_FLAG_MIRRORED) {
                    shadow->x2 = poly->x0;
                    shadow->x3 = poly->x3;
                    shadow->x0 = sprite->screen.vx + (-frame->x - frame->w) * (sprite->scale >> 1) / g_projected_depth;
                    shadow->x1 = sprite->screen.vx + -frame->x * (sprite->scale >> 1) / g_projected_depth;
                } else {
                    shadow->x2 = poly->x0;
                    shadow->x3 = poly->x3;
                    shadow->x0 = sprite->screen.vx + frame->x * (sprite->scale >> 1) / g_projected_depth;
                    shadow->x1 = sprite->screen.vx + (frame->x + frame->w) * (sprite->scale >> 1) / g_projected_depth;
                }
                if (shadow->x0 <= SCREEN_WIDTH && shadow->x1 >= 0) {
                    *(u_long *)&shadow->u0 = *(u_long *)&uv->u0;
                    *(u_long *)&shadow->u1 = *(u_long *)&uv->u1;
                    *(u_long *)&shadow->u2 = *(u_long *)&uv->u2;
                    *(u_long *)&shadow->u3 = *(u_long *)&uv->u3;
                    if (sprite->flags & SPRITE_FLAG_MIRRORED) {
                        u = shadow->u0;
                        shadow->u0 = shadow->u2 = shadow->u1;
                        shadow->u1 = shadow->u3 = u;
                    }
                    *(u_long *)&shadow->r0 = 0;
                    setPolyFT4(shadow);
                    setSemiTrans(shadow, 1);
                    addPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), shadow);
                    shadow++;
                    g_sprite_workspace = shadow;
                }
            }
        }
    }
}

/* Draws the actor's sprite as one textured quad, scaled by its depth and lit by its cell's palette. */
/* name: runtime evidence, analysis/runtime/functions.json */
void actor_draw_sprite(Actor *actor)
{
    SpriteFrame *frame = actor->sprite.frame;
    POLY_FT4 *poly;
    Sprite *sprite;
    SpriteUV *uv;
    CVECTOR *color;
    u_char u;
    short bottom;

    g_projected_depth = actor->sprite.depth;
    poly = g_sprite_workspace;
    poly->y1 = poly->y0 = actor->sprite.screen.vy + frame->y * actor->sprite.scale / g_projected_depth;
    bottom = actor->sprite.screen.vy + (frame->y + frame->h) * actor->sprite.scale / g_projected_depth;
    poly->y3 = poly->y2 = bottom;
    sprite = &actor->sprite;
    uv = &actor->sprite.uv;
    if (poly->y0 <= SCREEN_HEIGHT && bottom >= 0) {
        if (actor->sprite.flags & SPRITE_FLAG_MIRRORED) {
            poly->x2 = poly->x0 = actor->sprite.screen.vx + (-frame->x - frame->w) * actor->sprite.scale / g_projected_depth;
            poly->x3 = poly->x1 = actor->sprite.screen.vx + -frame->x * actor->sprite.scale / g_projected_depth;
        } else {
            poly->x2 = poly->x0 = actor->sprite.screen.vx + frame->x * actor->sprite.scale / g_projected_depth;
            poly->x3 = poly->x1 = actor->sprite.screen.vx + (frame->x + frame->w) * actor->sprite.scale / g_projected_depth;
        }
        if (poly->x0 <= SCREEN_WIDTH && poly->x1 >= 0) {
            *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
            *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
            *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
            *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
            if (sprite->flags & SPRITE_FLAG_MIRRORED) {
                u = poly->u0;
                poly->u0 = poly->u2 = poly->u1;
                poly->u1 = poly->u3 = u;
            }
            color = &g_palette[actor->cell->palette].colors[0];
            g_transform_result = g_projected_depth;
            if (g_transform_result < ONE) {
                poly->r0 = color->r;
                poly->g0 = color->g;
                poly->b0 = color->b;
            } else {
                g_transform_result -= ONE;
                DpqColor(color, g_transform_result, (CVECTOR *)&poly->r0);
            }
            setPolyFT4(poly);
            addPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), poly);
            poly++;
            g_sprite_workspace = poly;
        }
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
TexturePage *actor_sprite_upload(Actor *actor)
{
    Sprite *sprite = &actor->sprite;
    SpriteUV *uv = &actor->sprite.uv;
    TexturePage *slot;
    short count;
    int kind;

    kind = actor->kind;
    if (kind < ACTOR_KIND_BAMBI && kind != 0) {
        slot = g_page_sets[PAGE_SET_SMALL_SPRITES].pages;
        count = g_page_sets[PAGE_SET_SMALL_SPRITES].count;
    } else {
        slot = g_page_sets[PAGE_SET_SPRITES].pages;
        count = g_page_sets[PAGE_SET_SPRITES].count;
    }
    for (; count; count--, slot++) {
        if (slot->users == 0) {
            sprite->slot = slot;
            if (sprite_frame_upload(sprite, 0)) {
                sprite_bind(uv, sprite, slot);
                sprite->slot->pixels = sprite->frame->pixels;
                return slot;
            }
            return 0;
        }
    }
    return 0;
}

/* name: runtime evidence, analysis/runtime/functions.json */
int actor_sprite_find_loaded(Actor *actor)
{
    Sprite *sprite = &actor->sprite;
    SpriteUV *uv = &actor->sprite.uv;
    TexturePage *slot;
    short count;
    int kind;

    kind = actor->kind;
    if (kind < ACTOR_KIND_BAMBI && kind != 0) {
        slot = g_page_sets[PAGE_SET_SMALL_SPRITES].pages;
        count = g_page_sets[PAGE_SET_SMALL_SPRITES].count;
    } else {
        slot = g_page_sets[PAGE_SET_SPRITES].pages;
        count = g_page_sets[PAGE_SET_SPRITES].count;
    }
    for (; count; count--, slot++) {
        if (slot->pixels == sprite->frame->pixels) {
            sprite->slot = slot;
            sprite_bind(uv, sprite, slot);
            return -1;
        }
    }
    return 0;
}

/* name: static evidence, analysis/runtime/functions.json */
int boss_sprite_update(Actor *actor)
{
    Sprite *sprite = &actor->sprite;
    TexturePage *slot = sprite->slot;

    if (slot->pixels == sprite->frame->pixels) {
        sprite->uv.v0 = sprite->uv.v1 = slot->y;
        sprite->uv.u3 = sprite->uv.u1 = sprite->frame->w - 1;
        sprite->uv.v3 = sprite->uv.v2 = sprite->slot->y + sprite->frame->h - 1;
        sprite->uv.tpage = slot->tpage;
        return -1;
    }
    if (boss_sprite_frame_upload(sprite) == 0)
        return 0;
    sprite->uv.v0 = sprite->uv.v1 = slot->y;
    sprite->uv.u3 = sprite->uv.u1 = sprite->frame->w - 1;
    sprite->uv.v3 = sprite->uv.v2 = sprite->slot->y + sprite->frame->h - 1;
    sprite->uv.tpage = slot->tpage;
    sprite->slot->pixels = sprite->frame->pixels;
    return -1;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_draw(Actor *actor)
{
    if (actor->action == ACTOR_DEAD) {
        actor_draw_sprite(actor);
        return;
    }
    switch (actor->kind) {
    default:
        if (actor->sprite.depth > ACTOR_SHADOW_DEPTH)
            actor_draw_sprite(actor);
        else
            actor_draw_shadowed(actor);
        break;
    case ACTOR_KIND_EGG:
    case ACTOR_KIND_WARRIOR_CEILING:
    case ACTOR_KIND_DOG_CEILING:
    case ACTOR_KIND_COLONIST:
        actor_draw_sprite(actor);
        break;
    case ACTOR_KIND_BOSS:
        if (actor->task->state[1] != QUEEN_AWAKE || actor->sprite.depth > ACTOR_BOSS_SHADOW_DEPTH)
            actor_draw_sprite(actor);
        else
            actor_draw_shadowed(actor);
        break;
    }
}

/* Task (main.c: TaskCreate(60, actors_draw_task)): draws the visible actors. Pass 1 draws the bosses and the
 * actors whose frame is already in VRAM; pass 2 uploads the others, sharing one slot between actors on the same
 * frame. Each drawn actor is removed from g_visible_actors; the list is cleared afterwards.
 * Each arm clears its own entry: the clears then share the entry address with the load (CSE within
 * the extended basic block), which keeps the address in one register across the calls. */
/* name: static evidence, config/main.json (TaskCreate(60, actors_draw_task)) */
void actors_draw_task(TASK *task)
{
    short count = g_visible_actor_count;
    short i;
    short j;
    Actor *actor;
    TexturePage *slot;

    for (i = 0; i < count; i++) {
        actor = g_visible_actors[i];
        if (actor->kind == ACTOR_KIND_BOSS) {
            if (boss_sprite_update(actor))
                actor_draw(actor);
            g_visible_actors[i] = 0;
            g_visible_actor_count--;
        } else if ((actor->flags & ACTOR_FLAG_NO_CORPSE) && actor->action == ACTOR_DEAD) {
            g_visible_actors[i] = 0;
            g_visible_actor_count--;
        } else if (actor_sprite_find_loaded(actor)) {
            actor_draw(actor);
            g_visible_actors[i] = 0;
            g_visible_actor_count--;
        }
    }
    if (g_visible_actor_count != 0) {
        for (i = 0; i < count; i++) {
            actor = g_visible_actors[i];
            if (actor == 0)
                continue;
            slot = actor_sprite_upload(actor);
            if (slot == 0)
                continue;
            actor_draw(actor);
            g_visible_actor_count--;
            for (j = i + 1; j < count; j++) {
                actor = g_visible_actors[j];
                if (actor == 0)
                    continue;
                if (slot->pixels != actor->sprite.frame->pixels)
                    continue;
                actor->sprite.slot = slot;
                sprite_bind(&actor->sprite.uv, &actor->sprite, slot);
                actor_draw(actor);
                g_visible_actors[j] = 0;
                g_visible_actor_count--;
            }
        }
    }
    g_visible_actor_count = 0;
    g_next_task = task->Next;
}

/* name: inferred from its body, which projects the actor and appends it to g_visible_actors */
void actor_project(Actor *actor)
{
    if (actor->cell->mask & g_visible_cell_mask) {
        g_projected_depth = RotTransPers(&actor->position, (long *)&actor->sprite.screen, &g_gte_interpolation, &g_gte_flag) << 2;
        if ((u_int)(g_projected_depth - (ACTOR_NEAR_DEPTH + 1)) < DRAW_FAR_DEPTH - ACTOR_NEAR_DEPTH - 1 && (u_short)(actor->sprite.screen.vx - ACTOR_CLIP_MIN_X) < ACTOR_CLIP_MAX_X - ACTOR_CLIP_MIN_X + 1
            && (g_projected_depth < ACTOR_CLIP_Y_DEPTH || (u_short)(actor->sprite.screen.vy - ACTOR_CLIP_MIN_Y) < ACTOR_CLIP_MAX_Y - ACTOR_CLIP_MIN_Y + 1)) {
            actor->sprite.depth = g_projected_depth;
            g_visible_actors[g_visible_actor_count++] = actor;
        }
    }
}

/* name: inferred from actor_start_attack: a kind-2 actor next to the player starts two screen overlay sprites */
short hugger_overlay_start(Actor *actor)
{
    TexturePage *slot = g_page_sets[PAGE_SET_SPRITES].pages;
    short count = g_page_sets[PAGE_SET_SPRITES].count;
    TexturePage *first = 0;
    TexturePage *second = 0;
    Sprite *upper;
    Sprite *lower;
    TrackEntry *entry;
    u_short *stream;

    for (; count; count--, slot++) {
        if (slot->users == 0) {
            first = slot;
            break;
        }
    }
    if (first == 0)
        return 0;
    for (count--, slot++; count; count--, slot++) {
        if (slot->users == 0) {
            second = slot;
            break;
        }
    }
    if (second == 0)
        return 0;
    upper = &g_hugger_upper;
    entry = &actor->animations[HUGGER_ANIM_OVERLAY_UPPER];
    stream = entry->stream;
    upper->screen.vx = HUGGER_OVERLAY_X;
    upper->screen.vy = HUGGER_OVERLAY_HIDDEN_Y;
    upper->flags = 0;
    upper->frames = entry->frames;
    upper->frame = 0;
    upper->remaining = 1;
    upper->duration = *stream++;
    upper->script = (short *)stream++;
    upper->loop = (short *)stream;
    sprite_step(upper);
    upper->slot = first;
    lower = &g_hugger_lower;
    entry = &actor->animations[HUGGER_ANIM_OVERLAY_LOWER];
    stream = entry->stream;
    lower->screen.vx = HUGGER_OVERLAY_X;
    lower->screen.vy = HUGGER_OVERLAY_HIDDEN_Y;
    lower->flags = 0;
    lower->frames = entry->frames;
    lower->frame = 0;
    lower->remaining = 1;
    lower->duration = *stream++;
    lower->script = (short *)stream++;
    lower->loop = (short *)stream;
    sprite_step(lower);
    lower->slot = second;
    if (upload_can_queue_pair(upper, lower) == 0)
        return 0;
    sprite_frame_upload(upper, 1);
    upper->uv.u2 = 0;
    upper->uv.u0 = 0;
    upper->uv.v0 = upper->uv.v1 = first->y;
    upper->uv.u3 = upper->uv.u1 = upper->frame->w - 1;
    upper->uv.v3 = upper->uv.v2 = upper->slot->y + upper->frame->h - 1;
    upper->uv.clut = actor->sprite.clut->clut;
    upper->uv.tpage = first->tpage;
    upper->slot->pixels = upper->frame->pixels;
    sprite_frame_upload(lower, 1);
    lower->uv.u2 = 0;
    lower->uv.u0 = 0;
    lower->uv.v0 = lower->uv.v1 = second->y;
    lower->uv.u3 = lower->uv.u1 = lower->frame->w - 1;
    lower->uv.v3 = lower->uv.v2 = lower->slot->y + lower->frame->h - 1;
    lower->uv.clut = actor->sprite.clut->clut;
    lower->uv.tpage = second->tpage;
    lower->slot->pixels = lower->frame->pixels;
    first->users++;
    second->users++;
    first->reserved = 1;
    second->reserved = 1;
    return -1;
}

/* The overlay's frame offsets are added as unsigned halfwords (retail lhu); SpriteFrame's are signed elsewhere. */
/* name: runtime evidence, analysis/runtime/functions.json */
void hugger_overlay_draw(Sprite *sprite, CVECTOR *color)
{
    SpriteFrame *frame = sprite->frame;
    POLY_FT4 *poly = g_sprite_workspace;
    int shake = g_frame_count & HUGGER_OVERLAY_SHAKE_MASK;

    poly->x2 = poly->x0 = sprite->screen.vx + (u_short)frame->x * HUGGER_OVERLAY_FRAME_SCALE;
    poly->y1 = poly->y0 = sprite->screen.vy + shake + (u_short)frame->y * HUGGER_OVERLAY_FRAME_SCALE;
    poly->x3 = poly->x1 = sprite->screen.vx + ((u_short)frame->x + (u_short)frame->w) * HUGGER_OVERLAY_FRAME_SCALE;
    poly->y3 = poly->y2 = sprite->screen.vy + shake + ((u_short)frame->y + (u_short)frame->h) * HUGGER_OVERLAY_FRAME_SCALE;
    *(u_long *)&poly->u0 = *(u_long *)&sprite->uv.u0;
    *(u_long *)&poly->u1 = *(u_long *)&sprite->uv.u1;
    *(u_long *)&poly->u2 = *(u_long *)&sprite->uv.u2;
    *(u_long *)&poly->u3 = *(u_long *)&sprite->uv.u3;
    poly->clut = g_fingers_clut->clut;
    setPolyFT4(poly);
    setRGB0(poly, color->r, color->g, color->b);
    addPrim(g_render_buffer, g_sprite_workspace);
    g_sprite_workspace = poly + 1;
}

/* name: invented; steps one overlay sprite and draws it, uploading a new frame first */
static __inline__ void overlay_step(Sprite *sprite)
{
    if (sprite->slot->pixels != sprite->frame->pixels) {
        if (sprite_frame_upload(sprite, 1)) {
            sprite->uv.u3 = sprite->uv.u1 = sprite->frame->w - 1;
            sprite->uv.v3 = sprite->uv.v2 = sprite->slot->y + sprite->frame->h - 1;
            sprite->slot->pixels = sprite->frame->pixels;
            hugger_overlay_draw(sprite, g_palette->colors);
        }
    } else {
        hugger_overlay_draw(sprite, g_palette->colors);
    }
    sprite_step(sprite);
}

/* name: inferred from its callers in hugger_cling_task: steps and draws the overlay, sliding it on screen */
void hugger_overlay_step(TASK *node)
{
    Sprite *sprite;
    short y;

    sprite = &g_hugger_upper;
    overlay_step(sprite);
    sprite = &g_hugger_lower;
    overlay_step(sprite);
    y = sprite->screen.vy;
    if (node->state[2] == 0) {
        if (y >= HUGGER_OVERLAY_TOP_Y + 1) {
            y -= HUGGER_OVERLAY_RISE_STEP;
            g_hugger_upper.screen.vy = y;
            g_hugger_lower.screen.vy = y;
        }
    } else if (y < HUGGER_OVERLAY_HIDDEN_Y) {
        y += HUGGER_OVERLAY_DROP_STEP;
        g_hugger_upper.screen.vy = y;
        g_hugger_lower.screen.vy = y;
    } else {
        node->state[2] = 2;
    }
}

/* name: inferred from its body: damages the player each tick, then drops the actor beside the player */
void hugger_cling_task(TASK *node)
{
    Actor *actor = (Actor *)node->state[0];
    MapCell *cell;
    u_int x, z;
    short height;
    short distance;

    if (node->state[1] == 0) {
        if (node->state[2] == 2) {
            actor->position.vx = g_player.position.vx + (random() & HUGGER_DROP_SCATTER_MASK) - HUGGER_DROP_SCATTER;
            actor->position.vz = g_player.position.vz + (random() & HUGGER_DROP_SCATTER_MASK) - HUGGER_DROP_SCATTER;
            z = actor->position.vz + g_level_map.offset_z;
            x = actor->position.vx + g_level_map.offset_x;
            cell = MAP_CELL(x, z);
            if (cell->marker == 0 && cell->type < CELL_TYPE_WALL) {
                height = floor_height(x, z);
                distance = ABS(g_player.position.vy - height);
                if (distance < HUGGER_DROP_MAX_STEP) {
                    actor->cell = cell;
                    actor->height = height;
                    actor->ceiling = ceiling_height(x, z);
                    actor->position.vy = height + ACTOR_FLOOR_CLEARANCE;
                    actor_die(actor);
                    node->callback = creature_task;
                    DISABLE_FLAG(PLAYER_HUGGER_ATTACHED);
                    g_hugger_upper.slot->users = 0;
                    g_hugger_lower.slot->users = 0;
                    g_hugger_upper.slot->reserved = 0;
                    g_hugger_lower.slot->reserved = 0;
                }
            }
        } else {
            hugger_overlay_step(node);
            player_damage_health(1, DEATH_FACEHUGGER);
        }
    } else {
        node->state[1]--;
        hugger_overlay_step(node);
        player_damage_health(1, DEATH_FACEHUGGER);
        if (node->state[1] == 0)
            node->state[2] = 1;
    }
    g_next_task = node->Next;
}

/* name: invented; pushes the actor directly away from the player at a quarter of its speed */
static __inline__ void actor_knock_back(Actor *actor)
{
    actor_set_velocity(actor, (actor->player_angle + ANGLE_180) & ANGLE_MASK, actor->speed >> 2);
}

/* name: invented; knocked back on open floor, stopped dead on special cells (type >= CELL_TYPE_SLOPE) */
static __inline__ void actor_recoil(Actor *actor)
{
    if (actor->cell->type < CELL_TYPE_SLOPE)
        actor_knock_back(actor);
    else
        actor_stop(actor);
}

/* name: inferred from action 6, the kill tally and the death animations */
void actor_die(Actor *actor)
{
    actor->action = ACTOR_DYING;
    actor->cell->marker = 0;
    g_player.tallies[actor->kind]++;
    switch (actor->kind) {
    case ACTOR_KIND_COLONIST:
        if (actor->speed != ACTOR_INDEX_NONE)
            g_player.tallies[ACTOR_KIND_CHESTBURSTER]++;
        break;
    case ACTOR_KIND_EGG:
        if (actor->attached_index != ACTOR_INDEX_NONE)
            g_player.tallies[ACTOR_KIND_FACEHUGGER]++;
        break;
    }
    if (actor->kind == ACTOR_KIND_STEAM_JET_VERTICAL || actor->kind == ACTOR_KIND_STEAM_JET || actor->kind == ACTOR_KIND_FLAME_JET_VERTICAL || actor->kind == ACTOR_KIND_FLAME_JET)
        return;
    if (actor->kind != ACTOR_KIND_EGG && actor->kind != ACTOR_KIND_BOSS && actor->attached_index != ACTOR_INDEX_NONE)
        actor_drop_item(actor);
    if (actor->velocity.vy > 0)
        actor->velocity.vy = -actor->velocity.vy;
    switch (actor->kind) {
    case ACTOR_KIND_DOG_CEILING:
        actor_stop(actor);
        actor->kind = ACTOR_KIND_DOG;
        actor->flags &= ~ACTOR_FLAG_ON_CEILING;
        actor_play_animation(actor, DOG_CEILING_ANIM_FALL);
        break;
    case ACTOR_KIND_DOG:
        actor_recoil(actor);
        actor_play_animation(actor, DOG_ANIM_DIE);
        break;
    case ACTOR_KIND_WARRIOR_CEILING:
        actor_stop(actor);
        actor->kind = ACTOR_KIND_WARRIOR;
        actor->flags &= ~ACTOR_FLAG_ON_CEILING;
        actor_play_animation(actor, WARRIOR_CEILING_ANIM_FALL);
        break;
    case ACTOR_KIND_WARRIOR:
    case ACTOR_KIND_SOLDIER:
        actor_recoil(actor);
        actor_play_animation(actor, WARRIOR_ANIM_DIE);
        break;
    case ACTOR_KIND_FACEHUGGER:
        actor_recoil(actor);
        actor_play_animation(actor, HUGGER_ANIM_DIE);
        break;
    case ACTOR_KIND_HANDLER:
        actor_recoil(actor);
        actor_play_animation(actor, HANDLER_ANIM_DIE);
        break;
    case ACTOR_KIND_EGG:
        actor_play_animation(actor, EGG_ANIM_DIE);
        break;
    case ACTOR_KIND_CHESTBURSTER:
        actor_recoil(actor);
        actor_play_animation(actor, BURSTER_ANIM_DIE);
        break;
    case ACTOR_KIND_BAMBI:
        actor_recoil(actor);
        actor_play_animation(actor, BAMBI_ANIM_DIE);
        break;
    case ACTOR_KIND_GUARD:
        actor_recoil(actor);
        actor_play_animation(actor, GUARD_ANIM_DIE);
        break;
    case ACTOR_KIND_SYNTHETIC:
        actor_recoil(actor);
        actor_play_animation(actor, SYNTH_ANIM_DIE);
        break;
    case ACTOR_KIND_BOSS:
        actor_recoil(actor);
        actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
        actor_play_animation(actor, QUEEN_ANIM_DIE);
        break;
    case ACTOR_KIND_COLONIST:
        actor_play_animation(actor, COLONIST_ANIM_DIE);
        break;
    }
}

/* name: inferred from action 5, the 16-tick stun and the hit animations */
void actor_hurt(Actor *actor)
{
    int mode;

    if (actor->kind == ACTOR_KIND_WARRIOR_CEILING || actor->kind == ACTOR_KIND_DOG_CEILING)
        return;
    actor->action = ACTOR_HURT;
    actor->stun_timer = ACTOR_HURT_STUN;
    if (actor->velocity.vy > 0)
        actor->velocity.vy = -actor->velocity.vy;
    switch (actor->kind) {
    case ACTOR_KIND_CHESTBURSTER:
    case ACTOR_KIND_BAMBI:
    case ACTOR_KIND_DOG:
    case ACTOR_KIND_WARRIOR:
        actor_knock_back(actor);
        if (actor->shown_direction == FACING_NEG_Z || actor->shown_direction == FACING_NEG_X_NEG_Z || actor->shown_direction == FACING_POS_X_NEG_Z)
            actor_play_animation(actor, WARRIOR_ANIM_HURT_FRONT);
        else
            actor_play_animation(actor, WARRIOR_ANIM_HURT_BACK);
        break;
    case ACTOR_KIND_WARRIOR_CEILING:
    case ACTOR_KIND_DOG_CEILING:
        actor_knock_back(actor);
        break;
    case ACTOR_KIND_FACEHUGGER:
    case ACTOR_KIND_GUARD:
    case ACTOR_KIND_SOLDIER:
    case ACTOR_KIND_HANDLER:
        actor_knock_back(actor);
        actor_play_animation(actor, GUARD_ANIM_HURT);
        break;
    case ACTOR_KIND_EGG:
        mode = actor->task->state[1];
        if (mode == EGG_SHELL_CLOSED)
            actor_play_animation(actor, EGG_ANIM_HURT_CLOSED);
        else if (mode == EGG_SHELL_OPEN)
            actor_play_animation(actor, EGG_ANIM_HURT_OPEN);
        break;
    case ACTOR_KIND_SYNTHETIC:
        actor_knock_back(actor);
        if (actor->shown_direction == FACING_NEG_Z || actor->shown_direction == FACING_NEG_X_NEG_Z || actor->shown_direction == FACING_POS_X_NEG_Z)
            actor_play_animation(actor, WARRIOR_ANIM_HURT_FRONT);
        else
            actor_play_animation(actor, WARRIOR_ANIM_HURT_BACK);
        break;
    case ACTOR_KIND_BOSS:
        mode = actor->task->state[1];
        if ((u_int)mode < QUEEN_RISING) {
            actor_play_animation(actor, QUEEN_ANIM_HURT);
        } else if (mode == QUEEN_RISING) {
            actor->stun_timer = QUEEN_HURT_STUN;
            break;
        } else {
            actor_knock_back(actor);
            actor_play_animation(actor, QUEEN_ANIM_KNOCKED_BACK);
        }
        actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
        break;
    case ACTOR_KIND_COLONIST:
        if (actor->action != COLONIST_EMPTY)
            actor_play_animation(actor, actor->shown_direction + COLONIST_ANIM_HURT);
        break;
    }
}

/* name: inferred from its body, which reduces health unless stunned or already hurt, then hurts or kills the actor */
void actor_damage(Actor *actor, s16 amount, s16 set_flag) {
    s16 remaining;
    if (actor->stun_timer != 0) return;
    if (actor->action == ACTOR_HURT || actor->action >= ACTOR_HURT) return;
    if (actor->kind == ACTOR_KIND_COLONIST && actor->action == COLONIST_EMPTY) return;
    remaining = actor->health - amount;
    actor->health = remaining;
    if (remaining <= 0) {
    switch (actor->kind) {
    case ACTOR_KIND_FACEHUGGER: case ACTOR_KIND_CHESTBURSTER: case ACTOR_KIND_BAMBI: case ACTOR_KIND_DOG: case ACTOR_KIND_WARRIOR:
    case ACTOR_KIND_WARRIOR_CEILING: case ACTOR_KIND_DOG_CEILING: case ACTOR_KIND_GUARD: case ACTOR_KIND_SOLDIER: case ACTOR_KIND_SYNTHETIC: case ACTOR_KIND_HANDLER:
        if (set_flag != 0) actor->flags |= ACTOR_FLAG_NO_CORPSE;
        break;
    case ACTOR_KIND_BOSS: case ACTOR_KIND_COLONIST:
    default:
        break;
    }
    actor_die(actor);
    } else {
        actor_hurt(actor);
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_face_view(Actor *actor)
{
    short direction = (actor->direction -
        (((actor->player_angle + ANGLE_180 + (ANGLE_90 / 2) / 2) >> ANGLE_OCTANT_SHIFT) & FACING_MASK)) & FACING_MASK;
    if (actor->shown_direction != direction) {
        actor->shown_direction = direction;
        switch (actor->kind) {
        case ACTOR_KIND_EGG:
            break;
        case ACTOR_KIND_BOSS:
            switch (direction) {
            case FACING_NEG_X_POS_Z: case FACING_NEG_X: case FACING_NEG_X_NEG_Z:
                actor->sprite.flags |= SPRITE_FLAG_MIRRORED;
                break;
            default:
                actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
                break;
            }
            actor_play_animation(actor, direction);
            break;
        case ACTOR_KIND_FACEHUGGER: case ACTOR_KIND_CHESTBURSTER: case ACTOR_KIND_BAMBI: case ACTOR_KIND_DOG: case ACTOR_KIND_WARRIOR:
        case ACTOR_KIND_WARRIOR_CEILING: case ACTOR_KIND_DOG_CEILING: case ACTOR_KIND_GUARD: case ACTOR_KIND_SOLDIER: case ACTOR_KIND_HANDLER:
            switch (direction) {
            case FACING_NEG_X_POS_Z: case FACING_NEG_X: case FACING_NEG_X_NEG_Z:
                actor->sprite.flags |= SPRITE_FLAG_MIRRORED;
                break;
            default:
                actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
                break;
            }
            actor_play_animation(actor, direction);
            break;
        case ACTOR_KIND_SYNTHETIC:
            actor_play_animation(actor, direction);
            break;
        default:
            actor_play_animation(actor, direction);
            break;
        }
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_face_player(Actor *actor)
{
    short angle = actor->player_angle;
    actor->shown_direction = ACTOR_DIRECTION_NONE;
    actor->desired_angle = angle;
    actor->angle = angle;
    actor->direction = ((angle + (ANGLE_90 / 2) / 2) >> ANGLE_OCTANT_SHIFT) & FACING_MASK;
    actor_face_view(actor);
}

/* name: static evidence, analysis/runtime/functions.json */
void actor_face_target(Actor *actor)
{
    short angle = actor->target_angle;
    actor->shown_direction = ACTOR_DIRECTION_NONE;
    actor->desired_angle = angle;
    actor->angle = angle;
    actor->direction = ((angle + (ANGLE_90 / 2) / 2) >> ANGLE_OCTANT_SHIFT) & FACING_MASK;
    actor_face_view(actor);
}

/* name: runtime evidence, analysis/runtime/functions.json */
unsigned int actor_clip_x(unsigned int position, unsigned int boundary,
                    short positive, short *offset)
{
    int beyond;
    unsigned int result;
    if (positive) {
        boundary = (boundary & ~MAP_CELL_MASK) + (MAP_CELL_SIZE + ACTOR_RADIUS);
        beyond = boundary < position + *offset;
    } else {
        boundary = (boundary & ~MAP_CELL_MASK) - (ACTOR_RADIUS + 1);
        beyond = boundary > position + *offset;
    }
    result = boundary;
    if (beyond)
        result = position + *offset;
    return result;
}

/* name: runtime evidence, analysis/runtime/functions.json */
unsigned int actor_clip_z(unsigned int position, unsigned int boundary,
                    short positive, short *offset)
{
    int beyond;
    unsigned int result;
    if (positive) {
        boundary = (boundary & ~MAP_CELL_MASK) + (MAP_CELL_SIZE + ACTOR_RADIUS);
        beyond = boundary < position + offset[2];
    } else {
        boundary = (boundary & ~MAP_CELL_MASK) - (ACTOR_RADIUS + 1);
        beyond = boundary > position + offset[2];
    }
    result = boundary;
    if (beyond)
        result = position + offset[2];
    return result;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_move(Actor *actor)
{
    u_int x = actor->position.vx + g_level_map.offset_x;
    u_int z = actor->position.vz + g_level_map.offset_z;
    short y;
    MapCell *cell;
    short ceiling;

    y = actor->height = floor_height(x, z);
    ceiling = ceiling_height(x, z);
    actor->ceiling = ceiling;
    y += ACTOR_FLOOR_CLEARANCE;
    if (actor->flags & ACTOR_FLAG_ON_CEILING)
        y = ceiling - ACTOR_FLOOR_CLEARANCE;
    if (actor->velocity.vy != 0) {
        y = actor->position.vy + actor->velocity.vy;
        if (y > ceiling - actor->scale.vy) {
            actor->velocity.vy = 0;
            y = actor->ceiling - actor->scale.vy;
        } else if (y < actor->height + ACTOR_FLOOR_CLEARANCE) {
            actor->velocity.vy = 0;
            y = actor->height + ACTOR_FLOOR_CLEARANCE;
        } else {
            actor->velocity.vy -= ACTOR_GRAVITY;
            if (actor->velocity.vy == 0)
                actor->velocity.vy = -ACTOR_GRAVITY;
        }
    }
    if (actor->action < ACTOR_DYING)
        actor->cell->marker = 0;
    if (actor->velocity.vx != 0)
        x = actor_move_x(actor, x, y, z);
    if (actor->velocity.vz != 0)
        z = actor_move_z(actor, x, y, z);
    actor->position.vx = x - g_level_map.offset_x;
    actor->position.vy = y;
    actor->position.vz = z - g_level_map.offset_z;
    cell = MAP_CELL(x, z);
    if (cell != actor->cell) {
        actor->cell = cell;
        actor->layout_index = cell->layout_index;
        if (actor->action != ACTOR_DEAD)
            actor_cell_trigger(actor);
    }
    if (actor->action < ACTOR_DYING) {
        cell->marker = actor->kind;
        cell->occupant = actor->index;
    }
}

/* name: inferred from its body: planar distance, capped at DISTANCE_XZ_MAX */
int distance_xz(SVECTOR *left, SVECTOR *right)
{
    int raw_x = left->vx - right->vx;
    int raw_z = left->vz - right->vz;
    unsigned int dx = raw_x < 0 ? -raw_x : raw_x;
    unsigned int dz = raw_z < 0 ? -raw_z : raw_z;

    if (dx >= DISTANCE_XZ_MAX || dz >= DISTANCE_XZ_MAX)
        goto capped;
    goto square;
capped:
    return DISTANCE_XZ_MAX;
square:
    return SquareRoot0(dx * dx + dz * dz);
}

/* name: inferred from its body, which stores the player delta, distance and distance class */
void actor_measure_player(Actor *actor)
{
    int difference;
    unsigned int distance;

    difference = g_player.position.vx - actor->position.vx;
    if (difference < 0)
        difference = actor->position.vx - g_player.position.vx;
    actor->player_delta.vx = difference;
    difference = g_player.position.vy - actor->position.vy;
    if (difference < 0)
        difference = actor->position.vy - g_player.position.vy;
    actor->player_delta.vy = difference;
    difference = g_player.position.vz - actor->position.vz;
    if (difference < 0)
        difference = actor->position.vz - g_player.position.vz;
    actor->player_delta.vz = difference;

    distance = distance_xz(&g_player.position, &actor->position);
    actor->player_distance = distance;
    if (distance < ACTOR_CLOSE_RANGE)
        actor->player_class = ACTOR_RANGE_CLOSE;
    else if (distance < actor->near_range)
        actor->player_class = ACTOR_RANGE_NEAR;
    else if (distance < actor->far_range)
        actor->player_class = ACTOR_RANGE_MID;
    else if (distance < DISTANCE_XZ_MAX)
        actor->player_class = ACTOR_RANGE_FAR;
    else
        actor->player_class = ACTOR_RANGE_OUT;
}

/* name: inferred from its body, which stores the target delta, distance and distance class */
void actor_measure_target(Actor *actor)
{
    int difference;
    unsigned int distance;

    difference = actor->target.vx - actor->position.vx;
    if (difference < 0)
        difference = actor->position.vx - actor->target.vx;
    actor->target_delta.vx = difference;
    difference = actor->target.vy - actor->position.vy;
    if (difference < 0)
        difference = actor->position.vy - actor->target.vy;
    actor->target_delta.vy = difference;
    difference = actor->target.vz - actor->position.vz;
    if (difference < 0)
        difference = actor->position.vz - actor->target.vz;
    actor->target_delta.vz = difference;

    distance = distance_xz(&actor->target, &actor->position);
    actor->target_distance = distance;
    if (distance < ACTOR_CLOSE_RANGE)
        actor->target_class = ACTOR_RANGE_CLOSE;
    else if (distance < actor->near_range)
        actor->target_class = ACTOR_RANGE_NEAR;
    else if (distance < actor->far_range)
        actor->target_class = ACTOR_RANGE_MID;
    else if (distance < DISTANCE_XZ_MAX)
        actor->target_class = ACTOR_RANGE_FAR;
    else
        actor->target_class = ACTOR_RANGE_OUT;
}

/* name: inferred from its body, which returns -1 once every moving axis has covered its distance to the target */
int actor_passed_target(Actor *actor)
{
    int x = ABS(actor->velocity.vx);
    int z = ABS(actor->velocity.vz);

    if (x == 0 && z == 0)
        return 0;
    if (x && z) {
        if (x < actor->target_delta.vx)
            return 0;
        if (z < actor->target_delta.vz)
            return 0;
    } else if (x) {
        if (x < actor->target_delta.vx)
            return 0;
    } else {
        if (z < actor->target_delta.vz)
            return 0;
    }
    return -1;
}

/* name: invented; marks the actor's cell as occupied by it again */
static __inline__ void actor_occupy(Actor *actor)
{
    actor->cell->marker = actor->kind;
    actor->cell->occupant = actor->index;
}

/* name: inferred from its body, which walks two lines along angle for distance and fails on a wall, a marked cell, a low ceiling, or a climb or drop beyond the actor's limits */
int actor_path_clear(Actor *actor, short angle, u_int distance)
{
    u_short climb;
    u_short drop;
    short steps;
    u_int height;
    u_int x, z;
    MapCell *cell;
    short ceiling;
    short floor;

    if (distance > ACTOR_PATH_RANGE)
        return 0;
    PushMatrix();
    g_sight_step.vx = 0;
    g_sight_step.vy = 0;
    g_sight_step.vz = ACTOR_PATH_STEP;
    g_velocity_rotation.vx = 0;
    g_velocity_rotation.vy = (angle + ANGLE_180) & ANGLE_MASK;
    g_velocity_rotation.vz = 0;
    rotate_vector(&g_velocity_rotation, &g_sight_step, &g_sight_step);
    g_sight_point.vx = 0;
    g_sight_point.vy = 0;
    g_sight_point.vz = ACTOR_RADIUS;
    g_8d148.vx = 0;
    g_8d148.vy = 0;
    g_8d148.vz = ACTOR_RADIUS;
    g_velocity_rotation.vy = (angle + ANGLE_90) & ANGLE_MASK;
    rotate_vector_offset(&g_velocity_rotation, &g_sight_point, &actor->position, &g_sight_point);
    g_velocity_rotation.vy = (angle + (ANGLE_90 * 3)) & ANGLE_MASK;
    rotate_vector_offset(&g_velocity_rotation, &g_8d148, &actor->position, &g_8d148);
    PopMatrix();
    climb = ACTOR_MAX_CLIMB;
    drop = ACTOR_MAX_DROP;
    if (actor->flags & ACTOR_FLAG_ON_CEILING) {
        climb = ACTOR_CEILING_MAX_CLIMB;
        drop = ACTOR_CEILING_MAX_DROP;
    }
    steps = distance >> ACTOR_PATH_STEP_SHIFT;
    if (steps < 2)
        steps = 2;
    actor->cell->marker = 0;
    height = actor->position.vy;
    while (steps-- != 0U) {
        x = g_sight_point.vx + g_level_map.offset_x;
        z = g_sight_point.vz + g_level_map.offset_z;
        if (MAP_OUTSIDE(x, z)) {
            actor_occupy(actor);
            return 0;
        }
        cell = MAP_CELL(x, z);
        if ((cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE) || cell->marker != 0) {
            actor_occupy(actor);
            return 0;
        }
        if (cell->type >= CELL_TYPE_SLOPE) {
            height = floor_height(x, z);
            if (actor->kind == ACTOR_KIND_BOSS && (ceiling = ceiling_height(x, z)) < height + ACTOR_BOSS_HEADROOM) {
                actor_occupy(actor);
                return 0;
            }
        } else {
            ceiling = ceiling_height(x, z);
            if (!(actor->flags & ACTOR_FLAG_ON_CEILING)) {
                if (ceiling < height + actor->scale.vy) {
                    actor_occupy(actor);
                    return 0;
                }
                if (actor->kind == ACTOR_KIND_BOSS && ceiling < height + ACTOR_BOSS_HEADROOM) {
                    actor_occupy(actor);
                    return 0;
                }
                floor = floor_height(x, z);
            } else {
                floor = ceiling;
            }
            if (height < floor && floor - height > climb) {
                actor_occupy(actor);
                return 0;
            }
            if (floor < height && height - floor > drop) {
                actor_occupy(actor);
                return 0;
            }
            height = floor;
        }
        x = g_8d148.vx + g_level_map.offset_x;
        z = g_8d148.vz + g_level_map.offset_z;
        if (MAP_OUTSIDE(x, z)) {
            actor_occupy(actor);
            return 0;
        }
        cell = MAP_CELL(x, z);
        if ((cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE) || cell->marker != 0) {
            actor_occupy(actor);
            return 0;
        }
        if (cell->type >= CELL_TYPE_SLOPE) {
            height = floor_height(x, z);
            if (actor->kind == ACTOR_KIND_BOSS && (ceiling = ceiling_height(x, z)) < height + ACTOR_BOSS_HEADROOM) {
                actor_occupy(actor);
                return 0;
            }
        } else {
            ceiling = ceiling_height(x, z);
            if (!(actor->flags & ACTOR_FLAG_ON_CEILING)) {
                if (ceiling < height + actor->scale.vy) {
                    actor_occupy(actor);
                    return 0;
                }
                if (actor->kind == ACTOR_KIND_BOSS && ceiling < height + ACTOR_BOSS_HEADROOM) {
                    actor_occupy(actor);
                    return 0;
                }
                floor = floor_height(x, z);
            } else {
                floor = ceiling;
            }
            if (height < floor && floor - height > climb) {
                actor_occupy(actor);
                return 0;
            }
            if (floor < height && height - floor > drop) {
                actor_occupy(actor);
                return 0;
            }
            height = floor;
        }
        g_sight_point.vx += g_sight_step.vx;
        g_sight_point.vz += g_sight_step.vz;
        g_8d148.vx += g_sight_step.vx;
        g_8d148.vz += g_sight_step.vz;
    }
    actor_occupy(actor);
    return -1;
}

/* name: inferred from the ray it marches from the actor toward the player, failing on walls, floor and ceiling */
int actor_line_of_sight(Actor *actor, short angle, u_int distance)
{
    SVECTOR aim;
    SVECTOR turn;
    short steps;
    u_int x, z;
    MapCell *cell;
    u_int floor;
    u_int ceiling;

    if (distance >= ACTOR_SIGHT_RANGE)
        return 0;
    PushMatrix();
    g_sight_step.vx = 0;
    g_sight_step.vy = 0;
    g_sight_step.vz = ACTOR_PATH_STEP;
    g_sight_point.vx = actor->position.vx;
    g_sight_point.vy = actor->position.vy + ACTOR_EYE_HEIGHT;
    g_sight_point.vz = actor->position.vz;
    aim.vz = point_distance_xz(&g_sight_point, &g_player.position);
    aim.vx = (short)(g_sight_point.vy - ACTOR_SIGHT_AIM_DROP) - g_player.position.vy;
    aim.vx = point_heading_xz(&g_zero_vector, &aim) - ANGLE_180;
    aim.vy = 0;
    aim.vz = 0;
    turn.vx = 0;
    turn.vy = (angle + ANGLE_180) & ANGLE_MASK;
    turn.vz = ANGLE_180;
    rotate_vector(&aim, &g_sight_step, &g_sight_step);
    rotate_vector(&turn, &g_sight_step, &g_sight_step);
    PopMatrix();
    steps = distance >> ACTOR_PATH_STEP_SHIFT;
    if (steps < 2)
        steps = 2;
    actor->cell->marker = 0;
    while (steps-- != 0U) {
        x = g_sight_point.vx + g_level_map.offset_x;
        z = g_sight_point.vz + g_level_map.offset_z;
        if (MAP_OUTSIDE(x, z)) {
            actor_occupy(actor);
            return 0;
        }
        cell = MAP_CELL(x, z);
        if (cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE) {
            actor_occupy(actor);
            return 0;
        }
        floor = floor_height(x, z);
        if (cell->marker != 0) {
            switch (cell->marker) {
            case LEVEL_OBJECT_TARGET:
                floor += OBJECT_HIT_HEIGHT;
                break;
            case LEVEL_OBJECT_EXPLOSIVE:
                floor += OBJECT_HIT_HEIGHT;
                break;
            case LEVEL_OBJECT_TARGET_ALT:
                floor += OBJECT_TALL_HIT_HEIGHT;
                break;
            default:
                actor_occupy(actor);
                return 0;
            }
        }
        if (g_sight_point.vy < floor) {
            actor_occupy(actor);
            return 0;
        }
        ceiling = ceiling_height(x, z);
        if (ceiling < g_sight_point.vy) {
            actor_occupy(actor);
            return 0;
        }
        g_sight_point.vx += g_sight_step.vx;
        g_sight_point.vy += g_sight_step.vy;
        g_sight_point.vz += g_sight_step.vz;
    }
    actor_occupy(actor);
    return -1;
}

/* name: inferred from its body, which steps angle toward desired_angle by turn_rate */
void actor_turn_toward(Actor *actor, int angle)
{
    s16 current;
    s16 target = angle;
    int delta;

    actor->desired_angle = angle;
    current = actor->angle;
    if (current != target) {
        delta = ((u16)(current - angle) << ANGLE_SIGN_SHIFT) >> ANGLE_SIGN_SHIFT;
        if (delta > 0) {
            if (delta < actor->turn_rate)
                actor->angle = target;
            else
                actor->angle = current - actor->turn_rate;
            actor->angle &= ANGLE_MASK;
        } else if (delta < 0) {
            if (-delta < actor->turn_rate)
                actor->angle = target;
            else
                actor->angle = current + actor->turn_rate;
            actor->angle &= ANGLE_MASK;
        } else {
            actor->angle = target;
        }
        actor->direction = ((s16)actor->angle + (ANGLE_90 / 2) / 2 >> ANGLE_OCTANT_SHIFT) & FACING_MASK;
    }
}

/* name: inferred from its body, which stops horizontal movement and any rise */
void actor_stop(Actor *actor)
{
    actor->velocity.vx = 0;
    if (actor->velocity.vy > 0)
        actor->velocity.vy = -actor->velocity.vy;
    actor->velocity.vz = 0;
    actor->shot_speed = 0;
}

/* name: inferred from its body, which sets the horizontal velocity to speed along angle, keeping vy */
void actor_set_velocity(Actor *actor, short angle, short speed)
{
    short old_y = actor->velocity.vy;
    actor->velocity.vx = 0;
    actor->velocity.vz = speed;
    g_velocity_rotation.vx = 0;
    g_velocity_rotation.vy = (angle + ANGLE_180) & ANGLE_MASK;
    g_velocity_rotation.vz = 0;
    PushMatrix();
    rotate_vector(&g_velocity_rotation, &actor->velocity, &actor->velocity);
    PopMatrix();
    actor->velocity.vy = old_y;
}

/* name: runtime evidence, analysis/runtime/functions.json */
int actor_pick_target_near_player(Actor *actor)
{
    short i;
    u_int x, z;
    MapCell *cell;

    g_wander_rotation.vx = g_wander_rotation.vz = 0;
    for (i = 0; i < ACTOR_TARGET_TRIES; i++) {
        g_wander_offset.vx = g_wander_offset.vy = 0;
        g_wander_offset.vz = (random() & ACTOR_TARGET_SPREAD_MASK) + TARGET_NEAR_PLAYER_DISTANCE;
        g_wander_rotation.vy = random() & ANGLE_MASK;
        PushMatrix();
        rotate_vector_offset(&g_wander_rotation, &g_wander_offset, &g_player.position, &actor->target);
        PopMatrix();
        x = actor->target.vx + g_level_map.offset_x;
        z = actor->target.vz + g_level_map.offset_z;
        if (MAP_OUTSIDE(x, z))
            continue;
        cell = MAP_CELL(x, z);
        if (cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE)
            continue;
        if (cell->marker != 0 && cell->marker != CELL_MARKER_PLAYER)
            continue;
        actor_measure_target(actor);
        if (actor->target_class == ACTOR_RANGE_CLOSE || actor->target_class == ACTOR_RANGE_OUT)
            continue;
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance))
            return -1;
    }
    return 0;
}

static __inline__ void place_actor(Actor *actor, int index)
{
    LayoutRecord *layout;
    s32 record_x, record_y;
    u16 offset_x, offset_y;

    layout = &g_level_map.layout_records[index];
    record_x = (s32)layout->x << MAP_CELL_SHIFT;
    offset_x = g_level_map.offset_x;
    actor->target.vx = (layout->flags & 1)
        ? record_x - (offset_x - MAP_CELL_HALF)
        : record_x - offset_x;
    actor->target.vy = 0;
    record_y = (s32)layout->y << MAP_CELL_SHIFT;
    offset_y = g_level_map.offset_z;
    actor->target.vz = (layout->flags & 2)
        ? record_y - (offset_y - MAP_CELL_HALF)
        : record_y - offset_y;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_target_toward_player(Actor *actor)
{
    LayoutRecord *layout = &g_level_map.layout_records[actor->layout_index];
    short best_flow = layout->flow;
    u8 best = actor->layout_index;
    LayoutRecord *next;

    next = &g_level_map.layout_records[layout->links[0]];
    if (best_flow < next->flow) {
        best_flow = next->flow;
        best = layout->links[0];
    }
    if (layout->links[1] != LAYOUT_NO_LINK) {
        next = &g_level_map.layout_records[layout->links[1]];
        if (best_flow < next->flow) {
            best_flow = next->flow;
            best = layout->links[1];
        }
        if (layout->links[2] != LAYOUT_NO_LINK) {
            next = &g_level_map.layout_records[layout->links[2]];
            if (best_flow < next->flow) {
                best_flow = next->flow;
                best = layout->links[2];
            }
            if (layout->links[3] != LAYOUT_NO_LINK) {
                next = &g_level_map.layout_records[layout->links[3]];
                if (best_flow < next->flow) {
                    best_flow = next->flow;
                    best = layout->links[3];
                }
            }
        }
    }
    place_actor(actor, best);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_target_away_from_player(Actor *actor)
{
    LayoutRecord *layout = &g_level_map.layout_records[actor->layout_index];
    short best_flow = layout->flow;
    u8 best = actor->layout_index;
    LayoutRecord *next;

    next = &g_level_map.layout_records[layout->links[0]];
    if (next->flow < best_flow) {
        best_flow = next->flow;
        best = layout->links[0];
    }
    if (layout->links[1] != LAYOUT_NO_LINK) {
        next = &g_level_map.layout_records[layout->links[1]];
        if (next->flow < best_flow) {
            best_flow = next->flow;
            best = layout->links[1];
        }
        if (layout->links[2] != LAYOUT_NO_LINK) {
            next = &g_level_map.layout_records[layout->links[2]];
            if (next->flow < best_flow) {
                best_flow = next->flow;
                best = layout->links[2];
            }
            if (layout->links[3] != LAYOUT_NO_LINK) {
                next = &g_level_map.layout_records[layout->links[3]];
                if (next->flow < best_flow) {
                    best_flow = next->flow;
                    best = layout->links[3];
                }
            }
        }
    }
    place_actor(actor, best);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_target_layout_centre(Actor *actor)
{
    place_actor(actor, actor->layout_index);
}

/* name: runtime evidence, analysis/runtime/functions.json */
int actor_pick_chase_target(Actor *actor)
{
    u_int x, z;
    MapCell *cell;

    g_wander_rotation.vz = 0;
    g_wander_rotation.vx = 0;
    g_wander_offset.vy = 0;
    g_wander_offset.vx = 0;
    actor_target_toward_player(actor);
    actor_measure_target(actor);
    if (actor->target_class == ACTOR_RANGE_CLOSE || actor->target_class >= ACTOR_RANGE_OUT)
        goto fallback;
    x = actor->target.vx + g_level_map.offset_x;
    z = actor->target.vz + g_level_map.offset_z;
    if (MAP_OUTSIDE(x, z))
        goto fallback;
    cell = MAP_CELL(x, z);
    if (!(cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE) && (cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER)) {
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance))
            return -1;
    }
    g_wander_offset.vz = (random() & ACTOR_TARGET_SPREAD_MASK) + TARGET_STEP_DISTANCE;
    g_wander_rotation.vy = random() & ANGLE_MASK;
    PushMatrix();
    rotate_vector_offset(&g_wander_rotation, &g_wander_offset, &actor->target, &actor->target);
    PopMatrix();
    actor_measure_target(actor);
    if (actor->target_class == ACTOR_RANGE_CLOSE || actor->target_class >= ACTOR_RANGE_OUT)
        goto fallback;
    x = actor->target.vx + g_level_map.offset_x;
    z = actor->target.vz + g_level_map.offset_z;
    if (MAP_OUTSIDE(x, z))
        goto fallback;
    cell = MAP_CELL(x, z);
    if (cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE)
        goto fallback;
    if (cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER) {
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance))
            return -1;
    }
fallback:
    actor_target_layout_centre(actor);
    actor_measure_target(actor);
    if (actor->target_class == ACTOR_RANGE_CLOSE || actor->target_class >= ACTOR_RANGE_OUT)
        return 0;
    x = actor->target.vx + g_level_map.offset_x;
    z = actor->target.vz + g_level_map.offset_z;
    if (MAP_OUTSIDE(x, z))
        return 0;
    cell = MAP_CELL(x, z);
    if (!(cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE) && (cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER)) {
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance))
            return -1;
    }
    g_wander_offset.vz = (random() & ACTOR_TARGET_SPREAD_MASK) + TARGET_STEP_DISTANCE;
    g_wander_rotation.vy = random() & ANGLE_MASK;
    PushMatrix();
    rotate_vector_offset(&g_wander_rotation, &g_wander_offset, &actor->target, &actor->target);
    PopMatrix();
    actor_measure_target(actor);
    if (actor->target_class == ACTOR_RANGE_CLOSE || actor->target_class >= ACTOR_RANGE_OUT)
        return 0;
    x = actor->target.vx + g_level_map.offset_x;
    z = actor->target.vz + g_level_map.offset_z;
    if (MAP_OUTSIDE(x, z))
        return 0;
    cell = MAP_CELL(x, z);
    if (cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE)
        return 0;
    if (cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER) {
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance))
            return -1;
    }
    return 0;
}

/* name: runtime evidence, analysis/runtime/functions.json */
int actor_pick_flee_target(Actor *actor)
{
    u_int x, z;
    MapCell *cell;

    g_wander_rotation.vz = 0;
    g_wander_rotation.vx = 0;
    g_wander_offset.vy = 0;
    g_wander_offset.vx = 0;
    actor_target_away_from_player(actor);
    actor_measure_target(actor);
    if (actor->target_class == ACTOR_RANGE_CLOSE || actor->target_class >= ACTOR_RANGE_OUT)
        return 0;
    x = actor->target.vx + g_level_map.offset_x;
    z = actor->target.vz + g_level_map.offset_z;
    if (MAP_OUTSIDE(x, z))
        return 0;
    cell = MAP_CELL(x, z);
    if (!(cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE) && (cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER)) {
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance))
            return -1;
    }
    g_wander_offset.vz = (random() & ACTOR_TARGET_SPREAD_MASK) + TARGET_STEP_DISTANCE;
    g_wander_rotation.vy = random() & ANGLE_MASK;
    PushMatrix();
    rotate_vector_offset(&g_wander_rotation, &g_wander_offset, &actor->target, &actor->target);
    PopMatrix();
    actor_measure_target(actor);
    if (actor->target_class == ACTOR_RANGE_CLOSE || actor->target_class >= ACTOR_RANGE_OUT)
        return 0;
    x = actor->target.vx + g_level_map.offset_x;
    z = actor->target.vz + g_level_map.offset_z;
    if (MAP_OUTSIDE(x, z))
        return 0;
    cell = MAP_CELL(x, z);
    if (cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE)
        return 0;
    if (cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER) {
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance))
            return -1;
    }
    return 0;
}

/* name: inferred from its body, which tests the player against the actor's view cone */
int actor_sees_player(Actor *actor)
{
    s16 delta;

    if (actor->cell->mask & g_visible_cell_mask) {
        delta = ((u16)(actor->angle - actor->player_angle) << ANGLE_SIGN_SHIFT) >> ANGLE_SIGN_SHIFT;
        if (actor->view_cone >= (s16)ABS(delta))
            return 1;
        return 0;
    }
    return 0;
}

/* name: runtime evidence, analysis/runtime/functions.json */
int actor_hears_player(Actor *actor)
{
    MapCell *cell = actor->cell;

    if (cell->face != 0 && (cell->mask & g_visible_cell_mask) == 0)
        return 0;
    if (actor->player_distance < actor->reach >> 1)
        return -1;
    if (actor->player_distance < actor->reach && g_player.noise_timer)
        return -1;
    return 0;
}

/* name: runtime evidence, analysis/runtime/functions.json */
int actor_pick_target_behind(Actor *actor)
{
    short i;
    short heading;
    u_int spread;
    u_int x, z;
    MapCell *cell;

    g_wander_offset.vy = 0;
    g_wander_offset.vx = 0;
    g_wander_rotation.vz = 0;
    g_wander_rotation.vx = 0;
    g_wander_offset.vz = TARGET_BEHIND_DISTANCE;
    PushMatrix();
    for (i = 0; i < ACTOR_TARGET_TRIES; i++) {
        spread = (random() & (ANGLE_90 - 1)) + ANGLE_180;
        heading = (actor->angle + spread - (ANGLE_90 / 2)) & ANGLE_MASK;
        g_wander_rotation.vy = heading;
        rotate_vector_offset(&g_wander_rotation, &g_wander_offset, &actor->position, &actor->target);
        x = actor->target.vx + g_level_map.offset_x;
        z = actor->target.vz + g_level_map.offset_z;
        if (MAP_OUTSIDE(x, z))
            continue;
        cell = MAP_CELL(x, z);
        if (cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE)
            continue;
        if (cell->marker != 0)
            continue;
        actor_measure_target(actor);
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance)) {
            PopMatrix();
            return -1;
        }
    }
    PopMatrix();
    return 0;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_start_attack(TASK *node, Actor *actor)
{
    actor->engage_cooldown = ACTOR_CHARGE_COOLDOWN;
    actor->action = ACTOR_LEAP;
    if (actor->player_class < ACTOR_RANGE_MID) {
        switch (actor->kind) {
        case ACTOR_KIND_FACEHUGGER:
            if (actor->player_class == ACTOR_RANGE_CLOSE && actor->player_delta.vy < ACTOR_ATTACK_HEIGHT + 1
                && !(g_player.flags & PLAYER_HUGGER_ATTACHED) && hugger_overlay_start(actor)) {
                g_player.flags |= PLAYER_HUGGER_ATTACHED;
                node->callback = hugger_cling_task;
                node->state[1] = HUGGER_CLING_TIME;
                node->state[2] = 0;
                actor->cell->marker = 0;
                break;
            }
            actor_set_velocity(actor, actor->angle, HUGGER_LEAP_SPEED);
            actor_play_animation(actor, HUGGER_ANIM_LEAP);
            if (actor->velocity.vy == 0)
                actor->velocity.vy = ACTOR_LEAP_LIFT;
            break;
        case ACTOR_KIND_DOG_CEILING:
            actor_to_floor(actor, WARRIOR_ANIM_HURT_FRONT);
            break;
        case ACTOR_KIND_CHESTBURSTER:
            actor_set_velocity(actor, actor->angle, BURSTER_LEAP_SPEED);
            actor_play_animation(actor, BURSTER_ANIM_LEAP);
            if (actor->velocity.vy == 0)
                actor->velocity.vy = ACTOR_LEAP_LIFT;
            break;
        case ACTOR_KIND_WARRIOR_CEILING:
            actor_to_floor(actor, WARRIOR_ANIM_HURT_BACK);
            break;
        case ACTOR_KIND_BOSS:
            goto random_strike;
        case ACTOR_KIND_BAMBI:
            actor_play_animation(actor, WARRIOR_ANIM_ATTACK);
            break;
        case ACTOR_KIND_GUARD:
        case ACTOR_KIND_SOLDIER:
            actor_play_animation(actor, GUARD_ANIM_FIRE);
            break;
        case ACTOR_KIND_DOG:
        case ACTOR_KIND_SYNTHETIC:
            actor_play_animation(actor, SYNTH_ANIM_ATTACK);
            break;
        case ACTOR_KIND_WARRIOR:
        case ACTOR_KIND_HANDLER:
            actor_play_animation(actor, WARRIOR_ANIM_ATTACK_CLOSE);
            break;
        case ACTOR_KIND_EGG:
        case ACTOR_KIND_FLAME_JET_VERTICAL:
            break;
        }
    } else {
        switch (actor->kind) {
        case ACTOR_KIND_FACEHUGGER:
            actor_set_velocity(actor, actor->angle, HUGGER_LEAP_SPEED);
            actor_play_animation(actor, HUGGER_ANIM_LEAP);
            if (actor->velocity.vy == 0)
                actor->velocity.vy = ACTOR_LEAP_LIFT;
            break;
        case ACTOR_KIND_CHESTBURSTER:
            actor_set_velocity(actor, actor->angle, BURSTER_LEAP_SPEED);
            actor_play_animation(actor, BURSTER_ANIM_LEAP);
            if (actor->velocity.vy == 0)
                actor->velocity.vy = ACTOR_LEAP_LIFT;
            break;
        case ACTOR_KIND_WARRIOR_CEILING:
            actor_to_floor(actor, WARRIOR_ANIM_HURT_BACK);
            break;
        case ACTOR_KIND_BOSS:
        random_strike:
            switch ((short)(random() & QUEEN_STRIKE_MASK)) {
            case 1:
                actor_play_animation(actor, QUEEN_ANIM_STRIKE);
                break;
            case 2:
                actor_play_animation(actor, QUEEN_ANIM_SWIPE);
                break;
            case 3:
                actor_play_animation(actor, QUEEN_ANIM_SWIPE);
                actor->sprite.flags |= SPRITE_FLAG_MIRRORED;
                break;
            default:
                actor_play_animation(actor, QUEEN_ANIM_BITE);
                break;
            case 0:
                actor_play_animation(actor, QUEEN_ANIM_BITE);
                break;
            }
            break;
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_WARRIOR:
            actor_play_animation(actor, WARRIOR_ANIM_ATTACK);
            break;
        case ACTOR_KIND_GUARD:
        case ACTOR_KIND_SOLDIER:
            actor_play_animation(actor, GUARD_ANIM_FIRE);
            break;
        case ACTOR_KIND_SYNTHETIC:
            actor_play_animation(actor, SYNTH_ANIM_ATTACK);
            break;
        case ACTOR_KIND_DOG:
            actor_play_animation(actor, DOG_ANIM_ATTACK);
            break;
        case ACTOR_KIND_HANDLER:
            switch ((short)(random() & HANDLER_ATTACK_MASK)) {
            case 0:
            case 1:
            case 2:
                actor_play_animation(actor, WARRIOR_ANIM_ATTACK_CLOSE);
                break;
            default:
                actor->flags |= ACTOR_FLAG_GRENADE;
                actor_play_animation(actor, HANDLER_ANIM_GRENADE);
                break;
            }
            break;
        case ACTOR_KIND_EGG:
        case ACTOR_KIND_FLAME_JET_VERTICAL:
            break;
        }
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void actor_melee_hit(Actor *actor)
{
    if (actor->player_class < ACTOR_RANGE_MID) {
        switch (actor->kind) {
        case ACTOR_KIND_FACEHUGGER:
        case ACTOR_KIND_CHESTBURSTER:
            if (actor_sees_player(actor) && (actor->player_class == ACTOR_RANGE_CLOSE || actor_path_clear(actor, actor->player_angle, actor->player_distance)))
                player_hit(HUGGER_BITE_DAMAGE, DEATH_FACEHUGGER);
            break;
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_DOG:
            if (actor_sees_player(actor) && (actor->player_class == ACTOR_RANGE_CLOSE || actor_path_clear(actor, actor->player_angle, actor->player_distance)))
                player_hit_from(DOG_BITE_DAMAGE, (actor->angle + ANGLE_180) & ANGLE_MASK, DEATH_DOG);
            break;
        case ACTOR_KIND_WARRIOR:
            if (actor_sees_player(actor) && (actor->player_class == ACTOR_RANGE_CLOSE || actor_path_clear(actor, actor->player_angle, actor->player_distance)))
                player_hit_from(WARRIOR_CLAW_DAMAGE, (actor->angle + ANGLE_180) & ANGLE_MASK, DEATH_WARRIOR);
            break;
        case ACTOR_KIND_BOSS:
            if (actor_sees_player(actor) && (actor->player_class == ACTOR_RANGE_CLOSE || actor_path_clear(actor, actor->player_angle, actor->player_distance)))
                player_hit_from(QUEEN_STRIKE_DAMAGE, (actor->angle + ANGLE_180) & ANGLE_MASK, DEATH_QUEEN);
            break;
        case ACTOR_KIND_EGG:
        case ACTOR_KIND_FLAME_JET_VERTICAL:
            break;
        }
    } else if (actor->player_class == ACTOR_RANGE_MID) {
        switch (actor->kind) {
        case ACTOR_KIND_FACEHUGGER:
        case ACTOR_KIND_CHESTBURSTER:
            if (actor_sees_player(actor) && actor_path_clear(actor, actor->player_angle, actor->player_distance))
                player_hit(HUGGER_BITE_DAMAGE_FAR, DEATH_FACEHUGGER);
            break;
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_DOG:
            if (actor_sees_player(actor) && actor_path_clear(actor, actor->player_angle, actor->player_distance))
                player_hit_from(DOG_BITE_DAMAGE_FAR, (actor->angle + ANGLE_180) & ANGLE_MASK, DEATH_DOG);
            break;
        case ACTOR_KIND_WARRIOR:
            if (actor_sees_player(actor) && actor_path_clear(actor, actor->player_angle, actor->player_distance))
                player_hit_from(WARRIOR_CLAW_DAMAGE_FAR, (actor->angle + ANGLE_180) & ANGLE_MASK, DEATH_WARRIOR);
            break;
        case ACTOR_KIND_BOSS:
            if (actor_sees_player(actor) && actor_path_clear(actor, actor->player_angle, actor->player_distance))
                player_hit_from(QUEEN_STRIKE_DAMAGE_FAR, (actor->angle + ANGLE_180) & ANGLE_MASK, DEATH_QUEEN);
            break;
        case ACTOR_KIND_EGG:
        case ACTOR_KIND_FLAME_JET_VERTICAL:
            break;
        }
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void creature_task(TASK *task)
{
    Actor *actor = (Actor *)task->state[0];
    int clear;

    if (actor->stun_timer != 0)
        actor->stun_timer--;
    if (actor->idle_timer != 0)
        actor->idle_timer--;
    if (actor->engage_cooldown != 0)
        actor->engage_cooldown--;
    actor_move(actor);
    actor_measure_player(actor);
    if (actor->player_class == ACTOR_RANGE_OUT) {
        if ((g_game_flags & GAME_FLAG_HARD) && actor->action == ACTOR_DEAD) {
            actor->health = actor_slot_health(&g_actor_slots[actor->slot_index]);
            actor->flags = 0;
            actor->action = CREATURE_IDLE;
            actor->idle_timer = ACTOR_WAKE_DELAY;
        }
        actor_stop(actor);
        g_next_task = task->Next;
        return;
    }
    if (actor->action != ACTOR_DEAD)
        actor->player_angle = point_heading_xz(&actor->position, &g_player.position) & ANGLE_MASK;
    switch (actor->action) {
    case CREATURE_IDLE:
        if (actor->idle_timer == 0 && CHEAT_IS_ACTIVE(CHEAT_FLAG_ACTORS_WAKE))
            actor->action = CREATURE_ALERT;
        else if (actor->flags & ACTOR_FLAG_ALERTED)
            actor_turn_toward(actor, actor->player_angle);
        actor_face_view(actor);
        break;
    case CREATURE_ALERT:
        if (actor->flags & ACTOR_FLAG_RETREAT) {
            if (actor_pick_flee_target(actor)) {
                actor->action = CREATURE_MOVE;
                break;
            }
            actor->flags &= ~ACTOR_FLAG_RETREAT;
        }
        if (actor->flags & ACTOR_FLAG_ALERTED) {
            if (actor_pick_target_near_player(actor) || actor_pick_chase_target(actor) || actor_pick_target_behind(actor)) {
                actor->action = CREATURE_MOVE;
                break;
            }
            if (actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT && actor->player_delta.vy < ACTOR_ATTACK_HEIGHT + 1
                && (actor->player_class == ACTOR_RANGE_CLOSE
                    || actor_line_of_sight(actor, actor->player_angle, actor->player_distance)))
                actor->action = CREATURE_ENGAGE;
            break;
        }
        if (actor_hears_player(actor)) {
            actor->flags |= ACTOR_FLAG_ALERTED;
            break;
        }
        if (!actor_sees_player(actor))
            break;
        if (actor->player_class == ACTOR_RANGE_CLOSE && actor->player_delta.vy < ACTOR_ATTACK_HEIGHT + 1
            && actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
            actor->action = CREATURE_ENGAGE;
            break;
        }
        if (actor_line_of_sight(actor, actor->player_angle, actor->player_distance)
            || actor_path_clear(actor, actor->player_angle, actor->player_distance))
            actor->flags |= ACTOR_FLAG_ALERTED;
        break;
    case CREATURE_MOVE:
        actor_measure_target(actor);
        if (actor_passed_target(actor)) {
            actor_stop(actor);
            actor->action = CREATURE_ENGAGE;
            break;
        }
        if (!(actor->flags & ACTOR_FLAG_RETREAT) && actor->engage_cooldown == 0) {
            if (actor->player_class == ACTOR_RANGE_CLOSE && actor->player_delta.vy < ACTOR_ATTACK_HEIGHT + 1) {
                actor->engage_cooldown = ACTOR_CHARGE_COOLDOWN;
                actor_stop(actor);
                actor->action = CREATURE_ENGAGE;
                break;
            }
            if (actor->engage_cooldown == 0 && actor->player_class != ACTOR_RANGE_CLOSE && actor->player_class < ACTOR_RANGE_FAR) {
                if (actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT)
                    clear = actor_line_of_sight(actor, actor->player_angle, actor->player_distance);
                else
                    clear = actor_path_clear(actor, actor->player_angle, actor->player_distance);
                if (clear) {
                    actor->engage_cooldown = ACTOR_CHARGE_COOLDOWN;
                    actor_stop(actor);
                    actor->action = CREATURE_ENGAGE;
                    break;
                }
            }
        }
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        actor_turn_toward(actor, actor->target_angle);
        actor_face_view(actor);
        if (actor->angle != actor->desired_angle)
            break;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance)) {
            actor_set_velocity(actor, actor->angle, actor->speed);
        } else {
            actor_stop(actor);
            actor->action = CREATURE_IDLE;
            actor->idle_timer = ACTOR_PAUSE;
        }
        break;
    case CREATURE_ENGAGE:
        if (actor->flags & ACTOR_FLAG_RETREAT) {
            actor->action = CREATURE_ALERT;
            break;
        }
        if (actor->player_class >= ACTOR_RANGE_FAR) {
            actor->action = CREATURE_ALERT;
            break;
        }
        if (actor->player_class == ACTOR_RANGE_CLOSE && actor->player_delta.vy < ACTOR_BITE_HEIGHT + 1
            && actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
            actor_turn_toward(actor, actor->player_angle);
            actor_face_view(actor);
            if (actor->angle == actor->desired_angle)
                actor_start_attack(task, actor);
            break;
        }
        if (actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT) {
            if (actor_line_of_sight(actor, actor->player_angle, actor->player_distance)) {
                actor_turn_toward(actor, actor->player_angle);
                actor_face_view(actor);
                if (actor->angle == actor->desired_angle)
                    actor_start_attack(task, actor);
                break;
            }
        } else if (actor->player_delta.vy < ACTOR_BITE_HEIGHT + 1
                   && actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
            actor_turn_toward(actor, actor->player_angle);
            actor_face_view(actor);
            if (actor->angle == actor->desired_angle)
                actor_start_attack(task, actor);
            break;
        }
        actor->action = CREATURE_ALERT;
        break;
    case ACTOR_LEAP:
        actor_turn_toward(actor, actor->player_angle);
        if (actor->sprite.flags & SPRITE_FLAG_CUE) {
            if (actor->flags & ACTOR_FLAG_GRENADE)
                actor_lob_grenade(actor);
            else
                actor_fire_bullet(actor);
            actor->flags &= ~ACTOR_FLAG_GRENADE;
            actor->sprite.flags &= ~SPRITE_FLAG_CUE;
        }
        if (!(actor->sprite.flags & SPRITE_FLAG_ENDED))
            break;
        actor_stop(actor);
        actor_melee_hit(actor);
        if (actor->kind == ACTOR_KIND_FACEHUGGER) {
            actor->flags |= ACTOR_FLAG_RETREAT;
            actor_face_player(actor);
            actor->action = CREATURE_ALERT;
            break;
        }
        if (actor->player_class < ACTOR_RANGE_FAR) {
            if (actor->player_class == ACTOR_RANGE_CLOSE && actor->player_delta.vy < ACTOR_ATTACK_HEIGHT + 1
                && actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                actor_face_player(actor);
                actor_start_attack(task, actor);
                break;
            }
            if (actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT) {
                if (actor->player_class == ACTOR_RANGE_NEAR
                    && actor_line_of_sight(actor, actor->player_angle, actor->player_distance)) {
                    actor_face_player(actor);
                    actor_start_attack(task, actor);
                    break;
                }
                if (actor_pick_target_near_player(actor)) {
                    actor->engage_cooldown = ACTOR_RETARGET_COOLDOWN;
                    actor_face_target(actor);
                    actor->action = CREATURE_MOVE;
                    break;
                }
                if (actor_line_of_sight(actor, actor->player_angle, actor->player_distance)) {
                    actor_face_player(actor);
                    actor_start_attack(task, actor);
                    break;
                }
            } else {
                if (actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                    actor_face_player(actor);
                    actor_start_attack(task, actor);
                    break;
                }
                if (actor_pick_target_near_player(actor)) {
                    actor->engage_cooldown = ACTOR_RETARGET_COOLDOWN;
                    actor_face_target(actor);
                    actor->action = CREATURE_MOVE;
                    break;
                }
            }
        } else if (actor_pick_target_near_player(actor)) {
            actor_face_target(actor);
            actor->action = CREATURE_MOVE;
            break;
        }
        actor_face_player(actor);
        actor->action = CREATURE_IDLE;
        actor->idle_timer = ACTOR_PAUSE_SHORT;
        break;
    case ACTOR_HURT:
        if (!(actor->sprite.flags & SPRITE_FLAG_ENDED))
            break;
        actor_stop(actor);
        if (actor->kind == ACTOR_KIND_CHESTBURSTER) {
            actor->flags |= ACTOR_FLAG_RETREAT;
            actor_face_player(actor);
            actor->action = CREATURE_ALERT;
            break;
        }
        if (actor->player_class < ACTOR_RANGE_FAR) {
            if (actor->player_class != ACTOR_RANGE_CLOSE || actor->player_delta.vy >= ACTOR_ATTACK_HEIGHT + 1) {
                if (actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT) {
                    if (!actor_line_of_sight(actor, actor->player_angle, actor->player_distance)) {
                    wander:
                        if (actor_pick_target_behind(actor)) {
                            actor->engage_cooldown = ACTOR_CHARGE_COOLDOWN;
                            actor_face_target(actor);
                            actor->action = CREATURE_MOVE;
                            break;
                        }
                        actor_face_player(actor);
                        actor->action = CREATURE_IDLE;
                        actor->idle_timer = ACTOR_PAUSE;
                        break;
                    }
                } else if (!actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                    goto wander;
                }
            }
            actor_face_player(actor);
            actor_start_attack(task, actor);
            break;
        }
        if (actor_pick_target_behind(actor)) {
            actor->engage_cooldown = ACTOR_CHARGE_COOLDOWN;
            actor_face_target(actor);
            actor->action = CREATURE_MOVE;
            break;
        }
        actor_face_player(actor);
        actor->action = CREATURE_IDLE;
        actor->idle_timer = ACTOR_PAUSE;
        break;
    case ACTOR_DYING:
        if (actor->flags & ACTOR_FLAG_NO_CORPSE)
            goto remove;
        if (!(actor->sprite.flags & SPRITE_FLAG_HALTED))
            break;
        actor->action = ACTOR_DEAD;
        actor_stop(actor);
        if (actor->cell->type >= CELL_TYPE_SLOPE) {
            switch (actor->cell->type) {
            case CELL_TYPE_SLOPE:
            case CELL_TYPE_SLOPE + CELL_SLOPE_BAND:
                actor->velocity.vx = 0;
                actor->velocity.vz = -actor->speed;
                break;
            case CELL_TYPE_SLOPE + 1:
            case CELL_TYPE_SLOPE + CELL_SLOPE_BAND + 1:
                actor->velocity.vz = 0;
                actor->velocity.vx = actor->speed;
                break;
            case CELL_TYPE_SLOPE + 2:
            case CELL_TYPE_SLOPE + CELL_SLOPE_BAND + 2:
                actor->velocity.vz = 0;
                actor->velocity.vx = -actor->speed;
                break;
            case CELL_TYPE_SLOPE + 3:
            case CELL_TYPE_SLOPE + CELL_SLOPE_BAND + 3:
                actor->velocity.vx = 0;
                actor->velocity.vz = actor->speed;
                break;
            }
        }
        break;
    case ACTOR_DEAD:
        if (actor->cell->type == CELL_TYPE_DOOR && actor->cell->ceiling - actor->cell->value < DOOR_SHUT_GAP) {
        remove:
            actor_unlink(actor);
            actor_free(actor);
            TaskDie(task);
            return;
        }
        if (actor->cell->type >= CELL_TYPE_SLOPE)
            break;
        actor_stop(actor);
        if (get_difficulty() == DIFFICULTY_ACID_REIGN)
            break;
        switch (actor->kind) {
        case ACTOR_KIND_FACEHUGGER:
        case ACTOR_KIND_CHESTBURSTER:
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_DOG:
        case ACTOR_KIND_WARRIOR:
            if (actor->cell->type == CELL_TYPE_FLOOR)
                actor->cell->type = CELL_TYPE_REMAINS;
            break;
        }
        break;
    }
    if (!(actor->sprite.flags & (SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED)) && actor->action >= CREATURE_MOVE) {
        sprite_step(&actor->sprite);
        if (actor->sprite.flags & SPRITE_FLAG_SOUND)
            sound_play_at_position(actor->sprite.sound, &actor->position);
    }
    actor_project(actor);
    g_next_task = task->Next;
}

/* name: static evidence, analysis/runtime/functions.json */
Actor *actor_spawn_leaping(Actor *actor, short index)
{
    Actor *other;
    ActorSlot *slot;
    unsigned int x, z;
    short angle;
    if (index == ACTOR_INDEX_NONE)
        return 0;
    x = ((unsigned int)(actor->position.vx + g_level_map.offset_x)) >> MAP_CELL_SHIFT;
    slot = &g_actor_slots[index];
    z = ((unsigned int)(actor->position.vz + g_level_map.offset_z)) >> MAP_CELL_SHIFT;
    slot->grid_x = x;
    slot->grid_z = z;
    other = actor_spawn(index);
    if (!other)
        return 0;
    angle = actor->player_angle;
    other->idle_timer = 0;
    other->desired_angle = angle;
    other->angle = angle;
    other->direction = ((angle + (ANGLE_90 / 2) / 2) >> ANGLE_OCTANT_SHIFT) & FACING_MASK;
    other->shown_direction = other->direction;
    other->flags |= ACTOR_FLAG_ALERTED;
    actor_set_velocity(other, other->angle, ACTOR_SPAWN_LEAP_SPEED);
    other->velocity.vy = ACTOR_SPAWN_LEAP_LIFT;
    other->action = ACTOR_LEAP;
    return other;
}

/* name: static evidence, analysis/runtime/functions.json */
int egg_release_hugger(Actor *actor)
{
    Actor *other = actor_spawn_leaping(actor, actor->attached_index);
    actor->attached_index = ACTOR_INDEX_NONE;
    if (other)
        actor_play_animation(other, HUGGER_ANIM_LEAP);
    return -1;
}

/* name: static evidence, analysis/runtime/functions.json */
int colonist_release_burster(Actor *actor)
{
    Actor *other = actor_spawn_leaping(actor, actor->speed);
    actor->speed = ACTOR_INDEX_NONE;
    if (other) {
        actor_play_animation(other, BURSTER_ANIM_EMERGE);
        other->position.vy = other->height + COLONIST_HANG_HEIGHT;
        actor_set_velocity(other, other->angle, BURSTER_EMERGE_SPEED);
        other->velocity.vy = BURSTER_EMERGE_LIFT;
    }
    return -1;
}

typedef TASK ActorUpdateContext;
/* name: runtime evidence, analysis/runtime/functions.json */
void egg_task(ActorUpdateContext *ctx)
{
    Actor *a = (Actor *)ctx->state[0];
    if (a->stun_timer != 0) a->stun_timer--;
    if (a->idle_timer != 0) a->idle_timer--;
    actor_move(a);
    actor_measure_player(a);
    if (a->player_class != ACTOR_RANGE_OUT) {
        if (a->action != ACTOR_DEAD)
            a->player_angle = (s16)(point_heading_xz(&a->position, &g_player.position) & ANGLE_MASK);
        switch (a->action) {
        case EGG_CLOSED:
            if (a->idle_timer == 0 && (a->sprite.flags & SPRITE_FLAG_HALTED)) a->action = EGG_WAKING;
            break;
        case EGG_WAKING:
            if (a->player_class < ACTOR_RANGE_FAR) {
                actor_play_animation(a, EGG_ANIM_OPEN);
                ctx->state[1] = EGG_SHELL_OPENING;
                a->action = EGG_OPENING;
            } else {
                actor_play_animation(a, EGG_ANIM_CLOSED);
                a->action = EGG_CLOSED;
                a->idle_timer = (s16)((random() & EGG_CLOSE_DELAY_MASK) + EGG_CLOSE_DELAY);
            }
            break;
        case EGG_OPENING:
            if (a->sprite.flags & SPRITE_FLAG_HALTED) {
                ctx->state[1] = EGG_SHELL_OPEN;
                a->action = EGG_OPEN;
            }
            break;
        case EGG_OPEN:
            if (a->player_class < ACTOR_RANGE_MID && actor_line_of_sight(a, a->player_angle, a->player_distance) && egg_release_hugger(a)) {
                ctx->state[1] = EGG_SHELL_HATCHED;
                actor_die(a);
            }
            break;
        case ACTOR_HURT:
            if (a->stun_timer == 0) {
                if (ctx->state[1] == EGG_SHELL_OPENING) a->action = EGG_OPENING;
                else if (a->sprite.flags & SPRITE_FLAG_ENDED) {
                    if (ctx->state[1] == EGG_SHELL_CLOSED) {
                        actor_play_animation(a, EGG_ANIM_OPEN);
                        ctx->state[1] = EGG_SHELL_OPENING;
                        a->action = EGG_OPENING;
                    } else if (ctx->state[1] == EGG_SHELL_OPEN) a->action = EGG_OPEN;
                }
            }
            break;
        case ACTOR_DYING:
            if (a->sprite.flags & SPRITE_FLAG_HALTED) a->action = ACTOR_DEAD;
            break;
        case ACTOR_DEAD:
            break;
        default:
            break;
        }
        if ((a->sprite.flags & (SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED)) == 0) {
            sprite_step(&a->sprite);
            if (a->sprite.flags & SPRITE_FLAG_SOUND) sound_play_at_position(a->sprite.sound, &a->position);
        }
        actor_project(a);
    }
    g_next_task = ctx->Next;
}

/* name: inferred from its messages: a flame jet or steam valve actor; it sprays particles while
 * the player is near and reports its shutdown once its slot group is incomplete. Returns -1 while spraying. */
short jet_update(TASK *task, short kind, short period, short speed)
{
    Actor *actor = (Actor *)task->state[0];
    short spraying = 0;
    short steam = 0;
    ActorSlot *slot;
    short phase;

    if (actor->kind == ACTOR_KIND_STEAM_JET_VERTICAL || actor->kind == ACTOR_KIND_STEAM_JET)
        steam = -1;
    if (++task->state[3] > actor->far_range)
        task->state[3] = 0;
    slot = &g_actor_slots[actor->slot_index];
    if (slot->group < slot->group_size) {
        if (!(actor->flags & ACTOR_FLAG_SHUT_DOWN)) {
            actor->flags |= ACTOR_FLAG_SHUT_DOWN;
            g_player.tallies[actor->kind]++;
            if (steam)
                hud_show_message("Steam valve closed");
            else
                hud_show_message("Flame jet shut down");
        }
        return 0;
    }
    if (!(actor->cell->mask & g_visible_cell_mask))
        return 0;
    actor_measure_player(actor);
    if (actor->player_distance > JET_ACTIVE_RANGE)
        return 0;
    switch (actor->action) {
    case JET_IDLE:
        if (actor->health <= 0) {
            task->state[1] = 0;
            task->state[3] = 0;
            task->state[2] = -1;
            actor->action = ACTOR_DEAD;
        }
        break;
    case ACTOR_HURT:
        task->state[1] = 0;
        task->state[3] = 0;
        actor->stun_timer = 0;
        actor->action = JET_IDLE;
        actor->idle_timer = 1;
        break;
    case ACTOR_DYING:
        explosion_spawn(actor->position.vx, actor->position.vy, actor->position.vz);
        actor->action = ACTOR_DEAD;
        break;
    case ACTOR_DEAD:
        phase = task->state[3] - actor->near_range;
        if (phase < 0 || phase > actor->speed) {
            spraying = -1;
            if (task->state[2] == -1)
                task->state[2] = sound_play_at_position(steam ? SOUND_STEAM_JET : SOUND_FLAME_JET, &actor->position);
            if (++task->state[1] > period) {
                task->state[1] = 0;
                if (actor->attached_index >= BYTE_SIGN_BIT)
                    kind = -kind;
                jet_spray(&actor->position, speed, actor->angle, kind);
            }
        }
        break;
    default:
        actor->action = ACTOR_HURT;
        break;
    }
    return spraying;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void steam_jet_task(TASK *node)
{
    if (!jet_update(node, PARTICLE_KIND_STEAM, STEAM_JET_PERIOD, STEAM_JET_SPEED)) {
        if (node->state[2] != -1) {
            sound_stop(node->state[2]);
            node->state[2] = -1;
        }
    }
    g_next_task = node->Next;
}

/* name: static evidence, analysis/runtime/functions.json */
void flame_jet_task(TASK *node)
{
    if (!jet_update(node, PARTICLE_KIND_FIRE, JET_PERIOD, FLAME_JET_SPEED)) {
        sound_stop(node->state[2]);
        node->state[2] = -1;
    }
    g_next_task = node->Next;
}

/* name: static evidence, analysis/runtime/functions.json */
void flame_jet_vertical_task(TASK *node)
{
    if (!jet_update(node, PARTICLE_KIND_FIRE_VERTICAL, JET_PERIOD, FLAME_JET_VERTICAL_SPEED)) {
        sound_stop(node->state[2]);
        node->state[2] = -1;
    }
    g_next_task = node->Next;
}

/* name: static evidence, analysis/runtime/functions.json */
void steam_jet_vertical_task(TASK *node)
{
    if (!jet_update(node, PARTICLE_KIND_STEAM_VERTICAL, JET_PERIOD, STEAM_JET_VERTICAL_SPEED)) {
        sound_stop(node->state[2]);
        node->state[2] = -1;
    }
    g_next_task = node->Next;
}

/* queen_task is the update task of the Queen (kind 7), an actor that ambushes from an idle animation (task->state[1] steps it) */
/* name: runtime evidence, analysis/runtime/functions.json */
void queen_task(TASK *task)
{
    Actor *actor = (Actor *)task->state[0];

    if (actor->stun_timer != 0)
        actor->stun_timer--;
    if (actor->idle_timer != 0)
        actor->idle_timer--;
    if (actor->engage_cooldown != 0)
        actor->engage_cooldown--;
    actor_move(actor);
    actor_measure_player(actor);
    if (actor->player_class == ACTOR_RANGE_OUT) {
        actor_stop(actor);
        g_next_task = task->Next;
        return;
    }
    if (actor->action != ACTOR_DEAD)
        actor->player_angle = point_heading_xz(&actor->position, &g_player.position) & ANGLE_MASK;
    switch (actor->action) {
    case CREATURE_IDLE:
        if (task->state[1] == QUEEN_AWAKE) {
            if (actor->idle_timer == 0 && CHEAT_IS_ACTIVE(CHEAT_FLAG_ACTORS_WAKE))
                actor->action = CREATURE_ALERT;
            actor_face_view(actor);
        } else if (task->state[1] == QUEEN_RISING) {
            if (actor->sprite.flags & SPRITE_FLAG_ENDED) {
                task->state[1] = QUEEN_AWAKE;
                actor_face_view(actor);
            }
        } else if (task->state[1] == QUEEN_WAKING) {
            if (!(actor->sprite.flags & SPRITE_FLAG_ENDED))
                break;
            if (actor->player_class < ACTOR_RANGE_MID) {
                task->state[1] = QUEEN_RISING;
                actor_play_animation(actor, QUEEN_ANIM_RISE);
            } else {
                task->state[1] = QUEEN_DORMANT;
                actor_play_animation(actor, QUEEN_ANIM_DORMANT);
                actor->idle_timer = QUEEN_DORMANT_DELAY;
            }
        } else {
            if (actor->idle_timer == 0 && CHEAT_IS_ACTIVE(CHEAT_FLAG_ACTORS_WAKE) && actor->player_class < ACTOR_RANGE_FAR) {
                task->state[1] = QUEEN_WAKING;
                actor_play_animation(actor, QUEEN_ANIM_WAKE);
            }
        }
        break;
    case CREATURE_ALERT:
        if (actor->flags & ACTOR_FLAG_ALERTED) {
            if (actor_pick_target_near_player(actor) || actor_pick_chase_target(actor) || actor_pick_target_behind(actor))
                actor->action = CREATURE_MOVE;
            break;
        }
        if (actor_hears_player(actor)
            || (actor_sees_player(actor)
                && (actor->player_class == ACTOR_RANGE_CLOSE
                    || actor_path_clear(actor, actor->player_angle, actor->player_distance))))
            actor->flags |= ACTOR_FLAG_ALERTED;
        break;
    case CREATURE_MOVE:
        actor_measure_target(actor);
        if (actor_passed_target(actor)) {
            actor_stop(actor);
            actor->action = CREATURE_ENGAGE;
            break;
        }
        if (actor->engage_cooldown == 0) {
            if (actor->player_class == ACTOR_RANGE_CLOSE
                || (actor->player_class < ACTOR_RANGE_FAR
                    && actor_path_clear(actor, actor->player_angle, actor->player_distance))) {
                actor->engage_cooldown = ACTOR_CHARGE_COOLDOWN;
                actor_stop(actor);
                actor->action = CREATURE_ENGAGE;
                break;
            }
        }
        actor->target_angle = point_heading_xz(&actor->position, &actor->target) & ANGLE_MASK;
        actor_turn_toward(actor, actor->target_angle);
        actor_face_view(actor);
        if (actor->angle != actor->desired_angle)
            break;
        if (actor_path_clear(actor, actor->target_angle, actor->target_distance)) {
            actor_set_velocity(actor, actor->angle, actor->speed);
        } else {
            actor_stop(actor);
            actor->action = CREATURE_IDLE;
            actor->idle_timer = ACTOR_PAUSE;
        }
        break;
    case CREATURE_ENGAGE:
        if (actor->player_class < ACTOR_RANGE_FAR) {
            if (actor->player_class == ACTOR_RANGE_CLOSE) {
                actor_turn_toward(actor, actor->player_angle);
                actor_face_view(actor);
                if (actor->angle == actor->desired_angle)
                    actor_start_attack(task, actor);
                break;
            }
            if (actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                actor_turn_toward(actor, actor->player_angle);
                actor_face_view(actor);
                if (actor->angle == actor->desired_angle)
                    actor_start_attack(task, actor);
                break;
            }
        }
        actor->action = CREATURE_ALERT;
        break;
    case ACTOR_LEAP:
        actor_turn_toward(actor, actor->player_angle);
        if (!(actor->sprite.flags & SPRITE_FLAG_ENDED))
            break;
        actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
        actor_stop(actor);
        actor_melee_hit(actor);
        if (actor->player_class < ACTOR_RANGE_FAR) {
            if (actor->player_class == ACTOR_RANGE_CLOSE
                || actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                actor_face_player(actor);
                actor_start_attack(task, actor);
                break;
            }
            if (actor_pick_target_near_player(actor)) {
                actor->engage_cooldown = ACTOR_RETARGET_COOLDOWN;
                actor_face_target(actor);
                actor->action = CREATURE_MOVE;
                break;
            }
        } else if (actor_pick_target_near_player(actor)) {
            actor_face_target(actor);
            actor->action = CREATURE_MOVE;
            break;
        }
        actor_face_player(actor);
        actor->action = CREATURE_IDLE;
        actor->idle_timer = ACTOR_PAUSE_SHORT;
        break;
    case ACTOR_HURT:
        if (!(actor->sprite.flags & SPRITE_FLAG_ENDED))
            break;
        actor_stop(actor);
        if (task->state[1] == QUEEN_RISING) {
            task->state[1] = QUEEN_AWAKE;
            actor_face_view(actor);
        }
        if (task->state[1] < QUEEN_RISING) {
            task->state[1] = QUEEN_RISING;
            actor_play_animation(actor, QUEEN_ANIM_RISE);
            actor->action = CREATURE_IDLE;
            actor->idle_timer = QUEEN_RECOVER_DELAY;
            break;
        }
        if (actor->player_class < ACTOR_RANGE_FAR) {
            if (actor->player_class == ACTOR_RANGE_CLOSE
                || actor_path_clear(actor, actor->player_angle, actor->player_distance)) {
                actor_face_player(actor);
                actor_start_attack(task, actor);
                break;
            }
            if (actor_pick_target_behind(actor)) {
                actor->engage_cooldown = ACTOR_CHARGE_COOLDOWN;
                actor_face_target(actor);
                actor->action = CREATURE_MOVE;
                break;
            }
            actor_face_player(actor);
            actor->action = CREATURE_IDLE;
            actor->idle_timer = ACTOR_PAUSE;
            break;
        }
        if (actor_pick_target_behind(actor)) {
            actor->engage_cooldown = ACTOR_CHARGE_COOLDOWN;
            actor_face_target(actor);
            actor->action = CREATURE_MOVE;
            break;
        }
        actor_face_player(actor);
        actor->action = CREATURE_IDLE;
        actor->idle_timer = ACTOR_PAUSE;
        break;
    case ACTOR_DYING:
        if (!(actor->sprite.flags & SPRITE_FLAG_HALTED))
            break;
        actor->action = ACTOR_DEAD;
        if (actor->cell->type >= CELL_TYPE_SLOPE) {
            switch (actor->cell->type) {
            case CELL_TYPE_SLOPE:
            case CELL_TYPE_SLOPE + CELL_SLOPE_BAND:
                actor->velocity.vx = 0;
                actor->velocity.vz = -actor->speed;
                break;
            case CELL_TYPE_SLOPE + 1:
            case CELL_TYPE_SLOPE + CELL_SLOPE_BAND + 1:
                actor->velocity.vz = 0;
                actor->velocity.vx = actor->speed;
                break;
            case CELL_TYPE_SLOPE + 2:
            case CELL_TYPE_SLOPE + CELL_SLOPE_BAND + 2:
                actor->velocity.vz = 0;
                actor->velocity.vx = -actor->speed;
                break;
            case CELL_TYPE_SLOPE + 3:
            case CELL_TYPE_SLOPE + CELL_SLOPE_BAND + 3:
                actor->velocity.vx = 0;
                actor->velocity.vz = actor->speed;
                break;
            }
        }
        break;
    case ACTOR_DEAD:
        if (actor->attached_index != ACTOR_INDEX_NONE) {
            level_commands_run(actor->attached_index);
            actor->attached_index = ACTOR_INDEX_NONE;
        }
        if (actor->cell->type == CELL_TYPE_DOOR && actor->cell->ceiling - actor->cell->value < DOOR_SHUT_GAP) {
            actor_unlink(actor);
            actor_free(actor);
            TaskDie(task);
            return;
        }
        if (actor->cell->type >= CELL_TYPE_SLOPE)
            break;
        actor_stop(actor);
        if (get_difficulty() == DIFFICULTY_ACID_REIGN)
            break;
        if (actor->cell->type == CELL_TYPE_FLOOR)
            actor->cell->type = CELL_TYPE_REMAINS;
        break;
    }
    if (!(actor->sprite.flags & (SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED))) {
        sprite_step(&actor->sprite);
        if (actor->sprite.flags & SPRITE_FLAG_SOUND)
            sound_play_at_position(actor->sprite.sound, &actor->position);
    }
    actor_project(actor);
    g_next_task = task->Next;
}

/* colonist_task is the update task of a stationary actor that turns its shown
 * direction toward the player and plays a different animation set by the player's octant */
/* name: static evidence, analysis/runtime/functions.json */
void colonist_task(TASK *task)
{
    Actor *actor = (Actor *)task->state[0];
    short direction;

    actor_measure_player(actor);
    if (actor->player_class == ACTOR_RANGE_OUT) {
        g_next_task = task->Next;
        return;
    }
    if (actor->stun_timer > 0)
        actor->stun_timer--;
    if (actor->idle_timer > 0)
        actor->idle_timer--;
    actor->player_angle = point_heading_xz(&actor->position, &g_player.position) & ANGLE_MASK;
    direction = (actor->direction - (((actor->player_angle + ANGLE_180 + (ANGLE_90 / 2) / 2) >> ANGLE_OCTANT_SHIFT) & FACING_MASK)) & FACING_MASK;
    switch (actor->action) {
    case COLONIST_IDLE:
        if (actor->idle_timer == 0) {
            if (actor->player_class < ACTOR_RANGE_MID && direction == COLONIST_REACH_OCTANT) {
                actor_play_animation(actor, COLONIST_ANIM_BURST);
                actor->action = COLONIST_BURSTING;
                actor->idle_timer = (random() & COLONIST_DELAY_MASK) + COLONIST_BURST_DELAY;
            } else if (actor->player_class < ACTOR_RANGE_FAR) {
                if (direction == COLONIST_REACH_OCTANT)
                    actor_play_animation(actor, COLONIST_ANIM_REACH);
                else
                    sound_play_at_position(SOUND_COLONIST, &actor->position);
                actor->action = COLONIST_REACHING;
                actor->idle_timer = (random() & COLONIST_DELAY_MASK) + COLONIST_REACH_DELAY;
            }
        } else if (actor->shown_direction != direction) {
            actor->shown_direction = direction;
            actor_play_animation(actor, direction + COLONIST_ANIM_COCOONED);
        }
        break;
    case COLONIST_REACHING:
        if ((actor->sprite.flags & SPRITE_FLAG_ENDED) || direction != COLONIST_REACH_OCTANT) {
            actor_play_animation(actor, direction + COLONIST_ANIM_COCOONED);
            actor->action = COLONIST_IDLE;
        }
        break;
    case COLONIST_BURSTING:
        if (actor->sprite.flags & SPRITE_FLAG_ENDED) {
            actor->action = COLONIST_EMPTY;
            colonist_release_burster(actor);
            if (actor->attached_index != ACTOR_INDEX_NONE)
                actor_drop_item(actor);
        } else if (direction != COLONIST_REACH_OCTANT) {
            actor_play_animation(actor, direction + COLONIST_ANIM_COCOONED);
            actor->action = COLONIST_IDLE;
        }
        break;
    case COLONIST_EMPTY:
        if (actor->shown_direction != direction) {
            actor->shown_direction = direction;
            actor_play_animation(actor, direction);
        }
        break;
    case ACTOR_HURT:
        if (actor->sprite.flags & SPRITE_FLAG_ENDED) {
            actor_play_animation(actor, direction + COLONIST_ANIM_COCOONED);
            actor->action = COLONIST_IDLE;
            actor->idle_timer = ACTOR_PAUSE;
        }
        break;
    case ACTOR_DYING:
        if (actor->sprite.flags & SPRITE_FLAG_HALTED) {
            actor->action = ACTOR_DEAD;
            actor_play_animation(actor, direction + COLONIST_ANIM_DEAD);
        }
        break;
    case ACTOR_DEAD:
        if (actor->shown_direction != direction) {
            actor->shown_direction = direction;
            actor_play_animation(actor, direction + COLONIST_ANIM_DEAD);
        }
        break;
    }
    if (!(actor->sprite.flags & (SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED))) {
        sprite_step(&actor->sprite);
        if (actor->sprite.flags & SPRITE_FLAG_SOUND)
            sound_play_at_position(actor->sprite.sound, &actor->position);
    }
    actor_project(actor);
    g_next_task = task->Next;
}
