#include <sys/types.h>
#include <STRINGS.H>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LEVEL.H>
#include <CAMERA.H>
#include <TASK.H>
#include <MODEL.H>
#include <DISPLAY.H>
#include <HEAP.H>
#include <GAME.H>
#include <SPRITEUV.H>
#include <TEXTURE.H>
#include <TEXT.H>
#include <HUD.H>
#include <ITEMS.H>
#include <ACTOR.H>
#include <VIEW.H>
#include <AUDIO.H>
/* Glyph counts of the two HUD fonts (font_init_widths) and the first glyph of the button
 * icons that draw_glyph_now draws for codes below ' '. names invented */
#define HUD_FONT_GLYPHS 0x5b
#define HUD_FONT2_GLYPHS 0x3b
#define HUD_FONT_ICON_FIRST 0x48
/* Motion tracker: the blips' view depth and the range of actors it shows (hud_draw_tracker). names invented */
#define TRACKER_DEPTH 0x100
#define TRACKER_RANGE 0x3000
/* Screen positions of the ammunition and health counters and of the message lines. names invented */
#define HUD_AMMO_X 18
#define HUD_AMMO_Y 217
#define HUD_HEALTH_X 221
#define HUD_HEALTH_Y 22
#define HUD_MESSAGE_X 16
#define HUD_MESSAGE_Y 58
/* Words of a texture record (SpriteUV, GlyphUV) as copied into a POLY_FT4. name invented */
#define UV_WORDS 4
/* The counters: values above HUD_NUMBER_MAX show as HUD_NUMBER_MAX; HUD font glyph of '0';
 * glyph heights of the HUD font and the large HUD font. names invented */
#define HUD_NUMBER_LIMIT 1000
#define HUD_NUMBER_MAX (HUD_NUMBER_LIMIT - 1)
#define HUD_GLYPH_DIGIT_ZERO ('0' - CHAR_FIRST_GLYPH)
#define HUD_FONT_HEIGHT 9
#define HUD_FONT2_HEIGHT 10
/* draw_glyph_now: the button icons are HUD_ICON_SIZE square and sit HUD_ICON_RAISE above the
 * line; the drop shadow is HUD_SHADOW_HEIGHT high. The width of a space. names invented */
#define HUD_ICON_SIZE 12
#define HUD_ICON_RAISE 2
#define HUD_SHADOW_HEIGHT 11
#define HUD_SPACE_WIDTH 8
/* Sprites of the HUD panel set (g_hud_panel_uvs). names invented, from the code drawing each */
enum {
    HUD_SPRITE_LOWER_PANEL = 0,
    HUD_SPRITE_UPPER_PANEL = 1,
    HUD_SPRITE_CHARGE_GRENADES = 2,
    HUD_SPRITE_ARMOUR = 3,
    HUD_SPRITE_INVULNERABILITY = 4,
    HUD_SPRITE_PULSE_GRENADES = 28,
    HUD_SPRITE_BOOTS = 29,
    HUD_SPRITE_TRACKER_EDGE = 30,
    HUD_SPRITE_INDICATOR = 31,
    HUD_SPRITE_TRACKER_SWEEP = 32 /* TRACKER_FRAMES sweep frames follow */
};
/* Screen rectangles of the lower (ammunition) and upper (health) panels; the item indicators
 * share the upper panel's left and right. names invented */
#define HUD_LOWER_PANEL_LEFT 14
#define HUD_LOWER_PANEL_TOP 211
#define HUD_LOWER_PANEL_RIGHT 144
#define HUD_LOWER_PANEL_BOTTOM 229
#define HUD_PANEL_LEFT 215
#define HUD_PANEL_RIGHT 313
#define HUD_UPPER_PANEL_TOP 19
#define HUD_UPPER_PANEL_BOTTOM 53
/* Item indicators (hud_draw_grenades, hud_draw_armour, hud_draw_boots, hud_draw_invulnerability):
 * rows from HUD_INDICATOR_TOP, HUD_INDICATOR_SPACING apart, each HUD_INDICATOR_HEIGHT high with its
 * count and icon HUD_INDICATOR_INSET below the row top; the icons' right edges. names invented */
#define HUD_INDICATOR_TOP 55
#define HUD_INDICATOR_SPACING 15
#define HUD_INDICATOR_SECOND (HUD_INDICATOR_TOP + HUD_INDICATOR_SPACING)
#define HUD_INDICATOR_HEIGHT 18
#define HUD_INDICATOR_INSET 2
#define HUD_INDICATOR_NUMBER_X 281
#define HUD_INDICATOR_ICON_X 221
#define HUD_INDICATOR_ICON_BOTTOM 11
#define HUD_ICON_BOOTS_BOTTOM 10
#define HUD_ICON_PULSE_GRENADES_RIGHT 271
#define HUD_ICON_CHARGE_GRENADES_RIGHT 265
#define HUD_ICON_ARMOUR_RIGHT 271
#define HUD_ICON_BOOTS_RIGHT 257
#define HUD_ICON_INVULNERABILITY_RIGHT 259
/* The boots and invulnerability timers count ticks; the indicators show seconds (TICKS_PER_SECOND). */
/* Motion tracker: the sweep's TRACKER_FRAMES frames, the pause after a full sweep, the blip
 * scale (world units >> TRACKER_BLIP_SHIFT) and size, the blip count from which the ping is
 * loudest; the four sweep quarters around the centre and the frame bars above and below.
 * names invented */
#define TRACKER_FRAMES 8
#define TRACKER_FRAME_LAST (TRACKER_FRAMES - 1)
#define TRACKER_PAUSE 32
#define TRACKER_BLIP_SHIFT 9
#define TRACKER_BLIP_SIZE 2
#define TRACKER_PING_MANY_BLIPS 3
#define TRACKER_PING_VOLUME_ONE (SPU_VOLUME_MAX / 8)
#define TRACKER_PING_VOLUME_TWO (SPU_VOLUME_MAX / 4)
#define TRACKER_PING_VOLUME_MANY (SPU_VOLUME_MAX / 2)
#define TRACKER_PING_VOLUME_EMPTY (SPU_VOLUME_MAX / 2)
#define TRACKER_LEFT 224
#define TRACKER_CENTRE_X 264
#define TRACKER_RIGHT 304
#define TRACKER_TOP 160
#define TRACKER_CENTRE_Y 192
#define TRACKER_BOTTOM 224
#define TRACKER_FRAME_LEFT 223
#define TRACKER_FRAME_RIGHT 305
#define TRACKER_FRAME_TOP 155
#define TRACKER_FRAME_BOTTOM 229
/* Health colour bands (hud_draw_health): red and flashing (every 1 << HUD_BLINK_SHIFT frames)
 * up to HUD_HEALTH_LOW, amber up to HUD_HEALTH_MEDIUM; the bar is full at PLAYER_HEALTH_MAX,
 * and health above it, up to PLAYER_HEALTH_ADRENALINE, shows as one extra bar per
 * HUD_EXTRA_HEALTH_STEP (hud_draw_extra_health). names invented */
#define HUD_HEALTH_LOW 30
#define HUD_HEALTH_MEDIUM 75
#define HUD_EXTRA_HEALTH_STEP 10
#define HUD_BLINK_SHIFT 3
/* The health bar: HUD_HEALTH_BAR_SCALE pixels per PLAYER_HEALTH_MAX points, drawn
 * HUD_HEALTH_BAR_FULL_WIDTH wide at full health. names invented */
#define HUD_HEALTH_BAR_LEFT 225
#define HUD_HEALTH_BAR_TOP 22
#define HUD_HEALTH_BAR_RIGHT 303
#define HUD_HEALTH_BAR_BOTTOM 48
#define HUD_HEALTH_BAR_FULL_WIDTH 78
#define HUD_HEALTH_BAR_SCALE 80
/* The extra health bars. names invented */
#define HUD_EXTRA_HEALTH_X 265
#define HUD_EXTRA_HEALTH_RIGHT 267
#define HUD_EXTRA_HEALTH_BOTTOM 47
#define HUD_EXTRA_HEALTH_SPACING 4
/* The ammunition bar: HUD_AMMO_BAR_UNITS units of HUD_AMMO_BAR_UNIT_WIDTH pixels, red up to
 * HUD_AMMO_BAR_LOW_WIDTH. names invented */
#define HUD_AMMO_BAR_LEFT 48
#define HUD_AMMO_BAR_TOP 217
#define HUD_AMMO_BAR_RIGHT 136
#define HUD_AMMO_BAR_BOTTOM 224
#define HUD_AMMO_BAR_UNITS 22
#define HUD_AMMO_BAR_UNIT_WIDTH 4
#define HUD_AMMO_BAR_LOW_WIDTH 20
/* The bright colours double the palette's, saturating at HUD_COLOR_MAX from HUD_COLOR_SATURATE.
 * names invented */
#define HUD_COLOR_SATURATE 128
#define HUD_COLOR_MAX 255
/* The debug display's heap usage bars (hud_draw_usage_bars). names invented */
#define USAGE_BAR_X 16
#define USAGE_BAR_Y 16
#define USAGE_BAR_WIDTH 288
#define USAGE_BAR_HEIGHT 4
#define USAGE_BAR_SPACING 8
#define USAGE_BAR_INTENSITY 128
/* message_task: a line stays MESSAGE_HOLD_TICKS after it is typed, fading from
 * MESSAGE_GREEN; the cursor blinks on bit MESSAGE_CURSOR_BLINK of the progress. names invented */
#define MESSAGE_HOLD_TICKS 40
#define MESSAGE_GREEN 180
#define MESSAGE_CURSOR_BLINK 4
#define HUD_MESSAGE_LINES 4
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed short s16;


/* Texture words of one font glyph, copied whole into a POLY_FT4. */
typedef union GlyphUV {
    u_long word[UV_WORDS];
    SpriteUV uv;
} GlyphUV;

typedef struct MessageLine {
    s16 x;
    s16 y;
    s16 length;
    s16 progress;
    const char *text;
} MessageLine;

static TASK *g_message_task = 0;
static u16 g_message_count = 0;
/* invented names: the motion tracker's sweep frame (0..7), the frame delay between sweep
 * steps and the pause after a full sweep (hud_draw_tracker) */
static short g_tracker_frame = TRACKER_FRAME_LAST;
static short g_tracker_delay = 0;
static short g_tracker_pause = 0;
/* invented name: the motion tracker's rotation; tracker_draw_blips sets its roll to the negated view yaw */
static SVECTOR g_tracker_rotation = {0, 0, 0, 0};

extern GlyphUV g_font_uvs[];
extern GlyphUV g_font2_uvs[];
extern u_long g_hud_panel_uvs[][UV_WORDS];
extern u_short g_font_widths[];
extern u_short g_font2_widths[];
/* The four on-screen message lines (hud_show_message); hud_messages_reset sets x to HUD_MESSAGE_X. */
static MessageLine g_message_lines[HUD_MESSAGE_LINES] = {
    { 0, HUD_MESSAGE_Y, 0, 0, 0 }, { 0, HUD_MESSAGE_Y, 0, 0, 0 }, { 0, HUD_MESSAGE_Y, 0, 0, 0 }, { 0, HUD_MESSAGE_Y, 0, 0, 0 }
};
/* Panel glyph and width of each character drawn from the panel page (draw_glyph_now, hud_draw_glyph). */
static ExtendedGlyph g_glyphs_high[] = {
    { 275, 9 }, { 270, 7 }, { 259, 7 }, { 252, 7 }, { 253, 7 }, { 255, 7 }, { 254, 7 },
    { 260, 7 }, { 256, 7 }, { 257, 7 }, { 258, 7 }, { 261, 4 }, { 262, 4 }, { 263, 3 },
    { 276, 10 }, { 277, 10 }, { 278, 8 }, { 282, 12 }, { 286, 14 }, { 265, 7 }, { 266, 7 },
    { 267, 7 }, { 269, 7 }, { 271, 7 }, { 273, 8 }, { 279, 9 }, { 280, 9 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 288, 7 }, { 264, 4 }, { 268, 7 },
    { 272, 7 }, { 274, 7 }, { 281, 8 }, { 0, 7 }, { 0, 7 }, { 284, 8 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 283, 2 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 },
    { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 287, 7 }
};
static u_short g_extra_health_heights[] = { 1, 3, 5, 7, 8, 8, 8, 8, 8, 8 };
/* invented name: the matrix tracker_draw_blips builds from g_tracker_rotation */
extern MATRIX g_tracker_matrix;

/* name: inferred from its body, which fills the glyph width tables from the font UVs */
void font_init_widths(void)
{
    GlyphUV *src;
    u_short *dst;
    s16 i;
    src = g_font_uvs;
    i = 0;
    dst = g_font_widths;
    do {
        dst[i] = src->uv.u1 - src->uv.u0;
        i++;
        src++;
    } while (i < HUD_FONT_GLYPHS);
    src = g_font2_uvs;
    i = 0;
    dst = g_font2_widths;
    do {
        dst[i] = src->uv.u1 - src->uv.u0;
        i++;
        src++;
    } while (i < HUD_FONT2_GLYPHS);
}

/* Draws value (clamped to 0..999) as three HUD font digits at x, y in the palette colour,
 * red when alert is set. */
/* name: static evidence, analysis/runtime/functions.json */
void hud_draw_digits(short x, short y, int value, short alert)
{
    POLY_FT4 *p;
    u_long *uv;
    short n = value;
    short digit;
    short glyph;
    short width;
    if (n >= HUD_NUMBER_LIMIT)
        n = HUD_NUMBER_MAX;
    else if (n <= 0)
        n = 0;
    digit = n / 100;
    glyph = digit + HUD_GLYPH_DIGIT_ZERO;
    p = g_sprite_workspace;
    width = g_font_widths[glyph];
    uv = g_font_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->y3 = p->y2 = y + HUD_FONT_HEIGHT;
    p->x3 = p->x1 = x + width;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    if (alert) {
        p->r0 = g_palette->colors[0].r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = g_palette->colors[0].r;
        p->g0 = g_palette->colors[0].g;
        p->b0 = 0;
    }
    n -= digit * 100;
    digit = n / 10;
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    x += width;
    glyph = digit + HUD_GLYPH_DIGIT_ZERO;
    width = g_font_widths[glyph];
    uv = g_font_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->y3 = p->y2 = y + HUD_FONT_HEIGHT;
    p->x3 = p->x1 = x + width;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    if (alert) {
        p->r0 = g_palette->colors[0].r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = g_palette->colors[0].r;
        p->g0 = g_palette->colors[0].g;
        p->b0 = 0;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    x += width;
    glyph = n - digit * 10 + HUD_GLYPH_DIGIT_ZERO;
    width = g_font_widths[glyph];
    uv = g_font_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->y3 = p->y2 = y + HUD_FONT_HEIGHT;
    p->x3 = p->x1 = x + width;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    if (alert) {
        p->r0 = g_palette->colors[0].r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = g_palette->colors[0].r;
        p->g0 = g_palette->colors[0].g;
        p->b0 = 0;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

/* Draws value (clamped to 0..999) as three HUD font digits at x, y in rgb. */
void draw_number(short x, short y, int value, u_char *rgb)
{
    POLY_FT4 *p;
    u_long *uv;
    short n = value;
    short digit;
    short glyph;
    short width;
    if (n >= HUD_NUMBER_LIMIT)
        n = HUD_NUMBER_MAX;
    else if (n <= 0)
        n = 0;
    digit = n / 100;
    glyph = digit + HUD_GLYPH_DIGIT_ZERO;
    p = g_sprite_workspace;
    width = g_font_widths[glyph];
    uv = g_font_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->y3 = p->y2 = y + HUD_FONT_HEIGHT;
    p->x3 = p->x1 = x + width;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    *(u_long *)&p->r0 = *(u_long *)rgb;
    setPolyFT4(p);
    n -= digit * 100;
    digit = n / 10;
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    x += width;
    glyph = digit + HUD_GLYPH_DIGIT_ZERO;
    width = g_font_widths[glyph];
    uv = g_font_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->y3 = p->y2 = y + HUD_FONT_HEIGHT;
    p->x3 = p->x1 = x + width;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    *(u_long *)&p->r0 = *(u_long *)rgb;
    setPolyFT4(p);
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    x += width;
    glyph = n - digit * 10 + HUD_GLYPH_DIGIT_ZERO;
    width = g_font_widths[glyph];
    uv = g_font_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->x3 = p->x1 = x + width;
    p->y3 = p->y2 = y + HUD_FONT_HEIGHT;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    *(u_long *)&p->r0 = *(u_long *)rgb;
    setPolyFT4(p);
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

/* Draws value (clamped to 0..999) as three large HUD font digits at x, y in rgb. */
/* name: runtime evidence, analysis/runtime/functions.json */
void draw_number_large(short x, short y, int value, u_char *rgb)
{
    POLY_FT4 *p;
    u_long *uv;
    short n = value;
    short digit;
    short glyph;
    short width;
    if (n >= HUD_NUMBER_LIMIT)
        n = HUD_NUMBER_MAX;
    else if (n <= 0)
        n = 0;
    digit = n / 100;
    glyph = digit + HUD_GLYPH_DIGIT_ZERO;
    p = g_sprite_workspace;
    width = g_font2_widths[glyph];
    uv = g_font2_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->y3 = p->y2 = y + HUD_FONT2_HEIGHT;
    p->x3 = p->x1 = x + width;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    *(u_long *)&p->r0 = *(u_long *)rgb;
    setPolyFT4(p);
    n -= digit * 100;
    digit = n / 10;
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    x += width + 1;
    glyph = digit + HUD_GLYPH_DIGIT_ZERO;
    width = g_font2_widths[glyph];
    uv = g_font2_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->y3 = p->y2 = y + HUD_FONT2_HEIGHT;
    p->x3 = p->x1 = x + width;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    *(u_long *)&p->r0 = *(u_long *)rgb;
    setPolyFT4(p);
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    x += width + 1;
    glyph = n - digit * 10 + HUD_GLYPH_DIGIT_ZERO;
    width = g_font2_widths[glyph];
    uv = g_font2_uvs[glyph].word;
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->x3 = p->x1 = x + width;
    p->y3 = p->y2 = y + HUD_FONT2_HEIGHT;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    *(u_long *)&p->r0 = *(u_long *)rgb;
    setPolyFT4(p);
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

void hud_draw_lower_panel(void)
{
    POLY_FT4 *p = g_sprite_workspace;
    u_long *uv = g_hud_panel_uvs[HUD_SPRITE_LOWER_PANEL];
    CVECTOR *rgb = &g_palette->colors[0];
    p->x2 = p->x0 = HUD_LOWER_PANEL_LEFT;
    p->y1 = p->y0 = HUD_LOWER_PANEL_TOP;
    p->x3 = p->x1 = HUD_LOWER_PANEL_RIGHT;
    p->y3 = p->y2 = HUD_LOWER_PANEL_BOTTOM;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = rgb->r;
        p->g0 = rgb->g;
        p->b0 = rgb->b;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

void hud_draw_upper_panel(void)
{
    POLY_FT4 *p = g_sprite_workspace;
    u_long *uv = g_hud_panel_uvs[HUD_SPRITE_UPPER_PANEL];
    CVECTOR *rgb = &g_palette->colors[0];
    p->x2 = p->x0 = HUD_PANEL_LEFT;
    p->y1 = p->y0 = HUD_UPPER_PANEL_TOP;
    p->x3 = p->x1 = HUD_PANEL_RIGHT;
    p->y3 = p->y2 = HUD_UPPER_PANEL_BOTTOM;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = rgb->r;
        p->g0 = rgb->g;
        p->b0 = rgb->b;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

/* Draws the grenade indicator at row y: a panel, the grenade count and the grenade icon. */
/* name: runtime evidence, analysis/runtime/functions.json */
void hud_draw_grenades(short y)
{
    CVECTOR *rgb = &g_palette[1].colors[0];
    u_char color[3];
    POLY_FT4 *p;
    u_long *uv;
    if (g_player.damage_timer) {
        color[0] = rgb->r;
        color[1] = 0;
        color[2] = 0;
    } else {
        color[0] = rgb->r;
        color[1] = rgb->g;
        color[2] = rgb->b;
        rgb = &g_palette[0].colors[0];
    }
    p = g_sprite_workspace;
    uv = g_hud_panel_uvs[HUD_SPRITE_INDICATOR];
    p->x2 = p->x0 = HUD_PANEL_LEFT;
    p->y1 = p->y0 = y;
    p->x3 = p->x1 = HUD_PANEL_RIGHT;
    p->y3 = p->y2 = y + HUD_INDICATOR_HEIGHT;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = 0;
        p->g0 = rgb->g;
        p->b0 = 0;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    if (g_player.weapon == WEAPON_PULSE_RIFLE) {
        draw_number(HUD_INDICATOR_NUMBER_X, y + HUD_INDICATOR_INSET, g_player.pulse_rifle_grenades, color);
        uv = g_hud_panel_uvs[HUD_SPRITE_PULSE_GRENADES];
        p = g_sprite_workspace;
        p->x2 = p->x0 = HUD_INDICATOR_ICON_X;
        p->y1 = p->y0 = y + HUD_INDICATOR_INSET;
        p->x3 = p->x1 = HUD_ICON_PULSE_GRENADES_RIGHT;
        p->y3 = p->y2 = y + HUD_INDICATOR_ICON_BOTTOM;
    } else {
        draw_number(HUD_INDICATOR_NUMBER_X, y + HUD_INDICATOR_INSET, g_player.charge_grenades, color);
        uv = g_hud_panel_uvs[HUD_SPRITE_CHARGE_GRENADES];
        p = g_sprite_workspace;
        p->x2 = p->x0 = HUD_INDICATOR_ICON_X;
        p->y1 = p->y0 = y + HUD_INDICATOR_INSET;
        p->x3 = p->x1 = HUD_ICON_CHARGE_GRENADES_RIGHT;
        p->y3 = p->y2 = y + HUD_INDICATOR_ICON_BOTTOM;
    }
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = 0;
        p->g0 = rgb->g;
        p->b0 = 0;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

/* Draws the armour indicator at row y: a panel, the armour count and the armour icon. */
/* name: static evidence, analysis/runtime/functions.json */
void hud_draw_armour(short y)
{
    CVECTOR *rgb = &g_palette[1].colors[0];
    u_char color[3];
    POLY_FT4 *p;
    u_long *uv;
    if (g_player.damage_timer) {
        color[0] = rgb->r;
        color[1] = 0;
        color[2] = 0;
    } else {
        color[0] = rgb->r;
        color[1] = rgb->g;
        color[2] = rgb->b;
        rgb = &g_palette[0].colors[0];
    }
    p = g_sprite_workspace;
    uv = g_hud_panel_uvs[HUD_SPRITE_INDICATOR];
    p->x2 = p->x0 = HUD_PANEL_LEFT;
    p->y1 = p->y0 = y;
    p->x3 = p->x1 = HUD_PANEL_RIGHT;
    p->y3 = p->y2 = y + HUD_INDICATOR_HEIGHT;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = 0;
        p->g0 = rgb->g;
        p->b0 = 0;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    draw_number(HUD_INDICATOR_NUMBER_X, y + HUD_INDICATOR_INSET, g_player.armour, color);
    uv = g_hud_panel_uvs[HUD_SPRITE_ARMOUR];
    p = g_sprite_workspace;
    p->x2 = p->x0 = HUD_INDICATOR_ICON_X;
    p->y1 = p->y0 = y + HUD_INDICATOR_INSET;
    p->x3 = p->x1 = HUD_ICON_ARMOUR_RIGHT;
    p->y3 = p->y2 = y + HUD_INDICATOR_ICON_BOTTOM;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = 0;
        p->g0 = rgb->g;
        p->b0 = 0;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

/* Draws the boots indicator at row y: a panel, the seconds left and the boots icon. */
/* name: static evidence, analysis/runtime/functions.json */
void hud_draw_boots(short y)
{
    CVECTOR *rgb = &g_palette[1].colors[0];
    u_char color[3];
    POLY_FT4 *p;
    u_long *uv;
    short seconds;
    if (g_player.damage_timer) {
        color[0] = rgb->r;
        color[1] = 0;
        color[2] = 0;
    } else {
        color[0] = rgb->r;
        color[1] = rgb->g;
        color[2] = rgb->b;
        rgb = &g_palette[0].colors[0];
    }
    p = g_sprite_workspace;
    uv = g_hud_panel_uvs[HUD_SPRITE_INDICATOR];
    p->x2 = p->x0 = HUD_PANEL_LEFT;
    p->y1 = p->y0 = y;
    p->x3 = p->x1 = HUD_PANEL_RIGHT;
    p->y3 = p->y2 = y + HUD_INDICATOR_HEIGHT;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = 0;
        p->g0 = rgb->g;
        p->b0 = 0;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    seconds = g_player.boots_timer;
    if (seconds) {
        seconds /= TICKS_PER_SECOND;
        if (seconds == 0)
            seconds++;
    }
    draw_number(HUD_INDICATOR_NUMBER_X, y + HUD_INDICATOR_INSET, seconds, color);
    uv = g_hud_panel_uvs[HUD_SPRITE_BOOTS];
    p = g_sprite_workspace;
    p->x2 = p->x0 = HUD_INDICATOR_ICON_X;
    p->y1 = p->y0 = y + HUD_INDICATOR_INSET;
    p->x3 = p->x1 = HUD_ICON_BOOTS_RIGHT;
    p->y3 = p->y2 = y + HUD_ICON_BOOTS_BOTTOM;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = 0;
        p->g0 = rgb->g;
        p->b0 = 0;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

/* Draws the invulnerability indicator at row y: a panel, the seconds left and its icon. */
/* name: static evidence, analysis/runtime/functions.json */
void hud_draw_invulnerability(short y)
{
    CVECTOR *rgb = &g_palette[1].colors[0];
    u_char color[3];
    POLY_FT4 *p;
    u_long *uv;
    short seconds;
    color[0] = rgb->r;
    color[1] = rgb->g;
    color[2] = rgb->b;
    rgb = &g_palette[0].colors[0];
    p = g_sprite_workspace;
    uv = g_hud_panel_uvs[HUD_SPRITE_INDICATOR];
    p->x2 = p->x0 = HUD_PANEL_LEFT;
    p->y1 = p->y0 = y;
    p->x3 = p->x1 = HUD_PANEL_RIGHT;
    p->y3 = p->y2 = y + HUD_INDICATOR_HEIGHT;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    p->r0 = 0;
    p->g0 = rgb->g;
    p->b0 = 0;
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    seconds = g_player.invulnerable_timer;
    if (seconds) {
        seconds /= TICKS_PER_SECOND;
        if (seconds == 0)
            seconds++;
    }
    draw_number(HUD_INDICATOR_NUMBER_X, y + HUD_INDICATOR_INSET, seconds, color);
    uv = g_hud_panel_uvs[HUD_SPRITE_INVULNERABILITY];
    p = g_sprite_workspace;
    p->x2 = p->x0 = HUD_INDICATOR_ICON_X;
    p->y1 = p->y0 = y + HUD_INDICATOR_INSET;
    p->x3 = p->x1 = HUD_ICON_INVULNERABILITY_RIGHT;
    p->y3 = p->y2 = y + HUD_INDICATOR_ICON_BOTTOM;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    p->r0 = 0;
    p->g0 = rgb->g;
    p->b0 = 0;
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

/* Motion tracker: projects every moving live actor near the player onto the tracker
 * display as a 2x2 tile; returns the number of blips drawn. */
/* name: runtime evidence, analysis/runtime/functions.json */
short tracker_draw_blips(void)
{
    CVECTOR *rgb = &g_palette[1].colors[0];
    Actor *actor;
    TILE *p;
    short count = 0;
    VECTOR distance;
    VECTOR delta;
    SVECTOR blip;
    SetGeomOffset(TRACKER_CENTRE_X, TRACKER_CENTRE_Y);
    PushMatrix();
    g_translation.vx = 0;
    g_translation.vy = 0;
    g_translation.vz = TRACKER_DEPTH;
    g_tracker_rotation.vz = -g_view_rotation.vy & (ONE - 1);
    RotMatrix(&g_tracker_rotation, &g_tracker_matrix);
    TransMatrix(&g_tracker_matrix, &g_translation);
    SetRotMatrix(&g_tracker_matrix);
    SetTransMatrix(&g_tracker_matrix);
    for (actor = g_live_actors; actor != 0; actor = actor->next) {
        if (actor->kind == ACTOR_KIND_COLONIST && actor->action == COLONIST_EMPTY)
            continue;
        if (actor->action >= ACTOR_DYING || actor->kind >= ACTOR_KIND_INERT)
            continue;
        if (actor->kind != ACTOR_KIND_COLONIST && *(long *)&actor->velocity == 0 && actor->velocity.vz == 0)
            continue;
        delta.vx = g_player.position.vx - actor->position.vx;
        delta.vz = g_player.position.vz - actor->position.vz;
        distance.vx = delta.vx < 0 ? -delta.vx : delta.vx;
        distance.vz = delta.vz < 0 ? -delta.vz : delta.vz;
        if (distance.vx > TRACKER_RANGE || distance.vz > TRACKER_RANGE)
            continue;
        blip.vx = delta.vx >> TRACKER_BLIP_SHIFT;
        blip.vy = delta.vz >> TRACKER_BLIP_SHIFT;
        blip.vz = 0;
        p = g_sprite_workspace;
        RotTransPers(&blip, (long *)&p->x0, &g_gte_interpolation, &g_gte_flag);
        setTile(p);
        if (g_player.damage_timer) {
            p->r0 = rgb->r;
            p->g0 = 0;
            p->b0 = 0;
        } else {
            p->r0 = rgb->r;
            p->g0 = rgb->g;
            p->b0 = rgb->b;
        }
        setWH(p, TRACKER_BLIP_SIZE, TRACKER_BLIP_SIZE);
        addPrim(g_render_buffer, g_sprite_workspace);
        p++;
        g_sprite_workspace = p;
        count++;
    }
    SetGeomOffset(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    PopMatrix();
    return count;
}

/* Draws the motion tracker: its frame, the sweep (g_tracker_frame) as four mirrored
 * quarters, the actor blips (tracker_draw_blips), and pings when a sweep starts. */
/* name: runtime evidence, analysis/runtime/functions.json */
void hud_draw_tracker(void)
{
    CVECTOR *rgb;
    short ping = 0;
    short count;
    POLY_FT4 *p;
    u_long *uv;
    u_char t;
    if (g_player.damage_timer)
        rgb = &g_palette[1].colors[0];
    else
        rgb = &g_palette[0].colors[0];
    if (g_tracker_pause == 0) {
        if (g_tracker_delay == 0) {
            g_tracker_delay = 1;
            g_tracker_frame = (g_tracker_frame + 1) & TRACKER_FRAME_LAST;
            if (g_tracker_frame == 0) {
                g_tracker_pause = TRACKER_PAUSE;
                ping = -1;
            }
        } else {
            g_tracker_delay--;
        }
    } else {
        g_tracker_pause--;
    }
    count = tracker_draw_blips();
    if (ping) {
        if (count > 0) {
            short volume = TRACKER_PING_VOLUME_ONE;
            if (count >= 2)
                volume = TRACKER_PING_VOLUME_TWO;
            if (count >= TRACKER_PING_MANY_BLIPS)
                volume = TRACKER_PING_VOLUME_MANY;
            sound_play(SOUND_TRACKER_CONTACT, -1, volume);
        } else {
            sound_play(SOUND_TRACKER_PING, -1, TRACKER_PING_VOLUME_EMPTY);
        }
    }

    p = g_sprite_workspace;
    uv = g_hud_panel_uvs[HUD_SPRITE_TRACKER_EDGE];
    p->x2 = p->x0 = TRACKER_FRAME_LEFT;
    p->y1 = p->y0 = TRACKER_FRAME_TOP;
    p->x3 = p->x1 = TRACKER_FRAME_RIGHT;
    p->y3 = p->y2 = TRACKER_TOP;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    t = p->v0;
    p->v0 = p->v1 = p->v2;
    p->v2 = p->v3 = t;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = rgb->r;
        p->g0 = rgb->g;
        p->b0 = rgb->b;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;

    uv = g_hud_panel_uvs[HUD_SPRITE_TRACKER_EDGE];
    p->x2 = p->x0 = TRACKER_FRAME_LEFT;
    p->y1 = p->y0 = TRACKER_BOTTOM;
    p->x3 = p->x1 = TRACKER_FRAME_RIGHT;
    p->y3 = p->y2 = TRACKER_FRAME_BOTTOM;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = rgb->r;
        p->g0 = rgb->g;
        p->b0 = rgb->b;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;

    uv = g_hud_panel_uvs[HUD_SPRITE_TRACKER_SWEEP + g_tracker_frame];
    p->x2 = p->x0 = TRACKER_LEFT;
    p->y1 = p->y0 = TRACKER_TOP;
    p->x3 = p->x1 = TRACKER_CENTRE_X;
    p->y3 = p->y2 = TRACKER_CENTRE_Y;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = rgb->r;
        p->g0 = rgb->g;
        p->b0 = rgb->b;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;

    uv = g_hud_panel_uvs[HUD_SPRITE_TRACKER_SWEEP + g_tracker_frame];
    p->x2 = p->x0 = TRACKER_CENTRE_X;
    p->y1 = p->y0 = TRACKER_TOP;
    p->x3 = p->x1 = TRACKER_RIGHT;
    p->y3 = p->y2 = TRACKER_CENTRE_Y;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    p->u1--;
    p->u3--;
    t = p->u0;
    p->u0 = p->u2 = p->u1;
    p->u1 = p->u3 = t;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = rgb->r;
        p->g0 = rgb->g;
        p->b0 = rgb->b;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;

    uv = g_hud_panel_uvs[HUD_SPRITE_TRACKER_SWEEP + g_tracker_frame];
    p->x2 = p->x0 = TRACKER_LEFT;
    p->y1 = p->y0 = TRACKER_CENTRE_Y;
    p->x3 = p->x1 = TRACKER_CENTRE_X;
    p->y3 = p->y2 = TRACKER_BOTTOM;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    p->v2--;
    p->v3--;
    t = p->v0;
    p->v0 = p->v1 = p->v2;
    p->v2 = p->v3 = t;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = rgb->r;
        p->g0 = rgb->g;
        p->b0 = rgb->b;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;

    uv = g_hud_panel_uvs[HUD_SPRITE_TRACKER_SWEEP + g_tracker_frame];
    p->x2 = p->x0 = TRACKER_CENTRE_X;
    p->y1 = p->y0 = TRACKER_CENTRE_Y;
    p->x3 = p->x1 = TRACKER_RIGHT;
    p->y3 = p->y2 = TRACKER_BOTTOM;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    p->u1--;
    p->u3--;
    p->v2--;
    p->v3--;
    t = p->u0;
    p->u0 = p->u2 = p->u1;
    p->u1 = p->u3 = t;
    t = p->v0;
    p->v0 = p->v1 = p->v2;
    p->v2 = p->v3 = t;
    setPolyFT4(p);
    setSemiTrans(p, 1);
    if (g_player.damage_timer) {
        p->r0 = rgb->r;
        p->g0 = 0;
        p->b0 = 0;
    } else {
        p->r0 = rgb->r;
        p->g0 = rgb->g;
        p->b0 = rgb->b;
    }
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
}

void hud_draw_extra_health(void)
{
    short clamped;
    short count;
    short i;
    short x;
    POLY_F4 *p;
    u_int green;
    u_int blue;
    if (g_player.health <= PLAYER_HEALTH_MAX)
        return;
    green = g_palette->colors[0].g;
    blue = g_palette->colors[0].b;
    green >>= 1;
    clamped = g_player.health;
    if (g_player.health > PLAYER_HEALTH_ADRENALINE)
        clamped = PLAYER_HEALTH_ADRENALINE;
    count = (short)(clamped - PLAYER_HEALTH_MAX) / HUD_EXTRA_HEALTH_STEP;
    if (count == 0)
        count = 1;
    x = 0;
    for (i = 0; i < count; i++) {
        p = g_sprite_workspace;
        p->x2 = p->x0 = x + HUD_EXTRA_HEALTH_X;
        p->y1 = p->y0 = HUD_EXTRA_HEALTH_BOTTOM - g_extra_health_heights[i];
        setRGB0(p, 0, green, blue);
        setPolyF4(p);
        p->x3 = p->x1 = x + HUD_EXTRA_HEALTH_RIGHT;
        p->y3 = p->y2 = HUD_EXTRA_HEALTH_BOTTOM;
        addPrim(g_render_buffer, g_sprite_workspace);
        p++;
        g_sprite_workspace = p;
        x += HUD_EXTRA_HEALTH_SPACING;
    }
}

/* Draws the ammunition counter and bar in the lower HUD panel, then the item indicators. */
/* name: runtime evidence, analysis/runtime/functions.json */
void hud_draw_ammo(void)
{
    int red = g_palette->colors[0].r;
    int green = g_palette->colors[0].g;
    int blue = g_palette->colors[0].b;
    int bright_red = red * 2;
    int bright_green;
    u_char color[3];
    POLY_F4 *p;
    short ammo;
    short width;
    short y;
    int grenades;
    if (red >= HUD_COLOR_SATURATE)
        bright_red = HUD_COLOR_MAX;
    bright_green = green * 2;
    if (green >= HUD_COLOR_SATURATE)
        bright_green = HUD_COLOR_MAX;
    switch (g_player.weapon) {
    case WEAPON_PISTOL:
        ammo = g_player.pistol_ammo + g_player.pistol_clips * PISTOL_CLIP_ROUNDS;
        break;
    case WEAPON_PULSE_RIFLE:
        ammo = g_player.pulse_rifle_ammo + g_player.pulse_rifle_clips * PULSE_RIFLE_CLIP_ROUNDS;
        break;
    case WEAPON_SHOTGUN:
        ammo = g_player.shotgun_ammo;
        break;
    case WEAPON_FLAMETHROWER:
        ammo = g_player.flamethrower_fuel;
        break;
    case WEAPON_SMART_GUN:
        ammo = g_player.smart_gun_ammo + g_player.smart_gun_clips * SMART_GUN_CLIP_ROUNDS;
        break;
    default:
        ammo = 0;
        break;
    }
    if (g_player.damage_timer) {
        color[0] = bright_red;
        color[1] = 0;
        color[2] = 0;
    } else {
        color[0] = red;
        color[1] = green;
        color[2] = blue;
    }
    draw_number(HUD_AMMO_X, HUD_AMMO_Y, ammo, color);
    hud_draw_lower_panel();
    if (ammo > HUD_AMMO_BAR_UNITS)
        ammo = HUD_AMMO_BAR_UNITS;
    width = ammo * HUD_AMMO_BAR_UNIT_WIDTH;
    if (width) {
        p = g_sprite_workspace;
        p->x2 = p->x0 = HUD_AMMO_BAR_LEFT;
        p->y1 = p->y0 = HUD_AMMO_BAR_TOP;
        p->x3 = p->x1 = width + HUD_AMMO_BAR_LEFT;
        p->y3 = p->y2 = HUD_AMMO_BAR_BOTTOM;
        if (g_player.damage_timer || width <= HUD_AMMO_BAR_LOW_WIDTH)
            setRGB0(p, bright_red, 0, 0);
        else
            setRGB0(p, 0, bright_green, 0);
        setPolyF4(p);
        addPrim(g_render_buffer, g_sprite_workspace);
        p++;
        g_sprite_workspace = p;
    }
    p = g_sprite_workspace;
    p->x2 = p->x0 = HUD_AMMO_BAR_LEFT;
    p->y1 = p->y0 = HUD_AMMO_BAR_TOP;
    p->x3 = p->x1 = HUD_AMMO_BAR_RIGHT;
    p->y3 = p->y2 = HUD_AMMO_BAR_BOTTOM;
    if (g_player.damage_timer)
        setRGB0(p, red, 0, 0);
    else
        setRGB0(p, 0, green, 0);
    setPolyF4(p);
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    y = HUD_INDICATOR_TOP;
    if (g_player.weapon == WEAPON_PULSE_RIFLE)
        grenades = g_player.pulse_rifle_grenades;
    else
        grenades = g_player.charge_grenades;
    if (grenades) {
        hud_draw_grenades(HUD_INDICATOR_TOP);
        y = HUD_INDICATOR_SECOND;
    }
    if (g_player.armour) {
        hud_draw_armour(y);
        y += HUD_INDICATOR_SPACING;
    }
    if (g_player.boots_timer) {
        hud_draw_boots(y);
        y += HUD_INDICATOR_SPACING;
    }
    if (g_player.invulnerable_timer)
        hud_draw_invulnerability(y);
    hud_draw_tracker();
}

/* Draws the health counter and bar in the upper HUD panel. */
/* name: runtime evidence, analysis/runtime/functions.json */
void hud_draw_health(void)
{
    int red = g_palette->colors[0].r;
    int green = g_palette->colors[0].g;
    int blue = g_palette->colors[0].b;
    int bright_red = red * 2;
    int bright_green;
    u_char color[3];
    POLY_F4 *p;
    short width;
    if (red >= HUD_COLOR_SATURATE)
        bright_red = HUD_COLOR_MAX;
    bright_green = green * 2;
    if (green >= HUD_COLOR_SATURATE)
        bright_green = HUD_COLOR_MAX;
    if (g_player.damage_timer || g_player.health <= HUD_HEALTH_LOW) {
        color[0] = bright_red;
        color[1] = 0;
        color[2] = 0;
    } else if (g_player.health <= HUD_HEALTH_MEDIUM) {
        color[0] = red;
        color[1] = green;
        color[2] = 0;
    } else {
        color[0] = red;
        color[1] = green;
        color[2] = blue;
    }
    if (g_player.damage_timer)
        draw_number_large(HUD_HEALTH_X, HUD_HEALTH_Y, g_player.health, color);
    else
        draw_number_large(HUD_HEALTH_X, HUD_HEALTH_Y, g_player.health, (u_char *)g_palette);
    hud_draw_extra_health();
    hud_draw_upper_panel();
    if (g_player.health > 0) {
        if (g_player.health >= PLAYER_HEALTH_MAX)
            width = HUD_HEALTH_BAR_FULL_WIDTH;
        else
            width = g_player.health * HUD_HEALTH_BAR_SCALE / PLAYER_HEALTH_MAX;
        p = g_sprite_workspace;
        p->x2 = p->x0 = HUD_HEALTH_BAR_LEFT;
        p->y1 = p->y0 = HUD_HEALTH_BAR_TOP;
        p->x3 = p->x1 = width + HUD_HEALTH_BAR_LEFT;
        p->y3 = p->y2 = HUD_HEALTH_BAR_BOTTOM;
        if (g_player.damage_timer)
            setRGB0(p, bright_red, 0, 0);
        else if (g_player.health <= HUD_HEALTH_LOW) {
            if ((g_frame_count >> HUD_BLINK_SHIFT) & 1)
                setRGB0(p, bright_red, 0, 0);
            else
                setRGB0(p, red, 0, 0);
        } else if (g_player.health <= HUD_HEALTH_MEDIUM)
            setRGB0(p, bright_red, bright_green, 0);
        else
            setRGB0(p, 0, bright_green, 0);
        setPolyF4(p);
        addPrim(g_render_buffer, g_sprite_workspace);
        p++;
        p->x2 = p->x0 = HUD_HEALTH_BAR_LEFT;
        p->y1 = p->y0 = HUD_HEALTH_BAR_TOP;
        p->x3 = p->x1 = HUD_HEALTH_BAR_RIGHT;
        p->y3 = p->y2 = HUD_HEALTH_BAR_BOTTOM;
        g_sprite_workspace = p;
        if (g_player.damage_timer)
            setRGB0(p, red, 0, 0);
        else
            setRGB0(p, 0, green, 0);
        setPolyF4(p);
        addPrim(g_render_buffer, g_sprite_workspace);
        p++;
        g_sprite_workspace = p;
    }
}

void hud_draw_usage_bars(void)
{
    u_long scale = g_heap_size / USAGE_BAR_WIDTH;
    u_long first;
    u_long second;
    short x;
    short y = USAGE_BAR_Y;
    POLY_F4 *p;
    if (scale == 0)
        return;
    first = g_heap_used / scale;
    second = g_heap_free / scale;
    x = USAGE_BAR_X;
    if (first) {
        p = g_sprite_workspace;
        setRGB0(p, USAGE_BAR_INTENSITY, 0, 0);
        setPolyF4(p);
        p->x2 = p->x0 = x;
        p->y1 = p->y0 = y;
        p->x3 = p->x1 = x + first;
        p->y3 = p->y2 = y + USAGE_BAR_HEIGHT;
        addPrim(g_render_buffer, g_sprite_workspace);
        p++;
        g_sprite_workspace = p;
    }
    y += USAGE_BAR_SPACING;
    if (second) {
        p = g_sprite_workspace;
        setRGB0(p, 0, USAGE_BAR_INTENSITY, 0);
        setPolyF4(p);
        p->x2 = p->x0 = x;
        p->y1 = p->y0 = y;
        p->x3 = p->x1 = x + second;
        p->y3 = p->y2 = y + USAGE_BAR_HEIGHT;
        addPrim(g_render_buffer, g_sprite_workspace);
        p++;
        g_sprite_workspace = p;
    }
}

/* name: inferred from its body, which draws one HUD font glyph and returns the next x */
short hud_draw_glyph(u_short ch, short x, short y, u_char *rgb)
{
    POLY_FT4 *p = g_sprite_workspace;
    u_long *uv;
    short right;
    ch &= CHAR_CODE_MASK;
    if ((short)ch >= CHAR_EXTENDED) {
        short index = ch - CHAR_EXTENDED;
        right = g_glyphs_high[index].width + x;
        uv = (u_long *)&g_panel_uvs[g_glyphs_high[index].sprite];
    } else {
        short index = ch - CHAR_FIRST_GLYPH;
        right = g_font_widths[index] + x;
        uv = g_font_uvs[index].word;
    }
    p->x2 = p->x0 = x;
    p->y1 = p->y0 = y;
    p->x3 = p->x1 = right;
    p->y3 = p->y2 = y + HUD_FONT_HEIGHT;
    *(u_long *)&p->u0 = *uv++;
    *(u_long *)&p->u1 = *uv++;
    *(u_long *)&p->u2 = *uv++;
    *(u_long *)&p->u3 = *uv;
    setPolyFT4(p);
    setRGB0(p, rgb[0], rgb[1], rgb[2]);
    setSemiTrans(p, 1);
    addPrim(g_render_buffer, g_sprite_workspace);
    p++;
    g_sprite_workspace = p;
    return right;
}

/* Draws one font glyph immediately (DrawPrim) over a drop shadow; returns the next x. */
/* name: runtime evidence, analysis/runtime/functions.json */
short draw_glyph_now(u_short ch, short x, short y, u_char *rgb, u_char *shadow)
{
    POLY_FT4 prims[2];
    POLY_FT4 *p = prims;
    u_long *uv;
    short right;
    short height = HUD_FONT_HEIGHT;
    short i;
    short index = (ch & CHAR_CODE_MASK) - CHAR_FIRST_GLYPH;
    if ((u_char)ch < CHAR_FIRST_GLYPH) {
        height = HUD_ICON_SIZE;
        right = x + HUD_ICON_SIZE;
        y -= HUD_ICON_RAISE;
        uv = g_font_uvs[(u_char)ch + HUD_FONT_ICON_FIRST].word;
    } else if ((short)(ch & CHAR_CODE_MASK) >= CHAR_EXTENDED) {
        short high = (ch & CHAR_CODE_MASK) - CHAR_EXTENDED;
        right = g_glyphs_high[high].width + x;
        uv = (u_long *)&g_panel_uvs[g_glyphs_high[high].sprite];
    } else {
        right = g_font_widths[index] + x;
        uv = g_font_uvs[index].word;
    }
    for (i = 0; i < 2; i++) {
        if (i == 1) {
            p->x2 = p->x0 = x;
            p->y1 = p->y0 = y;
            p->x3 = p->x1 = right;
            p->y3 = p->y2 = y + height;
        } else {
            p->x2 = p->x0 = x + 1;
            p->y1 = p->y0 = y + 1;
            p->x3 = p->x1 = right + 2;
            p->y3 = p->y2 = y + HUD_SHADOW_HEIGHT;
        }
        *(u_long *)&p->u0 = uv[0];
        *(u_long *)&p->u1 = uv[1];
        *(u_long *)&p->u2 = uv[2];
        *(u_long *)&p->u3 = uv[3];
        setPolyFT4(p);
        if (i == 1) {
            setRGB0(p, rgb[0], rgb[1], rgb[2]);
        } else {
            setRGB0(p, shadow[0], shadow[1], shadow[2]);
        }
        DrawPrim(p);
        p++;
    }
    return right + 1;
}

MessageLine *message_line_active(void)
{
    s16 i;
    for (i = 0; i < HUD_MESSAGE_LINES; i++) {
        if (g_message_lines[i].text != 0)
            return &g_message_lines[i];
    }
    return 0;
}

void message_task(TASK *task)
{
    MessageLine *slot;
    const char *text;
    short x;
    s16 count;
    u8 cursor;
    short y;
    u32 green;
    u8 color[3];
    if (task->state[0] == 0) {
        task->state[0] = (int)message_line_active();
        if (task->state[0] == 0) {
            g_message_task = 0;
            TaskDie(task);
            return;
        }
    }
    slot = (MessageLine *)task->state[0];
    slot->progress++;
    if (slot->progress >= slot->length + MESSAGE_HOLD_TICKS) {
        slot->text = 0;
        task->state[0] = 0;
        g_message_count = g_message_count - 1;
    } else {
        if (g_game_flags & GAME_FLAG_FADE)
            green = g_palette->colors[0].g >> 1;
        else
            green = MESSAGE_GREEN - (slot->progress - slot->length) * 2;
        if (slot->progress > slot->length) {
            count = slot->length;
            cursor = (slot->progress & MESSAGE_CURSOR_BLINK) ? ' ' : '*';
        } else {
            count = slot->progress;
            cursor = '*';
        }
        x = slot->x;
        text = slot->text;
        y = slot->y;
        while (count-- > 0U) {
            u8 c = *text++;
            if (c == ' ') {
                x += HUD_SPACE_WIDTH;
            } else {
                color[0] = 0;
                color[1] = green;
                color[2] = 0;
                x = hud_draw_glyph(c, x, y, color);
            }
        }
        if (cursor != ' ') {
            color[0] = 0;
            color[1] = green;
            color[2] = 0;
            hud_draw_glyph(cursor, x, y, color);
        }
    }
    g_next_task = task->Next;
}

/* name: inferred from its body, which queues a message line and starts message_task */
void hud_show_message(const char *text)
{
    s16 i;
    MessageLine *slot = 0;
    TASK *task;
    for (i = 0; i < HUD_MESSAGE_LINES; ++i) {
        if (g_message_lines[i].text == 0) slot = &g_message_lines[i];
    }
    if (slot == 0) return;
    task = g_message_task;
    if (task == 0) {
        task = TaskCreate(TASK_PRIORITY_OVERLAY, message_task);
        if (task == 0) return;
        g_message_task = task;
        task->state[0] = 0;
    }
    slot->text = text;
    slot->length = strlen(text);
    slot->progress = 0;
    slot->x = HUD_MESSAGE_X;
    g_message_count++;
}

/* name: inferred from its body, which clears every message line and the message task */
void hud_messages_reset(void)
{
    int i;
    for (i = 0; i < HUD_MESSAGE_LINES; i++) {
        g_message_lines[i].text = 0;
        g_message_lines[i].length = 0;
        g_message_lines[i].progress = 0;
        g_message_lines[i].x = HUD_MESSAGE_X;
        g_message_lines[i].y = HUD_MESSAGE_Y;
    }
    g_message_count = 0;
    g_message_task = 0;
}
