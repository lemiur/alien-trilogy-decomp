/*
 * Map height queries at world position x, z (the map cell under it plus its slope).
 * floor_height (src/game_20000_3.c) is the floor, ceiling_height
 * (same unit) the ceiling. Requires <sys/types.h>.
 * names: inferred from the actor movement code, which compares them with an actor's
 * height to stop climbs, drops and low ceilings (actor_path_clear).
 */
#ifndef HEIGHT_H
#define HEIGHT_H

extern u_int floor_height(u_int x, u_int z);
extern int ceiling_height(u_int x, u_int z);

/* src/game_20000_3.c: push a position off its cell's diagonal walls */
extern u_int diagonal_clip_x(u_int x, u_int z);
extern u_int diagonal_clip_z(u_int x, u_int z);

#endif
