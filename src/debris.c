/* debris.c -- debris panels: a pool of 128 four-vertex panels that breakable walls burst into */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <CAMERA.H>
#include <VIEW.H>
#include <LEVEL.H>
#include <MODEL.H>
#include <PARTICLE.H>
#include <HEAP.H>
#include <GAME.H>
#include <TEXTURE.H>
#include <DEBRIS.H>

#define PANEL_COUNT 128
#define PANEL_FREE 0xffff   /* dispatchRow_t.count of a pool panel not in use */
#define PANEL_PALETTE 0x0a  /* palette entry (flags & ROW_PALETTE_MASK) of a fresh panel. name: invented */
#define PANEL_VERTICES 4    /* a panel is a quad. name: invented */
#define PANEL_GRID_POINTS 9 /* the 3x3 vertex offsets of g_panelGridXY and g_panelGridYz. name: invented */

/* Debris tuning: a random value is base + (Sys_Random() & mask), a jitter (Sys_Random() & mask) - jitter.
 * names: invented */
#define DEBRIS_SPEED_MIN 0x10
#define DEBRIS_SPEED_MASK 0xf
#define PANEL_LIFT_JITTER 8 /* Debris_LaunchPanel */
#define PANEL_LIFT_JITTER_MASK 0xf
#define PANEL_LIFE 0x10
#define PANEL_LIFE_MASK 0xf
#define PANEL_SIZE 0x20 /* particle_t.size of a panel or shard */
#define PANEL_SIZE_MASK 0xf
#define PANEL_CENTRE_JITTER 0x100 /* Debris_SpawnPanel */
#define PANEL_CENTRE_JITTER_MASK 0x1ff
#define SHARD_CENTRE_JITTER 0x80 /* Debris_SplitShard */
#define SHARD_CENTRE_JITTER_MASK 0xff
#define SHARD_LIFE 0x20
#define SHARD_LIFE_EXPLOSIVE 40
#define SHARD_LIFE_MASK 0xf
#define SHARD_SPREAD 0x10
#define SHARD_SPREAD_MASK 0x1f


/* Row-major positions in the 3x3 grid of g_panelGridXY / g_panelGridYz. names: invented */
enum {
    PANEL_GRID_TOP_LEFT,
    PANEL_GRID_TOP_MIDDLE,
    PANEL_GRID_TOP_RIGHT,
    PANEL_GRID_MIDDLE_LEFT,
    PANEL_GRID_CENTRE,
    PANEL_GRID_MIDDLE_RIGHT,
    PANEL_GRID_BOTTOM_LEFT,
    PANEL_GRID_BOTTOM_MIDDLE,
    PANEL_GRID_BOTTOM_RIGHT
};

/* A random debris speed of 16..31. */
#define DEBRIS_SPEED() ( ( Sys_Random() & DEBRIS_SPEED_MASK ) + DEBRIS_SPEED_MIN )

/* 3x3 vertex offsets of a panel facing z, and of one facing x.
 * names: inferred from the wall orientations Debris_ShatterWall uses them for */
SVECTOR g_panelGridXY[PANEL_GRID_POINTS] = {
    { -512, -512, 0 }, { 0, -512, 0 },   { 512, -512, 0 }, { -512, 0, 0 },  { 0, 0, 0 },
    { 512, 0, 0 },     { -512, 512, 0 }, { 0, 512, 0 },    { 512, 512, 0 },
};

SVECTOR g_panelGridYz[PANEL_GRID_POINTS] = {
    { 0, -512, -512 }, { 0, -512, 0 },   { 0, -512, 512 }, { 0, 0, -512 },  { 0, 0, 0 },
    { 0, 0, 512 },     { 0, 512, -512 }, { 0, 512, 0 },    { 0, 512, 512 },
};

/* Module statics (.sbss 0x8008d2c8..0x8008d300), in their retail order.
 * names: inferred from Debris_InitPanelPool, which points each panel at four pool vertices */
static SVECTOR       *g_panelVertices;
static dispatchRow_t *g_panels;
/* The split points of Debris_SplitShard: the midpoints of the panel's top and left edges, its
 * jittered centre, and the midpoints of its right and bottom edges. names: invented */
static SVECTOR g_splitTop;
static SVECTOR g_splitLeft;
static SVECTOR g_splitCentre;
static SVECTOR g_splitRight;
static SVECTOR g_splitBottom;

/*
==================
Debris_PanelPoolAlloc
==================
*/
/* name: inferred from its body, which allocates the panel, vertex and work tables */
void Debris_PanelPoolAlloc( void )
{
    g_panelVertices = Heap_Alloc( PANEL_COUNT * PANEL_VERTICES * sizeof( SVECTOR ) );
    g_panels = Heap_Alloc( PANEL_COUNT * sizeof( dispatchRow_t ) );
    g_debrisUvs = Heap_Alloc( PANEL_COUNT * sizeof( spriteUV_t ) );
}

/*
==================
Debris_InitPanelPool
==================
*/
/* name: inferred from its body, which gives every panel its four vertices */
void Debris_InitPanelPool( void )
{
    dispatchRow_t *panel = g_panels;
    SVECTOR       *vertex = g_panelVertices;
    short          i;
    for ( i = 0; i < PANEL_COUNT; i++ ) {
        panel->vertices[QUAD_VERTEX_TOP_LEFT] = vertex++;
        panel->vertices[QUAD_VERTEX_TOP_RIGHT] = vertex++;
        panel->vertices[QUAD_VERTEX_BOTTOM_RIGHT] = vertex++;
        panel->vertices[QUAD_VERTEX_BOTTOM_LEFT] = vertex++;
        panel->count = PANEL_FREE;
        panel->selector = ROW_SELECT_QUAD_NOCULL;
        panel->flags = ROW_FLAG_LAST | PANEL_PALETTE;
        panel++;
    }
}

/*
==================
Debris_ClaimPanel

First free panel, linked to its own index; 0 when the pool is exhausted.
==================
*/
/* name: inferred from its body, which claims the first free panel */
dispatchRow_t *Debris_ClaimPanel( void )
{
    dispatchRow_t *panel = g_panels;
    short          i = 0;
    while ( i < PANEL_COUNT ) {
        if ( panel->count == PANEL_FREE ) {
            panel->count = i;
            return panel;
        }
        i++;
        panel++;
    }
    return 0;
}

/*
==================
Debris_FreePanel
==================
*/
/* name: inferred from its body, which frees the panel a debris particle carries */
void Debris_FreePanel( particle_t *particle )
{
    particle->model->count = PANEL_FREE;
    particle->model->selector = ROW_SELECT_QUAD_NOCULL;
}

/*
==================
Debris_LaunchPanel

Starts a debris particle carrying panel: at position, moving at velocity with a
little random lift, for 16..31 frames.
==================
*/
/* name: invented */
static __inline__ void Debris_LaunchPanel( particle_t *particle, dispatchRow_t *panel, SVECTOR *position, SVECTOR *velocity )
{
    copyVector( particle->position, position );
    particle->velocity->vx = velocity->vx;
    particle->velocity->vy = velocity->vy + ( Sys_Random() & PANEL_LIFT_JITTER_MASK ) - PANEL_LIFT_JITTER;
    particle->velocity->vz = velocity->vz;
    particle->life = ( Sys_Random() & PANEL_LIFE_MASK ) + PANEL_LIFE;
    particle->size = ( Sys_Random() & PANEL_SIZE_MASK ) + PANEL_SIZE;
    particle->kind = PARTICLE_KIND_PANEL;
    particle->model = panel;
    SET_VECTOR_TO_ZERO( particle->rotation );
    Particle_Link( particle );
}

/*
==================
Debris_SpawnPanel

Splits the 3x3 grid into four panels around a random centre point, textured with
the level's wall texture split at its centre, each carried by a debris particle.
Returns 0 (taking nothing) when the particle or panel pool runs out.
==================
*/
/* RULE-EXCEPTION: R19, each texture word is copied once into the four panels (one lw, four sw), then
 * the centre u/v bytes are re-stored */
int Debris_SpawnPanel( SVECTOR *grid, SVECTOR *position, SVECTOR *velocity )
{
    particle_t    *p0, *p1, *p2, *p3;
    dispatchRow_t *f0, *f1, *f2, *f3;
    spriteUV_t    *texture;
    spriteUV_t    *t0, *t1, *t2, *t3;
    u_int          u, v;
    short          cx, cy, cz;

    p0 = Particle_Alloc();
    if ( p0 == 0 )
        return 0;
    p1 = Particle_Alloc();
    if ( p1 == 0 ) {
        Particle_Free( p0 );
        return 0;
    }
    p2 = Particle_Alloc();
    if ( p2 == 0 ) {
        Particle_Free( p0 );
        Particle_Free( p1 );
        return 0;
    }
    p3 = Particle_Alloc();
    if ( p3 == 0 ) {
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        return 0;
    }
    f0 = Debris_ClaimPanel();
    if ( f0 == 0 ) {
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        Particle_Free( p3 );
        return 0;
    }
    f1 = Debris_ClaimPanel();
    if ( f1 == 0 ) {
        f0->count = PANEL_FREE;
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        Particle_Free( p3 );
        return 0;
    }
    f2 = Debris_ClaimPanel();
    if ( f2 == 0 ) {
        f0->count = PANEL_FREE;
        f1->count = PANEL_FREE;
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        Particle_Free( p3 );
        return 0;
    }
    f3 = Debris_ClaimPanel();
    if ( f3 == 0 ) {
        f0->count = PANEL_FREE;
        f1->count = PANEL_FREE;
        f2->count = PANEL_FREE;
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        Particle_Free( p3 );
        return 0;
    }
    copyVector( f0->vertices[QUAD_VERTEX_TOP_LEFT], &grid[PANEL_GRID_TOP_LEFT] );
    copyVector( f0->vertices[QUAD_VERTEX_TOP_RIGHT], &grid[PANEL_GRID_TOP_MIDDLE] );
    copyVector( f0->vertices[QUAD_VERTEX_BOTTOM_LEFT], &grid[PANEL_GRID_MIDDLE_LEFT] );
    copyVector( f1->vertices[QUAD_VERTEX_TOP_LEFT], &grid[PANEL_GRID_TOP_MIDDLE] );
    copyVector( f1->vertices[QUAD_VERTEX_TOP_RIGHT], &grid[PANEL_GRID_TOP_RIGHT] );
    copyVector( f1->vertices[QUAD_VERTEX_BOTTOM_RIGHT], &grid[PANEL_GRID_MIDDLE_RIGHT] );
    copyVector( f2->vertices[QUAD_VERTEX_TOP_LEFT], &grid[PANEL_GRID_MIDDLE_LEFT] );
    copyVector( f2->vertices[QUAD_VERTEX_BOTTOM_RIGHT], &grid[PANEL_GRID_BOTTOM_MIDDLE] );
    copyVector( f2->vertices[QUAD_VERTEX_BOTTOM_LEFT], &grid[PANEL_GRID_BOTTOM_LEFT] );
    copyVector( f3->vertices[QUAD_VERTEX_TOP_RIGHT], &grid[PANEL_GRID_MIDDLE_RIGHT] );
    copyVector( f3->vertices[QUAD_VERTEX_BOTTOM_RIGHT], &grid[PANEL_GRID_BOTTOM_RIGHT] );
    copyVector( f3->vertices[QUAD_VERTEX_BOTTOM_LEFT], &grid[PANEL_GRID_BOTTOM_MIDDLE] );

    cx = ( Sys_Random() & PANEL_CENTRE_JITTER_MASK ) - PANEL_CENTRE_JITTER;
    cy = ( Sys_Random() & PANEL_CENTRE_JITTER_MASK ) - PANEL_CENTRE_JITTER;
    cz = ( Sys_Random() & PANEL_CENTRE_JITTER_MASK ) - PANEL_CENTRE_JITTER;
    f0->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vx = cx;
    f0->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vy = cy;
    f0->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vz = cz;
    f1->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vx = cx;
    f1->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vy = cy;
    f1->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vz = cz;
    f2->vertices[QUAD_VERTEX_TOP_RIGHT]->vx = cx;
    f2->vertices[QUAD_VERTEX_TOP_RIGHT]->vy = cy;
    f2->vertices[QUAD_VERTEX_TOP_RIGHT]->vz = cz;
    f3->vertices[QUAD_VERTEX_TOP_LEFT]->vx = cx;
    f3->vertices[QUAD_VERTEX_TOP_LEFT]->vy = cy;
    f3->vertices[QUAD_VERTEX_TOP_LEFT]->vz = cz;

    if ( g_level == LEVEL_131_MED_LAB ) {
        texture = &g_spriteUvs[SPRITE_UV_WALL_PANEL_MED_LAB];
    } else {
        texture = &g_spriteUvs[SPRITE_UV_WALL_PANEL];
        if ( g_level == LEVEL_211_LIVING_QUARTERS )
            texture = &g_spriteUvs[SPRITE_UV_WALL_PANEL_LIVING_QUARTERS];
    }
    t0 = &g_debrisUvs[f0->count];
    t1 = &g_debrisUvs[f1->count];
    t2 = &g_debrisUvs[f2->count];
    t3 = &g_debrisUvs[f3->count];
    *(u_long *)&t0->u0 = *(u_long *)&t1->u0 = *(u_long *)&t2->u0 = *(u_long *)&t3->u0 = *(u_long *)&texture->u0;
    *(u_long *)&t0->u1 = *(u_long *)&t1->u1 = *(u_long *)&t2->u1 = *(u_long *)&t3->u1 = *(u_long *)&texture->u1;
    *(u_long *)&t0->u2 = *(u_long *)&t1->u2 = *(u_long *)&t2->u2 = *(u_long *)&t3->u2 = *(u_long *)&texture->u2;
    *(u_long *)&t0->u3 = *(u_long *)&t1->u3 = *(u_long *)&t2->u3 = *(u_long *)&t3->u3 = *(u_long *)&texture->u3;
    u = texture->u0 + texture->u1 + texture->u2 + texture->u3;
    u >>= 2;
    v = texture->v0 + texture->v1 + texture->v2 + texture->v3;
    v >>= 2;
    t0->u1 = u;
    t0->u3 = u;
    t0->v3 = v;
    t0->v2 = v;
    t1->u0 = u;
    t1->u2 = u;
    t1->v2 = v;
    t1->v3 = v;
    t2->u1 = u;
    t2->v1 = v;
    t2->u3 = u;
    t2->v0 = v;
    t3->u0 = u;
    t3->v0 = v;
    t3->v1 = v;
    t3->u2 = u;

    Debris_LaunchPanel( p0, f0, position, velocity );
    Debris_LaunchPanel( p1, f1, position, velocity );
    Debris_LaunchPanel( p2, f2, position, velocity );
    Debris_LaunchPanel( p3, f3, position, velocity );
    return -1;
}

/*
==================
Debris_ShatterWall

Bursts a breakable wall (a levelObject_t of type 0x1c, from Object_BlockObjectTask) into debris
panels along its width or depth.
==================
*/
/* name: inferred from that */
void Debris_ShatterWall( levelObject_t *source )
{
    SVECTOR position;
    SVECTOR velocity;
    short   i;

    switch ( source->facing ) {
    case FACING_NEG_Z:
    case FACING_POS_Z:
        position.vx = source->position.vx;
        position.vz = source->position.vz + ( source->depth - 1 ) * MAP_CELL_HALF;
        if ( position.vz < g_player.position.vz ) {
            for ( i = 0; i < source->width; i++ ) {
                velocity.vx = 0;
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = -( DEBRIS_SPEED() );
                position.vy = source->position.vy + MAP_CELL_HALF;
                if ( Debris_SpawnPanel( g_panelGridXY, &position, &velocity ) == 0 )
                    return;
                position.vy = source->position.vy + ( MAP_CELL_HALF + MAP_CELL_SIZE );
                velocity.vx = 0;
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = -( DEBRIS_SPEED() );
                if ( Debris_SpawnPanel( g_panelGridXY, &position, &velocity ) == 0 )
                    return;
                if ( g_level == LEVEL_212_CANTEEN ) {
                    position.vy = source->position.vy + ( MAP_CELL_HALF + 2 * MAP_CELL_SIZE );
                    velocity.vx = 0;
                    velocity.vy = DEBRIS_SPEED();
                    velocity.vz = -( DEBRIS_SPEED() );
                    if ( Debris_SpawnPanel( g_panelGridXY, &position, &velocity ) == 0 )
                        return;
                }
                position.vx += MAP_CELL_SIZE;
            }
        } else {
            for ( i = 0; i < source->width; i++ ) {
                velocity.vx = 0;
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = DEBRIS_SPEED();
                position.vy = source->position.vy + MAP_CELL_HALF;
                if ( Debris_SpawnPanel( g_panelGridXY, &position, &velocity ) == 0 )
                    return;
                velocity.vx = 0;
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = DEBRIS_SPEED();
                position.vy = source->position.vy + ( MAP_CELL_HALF + MAP_CELL_SIZE );
                if ( Debris_SpawnPanel( g_panelGridXY, &position, &velocity ) == 0 )
                    return;
                if ( g_level == LEVEL_212_CANTEEN ) {
                    position.vy = source->position.vy + ( MAP_CELL_HALF + 2 * MAP_CELL_SIZE );
                    velocity.vx = 0;
                    velocity.vy = DEBRIS_SPEED();
                    velocity.vz = DEBRIS_SPEED();
                    if ( Debris_SpawnPanel( g_panelGridXY, &position, &velocity ) == 0 )
                        return;
                }
                position.vx += MAP_CELL_SIZE;
            }
        }
        break;
    case FACING_POS_X:
    case FACING_NEG_X:
        position.vx = source->position.vx + ( source->width - 1 ) * MAP_CELL_HALF;
        position.vz = source->position.vz;
        if ( position.vx < g_player.position.vx ) {
            for ( i = 0; i < source->depth; i++ ) {
                velocity.vx = -( DEBRIS_SPEED() );
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = 0;
                position.vy = source->position.vy + MAP_CELL_HALF;
                if ( Debris_SpawnPanel( g_panelGridYz, &position, &velocity ) == 0 )
                    return;
                velocity.vx = -( DEBRIS_SPEED() );
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = 0;
                position.vy = source->position.vy + ( MAP_CELL_HALF + MAP_CELL_SIZE );
                if ( Debris_SpawnPanel( g_panelGridYz, &position, &velocity ) == 0 )
                    return;
                if ( g_level == LEVEL_212_CANTEEN ) {
                    position.vy = source->position.vy + ( MAP_CELL_HALF + 2 * MAP_CELL_SIZE );
                    velocity.vx = -( DEBRIS_SPEED() );
                    velocity.vy = DEBRIS_SPEED();
                    velocity.vz = 0;
                    if ( Debris_SpawnPanel( g_panelGridXY, &position, &velocity ) == 0 )
                        return;
                }
                position.vz += MAP_CELL_SIZE;
            }
        } else {
            for ( i = 0; i < source->depth; i++ ) {
                velocity.vx = DEBRIS_SPEED();
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = 0;
                position.vy = source->position.vy + MAP_CELL_HALF;
                if ( Debris_SpawnPanel( g_panelGridYz, &position, &velocity ) == 0 )
                    return;
                velocity.vx = DEBRIS_SPEED();
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = 0;
                position.vy = source->position.vy + ( MAP_CELL_HALF + MAP_CELL_SIZE );
                if ( Debris_SpawnPanel( g_panelGridYz, &position, &velocity ) == 0 )
                    return;
                if ( g_level == LEVEL_212_CANTEEN ) {
                    position.vy = source->position.vy + ( MAP_CELL_HALF + 2 * MAP_CELL_SIZE );
                    velocity.vx = DEBRIS_SPEED();
                    velocity.vy = DEBRIS_SPEED();
                    velocity.vz = 0;
                    if ( Debris_SpawnPanel( g_panelGridXY, &position, &velocity ) == 0 )
                        return;
                }
                position.vz += MAP_CELL_SIZE;
            }
        }
        break;
    }
}

/* Starts a debris particle carrying panel from the shattered object's position plus the
 * split centre, tumbling about y like the object, for life..life+15 frames.
 * name: invented */
#define SHARD_LAUNCH( particle, panel, object, life )                                 \
    ( particle )->position->vx = ( object )->position.vx + g_splitCentre.vx;          \
    ( particle )->position->vy = ( object )->position.vy + g_splitCentre.vy;          \
    ( particle )->position->vz = ( object )->position.vz + g_splitCentre.vz;          \
    ( particle )->velocity->vx = ( Sys_Random() & SHARD_SPREAD_MASK ) - SHARD_SPREAD; \
    ( particle )->velocity->vy = ( life ) + ( Sys_Random() & SHARD_SPREAD_MASK );     \
    ( particle )->velocity->vz = ( Sys_Random() & SHARD_SPREAD_MASK ) - SHARD_SPREAD; \
    ( particle )->life = ( life ) + ( Sys_Random() & SHARD_LIFE_MASK );               \
    ( particle )->size = ( Sys_Random() & PANEL_SIZE_MASK ) + PANEL_SIZE;             \
    ( particle )->kind = PARTICLE_KIND_SHARD;                                         \
    ( particle )->model = ( panel );                                                  \
    ( particle )->rotation.vx = 0;                                                    \
    ( particle )->rotation.vy = ( object )->rotation.vy;                              \
    ( particle )->rotation.vz = Sys_Random() & ANGLE_MASK;                            \
    Particle_Link( particle )

/*
==================
Debris_SplitShard

Splits one model row of a shattered object into four panels around a jittered
centre, textured from the row's own texture, each carried by a debris particle.
Returns 0 when the particle or panel pool runs out, -1 otherwise (rows of selector
1 are skipped).
==================
*/
int Debris_SplitShard( levelObject_t *object, dispatchRow_t *panel )
{
    short          life;
    particle_t    *p0, *p1, *p2, *p3;
    dispatchRow_t *f0, *f1, *f2, *f3;
    spriteUV_t    *texture;
    spriteUV_t    *t0, *t1, *t2, *t3;
    u_int          u, v;

    if ( panel->selector != 0 && panel->selector != 2 )
        return -1;
    life = SHARD_LIFE;
    if ( object->type == LEVEL_OBJECT_EXPLOSIVE )
        life = SHARD_LIFE_EXPLOSIVE;
    p0 = Particle_Alloc();
    if ( p0 == 0 )
        return 0;
    p1 = Particle_Alloc();
    if ( p1 == 0 ) {
        Particle_Free( p0 );
        return 0;
    }
    p2 = Particle_Alloc();
    if ( p2 == 0 ) {
        Particle_Free( p0 );
        Particle_Free( p1 );
        return 0;
    }
    p3 = Particle_Alloc();
    if ( p3 == 0 ) {
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        return 0;
    }
    f0 = Debris_ClaimPanel();
    if ( f0 == 0 ) {
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        Particle_Free( p3 );
        return 0;
    }
    f1 = Debris_ClaimPanel();
    if ( f1 == 0 ) {
        f0->count = PANEL_FREE;
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        Particle_Free( p3 );
        return 0;
    }
    f2 = Debris_ClaimPanel();
    if ( f2 == 0 ) {
        f0->count = PANEL_FREE;
        f1->count = PANEL_FREE;
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        Particle_Free( p3 );
        return 0;
    }
    f3 = Debris_ClaimPanel();
    if ( f3 == 0 ) {
        f0->count = PANEL_FREE;
        f1->count = PANEL_FREE;
        f2->count = PANEL_FREE;
        Particle_Free( p0 );
        Particle_Free( p1 );
        Particle_Free( p2 );
        Particle_Free( p3 );
        return 0;
    }
    LoadAverageShort12( panel->vertices[QUAD_VERTEX_TOP_LEFT], panel->vertices[QUAD_VERTEX_TOP_RIGHT], ONE / 2, ONE / 2, &g_splitTop );
    LoadAverageShort12( panel->vertices[QUAD_VERTEX_TOP_LEFT], panel->vertices[QUAD_VERTEX_BOTTOM_LEFT], ONE / 2,
                        ONE / 2, &g_splitLeft );
    LoadAverageShort12( panel->vertices[QUAD_VERTEX_TOP_RIGHT], panel->vertices[QUAD_VERTEX_BOTTOM_RIGHT], ONE / 2,
                        ONE / 2, &g_splitRight );
    LoadAverageShort12( panel->vertices[QUAD_VERTEX_BOTTOM_LEFT], panel->vertices[QUAD_VERTEX_BOTTOM_RIGHT], ONE / 2,
                        ONE / 2, &g_splitBottom );
    LoadAverageShort12( &g_splitLeft, &g_splitRight, ONE / 2, ONE / 2, &g_splitCentre );
    g_splitCentre.vx += ( Sys_Random() & SHARD_CENTRE_JITTER_MASK ) - SHARD_CENTRE_JITTER;
    g_splitCentre.vy += ( Sys_Random() & SHARD_CENTRE_JITTER_MASK ) - SHARD_CENTRE_JITTER;
    g_splitCentre.vz += ( Sys_Random() & SHARD_CENTRE_JITTER_MASK ) - SHARD_CENTRE_JITTER;
    g_splitTop.vx -= g_splitCentre.vx;
    g_splitTop.vy -= g_splitCentre.vy;
    g_splitTop.vz -= g_splitCentre.vz;
    g_splitLeft.vx -= g_splitCentre.vx;
    g_splitLeft.vy -= g_splitCentre.vy;
    g_splitLeft.vz -= g_splitCentre.vz;
    g_splitCentre.vx -= g_splitCentre.vx;
    g_splitCentre.vy -= g_splitCentre.vy;
    g_splitCentre.vz -= g_splitCentre.vz;
    g_splitRight.vx -= g_splitCentre.vx;
    g_splitRight.vy -= g_splitCentre.vy;
    g_splitRight.vz -= g_splitCentre.vz;
    g_splitBottom.vx -= g_splitCentre.vx;
    g_splitBottom.vy -= g_splitCentre.vy;
    g_splitBottom.vz -= g_splitCentre.vz;

    f0->vertices[QUAD_VERTEX_TOP_LEFT]->vx = panel->vertices[QUAD_VERTEX_TOP_LEFT]->vx - g_splitCentre.vx;
    f0->vertices[QUAD_VERTEX_TOP_LEFT]->vy = panel->vertices[QUAD_VERTEX_TOP_LEFT]->vy - g_splitCentre.vy;
    f0->vertices[QUAD_VERTEX_TOP_LEFT]->vz = panel->vertices[QUAD_VERTEX_TOP_LEFT]->vz - g_splitCentre.vz;
    copyVector( f0->vertices[QUAD_VERTEX_TOP_RIGHT], &g_splitTop );
    SET_VECTOR_TO_ZERO( *f0->vertices[QUAD_VERTEX_BOTTOM_RIGHT] );
    copyVector( f0->vertices[QUAD_VERTEX_BOTTOM_LEFT], &g_splitLeft );
    copyVector( f1->vertices[QUAD_VERTEX_TOP_LEFT], &g_splitTop );
    f1->vertices[QUAD_VERTEX_TOP_RIGHT]->vx = panel->vertices[QUAD_VERTEX_TOP_RIGHT]->vx - g_splitCentre.vx;
    f1->vertices[QUAD_VERTEX_TOP_RIGHT]->vy = panel->vertices[QUAD_VERTEX_TOP_RIGHT]->vy - g_splitCentre.vy;
    f1->vertices[QUAD_VERTEX_TOP_RIGHT]->vz = panel->vertices[QUAD_VERTEX_TOP_RIGHT]->vz - g_splitCentre.vz;
    copyVector( f1->vertices[QUAD_VERTEX_BOTTOM_RIGHT], &g_splitRight );
    SET_VECTOR_TO_ZERO( *f1->vertices[QUAD_VERTEX_BOTTOM_LEFT] );
    copyVector( f2->vertices[QUAD_VERTEX_TOP_LEFT], &g_splitLeft );
    SET_VECTOR_TO_ZERO( *f2->vertices[QUAD_VERTEX_TOP_RIGHT] );
    copyVector( f2->vertices[QUAD_VERTEX_BOTTOM_RIGHT], &g_splitBottom );
    f2->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vx = panel->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vx - g_splitCentre.vx;
    f2->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vy = panel->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vy - g_splitCentre.vy;
    f2->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vz = panel->vertices[QUAD_VERTEX_BOTTOM_LEFT]->vz - g_splitCentre.vz;
    SET_VECTOR_TO_ZERO( *f3->vertices[QUAD_VERTEX_TOP_LEFT] );
    copyVector( f3->vertices[QUAD_VERTEX_TOP_RIGHT], &g_splitRight );
    f3->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vx = panel->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vx - g_splitCentre.vx;
    f3->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vy = panel->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vy - g_splitCentre.vy;
    f3->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vz = panel->vertices[QUAD_VERTEX_BOTTOM_RIGHT]->vz - g_splitCentre.vz;
    copyVector( f3->vertices[QUAD_VERTEX_BOTTOM_LEFT], &g_splitBottom );

    if ( g_level >= LEVEL_311_TUNNELS )
        texture = &g_panelUvs[panel->count];
    else
        texture = &g_pickupUvs[panel->count];
    t0 = &g_debrisUvs[f0->count];
    t1 = &g_debrisUvs[f1->count];
    t2 = &g_debrisUvs[f2->count];
    t3 = &g_debrisUvs[f3->count];
    *(u_long *)&t0->u0 = *(u_long *)&t1->u0 = *(u_long *)&t2->u0 = *(u_long *)&t3->u0 = *(u_long *)&texture->u0;
    *(u_long *)&t0->u1 = *(u_long *)&t1->u1 = *(u_long *)&t2->u1 = *(u_long *)&t3->u1 = *(u_long *)&texture->u1;
    *(u_long *)&t0->u2 = *(u_long *)&t1->u2 = *(u_long *)&t2->u2 = *(u_long *)&t3->u2 = *(u_long *)&texture->u2;
    *(u_long *)&t0->u3 = *(u_long *)&t1->u3 = *(u_long *)&t2->u3 = *(u_long *)&t3->u3 = *(u_long *)&texture->u3;
    u = texture->u0 + texture->u1 + texture->u2 + texture->u3;
    u >>= 2;
    v = texture->v0 + texture->v1 + texture->v2 + texture->v3;
    v >>= 2;
    t0->u1 = u;
    t0->u3 = u;
    t0->v3 = v;
    t0->v2 = v;
    t1->u0 = u;
    t1->u2 = u;
    t1->v2 = v;
    t1->v3 = v;
    t2->u1 = u;
    t2->v1 = v;
    t2->u3 = u;
    t2->v0 = v;
    t3->u0 = u;
    t3->v0 = v;
    t3->v1 = v;
    t3->u2 = u;

    SHARD_LAUNCH( p0, f0, object, life );
    SHARD_LAUNCH( p1, f1, object, life );
    SHARD_LAUNCH( p2, f2, object, life );
    SHARD_LAUNCH( p3, f3, object, life );
    return -1;
}

/*
==================
Debris_WalkPanelList

Walks an owner's panel list two panels at a time, handing every other panel to
Debris_SplitShard, until it fails or a panel flagged ROW_FLAG_LAST is reached.
The panel list is the object's model rows viewed as panels.
==================
*/
/* name: inferred from its body, which walks an owner's panel list */
void Debris_WalkPanelList( levelObject_t *owner )
{
    dispatchRow_t *next;
    dispatchRow_t *panel = owner->model;
    for ( ;; ) {
        next = panel;
        panel++;
        if ( Debris_SplitShard( owner, next ) == 0 )
            break;
        if ( panel->flags & ROW_FLAG_LAST )
            break;
        panel++;
        if ( panel->flags & ROW_FLAG_LAST )
            break;
    }
}
