/*
 * Debris panels (src/debris.c): the panel pool breakable walls burst into.
 * Requires <sys/types.h>.
 */
#ifndef DEBRIS_H
#define DEBRIS_H

struct particle_s;
struct levelObject_s;

extern void Debris_PanelPoolAlloc( void );
extern void Debris_InitPanelPool( void );
extern void Debris_WalkPanelList( struct levelObject_s *owner );
/* frees the debris panel a PARTICLE_KIND_SHARD or PARTICLE_KIND_PANEL particle carries in its model slot */
extern void Debris_FreePanel( struct particle_s *particle );
/* bursts a destroyed wall object into debris panels (Object_BlockObjectTask) */
extern void Debris_ShatterWall( struct levelObject_s *source );

#endif
