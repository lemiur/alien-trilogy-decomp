/* Frozen copy of src/texture_upload.c with its project headers inlined: a static in .sdata
 * (g_unpack_hblanks) beside six .sbss statics. Test input only (tools/test_gate.py). */
/* VRAM upload queue: sprite frames waiting for LoadImage, flushed once a frame by
 * f_2194c. Retail .text 0x800218a4..0x80021e88. One translation unit: its statics
 * (.sbss 0x8008d0d0..0x8008d0f8, .sdata 0x8008cfa8) are read only by these functions.
 * Name invented.
 */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>

/* include/SPRITEUV.H, inlined */
/*
 * The texture half of a POLY_FT4: four uv pairs with clut and tpage, copied into
 * packets word by word. Requires <sys/types.h>.
 */
#ifndef SPRITEUV_H
#define SPRITEUV_H

typedef struct {
    u_char u0, v0;
    u_short clut;
    u_char u1, v1;
    u_short tpage;
    u_char u2, v2;
    u_short pad1;
    u_char u3, v3;
    u_short pad2;
} SpriteUV;

#endif
/* include/SPRITE.H, inlined */
/*
 * Animated sprite: texture block, current frame, animation script and VRAM slot.
 * Actors embed one at +0x80. sprite_step steps the script, f_21ef4 runs its commands,
 * f_21a80 uploads the frame's pixels to the VRAM slot and the bind code at
 * 0x8002a714..0x8002aa54 points the texture block at that slot.
 * Requires <sys/types.h>, <LIBGTE.H> and <SPRITEUV.H>. Names without evidence are
 * marked invented; unidentified members are identity-TODO.
 */
#ifndef SPRITE_H
#define SPRITE_H

/* 12-byte animation frame; frame lists are indexed by script commands (sprite_step). */
typedef struct SpriteFrame {
    short x; /* offset from the sprite's screen position (f_22158) */
    short y;
    short w;
    short h;
    u_long *pixels;
} SpriteFrame;

typedef struct Sprite {
    SpriteUV uv;
    SpriteFrame *frame;
    SpriteFrame *frames;
    short *script; /* invented name: animation command stream (f_21ef4) */
    short *loop; /* invented name: command f_21ef4 jumps back to */
    struct ClutRow *clut; /* the CLUT row the sprite is drawn with (<TEXTURE.H>) */
    struct TexturePage *slot; /* page of g_page_sets[0] or [3] the frame is uploaded to (<TEXTURE.H>) */
    DVECTOR screen; /* RotTransPers output (actor_project) */
    short depth;
    short unknown_2e; /* identity-TODO */
    u_short remaining; /* invented name: ticks left on the current command (sprite_step) */
    u_short duration; /* invented name */
    short scale; /* invented name: world size; f_29af8 and f_2a360 draw frame offsets * scale / depth, f_28a30 sets it per kind */
    u_short flags;
    short sound; /* invented name: sound id set by script command 0x1xx (f_21ef4), played with flag 0x10 (f_2fc74) */
    short repeat; /* invented name: loop counter of command 0x02xx (f_21ef4) */
} Sprite;

/* Frame staging buffers allocated by f_21e88 (src/game_helpers7.c); texture_upload
 * unpacks frames into g_unpack_buffer, up to g_unpack_buffer_end (0x4000 bytes on).
 * g_boss_unpack_buffer exists only on levels 11, 21 and 34. The automap (automap_build)
 * reuses g_unpack_buffer as scratch.
 * names: runtime evidence, analysis/runtime/globals.json */
extern void *g_unpack_buffer;
extern void *g_unpack_buffer_end;
extern void *g_boss_unpack_buffer;

/* src/game_20000_2.c: unpack a compressed sprite frame into dst (two encodings) */
extern int f_2175c(unsigned char *src, unsigned char *dst);
extern void f_217d8(unsigned char *src, unsigned char *dst);

/* src/texture_upload.c: queue a sprite's frame for upload to its VRAM slot, unpacking a
 * compressed frame first (mode picks the encoding; f_21ccc unpacks into
 * g_boss_unpack_buffer); 0 when no upload record or staging space is free */
extern int f_21a80(Sprite *sprite, short mode);
extern int f_21ccc(Sprite *sprite);

/* src/player.c */
extern struct TexturePage *weapon_sprite_bind_slot(void);

/* src/game_20000_8.c: queue a sprite's frame as a POLY_FT4 */
extern void f_22158(Sprite *sprite, CVECTOR *color);
extern void f_2227c(Sprite *sprite, CVECTOR *color);
extern void f_2b260(Sprite *sprite, CVECTOR *color);

/*
 * name: inferred from actor_project and f_277cc: RotTransPers depth << 2 of the last projected
 * sprite or particle; sprites keep it as their depth, >> 5 is the ordering-table index.
 */
extern int g_projected_depth;

/* src/game_helpers7.c: steps a sprite's animation script */
extern void sprite_step(Sprite *sprite);

/* uploads a sprite's frame to its VRAM slot (unmatched, 0x80021a80); mode is a halfword (retail tests it as sll 16) */
extern int f_21a80(Sprite *sprite, short mode);

/* src/game_helpers7.c */
extern void f_21e88(void);

/* src/texture_upload.c */
extern int f_219f4(Sprite *first, Sprite *second);
extern void f_218a4(void);
extern void f_218cc(void);
extern void f_2194c(void);

#endif
/* include/HEAP.H, inlined */
/*
 * The game heap (src/main.c):
 * one malloc block used as a stack. heap_alloc takes word-aligned bytes from the
 * low end, heap_free returns the most recent ones, and heap_mark/heap_release save
 * and restore the whole state. Names invented.
 */
#ifndef HEAP_H
#define HEAP_H

#define HEAP_SNAPSHOT_COUNT 8

/* name: inferred from use: heap_mark saves the three counters, heap_release restores them
 * (member names follow the globals they save) */
typedef struct HeapSnapshot {
    unsigned int used;
    unsigned int free;
    unsigned int next;
} HeapSnapshot;

/* names: runtime evidence, analysis/runtime/globals.json (title and level 1 samples:
 * used + free == size == 0x15c400 throughout; death walks next back down) */
/* the next free byte; heap_alloc returns it and moves it up */
extern unsigned char *g_heap_next;
/* bytes allocated so far */
extern unsigned int g_heap_used;
/* bytes still free; heap_alloc stops with an error when too few */
extern unsigned int g_heap_free;
/* size of the malloc block init_heap claims */
extern unsigned int g_heap_size;
extern void *g_heap_allocation;
extern HeapSnapshot g_heap_snapshots[HEAP_SNAPSHOT_COUNT];
extern short g_heap_snapshot_current;

extern void init_heap(void);
extern void *heap_alloc(int bytes);
extern void heap_free(int bytes);
extern int heap_mark(void);
extern void heap_release(short slot);
extern void fatal_error(const char *message);

#endif
#include <KERNEL.H>
#include <LIBAPI.H>
/* include/TEXTURE.H, inlined */
/*
 * VRAM bookkeeping for sprite and menu textures (src/main.c, src/sprites.c):
 * grids of texture pages and runs of CLUT rows, reserved at boot by main and handed
 * out to sprite files. Requires <sys/types.h>. Names invented.
 */
#ifndef TEXTURE_H
#define TEXTURE_H


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

extern ClutSet g_clut_sets[2];
extern TexturePageSet g_page_sets[4];

extern void init_clut_rows(ClutSet *set);
extern void init_texture_pages(TexturePageSet *set);
extern TexturePage *find_free_texture_page(TexturePageSet *set);
extern void upload_clut_row(ClutRow *row, unsigned long *pixels);
extern void sprite_file_load(short id, TexturePage **pages, ClutRow **cluts, SpriteUV *uvs);

/* name: inferred from use: the front end's and the level's general UV records (g_model_uvs points here by default) */
extern SpriteUV g_sprite_uvs[];

/* Sprite sets loaded by sprite_sets_load; names from their files:
 * \LANGUAGE\PNL0GFXU.BND (HUD panel), \GFX\PICKGFX.BND (pickups), \GFX\EXPLGFX.BND (explosions). */
extern TexturePage *g_panel_page;
extern ClutRow *g_panel_cluts[9];
extern SpriteUV g_panel_uvs[];
extern TexturePage *g_pickup_page;
extern ClutRow *g_pickup_cluts[16];
extern SpriteUV g_pickup_uvs[];
extern TexturePage *g_explosion_page;
extern ClutRow *g_explosion_cluts[3];
extern SpriteUV g_explosion_uvs[];

/* The level's sprite CLUT rows: rows 6..23 of g_clut_sets[0], handed out by f_1d114
 * (src/sprites.c) to the weapon views (player.c) and the level sprites.
 * names: runtime evidence, analysis/runtime/globals.json (the weapon_select clut of each
 * weapon; the clut of the actors of kind 1 egg, 2 facehugger, 6 adult alien, 7 Queen).
 * The other rows are not tied to a creature yet. */
extern ClutRow *g_pulse_rifle_clut, *g_pistol_clut, *g_flamethrower_clut, *g_smart_gun_clut, *g_shotgun_clut, *g_egg_clut, *g_facehugger_clut;
extern ClutRow *g_alien_clut, *g_8d3d0, *g_8d398, *g_8d3a0, *g_8d3f8, *g_8d3e8, *g_8d450;
extern ClutRow *g_8d3d8, *g_8d488, *g_queen_clut, *g_8d3f0;
/* Actor kind remap: f_1d114 sets entry i to i; f_298cc replaces ActorSlot.kind by its entry.
 * name: runtime evidence, analysis/runtime/globals.json (identity at level 1) */
extern short g_actor_kind_map[20];

/* src/sprites.c */
extern void f_1c588(void);
extern void f_1cc34(void);
extern void f_1d114(void);
extern void panel_sprites_load_alternate(void);
extern void sprite_sets_load(void);

#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef struct TextureUpload {
    RECT rect;
    u_long *pixels;
    int compressed; /* invented name: pixels were unpacked into a staging buffer (f_21a80) */
    struct TextureUpload *next; /* free-list link, then the previously queued upload */
    struct TextureUpload *queued;
} TextureUpload;


/* name invented: horizontal blanks (GetRCnt(RCntCNT1)) spent unpacking frames; written
 * only. Module static in initialized .sdata (0x8008cfa8). */
static u_short g_unpack_hblanks = 0;

/* Module statics (.sbss 0x8008d0d0..0x8008d0f8), in their retail order.
 * names: runtime evidence, analysis/runtime/globals.json (write watches at level load
 * and in play: f_21a80 unpacks into the cursor and queues, f_2194c drains the queue
 * and rewinds the cursor every frame). */
/* Next free byte of the staging buffer g_unpack_buffer. */
static void *g_unpack_cursor;
/* -1 once the cursor reaches g_unpack_buffer_end, cleared every frame. */
static short g_unpack_full;
/* Free upload records. */
static void *g_upload_free;
/* Last queued upload. */
static TextureUpload *g_upload_queue_tail;
/* First queued upload. */
static TextureUpload *g_upload_queue;
/* The four upload records (heap block of 0x60 bytes, f_218a4). */
static u8 *g_upload_pool;

void f_218a4(void) { g_upload_pool = heap_alloc(0x60); }

void f_218cc(void)
{
    u8 *record = g_upload_pool;
    void **link;
    short i;
    int again;
    void *old_head;

    g_unpack_full = 0;
    g_upload_queue = 0;
    g_upload_queue_tail = 0;
    g_upload_free = 0;
    i = 0;
    g_unpack_cursor = g_unpack_buffer;
    link = (void **)(record + 0x10);
    do {
        old_head = g_upload_free;
        link[1] = 0;
        g_upload_free = record;
        ++i;
        record += 0x18;
        again = i < 4;
        link[0] = old_head;
        link += 6;
    } while (again);
}

void f_2194c(void)
{
    TextureUpload *upload;

    while ((upload = g_upload_queue) != 0) {
        if (upload->queued) {
            g_upload_queue = upload->queued;
            upload->queued = 0;
            upload->next = g_upload_free;
            g_upload_free = upload;
        } else {
            g_upload_queue = 0;
            g_upload_queue_tail = 0;
            upload->next = g_upload_free;
            g_upload_free = upload;
        }
        LoadImage(&upload->rect, upload->pixels);
    }
    g_unpack_full = 0;
    g_unpack_cursor = g_unpack_buffer;
}

int f_219f4(Sprite *first, Sprite *second)
{
    TextureUpload *free_list = g_upload_free;
    int a, b;

    if (free_list == 0 || free_list->next == 0)
        return 0;
    a = first->flags & 1;
    b = second->flags & 1;
    if (a && b) {
        if (g_unpack_cursor != g_unpack_buffer)
            return 0;
    } else if ((a || b) && g_unpack_full) {
        return 0;
    }
    return -1;
}

/* name: SPRITE.H. The two arms are written in different statement orders, and only the
 * uncompressed one ends with its own queue tail and return: that keeps retail's layout, where
 * the compressed arm's queue stores stay separate from the uncompressed arm's. */
int f_21a80(Sprite *sprite, short mode)
{
    TextureUpload *upload = g_upload_free;
    long start;

    if (upload == 0)
        return 0;
    if ((upload->compressed = sprite->flags & 1)) {
        if (g_unpack_full)
            return 0;
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        g_upload_free = upload->next;
        upload->rect.h = sprite->frame->h;
        upload->pixels = g_unpack_cursor;
        start = GetRCnt(RCntCNT1);
        if (mode == 0)
            f_2175c((u_char *)sprite->frame->pixels, g_unpack_cursor);
        else
            f_217d8((u_char *)sprite->frame->pixels, g_unpack_cursor);
        g_unpack_hblanks = GetRCnt(RCntCNT1) - start + g_unpack_hblanks;
        if (g_unpack_cursor == g_unpack_buffer_end)
            g_unpack_full = -1;
        else
            g_unpack_cursor = g_unpack_buffer_end;
        upload->next = g_upload_queue_tail;
        if (g_upload_queue_tail == 0)
            g_upload_queue = upload;
        else
            g_upload_queue_tail->queued = upload;
    } else {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        upload->rect.h = sprite->frame->h;
        g_upload_free = upload->next;
        upload->pixels = sprite->frame->pixels;
        upload->next = g_upload_queue_tail;
        if (g_upload_queue_tail == 0)
            g_upload_queue = upload;
        else
            g_upload_queue_tail->queued = upload;
        g_upload_queue_tail = upload;
        return -1;
    }
    g_upload_queue_tail = upload;
    return -1;
}

/* name: SPRITE.H. f_21a80 for the boss sprite sets: one encoding, unpacked into
 * g_boss_unpack_buffer, which is never full. */
int f_21ccc(Sprite *sprite)
{
    TextureUpload *upload = g_upload_free;
    long start;

    if (upload == 0)
        return 0;
    if ((upload->compressed = sprite->flags & 1)) {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        g_upload_free = upload->next;
        upload->rect.h = sprite->frame->h;
        upload->pixels = g_boss_unpack_buffer;
        start = GetRCnt(RCntCNT1);
        f_2175c((u_char *)sprite->frame->pixels, g_boss_unpack_buffer);
        g_unpack_hblanks = GetRCnt(RCntCNT1) - start + g_unpack_hblanks;
        upload->next = g_upload_queue_tail;
        if (g_upload_queue_tail == 0)
            g_upload_queue = upload;
        else
            g_upload_queue_tail->queued = upload;
    } else {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        upload->rect.h = sprite->frame->h;
        g_upload_free = upload->next;
        upload->pixels = sprite->frame->pixels;
        upload->next = g_upload_queue_tail;
        if (g_upload_queue_tail == 0)
            g_upload_queue = upload;
        else
            g_upload_queue_tail->queued = upload;
        g_upload_queue_tail = upload;
        return -1;
    }
    g_upload_queue_tail = upload;
    return -1;
}
