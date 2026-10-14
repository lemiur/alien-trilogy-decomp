/* effects.c -- animated effect tasks: each plays one effect frame sequence */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <ABS.H>
#include <TASK.H>
#include <MODEL.H>
#include <VIEW.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <DISPLAY.H>
#include <TEXTURE.H>
#include <EFFECTS.H>

extern int Fx_DrawAdditive( int x, int y, int z, int frame, const effectFrameSequence_t *frames );

/* The effect frame sequences (.data 0x800731bc..0x800735c4). */
/* name: analysis/runtime/globals.json */
effectFrameSequence_t g_explosionFrames = { 3072,
                                            3072,
                                            { 0, 1, 2, 3, 4, 5, 6, 7, 8, -1, -1, -1, -1, -1, -1, -1 },
                                            { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
                                            { 19, 26, 29, 27, 24, 19, 18, 16, 13, 0, 0, 0, 0, 0, 0, 0 },
                                            { 22, 26, 28, 32, 34, 34, 36, 34, 36, 0, 0, 0, 0, 0, 0, 0 },
                                            255,
                                            255,
                                            255,
                                            0 };
/* name: analysis/runtime/globals.json */
effectFrameSequence_t g_bigExplosionFrames = { 3900,
                                               4700,
                                               { 9, 10, 11, 12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1 },
                                               { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
                                               { 15, 19, 33, 29, 34, 40, 36, 39, 32, 0, 0, 0, 0, 0, 0, 0 },
                                               { 6, 12, 67, 75, 76, 77, 74, 76, 81, 0, 0, 0, 0, 0, 0, 0 },
                                               255,
                                               255,
                                               255,
                                               0 };
effectFrameSequence_t g_effectFrames73314 = { 2560,
                                              2560,
                                              { 18, 19, 20, 21, 22, 23, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
                                              { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
                                              { 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
                                              { 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
                                              255,
                                              255,
                                              255,
                                              0 };

/* The frame tables the acid, blood and synthetic splashes share; they differ only in the tint. */
#define SHARED_SPLASH_FRAME_DATA                                                                                      \
    2048, 2048, { 24, 25, 26, 27, 28, 29, 30, 31, -1, -1, -1, -1, -1, -1, -1, -1 },                                   \
        { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, { 7, 11, 10, 11, 11, 12, 12, 8, 0, 0, 0, 0, 0, 0, 0, 0 }, \
        { 5, 7, 8, 7, 10, 10, 6, 4, 0, 0, 0, 0, 0, 0, 0, 0 }
/* names: analysis/runtime/globals.json: acid (green), blood (red) and synthetic (white) splashes */
effectFrameSequence_t g_acidSplashFrames = { SHARED_SPLASH_FRAME_DATA, 0, 255, 0, 0 };
effectFrameSequence_t g_bloodSplashFrames = { SHARED_SPLASH_FRAME_DATA, 255, 0, 0, 0 };
effectFrameSequence_t g_synthSplashFrames = { SHARED_SPLASH_FRAME_DATA, 255, 255, 255, 0 };


/*
==================
Fx_Draw

Draws frame `frame` of an effect sequence as a scaled sprite at a
world position and returns -1 once the sequence has ended
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Fx_Draw( int x, int y, int z, short frame, const effectFrameSequence_t *frames )
{
    SVECTOR     position;
    DVECTOR     screen;
    POLY_FT4   *poly;
    spriteUV_t *uv;
    int         depth;
    int         width;
    int         height;
    int         x0, y0, x1, y1;

    if ( frames->frames[frame] == -1 )
        return -1;
    position.vx = x;
    position.vy = y;
    position.vz = z;
    depth = g_projectedDepth = RotTransPers( &position, (long *)&screen, &g_gteInterpolation, &g_gteFlag ) << 2;
    if ( depth > DRAW_NEAR_DEPTH && depth < DRAW_FAR_DEPTH && screen.vx >= EFFECT_MIN_SCREEN_X && screen.vx <= EFFECT_MAX_SCREEN_X ) {
        uv = &g_explosionUvs[frames->frames[frame]];
        poly = g_spriteWorkspace;
        *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
        *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
        *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
        *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
        width = poly->u1 - poly->u0;
        if ( width < 0 )
            width = -width;
        width++;
        height = poly->v2 - poly->v0;
        if ( height < 0 )
            height = -height;
        height++;
        x0 = -frames->offsetX[frame] * frames->scaleX / depth;
        y0 = -frames->offsetY[frame] * frames->scaleY / depth;
        x1 = ( -frames->offsetX[frame] + width ) * frames->scaleX / depth;
        y1 = ( -frames->offsetY[frame] + height ) * frames->scaleY / depth;
        setXY4( poly, screen.vx + x0, screen.vy + y0, screen.vx + x1, poly->y0, poly->x0, screen.vy + y1, poly->x1, poly->y2 );
        *(u_long *)&poly->r0 = *(u_long *)&g_palette->colors[0];
        if ( frames->blue == 0 ) {
            if ( frames->red ) {
                poly->g0 = 0;
                poly->b0 = 0;
            } else if ( frames->green ) {
                poly->r0 = 0;
                poly->b0 = 0;
            }
        }
        setPolyFT4( poly );
        setSemiTrans( poly, frames->translucency[frame] );
        addPrim( g_renderBuffer + ( g_projectedDepth >> OT_DEPTH_SHIFT ), poly );
        poly++;
        g_spriteWorkspace = poly;
    }
    return 0;
}

/*
==================
Fx_AcidSplashTask
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Fx_AcidSplashTask( TASK *task )
{
    unsigned int frame = task->state[EFFECT_STATE_FRAME] >> EFFECT_FRAME_SHIFT;
    if ( Fx_Draw( (short)task->state[0], (short)task->state[1], (short)task->state[2], frame, &g_acidSplashFrames ) ) {
        TaskDie( task );
    } else {
        task->state[EFFECT_STATE_FRAME] = task->state[EFFECT_STATE_FRAME] + EFFECT_FRAME_STEP;
        g_nextTask = task->Next;
    }
}

/*
==================
Fx_BloodSplashTask
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Fx_BloodSplashTask( TASK *task )
{
    unsigned int frame = task->state[EFFECT_STATE_FRAME] >> EFFECT_FRAME_SHIFT;
    if ( Fx_Draw( (short)task->state[0], (short)task->state[1], (short)task->state[2], frame, &g_bloodSplashFrames ) ) {
        TaskDie( task );
    } else {
        task->state[EFFECT_STATE_FRAME] = task->state[EFFECT_STATE_FRAME] + EFFECT_FRAME_STEP;
        g_nextTask = task->Next;
    }
}

/*
==================
Fx_SynthSplashTask
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Fx_SynthSplashTask( TASK *task )
{
    unsigned int frame = task->state[EFFECT_STATE_FRAME] >> EFFECT_FRAME_SHIFT;
    if ( Fx_Draw( (short)task->state[0], (short)task->state[1], (short)task->state[2], frame, &g_synthSplashFrames ) ) {
        TaskDie( task );
    } else {
        task->state[EFFECT_STATE_FRAME] = task->state[EFFECT_STATE_FRAME] + EFFECT_FRAME_STEP;
        g_nextTask = task->Next;
    }
}

/*
==================
Fx_ExplosionTask
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Fx_ExplosionTask( TASK *task )
{
    short frame = (short)( task->state[EFFECT_STATE_FRAME] >> 1 );
    if ( Fx_DrawAdditive( task->state[0], task->state[1], task->state[2], frame, &g_explosionFrames ) ) {
        TaskDie( task );
    } else {
        task->state[EFFECT_STATE_FRAME] += 1;
        g_nextTask = task->Next;
    }
}

/*
==================
Fx_BigExplosionTask
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Fx_BigExplosionTask( TASK *task )
{
    short frame = (short)( task->state[EFFECT_STATE_FRAME] >> 1 );
    if ( Fx_DrawAdditive( task->state[0], task->state[1], task->state[2], frame, &g_bigExplosionFrames ) ) {
        TaskDie( task );
    } else {
        task->state[EFFECT_STATE_FRAME] += 1;
        g_nextTask = task->Next;
    }
}

/*
==================
f_40228
==================
*/
/* identity-TODO: an additive effect task over g_effectFrames73314, like the task above;
 * no jal, pointer word, lui/lo pair or $gp operand in retail reaches it, so no spawner names it. */
void f_40228( TASK *task )
{
    short frame = (short)task->state[EFFECT_STATE_FRAME];
    if ( Fx_DrawAdditive( (short)task->state[0], (short)task->state[1], (short)task->state[2], frame, &g_effectFrames73314 ) ) {
        TaskDie( task );
    } else {
        task->state[EFFECT_STATE_FRAME] = task->state[EFFECT_STATE_FRAME] + 1;
        g_nextTask = task->Next;
    }
}
