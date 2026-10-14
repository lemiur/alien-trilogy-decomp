/*
 * The texture half of a POLY_FT4: four uv pairs with clut and tpage, copied into
 * packets word by word. Requires <sys/types.h>.
 */
#ifndef SPRITEUV_H
#define SPRITEUV_H

typedef struct {
    u_char  u0, v0;
    u_short clut;
    u_char  u1, v1;
    u_short tpage;
    u_char  u2, v2;
    u_short pad1;
    u_char  u3, v3;
    u_short pad2;
} spriteUV_t;

/* Words of a texture record (spriteUV_t, glyphUV_t) as copied into a POLY_FT4. name invented */
#define UV_WORDS 4

/* Texture words of one font glyph, copied whole into a POLY_FT4. */
typedef union glyphUV_s {
    u_long     word[UV_WORDS];
    spriteUV_t uv;
} glyphUV_t;

/* getTPage/LoadTPage texture modes and semi-transparency rates, as LIBGPU.H documents
 * tp (0 4-bit, 1 8-bit, 2 16-bit) and abr (0 B/2+F/2, 1 B+F, 2 B-F, 3 B+F/4). names invented */
enum {
    TPAGE_MODE_4BIT,
    TPAGE_MODE_8BIT,
    TPAGE_MODE_16BIT
};
enum {
    TPAGE_ABR_HALF,
    TPAGE_ABR_ADD,
    TPAGE_ABR_SUBTRACT,
    TPAGE_ABR_ADD_QUARTER
};

#endif
