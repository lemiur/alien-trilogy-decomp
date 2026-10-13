/*
 * The two display buffers (src/main.c allocates them in init_display_buffers):
 * each holds its display environment, a packed drawing environment, the ordering
 * table and the primitive area. Requires <sys/types.h>, <LIBGTE.H> and <LIBGPU.H>.
 * Names without evidence are marked invented.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#define OT_LENGTH 384
/* The ordering tables are cleared in reverse (ClearOTagR) and drawn from OT_LENGTH - 1 down:
 * the swap code puts the drawing environment in the last slot, and OT_BACK_SLOT is the first
 * slot drawn after it (the front end's screens and the pause menu's automap use it). name: invented */
#define OT_BACK_SLOT (OT_LENGTH - 2)
/* A primitive's ordering-table slot is its screen depth (the GTE's sz) shifted down by this. name: invented */
#define OT_DEPTH_SHIFT 5
/* name: invented: bytes of each buffer's primitive area */
#define PRIM_AREA_SIZE 0xc000

/* VRAM size in pixels (GPU frame buffer). names: invented */
#define VRAM_WIDTH  1024
#define VRAM_HEIGHT 512

/* The 320x240 display (each buffer's drawing area). names: invented */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

/* g_fade_depth (DpqColor depth) runs from 0 to FADE_DEPTH_FULL in FADE_DEPTH_STEP frames. names: invented */
#define FADE_DEPTH_FULL ONE
#define FADE_DEPTH_STEP 0x100

/* level_start's root counter 1 target; the scene timing reads the counter. name: invented */
#define HBLANK_COUNTER_TARGET 0x2000

/* Screen z from which polygons are depth cued (DpqColor of z - DEPTH_CUE_START); asm/gte.h has the same name */
#define DEPTH_CUE_START 0x1200

typedef struct DisplayBuffer {
    DISPENV disp;
    DR_ENV draw;
    u_long ot[OT_LENGTH];
    u_char prims[PRIM_AREA_SIZE];
} DisplayBuffer;

/* The two buffers; the swap code alternates g_display between them. */
extern DisplayBuffer *g_render_workspace;
extern DisplayBuffer *g_display_default;
/* name: inferred from use: the buffer being drawn; swap_display_buffers shows it and switches */
extern DisplayBuffer *g_display;
/* name: inferred from use: g_display's ordering table, which every primitive is added to */
extern u_long *g_render_buffer;
/* name: inferred from use: the next free byte of g_display's primitive area */
extern void *g_sprite_workspace;


/* src/main.c */
extern void setup_double_buffer(RECT *first, RECT *second, u_char isbg, u_char dfe);

/* src/main.c */
/* g_display_mode, which set_display_mode stores and vblank_handler acts on (src/main.c). names: invented */
enum DisplayMode {
    DISPLAY_MODE_NORMAL,     /* 0: the vertical blank only counts frames */
    DISPLAY_MODE_MESSAGE_BOX /* 1: the vertical blank also types the message box (message_box_type) */
};
extern void set_display_mode(int mode);

#endif
