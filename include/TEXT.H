/*
 * Text output. The menu font (src/text.c) draws 16-pixel lines with
 * draw_glyph; the HUD font (src/hud.c, src/pause_menu.c) draws 10-pixel lines with
 * hud_draw_glyph. Requires <sys/types.h>.
 */
#ifndef TEXT_H
#define TEXT_H

#include <SPRITEUV.H>

/* Character codes of both fonts: glyphs start at ' ', codes from CHAR_EXTENDED index the
 * extended glyph tables (draw_glyph, hud_draw_glyph, draw_glyph_now). */
#define CHAR_FIRST_GLYPH ' '
#define CHAR_EXTENDED 0x80
#define CHAR_CODE_MASK 0xff

/* An extended glyph table entry: the sprite (of the font's UV records) a code from
 * CHAR_EXTENDED draws and its advance width (text.c g_extended_glyphs, hud.c g_glyphs_high). */
typedef struct ExtendedGlyph {
    short sprite;
    u_short width;
} ExtendedGlyph;
/* draw_glyph_now's codes below ' ' are the button icons; message text writes them as '!' and a
 * digit ("Press !0 button to Continue..."). name invented */
#define CHAR_BUTTON_ICONS 0x14
/* Menu font icon codes ("ÿ", "þ", "ý" in the menu strings). names: inferred from
 * "Press ÿ to delete block" (the Cross button) and "Use þ and ý to select block",
 * whose entry changes with left and right */
#define CHAR_ICON_RIGHT 0xfd
#define CHAR_ICON_LEFT 0xfe
#define CHAR_ICON_CROSS 0xff
/* The menu font sprites draw_glyph draws the icons with; sprites from MENU_GLYPH_ICONS on
 * are drawn grey. names: as the icon codes */
#define MENU_GLYPH_CROSS 0x77
#define MENU_GLYPH_LEFT 0x7b
#define MENU_GLYPH_RIGHT 0x7d
#define MENU_GLYPH_ICONS MENU_GLYPH_CROSS
/* The frame sprite of the results' percentage bars (draw_percentage_bar). name invented */
#define MENU_GLYPH_BAR_FRAME 0x76

/* The menu font's sprite count (FONT1GFX.BND). name invented */
#define MENU_FONT_GLYPHS 128
/* Menu font metrics: draw_text's 16-pixel lines of 16-pixel glyphs, draw_text_multiline's
 * 10-pixel lines of 12-pixel glyphs, and the width of a space. names invented */
#define MENU_LINE_HEIGHT 16
#define MENU_GLYPH_HEIGHT 16
#define MENU_LINE_HEIGHT_SMALL 10
#define MENU_GLYPH_HEIGHT_SMALL 12
#define MENU_SPACE_WIDTH 8
/* The grey intensity draw_glyph draws the icon sprites in. name invented */
#define MENU_ICON_INTENSITY 200

/* name: inferred from use: font_load loads the font's texture records into it */
extern SpriteUV g_menu_glyph_uvs[MENU_FONT_GLYPHS];

extern void draw_text(const char *text, int x, short y, const u_char *color);
extern void draw_text_multiline(const char *text, int x, short y, const u_char *color);
extern short text_width(const char *text);
extern void draw_text_centered(const char *text, int x, int y, const u_char *color);
extern short parse_decimal(const u_char *text);
extern u_int read_be32(const u_char *bytes);

/* Mission text buffer: frontend copies the level's briefing into it (load_mission_text),
 * the pause menu lays it out. name: runtime evidence, analysis/runtime/globals.json */
#define MISSION_TEXT_SIZE 400
extern char g_mission_text[MISSION_TEXT_SIZE];

/* src/pause_menu.c: draws a string in the HUD font (hud_draw_glyph, include/HUD.H) */
extern void hud_draw_text(const u_char *text, short x, short y, u_char color);

/* src/text.c: loads the menu font */
extern void font_load(void);

/* hud_draw_text colours: the green intensity of the HUD font. names: invented */
#define TEXT_WHITE  0xff /* the newest typed glyph, the demo caption */
#define TEXT_BRIGHT 0xc8 /* the highlighted option */
#define TEXT_NORMAL 0x80
#define TEXT_DIM    0x40
#define TEXT_FAINT  0x30 /* typed option labels */
#define TEXT_DARK   0x18 /* an option not available */

#endif
