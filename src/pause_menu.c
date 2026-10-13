#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LIBETC.H>
#include <LEVEL.H>
#include <CAMERA.H>
#include <ITEMS.H>
#include <DISPLAY.H>
#include <AUDIO.H>
#include <GAME.H>
#include <VIEW.H>
#include <MODEL.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <TEXTURE.H>
#include <TEXT.H>
#include <HUD.H>
#include <MENU.H>

/* Pause menu: the option list typed out and browsed, the item, map, mission and
 * sound pages, the exit confirmation, and the loop that runs them. */

/* g_pause_state. names: inferred from the input each state handles in pause_menu_run */
enum {
    PAUSE_TYPING,       /* the option labels are typed out one by one */
    PAUSE_BROWSING,     /* up/down choose an option, the item page follows it */
    PAUSE_MAP,          /* the auto mapper scrolls the map */
    PAUSE_SOUND,        /* the sound page: SFX, CDDA volume and exit */
    PAUSE_EXIT_CONFIRM  /* "ARE YOU SURE ?" */
};

/* g_pause_option, in the order of g_pause_options' labels. */
enum {
    OPTION_AUTO_MAPPER,
    OPTION_LAMP,        /* "Shoulder Lamp" or "Night Vision" */
    OPTION_PISTOL,
    OPTION_SHOTGUN,
    OPTION_FLAMETHROWER,
    OPTION_PULSE_RIFLE,
    OPTION_SMART_GUN,
    OPTION_BATTERIES,
    OPTION_MISSION,
    OPTION_SOUND,       /* "Options" */
    OPTION_COUNT
};

/* g_sound_option, in the order of g_sound_options' labels. */
enum {
    SOUND_SFX_VOLUME,
    SOUND_CDDA_VOLUME,
    SOUND_EXIT_GAME,
    SOUND_OPTION_COUNT
};

/* Volumes run 0..10; each select steps one up and wraps. */
#define VOLUME_MAX 10

/* A 15-bit VRAM colour from 5-bit components. name: invented */
#define RGB15(r, g, b) (((b) << 10) | ((g) << 5) | (r))

/* Item models spin tilted forward by this pitch. name: invented */
#define ITEM_MODEL_TILT 0x80

/* Screen layout of the pages. names: invented */
#define OPTIONS_X 0x10              /* the option list */
#define PANEL_TOP 0x20              /* first line of the option list and of the right-hand panel */
#define SOUND_OPTIONS_X 0xa2
#define EXIT_ANSWER_X 0x70          /* "Yes"/"No" right of "ARE YOU SURE ?" */
#define OPTION_BAR_WIDTH 0x62       /* the highlight bar behind each option label */
#define OPTION_BAR_HEIGHT 0x12
#define OPTION_BAR_LIT 0x80         /* green of the bar of an available option */
#define OPTION_BAR_UNLIT 0x20
#define STATUS_Y 0xd4               /* "Available", "Selected" or "Not available" under the item */
#define STATUS_X 0xc0
#define STATUS_NOT_AVAILABLE_X 0xb8
#define STATUS_COUNT_X 0xb4         /* a charge count before "Available" */
#define STATUS_COUNT_TEXT_X 0xd4
#define AMMO_NONE_X 0xa0            /* "No ammo available" */
#define AMMO_COUNT_X 0x98           /* the round count before "rounds available" */
#define AMMO_TEXT_X 0xb6
#define MISSION_LEFT 0x80           /* the mission text's box */
#define MISSION_RIGHT 0x139
#define MISSION_BOTTOM 0xd1

/* The auto mapper: one texel per cell in a MAP_TEXTURE_SIZE square texture, drawn centred
 * on the panel, or MAP_ZOOM_SIZE pixels square at four pixels per cell when zoomed. names: invented */
#define MAP_TEXTURE_SIZE 128
#define MAP_PANEL_X 0xe0
#define MAP_PANEL_Y 0x60
#define MAP_ZOOM_X 0xa8             /* centre of the zoomed map's first cell */
#define MAP_ZOOM_Y 0x28
#define MAP_ZOOM_SIZE 0x74
#define MAP_BLINK 0x10              /* g_frame_count bit that blinks the player's blip */

/* More of the auto mapper: the zoomed map shows MAP_ZOOM_RADIUS cells each side of
 * g_map_center_x/_z at MAP_ZOOM_SCALE pixels a cell; the player is a MAP_BLIP_SIZE square
 * blip on the overview and an arrow (tip ahead, two back corners) when zoomed. names: invented */
#define MAP_ZOOM_RADIUS 14
#define MAP_ZOOM_CELLS (2 * MAP_ZOOM_RADIUS + 1)
#define MAP_ZOOM_SCALE 4
#define MAP_BLIP_SIZE 3
#define MAP_ARROW_LENGTH 8
#define MAP_ARROW_HALF_WIDTH 4
#define MAP_ARROW_BACK 4
#define MAP_CLUT_ROW 31             /* g_clut_sets[CLUT_SET_SPRITES] row the map palette is loaded into */
#define MAP_SCROLL_SOUND_TIME 100   /* frames SOUND_MAP_SCROLL plays before it is stopped */
/* Colours: the player's blip and arrow, the map texture's shading, the tile behind the map */
#define MAP_PLAYER_R 192
#define MAP_PLAYER_G 192
#define MAP_PLAYER_B 255
#define MAP_TINT_R 128
#define MAP_TINT_G 192
#define MAP_TINT_B 128
#define MAP_BACKING_G 24

/* Map texels: palette indices of the map texture (automap_build), and their colours. names: invented */
enum {
    AUTOMAP_EMPTY,
    AUTOMAP_FLOOR,      /* a cell seen (or shown by the auto mapper) */
    AUTOMAP_WALL,       /* CELL_BLOCKED, or a cell without a face */
    AUTOMAP_UNUSED_3,   /* identity-TODO: no matched code draws it */
    AUTOMAP_OBJECT,     /* a level object's cell (auto mapper only) */
    AUTOMAP_SPECIAL     /* identity-TODO: cells of type 6 */
};
#define AUTOMAP_FLOOR_COLOR RGB15(8, 10, 8)
#define AUTOMAP_WALL_COLOR RGB15(4, 6, 4)
#define AUTOMAP_UNUSED_3_COLOR RGB15(16, 6, 4)
#define AUTOMAP_OBJECT_COLOR RGB15(4, 6, 16)
#define AUTOMAP_SPECIAL_COLOR RGB15(4, 16, 4)

/* Text metrics: hud_draw_text and draw_text_typed advance HUD_SPACE_WIDTH for a space and
 * HUD_LINE_HEIGHT for a newline; the mission text uses its own. names: invented */
#define HUD_SPACE_WIDTH 4
#define HUD_LINE_HEIGHT 10
#define MISSION_SPACE_WIDTH 6
#define MISSION_LINE_HEIGHT 9
#define MISSION_PARAGRAPH_HEIGHT 18

/* Option lists: line pitch, the g_frame_count shift that blinks the highlighted option, and
 * the sound page's options with their volume bars under them. names: invented */
#define OPTION_SPACING 20
#define OPTION_BLINK_SHIFT 3
#define SOUND_OPTION_SPACING 40
#define SOUND_BAR_OFFSET_Y 20
#define VOLUME_BAR_SIZE 16

/* Item page models (draw_item_page): yaw steps per frame, and where each item's model and
 * its ammunition's model sit (g_translation). names: invented */
#define ITEM_MODEL_SPIN 16
#define AMMO_MODEL_SPIN 8
#define ITEM_VIEW_AUTO_MAPPER_X 256
#define ITEM_VIEW_AUTO_MAPPER_Y 256
#define ITEM_VIEW_AUTO_MAPPER_Z 1024
#define ITEM_VIEW_LAMP_X 96
#define ITEM_VIEW_LAMP_Y 32
#define ITEM_VIEW_LAMP_Z 384
#define ITEM_VIEW_PISTOL_X 128
#define ITEM_VIEW_PISTOL_Y 64
#define ITEM_VIEW_PISTOL_Z 512
#define ITEM_VIEW_SHOTGUN_X 256
#define ITEM_VIEW_SHOTGUN_Y 128
#define ITEM_VIEW_SHOTGUN_Z 1280
#define ITEM_VIEW_FLAMETHROWER_X 256
#define ITEM_VIEW_FLAMETHROWER_Y 128
#define ITEM_VIEW_FLAMETHROWER_Z 1024
#define ITEM_VIEW_PULSE_RIFLE_X 256
#define ITEM_VIEW_PULSE_RIFLE_Y 128
#define ITEM_VIEW_PULSE_RIFLE_Z 1024
#define ITEM_VIEW_SMART_GUN_X 384
#define ITEM_VIEW_SMART_GUN_Y 128
#define ITEM_VIEW_SMART_GUN_Z 1536
#define ITEM_VIEW_BATTERY_X 128
#define ITEM_VIEW_BATTERY_Y 0
#define ITEM_VIEW_BATTERY_Z 512
#define AMMO_VIEW_PISTOL_X 96
#define AMMO_VIEW_PISTOL_Y (-64)
#define AMMO_VIEW_PISTOL_Z 400
#define AMMO_VIEW_SHOTGUN_X 128
#define AMMO_VIEW_SHOTGUN_Y (-80)
#define AMMO_VIEW_SHOTGUN_Z 640
#define AMMO_VIEW_FLAMETHROWER_X 128
#define AMMO_VIEW_FLAMETHROWER_Y (-96)
#define AMMO_VIEW_FLAMETHROWER_Z 640
#define AMMO_VIEW_PULSE_RIFLE_X 160
#define AMMO_VIEW_PULSE_RIFLE_Y (-112)
#define AMMO_VIEW_PULSE_RIFLE_Z 768
#define AMMO_VIEW_SMART_GUN_X 160
#define AMMO_VIEW_SMART_GUN_Y (-112)
#define AMMO_VIEW_SMART_GUN_Z 768

/* Inline codes of the mission text: '!', '$' and '%' carry one parameter byte,
 * '&' a colour digit, '#' and '\n' are skipped, '\r' ends a paragraph. */




typedef struct MenuOption {
    short value;
    const char *label;
} MenuOption;

/* Module statics (.sbss 0x8008d218..0x8008d2b8), in their retail order. */
/* name: inferred from the PAUSE_* states it selects */
static short g_pause_state;
/* name: inferred from indexing g_pause_options; while typing, the labels typed so far */
static short g_pause_option;
/* name: inferred from draw_text_typed, which types g_pause_options' current label */
static short g_typed_length;
/* name: inferred from indexing g_sound_options */
static short g_sound_option;
/* name: inferred from the "Yes"/"No" answer to "ARE YOU SURE ?" */
static short g_exit_confirmed;
/* identity-TODO: a slot read only by unmatched code. */
static int g_8d240;
/* names: inferred from the "SFX VOLUME" and "CDDA VOLUME" bars */
static short g_sfx_volume;
static short g_cdda_volume;
/* names: inferred from the mission text layout (mission_text_draw) */
static short g_text_left;
static short g_text_x;
static short g_text_y;
static short g_8d270; /* identity-TODO: set to 0x40 by the mission text layout, never read here */
static u_char *g_text_cursor;
static u_char g_text_style[3];
/* names: inferred from the map scroll in pause_menu_run and the map texture window */
static short g_map_center_x;
static short g_map_center_z;
/* name: inferred from RotMatrix of the item models in draw_item_page */
static SVECTOR g_model_rotation;
/* name: inferred from RotMatrix of the ammunition models in draw_item_page */
static SVECTOR g_ammo_model_rotation;
/* names: inferred from the three vertices of the map arrow (draw_map_arrow) */
static SVECTOR g_arrow_tip;
static SVECTOR g_arrow_right;
static SVECTOR g_arrow_left;
/* name: inferred from typing g_typed_length characters of an option label */
extern void draw_text_typed(const unsigned char *text, short x, short y);
extern void automap_build(void);
/* Texture words of the option highlight bar.
 * name: inferred from draw_option_bar */
extern SpriteUV g_option_bar_uv;
/* name: inferred from text_word_width, which sums them per character */
extern u_short g_glyph_widths[256];
/* The automap texture words: automap_build fills them for the slot it uploads the map
 * image to, draw_map copies them into its POLY_FT4.
 * name: inferred from use */
extern SpriteUV g_map_uv;
/* name: inferred from RotMatrix/TransMatrix for the item models */
extern MATRIX g_model_matrix;
/* name: inferred from the ammunition counts it draws before "rounds available" */
extern void draw_number(int x, int y, int value, u_char *style);

/* name: inferred from the labels */
MenuOption g_pause_options[OPTION_COUNT] = {
    { 0, "Auto Mapper" },
    { 0, "Shoulder Lamp" },
    { 0, "9mm Pistol" },
    { 0, "Shotgun" },
    { 0, "Flamethrower" },
    { 0, "Pulse Rifle" },
    { 0, "Smart Gun" },
    { 0, "Batteries" },
    { 0, "Mission" },
    { 0, "Options" },
};

/* name: inferred from the labels */
MenuOption g_sound_options[SOUND_OPTION_COUNT] = {
    { -1, "SFX VOLUME" },
    { -1, "CDDA VOLUME" },
    { -1, "EXIT GAME" },
};

/* name: inferred from its body */
/* Builds the automap image in g_unpack_buffer: one pixel per map cell, coloured
 * by what the player has seen, uploaded with its CLUT into a free VRAM slot. */
void automap_build(void)
{
    u_short *palette;
    ClutRow *clut;
    u_char *pixel;
    MapCell *cell;
    TexturePage *slot;
    u_short x;
    u_short y;
    short i;
    RECT rect;

    palette = g_unpack_buffer;
    palette[AUTOMAP_EMPTY] = 0;
    palette[AUTOMAP_FLOOR] = AUTOMAP_FLOOR_COLOR;
    palette[AUTOMAP_WALL] = AUTOMAP_WALL_COLOR;
    palette[AUTOMAP_UNUSED_3] = AUTOMAP_UNUSED_3_COLOR;
    palette[AUTOMAP_OBJECT] = AUTOMAP_OBJECT_COLOR;
    palette[AUTOMAP_SPECIAL] = AUTOMAP_SPECIAL_COLOR;
    clut = &g_clut_sets[CLUT_SET_SPRITES].rows[MAP_CLUT_ROW];
    LoadClut(g_unpack_buffer, clut->x, clut->y);
    DrawSync(0);
    pixel = g_unpack_buffer;
    cell = g_level_map.cells;
    for (y = 0; y < MAP_TEXTURE_SIZE; y++) {
        for (x = 0; x < MAP_TEXTURE_SIZE; x++) {
            if (y >= g_level_map.height || x >= g_level_map.width) {
                *pixel++ = AUTOMAP_EMPTY;
            } else {
                if (g_player.flags & PLAYER_HAS_AUTO_MAPPER) {
                    if (g_secrets_found[cell->secret_index]) {
                        if (cell->marker != 0 && cell->marker >= LEVEL_OBJECT_TARGET && cell->marker < CELL_MARKER_PLAYER)
                            *pixel++ = AUTOMAP_OBJECT;
                        else if (cell->light & CELL_BLOCKED)
                            *pixel++ = AUTOMAP_WALL;
                        else if (cell->type == CELL_TYPE_DOOR)
                            *pixel++ = AUTOMAP_SPECIAL;
                        else if (cell->face != 0 || (cell->light & CELL_VISITED))
                            *pixel++ = AUTOMAP_FLOOR;
                        else
                            *pixel++ = AUTOMAP_EMPTY;
                    } else
                        *pixel++ = AUTOMAP_EMPTY;
                } else {
                    if ((cell->light & CELL_VISITED) && g_secrets_found[cell->secret_index]) {
                        if (cell->face == 0 || (cell->light & CELL_BLOCKED))
                            *pixel++ = AUTOMAP_WALL;
                        else if (cell->type == CELL_TYPE_DOOR)
                            *pixel++ = AUTOMAP_SPECIAL;
                        else
                            *pixel++ = AUTOMAP_FLOOR;
                    } else
                        *pixel++ = AUTOMAP_EMPTY;
                }
                cell++;
            }
        }
    }
    slot = g_page_sets[PAGE_SET_SPRITES].pages;
    for (i = 0; i < g_page_sets[PAGE_SET_SPRITES].count; i++, slot++) {
        if (!(slot->reserved & 1))
            break;
    }
    slot->pixels = 0;
    slot->users = 0;
    g_map_uv.u0 = 0;
    g_map_uv.clut = clut->clut;
    g_map_uv.u1 = g_level_map.width - 1;
    g_map_uv.v0 = slot->y;
    g_map_uv.v1 = g_map_uv.v0;
    g_map_uv.v2 = g_map_uv.v0 + g_level_map.height - 1;
    g_map_uv.v3 = g_map_uv.v2;
    g_map_uv.tpage = slot->tpage;
    g_map_uv.u2 = 0;
    g_map_uv.u3 = g_map_uv.u1;
    DrawSync(0);
    rect.x = slot->x;
    rect.y = slot->y;
    rect.w = MAP_TEXTURE_SIZE / 2;
    rect.h = MAP_TEXTURE_SIZE;
    LoadImage(&rect, g_unpack_buffer);
    DrawSync(0);
}

/* name: inferred from it resetting the menu state and option values before pause_menu_run's loop */
void pause_menu_init(void)
{
    MenuOption *option;
    short i;

    g_model_rotation.vx = 0;
    g_model_rotation.vy = g_view_rotation.vy;
    g_model_rotation.vz = 0;
    g_arrow_tip.vx = 0;
    g_arrow_tip.vy = 0;
    g_arrow_tip.vz = MAP_ARROW_LENGTH;
    g_arrow_right.vx = MAP_ARROW_HALF_WIDTH;
    g_arrow_right.vy = 0;
    g_arrow_right.vz = -MAP_ARROW_BACK;
    g_arrow_left.vx = -MAP_ARROW_HALF_WIDTH;
    g_arrow_left.vy = 0;
    g_arrow_left.vz = -MAP_ARROW_BACK;
    PushMatrix();
    rotate_vector(&g_model_rotation, &g_arrow_tip, &g_arrow_tip);
    rotate_vector(&g_model_rotation, &g_arrow_right, &g_arrow_right);
    rotate_vector(&g_model_rotation, &g_arrow_left, &g_arrow_left);
    PopMatrix();
    automap_build();
    g_pause_state = PAUSE_TYPING;
    g_pause_option = 0;
    g_typed_length = 0;
    g_map_center_x = (g_player.position.vx + g_level_map.offset_x) >> MAP_CELL_SHIFT;
    g_map_center_z = (g_player.position.vz + g_level_map.offset_z) >> MAP_CELL_SHIFT;
    g_sfx_volume = get_sound_volume();
    g_cdda_volume = get_music_volume();
    if (g_sfx_volume < 0)
        g_sfx_volume = 0;
    if (g_sfx_volume > VOLUME_MAX)
        g_sfx_volume = VOLUME_MAX;
    if (g_cdda_volume < 0)
        g_cdda_volume = 0;
    if (g_cdda_volume > VOLUME_MAX)
        g_cdda_volume = VOLUME_MAX;
    g_model_rotation.vx = ITEM_MODEL_TILT;
    g_model_rotation.vy = 0;
    g_model_rotation.vz = ANGLE_180;
    g_ammo_model_rotation.vx = ITEM_MODEL_TILT;
    g_ammo_model_rotation.vy = 0;
    option = g_pause_options;
    for (i = 0; i < OPTION_COUNT; i++, option++) {
        switch (i) {
        case OPTION_AUTO_MAPPER:
            option->value = (u_short)g_player.flags & PLAYER_HAS_AUTO_MAPPER;
            break;
        case OPTION_LAMP:
            if (g_player.lamp_charges) {
                option->label = "Shoulder Lamp";
                option->value = g_player.lamp_timer == 0;
            } else if (g_player.night_vision_charges) {
                option->label = "Night Vision";
                option->value = g_player.night_vision_timer == 0;
            } else {
                option->label = g_level < LEVEL_211_LIVING_QUARTERS ? "Shoulder Lamp" : "Night Vision";
                option->value = 0;
            }
            break;
        case OPTION_PISTOL:
            option->value = -1;
            break;
        case OPTION_SHOTGUN:
            option->value = (u_short)g_player.flags & PLAYER_HAS_SHOTGUN;
            if (option->value)
                option->value = g_player.shotgun_ammo;
            break;
        case OPTION_FLAMETHROWER:
            option->value = (u_short)g_player.flags & PLAYER_HAS_FLAMETHROWER;
            if (option->value)
                option->value = g_player.flamethrower_fuel;
            break;
        case OPTION_PULSE_RIFLE:
            option->value = (u_short)g_player.flags & PLAYER_HAS_PULSE_RIFLE;
            if (option->value)
                option->value = g_player.pulse_rifle_ammo != 0 || g_player.pulse_rifle_grenades != 0;
            break;
        case OPTION_SMART_GUN:
            option->value = (u_short)g_player.flags & PLAYER_HAS_SMART_GUN;
            if (option->value)
                option->value = g_player.smart_gun_ammo;
            break;
        case OPTION_BATTERIES:
            option->value = g_player.batteries;
            break;
        case OPTION_MISSION:
        case OPTION_SOUND:
            option->value = -1;
            break;
        }
    }
}

/* Draws a text string; space advances 4 pixels, newline returns to x. */
/* name: inferred from its body, which draws a string with hud_draw_glyph (10-pixel lines) */
void hud_draw_text(const unsigned char *text, short x, short y, unsigned char color)
{
    unsigned char style[3] = { 0, color, 0 };
    short origin_x = x;
    unsigned char character = *text++;

    while (character != 0) {
        if (character == ' ') {
            x += HUD_SPACE_WIDTH;
        } else if (character == '\n') {
            x = origin_x;
            y += HUD_LINE_HEIGHT;
        } else {
            x = hud_draw_glyph(character, x, y, style);
        }
        character = *text++;
    }
}

/* Multi-statement macro (name invented): draws one glyph in style, in white when highlight is set.
 * Its do { } while (0) block gives retail's register order in draw_text_typed. */
#define DRAW_GLYPH(c, x, y, style, color, highlight) do { \
    (style)[0] = 0; \
    (style)[2] = 0; \
    if (highlight) \
        (style)[1] = TEXT_WHITE; \
    else \
        (style)[1] = (color); \
    (x) = hud_draw_glyph((c), (x), (y), (style)); \
} while (0)

/* name: inferred from its body, which draws g_typed_length characters of text, one more per call,
 * with the newest character highlighted */
void draw_text_typed(const unsigned char *text, short x, short y)
{
    unsigned char style[3];
    short origin_x;
    short i;
    unsigned char color;

    if (text[g_typed_length] != 0)
        g_typed_length++;
    origin_x = x;
    for (i = 0; i < g_typed_length; i++) {
        int character = text[i];

        color = TEXT_FAINT;
        if (character == ' ') {
            x += HUD_SPACE_WIDTH;
        } else if (character == '\n') {
            x = origin_x;
            y += HUD_LINE_HEIGHT;
        } else {
            DRAW_GLYPH(character, x, y, style, color, text[g_typed_length] != 0 && i == g_typed_length - 1);
        }
    }
}

/* Pixel width of a text line up to its first space or carriage return.
 * name: inferred from that */
short text_word_width(const u_char *text)
{
    short stop = 0;
    short total = 0;

    do {
        u_char ch = *text++;
        if (!ch)
            break;
        switch (ch) {
        case '!': case '$': case '%': case '&':
            ++text;
            break;
        case '\r': case ' ':
            stop = -1;
            break;
        case '\n': case '#':
            break;
        default:
            total = (short)(total + g_glyph_widths[ch]);
            break;
        }
    } while (!stop);
    return total;
}

/* Draws the next word of the text at g_text_cursor, handling the inline codes.
 * name: inferred from mission_text_draw, its only caller */
void mission_text_draw_word(void)
{
    short stop = 0;
    u_char c;

    do {
        c = *g_text_cursor;
        if (c == 0)
            return;
        g_text_cursor++;
        switch (c) {
        case '\n':
        case '#':
            break;
        case '!':
        case '$':
        case '%':
            g_text_cursor++;
            break;
        case ' ':
            stop = -1;
            g_text_x += MISSION_SPACE_WIDTH;
            break;
        case '\r':
            stop = -1;
            c = *g_text_cursor;
            while (c) {
                switch (c) {
                case '\n':
                case '#':
                    g_text_cursor++;
                    c = *g_text_cursor;
                    break;
                case '!':
                case '$':
                case '%':
                    g_text_cursor += 2;
                    c = *g_text_cursor;
                    break;
                case '\r':
                    g_text_x = g_text_left - MISSION_SPACE_WIDTH;
                    g_text_cursor++;
                    g_text_y += MISSION_PARAGRAPH_HEIGHT;
                    c = 0;
                    break;
                default:
                    c = 0;
                    break;
                }
            }
            g_text_x += MISSION_SPACE_WIDTH;
            break;
        case '&':
            c = *g_text_cursor++;
            c -= '0';
            if (c == 0)
                g_text_style[1] = TEXT_DIM;
            else
                g_text_style[1] = TEXT_WHITE;
            break;
        default:
            g_text_x = hud_draw_glyph(c, g_text_x, g_text_y, g_text_style);
            break;
        }
    } while (stop == 0);
}

/* Lays out the text buffer g_mission_text word by word inside the options panel.
 * name: inferred from the "Mission" option, which shows it */
void mission_text_draw(void)
{
    short stop;
    u_char c;

    g_text_left = MISSION_LEFT;
    g_text_y = PANEL_TOP;
    g_text_x = MISSION_LEFT;
    g_8d270 = 0x40;
    g_text_cursor = (u_char *)g_mission_text;
    g_text_style[0] = 0;
    g_text_style[1] = TEXT_DIM;
    g_text_style[2] = 0;
    stop = 0;
    do {
        c = *g_text_cursor;
        if (c == 0)
            break;
        switch (c) {
        case '\n':
        case '\r':
        case ' ':
        case '#':
            g_text_cursor++;
            break;
        case '!':
        case '$':
        case '%':
            g_text_cursor += 2;
            break;
        default:
            stop = -1;
            break;
        }
    } while (stop == 0);
    while (*g_text_cursor) {
        if ((short)(g_text_x + text_word_width(g_text_cursor)) >= MISSION_RIGHT) {
            g_text_x = g_text_left;
            g_text_y += MISSION_LINE_HEIGHT;
        }
        if (g_text_y >= MISSION_BOTTOM)
            break;
        mission_text_draw_word();
    }
}

/* Option highlight bar: a 98x18 textured quad tinted by green.
 * name: inferred from that */
void draw_option_bar(int x, int y, int green)
{
    POLY_FT4 *poly;
    u_long *uv;
    short right;
    short bottom;

    uv = (u_long *)&g_option_bar_uv;
    right = x + OPTION_BAR_WIDTH;
    bottom = y + OPTION_BAR_HEIGHT;
    poly = g_sprite_workspace;
    poly->x0 = x;
    poly->x2 = x;
    poly->y0 = y;
    poly->y1 = y;
    poly->x1 = right;
    poly->x3 = right;
    poly->y2 = bottom;
    poly->y3 = bottom;
    *(u_long *)&poly->u0 = *uv++;
    *(u_long *)&poly->u1 = *uv++;
    *(u_long *)&poly->u2 = *uv++;
    *(u_long *)&poly->u3 = *uv;
    setPolyFT4(poly);
    setRGB0(poly, 0, green, 0);
    addPrim(g_render_buffer, g_sprite_workspace);
    g_sprite_workspace = poly + 1;
}

/* Blinking player arrow on the map overview.
 * name: inferred from that */
void draw_map_arrow(void)
{
    POLY_F3 *poly;
    int x;
    int y;
    int sx;
    int sy;

    if (g_frame_count & MAP_BLINK) {
        x = (g_player.position.vx + g_level_map.offset_x) >> MAP_CELL_SHIFT;
        y = (g_player.position.vz + g_level_map.offset_z) >> MAP_CELL_SHIFT;
        x -= g_map_center_x - MAP_ZOOM_RADIUS;
        y -= g_map_center_z - MAP_ZOOM_RADIUS;
        if (x >= 0 && x < MAP_ZOOM_CELLS && y >= 0 && y < MAP_ZOOM_CELLS) {
            poly = g_sprite_workspace;
            sx = x * MAP_ZOOM_SCALE + MAP_ZOOM_X;
            sy = y * MAP_ZOOM_SCALE + MAP_ZOOM_Y;
            poly->x0 = sx + g_arrow_tip.vx;
            poly->y0 = sy + g_arrow_tip.vz;
            poly->x1 = sx + g_arrow_left.vx;
            poly->y1 = sy + g_arrow_left.vz;
            poly->x2 = sx + g_arrow_right.vx;
            poly->y2 = sy + g_arrow_right.vz;
            setRGB0(poly, MAP_PLAYER_R, MAP_PLAYER_G, MAP_PLAYER_B);
            setPolyF3(poly);
            addPrim(&g_render_buffer[OT_BACK_SLOT], poly);
            poly++;
            g_sprite_workspace = poly;
        }
    }
}

/* Map overview page: player blip or arrow, the map texture quad (scrolled
 * when zoomed) and its dark backing tile.
 * name: inferred from that */
void draw_map(void)
{
    short ox;
    short oy;
    TILE *tile;
    POLY_FT4 *poly;
    u_long *uv;
    u_char u;
    u_char v;

    ox = MAP_PANEL_X - (g_level_map.width >> 1);
    oy = MAP_PANEL_Y - (g_level_map.height >> 1);
    if (g_pause_state != PAUSE_MAP) {
        if (g_frame_count & MAP_BLINK) {
            tile = g_sprite_workspace;
            tile->x0 = ox + ((g_player.position.vx + g_level_map.offset_x) >> MAP_CELL_SHIFT) - 1;
            tile->y0 = oy + ((g_player.position.vz + g_level_map.offset_z) >> MAP_CELL_SHIFT) - 1;
            tile->w = MAP_BLIP_SIZE;
            tile->h = MAP_BLIP_SIZE;
            setRGB0(tile, MAP_PLAYER_R, MAP_PLAYER_G, MAP_PLAYER_B);
            setTile(tile);
            addPrim(&g_render_buffer[OT_BACK_SLOT], tile);
            tile++;
            g_sprite_workspace = tile;
        }
    } else {
        draw_map_arrow();
    }
    poly = g_sprite_workspace;
    uv = (u_long *)&g_map_uv;
    if (g_pause_state == PAUSE_MAP) {
        poly->x0 = MAP_ZOOM_X - 2;
        poly->y0 = MAP_ZOOM_Y - 2;
        poly->x1 = poly->x0 + MAP_ZOOM_SIZE;
        poly->y2 = poly->y0 + MAP_ZOOM_SIZE;
    } else {
        poly->x0 = ox;
        poly->y0 = oy;
        poly->x1 = ox + g_level_map.width - 1;
        poly->y2 = oy + g_level_map.height - 1;
    }
    poly->y1 = poly->y0;
    poly->x2 = poly->x0;
    poly->x3 = poly->x1;
    poly->y3 = poly->y2;
    *(u_long *)&poly->u0 = *uv++;
    *(u_long *)&poly->u1 = *uv++;
    *(u_long *)&poly->u2 = *uv++;
    *(u_long *)&poly->u3 = *uv;
    if (g_pause_state == PAUSE_MAP) {
        u = poly->u0;
        v = poly->v0;
        if (g_map_center_x > MAP_ZOOM_RADIUS)
            poly->u0 = poly->u2 = u + (g_map_center_x - MAP_ZOOM_RADIUS);
        if (g_map_center_x < poly->u1 - MAP_ZOOM_RADIUS)
            poly->u1 = poly->u3 = u + (g_map_center_x + MAP_ZOOM_RADIUS);
        if (g_map_center_z > MAP_ZOOM_RADIUS)
            poly->v0 = poly->v1 = v + (g_map_center_z - MAP_ZOOM_RADIUS);
        if (g_map_center_z < poly->v2 - MAP_ZOOM_RADIUS)
            poly->v2 = poly->v3 = v + (g_map_center_z + MAP_ZOOM_RADIUS);
    }
    setRGB0(poly, MAP_TINT_R, MAP_TINT_G, MAP_TINT_B);
    setPolyFT4(poly);
    addPrim(&g_render_buffer[OT_BACK_SLOT], poly);
    poly++;
    g_sprite_workspace = poly;
    tile = (TILE *)poly;
    setRGB0(tile, 0, MAP_BACKING_G, 0);
    setTile(tile);
    tile->x0 = MAP_PANEL_X - MAP_TEXTURE_SIZE / 2 - 1;
    tile->y0 = MAP_PANEL_Y - MAP_TEXTURE_SIZE / 2 - 1;
    tile->w = MAP_TEXTURE_SIZE + 1;
    tile->h = MAP_TEXTURE_SIZE + 1;
    addPrim(&g_render_buffer[OT_BACK_SLOT], tile);
    tile++;
    g_sprite_workspace = tile;
}

/* Item page: spinning item model, then the ammunition count with its own
 * spinning model for the weapon pages.
 * name: inferred from that */
void draw_item_page(short page)
{
    u_char style[4] = { 0, TEXT_NORMAL, 0, 0 };
    MapCell cell;
    DispatchRow *model;
    short count;

    cell.palette = 1;
    switch (page) {
    case OPTION_AUTO_MAPPER:
        draw_map();
        model = g_object_models[PICKUP_AUTO_MAPPER];
        g_translation.vx = ITEM_VIEW_AUTO_MAPPER_X;
        g_translation.vy = ITEM_VIEW_AUTO_MAPPER_Y;
        g_translation.vz = ITEM_VIEW_AUTO_MAPPER_Z;
        break;
    case OPTION_LAMP:
        g_translation.vx = ITEM_VIEW_LAMP_X;
        g_translation.vy = ITEM_VIEW_LAMP_Y;
        g_translation.vz = ITEM_VIEW_LAMP_Z;
        if (g_level >= LEVEL_211_LIVING_QUARTERS)
            model = g_object_models[PICKUP_NIGHT_VISION];
        else
            model = g_object_models[PICKUP_SHOULDER_LAMP];
        break;
    case OPTION_PISTOL:
        model = g_object_models[PICKUP_PISTOL];
        g_translation.vx = ITEM_VIEW_PISTOL_X;
        g_translation.vy = ITEM_VIEW_PISTOL_Y;
        g_translation.vz = ITEM_VIEW_PISTOL_Z;
        break;
    case OPTION_SHOTGUN:
        model = g_object_models[PICKUP_SHOTGUN];
        g_translation.vx = ITEM_VIEW_SHOTGUN_X;
        g_translation.vy = ITEM_VIEW_SHOTGUN_Y;
        g_translation.vz = ITEM_VIEW_SHOTGUN_Z;
        break;
    case OPTION_FLAMETHROWER:
        model = g_object_models[PICKUP_FLAMETHROWER];
        g_translation.vx = ITEM_VIEW_FLAMETHROWER_X;
        g_translation.vy = ITEM_VIEW_FLAMETHROWER_Y;
        g_translation.vz = ITEM_VIEW_FLAMETHROWER_Z;
        break;
    case OPTION_PULSE_RIFLE:
        model = g_object_models[PICKUP_PULSE_RIFLE];
        g_translation.vx = ITEM_VIEW_PULSE_RIFLE_X;
        g_translation.vy = ITEM_VIEW_PULSE_RIFLE_Y;
        g_translation.vz = ITEM_VIEW_PULSE_RIFLE_Z;
        break;
    case OPTION_SMART_GUN:
        model = g_object_models[PICKUP_SMART_GUN];
        g_translation.vx = ITEM_VIEW_SMART_GUN_X;
        g_translation.vy = ITEM_VIEW_SMART_GUN_Y;
        g_translation.vz = ITEM_VIEW_SMART_GUN_Z;
        break;
    case OPTION_BATTERIES:
        g_translation.vx = ITEM_VIEW_BATTERY_X;
        g_translation.vy = ITEM_VIEW_BATTERY_Y;
        g_translation.vz = ITEM_VIEW_BATTERY_Z;
        model = g_object_models[PICKUP_BATTERY];
        break;
    default:
        return;
    }
    PushMatrix();
    g_model_uvs = g_pickup_uvs;
    RotMatrix(&g_model_rotation, &g_model_matrix);
    SetRotMatrix(&g_model_matrix);
    TransMatrix(&g_model_matrix, &g_translation);
    SetTransMatrix(&g_model_matrix);
    model_draw_mid_in_cell(model, &cell);
    g_model_rotation.vy = (g_model_rotation.vy + ITEM_MODEL_SPIN) & ANGLE_MASK;
    if (page == OPTION_PISTOL) {
        g_ammo_model_rotation.vz = ANGLE_180;
        g_translation.vx = AMMO_VIEW_PISTOL_X;
        g_translation.vy = AMMO_VIEW_PISTOL_Y;
        g_translation.vz = AMMO_VIEW_PISTOL_Z;
        model = g_object_models[PICKUP_PISTOL_CLIP];
        count = g_player.pistol_ammo + g_player.pistol_clips * PISTOL_CLIP_ROUNDS;
    } else if (page == OPTION_SHOTGUN) {
        model = g_object_models[PICKUP_SHOTGUN_CARTRIDGES];
        count = g_player.shotgun_ammo;
        g_ammo_model_rotation.vz = ANGLE_180;
        g_translation.vx = AMMO_VIEW_SHOTGUN_X;
        g_translation.vy = AMMO_VIEW_SHOTGUN_Y;
        g_translation.vz = AMMO_VIEW_SHOTGUN_Z;
    } else if (page == OPTION_FLAMETHROWER) {
        model = g_object_models[PICKUP_FLAMETHROWER_FUEL];
        count = g_player.flamethrower_fuel;
        g_ammo_model_rotation.vz = ANGLE_90 * 3;
        g_translation.vx = AMMO_VIEW_FLAMETHROWER_X;
        g_translation.vy = AMMO_VIEW_FLAMETHROWER_Y;
        g_translation.vz = AMMO_VIEW_FLAMETHROWER_Z;
    } else if (page == OPTION_PULSE_RIFLE) {
        g_ammo_model_rotation.vz = ANGLE_90 * 3;
        g_translation.vx = AMMO_VIEW_PULSE_RIFLE_X;
        g_translation.vy = AMMO_VIEW_PULSE_RIFLE_Y;
        g_translation.vz = AMMO_VIEW_PULSE_RIFLE_Z;
        model = g_object_models[PICKUP_PULSE_RIFLE_CLIP];
        count = g_player.pulse_rifle_ammo + g_player.pulse_rifle_clips * PULSE_RIFLE_CLIP_ROUNDS;
    } else if (page == OPTION_SMART_GUN) {
        g_ammo_model_rotation.vz = ANGLE_90 * 3;
        g_translation.vx = AMMO_VIEW_SMART_GUN_X;
        g_translation.vy = AMMO_VIEW_SMART_GUN_Y;
        g_translation.vz = AMMO_VIEW_SMART_GUN_Z;
        model = g_object_models[PICKUP_SMART_GUN_AMMO];
        count = g_player.smart_gun_ammo + g_player.smart_gun_clips * SMART_GUN_CLIP_ROUNDS;
    } else {
        g_model_uvs = g_sprite_uvs;
        PopMatrix();
        return;
    }
    if (count == 0) {
        hud_draw_text((const u_char *)"No ammo available", AMMO_NONE_X, PANEL_TOP, TEXT_NORMAL);
    } else {
        draw_number(AMMO_COUNT_X, PANEL_TOP, count, style);
        hud_draw_text((const u_char *)"rounds available", AMMO_TEXT_X, PANEL_TOP, TEXT_NORMAL);
    }
    RotMatrix(&g_ammo_model_rotation, &g_model_matrix);
    SetRotMatrix(&g_model_matrix);
    TransMatrix(&g_model_matrix, &g_translation);
    SetTransMatrix(&g_model_matrix);
    model_draw_mid_in_cell(model, &cell);
    g_ammo_model_rotation.vy += AMMO_MODEL_SPIN;
    g_model_uvs = g_sprite_uvs;
    PopMatrix();
}

/* Volume page: SFX and CD volume bars, EXIT GAME and its confirmation.
 * name: inferred from g_sound_options, which it draws */
void draw_sound_options(void)
{
    char sfx_bar[VOLUME_BAR_SIZE];
    char cd_bar[VOLUME_BAR_SIZE];
    MenuOption *option;
    short i;
    short y = PANEL_TOP;
    short x = SOUND_OPTIONS_X;

    for (i = 0; i < g_sfx_volume; i++)
        sfx_bar[i] = '*';
    sfx_bar[i] = 0;
    for (i = 0; i < g_cdda_volume; i++)
        cd_bar[i] = '*';
    cd_bar[i] = 0;
    option = g_sound_options;
    for (i = 0; i < SOUND_OPTION_COUNT; i++, option++) {
        if (g_pause_state >= PAUSE_SOUND && i == g_sound_option && ((g_frame_count >> OPTION_BLINK_SHIFT) & 1))
            hud_draw_text((const u_char *)option->label, x, y, option->value ? TEXT_BRIGHT : TEXT_DIM);
        else
            hud_draw_text((const u_char *)option->label, x, y, option->value ? TEXT_DIM : TEXT_DARK);
        draw_option_bar(x - 2, y - 2, option->value ? OPTION_BAR_LIT : OPTION_BAR_UNLIT);
        if (i == SOUND_SFX_VOLUME && g_sfx_volume)
            hud_draw_text((u_char *)sfx_bar, x, y + SOUND_BAR_OFFSET_Y, option->value ? TEXT_NORMAL : TEXT_DIM);
        else if (i == SOUND_CDDA_VOLUME && g_cdda_volume)
            hud_draw_text((u_char *)cd_bar, x, y + SOUND_BAR_OFFSET_Y, option->value ? TEXT_NORMAL : TEXT_DIM);
        y += SOUND_OPTION_SPACING;
    }
    if (g_pause_state == PAUSE_EXIT_CONFIRM) {
        y -= SOUND_BAR_OFFSET_Y;
        hud_draw_text((const u_char *)"ARE YOU SURE ?", x, y, TEXT_DIM);
        hud_draw_text((const u_char *)(g_exit_confirmed ? "Yes" : "No"), x + EXIT_ANSWER_X, y, TEXT_BRIGHT);
    }
}

/* Pause options screen: types the option list out, then draws it with the
 * highlighted option and its availability line.
 * name: inferred from that */
void pause_menu_draw(void)
{
    MenuOption *option;
    short x;
    short y;
    u_char style[4] = { 0, TEXT_NORMAL, 0, 0 };
    short i;
    short selected;
    short blink;

    if (g_pause_state == PAUSE_TYPING) {
        x = OPTIONS_X;
        y = PANEL_TOP;
        option = g_pause_options;
        for (i = 0; i < g_pause_option; y += OPTION_SPACING, i++, option++) {
            hud_draw_text(option->label, x, y, option->value ? TEXT_DIM : TEXT_DARK);
            draw_option_bar(x - 2, y - 2, option->value ? OPTION_BAR_LIT : OPTION_BAR_UNLIT);
        }
        draw_text_typed(option->label, x, y);
        draw_option_bar(x - 2, y - 2, option->value ? OPTION_BAR_LIT : OPTION_BAR_UNLIT);
        if (option->label[g_typed_length] == 0) {
            g_typed_length = 0;
            if (++g_pause_option >= OPTION_COUNT) {
                g_pause_option = 0;
                g_pause_state = PAUSE_BROWSING;
            }
        }
        draw_map();
        return;
    }
    x = OPTIONS_X;
    y = PANEL_TOP;
    option = g_pause_options;
    blink = (g_frame_count >> OPTION_BLINK_SHIFT) & 1;
    for (i = 0; i < OPTION_COUNT; y += OPTION_SPACING, i++, option++) {
        if (g_pause_state == PAUSE_MAP && i == g_pause_option)
            hud_draw_text(option->label, x, y, option->value ? TEXT_BRIGHT : TEXT_DIM);
        else if (g_pause_state >= PAUSE_SOUND) {
            if (i != g_pause_option)
                hud_draw_text(option->label, x, y, option->value ? TEXT_DIM : TEXT_DARK);
            else
                hud_draw_text(option->label, x, y, option->value ? TEXT_BRIGHT : TEXT_DIM);
        } else if (i == g_pause_option && blink)
            hud_draw_text(option->label, x, y, option->value ? TEXT_BRIGHT : TEXT_DIM);
        else
            hud_draw_text(option->label, x, y, option->value ? TEXT_DIM : TEXT_DARK);
        draw_option_bar(x - 2, y - 2, option->value ? OPTION_BAR_LIT : OPTION_BAR_UNLIT);
    }
    draw_item_page(g_pause_option);
    option = &g_pause_options[g_pause_option];
    if (g_pause_option < OPTION_MISSION) {
        if (option->value == 0) {
            selected = 0;
            switch (g_pause_option) {
            case OPTION_AUTO_MAPPER:
            case OPTION_BATTERIES:
                break;
            case OPTION_LAMP:
                if (g_player.lamp_timer || g_player.night_vision_timer) {
                    selected = -1;
                    hud_draw_text("Selected", STATUS_X, STATUS_Y, TEXT_NORMAL);
                }
                break;
            case OPTION_PISTOL:
                selected = -1;
                hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                break;
            case OPTION_SHOTGUN:
                if (g_player.flags & PLAYER_HAS_SHOTGUN) {
                    selected = -1;
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                }
                break;
            case OPTION_FLAMETHROWER:
                if (g_player.flags & PLAYER_HAS_FLAMETHROWER) {
                    selected = -1;
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                }
                break;
            case OPTION_PULSE_RIFLE:
                if (g_player.flags & PLAYER_HAS_PULSE_RIFLE) {
                    selected = -1;
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                }
                break;
            case OPTION_SMART_GUN:
                if (g_player.flags & PLAYER_HAS_SMART_GUN) {
                    selected = -1;
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                }
                break;
            }
            if (selected == 0)
                hud_draw_text("Not available", STATUS_NOT_AVAILABLE_X, STATUS_Y, TEXT_NORMAL);
        } else {
            selected = 0;
            switch (g_pause_option) {
            case OPTION_AUTO_MAPPER:
                if (g_pause_state == PAUSE_MAP)
                    selected = -1;
                else
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                break;
            case OPTION_LAMP:
                if (g_player.lamp_timer || g_player.night_vision_timer) {
                    selected = -1;
                } else {
                    if (g_player.lamp_charges)
                        draw_number(STATUS_COUNT_X, STATUS_Y, g_player.lamp_charges, style);
                    else
                        draw_number(STATUS_COUNT_X, STATUS_Y, g_player.night_vision_charges, style);
                    hud_draw_text("Available", STATUS_COUNT_TEXT_X, STATUS_Y, TEXT_NORMAL);
                }
                break;
            case OPTION_PISTOL:
                if (g_player.weapon == WEAPON_PISTOL)
                    selected = -1;
                else
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                break;
            case OPTION_SHOTGUN:
                if (g_player.weapon == WEAPON_SHOTGUN)
                    selected = -1;
                else
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                break;
            case OPTION_FLAMETHROWER:
                if (g_player.weapon == WEAPON_FLAMETHROWER)
                    selected = -1;
                else
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                break;
            case OPTION_PULSE_RIFLE:
                if (g_player.weapon == WEAPON_PULSE_RIFLE)
                    selected = -1;
                else
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                break;
            case OPTION_SMART_GUN:
                if (g_player.weapon == WEAPON_SMART_GUN)
                    selected = -1;
                else
                    hud_draw_text("Available", STATUS_X, STATUS_Y, TEXT_NORMAL);
                break;
            case OPTION_BATTERIES:
                draw_number(STATUS_COUNT_X, STATUS_Y, g_player.batteries, style);
                hud_draw_text("Available", STATUS_COUNT_TEXT_X, STATUS_Y, TEXT_NORMAL);
                break;
            }
            if (selected)
                hud_draw_text("Selected", STATUS_X, STATUS_Y, TEXT_NORMAL);
        }
    } else if (g_pause_option == OPTION_MISSION) {
        mission_text_draw();
    } else if (g_pause_option == OPTION_SOUND) {
        draw_sound_options();
    }
}

/* Applies the highlighted pause option: map view, lamp, weapon select or exit. */
/* name: inferred from its body, which applies the highlighted pause option */
void pause_menu_select(void)
{
    MenuOption *option = &g_pause_options[g_pause_option];

    switch (g_pause_option) {
    case OPTION_AUTO_MAPPER:
        if (option->value) {
            g_pause_state = PAUSE_MAP;
            g_map_center_x = (g_player.position.vx + g_level_map.offset_x) >> MAP_CELL_SHIFT;
            g_map_center_z = (g_player.position.vz + g_level_map.offset_z) >> MAP_CELL_SHIFT;
            if (g_map_center_x < MAP_ZOOM_RADIUS)
                g_map_center_x = MAP_ZOOM_RADIUS;
            if (g_map_center_x > g_level_map.width - MAP_ZOOM_RADIUS)
                g_map_center_x = g_level_map.width - MAP_ZOOM_RADIUS;
            if (g_map_center_z < MAP_ZOOM_RADIUS)
                g_map_center_z = MAP_ZOOM_RADIUS;
            if (g_map_center_z > g_level_map.height - MAP_ZOOM_RADIUS)
                g_map_center_z = g_level_map.height - MAP_ZOOM_RADIUS;
        }
        break;
    case OPTION_LAMP:
        if (option->value && g_player.lamp_timer == 0
            && g_player.night_vision_timer == 0) {
            if (g_player.lamp_charges) {
                if (--g_player.lamp_charges == 0)
                    option->value = 0;
                g_game_flags |= GAME_FLAG_LAMP | GAME_FLAG_LAMP_FADE_IN;
                g_player.lamp_timer = PLAYER_LAMP_TIME;
            } else if (g_player.night_vision_charges) {
                if (--g_player.night_vision_charges == 0)
                    option->value = 0;
                g_game_flags |= GAME_FLAG_NIGHT_VISION | GAME_FLAG_LAMP_FADE_IN;
                g_player.night_vision_timer = PLAYER_NIGHT_VISION_TIME;
            }
        }
        break;
    case OPTION_PISTOL:
        if (g_player.weapon != WEAPON_PISTOL)
            weapon_select_slot(WEAPON_PISTOL);
        break;
    case OPTION_SHOTGUN:
        if (option->value && g_player.weapon != WEAPON_SHOTGUN)
            weapon_select_slot(WEAPON_SHOTGUN);
        break;
    case OPTION_FLAMETHROWER:
        if (option->value && g_player.weapon != WEAPON_FLAMETHROWER)
            weapon_select_slot(WEAPON_FLAMETHROWER);
        break;
    case OPTION_PULSE_RIFLE:
        if (option->value && g_player.weapon != WEAPON_PULSE_RIFLE)
            weapon_select_slot(WEAPON_PULSE_RIFLE);
        break;
    case OPTION_SMART_GUN:
        if (option->value && g_player.weapon != WEAPON_SMART_GUN)
            weapon_select_slot(WEAPON_SMART_GUN);
        break;
    case OPTION_SOUND:
        g_sound_option = SOUND_SFX_VOLUME;
        g_pause_state = PAUSE_SOUND;
        break;
    }
}

/* Steps the highlighted volume, or asks to confirm EXIT GAME.
 * name: inferred from g_sound_options */
void sound_options_select(void)
{
    switch (g_sound_option) {
    case SOUND_SFX_VOLUME:
        ++g_sfx_volume;
        if (g_sfx_volume > VOLUME_MAX) g_sfx_volume = 0;
        set_sound_volume(g_sfx_volume);
        break;
    case SOUND_CDDA_VOLUME:
        ++g_cdda_volume;
        if (g_cdda_volume > VOLUME_MAX) g_cdda_volume = 0;
        set_music_volume(g_cdda_volume);
        break;
    case SOUND_EXIT_GAME:
        g_pause_state = PAUSE_EXIT_CONFIRM;
        g_exit_confirmed = 0;
        break;
    }
}

/* Left/right toggle the answer; X answers. Returns -1 to leave the game.
 * name: inferred from "ARE YOU SURE ?" */
int exit_confirm_update(void)
{
    int buttons = g_pad_pressed;
    if ((buttons & PADLleft) || (buttons & PADLright)) {
        g_exit_confirmed = !g_exit_confirmed;
        play_menu_sound(MENU_SOUND_MOVE);
        return 0;
    }
    if (buttons & PADRdown) {
        play_menu_sound(MENU_SOUND_MOVE);
        if (g_exit_confirmed) {
            g_game_flags |= GAME_FLAG_RESET;
            return -1;
        }
        g_pause_state = PAUSE_SOUND;
    }
    return 0;
}

/* Waits for drawing, shows the finished buffer and starts the other one.
 * name: inferred from that */
void swap_display_buffers(void)
{
    g_graphics_busy = -1;
    if (g_graphics_busy != 0) {
        while (g_graphics_busy != 0) {
        }
    }
    DrawSync(0);
    PutDispEnv(&g_display->disp);
    DrawOTag(&g_render_buffer[OT_LENGTH - 1]);
    g_display = g_display == g_render_workspace ?
        g_display_default : g_render_workspace;
    g_render_buffer = g_display->ot;
    g_sprite_workspace = g_display->prims;
    ClearOTagR(g_render_buffer, OT_LENGTH);
    AddPrim(&g_render_buffer[OT_LENGTH - 1], &g_display->draw);
}

/* The pause menu loop: reads the pad, draws the menu and handles input per state
 * until Start, an exit, or reset_combo_check ends it. */
/* name: inferred from its body, which runs the pause menu loop */
void pause_menu_run(void)
{
    short stop;
    short delay;
    int sound;
    short changed;

    stop = 0;
    music_pause();
    delay = 0;
    sound_mute_all();
    sound = -1;
    pause_menu_init();
    do {
        g_pad_previous = g_pad_held;
        g_pad_held = PadRead(0);
        g_pad_pressed = ~g_pad_previous & g_pad_held;
        if (reset_combo_check(g_pad_held))
            stop = -1;
        pause_menu_draw();
        swap_display_buffers();
        music_poll();
        if (delay > 0 && --delay == 0) {
            sound_stop(sound);
            sound = -1;
        }
        changed = 0;
        if (g_pause_state == PAUSE_BROWSING) {
            if (g_pad_pressed & PADLup) {
                play_menu_sound(MENU_SOUND_MOVE);
                if (--g_pause_option < 0)
                    g_pause_option = OPTION_COUNT - 1;
            } else if (g_pad_pressed & PADLdown) {
                play_menu_sound(MENU_SOUND_MOVE);
                if (++g_pause_option >= OPTION_COUNT)
                    g_pause_option = 0;
            } else if (g_pad_pressed & PADLright || g_pad_pressed & PADRdown) {
                play_menu_sound(MENU_SOUND_MOVE);
                pause_menu_select();
            }
        } else if (g_pause_state == PAUSE_MAP) {
            if (g_pad_held & PADLup) {
                if (g_map_center_z > MAP_ZOOM_RADIUS) {
                    changed = -1;
                    g_map_center_z--;
                }
            } else if (g_pad_held & PADLdown) {
                if (g_map_center_z < g_level_map.height - MAP_ZOOM_RADIUS) {
                    changed = -1;
                    g_map_center_z++;
                }
            }
            if (g_pad_held & PADLleft) {
                if (g_map_center_x > MAP_ZOOM_RADIUS) {
                    changed = -1;
                    g_map_center_x--;
                }
            } else if (g_pad_held & PADLright) {
                if (g_map_center_x < g_level_map.width - MAP_ZOOM_RADIUS) {
                    changed = -1;
                    g_map_center_x++;
                }
            }
            if (g_pad_pressed & PADRdown) {
                play_menu_sound(MENU_SOUND_MOVE);
                g_pause_state = PAUSE_BROWSING;
            }
        } else if (g_pause_state == PAUSE_SOUND) {
            if (g_pad_pressed & PADLup) {
                play_menu_sound(MENU_SOUND_MOVE);
                if (--g_sound_option < 0)
                    g_sound_option = SOUND_OPTION_COUNT - 1;
            } else if (g_pad_pressed & PADLdown) {
                play_menu_sound(MENU_SOUND_MOVE);
                if (++g_sound_option >= SOUND_OPTION_COUNT)
                    g_sound_option = 0;
            } else if (g_pad_pressed & PADLleft) {
                play_menu_sound(MENU_SOUND_MOVE);
                g_pause_state = PAUSE_BROWSING;
            } else if (g_pad_pressed & PADRdown) {
                sound_options_select();
                play_menu_sound(MENU_SOUND_MOVE);
            }
        } else if (g_pause_state == PAUSE_EXIT_CONFIRM) {
            stop = exit_confirm_update();
        }
        if (changed) {
            if (sound == -1) {
                sound = sound_play(SOUND_MAP_SCROLL, -1, SPU_VOLUME_MAX);
                delay = MAP_SCROLL_SOUND_TIME;
            }
        } else {
            sound_stop(sound);
            sound = -1;
        }
        if (stop == 0)
            stop = g_pad_pressed & PADstart;
    } while (stop == 0);
    sound_stop(sound);
    sound_unmute_all();
    music_unpause();
}
