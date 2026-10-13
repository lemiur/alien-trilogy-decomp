#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <DISPLAY.H>
#include <GAME.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <TEXTURE.H>



/* name: runtime evidence, analysis/runtime/functions.json */
void screen_sprite_draw_semi(Sprite *sprite, CVECTOR *color)
{
    SpriteFrame *frame = sprite->frame;
    POLY_FT4 *poly = g_sprite_workspace;

    poly->x2 = poly->x0 = sprite->screen.vx + frame->x;
    poly->y1 = poly->y0 = sprite->screen.vy + frame->y;
    poly->x3 = poly->x1 = frame->w + (sprite->screen.vx + frame->x);
    poly->y3 = poly->y2 = frame->h + (sprite->screen.vy + frame->y);
    *(u_long *)&poly->u0 = *(u_long *)&sprite->uv.u0;
    *(u_long *)&poly->u1 = *(u_long *)&sprite->uv.u1;
    *(u_long *)&poly->u2 = *(u_long *)&sprite->uv.u2;
    *(u_long *)&poly->u3 = *(u_long *)&sprite->uv.u3;
    setPolyFT4(poly);
    setSemiTrans(poly, 1);
    setRGB0(poly, color->r, color->g, color->b);
    addPrim(g_render_buffer + 1, g_sprite_workspace);
    g_sprite_workspace = poly + 1;
}

/* name: static evidence, analysis/runtime/functions.json */
void screen_sprite_draw(Sprite *sprite, CVECTOR *color)
{
    SpriteFrame *frame = sprite->frame;
    POLY_FT4 *poly = g_sprite_workspace;

    poly->x2 = poly->x0 = sprite->screen.vx + frame->x;
    poly->y1 = poly->y0 = sprite->screen.vy + frame->y;
    poly->x3 = poly->x1 = frame->w + (sprite->screen.vx + frame->x);
    poly->y3 = poly->y2 = frame->h + (sprite->screen.vy + frame->y);
    *(u_long *)&poly->u0 = *(u_long *)&sprite->uv.u0;
    *(u_long *)&poly->u1 = *(u_long *)&sprite->uv.u1;
    *(u_long *)&poly->u2 = *(u_long *)&sprite->uv.u2;
    *(u_long *)&poly->u3 = *(u_long *)&sprite->uv.u3;
    setPolyFT4(poly);
    setRGB0(poly, color->r, color->g, color->b);
    addPrim(g_render_buffer + 1, g_sprite_workspace);
    g_sprite_workspace = poly + 1;
}

