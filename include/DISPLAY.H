/*
 * The two display buffers (src/main.c allocates them in Sys_InitDisplayBuffers):
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
#define OT_BACK_SLOT ( OT_LENGTH - 2 )
/* A primitive's ordering-table slot is its screen depth (the GTE's sz) shifted down by this. name: invented */
#define OT_DEPTH_SHIFT 5
/* name: invented: bytes of each buffer's primitive area */
#define PRIM_AREA_SIZE 0xc000

/* VRAM size in pixels (GPU frame buffer). names: invented */
#define VRAM_WIDTH 1024
#define VRAM_HEIGHT 512

/* A GPU block-fill primitive (SetBlockFill, DrawPrim; Sys_ClearVRAM). */
typedef struct blockFillPacket_s {
    unsigned long  tag;
    unsigned char  red, green, blue, code;
    unsigned short x, y, width, height;
} blockFillPacket_t;

/* The 320x240 display (each buffer's drawing area). names: invented */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

/* g_fadeDepth (DpqColor depth) runs from 0 to FADE_DEPTH_FULL in FADE_DEPTH_STEP frames. names: invented */
#define FADE_DEPTH_FULL ONE
#define FADE_DEPTH_STEP 0x100

/* Sys_StartLevel's root counter 1 target; the scene timing reads the counter. name: invented */
#define HBLANK_COUNTER_TARGET 0x2000

/* Screen z from which polygons are depth cued (DpqColor of z - DEPTH_CUE_START); asm/gte.h has the same name */
#define DEPTH_CUE_START 0x1200

typedef struct displayBuffer_s {
    DISPENV disp;
    DR_ENV  draw;
    u_long  ot[OT_LENGTH];
    u_char  prims[PRIM_AREA_SIZE];
} displayBuffer_t;

/* The two buffers; the swap code alternates g_display between them. */
extern displayBuffer_t *g_renderWorkspace;
extern displayBuffer_t *g_displayDefault;
/* name: inferred from use: the buffer being drawn; Pause_SwapDisplayBuffers shows it and switches */
extern displayBuffer_t *g_display;
/* name: inferred from use: g_display's ordering table, which every primitive is added to */
extern u_long *g_renderBuffer;
/* name: inferred from use: the next free byte of g_display's primitive area */
extern void *g_spriteWorkspace;


/* src/main.c */
extern void Sys_SetupDoubleBuffer( RECT *first, RECT *second, u_char isbg, u_char dfe );

/* src/main.c */
/* g_displayMode, which Sys_SetDisplayMode stores and Sys_VBlankHandler acts on (src/main.c). names: invented */
enum displayMode_t {
    DISPLAY_MODE_NORMAL,     /* 0: the vertical blank only counts frames */
    DISPLAY_MODE_MESSAGE_BOX /* 1: the vertical blank also types the message box (Menu_MessageBoxType) */
};
extern void Sys_SetDisplayMode( int mode );

#endif
