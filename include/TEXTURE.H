/*
 * VRAM bookkeeping for sprite and menu textures (src/main.c, src/sprites.c):
 * grids of texture pages and runs of CLUT rows, reserved at boot by main and handed
 * out to sprite files. Requires <sys/types.h>. Names invented.
 */
#ifndef TEXTURE_H
#define TEXTURE_H

#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <SPRITEUV.H>

/* One pending VRAM upload of a sprite frame (src/texture_upload.c). */
typedef struct textureUpload_s {
    RECT    rect;
    u_long *pixels;
    int     compressed; /* invented name: pixels were unpacked into a staging buffer (Tex_UploadSpriteFrame) */
    struct textureUpload_s *next; /* free-list link, then the previously queued upload */
    struct textureUpload_s *queued;
} textureUpload_t;

/* One texture page of a grid. Sprites upload their frames into the pages of
 * g_pageSets[0] (0x8006a5a0) and [3] (0x8006a5dc) and point Sprite.slot at the page. */
typedef struct texturePage_s {
    unsigned short tpage;
    short          x, y;
    unsigned char reserved; /* held for good: sprite sets and the font; Sys_ReserveVRAMSlots, Actor_StartHuggerOverlay and Player_BindWeaponSpriteSlot lock sprite pages, which Sys_ReleaseVRAMSlots then keeps */
    unsigned char  users;   /* nonzero while taken; sprite pages count the sprites bound to them */
    unsigned long *pixels;  /* frame pixels the page currently holds (sprite pages) */
} texturePage_t;

/* A grid of texture pages in VRAM: pages start at (x, y), step by
 * (xStep, yStep) and wrap at (xEnd, yEnd). */
typedef struct texturePageSet_s {
    short          x, y;
    short          xEnd, yEnd;
    short          xStep, yStep;
    short          mode;
    short          count;
    texturePage_t *pages;
} texturePageSet_t;

/* g_pageSets entries, by what they hold (main reserves them at boot; sprites.c, text.c,
 * actor.c and player.c hand out their pages). names: invented */
enum texturePageSetId_t {
    PAGE_SET_SPRITES,       /* 0: twelve 128x128 pages for the level's actor and weapon sprites */
    PAGE_SET_MENU,          /* 1: five 256x256 pages of menu resources */
    PAGE_SET_SPRITE_SHEETS, /* 2: four 256x256 8-bit pages: the font, pickups, HUD panel, explosions */
    PAGE_SET_SMALL_SPRITES, /* 3: eight 128x64 pages for the sprites of the small actor kinds */
    PAGE_SET_COUNT
};

/* g_clutSets entries: the CLUT runs at (0, 480) and (256, 480). names: invented */
enum clutSetId_t {
    CLUT_SET_SPRITES,       /* 0: level sprite and menu resource CLUTs (mode 0) */
    CLUT_SET_SPRITE_SHEETS, /* 1: the font, pickup, panel and explosion CLUTs (mode 1) */
    CLUT_SET_COUNT
};

/* Width in VRAM halfwords of one CLUT row (Sys_UploadCLUTRow). name: invented */
#define CLUT_ROW_WIDTH 32

/* A CLUT row in VRAM, 8 bytes; inUse marks it as taken. */
typedef struct clutRow_s {
    unsigned short clut;
    short          x, y;
    short          inUse;
} clutRow_t;

/* A run of CLUT rows starting at (x, y). */
typedef struct clutSet_s {
    short      x, y;
    short      mode;
    short      count;
    clutRow_t *rows;
} clutSet_t;

extern clutSet_t        g_clutSets[CLUT_SET_COUNT];
extern texturePageSet_t g_pageSets[PAGE_SET_COUNT];

extern void Sys_InitCLUTRows( clutSet_t *set );
extern void Sys_InitTexturePages( texturePageSet_t *set );
extern texturePage_t *Sys_FindFreeTexturePage( texturePageSet_t *set );
extern void Sys_UploadCLUTRow( clutRow_t *row, unsigned long *pixels );
extern void Sprite_LoadFile( short id, texturePage_t **pages, clutRow_t **cluts, spriteUV_t *uvs );

/* name: inferred from use: the front end's and the level's general UV records (g_modelUvs points here by default) */
extern spriteUV_t g_spriteUvs[];
/* g_spriteUvs records Debris_ShatterWall copies onto a shattered wall's panels: level 131's, level 211's,
 * and every other level's. names: invented */
#define SPRITE_UV_WALL_PANEL_MED_LAB 280
#define SPRITE_UV_WALL_PANEL 335
#define SPRITE_UV_WALL_PANEL_LIVING_QUARTERS 344

/* Sprite sets loaded by Sprite_LoadSets; names from their files:
 * \LANGUAGE\PNL0GFXU.BND (HUD panel), \GFX\PICKGFX.BND (pickups), \GFX\EXPLGFX.BND (explosions). */
#define PANEL_CLUT_COUNT 9
#define PICKUP_CLUT_COUNT 16
#define EXPLOSION_CLUT_COUNT 3
extern texturePage_t *g_panelPage;
extern clutRow_t     *g_panelCluts[PANEL_CLUT_COUNT];
extern spriteUV_t     g_panelUvs[];
extern texturePage_t *g_pickupPage;
extern clutRow_t     *g_pickupCluts[PICKUP_CLUT_COUNT];
extern spriteUV_t     g_pickupUvs[];
extern texturePage_t *g_explosionPage;
extern clutRow_t     *g_explosionCluts[EXPLOSION_CLUT_COUNT];
extern spriteUV_t     g_explosionUvs[];

/* The level's sprite CLUT rows: rows 6..23 of g_clutSets[0], handed out by Sprite_LoadLevelSprites
 * (src/sprites.c) to the weapon views (player.c) and the level sprites.
 * names: runtime evidence, analysis/runtime/globals.json (the Player_SelectWeapon clut of each
 * weapon; the clut of the actors of kind 1 egg, 2 facehugger, 6 adult alien, 7 Queen).
 * The other rows are not tied to a creature yet. */
extern clutRow_t *g_pulseRifleCLUT, *g_pistolCLUT, *g_flamethrowerCLUT, *g_smartGunCLUT, *g_shotgunCLUT, *g_eggCLUT, *g_facehuggerCLUT;
extern clutRow_t *g_alienCLUT, *g_handlerCLUT, *g_bursterCLUT, *g_bambiCLUT, *g_soldierCLUT, *g_guardCLUT, *g_synthCLUT;
extern clutRow_t *g_dogCLUT, *g_fingersCLUT, *g_queenCLUT, *g_colonistCLUT;
/* Actor kind remap: Sprite_LoadLevelSprites sets entry i to i; Actor_InitSlots replaces actorSlot_t.kind by its entry.
 * name: runtime evidence, analysis/runtime/globals.json (identity at level 1) */
#define ACTOR_KIND_MAP_LENGTH 20 /* one entry per actor kind (ACTOR_KIND_* 0..19) */
extern short g_actorKindMap[ACTOR_KIND_MAP_LENGTH];

/* src/sprites.c */
extern void Sprite_ReserveMenuTextures( void );
extern void Sprite_LoadLevelPanelSprites( void );
extern void Sprite_LoadLevelSprites( void );
extern void Sprite_LoadPanelSpritesAlternate( void );
extern void Sprite_LoadSets( void );

#endif
