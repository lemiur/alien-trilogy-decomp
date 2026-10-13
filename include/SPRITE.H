/*
 * Animated sprite: texture block, current frame, animation script and VRAM slot.
 * Actors embed one at +0x80. sprite_step steps the script, sprite_run_script runs its commands,
 * sprite_frame_upload uploads the frame's pixels to the VRAM slot and the bind code at
 * 0x8002a714..0x8002aa54 points the texture block at that slot.
 * Requires <sys/types.h>, <LIBGTE.H> and <SPRITEUV.H>. Names without evidence are
 * marked invented; unidentified members are identity-TODO.
 */
#ifndef SPRITE_H
#define SPRITE_H

/* 12-byte animation frame; frame lists are indexed by script commands (sprite_step). */
typedef struct SpriteFrame {
    short x; /* offset from the sprite's screen position (screen_sprite_draw_semi) */
    short y;
    short w;
    short h;
    u_long *pixels;
} SpriteFrame;

typedef struct Sprite {
    SpriteUV uv;
    SpriteFrame *frame;
    SpriteFrame *frames;
    short *script; /* invented name: animation command stream (sprite_run_script) */
    short *loop; /* invented name: command sprite_run_script jumps back to */
    struct ClutRow *clut; /* the CLUT row the sprite is drawn with (<TEXTURE.H>) */
    struct TexturePage *slot; /* page of g_page_sets[0] or [3] the frame is uploaded to (<TEXTURE.H>) */
    DVECTOR screen; /* RotTransPers output (actor_project) */
    short depth;
    short unknown_2e; /* identity-TODO: no 0x2e access on a Sprite, 0xae on an Actor or load/store of 0x8009a022 (g_player.weapon_sprite) in retail; never written at runtime (actors keep stale heap bytes there) and no runtime reader in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime, round 5) */
    u_short remaining; /* invented name: ticks left on the current command (sprite_step) */
    u_short duration; /* invented name */
    short scale; /* invented name: world size; actor_draw_shadowed and actor_draw_sprite draw frame offsets * scale / depth, actor_spawn sets it per kind */
    u_short flags;
    short sound; /* invented name: sound id set by script command 0x1xx (sprite_run_script), played with flag 0x10 (egg_task) */
    short repeat; /* invented name: loop counter of command 0x02xx (sprite_run_script) */
} Sprite;

/* LevelScreen.resources bits (<LEVEL.H>): the sprite set level_sprites_load loads for each.
 * names: from the set's file (LEVEL_SPRITES_HUGGER loads HUGGER.BND and FINGERS.BND) */
#define LEVEL_SPRITES_EGG 0x1
#define LEVEL_SPRITES_HUGGER 0x2
#define LEVEL_SPRITES_BURSTER 0x4
#define LEVEL_SPRITES_BAMBI 0x8
#define LEVEL_SPRITES_DOG 0x10
#define LEVEL_SPRITES_WARRIOR 0x20
#define LEVEL_SPRITES_QUEEN 0x40
#define LEVEL_SPRITES_WARRIOR_CEILING 0x80
#define LEVEL_SPRITES_DOG_CEILING 0x100
#define LEVEL_SPRITES_COLONIST 0x200
#define LEVEL_SPRITES_GUARD 0x400
#define LEVEL_SPRITES_SOLDIER 0x800
#define LEVEL_SPRITES_SYNTH 0x1000
#define LEVEL_SPRITES_HANDLER 0x2000

/* Frame staging buffers allocated by unpack_buffers_alloc (src/game_helpers7.c); texture_upload
 * unpacks frames into g_unpack_buffer, up to g_unpack_buffer_end (0x4000 bytes on).
 * g_boss_unpack_buffer exists only on levels 11, 21 and 34. The automap (automap_build)
 * reuses g_unpack_buffer as scratch.
 * names: runtime evidence, analysis/runtime/globals.json */
extern void *g_unpack_buffer;
extern void *g_unpack_buffer_end;
extern void *g_boss_unpack_buffer;

/* src/game_20000_2.c: unpack a compressed sprite frame into dst (two encodings) */
extern int unpack_rle(unsigned char *src, unsigned char *dst);
extern void unpack_lz(unsigned char *src, unsigned char *dst);

/* src/texture_upload.c: queue a sprite's frame for upload to its VRAM slot, unpacking a
 * compressed frame first (mode picks the encoding; boss_sprite_frame_upload unpacks into
 * g_boss_unpack_buffer); 0 when no upload record or staging space is free */
extern int sprite_frame_upload(Sprite *sprite, short mode);
extern int boss_sprite_frame_upload(Sprite *sprite);

/* src/player.c */
extern struct TexturePage *weapon_sprite_bind_slot(void);

/* src/game_20000_8.c: queue a sprite's frame as a POLY_FT4 */
extern void screen_sprite_draw_semi(Sprite *sprite, CVECTOR *color);
extern void screen_sprite_draw(Sprite *sprite, CVECTOR *color);
extern void hugger_overlay_draw(Sprite *sprite, CVECTOR *color);

/*
 * name: inferred from actor_project and particle_draw: RotTransPers depth << 2 of the last projected
 * sprite or particle; sprites keep it as their depth, >> 5 is the ordering-table index.
 */
extern int g_projected_depth;

/* src/game_helpers7.c: steps a sprite's animation script */
extern void sprite_step(Sprite *sprite);

/* uploads a sprite's frame to its VRAM slot (unmatched, 0x80021a80); mode is a halfword (retail tests it as sll 16) */
extern int sprite_frame_upload(Sprite *sprite, short mode);

/* src/game_helpers7.c */
extern void unpack_buffers_alloc(void);

/* src/texture_upload.c */
extern int upload_can_queue_pair(Sprite *first, Sprite *second);
extern void upload_pool_alloc(void);
extern void upload_queue_reset(void);
extern void upload_queue_flush(void);

/* Sprite.flags bits set and cleared by the animation script (sprite_run_script).
 * names: invented, from sprite_run_script and the actor code that tests them */
#define SPRITE_FLAG_PACKED 0x01 /* the frame is packed: texture upload unpacks it (unpack_rle/unpack_lz; runtime evidence); set by SPRITE_COMMAND_MARK, cleared by SPRITE_COMMAND_UNMARK */
#define SPRITE_FLAG_ENDED 0x02  /* the script ended or its loop count ran out */
#define SPRITE_FLAG_HALTED 0x04 /* SPRITE_COMMAND_HALT */
#define SPRITE_FLAG_CUE 0x08    /* SPRITE_COMMAND_CUE; the actor code clears it when it acts on it */
#define SPRITE_FLAG_SOUND 0x10  /* Sprite.sound is to be played this tick */
#define SPRITE_FLAG_MIRRORED 0x20 /* set by the actor code: drawn flipped left to right, the octants facing left (actor_face_view) */
/* The flag bits the animation script sets; actor_play_animation clears them. name: invented */
#define SPRITE_SCRIPT_FLAGS (SPRITE_FLAG_PACKED | SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED | SPRITE_FLAG_CUE)
/* Script words: a frame index, or a command with SPRITE_SCRIPT_COMMAND set, its code in
 * SPRITE_COMMAND_MASK and an operand in SPRITE_COMMAND_OPERAND. names: invented */
#define SPRITE_SCRIPT_COMMAND 0x8000
#define SPRITE_COMMAND_MASK 0x7f00
#define SPRITE_COMMAND_OPERAND 0xff
enum {
    SPRITE_COMMAND_SOUND = 0x100,  /* operand: the sample id */
    SPRITE_COMMAND_LOOP = 0x200,   /* operand: the repeat count */
    SPRITE_COMMAND_MARK = 0x300,
    SPRITE_COMMAND_UNMARK = 0x400,
    SPRITE_COMMAND_SKIP_5 = 0x500, /* skipped; identity-TODO */
    SPRITE_COMMAND_END = 0x600,
    SPRITE_COMMAND_SKIP_7 = 0x700, /* skipped; identity-TODO */
    SPRITE_COMMAND_HALT = 0x800,
    SPRITE_COMMAND_CUE = 0x900
};
/* unpack_buffers_alloc: the frame staging buffers. names: invented */
#define UNPACK_BUFFER_SIZE 0x4000
#define BOSS_UNPACK_BUFFER_SIZE 0x10000

#endif
