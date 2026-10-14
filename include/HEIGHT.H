/*
 * Map height queries at world position x, z (the map cell under it plus its slope).
 * FloorHeight (src/game_20000_3.c) is the floor, CeilingHeight
 * (same unit) the ceiling. Requires <sys/types.h>.
 * names: inferred from the actor movement code, which compares them with an actor's
 * height to stop climbs, drops and low ceilings (Actor_PathClear).
 */
#ifndef HEIGHT_H
#define HEIGHT_H

extern u_int FloorHeight( u_int x, u_int z );
extern int CeilingHeight( u_int x, u_int z );

/* src/game_20000_3.c: push a position off its cell's diagonal walls */
extern u_int DiagonalClipX( u_int x, u_int z );
extern u_int DiagonalClipZ( u_int x, u_int z );

#endif
