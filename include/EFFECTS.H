/*
 * Animated effect tasks (src/effects.c): each plays one effect frame sequence;
 * particle.c starts them with TaskCreate.
 */
#ifndef EFFECTS_H
#define EFFECTS_H

struct TASK;

extern void acid_splash_task(struct TASK *task);
extern void blood_splash_task(struct TASK *task);
extern void synth_splash_task(struct TASK *task);
extern void explosion_task(struct TASK *task);
extern void big_explosion_task(struct TASK *task);

/* VRAM position of the explosion graphics page (g_explosion_uvs; EXPLGFX). names: invented */
#define EXPLOSION_TPAGE_X 0x340
#define EXPLOSION_TPAGE_Y 0x100

/* effect_draw draws a frame whose centre is within these screen columns. names: invented */
#define EFFECT_MIN_SCREEN_X (-0x3f)
#define EFFECT_MAX_SCREEN_X 0x17f

/* The splash tasks count frames in state[3] in 16.16 fixed point and show each frame for
 * two ticks (state[3] >> EFFECT_FRAME_SHIFT). names: invented */
#define EFFECT_FRAME_STEP 0x10000
#define EFFECT_FRAME_SHIFT 17

/* Frames of an effect frame sequence (EffectFrameSequence). name: invented */
#define EFFECT_SEQUENCE_LENGTH 16

#endif
