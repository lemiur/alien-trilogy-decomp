/*
 * Animated effect tasks (src/effects.c): each plays one effect frame sequence;
 * particle.c starts them with TaskCreate.
 */
#ifndef EFFECTS_H
#define EFFECTS_H

struct TASK;

extern void Fx_AcidSplashTask( struct TASK *task );
extern void Fx_BloodSplashTask( struct TASK *task );
extern void Fx_SynthSplashTask( struct TASK *task );
extern void Fx_ExplosionTask( struct TASK *task );
extern void Fx_BigExplosionTask( struct TASK *task );

/* VRAM position of the explosion graphics page (g_explosionUvs; EXPLGFX). names: invented */
#define EXPLOSION_TPAGE_X 0x340
#define EXPLOSION_TPAGE_Y 0x100

/* Fx_Draw draws a frame whose centre is within these screen columns. names: invented */
#define EFFECT_MIN_SCREEN_X ( -0x3f )
#define EFFECT_MAX_SCREEN_X 0x17f

/* Effect tasks keep their frame counter in this TASK.state[] slot. name: invented */
#define EFFECT_STATE_FRAME 3

/* The splash tasks count frames in state[EFFECT_STATE_FRAME] in 16.16 fixed point and show each frame for
 * two ticks (state[EFFECT_STATE_FRAME] >> EFFECT_FRAME_SHIFT). names: invented */
#define EFFECT_FRAME_STEP 0x10000
#define EFFECT_FRAME_SHIFT 17

/* Frames of an effect frame sequence (effectFrameSequence_t). name: invented */
#define EFFECT_SEQUENCE_LENGTH 16

/* The frames of one effect and its tint (src/effects.c g_explosionFrames ...): per frame a sprite frame, a
 * translucency rate and an offset. names: invented */
typedef struct effectFrameSequence_s effectFrameSequence_t;
struct effectFrameSequence_s {
    int           scaleX;
    int           scaleY;
    int           frames[EFFECT_SEQUENCE_LENGTH];
    short         translucency[EFFECT_SEQUENCE_LENGTH];
    short         offsetX[EFFECT_SEQUENCE_LENGTH];
    short         offsetY[EFFECT_SEQUENCE_LENGTH];
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char reserved;
};

#endif
