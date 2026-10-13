/*
 * VRAM bookkeeping for sprite and menu textures (src/main.c, src/sprites.c):
 * grids of texture pages and runs of CLUT rows, reserved at boot by main and handed
 * out to sprite files. Requires <sys/types.h>. Names invented.
 */
#ifndef TEXTURE_H
#define TEXTURE_H

#include <SPRITEUV.H>

/* One texture page of a grid. Sprites upload their frames into the pages of
 * g_page_sets[0] (0x8006a5a0) and [3] (0x8006a5dc) and point Sprite.slot at the page. */
typedef struct TexturePage {
    unsigned short tpage;
    short x, y;
    unsigned char reserved; /* held for good: sprite sets and the font; vram_slots_reserve, hugger_overlay_start and weapon_sprite_bind_slot lock sprite pages, which vram_slots_release then keeps */
    unsigned char users; /* nonzero while taken; sprite pages count the sprites bound to them */
    unsigned long *pixels; /* frame pixels the page currently holds (sprite pages) */
} TexturePage;

/* A grid of texture pages in VRAM: pages start at (x, y), step by
 * (x_step, y_step) and wrap at (x_end, y_end). */
typedef struct TexturePageSet {
    short x, y;
    short x_end, y_end;
    short x_step, y_step;
    short mode;
    short count;
    TexturePage *pages;
} TexturePageSet;

/* g_page_sets entries, by what they hold (main reserves them at boot; sprites.c, text.c,
 * actor.c and player.c hand out their pages). names: invented */
enum TexturePageSetId {
    PAGE_SET_SPRITES,        /* 0: twelve 128x128 pages for the level's actor and weapon sprites */
    PAGE_SET_MENU,           /* 1: five 256x256 pages of menu resources */
    PAGE_SET_SPRITE_SHEETS,  /* 2: four 256x256 8-bit pages: the font, pickups, HUD panel, explosions */
    PAGE_SET_SMALL_SPRITES,  /* 3: eight 128x64 pages for the sprites of the small actor kinds */
    PAGE_SET_COUNT
};

/* g_clut_sets entries: the CLUT runs at (0, 480) and (256, 480). names: invented */
enum ClutSetId {
    CLUT_SET_SPRITES,        /* 0: level sprite and menu resource CLUTs (mode 0) */
    CLUT_SET_SPRITE_SHEETS,  /* 1: the font, pickup, panel and explosion CLUTs (mode 1) */
    CLUT_SET_COUNT
};

/* Width in VRAM halfwords of one CLUT row (upload_clut_row). name: invented */
#define CLUT_ROW_WIDTH 32

/* A CLUT row in VRAM, 8 bytes; in_use marks it as taken. */
typedef struct ClutRow {
    unsigned short clut;
    short x, y;
    short in_use;
} ClutRow;

/* A run of CLUT rows starting at (x, y). */
typedef struct ClutSet {
    short x, y;
    short mode;
    short count;
    ClutRow *rows;
} ClutSet;

extern ClutSet g_clut_sets[CLUT_SET_COUNT];
extern TexturePageSet g_page_sets[PAGE_SET_COUNT];

extern void init_clut_rows(ClutSet *set);
extern void init_texture_pages(TexturePageSet *set);
extern TexturePage *find_free_texture_page(TexturePageSet *set);
extern void upload_clut_row(ClutRow *row, unsigned long *pixels);
extern void sprite_file_load(short id, TexturePage **pages, ClutRow **cluts, SpriteUV *uvs);

/* name: inferred from use: the front end's and the level's general UV records (g_model_uvs points here by default) */
extern SpriteUV g_sprite_uvs[];
/* g_sprite_uvs records wall_shatter copies onto a shattered wall's panels: level 131's, level 211's,
 * and every other level's. names: invented */
#define SPRITE_UV_WALL_PANEL_MED_LAB 280
#define SPRITE_UV_WALL_PANEL 335
#define SPRITE_UV_WALL_PANEL_LIVING_QUARTERS 344

/* Sprite sets loaded by sprite_sets_load; names from their files:
 * \LANGUAGE\PNL0GFXU.BND (HUD panel), \GFX\PICKGFX.BND (pickups), \GFX\EXPLGFX.BND (explosions). */
#define PANEL_CLUT_COUNT 9
#define PICKUP_CLUT_COUNT 16
#define EXPLOSION_CLUT_COUNT 3
extern TexturePage *g_panel_page;
extern ClutRow *g_panel_cluts[PANEL_CLUT_COUNT];
extern SpriteUV g_panel_uvs[];
extern TexturePage *g_pickup_page;
extern ClutRow *g_pickup_cluts[PICKUP_CLUT_COUNT];
extern SpriteUV g_pickup_uvs[];
extern TexturePage *g_explosion_page;
extern ClutRow *g_explosion_cluts[EXPLOSION_CLUT_COUNT];
extern SpriteUV g_explosion_uvs[];

/* The level's sprite CLUT rows: rows 6..23 of g_clut_sets[0], handed out by level_sprites_load
 * (src/sprites.c) to the weapon views (player.c) and the level sprites.
 * names: runtime evidence, analysis/runtime/globals.json (the weapon_select clut of each
 * weapon; the clut of the actors of kind 1 egg, 2 facehugger, 6 adult alien, 7 Queen).
 * The other rows are not tied to a creature yet. */
extern ClutRow *g_pulse_rifle_clut, *g_pistol_clut, *g_flamethrower_clut, *g_smart_gun_clut, *g_shotgun_clut, *g_egg_clut, *g_facehugger_clut;
extern ClutRow *g_alien_clut, *g_handler_clut, *g_burster_clut, *g_bambi_clut, *g_soldier_clut, *g_guard_clut, *g_synth_clut;
extern ClutRow *g_dog_clut, *g_fingers_clut, *g_queen_clut, *g_colonist_clut;
/* Actor kind remap: level_sprites_load sets entry i to i; actor_slots_init replaces ActorSlot.kind by its entry.
 * name: runtime evidence, analysis/runtime/globals.json (identity at level 1) */
#define ACTOR_KIND_MAP_LENGTH 20 /* one entry per actor kind (ACTOR_KIND_* 0..19) */
extern short g_actor_kind_map[ACTOR_KIND_MAP_LENGTH];

/* src/sprites.c */
extern void menu_textures_reserve(void);
extern void level_panel_sprites_load(void);
extern void level_sprites_load(void);
extern void panel_sprites_load_alternate(void);
extern void sprite_sets_load(void);

#endif
