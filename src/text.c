/* The menu font: loading its glyphs (font_load, \GFX\FONT1GFX.BND) and drawing text
 * with them, plus the byte readers next to it. Retail .text 0x8001bfa0..0x8001c588,
 * between frontend and the sprite loader. Its two statics are the first .sbss
 * slots after frontend's (0x8008d0b0, 0x8008d0b8). Name invented.
 * draw_glyph keeps its old-style definition: callers pass int arguments and the
 * body narrows them itself (retail re-masks ch before each test). */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LIBCD.H>
#include <HEAPFILE.H>
#include <DISPLAY.H>
#include <SPRITEUV.H>
#include <TEXTURE.H>
#include <TEXT.H>

/* name: inferred from use: font_load computes the glyph advance widths (from ' ') into it */
extern u_short g_menu_glyph_widths[MENU_FONT_GLYPHS];
/* Glyphs CHAR_EXTENDED and up: menu sprite and advance width. */
/* name: analysis/runtime/globals.json (round 2) */
static ExtendedGlyph g_extended_glyphs[] = {
    { 150, 8 }, { 145, 8 }, { 134, 8 }, { 127, 8 }, { 128, 8 }, { 130, 8 }, { 129, 8 },
    { 135, 8 }, { 131, 8 }, { 132, 8 }, { 133, 8 }, { 136, 4 }, { 137, 4 }, { 138, 4 },
    { 151, 8 }, { 152, 8 }, { 153, 8 }, { 157, 16 }, { 161, 16 }, { 140, 8 }, { 141, 8 },
    { 142, 8 }, { 144, 8 }, { 146, 8 }, { 148, 8 }, { 154, 8 }, { 155, 8 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 163, 8 }, { 139, 4 }, { 143, 8 },
    { 147, 8 }, { 149, 8 }, { 156, 8 }, { 0, 7 }, { 0, 7 }, { 159, 8 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 158, 8 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 162, 8 }
};

/* Queues one textured glyph quad at (x, y) and returns the next pen x.
 * The icon codes (CHAR_ICON_*) are drawn in fixed grey. */
/* name: inferred from its body, which emits one font glyph as a POLY_FT4 and returns the next x */
int draw_glyph(ch, x, y, color, height)
    u_char ch;
    short x;
    short y;
    const u_char *color;
    short height;
{
    POLY_FT4 *poly = g_sprite_workspace;
    short left = x;
    short glyph;
    u_long *uv;

    if (ch == CHAR_ICON_CROSS) {
        glyph = MENU_GLYPH_CROSS;
        x += g_menu_glyph_widths[glyph];
        uv = (u_long *)&g_menu_glyph_uvs[glyph];
    } else if (ch == CHAR_ICON_LEFT) {
        glyph = MENU_GLYPH_LEFT;
        x += g_menu_glyph_widths[glyph];
        uv = (u_long *)&g_menu_glyph_uvs[glyph];
    } else if (ch == CHAR_ICON_RIGHT) {
        glyph = MENU_GLYPH_RIGHT;
        x += g_menu_glyph_widths[glyph];
        uv = (u_long *)&g_menu_glyph_uvs[glyph];
    } else if ((short)ch >= CHAR_EXTENDED) {
        glyph = ch - CHAR_EXTENDED;
        x += g_extended_glyphs[glyph].width;
        uv = (u_long *)&g_menu_glyph_uvs[g_extended_glyphs[glyph].sprite];
    } else {
        glyph = ch - CHAR_FIRST_GLYPH;
        x += g_menu_glyph_widths[glyph];
        uv = (u_long *)&g_menu_glyph_uvs[glyph];
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
    if (glyph >= MENU_GLYPH_ICONS) {
        setRGB0(poly, MENU_ICON_INTENSITY, MENU_ICON_INTENSITY, MENU_ICON_INTENSITY);
    } else {
        setRGB0(poly, color[0], color[1], color[2]);
    }
    setPolyFT4(poly);
    addPrim(g_render_buffer, g_sprite_workspace);
    poly++;
    g_sprite_workspace = poly;
    return x;
}

/* name: inferred from its body, which draws a string with draw_glyph (16-pixel lines) */
void draw_text(const char *text, int x, short y, const unsigned char *color)
{
    int origin_x = x;
    short line_y = y;
    unsigned char ch;

    do {
        ch = (unsigned char)*text++;
        if (ch == 0)
            break;
        if (ch == '\n') {
            x = origin_x;
            line_y += MENU_LINE_HEIGHT;
        } else if (ch == ' ') {
            x += MENU_SPACE_WIDTH;
        } else {
            x = draw_glyph(ch, (short)x, line_y, color, MENU_GLYPH_HEIGHT);
        }
    } while (1);
}

/* name: inferred from its body, which draws text with newline and space handling, 10 lines apart */
void draw_text_multiline(const char *text, int x, short y, const unsigned char *color)
{
    int origin_x = x;
    short line_y = y;
    unsigned char ch;

    do {
        ch = (unsigned char)*text++;
        if (ch == 0)
            break;
        if (ch == '\n') {
            x = origin_x;
            line_y += MENU_LINE_HEIGHT_SMALL;
        } else if (ch == ' ') {
            x += MENU_SPACE_WIDTH;
        } else {
            x = draw_glyph(ch, (short)x, line_y, color, MENU_GLYPH_HEIGHT_SMALL);
        }
    } while (1);
}

extern const u_short g_char_widths[];
/* name: inferred from its body, which returns the widest line's width in pixels */
short text_width(const char *text)
{
    short max_width = 0;
    short width = 0;
    int ch = *text++;

    while (ch != 0) {
        ch &= CHAR_CODE_MASK;
        if (ch == '\n') {
            if (width > max_width)
                max_width = width;
            width = 0;
        } else if (ch == ' ') {
            width += MENU_SPACE_WIDTH;
        } else {
            width += g_char_widths[ch];
        }
        ch = *text++;
    }
    if (width > max_width)
        max_width = width;
    return max_width;
}

/* name: inferred from its body, which centres the text on the 320-wide screen before drawing it */
void draw_text_centered(const char *text, int x, int y, const unsigned char *color)
{
    short width = text_width(text);
    short x_short = (short)x;
    short y_short = (short)y;
    int centered_x = x_short + (SCREEN_WIDTH - width) / 2;
    draw_text(text, centered_x, y_short, color);
}

/* The font's texture page and CLUT row. Names invented. */
static TexturePage *g_font_page;
static ClutRow *g_font_clut;
extern short g_resource_status;

/* Loads the menu font: reserves the first page of g_page_sets[PAGE_SET_SPRITE_SHEETS] and the first
 * CLUT row of g_clut_sets[CLUT_SET_SPRITE_SHEETS], loads the glyphs and derives their widths. */
/* name: runtime evidence, analysis/runtime/functions.json */
void font_load(void)
{
    g_font_page = g_page_sets[PAGE_SET_SPRITE_SHEETS].pages;
    g_font_page->users = 1;
    g_font_page->reserved = 1;
    g_resource_status = 0;
    g_font_clut = g_clut_sets[CLUT_SET_SPRITE_SHEETS].rows;
    g_font_clut->in_use = 1;
    sprite_file_load(FILE_FONT1GFX_BND, &g_font_page, &g_font_clut, g_menu_glyph_uvs);
    {
        u_short *output = g_menu_glyph_widths;
        SpriteUV *record = g_menu_glyph_uvs;
        short i = 0;
        do {
            *output = (short)(record->u1 - record->u0);
            record++;
            output++;
            i++;
        } while (i < MENU_FONT_GLYPHS);
    }
    g_resource_status = -1;
}

/* name: inferred from its body, which assembles four bytes most significant first */
unsigned int read_be32(const unsigned char *bytes)
{
    return (((unsigned int)bytes[0] << 24) +
            ((unsigned int)bytes[1] << 16) +
            ((unsigned int)bytes[2] << 8)) | bytes[3];
}

short parse_decimal(const u_char *text)
{
    short value = 0;

    while (*text != 0)
        value = value * 10 + (*text++ - '0');
    return value;
}

