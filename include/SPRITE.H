/*
 * Animated sprite: texture block, current frame, animation script and VRAM slot.
 * Actors embed one at +0x80. StepSprite steps the script, SpriteRunScript runs its commands,
 * Tex_UploadSpriteFrame uploads the frame's pixels to the VRAM slot and the bind code at
 * 0x8002a714..0x8002aa54 points the texture block at that slot.
 * Requires <sys/types.h>, <LIBGTE.H> and <SPRITEUV.H>. Names without evidence are
 * marked invented; unidentified members are identity-TODO.
 */
#ifndef SPRITE_H
#define SPRITE_H

/* 12-byte animation frame; frame lists are indexed by script commands (StepSprite). */
typedef struct spriteFrame_s {
    short   x; /* offset from the sprite's screen position (DrawScreenSpriteSemi) */
    short   y;
    short   w;
    short   h;
    u_long *pixels;
} spriteFrame_t;

typedef struct sprite_s {
    spriteUV_t            uv;
    spriteFrame_t        *frame;
    spriteFrame_t        *frames;
    short                *script; /* invented name: animation command stream (SpriteRunScript) */
    short                *loop;   /* invented name: command SpriteRunScript jumps back to */
    struct clutRow_s     *clut;   /* the CLUT row the sprite is drawn with (<TEXTURE.H>) */
    struct texturePage_s *slot;   /* page of g_pageSets[0] or [3] the frame is uploaded to (<TEXTURE.H>) */
    DVECTOR               screen; /* RotTransPers output (Actor_Project) */
    short                 depth;
    short unknown_2e; /* identity-TODO: no 0x2e access on a sprite_t, 0xae on an actor_t or load/store of 0x8009a022 (g_player.weaponSprite) in retail; never written at runtime (actors keep stale heap bytes there) and no runtime reader in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime) */
    u_short remaining; /* invented name: ticks left on the current command (StepSprite) */
    u_short duration;  /* invented name */
    short scale; /* invented name: world size; Actor_DrawShadowed and Actor_DrawSprite draw frame offsets * scale / depth, Actor_Spawn sets it per kind */
    u_short flags;
    short sound; /* invented name: sound id set by script command 0x1xx (SpriteRunScript), played with flag 0x10 (Actor_EggTask) */
    short repeat; /* invented name: loop counter of command 0x02xx (SpriteRunScript) */
} sprite_t;

/* LevelScreen.resources bits (<LEVEL.H>): the sprite set Sprite_LoadLevelSprites loads for each.
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

/* Frame staging buffers allocated by AllocUnpackBuffers (src/game_helpers7.c); texture_upload
 * unpacks frames into g_unpackBuffer, up to g_unpackBufferEnd (0x4000 bytes on).
 * g_bossUnpackBuffer exists only on levels 11, 21 and 34. The automap (Pause_BuildAutomap)
 * reuses g_unpackBuffer as scratch.
 * names: runtime evidence, analysis/runtime/globals.json */
extern void *g_unpackBuffer;
extern void *g_unpackBufferEnd;
extern void *g_bossUnpackBuffer;

/* src/game_20000_2.c: unpack a compressed sprite frame into dst (two encodings) */
extern int UnpackRLE( unsigned char *src, unsigned char *dst );
extern void UnpackLZ( unsigned char *src, unsigned char *dst );

/* src/texture_upload.c: queue a sprite's frame for upload to its VRAM slot, unpacking a
 * compressed frame first (mode picks the encoding; Tex_UploadBossSpriteFrame unpacks into
 * g_bossUnpackBuffer); 0 when no upload record or staging space is free */
extern int Tex_UploadSpriteFrame( sprite_t *sprite, short mode );
extern int Tex_UploadBossSpriteFrame( sprite_t *sprite );

/* src/player.c */
extern struct texturePage_s *Player_BindWeaponSpriteSlot( void );

/* src/game_20000_8.c: queue a sprite's frame as a POLY_FT4 */
extern void DrawScreenSpriteSemi( sprite_t *sprite, CVECTOR *color );
extern void DrawScreenSprite( sprite_t *sprite, CVECTOR *color );
extern void Actor_DrawHuggerOverlay( sprite_t *sprite, CVECTOR *color );

/*
 * name: inferred from Actor_Project and Particle_Draw: RotTransPers depth << 2 of the last projected
 * sprite or particle; sprites keep it as their depth, >> 5 is the ordering-table index.
 */
extern int g_projectedDepth;

/* src/game_helpers7.c: steps a sprite's animation script */
extern void StepSprite( sprite_t *sprite );

/* uploads a sprite's frame to its VRAM slot (unmatched, 0x80021a80); mode is a halfword (retail tests it as sll 16) */
extern int Tex_UploadSpriteFrame( sprite_t *sprite, short mode );

/* src/game_helpers7.c */
extern void AllocUnpackBuffers( void );

/* src/texture_upload.c */
extern int Tex_UploadCanQueuePair( sprite_t *first, sprite_t *second );
extern void Tex_UploadPoolAlloc( void );
extern void Tex_ResetUploadQueue( void );
extern void Tex_FlushUploadQueue( void );

/* sprite_t.flags bits set and cleared by the animation script (SpriteRunScript).
 * names: invented, from SpriteRunScript and the actor code that tests them */
#define SPRITE_FLAG_PACKED \
    0x01 /* the frame is packed: texture upload unpacks it (UnpackRLE/UnpackLZ; runtime evidence); set by SPRITE_COMMAND_MARK, cleared by SPRITE_COMMAND_UNMARK */
#define SPRITE_FLAG_ENDED 0x02  /* the script ended or its loop count ran out */
#define SPRITE_FLAG_HALTED 0x04 /* SPRITE_COMMAND_HALT */
#define SPRITE_FLAG_CUE 0x08    /* SPRITE_COMMAND_CUE; the actor code clears it when it acts on it */
#define SPRITE_FLAG_SOUND 0x10  /* sprite_t.sound is to be played this tick */
#define SPRITE_FLAG_MIRRORED \
    0x20 /* set by the actor code: drawn flipped left to right, the octants facing left (Actor_FaceView) */
/* The flag bits the animation script sets; Actor_PlayAnimation clears them. name: invented */
#define SPRITE_SCRIPT_FLAGS ( SPRITE_FLAG_PACKED | SPRITE_FLAG_ENDED | SPRITE_FLAG_HALTED | SPRITE_FLAG_CUE )
/* Script words: a frame index, or a command with SPRITE_SCRIPT_COMMAND set, its code in
 * SPRITE_COMMAND_MASK and an operand in SPRITE_COMMAND_OPERAND. names: invented */
#define SPRITE_SCRIPT_COMMAND 0x8000
#define SPRITE_COMMAND_MASK 0x7f00
#define SPRITE_COMMAND_OPERAND 0xff
enum {
    SPRITE_COMMAND_SOUND = 0x100, /* operand: the sample id */
    SPRITE_COMMAND_LOOP = 0x200,  /* operand: the repeat count */
    SPRITE_COMMAND_MARK = 0x300,
    SPRITE_COMMAND_UNMARK = 0x400,
    SPRITE_COMMAND_SKIP_5 = 0x500, /* skipped; identity-TODO */
    SPRITE_COMMAND_END = 0x600,
    SPRITE_COMMAND_SKIP_7 = 0x700, /* skipped; identity-TODO */
    SPRITE_COMMAND_HALT = 0x800,
    SPRITE_COMMAND_CUE = 0x900
};
/* AllocUnpackBuffers: the frame staging buffers. names: invented */
#define UNPACK_BUFFER_SIZE 0x4000
#define BOSS_UNPACK_BUFFER_SIZE 0x10000

/* A sprite resource file: CD table index, the animations its frames serve and its CLUT row. */
typedef struct resourceFile_s {
    int                  file;
    struct trackEntry_s *animations;
    struct clutRow_s   **palette;
} resourceFile_t;

/* Header of a sprite resource file; names invented. */
typedef struct spriteFileHeader_s {
    short width, height;
    short depth;
    short spriteCount; /* invented name: BX boxes in the file, in every shipped sprite file (not read) */
    short clutCount;   /* invented name: CLUT rows of its CL chunks, in every shipped sprite file (not read) */
    short pageCount;   /* invented name: its TP chunks, in every shipped sprite file (not read) */
    short stride;
} spriteFileHeader_t;

/* One g_spriteList record: the frames of one loaded sprite file (Sprite_ListAdd). name: invented */
typedef struct spriteListEntry_s {
    short          count;
    spriteFrame_t *frames;
} spriteListEntry_t;

#endif
