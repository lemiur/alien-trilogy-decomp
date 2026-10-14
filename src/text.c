/* text.c -- the menu font: loading its glyphs (Text_LoadFont, \GFX\FONT1GFX.BND) and drawing text with
 * them, plus the byte readers next to it */
/* Retail .text 0x8001bfa0..0x8001c588, between frontend and the sprite loader. Its two statics are
 * the first .sbss slots after frontend's (0x8008d0b0, 0x8008d0b8). Name invented. Text_DrawGlyph
 * keeps its old-style definition: callers pass int arguments and the body narrows them itself
 * (retail re-masks ch before each test). */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LIBCD.H>
#include <HEAPFILE.H>
#include <DISPLAY.H>
#include <SPRITEUV.H>
#include <TEXTURE.H>
#include <TEXT.H>

/* name: inferred from use: Text_LoadFont computes the glyph advance widths (from ' ') into it */
extern u_short g_menuGlyphWidths[MENU_FONT_GLYPHS];
/* Glyphs CHAR_EXTENDED and up: menu sprite and advance width. */
/* name: analysis/runtime/globals.json */
static extendedGlyph_t g_extendedGlyphs[] = {
    { 150, 8 },  { 145, 8 }, { 134, 8 }, { 127, 8 }, { 128, 8 }, { 130, 8 }, { 129, 8 }, { 135, 8 }, { 131, 8 },
    { 132, 8 },  { 133, 8 }, { 136, 4 }, { 137, 4 }, { 138, 4 }, { 151, 8 }, { 152, 8 }, { 153, 8 }, { 157, 16 },
    { 161, 16 }, { 140, 8 }, { 141, 8 }, { 142, 8 }, { 144, 8 }, { 146, 8 }, { 148, 8 }, { 154, 8 }, { 155, 8 },
    { 0, 7 },    { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 163, 8 }, { 139, 4 }, { 143, 8 }, { 147, 8 },
    { 149, 8 },  { 156, 8 }, { 0, 7 },   { 0, 7 },   { 159, 8 }, { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },
    { 158, 8 },  { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },
    { 0, 7 },    { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },
    { 0, 7 },    { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },
    { 0, 7 },    { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },
    { 0, 7 },    { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },
    { 0, 7 },    { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 0, 7 },   { 162, 8 }
};

/*
==================
Text_DrawGlyph

Queues one textured glyph quad at (x, y) and returns the next pen x.
The icon codes (CHAR_ICON_*) are drawn in fixed grey.
==================
*/
/* name: inferred from its body, which emits one font glyph as a POLY_FT4 and returns the next x */
int Text_DrawGlyph( ch, x, y, color, height )
u_char        ch;
short         x;
short         y;
const u_char *color;
short         height;
{
    POLY_FT4 *poly = g_spriteWorkspace;
    short     left = x;
    short     glyph;
    u_long   *uv;

    if ( ch == CHAR_ICON_CROSS ) {
        glyph = MENU_GLYPH_CROSS;
        x += g_menuGlyphWidths[glyph];
        uv = (u_long *)&g_menuGlyphUvs[glyph];
    } else if ( ch == CHAR_ICON_LEFT ) {
        glyph = MENU_GLYPH_LEFT;
        x += g_menuGlyphWidths[glyph];
        uv = (u_long *)&g_menuGlyphUvs[glyph];
    } else if ( ch == CHAR_ICON_RIGHT ) {
        glyph = MENU_GLYPH_RIGHT;
        x += g_menuGlyphWidths[glyph];
        uv = (u_long *)&g_menuGlyphUvs[glyph];
    } else if ( (short)ch >= CHAR_EXTENDED ) {
        glyph = ch - CHAR_EXTENDED;
        x += g_extendedGlyphs[glyph].width;
        uv = (u_long *)&g_menuGlyphUvs[g_extendedGlyphs[glyph].sprite];
    } else {
        glyph = ch - CHAR_FIRST_GLYPH;
        x += g_menuGlyphWidths[glyph];
        uv = (u_long *)&g_menuGlyphUvs[glyph];
    }
    *(u_long *)&poly->u0 = *uv++;
    *(u_long *)&poly->u1 = *uv++;
    *(u_long *)&poly->u2 = *uv++;
    *(u_long *)&poly->u3 = *uv;
    poly->x0 = left;
    poly->x2 = left;
    poly->y0 = y;
    poly->y1 = y;
    poly->x1 = x;
    poly->x3 = x;
    poly->y2 = y + height;
    poly->y3 = y + height;
    if ( glyph >= MENU_GLYPH_ICONS ) {
        setRGB0( poly, MENU_ICON_INTENSITY, MENU_ICON_INTENSITY, MENU_ICON_INTENSITY );
    } else {
        setRGB0( poly, color[0], color[1], color[2] );
    }
    setPolyFT4( poly );
    addPrim( g_renderBuffer, g_spriteWorkspace );
    poly++;
    g_spriteWorkspace = poly;
    return x;
}

/*
==================
Text_DrawText
==================
*/
/* name: inferred from its body, which draws a string with Text_DrawGlyph (16-pixel lines) */
void Text_DrawText( const char *text, int x, short y, const unsigned char *color )
{
    int           originX = x;
    short         lineY = y;
    unsigned char ch;

    do {
        ch = (unsigned char)*text++;
        if ( ch == 0 )
            break;
        if ( ch == '\n' ) {
            x = originX;
            lineY += MENU_LINE_HEIGHT;
        } else if ( ch == ' ' ) {
            x += MENU_SPACE_WIDTH;
        } else {
            x = Text_DrawGlyph( ch, (short)x, lineY, color, MENU_GLYPH_HEIGHT );
        }
    } while ( 1 );
}

/*
==================
Text_DrawTextMultiline
==================
*/
/* name: inferred from its body, which draws text with newline and space handling, 10 lines apart */
void Text_DrawTextMultiline( const char *text, int x, short y, const unsigned char *color )
{
    int           originX = x;
    short         lineY = y;
    unsigned char ch;

    do {
        ch = (unsigned char)*text++;
        if ( ch == 0 )
            break;
        if ( ch == '\n' ) {
            x = originX;
            lineY += MENU_LINE_HEIGHT_SMALL;
        } else if ( ch == ' ' ) {
            x += MENU_SPACE_WIDTH;
        } else {
            x = Text_DrawGlyph( ch, (short)x, lineY, color, MENU_GLYPH_HEIGHT_SMALL );
        }
    } while ( 1 );
}

extern const u_short g_charWidths[];
/*
==================
Text_Width
==================
*/
/* name: inferred from its body, which returns the widest line's width in pixels */
short Text_Width( const char *text )
{
    short maxWidth = 0;
    short width = 0;
    int   ch = *text++;

    while ( ch != 0 ) {
        ch &= CHAR_CODE_MASK;
        if ( ch == '\n' ) {
            if ( width > maxWidth )
                maxWidth = width;
            width = 0;
        } else if ( ch == ' ' ) {
            width += MENU_SPACE_WIDTH;
        } else {
            width += g_charWidths[ch];
        }
        ch = *text++;
    }
    if ( width > maxWidth )
        maxWidth = width;
    return maxWidth;
}

/*
==================
Text_DrawTextCentered
==================
*/
/* name: inferred from its body, which centres the text on the 320-wide screen before drawing it */
void Text_DrawTextCentered( const char *text, int x, int y, const unsigned char *color )
{
    short width = Text_Width( text );
    short xShort = (short)x;
    short yShort = (short)y;
    int   centeredX = xShort + ( SCREEN_WIDTH - width ) / 2;
    Text_DrawText( text, centeredX, yShort, color );
}

/* The font's texture page and CLUT row. Names invented. */
static texturePage_t *g_fontPage;
static clutRow_t     *g_fontCLUT;
extern short          g_resourceStatus;

/*
==================
Text_LoadFont

Loads the menu font: reserves the first page of g_pageSets[PAGE_SET_SPRITE_SHEETS] and the first
CLUT row of g_clutSets[CLUT_SET_SPRITE_SHEETS], loads the glyphs and derives their widths.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Text_LoadFont( void )
{
    g_fontPage = g_pageSets[PAGE_SET_SPRITE_SHEETS].pages;
    g_fontPage->users = 1;
    g_fontPage->reserved = 1;
    g_resourceStatus = 0;
    g_fontCLUT = g_clutSets[CLUT_SET_SPRITE_SHEETS].rows;
    g_fontCLUT->inUse = 1;
    Sprite_LoadFile( FILE_FONT1GFX_BND, &g_fontPage, &g_fontCLUT, g_menuGlyphUvs );
    {
        u_short    *output = g_menuGlyphWidths;
        spriteUV_t *record = g_menuGlyphUvs;
        short       i = 0;
        do {
            *output = (short)( record->u1 - record->u0 );
            record++;
            output++;
            i++;
        } while ( i < MENU_FONT_GLYPHS );
    }
    g_resourceStatus = -1;
}

/* Text_ReadBigEndian32: the left shifts of its first three bytes and the index of the last. names: invented */
#define BIG_ENDIAN_BYTE0_SHIFT 24
#define BIG_ENDIAN_BYTE1_SHIFT 16
#define BIG_ENDIAN_BYTE2_SHIFT 8
#define BIG_ENDIAN_LAST_BYTE 3

/*
==================
Text_ReadBigEndian32
==================
*/
/* name: inferred from its body, which assembles four bytes most significant first */
unsigned int Text_ReadBigEndian32( const unsigned char *bytes )
{
    return ( ( (unsigned int)bytes[0] << BIG_ENDIAN_BYTE0_SHIFT ) + ( (unsigned int)bytes[1] << BIG_ENDIAN_BYTE1_SHIFT )
             + ( (unsigned int)bytes[2] << BIG_ENDIAN_BYTE2_SHIFT ) )
           | bytes[BIG_ENDIAN_LAST_BYTE];
}

/*
==================
Text_ParseDecimal
==================
*/
short Text_ParseDecimal( const u_char *text )
{
    short value = 0;

    while ( *text != 0 )
        value = value * DECIMAL_RADIX + ( *text++ - '0' );
    return value;
}
