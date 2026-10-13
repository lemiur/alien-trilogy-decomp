/*
 * Animation track entry: a sprite frame list and its command stream. Requires <sys/types.h>.
 * Indexed by the weapon view (weapon_view_play) and by actors (actor_play_animation), 8 bytes each.
 */
#ifndef TRACK_H
#define TRACK_H

typedef struct TrackEntry {
    struct SpriteFrame *frames; /* frame list the script indexes; becomes Sprite.frames (actor_play_animation) */
    u_short *stream; /* invented name: first word is the duration, commands follow */
} TrackEntry;

/* Script header halfwords (asm/sprite_anims.s): [0] ticks per step, [1] frames in the frame list,
 * which sprite_chunk_add passes as the count of frames to relocate. name: invented */
#define TRACK_SCRIPT_FRAMES 1

#endif
