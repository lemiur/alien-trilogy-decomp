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
typedef struct EffectFrameSequence EffectFrameSequence;
struct EffectFrameSequence {
    int scale_x;
    int scale_y;
    int frames[EFFECT_SEQUENCE_LENGTH];
    short translucency[EFFECT_SEQUENCE_LENGTH];
    short offset_x[EFFECT_SEQUENCE_LENGTH];
    short offset_y[EFFECT_SEQUENCE_LENGTH];
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char reserved;
};

extern int effect_draw_additive(int x, int y, int z, int frame, const EffectFrameSequence *frames);

/* The effect frame sequences (.data 0x800731bc..0x800735c4). */
/* name: analysis/runtime/globals.json (round 2) */
EffectFrameSequence g_explosion_frames = {
    3072, 3072,
    {0,1,2,3,4,5,6,7,8,-1,-1,-1,-1,-1,-1,-1},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1},
    {19,26,29,27,24,19,18,16,13,0,0,0,0,0,0,0},
    {22,26,28,32,34,34,36,34,36,0,0,0,0,0,0,0},
    255,255,255,0
};
/* name: analysis/runtime/globals.json (round 2) */
EffectFrameSequence g_big_explosion_frames = {
    3900, 4700,
    {9,10,11,12,13,14,15,16,17,-1,-1,-1,-1,-1,-1,-1},
    {0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1},
    {15,19,33,29,34,40,36,39,32,0,0,0,0,0,0,0},
    {6,12,67,75,76,77,74,76,81,0,0,0,0,0,0,0},
    255,255,255,0
};
EffectFrameSequence g_effect_frames_73314 = {
    2560, 2560,
    {18,19,20,21,22,23,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1},
    {4,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0},
    {4,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0},
    255,255,255,0
};

#define SHARED_EFFECT_INITIALIZER(red, green, blue) { \
    2048, 2048, \
    {24,25,26,27,28,29,30,31,-1,-1,-1,-1,-1,-1,-1,-1}, \
    {0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1}, \
    {7,11,10,11,11,12,12,8,0,0,0,0,0,0,0,0}, \
    {5,7,8,7,10,10,6,4,0,0,0,0,0,0,0,0}, \
    red, green, blue, 0 \
}
/* names: analysis/runtime/globals.json (round 2): acid (green), blood (red) and synthetic (white) splashes */
EffectFrameSequence g_acid_splash_frames = SHARED_EFFECT_INITIALIZER(0,255,0);
EffectFrameSequence g_blood_splash_frames = SHARED_EFFECT_INITIALIZER(255,0,0);
EffectFrameSequence g_synth_splash_frames = SHARED_EFFECT_INITIALIZER(255,255,255);


/* Draws frame `frame` of an effect sequence as a scaled sprite at a
 * world position and returns -1 once the sequence has ended */
/* name: runtime evidence, analysis/runtime/functions.json */
int effect_draw(int x, int y, int z, short frame, const EffectFrameSequence *frames)
{
    SVECTOR position;
    DVECTOR screen;
    POLY_FT4 *poly;
    SpriteUV *uv;
    int depth;
    int width;
    int height;
    int x0, y0, x1, y1;

    if (frames->frames[frame] == -1)
        return -1;
    position.vx = x;
    position.vy = y;
    position.vz = z;
    depth = g_projected_depth = RotTransPers(&position, (long *)&screen, &g_gte_interpolation, &g_gte_flag) << 2;
    if (depth > DRAW_NEAR_DEPTH && depth < DRAW_FAR_DEPTH && screen.vx >= EFFECT_MIN_SCREEN_X && screen.vx <= EFFECT_MAX_SCREEN_X) {
        uv = &g_explosion_uvs[frames->frames[frame]];
        poly = g_sprite_workspace;
        *(u_long *)&poly->u0 = *(u_long *)&uv->u0;
        *(u_long *)&poly->u1 = *(u_long *)&uv->u1;
        *(u_long *)&poly->u2 = *(u_long *)&uv->u2;
        *(u_long *)&poly->u3 = *(u_long *)&uv->u3;
        width = poly->u1 - poly->u0;
        if (width < 0)
            width = -width;
        width++;
        height = poly->v2 - poly->v0;
        if (height < 0)
            height = -height;
        height++;
        x0 = -frames->offset_x[frame] * frames->scale_x / depth;
        y0 = -frames->offset_y[frame] * frames->scale_y / depth;
        x1 = (-frames->offset_x[frame] + width) * frames->scale_x / depth;
        y1 = (-frames->offset_y[frame] + height) * frames->scale_y / depth;
        setXY4(poly, screen.vx + x0, screen.vy + y0, screen.vx + x1, poly->y0, poly->x0, screen.vy + y1, poly->x1, poly->y2);
        *(u_long *)&poly->r0 = *(u_long *)&g_palette->colors[0];
        if (frames->blue == 0) {
            if (frames->red) {
                poly->g0 = 0;
                poly->b0 = 0;
            } else if (frames->green) {
                poly->r0 = 0;
                poly->b0 = 0;
            }
        }
        setPolyFT4(poly);
        setSemiTrans(poly, frames->translucency[frame]);
        addPrim(g_render_buffer + (g_projected_depth >> OT_DEPTH_SHIFT), poly);
        poly++;
        g_sprite_workspace = poly;
    }
    return 0;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void acid_splash_task(TASK *task)
{
    unsigned int frame = task->state[3] >> EFFECT_FRAME_SHIFT;
    if (effect_draw((short)task->state[0], (short)task->state[1], (short)task->state[2], frame, &g_acid_splash_frames)) {
        TaskDie(task);
    } else {
        task->state[3] = task->state[3] + EFFECT_FRAME_STEP;
        g_next_task = task->Next;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void blood_splash_task(TASK *task)
{
    unsigned int frame = task->state[3] >> EFFECT_FRAME_SHIFT;
    if (effect_draw((short)task->state[0], (short)task->state[1], (short)task->state[2], frame, &g_blood_splash_frames)) {
        TaskDie(task);
    } else {
        task->state[3] = task->state[3] + EFFECT_FRAME_STEP;
        g_next_task = task->Next;
    }
}

/* name: static evidence, analysis/runtime/functions.json */
void synth_splash_task(TASK *task)
{
    unsigned int frame = task->state[3] >> EFFECT_FRAME_SHIFT;
    if (effect_draw((short)task->state[0], (short)task->state[1], (short)task->state[2], frame, &g_synth_splash_frames)) {
        TaskDie(task);
    } else {
        task->state[3] = task->state[3] + EFFECT_FRAME_STEP;
        g_next_task = task->Next;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void explosion_task(TASK *task)
{
    short frame = (short)(task->state[3] >> 1);
    if (effect_draw_additive(task->state[0], task->state[1], task->state[2], frame, &g_explosion_frames)) {
        TaskDie(task);
    } else {
        task->state[3] += 1;
        g_next_task = task->Next;
    }
}

/* name: static evidence, analysis/runtime/functions.json */
void big_explosion_task(TASK *task)
{
    short frame = (short)(task->state[3] >> 1);
    if (effect_draw_additive(task->state[0], task->state[1], task->state[2], frame, &g_big_explosion_frames)) {
        TaskDie(task);
    } else {
        task->state[3] += 1;
        g_next_task = task->Next;
    }
}

void f_40228(TASK *task)
{
    short frame = (short)task->state[3];
    if (effect_draw_additive((short)task->state[0], (short)task->state[1], (short)task->state[2], frame, &g_effect_frames_73314)) {
        TaskDie(task);
    } else {
        task->state[3] = task->state[3] + 1;
        g_next_task = task->Next;
    }
}

