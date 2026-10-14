/*
 * Animation track entry: a sprite frame list and its command stream. Requires <sys/types.h>.
 * Indexed by the weapon view (Player_PlayWeaponView) and by actors (Actor_PlayAnimation), 8 bytes each.
 */
#ifndef TRACK_H
#define TRACK_H

typedef struct trackEntry_s {
    struct spriteFrame_s *frames; /* frame list the script indexes; becomes Sprite.frames (Actor_PlayAnimation) */
    u_short              *stream; /* invented name: first word is the duration, commands follow */
} trackEntry_t;

/* Script header halfwords (asm/sprite_anims.s): [0] ticks per step, [1] frames in the frame list,
 * which Sprite_AddChunk passes as the count of frames to relocate. name: invented */
#define TRACK_SCRIPT_FRAMES 1

#endif
