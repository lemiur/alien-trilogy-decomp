/*
 * Debris panels (src/debris.c): the panel pool breakable walls burst into.
 * Requires <sys/types.h>.
 */
#ifndef DEBRIS_H
#define DEBRIS_H

struct Particle;
struct LevelObject;

extern void panel_pool_alloc(void);
extern void panel_pool_init(void);
extern void panel_list_walk(struct LevelObject *owner);
/* frees the debris panel a PARTICLE_KIND_SHARD or PARTICLE_KIND_PANEL particle carries in its model slot */
extern void panel_free(struct Particle *particle);
/* bursts a destroyed wall object into debris panels (block_object_task) */
extern void wall_shatter(struct LevelObject *source);

#endif
