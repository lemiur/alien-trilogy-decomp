/* Debris panels: a pool of 128 four-vertex panels that breakable walls burst into. */
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
#define PANEL_FREE 0xffff /* Panel.link of a panel not in use */
#define PANEL_PALETTE 0x0a /* palette entry (flags & ROW_PALETTE_MASK) of a fresh panel. name: invented */
#define PANEL_VERTICES 4 /* a panel is a quad. name: invented */
#define PANEL_GRID_POINTS 9 /* the 3x3 vertex offsets of g_panel_grid_xy and g_panel_grid_yz. name: invented */

/* Debris tuning: a random value is base + (random() & mask), a jitter (random() & mask) - jitter.
 * names: invented */
#define DEBRIS_SPEED_MIN 0x10
#define DEBRIS_SPEED_MASK 0xf
#define PANEL_LIFT_JITTER 8 /* panel_launch */
#define PANEL_LIFT_JITTER_MASK 0xf
#define PANEL_LIFE 0x10
#define PANEL_LIFE_MASK 0xf
#define PANEL_SIZE 0x20 /* Particle.size of a panel or shard */
#define PANEL_SIZE_MASK 0xf
#define PANEL_CENTRE_JITTER 0x100 /* panel_spawn */
#define PANEL_CENTRE_JITTER_MASK 0x1ff
#define SHARD_CENTRE_JITTER 0x80 /* shard_split */
#define SHARD_CENTRE_JITTER_MASK 0xff
#define SHARD_LIFE 0x20
#define SHARD_LIFE_EXPLOSIVE 40
#define SHARD_LIFE_MASK 0xf
#define SHARD_SPREAD 0x10
#define SHARD_SPREAD_MASK 0x1f

/* name: inferred from its four vertex pointers into the panel vertex pool */
typedef struct Panel {
    SVECTOR *vertices[PANEL_VERTICES];
    u_short link;
    u_char flags;
    u_char vertex_count;
} Panel;


/* A random debris speed of 16..31. */
#define DEBRIS_SPEED() ((random() & DEBRIS_SPEED_MASK) + DEBRIS_SPEED_MIN)

/* 3x3 vertex offsets of a panel facing z, and of one facing x.
 * names: inferred from the wall orientations wall_shatter uses them for */
SVECTOR g_panel_grid_xy[PANEL_GRID_POINTS] = {
    { -512, -512, 0 }, { 0, -512, 0 }, { 512, -512, 0 },
    { -512, 0, 0 }, { 0, 0, 0 }, { 512, 0, 0 },
    { -512, 512, 0 }, { 0, 512, 0 }, { 512, 512, 0 },
};

SVECTOR g_panel_grid_yz[PANEL_GRID_POINTS] = {
    { 0, -512, -512 }, { 0, -512, 0 }, { 0, -512, 512 },
    { 0, 0, -512 }, { 0, 0, 0 }, { 0, 0, 512 },
    { 0, 512, -512 }, { 0, 512, 0 }, { 0, 512, 512 },
};

/* Module statics (.sbss 0x8008d2c8..0x8008d300), in their retail order.
 * names: inferred from panel_pool_init, which points each panel at four pool vertices */
static SVECTOR *g_panel_vertices;
static Panel *g_panels;
/* The split points of shard_split: the midpoints of the panel's top and left edges, its
 * jittered centre, and the midpoints of its right and bottom edges. names: invented */
static SVECTOR g_split_top;
static SVECTOR g_split_left;
static SVECTOR g_split_centre;
static SVECTOR g_split_right;
static SVECTOR g_split_bottom;

/* name: inferred from its body, which allocates the panel, vertex and work tables */
void panel_pool_alloc(void)
{
    g_panel_vertices = heap_alloc(PANEL_COUNT * PANEL_VERTICES * sizeof(SVECTOR));
    g_panels = heap_alloc(PANEL_COUNT * sizeof(Panel));
    g_debris_uvs = heap_alloc(PANEL_COUNT * sizeof(SpriteUV));
}

/* name: inferred from its body, which gives every panel its four vertices */
void panel_pool_init(void)
{
    Panel *panel = g_panels;
    SVECTOR *vertex = g_panel_vertices;
    short i;
    for (i = 0; i < PANEL_COUNT; i++) {
        panel->vertices[0] = vertex++;
        panel->vertices[1] = vertex++;
        panel->vertices[2] = vertex++;
        panel->vertices[3] = vertex++;
        panel->link = PANEL_FREE;
        panel->vertex_count = PANEL_VERTICES;
        panel->flags = ROW_FLAG_LAST | PANEL_PALETTE;
        panel++;
    }
}

/* First free panel, linked to its own index; 0 when the pool is exhausted. */
/* name: inferred from its body, which claims the first free panel */
Panel *panel_claim(void)
{
    Panel *panel = g_panels;
    short i = 0;
    while (i < PANEL_COUNT) {
        if (panel->link == PANEL_FREE) {
            panel->link = i;
            return panel;
        }
        i++;
        panel++;
    }
    return 0;
}

/* name: inferred from its body, which frees the panel a debris particle carries */
void panel_free(Particle *particle)
{
    ((Panel *)particle->model)->link = PANEL_FREE;
    ((Panel *)particle->model)->vertex_count = PANEL_VERTICES;
}

/* Starts a debris particle carrying panel: at position, moving at velocity with a
 * little random lift, for 16..31 frames. name: invented */
static __inline__ void panel_launch(Particle *particle, Panel *panel, SVECTOR *position,
                                    SVECTOR *velocity)
{
    copyVector(particle->position, position);
    particle->velocity->vx = velocity->vx;
    particle->velocity->vy = velocity->vy + (random() & PANEL_LIFT_JITTER_MASK) - PANEL_LIFT_JITTER;
    particle->velocity->vz = velocity->vz;
    particle->life = (random() & PANEL_LIFE_MASK) + PANEL_LIFE;
    particle->size = (random() & PANEL_SIZE_MASK) + PANEL_SIZE;
    particle->kind = PARTICLE_KIND_PANEL;
    particle->model = (DispatchRow *)panel;
    SET_VECTOR_TO_ZERO(particle->rotation);
    particle_link(particle);
}

/* Splits the 3x3 grid into four panels around a random centre point, textured with
 * the level's wall texture split at its centre, each carried by a debris particle.
 * Returns 0 (taking nothing) when the particle or panel pool runs out. */
int panel_spawn(SVECTOR *grid, SVECTOR *position, SVECTOR *velocity)
{
    Particle *p0, *p1, *p2, *p3;
    Panel *f0, *f1, *f2, *f3;
    SpriteUV *texture;
    SpriteUV *t0, *t1, *t2, *t3;
    u_int u, v;
    short cx, cy, cz;

    p0 = particle_alloc();
    if (p0 == 0)
        return 0;
    p1 = particle_alloc();
    if (p1 == 0) {
        particle_free(p0);
        return 0;
    }
    p2 = particle_alloc();
    if (p2 == 0) {
        particle_free(p0);
        particle_free(p1);
        return 0;
    }
    p3 = particle_alloc();
    if (p3 == 0) {
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        return 0;
    }
    f0 = panel_claim();
    if (f0 == 0) {
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        particle_free(p3);
        return 0;
    }
    f1 = panel_claim();
    if (f1 == 0) {
        f0->link = PANEL_FREE;
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        particle_free(p3);
        return 0;
    }
    f2 = panel_claim();
    if (f2 == 0) {
        f0->link = PANEL_FREE;
        f1->link = PANEL_FREE;
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        particle_free(p3);
        return 0;
    }
    f3 = panel_claim();
    if (f3 == 0) {
        f0->link = PANEL_FREE;
        f1->link = PANEL_FREE;
        f2->link = PANEL_FREE;
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        particle_free(p3);
        return 0;
    }
    copyVector(f0->vertices[0], &grid[0]);
    copyVector(f0->vertices[1], &grid[1]);
    copyVector(f0->vertices[3], &grid[3]);
    copyVector(f1->vertices[0], &grid[1]);
    copyVector(f1->vertices[1], &grid[2]);
    copyVector(f1->vertices[2], &grid[5]);
    copyVector(f2->vertices[0], &grid[3]);
    copyVector(f2->vertices[2], &grid[7]);
    copyVector(f2->vertices[3], &grid[6]);
    copyVector(f3->vertices[1], &grid[5]);
    copyVector(f3->vertices[2], &grid[8]);
    copyVector(f3->vertices[3], &grid[7]);

    cx = (random() & PANEL_CENTRE_JITTER_MASK) - PANEL_CENTRE_JITTER;
    cy = (random() & PANEL_CENTRE_JITTER_MASK) - PANEL_CENTRE_JITTER;
    cz = (random() & PANEL_CENTRE_JITTER_MASK) - PANEL_CENTRE_JITTER;
    f0->vertices[2]->vx = cx;
    f0->vertices[2]->vy = cy;
    f0->vertices[2]->vz = cz;
    f1->vertices[3]->vx = cx;
    f1->vertices[3]->vy = cy;
    f1->vertices[3]->vz = cz;
    f2->vertices[1]->vx = cx;
    f2->vertices[1]->vy = cy;
    f2->vertices[1]->vz = cz;
    f3->vertices[0]->vx = cx;
    f3->vertices[0]->vy = cy;
    f3->vertices[0]->vz = cz;

    if (g_level == LEVEL_131_MED_LAB) {
        texture = &g_sprite_uvs[SPRITE_UV_WALL_PANEL_MED_LAB];
    } else {
        texture = &g_sprite_uvs[SPRITE_UV_WALL_PANEL];
        if (g_level == LEVEL_211_LIVING_QUARTERS)
            texture = &g_sprite_uvs[SPRITE_UV_WALL_PANEL_LIVING_QUARTERS];
    }
    t0 = &g_debris_uvs[f0->link];
    t1 = &g_debris_uvs[f1->link];
    t2 = &g_debris_uvs[f2->link];
    t3 = &g_debris_uvs[f3->link];
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

    panel_launch(p0, f0, position, velocity);
    panel_launch(p1, f1, position, velocity);
    panel_launch(p2, f2, position, velocity);
    panel_launch(p3, f3, position, velocity);
    return -1;
}

/* Bursts a breakable wall (a LevelObject of type 0x1c, from block_object_task) into debris
 * panels along its width or depth. name: inferred from that */
void wall_shatter(LevelObject *source)
{
    SVECTOR position;
    SVECTOR velocity;
    short i;

    switch (source->facing) {
    case FACING_NEG_Z:
    case FACING_POS_Z:
        position.vx = source->position.vx;
        position.vz = source->position.vz + (source->depth - 1) * MAP_CELL_HALF;
        if (position.vz < g_player.position.vz) {
            for (i = 0; i < source->width; i++) {
                velocity.vx = 0;
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = -(DEBRIS_SPEED());
                position.vy = source->position.vy + MAP_CELL_HALF;
                if (panel_spawn(g_panel_grid_xy, &position, &velocity) == 0)
                    return;
                position.vy = source->position.vy + (MAP_CELL_HALF + MAP_CELL_SIZE);
                velocity.vx = 0;
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = -(DEBRIS_SPEED());
                if (panel_spawn(g_panel_grid_xy, &position, &velocity) == 0)
                    return;
                if (g_level == LEVEL_212_CANTEEN) {
                    position.vy = source->position.vy + (MAP_CELL_HALF + 2 * MAP_CELL_SIZE);
                    velocity.vx = 0;
                    velocity.vy = DEBRIS_SPEED();
                    velocity.vz = -(DEBRIS_SPEED());
                    if (panel_spawn(g_panel_grid_xy, &position, &velocity) == 0)
                        return;
                }
                position.vx += MAP_CELL_SIZE;
            }
        } else {
            for (i = 0; i < source->width; i++) {
                velocity.vx = 0;
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = DEBRIS_SPEED();
                position.vy = source->position.vy + MAP_CELL_HALF;
                if (panel_spawn(g_panel_grid_xy, &position, &velocity) == 0)
                    return;
                velocity.vx = 0;
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = DEBRIS_SPEED();
                position.vy = source->position.vy + (MAP_CELL_HALF + MAP_CELL_SIZE);
                if (panel_spawn(g_panel_grid_xy, &position, &velocity) == 0)
                    return;
                if (g_level == LEVEL_212_CANTEEN) {
                    position.vy = source->position.vy + (MAP_CELL_HALF + 2 * MAP_CELL_SIZE);
                    velocity.vx = 0;
                    velocity.vy = DEBRIS_SPEED();
                    velocity.vz = DEBRIS_SPEED();
                    if (panel_spawn(g_panel_grid_xy, &position, &velocity) == 0)
                        return;
                }
                position.vx += MAP_CELL_SIZE;
            }
        }
        break;
    case FACING_POS_X:
    case FACING_NEG_X:
        position.vx = source->position.vx + (source->width - 1) * MAP_CELL_HALF;
        position.vz = source->position.vz;
        if (position.vx < g_player.position.vx) {
            for (i = 0; i < source->depth; i++) {
                velocity.vx = -(DEBRIS_SPEED());
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = 0;
                position.vy = source->position.vy + MAP_CELL_HALF;
                if (panel_spawn(g_panel_grid_yz, &position, &velocity) == 0)
                    return;
                velocity.vx = -(DEBRIS_SPEED());
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = 0;
                position.vy = source->position.vy + (MAP_CELL_HALF + MAP_CELL_SIZE);
                if (panel_spawn(g_panel_grid_yz, &position, &velocity) == 0)
                    return;
                if (g_level == LEVEL_212_CANTEEN) {
                    position.vy = source->position.vy + (MAP_CELL_HALF + 2 * MAP_CELL_SIZE);
                    velocity.vx = -(DEBRIS_SPEED());
                    velocity.vy = DEBRIS_SPEED();
                    velocity.vz = 0;
                    if (panel_spawn(g_panel_grid_xy, &position, &velocity) == 0)
                        return;
                }
                position.vz += MAP_CELL_SIZE;
            }
        } else {
            for (i = 0; i < source->depth; i++) {
                velocity.vx = DEBRIS_SPEED();
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = 0;
                position.vy = source->position.vy + MAP_CELL_HALF;
                if (panel_spawn(g_panel_grid_yz, &position, &velocity) == 0)
                    return;
                velocity.vx = DEBRIS_SPEED();
                velocity.vy = DEBRIS_SPEED();
                velocity.vz = 0;
                position.vy = source->position.vy + (MAP_CELL_HALF + MAP_CELL_SIZE);
                if (panel_spawn(g_panel_grid_yz, &position, &velocity) == 0)
                    return;
                if (g_level == LEVEL_212_CANTEEN) {
                    position.vy = source->position.vy + (MAP_CELL_HALF + 2 * MAP_CELL_SIZE);
                    velocity.vx = DEBRIS_SPEED();
                    velocity.vy = DEBRIS_SPEED();
                    velocity.vz = 0;
                    if (panel_spawn(g_panel_grid_xy, &position, &velocity) == 0)
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
#define SHARD_LAUNCH(particle, panel, object, life) \
    (particle)->position->vx = (object)->position.vx + g_split_centre.vx; \
    (particle)->position->vy = (object)->position.vy + g_split_centre.vy; \
    (particle)->position->vz = (object)->position.vz + g_split_centre.vz; \
    (particle)->velocity->vx = (random() & SHARD_SPREAD_MASK) - SHARD_SPREAD; \
    (particle)->velocity->vy = (life) + (random() & SHARD_SPREAD_MASK); \
    (particle)->velocity->vz = (random() & SHARD_SPREAD_MASK) - SHARD_SPREAD; \
    (particle)->life = (life) + (random() & SHARD_LIFE_MASK); \
    (particle)->size = (random() & PANEL_SIZE_MASK) + PANEL_SIZE; \
    (particle)->kind = PARTICLE_KIND_SHARD; \
    (particle)->model = (DispatchRow *)(panel); \
    (particle)->rotation.vx = 0; \
    (particle)->rotation.vy = (object)->rotation.vy; \
    (particle)->rotation.vz = random() & ANGLE_MASK; \
    particle_link(particle)

/* Splits one model row of a shattered object into four panels around a jittered
 * centre, textured from the row's own texture, each carried by a debris particle.
 * Returns 0 when the particle or panel pool runs out, -1 otherwise (rows of selector
 * 1 are skipped). */
int shard_split(LevelObject *object, Panel *panel)
{
    short life;
    Particle *p0, *p1, *p2, *p3;
    Panel *f0, *f1, *f2, *f3;
    SpriteUV *texture;
    SpriteUV *t0, *t1, *t2, *t3;
    u_int u, v;

    if (panel->vertex_count != 0 && panel->vertex_count != 2)
        return -1;
    life = SHARD_LIFE;
    if (object->type == LEVEL_OBJECT_EXPLOSIVE)
        life = SHARD_LIFE_EXPLOSIVE;
    p0 = particle_alloc();
    if (p0 == 0)
        return 0;
    p1 = particle_alloc();
    if (p1 == 0) {
        particle_free(p0);
        return 0;
    }
    p2 = particle_alloc();
    if (p2 == 0) {
        particle_free(p0);
        particle_free(p1);
        return 0;
    }
    p3 = particle_alloc();
    if (p3 == 0) {
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        return 0;
    }
    f0 = panel_claim();
    if (f0 == 0) {
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        particle_free(p3);
        return 0;
    }
    f1 = panel_claim();
    if (f1 == 0) {
        f0->link = PANEL_FREE;
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        particle_free(p3);
        return 0;
    }
    f2 = panel_claim();
    if (f2 == 0) {
        f0->link = PANEL_FREE;
        f1->link = PANEL_FREE;
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        particle_free(p3);
        return 0;
    }
    f3 = panel_claim();
    if (f3 == 0) {
        f0->link = PANEL_FREE;
        f1->link = PANEL_FREE;
        f2->link = PANEL_FREE;
        particle_free(p0);
        particle_free(p1);
        particle_free(p2);
        particle_free(p3);
        return 0;
    }
    LoadAverageShort12(panel->vertices[0], panel->vertices[1], ONE / 2, ONE / 2, &g_split_top);
    LoadAverageShort12(panel->vertices[0], panel->vertices[3], ONE / 2, ONE / 2, &g_split_left);
    LoadAverageShort12(panel->vertices[1], panel->vertices[2], ONE / 2, ONE / 2, &g_split_right);
    LoadAverageShort12(panel->vertices[3], panel->vertices[2], ONE / 2, ONE / 2, &g_split_bottom);
    LoadAverageShort12(&g_split_left, &g_split_right, ONE / 2, ONE / 2, &g_split_centre);
    g_split_centre.vx += (random() & SHARD_CENTRE_JITTER_MASK) - SHARD_CENTRE_JITTER;
    g_split_centre.vy += (random() & SHARD_CENTRE_JITTER_MASK) - SHARD_CENTRE_JITTER;
    g_split_centre.vz += (random() & SHARD_CENTRE_JITTER_MASK) - SHARD_CENTRE_JITTER;
    g_split_top.vx -= g_split_centre.vx;
    g_split_top.vy -= g_split_centre.vy;
    g_split_top.vz -= g_split_centre.vz;
    g_split_left.vx -= g_split_centre.vx;
    g_split_left.vy -= g_split_centre.vy;
    g_split_left.vz -= g_split_centre.vz;
    g_split_centre.vx -= g_split_centre.vx;
    g_split_centre.vy -= g_split_centre.vy;
    g_split_centre.vz -= g_split_centre.vz;
    g_split_right.vx -= g_split_centre.vx;
    g_split_right.vy -= g_split_centre.vy;
    g_split_right.vz -= g_split_centre.vz;
    g_split_bottom.vx -= g_split_centre.vx;
    g_split_bottom.vy -= g_split_centre.vy;
    g_split_bottom.vz -= g_split_centre.vz;

    f0->vertices[0]->vx = panel->vertices[0]->vx - g_split_centre.vx;
    f0->vertices[0]->vy = panel->vertices[0]->vy - g_split_centre.vy;
    f0->vertices[0]->vz = panel->vertices[0]->vz - g_split_centre.vz;
    copyVector(f0->vertices[1], &g_split_top);
    SET_VECTOR_TO_ZERO(*f0->vertices[2]);
    copyVector(f0->vertices[3], &g_split_left);
    copyVector(f1->vertices[0], &g_split_top);
    f1->vertices[1]->vx = panel->vertices[1]->vx - g_split_centre.vx;
    f1->vertices[1]->vy = panel->vertices[1]->vy - g_split_centre.vy;
    f1->vertices[1]->vz = panel->vertices[1]->vz - g_split_centre.vz;
    copyVector(f1->vertices[2], &g_split_right);
    SET_VECTOR_TO_ZERO(*f1->vertices[3]);
    copyVector(f2->vertices[0], &g_split_left);
    SET_VECTOR_TO_ZERO(*f2->vertices[1]);
    copyVector(f2->vertices[2], &g_split_bottom);
    f2->vertices[3]->vx = panel->vertices[3]->vx - g_split_centre.vx;
    f2->vertices[3]->vy = panel->vertices[3]->vy - g_split_centre.vy;
    f2->vertices[3]->vz = panel->vertices[3]->vz - g_split_centre.vz;
    SET_VECTOR_TO_ZERO(*f3->vertices[0]);
    copyVector(f3->vertices[1], &g_split_right);
    f3->vertices[2]->vx = panel->vertices[2]->vx - g_split_centre.vx;
    f3->vertices[2]->vy = panel->vertices[2]->vy - g_split_centre.vy;
    f3->vertices[2]->vz = panel->vertices[2]->vz - g_split_centre.vz;
    copyVector(f3->vertices[3], &g_split_bottom);

    if (g_level >= LEVEL_311_TUNNELS)
        texture = &g_panel_uvs[panel->link];
    else
        texture = &g_pickup_uvs[panel->link];
    t0 = &g_debris_uvs[f0->link];
    t1 = &g_debris_uvs[f1->link];
    t2 = &g_debris_uvs[f2->link];
    t3 = &g_debris_uvs[f3->link];
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

    SHARD_LAUNCH(p0, f0, object, life);
    SHARD_LAUNCH(p1, f1, object, life);
    SHARD_LAUNCH(p2, f2, object, life);
    SHARD_LAUNCH(p3, f3, object, life);
    return -1;
}

/* Walks an owner's panel list two panels at a time, handing every other panel to
 * shard_split, until it fails or a panel flagged ROW_FLAG_LAST is reached.
 * The panel list is the object's model rows viewed as panels. */
/* name: inferred from its body, which walks an owner's panel list */
void panel_list_walk(LevelObject *owner)
{
    Panel *next;
    Panel *panel = (Panel *)owner->model;
    for (;;) {
        next = panel;
        panel++;
        if (shard_split(owner, next) == 0)
            break;
        if (panel->flags & ROW_FLAG_LAST)
            break;
        panel++;
        if (panel->flags & ROW_FLAG_LAST)
            break;
    }
}
