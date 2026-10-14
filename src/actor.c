/* actor.c -- actors: the actor pool and its slots, spawning, animation, projection and drawing, the
 * facehugger overlay, damage and death, attacks, movement, wandering and line of sight, the
 * per-kind update tasks and the jets */
/* Retail .text from 0x80028804 to at least 0x80030b70. One translation unit: its statics (.sbss
 * 0x8008d140..0x8008d188) are read from Actor_Free (0x80028804) to Actor_UpdateJet (0x8002ff08) and
 * by nothing else, so the code between is one module; the .rdata of its jump tables and strings
 * (0x80011c4c..0x80011f00) follows the function order. Name invented. */
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
#include <TYPES.H>

/* Module statics (.sbss 0x8008d140..0x8008d188), in their retail order. */
static SVECTOR g_sightPoint; /* name: inferred from Actor_LineOfSight, the ray position it steps */
/* name: inferred from Actor_PathClear, the ray along the actor's other side (ACTOR_RADIUS at angle + 270, g_sightPoint takes angle + 90) */
static SVECTOR g_pathSidePoint;
static SVECTOR g_sightStep; /* name: inferred from Actor_LineOfSight, the per-step ray offset */
/* name: inferred from use: the rotation Actor_PathClear and Actor_SetVelocity turn a step or the velocity by */
static SVECTOR g_velocityRotation;
static SVECTOR g_wanderOffset; /* name: inferred from Actor_PickTargetNearPlayer, the random distance to the next target */
static SVECTOR g_wanderRotation; /* name: inferred from Actor_PickTargetNearPlayer, the random heading to the next target */
/* name: inferred from use: Actor_Free pushes and Actor_Alloc pops it */
static actor_t *g_actorFree;
/* name: inferred from use: Actor_InitSlots stores the level's actorSlot_t records */
static actorSlot_t *g_actorSlots;
/* name: runtime evidence, analysis/runtime/globals.json: count of the actorSlot_t records in g_actorSlots */
static short g_actorSlotCount;
/* name: inferred from use: Actor_Project counts the entries of g_visibleActors, Actor_InitPool clears it */
static short g_visibleActorCount;

/* Animation tables of the actor kinds (frame lists and scripts in asm/sprite_anims.s). */
/* name: analysis/runtime/globals.json */
static trackEntry_t g_eggAnimations[] = {
    { g_eggAnim5Frames, g_eggAnim5Script }, { g_eggAnim2Frames, g_eggAnim2Script },
    { g_eggAnim4Frames, g_eggAnim4Script }, { g_eggAnim0Frames, g_eggAnim0Script },
    { g_eggAnim1Frames, g_eggAnim1Script }, { g_eggAnim3Frames, g_eggAnim3Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_huggerAnimations[] = {
    { g_huggerAnim0Frames, g_huggerAnim0Script },  { g_huggerAnim2Frames, g_huggerAnim2Script },
    { g_huggerAnim1Frames, g_huggerAnim1Script },  { g_huggerAnim3Frames, g_huggerAnim3Script },
    { g_huggerAnim4Frames, g_huggerAnim4Script },  { g_huggerAnim3Frames, g_huggerAnim3Script },
    { g_huggerAnim1Frames, g_huggerAnim1Script },  { g_huggerAnim2Frames, g_huggerAnim2Script },
    { g_huggerAnim5Frames, g_huggerAnim5Script },  { g_huggerAnim6Frames, g_huggerAnim6Script },
    { g_huggerAnim7Frames, g_huggerAnim7Script },  { g_fingersAnim1Frames, g_fingersAnim1Script },
    { g_fingersAnim0Frames, g_fingersAnim0Script }
};
/* name: runtime evidence, analysis/runtime/globals.json: animations of every kind-6 (adult alien) actor */
static trackEntry_t g_alienAnimations[] = {
    { g_warriorAnim3Frames, g_warriorAnim3Script }, { g_warriorAnim4Frames, g_warriorAnim4Script },
    { g_warriorAnim5Frames, g_warriorAnim5Script }, { g_warriorAnim6Frames, g_warriorAnim6Script },
    { g_warriorAnim7Frames, g_warriorAnim7Script }, { g_warriorAnim6Frames, g_warriorAnim6Script },
    { g_warriorAnim5Frames, g_warriorAnim5Script }, { g_warriorAnim4Frames, g_warriorAnim4Script },
    { g_warriorAnim0Frames, g_warriorAnim0Script }, { g_warriorAnim1Frames, g_warriorAnim1Script },
    { g_warriorAnim2Frames, g_warriorAnim2Script }, { g_warriorAnim8Frames, g_warriorAnim8Script },
    { g_warriorAnim9Frames, g_warriorAnim9Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_alienCeilingAnimations[] = { { g_warriorCeilingAnim2Frames, g_warriorCeilingAnim2Script },
                                                   { g_warriorCeilingAnim3Frames, g_warriorCeilingAnim3Script },
                                                   { g_warriorCeilingAnim3Frames, g_warriorCeilingAnim3Script },
                                                   { g_warriorCeilingAnim2Frames, g_warriorCeilingAnim2Script },
                                                   { g_warriorCeilingAnim3Frames, g_warriorCeilingAnim3Script },
                                                   { g_warriorCeilingAnim3Frames, g_warriorCeilingAnim3Script },
                                                   { g_warriorCeilingAnim3Frames, g_warriorCeilingAnim3Script },
                                                   { g_warriorCeilingAnim3Frames, g_warriorCeilingAnim3Script },
                                                   { g_warriorCeilingAnim0Frames, g_warriorCeilingAnim0Script },
                                                   { g_warriorCeilingAnim1Frames, g_warriorCeilingAnim1Script },
                                                   { g_warriorCeilingAnim4Frames, g_warriorCeilingAnim4Script } };
/* name: analysis/runtime/globals.json */
static trackEntry_t g_dogCeilingAnimations[] = {
    { g_dogCeilingAnim0Frames, g_dogCeilingAnim0Script }, { g_dogCeilingAnim1Frames, g_dogCeilingAnim1Script },
    { g_dogCeilingAnim1Frames, g_dogCeilingAnim1Script }, { g_dogCeilingAnim0Frames, g_dogCeilingAnim0Script },
    { g_dogCeilingAnim1Frames, g_dogCeilingAnim1Script }, { g_dogCeilingAnim1Frames, g_dogCeilingAnim1Script },
    { g_dogCeilingAnim1Frames, g_dogCeilingAnim1Script }, { g_dogCeilingAnim1Frames, g_dogCeilingAnim1Script },
    { g_dogCeilingAnim2Frames, g_dogCeilingAnim2Script }, { g_dogCeilingAnim3Frames, g_dogCeilingAnim3Script },
    { g_dogCeilingAnim4Frames, g_dogCeilingAnim4Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_handlerAnimations[] = {
    { g_handlerAnim1Frames, g_handlerAnim1Script }, { g_handlerAnim2Frames, g_handlerAnim2Script },
    { g_handlerAnim0Frames, g_handlerAnim0Script }, { g_handlerAnim4Frames, g_handlerAnim4Script },
    { g_handlerAnim3Frames, g_handlerAnim3Script }, { g_handlerAnim4Frames, g_handlerAnim4Script },
    { g_handlerAnim0Frames, g_handlerAnim0Script }, { g_handlerAnim2Frames, g_handlerAnim2Script },
    { g_handlerAnim5Frames, g_handlerAnim5Script }, { g_handlerAnim6Frames, g_handlerAnim6Script },
    { g_handlerAnim7Frames, g_handlerAnim7Script }, { g_handlerAnim8Frames, g_handlerAnim8Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_bursterAnimations[] = {
    { g_bursterAnim9Frames, g_bursterAnim9Script },   { g_bursterAnim6Frames, g_bursterAnim6Script },
    { g_bursterAnim8Frames, g_bursterAnim8Script },   { g_bursterAnim7Frames, g_bursterAnim7Script },
    { g_bursterAnim10Frames, g_bursterAnim10Script }, { g_bursterAnim7Frames, g_bursterAnim7Script },
    { g_bursterAnim8Frames, g_bursterAnim8Script },   { g_bursterAnim6Frames, g_bursterAnim6Script },
    { g_bursterAnim2Frames, g_bursterAnim2Script },   { g_bursterAnim3Frames, g_bursterAnim3Script },
    { g_bursterAnim4Frames, g_bursterAnim4Script },   { g_bursterAnim5Frames, g_bursterAnim5Script },
    { g_bursterAnim0Frames, g_bursterAnim0Script },   { g_bursterAnim1Frames, g_bursterAnim1Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_bambiAnimations[] = {
    { g_bambiAnim4Frames, g_bambiAnim4Script }, { g_bambiAnim6Frames, g_bambiAnim6Script },
    { g_bambiAnim3Frames, g_bambiAnim3Script }, { g_bambiAnim7Frames, g_bambiAnim7Script },
    { g_bambiAnim5Frames, g_bambiAnim5Script }, { g_bambiAnim7Frames, g_bambiAnim7Script },
    { g_bambiAnim3Frames, g_bambiAnim3Script }, { g_bambiAnim6Frames, g_bambiAnim6Script },
    { g_bambiAnim1Frames, g_bambiAnim1Script }, { g_bambiAnim2Frames, g_bambiAnim2Script },
    { g_bambiAnim8Frames, g_bambiAnim8Script }, { g_bambiAnim9Frames, g_bambiAnim9Script },
    { g_bambiAnim0Frames, g_bambiAnim0Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_soldierAnimations[] = {
    { g_soldierAnim3Frames, g_soldierAnim3Script }, { g_soldierAnim6Frames, g_soldierAnim6Script },
    { g_soldierAnim4Frames, g_soldierAnim4Script }, { g_soldierAnim7Frames, g_soldierAnim7Script },
    { g_soldierAnim5Frames, g_soldierAnim5Script }, { g_soldierAnim7Frames, g_soldierAnim7Script },
    { g_soldierAnim4Frames, g_soldierAnim4Script }, { g_soldierAnim6Frames, g_soldierAnim6Script },
    { g_soldierAnim0Frames, g_soldierAnim0Script }, { g_soldierAnim1Frames, g_soldierAnim1Script },
    { g_soldierAnim2Frames, g_soldierAnim2Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_guardAnimations[] = {
    { g_guardAnim4Frames, g_guardAnim4Script }, { g_guardAnim7Frames, g_guardAnim7Script },
    { g_guardAnim5Frames, g_guardAnim5Script }, { g_guardAnim6Frames, g_guardAnim6Script },
    { g_guardAnim3Frames, g_guardAnim3Script }, { g_guardAnim6Frames, g_guardAnim6Script },
    { g_guardAnim5Frames, g_guardAnim5Script }, { g_guardAnim7Frames, g_guardAnim7Script },
    { g_guardAnim0Frames, g_guardAnim0Script }, { g_guardAnim2Frames, g_guardAnim2Script },
    { g_guardAnim1Frames, g_guardAnim1Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_synthAnimations[] = {
    { g_synthAnim6Frames, g_synthAnim6Script }, { g_synthAnim11Frames, g_synthAnim11Script },
    { g_synthAnim7Frames, g_synthAnim7Script }, { g_synthAnim9Frames, g_synthAnim9Script },
    { g_synthAnim5Frames, g_synthAnim5Script }, { g_synthAnim10Frames, g_synthAnim10Script },
    { g_synthAnim8Frames, g_synthAnim8Script }, { g_synthAnim12Frames, g_synthAnim12Script },
    { g_synthAnim0Frames, g_synthAnim0Script }, { g_synthAnim1Frames, g_synthAnim1Script },
    { g_synthAnim4Frames, g_synthAnim4Script }, { g_synthAnim2Frames, g_synthAnim2Script },
    { g_synthAnim3Frames, g_synthAnim3Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_dogAnimations[] = {
    { g_dogAnim4Frames, g_dogAnim4Script }, { g_dogAnim5Frames, g_dogAnim5Script },
    { g_dogAnim6Frames, g_dogAnim6Script }, { g_dogAnim7Frames, g_dogAnim7Script },
    { g_dogAnim8Frames, g_dogAnim8Script }, { g_dogAnim7Frames, g_dogAnim7Script },
    { g_dogAnim6Frames, g_dogAnim6Script }, { g_dogAnim5Frames, g_dogAnim5Script },
    { g_dogAnim0Frames, g_dogAnim0Script }, { g_dogAnim1Frames, g_dogAnim1Script },
    { g_dogAnim9Frames, g_dogAnim9Script }, { g_dogAnim10Frames, g_dogAnim10Script },
    { g_dogAnim2Frames, g_dogAnim2Script }, { g_dogAnim3Frames, g_dogAnim3Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_queenAnimations[] = {
    { g_queenAnim5Frames, g_queenAnim5Script },   { g_queenAnim4Frames, g_queenAnim4Script },
    { g_queenAnim4Frames, g_queenAnim4Script },   { g_queenAnim4Frames, g_queenAnim4Script },
    { g_queenAnim5Frames, g_queenAnim5Script },   { g_queenAnim4Frames, g_queenAnim4Script },
    { g_queenAnim4Frames, g_queenAnim4Script },   { g_queenAnim4Frames, g_queenAnim4Script },
    { g_queenAnim0Frames, g_queenAnim0Script },   { g_queenAnim1Frames, g_queenAnim1Script },
    { g_queenAnim2Frames, g_queenAnim2Script },   { g_queenAnim3Frames, g_queenAnim3Script },
    { g_queenAnim6Frames, g_queenAnim6Script },   { g_queenAnim10Frames, g_queenAnim10Script },
    { g_queenAnim11Frames, g_queenAnim11Script }, { g_queenAnim7Frames, g_queenAnim7Script },
    { g_queenAnim8Frames, g_queenAnim8Script },   { g_queenAnim9Frames, g_queenAnim9Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_colonistAnimations[] = {
    { g_colonistAnim3Frames, g_colonistAnim3Script },   { g_colonistAnim3Frames, g_colonistAnim3Script },
    { g_colonistAnim4Frames, g_colonistAnim4Script },   { g_colonistAnim0Frames, g_colonistAnim0Script },
    { g_colonistAnim3Frames, g_colonistAnim3Script },   { g_colonistAnim1Frames, g_colonistAnim1Script },
    { g_colonistAnim2Frames, g_colonistAnim2Script },   { g_colonistAnim3Frames, g_colonistAnim3Script },
    { g_colonistAnim9Frames, g_colonistAnim9Script },   { g_colonistAnim9Frames, g_colonistAnim9Script },
    { g_colonistAnim8Frames, g_colonistAnim8Script },   { g_colonistAnim5Frames, g_colonistAnim5Script },
    { g_colonistAnim9Frames, g_colonistAnim9Script },   { g_colonistAnim6Frames, g_colonistAnim6Script },
    { g_colonistAnim7Frames, g_colonistAnim7Script },   { g_colonistAnim9Frames, g_colonistAnim9Script },
    { g_colonistAnim11Frames, g_colonistAnim11Script }, { g_colonistAnim11Frames, g_colonistAnim11Script },
    { g_colonistAnim10Frames, g_colonistAnim10Script }, { g_colonistAnim12Frames, g_colonistAnim12Script },
    { g_colonistAnim11Frames, g_colonistAnim11Script }, { g_colonistAnim13Frames, g_colonistAnim13Script },
    { g_colonistAnim14Frames, g_colonistAnim14Script }, { g_colonistAnim11Frames, g_colonistAnim11Script },
    { g_colonistAnim15Frames, g_colonistAnim15Script }, { g_colonistAnim16Frames, g_colonistAnim16Script },
    { g_colonistAnim17Frames, g_colonistAnim17Script }, { g_colonistAnim19Frames, g_colonistAnim19Script },
    { g_colonistAnim19Frames, g_colonistAnim19Script }, { g_colonistAnim18Frames, g_colonistAnim18Script },
    { g_colonistAnim20Frames, g_colonistAnim20Script }, { g_colonistAnim19Frames, g_colonistAnim19Script },
    { g_colonistAnim21Frames, g_colonistAnim21Script }, { g_colonistAnim22Frames, g_colonistAnim22Script },
    { g_colonistAnim19Frames, g_colonistAnim19Script }
};
extern void Actor_EggTask( TASK *task );
extern void Actor_QueenTask( TASK *task );
extern void Actor_ColonistTask( TASK *task );
extern void Actor_SteamJetTask( TASK *task );
extern void Actor_FlameJetTask( TASK *task );
extern void Actor_FlameJetVerticalTask( TASK *task );
extern void Actor_SteamJetVerticalTask( TASK *task );
extern void Actor_InitSlots( actorSlot_t *slots, short count );
extern void Actor_DrawSprite( actor_t *actor );
extern void Actor_DrawShadowed( actor_t *actor );
extern sprite_t g_huggerUpper; /* name: inferred from Actor_StartHuggerOverlay, the first screen overlay sprite (animation 12) */
extern sprite_t g_huggerLower; /* name: inferred from Actor_StartHuggerOverlay, the second screen overlay sprite (animation 11) */
extern void Actor_CreatureTask( TASK *node );
extern int Actor_HearsPlayer( actor_t *actor );
extern short Actor_StartHuggerOverlay( actor_t *actor );
extern void Actor_HuggerClingTask( TASK *node );
extern void Actor_FaceView( actor_t *actor );
extern void Actor_FacePlayer( actor_t *actor );
extern void Actor_FaceTarget( actor_t *actor );
extern int Actor_PickTargetNearPlayer( actor_t *actor );
extern int Actor_PickChaseTarget( actor_t *actor );
extern int Actor_PickFleeTarget( actor_t *actor );
extern int Actor_PickTargetBehind( actor_t *actor );
extern void Actor_StartAttack( TASK *node, actor_t *actor );
extern u_int Actor_MoveX( actor_t *actor, u_int x, short y, u_int z );
extern u_int Actor_MoveZ( actor_t *actor, u_int x, short y, u_int z );
extern int Actor_DistanceXZ( SVECTOR *, SVECTOR * );
extern void Actor_TargetTowardPlayer( actor_t *actor );
extern void Actor_TargetAwayFromPlayer( actor_t *actor );
extern void Actor_TargetLayoutCentre( actor_t *actor );
extern int Actor_EggReleaseHugger( actor_t * );

/* The actor pool: ACTOR_COUNT records, a free list and the live list (see <ACTOR.H>). */

/*
==================
Actor_Free
==================
*/
void Actor_Free( actor_t *actor )
{
    actor->next = g_actorFree;
    g_actorFree = actor;
}

/*
==================
Actor_InitPool
==================
*/
/* name: inferred from its body, which numbers every pool record, frees it and clears the visible list */
void Actor_InitPool( void )
{
    int       i;
    actor_t  *actor;
    actor_t **visible;
    g_visibleActorCount = 0;
    g_actorFree = 0;
    g_liveActors = 0;
    i = 0;
    visible = g_visibleActors;
    actor = g_actorPool;
    do {
        actor->index = i;
        Actor_Free( actor );
        *visible = 0;
        visible++;
        i++;
        actor++;
    } while ( i < ACTOR_COUNT );
}

/*
==================
Actor_PoolAlloc
==================
*/
/* name: inferred from its body, which allocates the ACTOR_COUNT pool records */
void Actor_PoolAlloc( void )
{
    g_actorPool = (actor_t *)Heap_Alloc( ACTOR_COUNT * sizeof( actor_t ) );
}

/*
==================
Actor_Alloc
==================
*/
actor_t *Actor_Alloc( void )
{
    actor_t *first;

    if ( ( first = g_actorFree ) )
        g_actorFree = first->next;

    return first;
}

/*
==================
Actor_Link
==================
*/
void Actor_Link( actor_t *actor )
{
    actor_t *old = g_liveActors;
    if ( old )
        old->prev = actor;
    g_liveActors = actor;
    actor->next = old;
    actor->prev = 0;
}

/*
==================
Actor_Unlink
==================
*/
void Actor_Unlink( actor_t *actor )
{
    actor_t *next = actor->next;
    actor_t *prev = actor->prev;
    if ( next )
        next->prev = prev;
    if ( prev )
        prev->next = next;
    else
        g_liveActors = next;
    actor->prev = 0;
    actor->next = 0;
}

/* Multi-statement macro (name invented): a non-zero value below lo becomes lo. Its
 * do { } while (0) block gives retail's register order (health outranks slot for s0). */
#define RAISE_NONZERO( v, lo )              \
    do {                                    \
        if ( ( v ) != 0 && ( v ) < ( lo ) ) \
            ( v ) = ( lo );                 \
    } while ( 0 )

/*
==================
Actor_SlotHealth
==================
*/
/* name: inferred from its body, which scales the slot's strength by the difficulty into hit points */
short Actor_SlotHealth( actorSlot_t *slot )
{
    short health = 0;
    int   shift;

    shift = slot->kind == ACTOR_KIND_BOSS ? ACTOR_BOSS_HEALTH_SHIFT : ACTOR_HEALTH_SHIFT;
    switch ( Menu_GetDifficulty() ) {
    case DIFFICULTY_RAGING_TERROR:
        if ( slot->strength )
            health = ( slot->strength + 1 ) << shift;
        break;
    case DIFFICULTY_XENOMANIA:
        if ( slot->strength )
            health = ( slot->strength + 2 ) << shift;
        break;
    case DIFFICULTY_ACID_REIGN:
    default:
        health = slot->strength << shift;
        break;
    }
    RAISE_NONZERO( health, ACTOR_MIN_HEALTH );
    return health;
}

/*
==================
Actor_Spawn

Spawns the actor of actorSlot_t index and its update task. The K&R definition (short
parameter under ACTOR.H's int prototype) gives retail's sign-extending prologue.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
actor_t *Actor_Spawn( index )
short index;
{
    actorSlot_t *slot = &g_actorSlots[index];
    TASK        *task = 0;
    actor_t     *actor = Actor_Alloc();
    int          dx, dz;
    short        heightStep;

    if ( actor == 0 )
        return 0;
    actor->position.vx = ( slot->gridX << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetX;
    actor->position.vz = ( slot->gridZ << MAP_CELL_SHIFT ) + MAP_CELL_HALF - g_levelMap.offsetZ;
    actor->height = FloorHeight( actor->position.vx + g_levelMap.offsetX, actor->position.vz + g_levelMap.offsetZ );
    actor->ceiling = CeilingHeight( actor->position.vx + g_levelMap.offsetX, actor->position.vz + g_levelMap.offsetZ );
    actor->position.vy = actor->height + ACTOR_FLOOR_CLEARANCE;
    actor->cell = MAP_CELL( actor->position.vx + g_levelMap.offsetX, actor->position.vz + g_levelMap.offsetZ );
    actor->kind = slot->kind;
    actor->health = Actor_SlotHealth( slot );
    if ( actor->health != 0 ) {
        actor->cell->marker = actor->kind;
        actor->cell->occupant = actor->index;
    }
    actor->action = 0;
    actor->idleTimer = ACTOR_WAKE_DELAY;
    actor->stunTimer = ACTOR_SPAWN_STUN;
    actor->slotIndex = index;
    actor->engageCooldown = 0;
    actor->direction = slot->direction;
    actor->shownDirection = ACTOR_DIRECTION_NONE;
    actor->layoutIndex = actor->cell->layoutIndex;
    actor->attachedIndex = slot->attachedIndex;
    actor->flags = 0;
    actor->viewCone = slot->viewCone;
    actor->reach = slot->reach;
    actor->speed = slot->speed;
    actor->farRange = slot->farRange;
    actor->nearRange = slot->nearRange;
    actor->lastTrigger = 0;
    actor->shotSpeed = 0;
    actor->velocity.vx = actor->velocity.vy = actor->velocity.vz = 0;
    switch ( actor->kind ) {
    case ACTOR_KIND_STEAM_JET:
    case ACTOR_KIND_FLAME_JET:
        break;
    case ACTOR_KIND_STEAM_JET_VERTICAL:
    case ACTOR_KIND_FLAME_JET_VERTICAL:
        break;
    default:
        if ( actor->viewCone < ACTOR_VIEW_CONE_MIN )
            actor->viewCone = ACTOR_VIEW_CONE_MIN;
        if ( actor->viewCone > ACTOR_VIEW_CONE_MAX )
            actor->viewCone = ACTOR_VIEW_CONE_WIDE;
        if ( actor->nearRange < ACTOR_NEAR_RANGE_MIN )
            actor->nearRange = ACTOR_NEAR_RANGE_MIN;
        break;
    }
    switch ( actor->direction ) {
    case FACING_NEG_Z:
        actor->angle = 0;
        actor->desiredAngle = 0;
        break;
    case FACING_POS_X_NEG_Z:
        actor->desiredAngle = actor->angle = ANGLE_45;
        break;
    case FACING_POS_X:
        actor->desiredAngle = actor->angle = ANGLE_90;
        break;
    case FACING_POS_X_POS_Z:
        actor->desiredAngle = actor->angle = ANGLE_135;
        break;
    case FACING_POS_Z:
        actor->desiredAngle = actor->angle = ANGLE_180;
        break;
    case FACING_NEG_X_POS_Z:
        actor->desiredAngle = actor->angle = ANGLE_225;
        break;
    case FACING_NEG_X:
        actor->desiredAngle = actor->angle = ANGLE_270;
        break;
    case FACING_NEG_X_NEG_Z:
        actor->desiredAngle = actor->angle = ANGLE_315;
        break;
    }
    switch ( actor->kind ) {
    case ACTOR_KIND_EGG:
        actor->animations = g_eggAnimations;
        actor->sprite.slot = 0;
        actor->sprite.flags = 0;
        actor->sprite.scale = EGG_SPRITE_SCALE;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->sprite.clut = g_eggCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_EggTask );
        task->state[0] = (unsigned int)actor;
        task->state[1] = 0;
        actor->farRange = EGG_FAR_RANGE;
        actor->nearRange = EGG_NEAR_RANGE;
        Actor_PlayAnimation( actor, EGG_ANIM_CLOSED );
        break;
    case ACTOR_KIND_FACEHUGGER:
        actor->animations = g_huggerAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = HUGGER_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_facehuggerCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_CHESTBURSTER:
        actor->animations = g_bursterAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = BURSTER_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_bursterCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_BAMBI:
        actor->animations = g_bambiAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = BAMBI_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_bambiCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_DOG:
        actor->animations = g_dogAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = DOG_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_TALL;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_dogCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_DOG_CEILING:
        actor->animations = g_dogCeilingAnimations;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->sprite.slot = 0;
        actor->sprite.scale = DOG_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->turnRate = ANGLE_45;
        actor->flags |= ACTOR_FLAG_ON_CEILING;
        actor->sprite.clut = g_dogCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_WARRIOR_CEILING:
        actor->animations = g_alienCeilingAnimations;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->sprite.slot = 0;
        actor->sprite.scale = WARRIOR_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->turnRate = ANGLE_45;
        actor->flags |= ACTOR_FLAG_ON_CEILING;
        actor->sprite.clut = g_alienCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_WARRIOR:
        actor->animations = g_alienAnimations;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_TALL;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->sprite.slot = 0;
        actor->sprite.scale = WARRIOR_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_alienCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_BOSS:
        actor->sprite.clut = g_queenCLUT;
        actor->animations = g_queenAnimations;
        actor->sprite.slot = &g_pageSets[PAGE_SET_SPRITES].pages[1];
        actor->sprite.scale = QUEEN_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = QUEEN_SCALE_GIRTH;
        actor->scale.vy = QUEEN_SCALE_HEIGHT;
        actor->scale.vz = QUEEN_SCALE_GIRTH;
        actor->turnRate = ANGLE_45;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_QueenTask );
        task->state[0] = (unsigned int)actor;
        task->state[1] = 0;
        Actor_PlayAnimation( actor, QUEEN_ANIM_DORMANT );
        break;
    case ACTOR_KIND_COLONIST:
        actor->animations = g_colonistAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = COLONIST_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_UNIT * 2;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_colonistCLUT;
        switch ( actor->direction ) {
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
        Actor_PlayAnimation( actor, COLONIST_ANIM_COCOONED + COLONIST_REACH_OCTANT );
        actor->position.vy = actor->height + COLONIST_HANG_HEIGHT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_ColonistTask );
        if ( task == 0 )
            break;
        task->state[0] = (unsigned int)actor;
        task->state[1] = Sys_Random() & COLONIST_START_PHASE_MASK;
        task->state[2] = 0;
        task->state[ACTOR_STATE_COUNTER] = 0;
        break;
    case ACTOR_KIND_GUARD:
        actor->animations = g_guardAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = GUARD_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_TALL;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_guardCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_SOLDIER:
        actor->animations = g_soldierAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = SOLDIER_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_TALL;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_soldierCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_SYNTHETIC:
        actor->animations = g_synthAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = SYNTH_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_TALL;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_synthCLUT;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_HANDLER:
        actor->animations = g_handlerAnimations;
        actor->sprite.slot = 0;
        actor->sprite.scale = HANDLER_SPRITE_SCALE;
        actor->sprite.flags = 0;
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_TALL;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->turnRate = ANGLE_45;
        actor->sprite.clut = g_handlerCLUT;
        actor->speed >>= 1;
        task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_CreatureTask );
        task->state[0] = (unsigned int)actor;
        break;
    case ACTOR_KIND_INERT:
        break;
    case ACTOR_KIND_STEAM_JET_VERTICAL:
    case ACTOR_KIND_FLAME_JET_VERTICAL:
        if ( actor->kind == ACTOR_KIND_STEAM_JET_VERTICAL )
            task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_SteamJetVerticalTask );
        else
            task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_FlameJetVerticalTask );
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_TALL;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        heightStep = actor->attachedIndex >= BYTE_SIGN_BIT ? actor->attachedIndex | BYTE_SIGN_EXTENSION : actor->attachedIndex;
        if ( heightStep < 0 )
            actor->position.vy = actor->ceiling - ( ( -heightStep + JET_CEILING_STEP_BIAS ) << JET_HEIGHT_STEP_SHIFT );
        else
            actor->position.vy = actor->height + ( heightStep << JET_HEIGHT_STEP_SHIFT );
        actor->stunTimer = 0;
        actor->angle = ( actor->angle + ANGLE_180 ) & ANGLE_MASK;
        if ( task == 0 )
            break;
        task->state[0] = (unsigned int)actor;
        task->state[1] = 0;
        task->state[2] = -1;
        task->state[ACTOR_STATE_COUNTER] = 0;
        break;
    case ACTOR_KIND_STEAM_JET:
    case ACTOR_KIND_FLAME_JET:
        if ( actor->kind == ACTOR_KIND_FLAME_JET )
            task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_FlameJetTask );
        else
            task = TaskCreate( TASK_PRIORITY_ACTORS, Actor_SteamJetTask );
        actor->scale.vx = ACTOR_SCALE_UNIT;
        actor->scale.vy = ACTOR_SCALE_TALL;
        actor->scale.vz = ACTOR_SCALE_UNIT;
        actor->position.vy += actor->attachedIndex << JET_HEIGHT_STEP_SHIFT;
        actor->attachedIndex = ACTOR_INDEX_NONE;
        actor->stunTimer = 0;
        actor->angle = ( actor->angle + ANGLE_180 ) & ANGLE_MASK;
        if ( task == 0 )
            break;
        task->state[0] = (unsigned int)actor;
        task->state[1] = 0;
        task->state[2] = -1;
        task->state[ACTOR_STATE_COUNTER] = 0;
        break;
    }
    if ( task == 0 ) {
        Actor_Free( actor );
        return 0;
    }
    Actor_Link( actor );
    actor->task = task;
    return actor;
}

/* Multi-statement macro (name invented) that sets the three scale axes; its
 * do { } while (0) block gives retail's register order in both kind changes. */
#define SET_SCALE( actor, x, y, z )  \
    do {                             \
        ( actor )->scale.vx = ( x ); \
        ( actor )->scale.vy = ( y ); \
        ( actor )->scale.vz = ( z ); \
    } while ( 0 )

/*
==================
Actor_ToCeiling
==================
*/
/* name: inferred from flags bit 3 (Actor_Move then follows the ceiling) and the kind 5->9, 6->8 swap */
void Actor_ToCeiling( actor_t *actor, short index )
{
    switch ( actor->kind ) {
    case ACTOR_KIND_DOG:
        actor->kind = ACTOR_KIND_DOG_CEILING;
        actor->animations = g_dogCeilingAnimations;
        break;
    case ACTOR_KIND_WARRIOR:
        actor->kind = ACTOR_KIND_WARRIOR_CEILING;
        actor->animations = g_alienCeilingAnimations;
        break;
    default:
        return;
    }
    SET_SCALE( actor, ACTOR_SCALE_UNIT, ACTOR_SCALE_UNIT, ACTOR_SCALE_UNIT );
    actor->flags |= ACTOR_FLAG_ON_CEILING;
    if ( index != -1 )
        Actor_PlayAnimation( actor, index );
}

/*
==================
Actor_ToFloor
==================
*/
/* name: inferred from it undoing Actor_ToCeiling: kind 9->5, 8->6, flags bit 3 cleared */
void Actor_ToFloor( actor_t *actor, short index )
{
    switch ( actor->kind ) {
    case ACTOR_KIND_DOG_CEILING:
        actor->kind = ACTOR_KIND_DOG;
        actor->animations = g_dogAnimations;
        break;
    case ACTOR_KIND_WARRIOR_CEILING:
        actor->kind = ACTOR_KIND_WARRIOR;
        actor->animations = g_alienAnimations;
        break;
    default:
        return;
    }
    SET_SCALE( actor, ACTOR_SCALE_UNIT, ACTOR_SCALE_TALL, ACTOR_SCALE_UNIT );
    actor->flags &= ~ACTOR_FLAG_ON_CEILING;
    if ( index != -1 )
        Actor_PlayAnimation( actor, index );
}

/*
==================
Actor_GroupAdd
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Actor_GroupAdd( u_char amount, u_char index )
{
    short        delta = amount;
    actorSlot_t *slot;

    if ( delta >= BYTE_SIGN_BIT )
        delta |= BYTE_SIGN_EXTENSION;
    slot = &g_actorSlots[index];

    if ( delta > 0 && slot->group == slot->groupSize )
        return;
    slot->group += delta;
    if ( slot->group > slot->groupSize )
        slot->group = slot->groupSize;
    if ( slot->group == slot->groupSize )
        Actor_Spawn( index );
}

/*
==================
Actor_SpawnFromObject
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Actor_SpawnFromObject( levelObject_t *object, u8 index )
{
    actorSlot_t *slot = &g_actorSlots[index];
    actor_t     *actor;
    u32          x, z;

    slot->group++;
    if ( slot->group > slot->groupSize )
        slot->group = slot->groupSize;
    if ( slot->group != slot->groupSize )
        return;
    x = (u32)( object->position.vx + g_levelMap.offsetX ) >> MAP_CELL_SHIFT;
    z = (u32)( object->position.vz + g_levelMap.offsetZ ) >> MAP_CELL_SHIFT;
    slot->gridX = x;
    slot->gridZ = z;
    actor = Actor_Spawn( index );
    if ( actor->kind == ACTOR_KIND_FACEHUGGER ) {
        actor->angle = actor->desiredAngle = object->facing << ANGLE_OCTANT_SHIFT;
        actor->shownDirection = actor->direction = object->facing;
        actor->idleTimer = 0;
        actor->flags |= ACTOR_FLAG_ALERTED;
        Actor_SetVelocity( actor, actor->angle, ACTOR_SPAWN_LEAP_SPEED );
        Actor_PlayAnimation( actor, HUGGER_ANIM_LEAP );
        actor->velocity.vy = ACTOR_SPAWN_LEAP_LIFT;
        actor->action = ACTOR_LEAP;
    }
    if ( actor->kind == ACTOR_KIND_CHESTBURSTER ) {
        actor->angle = actor->desiredAngle = object->facing << ANGLE_OCTANT_SHIFT;
        actor->shownDirection = actor->direction = object->facing;
        actor->idleTimer = 0;
        actor->flags |= ACTOR_FLAG_ALERTED;
        Actor_SetVelocity( actor, actor->angle, ACTOR_SPAWN_LEAP_SPEED );
        Actor_PlayAnimation( actor, BURSTER_ANIM_LEAP );
        actor->velocity.vy = ACTOR_SPAWN_LEAP_LIFT;
        actor->action = ACTOR_LEAP;
    }
}

/*
==================
Actor_InitSlots
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_InitSlots( actorSlot_t *slots, short count )
{
    u8 oldKind;

    g_actorSlots = slots;
    g_actorSlotCount = count;
    for ( ; count; count--, slots++ ) {
        oldKind = slots->kind;
        slots->kind = g_actorKindMap[slots->kind];
        if ( slots->kind != oldKind ) {
            switch ( slots->kind ) {
            case ACTOR_KIND_WARRIOR:
                slots->farRange = REMAPPED_FAR_RANGE;
                slots->nearRange = REMAPPED_NEAR_RANGE;
                slots->speed = ( Sys_Random() & REMAPPED_SPEED_MASK ) + REMAPPED_WARRIOR_SPEED;
                break;
            case ACTOR_KIND_FACEHUGGER:
                slots->farRange = ( Sys_Random() & REMAPPED_HUGGER_FAR_RANGE_MASK ) + REMAPPED_HUGGER_FAR_RANGE;
                slots->nearRange = REMAPPED_NEAR_RANGE;
                slots->speed = ( Sys_Random() & REMAPPED_SPEED_MASK ) + REMAPPED_SPEED;
                break;
            case ACTOR_KIND_DOG:
                slots->farRange = REMAPPED_FAR_RANGE;
                slots->nearRange = REMAPPED_NEAR_RANGE;
                slots->speed = ( Sys_Random() & REMAPPED_SPEED_MASK ) + REMAPPED_SPEED;
                break;
            }
        }
        g_levelMap.kindCounts[slots->kind]++;
        if ( slots->group == slots->groupSize )
            Actor_Spawn( g_actorSlotCount - count );
    }
}

/*
==================
Actor_Init
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_Init( void )
{
    Actor_InitPool();
    Actor_InitSlots( g_levelMap.actorSlots, g_levelMap.actorSlotCount );
}

/*
==================
Actor_PlayAnimation
==================
*/
/* name: inferred from its body, which starts an animation track entry on the actor's sprite */
void Actor_PlayAnimation( actor_t *actor, s16 index )
{
    trackEntry_t *entry = &actor->animations[index];
    u_short      *stream = entry->stream;
    u_short       clut;

    actor->sprite.uv.u2 = 0;
    actor->sprite.uv.u0 = 0;
    clut = actor->sprite.clut->clut;
    actor->sprite.flags &= ~SPRITE_SCRIPT_FLAGS;
    actor->sprite.uv.clut = clut;
    actor->sprite.frames = entry->frames;
    actor->sprite.frame = 0;
    actor->sprite.remaining = 1;
    actor->sprite.duration = *stream++;
    actor->sprite.repeat = 0;
    actor->sprite.script = (short *)stream++;
    actor->sprite.loop = (short *)stream;
    StepSprite( &actor->sprite );
}

/*
==================
Actor_BindSprite
==================
*/
static inline void Actor_BindSprite( spriteUV_t *uv, sprite_t *sprite, texturePage_t *slot )
{
    slot->users++;
    uv->v0 = uv->v1 = slot->y;
    uv->u3 = uv->u1 = sprite->frame->w - 1;
    uv->v3 = uv->v2 = sprite->slot->y + sprite->frame->h - 1;
    uv->tpage = slot->tpage;
}

/*
==================
Actor_DrawShadowed

Draws the actor's sprite like Actor_DrawSprite but clips its v range at the screen's top and bottom,
then draws its semi-transparent shadow quad at the floor under it.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_DrawShadowed( actor_t *actor )
{
    spriteFrame_t *frame = actor->sprite.frame;
    POLY_FT4      *poly;
    POLY_FT4      *shadow;
    sprite_t      *sprite;
    spriteUV_t    *uv;
    CVECTOR       *color;
    SVECTOR        floor;
    int            scale;
    int            cut;
    u_char         u;
    short          bottom;
    int            top;
    int            height;

    g_projectedDepth = actor->sprite.depth;
    poly = g_spriteWorkspace;
    poly->y1 = poly->y0 = actor->sprite.screen.vy + frame->y * actor->sprite.scale / g_projectedDepth;
    bottom = actor->sprite.screen.vy + ( frame->y + frame->h ) * actor->sprite.scale / g_projectedDepth;
    poly->y3 = poly->y2 = bottom;
    sprite = &actor->sprite;
    uv = &actor->sprite.uv;
    if ( poly->y0 <= SCREEN_HEIGHT && bottom >= 0 ) {
        if ( actor->sprite.flags & SPRITE_FLAG_MIRRORED ) {
            poly->x2 = poly->x0 = actor->sprite.screen.vx + ( -frame->x - frame->w ) * actor->sprite.scale / g_projectedDepth;
            poly->x3 = poly->x1 = actor->sprite.screen.vx + -frame->x * actor->sprite.scale / g_projectedDepth;
        } else {
            poly->x2 = poly->x0 = actor->sprite.screen.vx + frame->x * actor->sprite.scale / g_projectedDepth;
            poly->x3 = poly->x1 = actor->sprite.screen.vx + ( frame->x + frame->w ) * actor->sprite.scale / g_projectedDepth;
        }
        if ( poly->x0 <= SCREEN_WIDTH && poly->x1 >= 0 ) {
            *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
            *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
            *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
            *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
            height = ABS( poly->v0 - poly->v2 );
            top = poly->y0;
            scale = ( ABS( top - poly->y2 ) << ACTOR_CLIP_FRACTION_SHIFT ) / height;
            if ( scale != 0 ) {
                if ( top < 0 ) {
                    cut = ( -top << ACTOR_CLIP_FRACTION_SHIFT ) / scale;
                    poly->y0 = poly->y1 = 0;
                    poly->v0 = poly->v1 = poly->v0 + cut;
                }
                if ( poly->y2 > SCREEN_HEIGHT ) {
                    cut = ( ( poly->y2 - SCREEN_HEIGHT ) << ACTOR_CLIP_FRACTION_SHIFT ) / scale;
                    poly->y2 = poly->y3 = SCREEN_HEIGHT;
                    poly->v2 = poly->v3 = poly->v2 - cut;
                }
            }
            color = &g_palette[actor->cell->palette].colors[0];
            g_transformResult = g_projectedDepth;
            if ( g_transformResult < ONE ) {
                poly->r0 = color->r;
                poly->g0 = color->g;
                poly->b0 = color->b;
            } else {
                g_transformResult -= ONE;
                DpqColor( color, g_transformResult, (CVECTOR *)&poly->r0 );
            }
            if ( sprite->flags & SPRITE_FLAG_MIRRORED ) {
                u = poly->u0;
                poly->u0 = poly->u2 = poly->u1;
                poly->u1 = poly->u3 = u;
            }
            setPolyFT4( poly );
            addPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), poly );
            g_spriteWorkspace = poly + 1;
            shadow = g_spriteWorkspace;
            floor.vx = actor->position.vx;
            floor.vy = actor->height + ACTOR_FLOOR_CLEARANCE;
            floor.vz = actor->position.vz;
            RotTransPers( &floor, (long *)&floor, &g_gteInterpolation, &g_gteFlag );
            shadow->y1 = shadow->y0 = floor.vy + frame->y * ( sprite->scale >> 2 ) / g_projectedDepth;
            shadow->y2 = shadow->y3 = floor.vy + ( frame->y + frame->h ) * sprite->scale / g_projectedDepth;
            if ( shadow->y0 <= SCREEN_HEIGHT && shadow->y3 >= 0 ) {
                if ( sprite->flags & SPRITE_FLAG_MIRRORED ) {
                    shadow->x2 = poly->x0;
                    shadow->x3 = poly->x3;
                    shadow->x0 = sprite->screen.vx + ( -frame->x - frame->w ) * ( sprite->scale >> 1 ) / g_projectedDepth;
                    shadow->x1 = sprite->screen.vx + -frame->x * ( sprite->scale >> 1 ) / g_projectedDepth;
                } else {
                    shadow->x2 = poly->x0;
                    shadow->x3 = poly->x3;
                    shadow->x0 = sprite->screen.vx + frame->x * ( sprite->scale >> 1 ) / g_projectedDepth;
                    shadow->x1 = sprite->screen.vx + ( frame->x + frame->w ) * ( sprite->scale >> 1 ) / g_projectedDepth;
                }
                if ( shadow->x0 <= SCREEN_WIDTH && shadow->x1 >= 0 ) {
                    *(u_long *)&shadow->u0 = *(u_long *)&uv->u0;
                    *(u_long *)&shadow->u1 = *(u_long *)&uv->u1;
                    *(u_long *)&shadow->u2 = *(u_long *)&uv->u2;
                    *(u_long *)&shadow->u3 = *(u_long *)&uv->u3;
                    if ( sprite->flags & SPRITE_FLAG_MIRRORED ) {
                        u = shadow->u0;
                        shadow->u0 = shadow->u2 = shadow->u1;
                        shadow->u1 = shadow->u3 = u;
                    }
                    *(u_long *)&shadow->r0 = 0;
                    setPolyFT4( shadow );
                    setSemiTrans( shadow, 1 );
                    addPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), shadow );
                    shadow++;
                    g_spriteWorkspace = shadow;
                }
            }
        }
    }
}

/*
==================
Actor_DrawSprite

Draws the actor's sprite as one textured quad, scaled by its depth and lit by its cell's palette.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_DrawSprite( actor_t *actor )
{
    spriteFrame_t *frame = actor->sprite.frame;
    POLY_FT4      *poly;
    sprite_t      *sprite;
    spriteUV_t    *uv;
    CVECTOR       *color;
    u_char         u;
    short          bottom;

    g_projectedDepth = actor->sprite.depth;
    poly = g_spriteWorkspace;
    poly->y1 = poly->y0 = actor->sprite.screen.vy + frame->y * actor->sprite.scale / g_projectedDepth;
    bottom = actor->sprite.screen.vy + ( frame->y + frame->h ) * actor->sprite.scale / g_projectedDepth;
    poly->y3 = poly->y2 = bottom;
    sprite = &actor->sprite;
    uv = &actor->sprite.uv;
    if ( poly->y0 <= SCREEN_HEIGHT && bottom >= 0 ) {
        if ( actor->sprite.flags & SPRITE_FLAG_MIRRORED ) {
            poly->x2 = poly->x0 = actor->sprite.screen.vx + ( -frame->x - frame->w ) * actor->sprite.scale / g_projectedDepth;
            poly->x3 = poly->x1 = actor->sprite.screen.vx + -frame->x * actor->sprite.scale / g_projectedDepth;
        } else {
            poly->x2 = poly->x0 = actor->sprite.screen.vx + frame->x * actor->sprite.scale / g_projectedDepth;
            poly->x3 = poly->x1 = actor->sprite.screen.vx + ( frame->x + frame->w ) * actor->sprite.scale / g_projectedDepth;
        }
        if ( poly->x0 <= SCREEN_WIDTH && poly->x1 >= 0 ) {
            *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
            *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
            *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
            *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
            if ( sprite->flags & SPRITE_FLAG_MIRRORED ) {
                u = poly->u0;
                poly->u0 = poly->u2 = poly->u1;
                poly->u1 = poly->u3 = u;
            }
            color = &g_palette[actor->cell->palette].colors[0];
            g_transformResult = g_projectedDepth;
            if ( g_transformResult < ONE ) {
                poly->r0 = color->r;
                poly->g0 = color->g;
                poly->b0 = color->b;
            } else {
                g_transformResult -= ONE;
                DpqColor( color, g_transformResult, (CVECTOR *)&poly->r0 );
            }
            setPolyFT4( poly );
            addPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), poly );
            poly++;
            g_spriteWorkspace = poly;
        }
    }
}

/*
==================
Actor_UploadSprite
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
texturePage_t *Actor_UploadSprite( actor_t *actor )
{
    sprite_t      *sprite = &actor->sprite;
    spriteUV_t    *uv = &actor->sprite.uv;
    texturePage_t *slot;
    short          count;
    int            kind;

    kind = actor->kind;
    if ( kind < ACTOR_KIND_BAMBI && kind != 0 ) {
        slot = g_pageSets[PAGE_SET_SMALL_SPRITES].pages;
        count = g_pageSets[PAGE_SET_SMALL_SPRITES].count;
    } else {
        slot = g_pageSets[PAGE_SET_SPRITES].pages;
        count = g_pageSets[PAGE_SET_SPRITES].count;
    }
    for ( ; count; count--, slot++ ) {
        if ( slot->users == 0 ) {
            sprite->slot = slot;
            if ( Tex_UploadSpriteFrame( sprite, 0 ) ) {
                Actor_BindSprite( uv, sprite, slot );
                sprite->slot->pixels = sprite->frame->pixels;
                return slot;
            }
            return 0;
        }
    }
    return 0;
}

/*
==================
Actor_SpriteFindLoaded
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Actor_SpriteFindLoaded( actor_t *actor )
{
    sprite_t      *sprite = &actor->sprite;
    spriteUV_t    *uv = &actor->sprite.uv;
    texturePage_t *slot;
    short          count;
    int            kind;

    kind = actor->kind;
    if ( kind < ACTOR_KIND_BAMBI && kind != 0 ) {
        slot = g_pageSets[PAGE_SET_SMALL_SPRITES].pages;
        count = g_pageSets[PAGE_SET_SMALL_SPRITES].count;
    } else {
        slot = g_pageSets[PAGE_SET_SPRITES].pages;
        count = g_pageSets[PAGE_SET_SPRITES].count;
    }
    for ( ; count; count--, slot++ ) {
        if ( slot->pixels == sprite->frame->pixels ) {
            sprite->slot = slot;
            Actor_BindSprite( uv, sprite, slot );
            return -1;
        }
    }
    return 0;
}

/*
==================
Actor_UpdateBossSprite
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
int Actor_UpdateBossSprite( actor_t *actor )
{
    sprite_t      *sprite = &actor->sprite;
    texturePage_t *slot = sprite->slot;

    if ( slot->pixels == sprite->frame->pixels ) {
        sprite->uv.v0 = sprite->uv.v1 = slot->y;
        sprite->uv.u3 = sprite->uv.u1 = sprite->frame->w - 1;
        sprite->uv.v3 = sprite->uv.v2 = sprite->slot->y + sprite->frame->h - 1;
        sprite->uv.tpage = slot->tpage;
        return -1;
    }
    if ( Tex_UploadBossSpriteFrame( sprite ) == 0 )
        return 0;
    sprite->uv.v0 = sprite->uv.v1 = slot->y;
    sprite->uv.u3 = sprite->uv.u1 = sprite->frame->w - 1;
    sprite->uv.v3 = sprite->uv.v2 = sprite->slot->y + sprite->frame->h - 1;
    sprite->uv.tpage = slot->tpage;
    sprite->slot->pixels = sprite->frame->pixels;
    return -1;
}

/*
==================
Actor_Draw
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_Draw( actor_t *actor )
{
    if ( actor->action == ACTOR_DEAD ) {
        Actor_DrawSprite( actor );
        return;
    }
    switch ( actor->kind ) {
    default:
        if ( actor->sprite.depth > ACTOR_SHADOW_DEPTH )
            Actor_DrawSprite( actor );
        else
            Actor_DrawShadowed( actor );
        break;
    case ACTOR_KIND_EGG:
    case ACTOR_KIND_WARRIOR_CEILING:
    case ACTOR_KIND_DOG_CEILING:
    case ACTOR_KIND_COLONIST:
        Actor_DrawSprite( actor );
        break;
    case ACTOR_KIND_BOSS:
        if ( actor->task->state[1] != QUEEN_AWAKE || actor->sprite.depth > ACTOR_BOSS_SHADOW_DEPTH )
            Actor_DrawSprite( actor );
        else
            Actor_DrawShadowed( actor );
        break;
    }
}

/*
==================
Actor_DrawTask

Task (main.c: TaskCreate(60, Actor_DrawTask)): draws the visible actors. Pass 1 draws the bosses and
the actors whose frame is already in VRAM; pass 2 uploads the others, sharing one slot between
actors on the same frame. Each drawn actor is removed from g_visibleActors; the list is cleared
afterwards. Each arm clears its own entry: the clears then share the entry address with the load
(CSE within the extended basic block), which keeps the address in one register across the calls.
==================
*/
/* name: static evidence, config/main.json (TaskCreate(60, Actor_DrawTask)) */
void Actor_DrawTask( TASK *task )
{
    short          count = g_visibleActorCount;
    short          i;
    short          j;
    actor_t       *actor;
    texturePage_t *slot;

    for ( i = 0; i < count; i++ ) {
        actor = g_visibleActors[i];
        if ( actor->kind == ACTOR_KIND_BOSS ) {
            if ( Actor_UpdateBossSprite( actor ) )
                Actor_Draw( actor );
            g_visibleActors[i] = 0;
            g_visibleActorCount--;
        } else if ( ( actor->flags & ACTOR_FLAG_NO_CORPSE ) && actor->action == ACTOR_DEAD ) {
            g_visibleActors[i] = 0;
            g_visibleActorCount--;
        } else if ( Actor_SpriteFindLoaded( actor ) ) {
            Actor_Draw( actor );
            g_visibleActors[i] = 0;
            g_visibleActorCount--;
        }
    }
    if ( g_visibleActorCount != 0 ) {
        for ( i = 0; i < count; i++ ) {
            actor = g_visibleActors[i];
            if ( actor == 0 )
                continue;
            slot = Actor_UploadSprite( actor );
            if ( slot == 0 )
                continue;
            Actor_Draw( actor );
            g_visibleActorCount--;
            for ( j = i + 1; j < count; j++ ) {
                actor = g_visibleActors[j];
                if ( actor == 0 )
                    continue;
                if ( slot->pixels != actor->sprite.frame->pixels )
                    continue;
                actor->sprite.slot = slot;
                Actor_BindSprite( &actor->sprite.uv, &actor->sprite, slot );
                Actor_Draw( actor );
                g_visibleActors[j] = 0;
                g_visibleActorCount--;
            }
        }
    }
    g_visibleActorCount = 0;
    g_nextTask = task->Next;
}

/*
==================
Actor_Project
==================
*/
/* name: inferred from its body, which projects the actor and appends it to g_visibleActors */
void Actor_Project( actor_t *actor )
{
    if ( actor->cell->mask & g_visibleCellMask ) {
        g_projectedDepth = RotTransPers( &actor->position, (long *)&actor->sprite.screen, &g_gteInterpolation, &g_gteFlag ) << 2;
        if ( (u_int)( g_projectedDepth - ( ACTOR_NEAR_DEPTH + 1 ) ) < DRAW_FAR_DEPTH - ACTOR_NEAR_DEPTH - 1
             && (u_short)( actor->sprite.screen.vx - ACTOR_CLIP_MIN_X ) < ACTOR_CLIP_MAX_X - ACTOR_CLIP_MIN_X + 1
             && ( g_projectedDepth < ACTOR_CLIP_Y_DEPTH
                  || (u_short)( actor->sprite.screen.vy - ACTOR_CLIP_MIN_Y ) < ACTOR_CLIP_MAX_Y - ACTOR_CLIP_MIN_Y + 1 ) ) {
            actor->sprite.depth = g_projectedDepth;
            g_visibleActors[g_visibleActorCount++] = actor;
        }
    }
}

/*
==================
Actor_StartHuggerOverlay
==================
*/
/* name: inferred from Actor_StartAttack: a kind-2 actor next to the player starts two screen overlay sprites */
short Actor_StartHuggerOverlay( actor_t *actor )
{
    texturePage_t *slot = g_pageSets[PAGE_SET_SPRITES].pages;
    short          count = g_pageSets[PAGE_SET_SPRITES].count;
    texturePage_t *first = 0;
    texturePage_t *second = 0;
    sprite_t      *upper;
    sprite_t      *lower;
    trackEntry_t  *entry;
    u_short       *stream;

    for ( ; count; count--, slot++ ) {
        if ( slot->users == 0 ) {
            first = slot;
            break;
        }
    }
    if ( first == 0 )
        return 0;
    for ( count--, slot++; count; count--, slot++ ) {
        if ( slot->users == 0 ) {
            second = slot;
            break;
        }
    }
    if ( second == 0 )
        return 0;
    upper = &g_huggerUpper;
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
    StepSprite( upper );
    upper->slot = first;
    lower = &g_huggerLower;
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
    StepSprite( lower );
    lower->slot = second;
    if ( Tex_UploadCanQueuePair( upper, lower ) == 0 )
        return 0;
    Tex_UploadSpriteFrame( upper, 1 );
    upper->uv.u2 = 0;
    upper->uv.u0 = 0;
    upper->uv.v0 = upper->uv.v1 = first->y;
    upper->uv.u3 = upper->uv.u1 = upper->frame->w - 1;
    upper->uv.v3 = upper->uv.v2 = upper->slot->y + upper->frame->h - 1;
    upper->uv.clut = actor->sprite.clut->clut;
    upper->uv.tpage = first->tpage;
    upper->slot->pixels = upper->frame->pixels;
    Tex_UploadSpriteFrame( lower, 1 );
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

/*
==================
Actor_DrawHuggerOverlay

The overlay's frame offsets are added as unsigned halfwords (retail lhu); spriteFrame_t's are signed
elsewhere.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_DrawHuggerOverlay( sprite_t *sprite, CVECTOR *color )
{
    spriteFrame_t *frame = sprite->frame;
    POLY_FT4      *poly = g_spriteWorkspace;
    int            shake = g_frameCount & HUGGER_OVERLAY_SHAKE_MASK;

    poly->x2 = poly->x0 = sprite->screen.vx + (u_short)frame->x * HUGGER_OVERLAY_FRAME_SCALE;
    poly->y1 = poly->y0 = sprite->screen.vy + shake + (u_short)frame->y * HUGGER_OVERLAY_FRAME_SCALE;
    poly->x3 = poly->x1 = sprite->screen.vx + ( (u_short)frame->x + (u_short)frame->w ) * HUGGER_OVERLAY_FRAME_SCALE;
    poly->y3 = poly->y2 = sprite->screen.vy + shake + ( (u_short)frame->y + (u_short)frame->h ) * HUGGER_OVERLAY_FRAME_SCALE;
    *(u_long *)&poly->u0 = *(u_long *)&sprite->uv.u0;
    *(u_long *)&poly->u1 = *(u_long *)&sprite->uv.u1;
    *(u_long *)&poly->u2 = *(u_long *)&sprite->uv.u2;
    *(u_long *)&poly->u3 = *(u_long *)&sprite->uv.u3;
    poly->clut = g_fingersCLUT->clut;
    setPolyFT4( poly );
    setRGB0( poly, color->r, color->g, color->b );
    addPrim( g_renderBuffer, g_spriteWorkspace );
    g_spriteWorkspace = poly + 1;
}

/*
==================
Actor_StepOverlay
==================
*/
/* name: invented; steps one overlay sprite and draws it, uploading a new frame first */
static __inline__ void Actor_StepOverlay( sprite_t *sprite )
{
    if ( sprite->slot->pixels != sprite->frame->pixels ) {
        if ( Tex_UploadSpriteFrame( sprite, 1 ) ) {
            sprite->uv.u3 = sprite->uv.u1 = sprite->frame->w - 1;
            sprite->uv.v3 = sprite->uv.v2 = sprite->slot->y + sprite->frame->h - 1;
            sprite->slot->pixels = sprite->frame->pixels;
            Actor_DrawHuggerOverlay( sprite, g_palette->colors );
        }
    } else {
        Actor_DrawHuggerOverlay( sprite, g_palette->colors );
    }
    StepSprite( sprite );
}

/*
==================
Actor_StepHuggerOverlay
==================
*/
/* name: inferred from its callers in Actor_HuggerClingTask: steps and draws the overlay, sliding it on screen */
void Actor_StepHuggerOverlay( TASK *node )
{
    sprite_t *sprite;
    short     y;

    sprite = &g_huggerUpper;
    Actor_StepOverlay( sprite );
    sprite = &g_huggerLower;
    Actor_StepOverlay( sprite );
    y = sprite->screen.vy;
    if ( node->state[2] == 0 ) {
        if ( y >= HUGGER_OVERLAY_TOP_Y + 1 ) {
            y -= HUGGER_OVERLAY_RISE_STEP;
            g_huggerUpper.screen.vy = y;
            g_huggerLower.screen.vy = y;
        }
    } else if ( y < HUGGER_OVERLAY_HIDDEN_Y ) {
        y += HUGGER_OVERLAY_DROP_STEP;
        g_huggerUpper.screen.vy = y;
        g_huggerLower.screen.vy = y;
    } else {
        node->state[2] = 2;
    }
}

/*
==================
Actor_HuggerClingTask
==================
*/
/* name: inferred from its body: damages the player each tick, then drops the actor beside the player */
void Actor_HuggerClingTask( TASK *node )
{
    actor_t   *actor = (actor_t *)node->state[0];
    mapCell_t *cell;
    u_int      x, z;
    short      height;
    short      distance;

    if ( node->state[1] == 0 ) {
        if ( node->state[2] == 2 ) {
            actor->position.vx = g_player.position.vx + ( Sys_Random() & HUGGER_DROP_SCATTER_MASK ) - HUGGER_DROP_SCATTER;
            actor->position.vz = g_player.position.vz + ( Sys_Random() & HUGGER_DROP_SCATTER_MASK ) - HUGGER_DROP_SCATTER;
            z = actor->position.vz + g_levelMap.offsetZ;
            x = actor->position.vx + g_levelMap.offsetX;
            cell = MAP_CELL( x, z );
            if ( cell->marker == 0 && cell->type < CELL_TYPE_WALL ) {
                height = FloorHeight( x, z );
                distance = ABS( g_player.position.vy - height );
                if ( distance < HUGGER_DROP_MAX_STEP ) {
                    actor->cell = cell;
                    actor->height = height;
                    actor->ceiling = CeilingHeight( x, z );
                    actor->position.vy = height + ACTOR_FLOOR_CLEARANCE;
                    Actor_Die( actor );
                    node->callback = Actor_CreatureTask;
                    DISABLE_FLAG( PLAYER_HUGGER_ATTACHED );
                    g_huggerUpper.slot->users = 0;
                    g_huggerLower.slot->users = 0;
                    g_huggerUpper.slot->reserved = 0;
                    g_huggerLower.slot->reserved = 0;
                }
            }
        } else {
            Actor_StepHuggerOverlay( node );
            Player_DamageHealth( 1, DEATH_FACEHUGGER );
        }
    } else {
        node->state[1]--;
        Actor_StepHuggerOverlay( node );
        Player_DamageHealth( 1, DEATH_FACEHUGGER );
        if ( node->state[1] == 0 )
            node->state[2] = 1;
    }
    g_nextTask = node->Next;
}

/*
==================
Actor_KnockBack
==================
*/
/* name: invented; pushes the actor directly away from the player at a quarter of its speed */
static __inline__ void Actor_KnockBack( actor_t *actor )
{
    Actor_SetVelocity( actor, ( actor->playerAngle + ANGLE_180 ) & ANGLE_MASK, actor->speed >> 2 );
}

/*
==================
Actor_Recoil
==================
*/
/* name: invented; knocked back on open floor, stopped dead on special cells (type >= CELL_TYPE_SLOPE) */
static __inline__ void Actor_Recoil( actor_t *actor )
{
    if ( actor->cell->type < CELL_TYPE_SLOPE )
        Actor_KnockBack( actor );
    else
        Actor_Stop( actor );
}

/*
==================
Actor_Die
==================
*/
/* name: inferred from action 6, the kill tally and the death animations */
void Actor_Die( actor_t *actor )
{
    actor->action = ACTOR_DYING;
    actor->cell->marker = 0;
    g_player.tallies[actor->kind]++;
    switch ( actor->kind ) {
    case ACTOR_KIND_COLONIST:
        if ( actor->speed != ACTOR_INDEX_NONE )
            g_player.tallies[ACTOR_KIND_CHESTBURSTER]++;
        break;
    case ACTOR_KIND_EGG:
        if ( actor->attachedIndex != ACTOR_INDEX_NONE )
            g_player.tallies[ACTOR_KIND_FACEHUGGER]++;
        break;
    }
    if ( actor->kind == ACTOR_KIND_STEAM_JET_VERTICAL || actor->kind == ACTOR_KIND_STEAM_JET
         || actor->kind == ACTOR_KIND_FLAME_JET_VERTICAL || actor->kind == ACTOR_KIND_FLAME_JET )
        return;
    if ( actor->kind != ACTOR_KIND_EGG && actor->kind != ACTOR_KIND_BOSS && actor->attachedIndex != ACTOR_INDEX_NONE )
        Object_ActorDropItem( actor );
    if ( actor->velocity.vy > 0 )
        actor->velocity.vy = -actor->velocity.vy;
    switch ( actor->kind ) {
    case ACTOR_KIND_DOG_CEILING:
        Actor_Stop( actor );
        actor->kind = ACTOR_KIND_DOG;
        actor->flags &= ~ACTOR_FLAG_ON_CEILING;
        Actor_PlayAnimation( actor, DOG_CEILING_ANIM_FALL );
        break;
    case ACTOR_KIND_DOG:
        Actor_Recoil( actor );
        Actor_PlayAnimation( actor, DOG_ANIM_DIE );
        break;
    case ACTOR_KIND_WARRIOR_CEILING:
        Actor_Stop( actor );
        actor->kind = ACTOR_KIND_WARRIOR;
        actor->flags &= ~ACTOR_FLAG_ON_CEILING;
        Actor_PlayAnimation( actor, WARRIOR_CEILING_ANIM_FALL );
        break;
    case ACTOR_KIND_WARRIOR:
    case ACTOR_KIND_SOLDIER:
        Actor_Recoil( actor );
        Actor_PlayAnimation( actor, WARRIOR_ANIM_DIE );
        break;
    case ACTOR_KIND_FACEHUGGER:
        Actor_Recoil( actor );
        Actor_PlayAnimation( actor, HUGGER_ANIM_DIE );
        break;
    case ACTOR_KIND_HANDLER:
        Actor_Recoil( actor );
        Actor_PlayAnimation( actor, HANDLER_ANIM_DIE );
        break;
    case ACTOR_KIND_EGG:
        Actor_PlayAnimation( actor, EGG_ANIM_DIE );
        break;
    case ACTOR_KIND_CHESTBURSTER:
        Actor_Recoil( actor );
        Actor_PlayAnimation( actor, BURSTER_ANIM_DIE );
        break;
    case ACTOR_KIND_BAMBI:
        Actor_Recoil( actor );
        Actor_PlayAnimation( actor, BAMBI_ANIM_DIE );
        break;
    case ACTOR_KIND_GUARD:
        Actor_Recoil( actor );
        Actor_PlayAnimation( actor, GUARD_ANIM_DIE );
        break;
    case ACTOR_KIND_SYNTHETIC:
        Actor_Recoil( actor );
        Actor_PlayAnimation( actor, SYNTH_ANIM_DIE );
        break;
    case ACTOR_KIND_BOSS:
        Actor_Recoil( actor );
        actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
        Actor_PlayAnimation( actor, QUEEN_ANIM_DIE );
        break;
    case ACTOR_KIND_COLONIST:
        Actor_PlayAnimation( actor, COLONIST_ANIM_DIE );
        break;
    }
}

/*
==================
Actor_Hurt
==================
*/
/* name: inferred from action 5, the 16-tick stun and the hit animations */
void Actor_Hurt( actor_t *actor )
{
    int mode;

    if ( actor->kind == ACTOR_KIND_WARRIOR_CEILING || actor->kind == ACTOR_KIND_DOG_CEILING )
        return;
    actor->action = ACTOR_HURT;
    actor->stunTimer = ACTOR_HURT_STUN;
    if ( actor->velocity.vy > 0 )
        actor->velocity.vy = -actor->velocity.vy;
    switch ( actor->kind ) {
    case ACTOR_KIND_CHESTBURSTER:
    case ACTOR_KIND_BAMBI:
    case ACTOR_KIND_DOG:
    case ACTOR_KIND_WARRIOR:
        Actor_KnockBack( actor );
        if ( actor->shownDirection == FACING_NEG_Z || actor->shownDirection == FACING_NEG_X_NEG_Z || actor->shownDirection == FACING_POS_X_NEG_Z )
            Actor_PlayAnimation( actor, WARRIOR_ANIM_HURT_FRONT );
        else
            Actor_PlayAnimation( actor, WARRIOR_ANIM_HURT_BACK );
        break;
    case ACTOR_KIND_WARRIOR_CEILING:
    case ACTOR_KIND_DOG_CEILING:
        Actor_KnockBack( actor );
        break;
    case ACTOR_KIND_FACEHUGGER:
    case ACTOR_KIND_GUARD:
    case ACTOR_KIND_SOLDIER:
    case ACTOR_KIND_HANDLER:
        Actor_KnockBack( actor );
        Actor_PlayAnimation( actor, GUARD_ANIM_HURT );
        break;
    case ACTOR_KIND_EGG:
        mode = actor->task->state[1];
        if ( mode == EGG_SHELL_CLOSED )
            Actor_PlayAnimation( actor, EGG_ANIM_HURT_CLOSED );
        else if ( mode == EGG_SHELL_OPEN )
            Actor_PlayAnimation( actor, EGG_ANIM_HURT_OPEN );
        break;
    case ACTOR_KIND_SYNTHETIC:
        Actor_KnockBack( actor );
        if ( actor->shownDirection == FACING_NEG_Z || actor->shownDirection == FACING_NEG_X_NEG_Z || actor->shownDirection == FACING_POS_X_NEG_Z )
            Actor_PlayAnimation( actor, WARRIOR_ANIM_HURT_FRONT );
        else
            Actor_PlayAnimation( actor, WARRIOR_ANIM_HURT_BACK );
        break;
    case ACTOR_KIND_BOSS:
        mode = actor->task->state[1];
        if ( (u_int)mode < QUEEN_RISING ) {
            Actor_PlayAnimation( actor, QUEEN_ANIM_HURT );
        } else if ( mode == QUEEN_RISING ) {
            actor->stunTimer = QUEEN_HURT_STUN;
            break;
        } else {
            Actor_KnockBack( actor );
            Actor_PlayAnimation( actor, QUEEN_ANIM_KNOCKED_BACK );
        }
        actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
        break;
    case ACTOR_KIND_COLONIST:
        if ( actor->action != COLONIST_EMPTY )
            Actor_PlayAnimation( actor, actor->shownDirection + COLONIST_ANIM_HURT );
        break;
    }
}

/*
==================
Actor_Damage
==================
*/
/* name: inferred from its body, which reduces health unless stunned or already hurt, then hurts or kills the actor */
void Actor_Damage( actor_t *actor, s16 amount, s16 setFlag )
{
    s16 remaining;
    if ( actor->stunTimer != 0 )
        return;
    if ( actor->action == ACTOR_HURT || actor->action >= ACTOR_HURT )
        return;
    if ( actor->kind == ACTOR_KIND_COLONIST && actor->action == COLONIST_EMPTY )
        return;
    remaining = actor->health - amount;
    actor->health = remaining;
    if ( remaining <= 0 ) {
        switch ( actor->kind ) {
        case ACTOR_KIND_FACEHUGGER:
        case ACTOR_KIND_CHESTBURSTER:
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_DOG:
        case ACTOR_KIND_WARRIOR:
        case ACTOR_KIND_WARRIOR_CEILING:
        case ACTOR_KIND_DOG_CEILING:
        case ACTOR_KIND_GUARD:
        case ACTOR_KIND_SOLDIER:
        case ACTOR_KIND_SYNTHETIC:
        case ACTOR_KIND_HANDLER:
            if ( setFlag != 0 )
                actor->flags |= ACTOR_FLAG_NO_CORPSE;
            break;
        case ACTOR_KIND_BOSS:
        case ACTOR_KIND_COLONIST:
        default:
            break;
        }
        Actor_Die( actor );
    } else {
        Actor_Hurt( actor );
    }
}

/*
==================
Actor_FaceView
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_FaceView( actor_t *actor )
{
    short direction =
        ( actor->direction - ( ( ( actor->playerAngle + ANGLE_180 + ANGLE_45 / 2 ) >> ANGLE_OCTANT_SHIFT ) & FACING_MASK ) ) & FACING_MASK;
    if ( actor->shownDirection != direction ) {
        actor->shownDirection = direction;
        switch ( actor->kind ) {
        case ACTOR_KIND_EGG:
            break;
        case ACTOR_KIND_BOSS:
            switch ( direction ) {
            case FACING_NEG_X_POS_Z:
            case FACING_NEG_X:
            case FACING_NEG_X_NEG_Z:
                actor->sprite.flags |= SPRITE_FLAG_MIRRORED;
                break;
            default:
                actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
                break;
            }
            Actor_PlayAnimation( actor, direction );
            break;
        case ACTOR_KIND_FACEHUGGER:
        case ACTOR_KIND_CHESTBURSTER:
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_DOG:
        case ACTOR_KIND_WARRIOR:
        case ACTOR_KIND_WARRIOR_CEILING:
        case ACTOR_KIND_DOG_CEILING:
        case ACTOR_KIND_GUARD:
        case ACTOR_KIND_SOLDIER:
        case ACTOR_KIND_HANDLER:
            switch ( direction ) {
            case FACING_NEG_X_POS_Z:
            case FACING_NEG_X:
            case FACING_NEG_X_NEG_Z:
                actor->sprite.flags |= SPRITE_FLAG_MIRRORED;
                break;
            default:
                actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
                break;
            }
            Actor_PlayAnimation( actor, direction );
            break;
        case ACTOR_KIND_SYNTHETIC:
            Actor_PlayAnimation( actor, direction );
            break;
        default:
            Actor_PlayAnimation( actor, direction );
            break;
        }
    }
}

/*
==================
Actor_FacePlayer
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_FacePlayer( actor_t *actor )
{
    short angle = actor->playerAngle;
    actor->shownDirection = ACTOR_DIRECTION_NONE;
    actor->desiredAngle = angle;
    actor->angle = angle;
    actor->direction = ( ( angle + ANGLE_45 / 2 ) >> ANGLE_OCTANT_SHIFT ) & FACING_MASK;
    Actor_FaceView( actor );
}

/*
==================
Actor_FaceTarget
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Actor_FaceTarget( actor_t *actor )
{
    short angle = actor->targetAngle;
    actor->shownDirection = ACTOR_DIRECTION_NONE;
    actor->desiredAngle = angle;
    actor->angle = angle;
    actor->direction = ( ( angle + ANGLE_45 / 2 ) >> ANGLE_OCTANT_SHIFT ) & FACING_MASK;
    Actor_FaceView( actor );
}

/*
==================
Actor_ClipX
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
unsigned int Actor_ClipX( unsigned int position, unsigned int boundary, short positive, short *offset )
{
    int          beyond;
    unsigned int result;
    if ( positive ) {
        boundary = ( boundary & ~MAP_CELL_MASK ) + ( MAP_CELL_SIZE + ACTOR_RADIUS );
        beyond = boundary < position + *offset;
    } else {
        boundary = ( boundary & ~MAP_CELL_MASK ) - ( ACTOR_RADIUS + 1 );
        beyond = boundary > position + *offset;
    }
    result = boundary;
    if ( beyond )
        result = position + *offset;
    return result;
}

/*
==================
Actor_ClipZ
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
unsigned int Actor_ClipZ( unsigned int position, unsigned int boundary, short positive, short *offset )
{
    int          beyond;
    unsigned int result;
    if ( positive ) {
        boundary = ( boundary & ~MAP_CELL_MASK ) + ( MAP_CELL_SIZE + ACTOR_RADIUS );
        beyond = boundary < position + offset[2];
    } else {
        boundary = ( boundary & ~MAP_CELL_MASK ) - ( ACTOR_RADIUS + 1 );
        beyond = boundary > position + offset[2];
    }
    result = boundary;
    if ( beyond )
        result = position + offset[2];
    return result;
}

/*
==================
Actor_Move
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_Move( actor_t *actor )
{
    u_int      x = actor->position.vx + g_levelMap.offsetX;
    u_int      z = actor->position.vz + g_levelMap.offsetZ;
    short      y;
    mapCell_t *cell;
    short      ceiling;

    y = actor->height = FloorHeight( x, z );
    ceiling = CeilingHeight( x, z );
    actor->ceiling = ceiling;
    y += ACTOR_FLOOR_CLEARANCE;
    if ( actor->flags & ACTOR_FLAG_ON_CEILING )
        y = ceiling - ACTOR_FLOOR_CLEARANCE;
    if ( actor->velocity.vy != 0 ) {
        y = actor->position.vy + actor->velocity.vy;
        if ( y > ceiling - actor->scale.vy ) {
            actor->velocity.vy = 0;
            y = actor->ceiling - actor->scale.vy;
        } else if ( y < actor->height + ACTOR_FLOOR_CLEARANCE ) {
            actor->velocity.vy = 0;
            y = actor->height + ACTOR_FLOOR_CLEARANCE;
        } else {
            actor->velocity.vy -= ACTOR_GRAVITY;
            if ( actor->velocity.vy == 0 )
                actor->velocity.vy = -ACTOR_GRAVITY;
        }
    }
    if ( actor->action < ACTOR_DYING )
        actor->cell->marker = 0;
    if ( actor->velocity.vx != 0 )
        x = Actor_MoveX( actor, x, y, z );
    if ( actor->velocity.vz != 0 )
        z = Actor_MoveZ( actor, x, y, z );
    actor->position.vx = x - g_levelMap.offsetX;
    actor->position.vy = y;
    actor->position.vz = z - g_levelMap.offsetZ;
    cell = MAP_CELL( x, z );
    if ( cell != actor->cell ) {
        actor->cell = cell;
        actor->layoutIndex = cell->layoutIndex;
        if ( actor->action != ACTOR_DEAD )
            Trigger_ActorCell( actor );
    }
    if ( actor->action < ACTOR_DYING ) {
        cell->marker = actor->kind;
        cell->occupant = actor->index;
    }
}

/*
==================
Actor_DistanceXZ
==================
*/
/* name: inferred from its body: planar distance, capped at DISTANCE_XZ_MAX */
int Actor_DistanceXZ( SVECTOR *left, SVECTOR *right )
{
    int          rawX = left->vx - right->vx;
    int          rawZ = left->vz - right->vz;
    unsigned int dx = rawX < 0 ? -rawX : rawX;
    unsigned int dz = rawZ < 0 ? -rawZ : rawZ;

    if ( dx >= DISTANCE_XZ_MAX || dz >= DISTANCE_XZ_MAX )
        return DISTANCE_XZ_MAX;
    else
        return SquareRoot0( dx * dx + dz * dz );
}

/*
==================
Actor_MeasurePlayer
==================
*/
/* name: inferred from its body, which stores the player delta, distance and distance class */
void Actor_MeasurePlayer( actor_t *actor )
{
    int          difference;
    unsigned int distance;

    difference = g_player.position.vx - actor->position.vx;
    if ( difference < 0 )
        difference = actor->position.vx - g_player.position.vx;
    actor->playerDelta.vx = difference;
    difference = g_player.position.vy - actor->position.vy;
    if ( difference < 0 )
        difference = actor->position.vy - g_player.position.vy;
    actor->playerDelta.vy = difference;
    difference = g_player.position.vz - actor->position.vz;
    if ( difference < 0 )
        difference = actor->position.vz - g_player.position.vz;
    actor->playerDelta.vz = difference;

    distance = Actor_DistanceXZ( &g_player.position, &actor->position );
    actor->playerDistance = distance;
    if ( distance < ACTOR_CLOSE_RANGE )
        actor->playerClass = ACTOR_RANGE_CLOSE;
    else if ( distance < actor->nearRange )
        actor->playerClass = ACTOR_RANGE_NEAR;
    else if ( distance < actor->farRange )
        actor->playerClass = ACTOR_RANGE_MID;
    else if ( distance < DISTANCE_XZ_MAX )
        actor->playerClass = ACTOR_RANGE_FAR;
    else
        actor->playerClass = ACTOR_RANGE_OUT;
}

/*
==================
Actor_MeasureTarget
==================
*/
/* name: inferred from its body, which stores the target delta, distance and distance class */
void Actor_MeasureTarget( actor_t *actor )
{
    int          difference;
    unsigned int distance;

    difference = actor->target.vx - actor->position.vx;
    if ( difference < 0 )
        difference = actor->position.vx - actor->target.vx;
    actor->targetDelta.vx = difference;
    difference = actor->target.vy - actor->position.vy;
    if ( difference < 0 )
        difference = actor->position.vy - actor->target.vy;
    actor->targetDelta.vy = difference;
    difference = actor->target.vz - actor->position.vz;
    if ( difference < 0 )
        difference = actor->position.vz - actor->target.vz;
    actor->targetDelta.vz = difference;

    distance = Actor_DistanceXZ( &actor->target, &actor->position );
    actor->targetDistance = distance;
    if ( distance < ACTOR_CLOSE_RANGE )
        actor->targetClass = ACTOR_RANGE_CLOSE;
    else if ( distance < actor->nearRange )
        actor->targetClass = ACTOR_RANGE_NEAR;
    else if ( distance < actor->farRange )
        actor->targetClass = ACTOR_RANGE_MID;
    else if ( distance < DISTANCE_XZ_MAX )
        actor->targetClass = ACTOR_RANGE_FAR;
    else
        actor->targetClass = ACTOR_RANGE_OUT;
}

/*
==================
Actor_PassedTarget
==================
*/
/* name: inferred from its body, which returns -1 once every moving axis has covered its distance to the target */
int Actor_PassedTarget( actor_t *actor )
{
    int x = ABS( actor->velocity.vx );
    int z = ABS( actor->velocity.vz );

    if ( x == 0 && z == 0 )
        return 0;
    if ( x && z ) {
        if ( x < actor->targetDelta.vx )
            return 0;
        if ( z < actor->targetDelta.vz )
            return 0;
    } else if ( x ) {
        if ( x < actor->targetDelta.vx )
            return 0;
    } else {
        if ( z < actor->targetDelta.vz )
            return 0;
    }
    return -1;
}

/*
==================
Actor_Occupy
==================
*/
/* name: invented; marks the actor's cell as occupied by it again */
static __inline__ void Actor_Occupy( actor_t *actor )
{
    actor->cell->marker = actor->kind;
    actor->cell->occupant = actor->index;
}

/*
==================
Actor_PathClear
==================
*/
/* name: inferred from its body, which walks two lines along angle for distance and fails on a wall, a marked cell, a low ceiling, or a climb or drop beyond the actor's limits */
int Actor_PathClear( actor_t *actor, short angle, u_int distance )
{
    u_short    climb;
    u_short    drop;
    short      steps;
    u_int      height;
    u_int      x, z;
    mapCell_t *cell;
    short      ceiling;
    short      floor;

    if ( distance > ACTOR_PATH_RANGE )
        return 0;
    PushMatrix();
    g_sightStep.vx = 0;
    g_sightStep.vy = 0;
    g_sightStep.vz = ACTOR_PATH_STEP;
    g_velocityRotation.vx = 0;
    g_velocityRotation.vy = ( angle + ANGLE_180 ) & ANGLE_MASK;
    g_velocityRotation.vz = 0;
    Geo_RotateVector( &g_velocityRotation, &g_sightStep, &g_sightStep );
    g_sightPoint.vx = 0;
    g_sightPoint.vy = 0;
    g_sightPoint.vz = ACTOR_RADIUS;
    g_pathSidePoint.vx = 0;
    g_pathSidePoint.vy = 0;
    g_pathSidePoint.vz = ACTOR_RADIUS;
    g_velocityRotation.vy = ( angle + ANGLE_90 ) & ANGLE_MASK;
    Geo_RotateVectorOffset( &g_velocityRotation, &g_sightPoint, &actor->position, &g_sightPoint );
    g_velocityRotation.vy = ( angle + ANGLE_270 ) & ANGLE_MASK;
    Geo_RotateVectorOffset( &g_velocityRotation, &g_pathSidePoint, &actor->position, &g_pathSidePoint );
    PopMatrix();
    climb = ACTOR_MAX_CLIMB;
    drop = ACTOR_MAX_DROP;
    if ( actor->flags & ACTOR_FLAG_ON_CEILING ) {
        climb = ACTOR_CEILING_MAX_CLIMB;
        drop = ACTOR_CEILING_MAX_DROP;
    }
    steps = distance >> ACTOR_PATH_STEP_SHIFT;
    if ( steps < 2 )
        steps = 2;
    actor->cell->marker = 0;
    height = actor->position.vy;
    while ( steps-- != 0U ) {
        x = g_sightPoint.vx + g_levelMap.offsetX;
        z = g_sightPoint.vz + g_levelMap.offsetZ;
        if ( MAP_OUTSIDE( x, z ) ) {
            Actor_Occupy( actor );
            return 0;
        }
        cell = MAP_CELL( x, z );
        if ( ( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE ) || cell->marker != 0 ) {
            Actor_Occupy( actor );
            return 0;
        }
        if ( cell->type >= CELL_TYPE_SLOPE ) {
            height = FloorHeight( x, z );
            if ( actor->kind == ACTOR_KIND_BOSS && ( ceiling = CeilingHeight( x, z ) ) < height + ACTOR_BOSS_HEADROOM ) {
                Actor_Occupy( actor );
                return 0;
            }
        } else {
            ceiling = CeilingHeight( x, z );
            if ( !( actor->flags & ACTOR_FLAG_ON_CEILING ) ) {
                if ( ceiling < height + actor->scale.vy ) {
                    Actor_Occupy( actor );
                    return 0;
                }
                if ( actor->kind == ACTOR_KIND_BOSS && ceiling < height + ACTOR_BOSS_HEADROOM ) {
                    Actor_Occupy( actor );
                    return 0;
                }
                floor = FloorHeight( x, z );
            } else {
                floor = ceiling;
            }
            if ( height < floor && floor - height > climb ) {
                Actor_Occupy( actor );
                return 0;
            }
            if ( floor < height && height - floor > drop ) {
                Actor_Occupy( actor );
                return 0;
            }
            height = floor;
        }
        x = g_pathSidePoint.vx + g_levelMap.offsetX;
        z = g_pathSidePoint.vz + g_levelMap.offsetZ;
        if ( MAP_OUTSIDE( x, z ) ) {
            Actor_Occupy( actor );
            return 0;
        }
        cell = MAP_CELL( x, z );
        if ( ( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE ) || cell->marker != 0 ) {
            Actor_Occupy( actor );
            return 0;
        }
        if ( cell->type >= CELL_TYPE_SLOPE ) {
            height = FloorHeight( x, z );
            if ( actor->kind == ACTOR_KIND_BOSS && ( ceiling = CeilingHeight( x, z ) ) < height + ACTOR_BOSS_HEADROOM ) {
                Actor_Occupy( actor );
                return 0;
            }
        } else {
            ceiling = CeilingHeight( x, z );
            if ( !( actor->flags & ACTOR_FLAG_ON_CEILING ) ) {
                if ( ceiling < height + actor->scale.vy ) {
                    Actor_Occupy( actor );
                    return 0;
                }
                if ( actor->kind == ACTOR_KIND_BOSS && ceiling < height + ACTOR_BOSS_HEADROOM ) {
                    Actor_Occupy( actor );
                    return 0;
                }
                floor = FloorHeight( x, z );
            } else {
                floor = ceiling;
            }
            if ( height < floor && floor - height > climb ) {
                Actor_Occupy( actor );
                return 0;
            }
            if ( floor < height && height - floor > drop ) {
                Actor_Occupy( actor );
                return 0;
            }
            height = floor;
        }
        g_sightPoint.vx += g_sightStep.vx;
        g_sightPoint.vz += g_sightStep.vz;
        g_pathSidePoint.vx += g_sightStep.vx;
        g_pathSidePoint.vz += g_sightStep.vz;
    }
    Actor_Occupy( actor );
    return -1;
}

/*
==================
Actor_LineOfSight
==================
*/
/* name: inferred from the ray it marches from the actor toward the player, failing on walls, floor and ceiling */
int Actor_LineOfSight( actor_t *actor, short angle, u_int distance )
{
    SVECTOR    aim;
    SVECTOR    turn;
    short      steps;
    u_int      x, z;
    mapCell_t *cell;
    u_int      floor;
    u_int      ceiling;

    if ( distance >= ACTOR_SIGHT_RANGE )
        return 0;
    PushMatrix();
    g_sightStep.vx = 0;
    g_sightStep.vy = 0;
    g_sightStep.vz = ACTOR_PATH_STEP;
    g_sightPoint.vx = actor->position.vx;
    g_sightPoint.vy = actor->position.vy + ACTOR_EYE_HEIGHT;
    g_sightPoint.vz = actor->position.vz;
    aim.vz = Geo_PointDistanceXZ( &g_sightPoint, &g_player.position );
    aim.vx = (short)( g_sightPoint.vy - ACTOR_SIGHT_AIM_DROP ) - g_player.position.vy;
    aim.vx = Geo_PointHeadingXZ( &g_zeroVector, &aim ) - ANGLE_180;
    aim.vy = 0;
    aim.vz = 0;
    turn.vx = 0;
    turn.vy = ( angle + ANGLE_180 ) & ANGLE_MASK;
    turn.vz = ANGLE_180;
    Geo_RotateVector( &aim, &g_sightStep, &g_sightStep );
    Geo_RotateVector( &turn, &g_sightStep, &g_sightStep );
    PopMatrix();
    steps = distance >> ACTOR_PATH_STEP_SHIFT;
    if ( steps < 2 )
        steps = 2;
    actor->cell->marker = 0;
    while ( steps-- != 0U ) {
        x = g_sightPoint.vx + g_levelMap.offsetX;
        z = g_sightPoint.vz + g_levelMap.offsetZ;
        if ( MAP_OUTSIDE( x, z ) ) {
            Actor_Occupy( actor );
            return 0;
        }
        cell = MAP_CELL( x, z );
        if ( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE ) {
            Actor_Occupy( actor );
            return 0;
        }
        floor = FloorHeight( x, z );
        if ( cell->marker != 0 ) {
            switch ( cell->marker ) {
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
                Actor_Occupy( actor );
                return 0;
            }
        }
        if ( g_sightPoint.vy < floor ) {
            Actor_Occupy( actor );
            return 0;
        }
        ceiling = CeilingHeight( x, z );
        if ( ceiling < g_sightPoint.vy ) {
            Actor_Occupy( actor );
            return 0;
        }
        g_sightPoint.vx += g_sightStep.vx;
        g_sightPoint.vy += g_sightStep.vy;
        g_sightPoint.vz += g_sightStep.vz;
    }
    Actor_Occupy( actor );
    return -1;
}

/*
==================
Actor_TurnToward
==================
*/
/* name: inferred from its body, which steps angle toward desiredAngle by turnRate */
void Actor_TurnToward( actor_t *actor, int angle )
{
    s16 current;
    s16 target = angle;
    int delta;

    actor->desiredAngle = angle;
    current = actor->angle;
    if ( current != target ) {
        delta = ( (u16)( current - angle ) << ANGLE_SIGN_SHIFT ) >> ANGLE_SIGN_SHIFT;
        if ( delta > 0 ) {
            if ( delta < actor->turnRate )
                actor->angle = target;
            else
                actor->angle = current - actor->turnRate;
            actor->angle &= ANGLE_MASK;
        } else if ( delta < 0 ) {
            if ( -delta < actor->turnRate )
                actor->angle = target;
            else
                actor->angle = current + actor->turnRate;
            actor->angle &= ANGLE_MASK;
        } else {
            actor->angle = target;
        }
        actor->direction = ( (s16)actor->angle + ANGLE_45 / 2 >> ANGLE_OCTANT_SHIFT ) & FACING_MASK;
    }
}

/*
==================
Actor_Stop
==================
*/
/* name: inferred from its body, which stops horizontal movement and any rise */
void Actor_Stop( actor_t *actor )
{
    actor->velocity.vx = 0;
    if ( actor->velocity.vy > 0 )
        actor->velocity.vy = -actor->velocity.vy;
    actor->velocity.vz = 0;
    actor->shotSpeed = 0;
}

/*
==================
Actor_SetVelocity
==================
*/
/* name: inferred from its body, which sets the horizontal velocity to speed along angle, keeping vy */
void Actor_SetVelocity( actor_t *actor, short angle, short speed )
{
    short oldY = actor->velocity.vy;
    actor->velocity.vx = 0;
    actor->velocity.vz = speed;
    g_velocityRotation.vx = 0;
    g_velocityRotation.vy = ( angle + ANGLE_180 ) & ANGLE_MASK;
    g_velocityRotation.vz = 0;
    PushMatrix();
    Geo_RotateVector( &g_velocityRotation, &actor->velocity, &actor->velocity );
    PopMatrix();
    actor->velocity.vy = oldY;
}

/*
==================
Actor_PickTargetNearPlayer
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Actor_PickTargetNearPlayer( actor_t *actor )
{
    short      i;
    u_int      x, z;
    mapCell_t *cell;

    g_wanderRotation.vx = g_wanderRotation.vz = 0;
    for ( i = 0; i < ACTOR_TARGET_TRIES; i++ ) {
        g_wanderOffset.vx = g_wanderOffset.vy = 0;
        g_wanderOffset.vz = ( Sys_Random() & ACTOR_TARGET_SPREAD_MASK ) + TARGET_NEAR_PLAYER_DISTANCE;
        g_wanderRotation.vy = Sys_Random() & ANGLE_MASK;
        PushMatrix();
        Geo_RotateVectorOffset( &g_wanderRotation, &g_wanderOffset, &g_player.position, &actor->target );
        PopMatrix();
        x = actor->target.vx + g_levelMap.offsetX;
        z = actor->target.vz + g_levelMap.offsetZ;
        if ( MAP_OUTSIDE( x, z ) )
            continue;
        cell = MAP_CELL( x, z );
        if ( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE )
            continue;
        if ( cell->marker != 0 && cell->marker != CELL_MARKER_PLAYER )
            continue;
        Actor_MeasureTarget( actor );
        if ( actor->targetClass == ACTOR_RANGE_CLOSE || actor->targetClass == ACTOR_RANGE_OUT )
            continue;
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) )
            return -1;
    }
    return 0;
}

/*
==================
Actor_PlaceActor
==================
*/
static __inline__ void Actor_PlaceActor( actor_t *actor, int index )
{
    layoutRecord_t *layout;
    s32             recordX, recordY;
    u16             offsetX, offsetY;

    layout = &g_levelMap.layoutRecords[index];
    recordX = (s32)layout->x << MAP_CELL_SHIFT;
    offsetX = g_levelMap.offsetX;
    actor->target.vx = ( layout->flags & 1 ) ? recordX - ( offsetX - MAP_CELL_HALF ) : recordX - offsetX;
    actor->target.vy = 0;
    recordY = (s32)layout->y << MAP_CELL_SHIFT;
    offsetY = g_levelMap.offsetZ;
    actor->target.vz = ( layout->flags & 2 ) ? recordY - ( offsetY - MAP_CELL_HALF ) : recordY - offsetY;
}

/*
==================
Actor_TargetTowardPlayer
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_TargetTowardPlayer( actor_t *actor )
{
    layoutRecord_t *layout = &g_levelMap.layoutRecords[actor->layoutIndex];
    short           bestFlow = layout->flow;
    u8              best = actor->layoutIndex;
    layoutRecord_t *next;

    next = &g_levelMap.layoutRecords[layout->links[0]];
    if ( bestFlow < next->flow ) {
        bestFlow = next->flow;
        best = layout->links[0];
    }
    if ( layout->links[1] != LAYOUT_NO_LINK ) {
        next = &g_levelMap.layoutRecords[layout->links[1]];
        if ( bestFlow < next->flow ) {
            bestFlow = next->flow;
            best = layout->links[1];
        }
        if ( layout->links[2] != LAYOUT_NO_LINK ) {
            next = &g_levelMap.layoutRecords[layout->links[2]];
            if ( bestFlow < next->flow ) {
                bestFlow = next->flow;
                best = layout->links[2];
            }
            if ( layout->links[LAYOUT_LINK_LAST] != LAYOUT_NO_LINK ) {
                next = &g_levelMap.layoutRecords[layout->links[LAYOUT_LINK_LAST]];
                if ( bestFlow < next->flow ) {
                    bestFlow = next->flow;
                    best = layout->links[LAYOUT_LINK_LAST];
                }
            }
        }
    }
    Actor_PlaceActor( actor, best );
}

/*
==================
Actor_TargetAwayFromPlayer
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_TargetAwayFromPlayer( actor_t *actor )
{
    layoutRecord_t *layout = &g_levelMap.layoutRecords[actor->layoutIndex];
    short           bestFlow = layout->flow;
    u8              best = actor->layoutIndex;
    layoutRecord_t *next;

    next = &g_levelMap.layoutRecords[layout->links[0]];
    if ( next->flow < bestFlow ) {
        bestFlow = next->flow;
        best = layout->links[0];
    }
    if ( layout->links[1] != LAYOUT_NO_LINK ) {
        next = &g_levelMap.layoutRecords[layout->links[1]];
        if ( next->flow < bestFlow ) {
            bestFlow = next->flow;
            best = layout->links[1];
        }
        if ( layout->links[2] != LAYOUT_NO_LINK ) {
            next = &g_levelMap.layoutRecords[layout->links[2]];
            if ( next->flow < bestFlow ) {
                bestFlow = next->flow;
                best = layout->links[2];
            }
            if ( layout->links[LAYOUT_LINK_LAST] != LAYOUT_NO_LINK ) {
                next = &g_levelMap.layoutRecords[layout->links[LAYOUT_LINK_LAST]];
                if ( next->flow < bestFlow ) {
                    bestFlow = next->flow;
                    best = layout->links[LAYOUT_LINK_LAST];
                }
            }
        }
    }
    Actor_PlaceActor( actor, best );
}

/*
==================
Actor_TargetLayoutCentre
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_TargetLayoutCentre( actor_t *actor )
{
    Actor_PlaceActor( actor, actor->layoutIndex );
}

/*
==================
Actor_PickChaseTarget
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Actor_PickChaseTarget( actor_t *actor )
{
    u_int      x, z;
    mapCell_t *cell;

    g_wanderRotation.vz = 0;
    g_wanderRotation.vx = 0;
    g_wanderOffset.vy = 0;
    g_wanderOffset.vx = 0;
    Actor_TargetTowardPlayer( actor );
    Actor_MeasureTarget( actor );
    if ( actor->targetClass == ACTOR_RANGE_CLOSE || actor->targetClass >= ACTOR_RANGE_OUT )
        goto fallback;
    x = actor->target.vx + g_levelMap.offsetX;
    z = actor->target.vz + g_levelMap.offsetZ;
    if ( MAP_OUTSIDE( x, z ) )
        goto fallback;
    cell = MAP_CELL( x, z );
    if ( !( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE )
         && ( cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER ) ) {
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) )
            return -1;
    }
    g_wanderOffset.vz = ( Sys_Random() & ACTOR_TARGET_SPREAD_MASK ) + TARGET_STEP_DISTANCE;
    g_wanderRotation.vy = Sys_Random() & ANGLE_MASK;
    PushMatrix();
    Geo_RotateVectorOffset( &g_wanderRotation, &g_wanderOffset, &actor->target, &actor->target );
    PopMatrix();
    Actor_MeasureTarget( actor );
    if ( actor->targetClass == ACTOR_RANGE_CLOSE || actor->targetClass >= ACTOR_RANGE_OUT )
        goto fallback;
    x = actor->target.vx + g_levelMap.offsetX;
    z = actor->target.vz + g_levelMap.offsetZ;
    if ( MAP_OUTSIDE( x, z ) )
        goto fallback;
    cell = MAP_CELL( x, z );
    if ( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE )
        goto fallback;
    if ( cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER ) {
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) )
            return -1;
    }
fallback:
    Actor_TargetLayoutCentre( actor );
    Actor_MeasureTarget( actor );
    if ( actor->targetClass == ACTOR_RANGE_CLOSE || actor->targetClass >= ACTOR_RANGE_OUT )
        return 0;
    x = actor->target.vx + g_levelMap.offsetX;
    z = actor->target.vz + g_levelMap.offsetZ;
    if ( MAP_OUTSIDE( x, z ) )
        return 0;
    cell = MAP_CELL( x, z );
    if ( !( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE )
         && ( cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER ) ) {
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) )
            return -1;
    }
    g_wanderOffset.vz = ( Sys_Random() & ACTOR_TARGET_SPREAD_MASK ) + TARGET_STEP_DISTANCE;
    g_wanderRotation.vy = Sys_Random() & ANGLE_MASK;
    PushMatrix();
    Geo_RotateVectorOffset( &g_wanderRotation, &g_wanderOffset, &actor->target, &actor->target );
    PopMatrix();
    Actor_MeasureTarget( actor );
    if ( actor->targetClass == ACTOR_RANGE_CLOSE || actor->targetClass >= ACTOR_RANGE_OUT )
        return 0;
    x = actor->target.vx + g_levelMap.offsetX;
    z = actor->target.vz + g_levelMap.offsetZ;
    if ( MAP_OUTSIDE( x, z ) )
        return 0;
    cell = MAP_CELL( x, z );
    if ( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE )
        return 0;
    if ( cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER ) {
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) )
            return -1;
    }
    return 0;
}

/*
==================
Actor_PickFleeTarget
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Actor_PickFleeTarget( actor_t *actor )
{
    u_int      x, z;
    mapCell_t *cell;

    g_wanderRotation.vz = 0;
    g_wanderRotation.vx = 0;
    g_wanderOffset.vy = 0;
    g_wanderOffset.vx = 0;
    Actor_TargetAwayFromPlayer( actor );
    Actor_MeasureTarget( actor );
    if ( actor->targetClass == ACTOR_RANGE_CLOSE || actor->targetClass >= ACTOR_RANGE_OUT )
        return 0;
    x = actor->target.vx + g_levelMap.offsetX;
    z = actor->target.vz + g_levelMap.offsetZ;
    if ( MAP_OUTSIDE( x, z ) )
        return 0;
    cell = MAP_CELL( x, z );
    if ( !( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE )
         && ( cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER ) ) {
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) )
            return -1;
    }
    g_wanderOffset.vz = ( Sys_Random() & ACTOR_TARGET_SPREAD_MASK ) + TARGET_STEP_DISTANCE;
    g_wanderRotation.vy = Sys_Random() & ANGLE_MASK;
    PushMatrix();
    Geo_RotateVectorOffset( &g_wanderRotation, &g_wanderOffset, &actor->target, &actor->target );
    PopMatrix();
    Actor_MeasureTarget( actor );
    if ( actor->targetClass == ACTOR_RANGE_CLOSE || actor->targetClass >= ACTOR_RANGE_OUT )
        return 0;
    x = actor->target.vx + g_levelMap.offsetX;
    z = actor->target.vz + g_levelMap.offsetZ;
    if ( MAP_OUTSIDE( x, z ) )
        return 0;
    cell = MAP_CELL( x, z );
    if ( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE )
        return 0;
    if ( cell->marker == 0 || cell->marker == CELL_MARKER_PLAYER ) {
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) )
            return -1;
    }
    return 0;
}

/*
==================
Actor_SeesPlayer
==================
*/
/* name: inferred from its body, which tests the player against the actor's view cone */
int Actor_SeesPlayer( actor_t *actor )
{
    s16 delta;

    if ( actor->cell->mask & g_visibleCellMask ) {
        delta = ( (u16)( actor->angle - actor->playerAngle ) << ANGLE_SIGN_SHIFT ) >> ANGLE_SIGN_SHIFT;
        if ( actor->viewCone >= (s16)ABS( delta ) )
            return 1;
        return 0;
    }
    return 0;
}

/*
==================
Actor_HearsPlayer
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Actor_HearsPlayer( actor_t *actor )
{
    mapCell_t *cell = actor->cell;

    if ( cell->face != 0 && ( cell->mask & g_visibleCellMask ) == 0 )
        return 0;
    if ( actor->playerDistance < actor->reach >> 1 )
        return -1;
    if ( actor->playerDistance < actor->reach && g_player.noiseTimer )
        return -1;
    return 0;
}

/*
==================
Actor_PickTargetBehind
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Actor_PickTargetBehind( actor_t *actor )
{
    short      i;
    short      heading;
    u_int      spread;
    u_int      x, z;
    mapCell_t *cell;

    g_wanderOffset.vy = 0;
    g_wanderOffset.vx = 0;
    g_wanderRotation.vz = 0;
    g_wanderRotation.vx = 0;
    g_wanderOffset.vz = TARGET_BEHIND_DISTANCE;
    PushMatrix();
    for ( i = 0; i < ACTOR_TARGET_TRIES; i++ ) {
        spread = ( Sys_Random() & ( ANGLE_90 - 1 ) ) + ANGLE_180;
        heading = ( actor->angle + spread - ANGLE_45 ) & ANGLE_MASK;
        g_wanderRotation.vy = heading;
        Geo_RotateVectorOffset( &g_wanderRotation, &g_wanderOffset, &actor->position, &actor->target );
        x = actor->target.vx + g_levelMap.offsetX;
        z = actor->target.vz + g_levelMap.offsetZ;
        if ( MAP_OUTSIDE( x, z ) )
            continue;
        cell = MAP_CELL( x, z );
        if ( cell->type >= CELL_TYPE_WALL && cell->type < CELL_TYPE_SLOPE )
            continue;
        if ( cell->marker != 0 )
            continue;
        Actor_MeasureTarget( actor );
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) ) {
            PopMatrix();
            return -1;
        }
    }
    PopMatrix();
    return 0;
}

/*
==================
Actor_StartAttack
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_StartAttack( TASK *node, actor_t *actor )
{
    actor->engageCooldown = ACTOR_CHARGE_COOLDOWN;
    actor->action = ACTOR_LEAP;
    if ( actor->playerClass < ACTOR_RANGE_MID ) {
        switch ( actor->kind ) {
        case ACTOR_KIND_FACEHUGGER:
            if ( actor->playerClass == ACTOR_RANGE_CLOSE && actor->playerDelta.vy < ACTOR_ATTACK_HEIGHT + 1
                 && !( g_player.flags & PLAYER_HUGGER_ATTACHED ) && Actor_StartHuggerOverlay( actor ) ) {
                g_player.flags |= PLAYER_HUGGER_ATTACHED;
                node->callback = Actor_HuggerClingTask;
                node->state[1] = HUGGER_CLING_TIME;
                node->state[2] = 0;
                actor->cell->marker = 0;
                break;
            }
            Actor_SetVelocity( actor, actor->angle, HUGGER_LEAP_SPEED );
            Actor_PlayAnimation( actor, HUGGER_ANIM_LEAP );
            if ( actor->velocity.vy == 0 )
                actor->velocity.vy = ACTOR_LEAP_LIFT;
            break;
        case ACTOR_KIND_DOG_CEILING:
            Actor_ToFloor( actor, WARRIOR_ANIM_HURT_FRONT );
            break;
        case ACTOR_KIND_CHESTBURSTER:
            Actor_SetVelocity( actor, actor->angle, BURSTER_LEAP_SPEED );
            Actor_PlayAnimation( actor, BURSTER_ANIM_LEAP );
            if ( actor->velocity.vy == 0 )
                actor->velocity.vy = ACTOR_LEAP_LIFT;
            break;
        case ACTOR_KIND_WARRIOR_CEILING:
            Actor_ToFloor( actor, WARRIOR_ANIM_HURT_BACK );
            break;
        case ACTOR_KIND_BOSS:
            goto randomStrike;
        case ACTOR_KIND_BAMBI:
            Actor_PlayAnimation( actor, WARRIOR_ANIM_ATTACK );
            break;
        case ACTOR_KIND_GUARD:
        case ACTOR_KIND_SOLDIER:
            Actor_PlayAnimation( actor, GUARD_ANIM_FIRE );
            break;
        case ACTOR_KIND_DOG:
        case ACTOR_KIND_SYNTHETIC:
            Actor_PlayAnimation( actor, SYNTH_ANIM_ATTACK );
            break;
        case ACTOR_KIND_WARRIOR:
        case ACTOR_KIND_HANDLER:
            Actor_PlayAnimation( actor, WARRIOR_ANIM_ATTACK_CLOSE );
            break;
        case ACTOR_KIND_EGG:
        case ACTOR_KIND_FLAME_JET_VERTICAL:
            break;
        }
    } else {
        switch ( actor->kind ) {
        case ACTOR_KIND_FACEHUGGER:
            Actor_SetVelocity( actor, actor->angle, HUGGER_LEAP_SPEED );
            Actor_PlayAnimation( actor, HUGGER_ANIM_LEAP );
            if ( actor->velocity.vy == 0 )
                actor->velocity.vy = ACTOR_LEAP_LIFT;
            break;
        case ACTOR_KIND_CHESTBURSTER:
            Actor_SetVelocity( actor, actor->angle, BURSTER_LEAP_SPEED );
            Actor_PlayAnimation( actor, BURSTER_ANIM_LEAP );
            if ( actor->velocity.vy == 0 )
                actor->velocity.vy = ACTOR_LEAP_LIFT;
            break;
        case ACTOR_KIND_WARRIOR_CEILING:
            Actor_ToFloor( actor, WARRIOR_ANIM_HURT_BACK );
            break;
        case ACTOR_KIND_BOSS:
randomStrike:
            switch ( (short)( Sys_Random() & QUEEN_STRIKE_MASK ) ) {
            case QUEEN_STRIKE_STAB:
                Actor_PlayAnimation( actor, QUEEN_ANIM_STRIKE );
                break;
            case QUEEN_STRIKE_SWIPE:
                Actor_PlayAnimation( actor, QUEEN_ANIM_SWIPE );
                break;
            case QUEEN_STRIKE_SWIPE_MIRRORED:
                Actor_PlayAnimation( actor, QUEEN_ANIM_SWIPE );
                actor->sprite.flags |= SPRITE_FLAG_MIRRORED;
                break;
            default:
                Actor_PlayAnimation( actor, QUEEN_ANIM_BITE );
                break;
            case QUEEN_STRIKE_BITE:
                Actor_PlayAnimation( actor, QUEEN_ANIM_BITE );
                break;
            }
            break;
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_WARRIOR:
            Actor_PlayAnimation( actor, WARRIOR_ANIM_ATTACK );
            break;
        case ACTOR_KIND_GUARD:
        case ACTOR_KIND_SOLDIER:
            Actor_PlayAnimation( actor, GUARD_ANIM_FIRE );
            break;
        case ACTOR_KIND_SYNTHETIC:
            Actor_PlayAnimation( actor, SYNTH_ANIM_ATTACK );
            break;
        case ACTOR_KIND_DOG:
            Actor_PlayAnimation( actor, DOG_ANIM_ATTACK );
            break;
        case ACTOR_KIND_HANDLER:
            switch ( (short)( Sys_Random() & HANDLER_ATTACK_MASK ) ) {
            case 0:
            case 1:
            case 2:
                Actor_PlayAnimation( actor, WARRIOR_ANIM_ATTACK_CLOSE );
                break;
            default:
                actor->flags |= ACTOR_FLAG_GRENADE;
                Actor_PlayAnimation( actor, HANDLER_ANIM_GRENADE );
                break;
            }
            break;
        case ACTOR_KIND_EGG:
        case ACTOR_KIND_FLAME_JET_VERTICAL:
            break;
        }
    }
}

/*
==================
Actor_MeleeHit
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_MeleeHit( actor_t *actor )
{
    if ( actor->playerClass < ACTOR_RANGE_MID ) {
        switch ( actor->kind ) {
        case ACTOR_KIND_FACEHUGGER:
        case ACTOR_KIND_CHESTBURSTER:
            if ( Actor_SeesPlayer( actor )
                 && ( actor->playerClass == ACTOR_RANGE_CLOSE || Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) )
                Player_Hit( HUGGER_BITE_DAMAGE, DEATH_FACEHUGGER );
            break;
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_DOG:
            if ( Actor_SeesPlayer( actor )
                 && ( actor->playerClass == ACTOR_RANGE_CLOSE || Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) )
                Player_HitFrom( DOG_BITE_DAMAGE, ( actor->angle + ANGLE_180 ) & ANGLE_MASK, DEATH_DOG );
            break;
        case ACTOR_KIND_WARRIOR:
            if ( Actor_SeesPlayer( actor )
                 && ( actor->playerClass == ACTOR_RANGE_CLOSE || Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) )
                Player_HitFrom( WARRIOR_CLAW_DAMAGE, ( actor->angle + ANGLE_180 ) & ANGLE_MASK, DEATH_WARRIOR );
            break;
        case ACTOR_KIND_BOSS:
            if ( Actor_SeesPlayer( actor )
                 && ( actor->playerClass == ACTOR_RANGE_CLOSE || Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) )
                Player_HitFrom( QUEEN_STRIKE_DAMAGE, ( actor->angle + ANGLE_180 ) & ANGLE_MASK, DEATH_QUEEN );
            break;
        case ACTOR_KIND_EGG:
        case ACTOR_KIND_FLAME_JET_VERTICAL:
            break;
        }
    } else if ( actor->playerClass == ACTOR_RANGE_MID ) {
        switch ( actor->kind ) {
        case ACTOR_KIND_FACEHUGGER:
        case ACTOR_KIND_CHESTBURSTER:
            if ( Actor_SeesPlayer( actor ) && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) )
                Player_Hit( HUGGER_BITE_DAMAGE_FAR, DEATH_FACEHUGGER );
            break;
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_DOG:
            if ( Actor_SeesPlayer( actor ) && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) )
                Player_HitFrom( DOG_BITE_DAMAGE_FAR, ( actor->angle + ANGLE_180 ) & ANGLE_MASK, DEATH_DOG );
            break;
        case ACTOR_KIND_WARRIOR:
            if ( Actor_SeesPlayer( actor ) && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) )
                Player_HitFrom( WARRIOR_CLAW_DAMAGE_FAR, ( actor->angle + ANGLE_180 ) & ANGLE_MASK, DEATH_WARRIOR );
            break;
        case ACTOR_KIND_BOSS:
            if ( Actor_SeesPlayer( actor ) && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) )
                Player_HitFrom( QUEEN_STRIKE_DAMAGE_FAR, ( actor->angle + ANGLE_180 ) & ANGLE_MASK, DEATH_QUEEN );
            break;
        case ACTOR_KIND_EGG:
        case ACTOR_KIND_FLAME_JET_VERTICAL:
            break;
        }
    }
}

/*
==================
Actor_CreatureTask
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_CreatureTask( TASK *task )
{
    actor_t *actor = (actor_t *)task->state[0];
    int      clear;

    if ( actor->stunTimer != 0 )
        actor->stunTimer--;
    if ( actor->idleTimer != 0 )
        actor->idleTimer--;
    if ( actor->engageCooldown != 0 )
        actor->engageCooldown--;
    Actor_Move( actor );
    Actor_MeasurePlayer( actor );
    if ( actor->playerClass == ACTOR_RANGE_OUT ) {
        if ( ( g_gameFlags & GAME_FLAG_HARD ) && actor->action == ACTOR_DEAD ) {
            actor->health = Actor_SlotHealth( &g_actorSlots[actor->slotIndex] );
            actor->flags = 0;
            actor->action = CREATURE_IDLE;
            actor->idleTimer = ACTOR_WAKE_DELAY;
        }
        Actor_Stop( actor );
        g_nextTask = task->Next;
        return;
    }
    if ( actor->action != ACTOR_DEAD )
        actor->playerAngle = Geo_PointHeadingXZ( &actor->position, &g_player.position ) & ANGLE_MASK;
    switch ( actor->action ) {
    case CREATURE_IDLE:
        if ( actor->idleTimer == 0 && CHEAT_IS_ACTIVE( CHEAT_FLAG_ACTORS_WAKE ) )
            actor->action = CREATURE_ALERT;
        else if ( actor->flags & ACTOR_FLAG_ALERTED )
            Actor_TurnToward( actor, actor->playerAngle );
        Actor_FaceView( actor );
        break;
    case CREATURE_ALERT:
        if ( actor->flags & ACTOR_FLAG_RETREAT ) {
            if ( Actor_PickFleeTarget( actor ) ) {
                actor->action = CREATURE_MOVE;
                break;
            }
            actor->flags &= ~ACTOR_FLAG_RETREAT;
        }
        if ( actor->flags & ACTOR_FLAG_ALERTED ) {
            if ( Actor_PickTargetNearPlayer( actor ) || Actor_PickChaseTarget( actor ) || Actor_PickTargetBehind( actor ) ) {
                actor->action = CREATURE_MOVE;
                break;
            }
            if ( actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT && actor->playerDelta.vy < ACTOR_ATTACK_HEIGHT + 1
                 && ( actor->playerClass == ACTOR_RANGE_CLOSE || Actor_LineOfSight( actor, actor->playerAngle, actor->playerDistance ) ) )
                actor->action = CREATURE_ENGAGE;
            break;
        }
        if ( Actor_HearsPlayer( actor ) ) {
            actor->flags |= ACTOR_FLAG_ALERTED;
            break;
        }
        if ( !Actor_SeesPlayer( actor ) )
            break;
        if ( actor->playerClass == ACTOR_RANGE_CLOSE && actor->playerDelta.vy < ACTOR_ATTACK_HEIGHT + 1
             && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
            actor->action = CREATURE_ENGAGE;
            break;
        }
        if ( Actor_LineOfSight( actor, actor->playerAngle, actor->playerDistance )
             || Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) )
            actor->flags |= ACTOR_FLAG_ALERTED;
        break;
    case CREATURE_MOVE:
        Actor_MeasureTarget( actor );
        if ( Actor_PassedTarget( actor ) ) {
            Actor_Stop( actor );
            actor->action = CREATURE_ENGAGE;
            break;
        }
        if ( !( actor->flags & ACTOR_FLAG_RETREAT ) && actor->engageCooldown == 0 ) {
            if ( actor->playerClass == ACTOR_RANGE_CLOSE && actor->playerDelta.vy < ACTOR_ATTACK_HEIGHT + 1 ) {
                actor->engageCooldown = ACTOR_CHARGE_COOLDOWN;
                Actor_Stop( actor );
                actor->action = CREATURE_ENGAGE;
                break;
            }
            if ( actor->engageCooldown == 0 && actor->playerClass != ACTOR_RANGE_CLOSE && actor->playerClass < ACTOR_RANGE_FAR ) {
                if ( actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT )
                    clear = Actor_LineOfSight( actor, actor->playerAngle, actor->playerDistance );
                else
                    clear = Actor_PathClear( actor, actor->playerAngle, actor->playerDistance );
                if ( clear ) {
                    actor->engageCooldown = ACTOR_CHARGE_COOLDOWN;
                    Actor_Stop( actor );
                    actor->action = CREATURE_ENGAGE;
                    break;
                }
            }
        }
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        Actor_TurnToward( actor, actor->targetAngle );
        Actor_FaceView( actor );
        if ( actor->angle != actor->desiredAngle )
            break;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) ) {
            Actor_SetVelocity( actor, actor->angle, actor->speed );
        } else {
            Actor_Stop( actor );
            actor->action = CREATURE_IDLE;
            actor->idleTimer = ACTOR_PAUSE;
        }
        break;
    case CREATURE_ENGAGE:
        if ( actor->flags & ACTOR_FLAG_RETREAT ) {
            actor->action = CREATURE_ALERT;
            break;
        }
        if ( actor->playerClass >= ACTOR_RANGE_FAR ) {
            actor->action = CREATURE_ALERT;
            break;
        }
        if ( actor->playerClass == ACTOR_RANGE_CLOSE && actor->playerDelta.vy < ACTOR_BITE_HEIGHT + 1
             && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
            Actor_TurnToward( actor, actor->playerAngle );
            Actor_FaceView( actor );
            if ( actor->angle == actor->desiredAngle )
                Actor_StartAttack( task, actor );
            break;
        }
        if ( actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT ) {
            if ( Actor_LineOfSight( actor, actor->playerAngle, actor->playerDistance ) ) {
                Actor_TurnToward( actor, actor->playerAngle );
                Actor_FaceView( actor );
                if ( actor->angle == actor->desiredAngle )
                    Actor_StartAttack( task, actor );
                break;
            }
        } else if ( actor->playerDelta.vy < ACTOR_BITE_HEIGHT + 1
                    && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
            Actor_TurnToward( actor, actor->playerAngle );
            Actor_FaceView( actor );
            if ( actor->angle == actor->desiredAngle )
                Actor_StartAttack( task, actor );
            break;
        }
        actor->action = CREATURE_ALERT;
        break;
    case ACTOR_LEAP:
        Actor_TurnToward( actor, actor->playerAngle );
        if ( actor->sprite.flags & SPRITE_FLAG_CUE ) {
            if ( actor->flags & ACTOR_FLAG_GRENADE )
                Particle_ActorLobGrenade( actor );
            else
                Particle_ActorFireBullet( actor );
            actor->flags &= ~ACTOR_FLAG_GRENADE;
            actor->sprite.flags &= ~SPRITE_FLAG_CUE;
        }
        if ( !( actor->sprite.flags & SPRITE_FLAG_ENDED ) )
            break;
        Actor_Stop( actor );
        Actor_MeleeHit( actor );
        if ( actor->kind == ACTOR_KIND_FACEHUGGER ) {
            actor->flags |= ACTOR_FLAG_RETREAT;
            Actor_FacePlayer( actor );
            actor->action = CREATURE_ALERT;
            break;
        }
        if ( actor->playerClass < ACTOR_RANGE_FAR ) {
            if ( actor->playerClass == ACTOR_RANGE_CLOSE && actor->playerDelta.vy < ACTOR_ATTACK_HEIGHT + 1
                 && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
                Actor_FacePlayer( actor );
                Actor_StartAttack( task, actor );
                break;
            }
            if ( actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT ) {
                if ( actor->playerClass == ACTOR_RANGE_NEAR && Actor_LineOfSight( actor, actor->playerAngle, actor->playerDistance ) ) {
                    Actor_FacePlayer( actor );
                    Actor_StartAttack( task, actor );
                    break;
                }
                if ( Actor_PickTargetNearPlayer( actor ) ) {
                    actor->engageCooldown = ACTOR_RETARGET_COOLDOWN;
                    Actor_FaceTarget( actor );
                    actor->action = CREATURE_MOVE;
                    break;
                }
                if ( Actor_LineOfSight( actor, actor->playerAngle, actor->playerDistance ) ) {
                    Actor_FacePlayer( actor );
                    Actor_StartAttack( task, actor );
                    break;
                }
            } else {
                if ( Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
                    Actor_FacePlayer( actor );
                    Actor_StartAttack( task, actor );
                    break;
                }
                if ( Actor_PickTargetNearPlayer( actor ) ) {
                    actor->engageCooldown = ACTOR_RETARGET_COOLDOWN;
                    Actor_FaceTarget( actor );
                    actor->action = CREATURE_MOVE;
                    break;
                }
            }
        } else if ( Actor_PickTargetNearPlayer( actor ) ) {
            Actor_FaceTarget( actor );
            actor->action = CREATURE_MOVE;
            break;
        }
        Actor_FacePlayer( actor );
        actor->action = CREATURE_IDLE;
        actor->idleTimer = ACTOR_PAUSE_SHORT;
        break;
    case ACTOR_HURT:
        if ( !( actor->sprite.flags & SPRITE_FLAG_ENDED ) )
            break;
        Actor_Stop( actor );
        if ( actor->kind == ACTOR_KIND_CHESTBURSTER ) {
            actor->flags |= ACTOR_FLAG_RETREAT;
            Actor_FacePlayer( actor );
            actor->action = CREATURE_ALERT;
            break;
        }
        if ( actor->playerClass < ACTOR_RANGE_FAR ) {
            if ( actor->playerClass != ACTOR_RANGE_CLOSE || actor->playerDelta.vy >= ACTOR_ATTACK_HEIGHT + 1 ) {
                if ( actor->kind >= ACTOR_KIND_GUARD && actor->kind < ACTOR_KIND_INERT ) {
                    if ( !Actor_LineOfSight( actor, actor->playerAngle, actor->playerDistance ) ) {
wander:
                        if ( Actor_PickTargetBehind( actor ) ) {
                            actor->engageCooldown = ACTOR_CHARGE_COOLDOWN;
                            Actor_FaceTarget( actor );
                            actor->action = CREATURE_MOVE;
                            break;
                        }
                        Actor_FacePlayer( actor );
                        actor->action = CREATURE_IDLE;
                        actor->idleTimer = ACTOR_PAUSE;
                        break;
                    }
                } else if ( !Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
                    goto wander;
                }
            }
            Actor_FacePlayer( actor );
            Actor_StartAttack( task, actor );
            break;
        }
        if ( Actor_PickTargetBehind( actor ) ) {
            actor->engageCooldown = ACTOR_CHARGE_COOLDOWN;
            Actor_FaceTarget( actor );
            actor->action = CREATURE_MOVE;
            break;
        }
        Actor_FacePlayer( actor );
        actor->action = CREATURE_IDLE;
        actor->idleTimer = ACTOR_PAUSE;
        break;
    case ACTOR_DYING:
        if ( actor->flags & ACTOR_FLAG_NO_CORPSE )
            goto remove;
        if ( !( actor->sprite.flags & SPRITE_FLAG_HALTED ) )
            break;
        actor->action = ACTOR_DEAD;
        Actor_Stop( actor );
        if ( actor->cell->type >= CELL_TYPE_SLOPE ) {
            switch ( actor->cell->type ) {
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_POS_Z ):
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_POS_Z ):
                actor->velocity.vx = 0;
                actor->velocity.vz = -actor->speed;
                break;
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_NEG_X ):
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_NEG_X ):
                actor->velocity.vz = 0;
                actor->velocity.vx = actor->speed;
                break;
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_POS_X ):
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_POS_X ):
                actor->velocity.vz = 0;
                actor->velocity.vx = -actor->speed;
                break;
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_NEG_Z ):
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_NEG_Z ):
                actor->velocity.vx = 0;
                actor->velocity.vz = actor->speed;
                break;
            }
        }
        break;
    case ACTOR_DEAD:
        if ( actor->cell->type == CELL_TYPE_DOOR && actor->cell->ceiling - actor->cell->value < DOOR_SHUT_GAP ) {
remove:
            Actor_Unlink( actor );
            Actor_Free( actor );
            TaskDie( task );
            return;
        }
        if ( actor->cell->type >= CELL_TYPE_SLOPE )
            break;
        Actor_Stop( actor );
        if ( Menu_GetDifficulty() == DIFFICULTY_ACID_REIGN )
            break;
        switch ( actor->kind ) {
        case ACTOR_KIND_FACEHUGGER:
        case ACTOR_KIND_CHESTBURSTER:
        case ACTOR_KIND_BAMBI:
        case ACTOR_KIND_DOG:
        case ACTOR_KIND_WARRIOR:
            if ( actor->cell->type == CELL_TYPE_FLOOR )
                actor->cell->type = CELL_TYPE_REMAINS;
            break;
        }
        break;
    }
    if ( !( actor->sprite.flags & ( SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED ) ) && actor->action >= CREATURE_MOVE ) {
        StepSprite( &actor->sprite );
        if ( actor->sprite.flags & SPRITE_FLAG_SOUND )
            Snd_PlayAtPosition( actor->sprite.sound, &actor->position );
    }
    Actor_Project( actor );
    g_nextTask = task->Next;
}

/*
==================
Actor_SpawnLeaping
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
actor_t *Actor_SpawnLeaping( actor_t *actor, short index )
{
    actor_t     *other;
    actorSlot_t *slot;
    unsigned int x, z;
    short        angle;
    if ( index == ACTOR_INDEX_NONE )
        return 0;
    x = ( (unsigned int)( actor->position.vx + g_levelMap.offsetX ) ) >> MAP_CELL_SHIFT;
    slot = &g_actorSlots[index];
    z = ( (unsigned int)( actor->position.vz + g_levelMap.offsetZ ) ) >> MAP_CELL_SHIFT;
    slot->gridX = x;
    slot->gridZ = z;
    other = Actor_Spawn( index );
    if ( !other )
        return 0;
    angle = actor->playerAngle;
    other->idleTimer = 0;
    other->desiredAngle = angle;
    other->angle = angle;
    other->direction = ( ( angle + ANGLE_45 / 2 ) >> ANGLE_OCTANT_SHIFT ) & FACING_MASK;
    other->shownDirection = other->direction;
    other->flags |= ACTOR_FLAG_ALERTED;
    Actor_SetVelocity( other, other->angle, ACTOR_SPAWN_LEAP_SPEED );
    other->velocity.vy = ACTOR_SPAWN_LEAP_LIFT;
    other->action = ACTOR_LEAP;
    return other;
}

/*
==================
Actor_EggReleaseHugger
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
int Actor_EggReleaseHugger( actor_t *actor )
{
    actor_t *other = Actor_SpawnLeaping( actor, actor->attachedIndex );
    actor->attachedIndex = ACTOR_INDEX_NONE;
    if ( other )
        Actor_PlayAnimation( other, HUGGER_ANIM_LEAP );
    return -1;
}

/*
==================
Actor_ColonistReleaseBurster
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
int Actor_ColonistReleaseBurster( actor_t *actor )
{
    actor_t *other = Actor_SpawnLeaping( actor, actor->speed );
    actor->speed = ACTOR_INDEX_NONE;
    if ( other ) {
        Actor_PlayAnimation( other, BURSTER_ANIM_EMERGE );
        other->position.vy = other->height + COLONIST_HANG_HEIGHT;
        Actor_SetVelocity( other, other->angle, BURSTER_EMERGE_SPEED );
        other->velocity.vy = BURSTER_EMERGE_LIFT;
    }
    return -1;
}

/*
==================
Actor_EggTask
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_EggTask( TASK *task )
{
    actor_t *a = (actor_t *)task->state[0];
    if ( a->stunTimer != 0 )
        a->stunTimer--;
    if ( a->idleTimer != 0 )
        a->idleTimer--;
    Actor_Move( a );
    Actor_MeasurePlayer( a );
    if ( a->playerClass != ACTOR_RANGE_OUT ) {
        if ( a->action != ACTOR_DEAD )
            a->playerAngle = (s16)( Geo_PointHeadingXZ( &a->position, &g_player.position ) & ANGLE_MASK );
        switch ( a->action ) {
        case EGG_CLOSED:
            if ( a->idleTimer == 0 && ( a->sprite.flags & SPRITE_FLAG_HALTED ) )
                a->action = EGG_WAKING;
            break;
        case EGG_WAKING:
            if ( a->playerClass < ACTOR_RANGE_FAR ) {
                Actor_PlayAnimation( a, EGG_ANIM_OPEN );
                task->state[1] = EGG_SHELL_OPENING;
                a->action = EGG_OPENING;
            } else {
                Actor_PlayAnimation( a, EGG_ANIM_CLOSED );
                a->action = EGG_CLOSED;
                a->idleTimer = (s16)( ( Sys_Random() & EGG_CLOSE_DELAY_MASK ) + EGG_CLOSE_DELAY );
            }
            break;
        case EGG_OPENING:
            if ( a->sprite.flags & SPRITE_FLAG_HALTED ) {
                task->state[1] = EGG_SHELL_OPEN;
                a->action = EGG_OPEN;
            }
            break;
        case EGG_OPEN:
            if ( a->playerClass < ACTOR_RANGE_MID && Actor_LineOfSight( a, a->playerAngle, a->playerDistance )
                 && Actor_EggReleaseHugger( a ) ) {
                task->state[1] = EGG_SHELL_HATCHED;
                Actor_Die( a );
            }
            break;
        case ACTOR_HURT:
            if ( a->stunTimer == 0 ) {
                if ( task->state[1] == EGG_SHELL_OPENING )
                    a->action = EGG_OPENING;
                else if ( a->sprite.flags & SPRITE_FLAG_ENDED ) {
                    if ( task->state[1] == EGG_SHELL_CLOSED ) {
                        Actor_PlayAnimation( a, EGG_ANIM_OPEN );
                        task->state[1] = EGG_SHELL_OPENING;
                        a->action = EGG_OPENING;
                    } else if ( task->state[1] == EGG_SHELL_OPEN )
                        a->action = EGG_OPEN;
                }
            }
            break;
        case ACTOR_DYING:
            if ( a->sprite.flags & SPRITE_FLAG_HALTED )
                a->action = ACTOR_DEAD;
            break;
        case ACTOR_DEAD:
            break;
        default:
            break;
        }
        if ( ( a->sprite.flags & ( SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED ) ) == 0 ) {
            StepSprite( &a->sprite );
            if ( a->sprite.flags & SPRITE_FLAG_SOUND )
                Snd_PlayAtPosition( a->sprite.sound, &a->position );
        }
        Actor_Project( a );
    }
    g_nextTask = task->Next;
}

/*
==================
Actor_UpdateJet
==================
*/
/* name: inferred from its messages: a flame jet or steam valve actor; it sprays particles while
 * the player is near and reports its shutdown once its slot group is incomplete. Returns -1 while spraying. */
short Actor_UpdateJet( TASK *task, short kind, short period, short speed )
{
    actor_t     *actor = (actor_t *)task->state[0];
    short        spraying = 0;
    short        steam = 0;
    actorSlot_t *slot;
    short        phase;

    if ( actor->kind == ACTOR_KIND_STEAM_JET_VERTICAL || actor->kind == ACTOR_KIND_STEAM_JET )
        steam = -1;
    if ( ++task->state[ACTOR_STATE_COUNTER] > actor->farRange )
        task->state[ACTOR_STATE_COUNTER] = 0;
    slot = &g_actorSlots[actor->slotIndex];
    if ( slot->group < slot->groupSize ) {
        if ( !( actor->flags & ACTOR_FLAG_SHUT_DOWN ) ) {
            actor->flags |= ACTOR_FLAG_SHUT_DOWN;
            g_player.tallies[actor->kind]++;
            if ( steam )
                Hud_ShowMessage( "Steam valve closed" );
            else
                Hud_ShowMessage( "Flame jet shut down" );
        }
        return 0;
    }
    if ( !( actor->cell->mask & g_visibleCellMask ) )
        return 0;
    Actor_MeasurePlayer( actor );
    if ( actor->playerDistance > JET_ACTIVE_RANGE )
        return 0;
    switch ( actor->action ) {
    case JET_IDLE:
        if ( actor->health <= 0 ) {
            task->state[1] = 0;
            task->state[ACTOR_STATE_COUNTER] = 0;
            task->state[2] = -1;
            actor->action = ACTOR_DEAD;
        }
        break;
    case ACTOR_HURT:
        task->state[1] = 0;
        task->state[ACTOR_STATE_COUNTER] = 0;
        actor->stunTimer = 0;
        actor->action = JET_IDLE;
        actor->idleTimer = 1;
        break;
    case ACTOR_DYING:
        Particle_SpawnExplosion( actor->position.vx, actor->position.vy, actor->position.vz );
        actor->action = ACTOR_DEAD;
        break;
    case ACTOR_DEAD:
        phase = task->state[ACTOR_STATE_COUNTER] - actor->nearRange;
        if ( phase < 0 || phase > actor->speed ) {
            spraying = -1;
            if ( task->state[2] == -1 )
                task->state[2] = Snd_PlayAtPosition( steam ? SOUND_STEAM_JET : SOUND_FLAME_JET, &actor->position );
            if ( ++task->state[1] > period ) {
                task->state[1] = 0;
                if ( actor->attachedIndex >= BYTE_SIGN_BIT )
                    kind = -kind;
                Particle_JetSpray( &actor->position, speed, actor->angle, kind );
            }
        }
        break;
    default:
        actor->action = ACTOR_HURT;
        break;
    }
    return spraying;
}

/*
==================
Actor_SteamJetTask
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_SteamJetTask( TASK *node )
{
    if ( !Actor_UpdateJet( node, PARTICLE_KIND_STEAM, STEAM_JET_PERIOD, STEAM_JET_SPEED ) ) {
        if ( node->state[2] != -1 ) {
            Snd_Stop( node->state[2] );
            node->state[2] = -1;
        }
    }
    g_nextTask = node->Next;
}

/*
==================
Actor_FlameJetTask
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Actor_FlameJetTask( TASK *node )
{
    if ( !Actor_UpdateJet( node, PARTICLE_KIND_FIRE, JET_PERIOD, FLAME_JET_SPEED ) ) {
        Snd_Stop( node->state[2] );
        node->state[2] = -1;
    }
    g_nextTask = node->Next;
}

/*
==================
Actor_FlameJetVerticalTask
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Actor_FlameJetVerticalTask( TASK *node )
{
    if ( !Actor_UpdateJet( node, PARTICLE_KIND_FIRE_VERTICAL, JET_PERIOD, FLAME_JET_VERTICAL_SPEED ) ) {
        Snd_Stop( node->state[2] );
        node->state[2] = -1;
    }
    g_nextTask = node->Next;
}

/*
==================
Actor_SteamJetVerticalTask
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Actor_SteamJetVerticalTask( TASK *node )
{
    if ( !Actor_UpdateJet( node, PARTICLE_KIND_STEAM_VERTICAL, JET_PERIOD, STEAM_JET_VERTICAL_SPEED ) ) {
        Snd_Stop( node->state[2] );
        node->state[2] = -1;
    }
    g_nextTask = node->Next;
}

/*
==================
Actor_QueenTask

Actor_QueenTask is the update task of the Queen (kind 7), an actor that ambushes from an idle
animation (task->state[1] steps it)
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Actor_QueenTask( TASK *task )
{
    actor_t *actor = (actor_t *)task->state[0];

    if ( actor->stunTimer != 0 )
        actor->stunTimer--;
    if ( actor->idleTimer != 0 )
        actor->idleTimer--;
    if ( actor->engageCooldown != 0 )
        actor->engageCooldown--;
    Actor_Move( actor );
    Actor_MeasurePlayer( actor );
    if ( actor->playerClass == ACTOR_RANGE_OUT ) {
        Actor_Stop( actor );
        g_nextTask = task->Next;
        return;
    }
    if ( actor->action != ACTOR_DEAD )
        actor->playerAngle = Geo_PointHeadingXZ( &actor->position, &g_player.position ) & ANGLE_MASK;
    switch ( actor->action ) {
    case CREATURE_IDLE:
        if ( task->state[1] == QUEEN_AWAKE ) {
            if ( actor->idleTimer == 0 && CHEAT_IS_ACTIVE( CHEAT_FLAG_ACTORS_WAKE ) )
                actor->action = CREATURE_ALERT;
            Actor_FaceView( actor );
        } else if ( task->state[1] == QUEEN_RISING ) {
            if ( actor->sprite.flags & SPRITE_FLAG_ENDED ) {
                task->state[1] = QUEEN_AWAKE;
                Actor_FaceView( actor );
            }
        } else if ( task->state[1] == QUEEN_WAKING ) {
            if ( !( actor->sprite.flags & SPRITE_FLAG_ENDED ) )
                break;
            if ( actor->playerClass < ACTOR_RANGE_MID ) {
                task->state[1] = QUEEN_RISING;
                Actor_PlayAnimation( actor, QUEEN_ANIM_RISE );
            } else {
                task->state[1] = QUEEN_DORMANT;
                Actor_PlayAnimation( actor, QUEEN_ANIM_DORMANT );
                actor->idleTimer = QUEEN_DORMANT_DELAY;
            }
        } else {
            if ( actor->idleTimer == 0 && CHEAT_IS_ACTIVE( CHEAT_FLAG_ACTORS_WAKE ) && actor->playerClass < ACTOR_RANGE_FAR ) {
                task->state[1] = QUEEN_WAKING;
                Actor_PlayAnimation( actor, QUEEN_ANIM_WAKE );
            }
        }
        break;
    case CREATURE_ALERT:
        if ( actor->flags & ACTOR_FLAG_ALERTED ) {
            if ( Actor_PickTargetNearPlayer( actor ) || Actor_PickChaseTarget( actor ) || Actor_PickTargetBehind( actor ) )
                actor->action = CREATURE_MOVE;
            break;
        }
        if ( Actor_HearsPlayer( actor )
             || ( Actor_SeesPlayer( actor )
                  && ( actor->playerClass == ACTOR_RANGE_CLOSE || Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) ) )
            actor->flags |= ACTOR_FLAG_ALERTED;
        break;
    case CREATURE_MOVE:
        Actor_MeasureTarget( actor );
        if ( Actor_PassedTarget( actor ) ) {
            Actor_Stop( actor );
            actor->action = CREATURE_ENGAGE;
            break;
        }
        if ( actor->engageCooldown == 0 ) {
            if ( actor->playerClass == ACTOR_RANGE_CLOSE
                 || ( actor->playerClass < ACTOR_RANGE_FAR && Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) ) {
                actor->engageCooldown = ACTOR_CHARGE_COOLDOWN;
                Actor_Stop( actor );
                actor->action = CREATURE_ENGAGE;
                break;
            }
        }
        actor->targetAngle = Geo_PointHeadingXZ( &actor->position, &actor->target ) & ANGLE_MASK;
        Actor_TurnToward( actor, actor->targetAngle );
        Actor_FaceView( actor );
        if ( actor->angle != actor->desiredAngle )
            break;
        if ( Actor_PathClear( actor, actor->targetAngle, actor->targetDistance ) ) {
            Actor_SetVelocity( actor, actor->angle, actor->speed );
        } else {
            Actor_Stop( actor );
            actor->action = CREATURE_IDLE;
            actor->idleTimer = ACTOR_PAUSE;
        }
        break;
    case CREATURE_ENGAGE:
        if ( actor->playerClass < ACTOR_RANGE_FAR ) {
            if ( actor->playerClass == ACTOR_RANGE_CLOSE ) {
                Actor_TurnToward( actor, actor->playerAngle );
                Actor_FaceView( actor );
                if ( actor->angle == actor->desiredAngle )
                    Actor_StartAttack( task, actor );
                break;
            }
            if ( Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
                Actor_TurnToward( actor, actor->playerAngle );
                Actor_FaceView( actor );
                if ( actor->angle == actor->desiredAngle )
                    Actor_StartAttack( task, actor );
                break;
            }
        }
        actor->action = CREATURE_ALERT;
        break;
    case ACTOR_LEAP:
        Actor_TurnToward( actor, actor->playerAngle );
        if ( !( actor->sprite.flags & SPRITE_FLAG_ENDED ) )
            break;
        actor->sprite.flags &= ~SPRITE_FLAG_MIRRORED;
        Actor_Stop( actor );
        Actor_MeleeHit( actor );
        if ( actor->playerClass < ACTOR_RANGE_FAR ) {
            if ( actor->playerClass == ACTOR_RANGE_CLOSE || Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
                Actor_FacePlayer( actor );
                Actor_StartAttack( task, actor );
                break;
            }
            if ( Actor_PickTargetNearPlayer( actor ) ) {
                actor->engageCooldown = ACTOR_RETARGET_COOLDOWN;
                Actor_FaceTarget( actor );
                actor->action = CREATURE_MOVE;
                break;
            }
        } else if ( Actor_PickTargetNearPlayer( actor ) ) {
            Actor_FaceTarget( actor );
            actor->action = CREATURE_MOVE;
            break;
        }
        Actor_FacePlayer( actor );
        actor->action = CREATURE_IDLE;
        actor->idleTimer = ACTOR_PAUSE_SHORT;
        break;
    case ACTOR_HURT:
        if ( !( actor->sprite.flags & SPRITE_FLAG_ENDED ) )
            break;
        Actor_Stop( actor );
        if ( task->state[1] == QUEEN_RISING ) {
            task->state[1] = QUEEN_AWAKE;
            Actor_FaceView( actor );
        }
        if ( task->state[1] < QUEEN_RISING ) {
            task->state[1] = QUEEN_RISING;
            Actor_PlayAnimation( actor, QUEEN_ANIM_RISE );
            actor->action = CREATURE_IDLE;
            actor->idleTimer = QUEEN_RECOVER_DELAY;
            break;
        }
        if ( actor->playerClass < ACTOR_RANGE_FAR ) {
            if ( actor->playerClass == ACTOR_RANGE_CLOSE || Actor_PathClear( actor, actor->playerAngle, actor->playerDistance ) ) {
                Actor_FacePlayer( actor );
                Actor_StartAttack( task, actor );
                break;
            }
            if ( Actor_PickTargetBehind( actor ) ) {
                actor->engageCooldown = ACTOR_CHARGE_COOLDOWN;
                Actor_FaceTarget( actor );
                actor->action = CREATURE_MOVE;
                break;
            }
            Actor_FacePlayer( actor );
            actor->action = CREATURE_IDLE;
            actor->idleTimer = ACTOR_PAUSE;
            break;
        }
        if ( Actor_PickTargetBehind( actor ) ) {
            actor->engageCooldown = ACTOR_CHARGE_COOLDOWN;
            Actor_FaceTarget( actor );
            actor->action = CREATURE_MOVE;
            break;
        }
        Actor_FacePlayer( actor );
        actor->action = CREATURE_IDLE;
        actor->idleTimer = ACTOR_PAUSE;
        break;
    case ACTOR_DYING:
        if ( !( actor->sprite.flags & SPRITE_FLAG_HALTED ) )
            break;
        actor->action = ACTOR_DEAD;
        if ( actor->cell->type >= CELL_TYPE_SLOPE ) {
            switch ( actor->cell->type ) {
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_POS_Z ):
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_POS_Z ):
                actor->velocity.vx = 0;
                actor->velocity.vz = -actor->speed;
                break;
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_NEG_X ):
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_NEG_X ):
                actor->velocity.vz = 0;
                actor->velocity.vx = actor->speed;
                break;
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_POS_X ):
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_POS_X ):
                actor->velocity.vz = 0;
                actor->velocity.vx = -actor->speed;
                break;
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_STEP, SLOPE_RISES_NEG_Z ):
            case CELL_TYPE_SLOPE_AT( SLOPE_BAND_RAMP, SLOPE_RISES_NEG_Z ):
                actor->velocity.vx = 0;
                actor->velocity.vz = actor->speed;
                break;
            }
        }
        break;
    case ACTOR_DEAD:
        if ( actor->attachedIndex != ACTOR_INDEX_NONE ) {
            Trigger_RunLevelCommands( actor->attachedIndex );
            actor->attachedIndex = ACTOR_INDEX_NONE;
        }
        if ( actor->cell->type == CELL_TYPE_DOOR && actor->cell->ceiling - actor->cell->value < DOOR_SHUT_GAP ) {
            Actor_Unlink( actor );
            Actor_Free( actor );
            TaskDie( task );
            return;
        }
        if ( actor->cell->type >= CELL_TYPE_SLOPE )
            break;
        Actor_Stop( actor );
        if ( Menu_GetDifficulty() == DIFFICULTY_ACID_REIGN )
            break;
        if ( actor->cell->type == CELL_TYPE_FLOOR )
            actor->cell->type = CELL_TYPE_REMAINS;
        break;
    }
    if ( !( actor->sprite.flags & ( SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED ) ) ) {
        StepSprite( &actor->sprite );
        if ( actor->sprite.flags & SPRITE_FLAG_SOUND )
            Snd_PlayAtPosition( actor->sprite.sound, &actor->position );
    }
    Actor_Project( actor );
    g_nextTask = task->Next;
}

/*
==================
Actor_ColonistTask

Actor_ColonistTask is the update task of a stationary actor that turns its shown
direction toward the player and plays a different animation set by the player's octant
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Actor_ColonistTask( TASK *task )
{
    actor_t *actor = (actor_t *)task->state[0];
    short    direction;

    Actor_MeasurePlayer( actor );
    if ( actor->playerClass == ACTOR_RANGE_OUT ) {
        g_nextTask = task->Next;
        return;
    }
    if ( actor->stunTimer > 0 )
        actor->stunTimer--;
    if ( actor->idleTimer > 0 )
        actor->idleTimer--;
    actor->playerAngle = Geo_PointHeadingXZ( &actor->position, &g_player.position ) & ANGLE_MASK;
    direction = ( actor->direction - ( ( ( actor->playerAngle + ANGLE_180 + ANGLE_45 / 2 ) >> ANGLE_OCTANT_SHIFT ) & FACING_MASK ) )
                & FACING_MASK;
    switch ( actor->action ) {
    case COLONIST_IDLE:
        if ( actor->idleTimer == 0 ) {
            if ( actor->playerClass < ACTOR_RANGE_MID && direction == COLONIST_REACH_OCTANT ) {
                Actor_PlayAnimation( actor, COLONIST_ANIM_BURST );
                actor->action = COLONIST_BURSTING;
                actor->idleTimer = ( Sys_Random() & COLONIST_DELAY_MASK ) + COLONIST_BURST_DELAY;
            } else if ( actor->playerClass < ACTOR_RANGE_FAR ) {
                if ( direction == COLONIST_REACH_OCTANT )
                    Actor_PlayAnimation( actor, COLONIST_ANIM_REACH );
                else
                    Snd_PlayAtPosition( SOUND_COLONIST, &actor->position );
                actor->action = COLONIST_REACHING;
                actor->idleTimer = ( Sys_Random() & COLONIST_DELAY_MASK ) + COLONIST_REACH_DELAY;
            }
        } else if ( actor->shownDirection != direction ) {
            actor->shownDirection = direction;
            Actor_PlayAnimation( actor, direction + COLONIST_ANIM_COCOONED );
        }
        break;
    case COLONIST_REACHING:
        if ( ( actor->sprite.flags & SPRITE_FLAG_ENDED ) || direction != COLONIST_REACH_OCTANT ) {
            Actor_PlayAnimation( actor, direction + COLONIST_ANIM_COCOONED );
            actor->action = COLONIST_IDLE;
        }
        break;
    case COLONIST_BURSTING:
        if ( actor->sprite.flags & SPRITE_FLAG_ENDED ) {
            actor->action = COLONIST_EMPTY;
            Actor_ColonistReleaseBurster( actor );
            if ( actor->attachedIndex != ACTOR_INDEX_NONE )
                Object_ActorDropItem( actor );
        } else if ( direction != COLONIST_REACH_OCTANT ) {
            Actor_PlayAnimation( actor, direction + COLONIST_ANIM_COCOONED );
            actor->action = COLONIST_IDLE;
        }
        break;
    case COLONIST_EMPTY:
        if ( actor->shownDirection != direction ) {
            actor->shownDirection = direction;
            Actor_PlayAnimation( actor, direction );
        }
        break;
    case ACTOR_HURT:
        if ( actor->sprite.flags & SPRITE_FLAG_ENDED ) {
            Actor_PlayAnimation( actor, direction + COLONIST_ANIM_COCOONED );
            actor->action = COLONIST_IDLE;
            actor->idleTimer = ACTOR_PAUSE;
        }
        break;
    case ACTOR_DYING:
        if ( actor->sprite.flags & SPRITE_FLAG_HALTED ) {
            actor->action = ACTOR_DEAD;
            Actor_PlayAnimation( actor, direction + COLONIST_ANIM_DEAD );
        }
        break;
    case ACTOR_DEAD:
        if ( actor->shownDirection != direction ) {
            actor->shownDirection = direction;
            Actor_PlayAnimation( actor, direction + COLONIST_ANIM_DEAD );
        }
        break;
    }
    if ( !( actor->sprite.flags & ( SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED ) ) ) {
        StepSprite( &actor->sprite );
        if ( actor->sprite.flags & SPRITE_FLAG_SOUND )
            Snd_PlayAtPosition( actor->sprite.sound, &actor->position );
    }
    Actor_Project( actor );
    g_nextTask = task->Next;
}
