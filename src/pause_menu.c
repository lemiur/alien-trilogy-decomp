/* pause_menu.c -- the pause menu: automap, mission text, options and item pages */
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

/* g_pauseState. names: inferred from the input each state handles in Pause_Run */
enum {
    PAUSE_TYPING,      /* the option labels are typed out one by one */
    PAUSE_BROWSING,    /* up/down choose an option, the item page follows it */
    PAUSE_MAP,         /* the auto mapper scrolls the map */
    PAUSE_SOUND,       /* the sound page: SFX, CDDA volume and exit */
    PAUSE_EXIT_CONFIRM /* "ARE YOU SURE ?" */
};

/* g_pauseOption, in the order of g_pauseOptions' labels. */
enum {
    OPTION_AUTO_MAPPER,
    OPTION_LAMP, /* "Shoulder Lamp" or "Night Vision" */
    OPTION_PISTOL,
    OPTION_SHOTGUN,
    OPTION_FLAMETHROWER,
    OPTION_PULSE_RIFLE,
    OPTION_SMART_GUN,
    OPTION_BATTERIES,
    OPTION_MISSION,
    OPTION_SOUND, /* "Options" */
    OPTION_COUNT
};

/* g_soundOption, in the order of g_soundOptions' labels. */
enum {
    SOUND_SFX_VOLUME,
    SOUND_CDDA_VOLUME,
    SOUND_EXIT_GAME,
    SOUND_OPTION_COUNT
};

/* Volumes run 0..10; each select steps one up and wraps. */
#define VOLUME_MAX 10

/* A 15-bit VRAM colour from 5-bit components. name: invented */
#define RGB15_BLUE_SHIFT 10
#define RGB15_GREEN_SHIFT 5
#define RGB15( r, g, b ) ( ( ( b ) << RGB15_BLUE_SHIFT ) | ( ( g ) << RGB15_GREEN_SHIFT ) | ( r ) )

/* A glyph style: r, g, b and a padding byte. The mission text layout stores MISSION_TEXT_MARK in g_8d270.
 * names: invented */
#define TEXT_STYLE_SIZE 4
#define MISSION_TEXT_MARK 0x40

/* Item models spin tilted forward by this pitch. name: invented */
#define ITEM_MODEL_TILT 0x80

/* Screen layout of the pages. names: invented */
#define OPTIONS_X 0x10 /* the option list */
#define PANEL_TOP 0x20 /* first line of the option list and of the right-hand panel */
#define SOUND_OPTIONS_X 0xa2
#define EXIT_ANSWER_X 0x70    /* "Yes"/"No" right of "ARE YOU SURE ?" */
#define OPTION_BAR_WIDTH 0x62 /* the highlight bar behind each option label */
#define OPTION_BAR_HEIGHT 0x12
#define OPTION_BAR_LIT 0x80 /* green of the bar of an available option */
#define OPTION_BAR_UNLIT 0x20
#define STATUS_Y 0xd4 /* "Available", "Selected" or "Not available" under the item */
#define STATUS_X 0xc0
#define STATUS_NOT_AVAILABLE_X 0xb8
#define STATUS_COUNT_X 0xb4 /* a charge count before "Available" */
#define STATUS_COUNT_TEXT_X 0xd4
#define AMMO_NONE_X 0xa0  /* "No ammo available" */
#define AMMO_COUNT_X 0x98 /* the round count before "rounds available" */
#define AMMO_TEXT_X 0xb6
#define MISSION_LEFT 0x80 /* the mission text's box */
#define MISSION_RIGHT 0x139
#define MISSION_BOTTOM 0xd1

/* The auto mapper: one texel per cell in a MAP_TEXTURE_SIZE square texture, drawn centred
 * on the panel, or MAP_ZOOM_SIZE pixels square at four pixels per cell when zoomed. names: invented */
#define MAP_TEXTURE_SIZE 128
#define MAP_PANEL_X 0xe0
#define MAP_PANEL_Y 0x60
#define MAP_ZOOM_X 0xa8 /* centre of the zoomed map's first cell */
#define MAP_ZOOM_Y 0x28
#define MAP_ZOOM_SIZE 0x74
#define MAP_BLINK 0x10 /* g_frameCount bit that blinks the player's blip */

/* More of the auto mapper: the zoomed map shows MAP_ZOOM_RADIUS cells each side of
 * g_mapCenterX/_z at MAP_ZOOM_SCALE pixels a cell; the player is a MAP_BLIP_SIZE square
 * blip on the overview and an arrow (tip ahead, two back corners) when zoomed. names: invented */
#define MAP_ZOOM_RADIUS 14
#define MAP_ZOOM_CELLS ( 2 * MAP_ZOOM_RADIUS + 1 )
#define MAP_ZOOM_SCALE 4
#define MAP_BLIP_SIZE 3
#define MAP_ARROW_LENGTH 8
#define MAP_ARROW_HALF_WIDTH 4
#define MAP_ARROW_BACK 4
#define MAP_CLUT_ROW 31           /* g_clutSets[CLUT_SET_SPRITES] row the map palette is loaded into */
#define MAP_SCROLL_SOUND_TIME 100 /* frames SOUND_MAP_SCROLL plays before it is stopped */
/* Colours: the player's blip and arrow, the map texture's shading, the tile behind the map */
#define MAP_PLAYER_R 192
#define MAP_PLAYER_G 192
#define MAP_PLAYER_B 255
#define MAP_TINT_R 128
#define MAP_TINT_G 192
#define MAP_TINT_B 128
#define MAP_BACKING_G 24

/* Map texels: palette indices of the map texture (Pause_BuildAutomap), and their colours. names: invented */
enum {
    AUTOMAP_EMPTY,
    AUTOMAP_FLOOR,    /* a cell seen (or shown by the auto mapper) */
    AUTOMAP_WALL,     /* CELL_BLOCKED, or a cell without a face */
    AUTOMAP_UNUSED_3, /* identity-TODO: no matched code draws it */
    AUTOMAP_OBJECT,   /* a level object's cell (auto mapper only) */
    AUTOMAP_SPECIAL   /* identity-TODO: cells of type 6 */
};
#define AUTOMAP_FLOOR_COLOR RGB15( 8, 10, 8 )
#define AUTOMAP_WALL_COLOR RGB15( 4, 6, 4 )
#define AUTOMAP_UNUSED_3_COLOR RGB15( 16, 6, 4 )
#define AUTOMAP_OBJECT_COLOR RGB15( 4, 6, 16 )
#define AUTOMAP_SPECIAL_COLOR RGB15( 4, 16, 4 )

/* Text metrics: Pause_DrawHUDText and Pause_DrawTextTyped advance HUD_SPACE_WIDTH for a space and
 * HUD_LINE_HEIGHT for a newline; the mission text uses its own. names: invented */
#define HUD_SPACE_WIDTH 4
#define HUD_LINE_HEIGHT 10
#define MISSION_SPACE_WIDTH 6
#define MISSION_LINE_HEIGHT 9
#define MISSION_PARAGRAPH_HEIGHT 18

/* Option lists: line pitch, the g_frameCount shift that blinks the highlighted option, and
 * the sound page's options with their volume bars under them. names: invented */
#define OPTION_SPACING 20
#define OPTION_BLINK_SHIFT 3
#define SOUND_OPTION_SPACING 40
#define SOUND_BAR_OFFSET_Y 20
#define VOLUME_BAR_SIZE 16

/* Item page models (Pause_DrawItemPage): yaw steps per frame, and where each item's model and
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
#define AMMO_VIEW_PISTOL_Y ( -64 )
#define AMMO_VIEW_PISTOL_Z 400
#define AMMO_VIEW_SHOTGUN_X 128
#define AMMO_VIEW_SHOTGUN_Y ( -80 )
#define AMMO_VIEW_SHOTGUN_Z 640
#define AMMO_VIEW_FLAMETHROWER_X 128
#define AMMO_VIEW_FLAMETHROWER_Y ( -96 )
#define AMMO_VIEW_FLAMETHROWER_Z 640
#define AMMO_VIEW_PULSE_RIFLE_X 160
#define AMMO_VIEW_PULSE_RIFLE_Y ( -112 )
#define AMMO_VIEW_PULSE_RIFLE_Z 768
#define AMMO_VIEW_SMART_GUN_X 160
#define AMMO_VIEW_SMART_GUN_Y ( -112 )
#define AMMO_VIEW_SMART_GUN_Z 768

/* Inline codes of the mission text: '!', '$' and '%' carry one parameter byte,
 * '&' a colour digit, '#' and '\n' are skipped, '\r' ends a paragraph. */
/* Module statics (.sbss 0x8008d218..0x8008d2b8), in their retail order. */
/* name: inferred from the PAUSE_* states it selects */
static short g_pauseState;
/* name: inferred from indexing g_pauseOptions; while typing, the labels typed so far */
static short g_pauseOption;
/* name: inferred from Pause_DrawTextTyped, which types g_pauseOptions' current label */
static short g_typedLength;
/* name: inferred from indexing g_soundOptions */
static short g_soundOption;
/* name: inferred from the "Yes"/"No" answer to "ARE YOU SURE ?" */
static short g_exitConfirmed;
/* identity-TODO: no jal, pointer word, lui/lo pair or $gp operand in retail reaches it (no access found at all). */
static int g_8d240;
/* names: inferred from the "SFX VOLUME" and "CDDA VOLUME" bars */
static short g_sfxVolume;
static short g_cddaVolume;
/* names: inferred from the mission text layout (Pause_DrawMissionText) */
static short g_textLeft;
static short g_textX;
static short g_textY;
static short g_8d270; /* identity-TODO: set to MISSION_TEXT_MARK by Pause_DrawMissionText; its only reference in retail */
static u_char *g_textCursor;
static u_char  g_textStyle[RGB_CHANNELS];
/* names: inferred from the map scroll in Pause_Run and the map texture window */
static short g_mapCenterX;
static short g_mapCenterZ;
/* name: inferred from RotMatrix of the item models in Pause_DrawItemPage */
static SVECTOR g_modelRotation;
/* name: inferred from RotMatrix of the ammunition models in Pause_DrawItemPage */
static SVECTOR g_ammoModelRotation;
/* names: inferred from the three vertices of the map arrow (Pause_DrawMapArrow) */
static SVECTOR g_arrowTip;
static SVECTOR g_arrowRight;
static SVECTOR g_arrowLeft;
/* name: inferred from typing g_typedLength characters of an option label */
extern void Pause_DrawTextTyped( const unsigned char *text, short x, short y );
extern void Pause_BuildAutomap( void );
/* Texture words of the option highlight bar.
 * name: inferred from Pause_DrawOptionBar */
extern spriteUV_t g_optionBarUV;
/* name: inferred from Pause_TextWordWidth, which sums them per character */
extern u_short g_glyphWidths[CHAR_CODE_MASK + 1];
/* The automap texture words: Pause_BuildAutomap fills them for the slot it uploads the map
 * image to, Pause_DrawMap copies them into its POLY_FT4.
 * name: inferred from use */
extern spriteUV_t g_mapUV;
/* name: inferred from RotMatrix/TransMatrix for the item models */
extern MATRIX g_modelMatrix;

/* name: inferred from the labels */
menuOption_t g_pauseOptions[OPTION_COUNT] = {
    { 0, "Auto Mapper" }, { 0, "Shoulder Lamp" }, { 0, "9mm Pistol" }, { 0, "Shotgun" }, { 0, "Flamethrower" },
    { 0, "Pulse Rifle" }, { 0, "Smart Gun" },     { 0, "Batteries" },  { 0, "Mission" }, { 0, "Options" },
};

/* name: inferred from the labels */
menuOption_t g_soundOptions[SOUND_OPTION_COUNT] = {
    { -1, "SFX VOLUME" },
    { -1, "CDDA VOLUME" },
    { -1, "EXIT GAME" },
};

/*
==================
Pause_BuildAutomap

Builds the automap image in g_unpackBuffer: one pixel per map cell, coloured
by what the player has seen, uploaded with its CLUT into a free VRAM slot.
==================
*/
/* name: inferred from its body */
void Pause_BuildAutomap( void )
{
    u_short       *palette;
    clutRow_t     *clut;
    u_char        *pixel;
    mapCell_t     *cell;
    texturePage_t *slot;
    u_short        x;
    u_short        y;
    short          i;
    RECT           rect;

    palette = g_unpackBuffer;
    palette[AUTOMAP_EMPTY] = 0;
    palette[AUTOMAP_FLOOR] = AUTOMAP_FLOOR_COLOR;
    palette[AUTOMAP_WALL] = AUTOMAP_WALL_COLOR;
    palette[AUTOMAP_UNUSED_3] = AUTOMAP_UNUSED_3_COLOR;
    palette[AUTOMAP_OBJECT] = AUTOMAP_OBJECT_COLOR;
    palette[AUTOMAP_SPECIAL] = AUTOMAP_SPECIAL_COLOR;
    clut = &g_clutSets[CLUT_SET_SPRITES].rows[MAP_CLUT_ROW];
    LoadClut( g_unpackBuffer, clut->x, clut->y );
    DrawSync( 0 );
    pixel = g_unpackBuffer;
    cell = g_levelMap.cells;
    for ( y = 0; y < MAP_TEXTURE_SIZE; y++ ) {
        for ( x = 0; x < MAP_TEXTURE_SIZE; x++ ) {
            if ( y >= g_levelMap.height || x >= g_levelMap.width ) {
                *pixel++ = AUTOMAP_EMPTY;
            } else {
                if ( g_player.flags & PLAYER_HAS_AUTO_MAPPER ) {
                    if ( g_secretsFound[cell->secretIndex] ) {
                        if ( cell->marker != 0 && cell->marker >= LEVEL_OBJECT_TARGET && cell->marker < CELL_MARKER_PLAYER )
                            *pixel++ = AUTOMAP_OBJECT;
                        else if ( cell->light & CELL_BLOCKED )
                            *pixel++ = AUTOMAP_WALL;
                        else if ( cell->type == CELL_TYPE_DOOR )
                            *pixel++ = AUTOMAP_SPECIAL;
                        else if ( cell->face != 0 || ( cell->light & CELL_VISITED ) )
                            *pixel++ = AUTOMAP_FLOOR;
                        else
                            *pixel++ = AUTOMAP_EMPTY;
                    } else
                        *pixel++ = AUTOMAP_EMPTY;
                } else {
                    if ( ( cell->light & CELL_VISITED ) && g_secretsFound[cell->secretIndex] ) {
                        if ( cell->face == 0 || ( cell->light & CELL_BLOCKED ) )
                            *pixel++ = AUTOMAP_WALL;
                        else if ( cell->type == CELL_TYPE_DOOR )
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
    slot = g_pageSets[PAGE_SET_SPRITES].pages;
    for ( i = 0; i < g_pageSets[PAGE_SET_SPRITES].count; i++, slot++ ) {
        if ( !( slot->reserved & 1 ) )
            break;
    }
    slot->pixels = 0;
    slot->users = 0;
    g_mapUV.u0 = 0;
    g_mapUV.clut = clut->clut;
    g_mapUV.u1 = g_levelMap.width - 1;
    g_mapUV.v0 = slot->y;
    g_mapUV.v1 = g_mapUV.v0;
    g_mapUV.v2 = g_mapUV.v0 + g_levelMap.height - 1;
    g_mapUV.v3 = g_mapUV.v2;
    g_mapUV.tpage = slot->tpage;
    g_mapUV.u2 = 0;
    g_mapUV.u3 = g_mapUV.u1;
    DrawSync( 0 );
    rect.x = slot->x;
    rect.y = slot->y;
    rect.w = MAP_TEXTURE_SIZE / 2;
    rect.h = MAP_TEXTURE_SIZE;
    LoadImage( &rect, g_unpackBuffer );
    DrawSync( 0 );
}

/*
==================
Pause_Init
==================
*/
/* name: inferred from it resetting the menu state and option values before Pause_Run's loop */
void Pause_Init( void )
{
    menuOption_t *option;
    short         i;

    g_modelRotation.vx = 0;
    g_modelRotation.vy = g_viewRotation.vy;
    g_modelRotation.vz = 0;
    g_arrowTip.vx = 0;
    g_arrowTip.vy = 0;
    g_arrowTip.vz = MAP_ARROW_LENGTH;
    g_arrowRight.vx = MAP_ARROW_HALF_WIDTH;
    g_arrowRight.vy = 0;
    g_arrowRight.vz = -MAP_ARROW_BACK;
    g_arrowLeft.vx = -MAP_ARROW_HALF_WIDTH;
    g_arrowLeft.vy = 0;
    g_arrowLeft.vz = -MAP_ARROW_BACK;
    PushMatrix();
    Geo_RotateVector( &g_modelRotation, &g_arrowTip, &g_arrowTip );
    Geo_RotateVector( &g_modelRotation, &g_arrowRight, &g_arrowRight );
    Geo_RotateVector( &g_modelRotation, &g_arrowLeft, &g_arrowLeft );
    PopMatrix();
    Pause_BuildAutomap();
    g_pauseState = PAUSE_TYPING;
    g_pauseOption = 0;
    g_typedLength = 0;
    g_mapCenterX = ( g_player.position.vx + g_levelMap.offsetX ) >> MAP_CELL_SHIFT;
    g_mapCenterZ = ( g_player.position.vz + g_levelMap.offsetZ ) >> MAP_CELL_SHIFT;
    g_sfxVolume = Menu_GetSoundVolume();
    g_cddaVolume = Menu_GetMusicVolume();
    if ( g_sfxVolume < 0 )
        g_sfxVolume = 0;
    if ( g_sfxVolume > VOLUME_MAX )
        g_sfxVolume = VOLUME_MAX;
    if ( g_cddaVolume < 0 )
        g_cddaVolume = 0;
    if ( g_cddaVolume > VOLUME_MAX )
        g_cddaVolume = VOLUME_MAX;
    g_modelRotation.vx = ITEM_MODEL_TILT;
    g_modelRotation.vy = 0;
    g_modelRotation.vz = ANGLE_180;
    g_ammoModelRotation.vx = ITEM_MODEL_TILT;
    g_ammoModelRotation.vy = 0;
    option = g_pauseOptions;
    for ( i = 0; i < OPTION_COUNT; i++, option++ ) {
        switch ( i ) {
        case OPTION_AUTO_MAPPER:
            option->value = (u_short)g_player.flags & PLAYER_HAS_AUTO_MAPPER;
            break;
        case OPTION_LAMP:
            if ( g_player.lampCharges ) {
                option->label = "Shoulder Lamp";
                option->value = g_player.lampTimer == 0;
            } else if ( g_player.nightVisionCharges ) {
                option->label = "Night Vision";
                option->value = g_player.nightVisionTimer == 0;
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
            if ( option->value )
                option->value = g_player.shotgunAmmo;
            break;
        case OPTION_FLAMETHROWER:
            option->value = (u_short)g_player.flags & PLAYER_HAS_FLAMETHROWER;
            if ( option->value )
                option->value = g_player.flamethrowerFuel;
            break;
        case OPTION_PULSE_RIFLE:
            option->value = (u_short)g_player.flags & PLAYER_HAS_PULSE_RIFLE;
            if ( option->value )
                option->value = g_player.pulseRifleAmmo != 0 || g_player.pulseRifleGrenades != 0;
            break;
        case OPTION_SMART_GUN:
            option->value = (u_short)g_player.flags & PLAYER_HAS_SMART_GUN;
            if ( option->value )
                option->value = g_player.smartGunAmmo;
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

/*
==================
Pause_DrawHUDText

Draws a text string; space advances 4 pixels, newline returns to x.
==================
*/
/* name: inferred from its body, which draws a string with Hud_DrawGlyph (10-pixel lines) */
void Pause_DrawHUDText( const unsigned char *text, short x, short y, unsigned char color )
{
    unsigned char style[RGB_CHANNELS] = { 0, color, 0 };
    short         originX = x;
    unsigned char character = *text++;

    while ( character != 0 ) {
        if ( character == ' ' ) {
            x += HUD_SPACE_WIDTH;
        } else if ( character == '\n' ) {
            x = originX;
            y += HUD_LINE_HEIGHT;
        } else {
            x = Hud_DrawGlyph( character, x, y, style );
        }
        character = *text++;
    }
}

/* Multi-statement macro (name invented): draws one glyph in style, in white when highlight is set.
 * Its do { } while (0) block gives retail's register order in Pause_DrawTextTyped. */
#define DRAW_GLYPH( c, x, y, style, color, highlight )           \
    do {                                                         \
        ( style )[0] = 0;                                        \
        ( style )[2] = 0;                                        \
        if ( highlight )                                         \
            ( style )[1] = TEXT_WHITE;                           \
        else                                                     \
            ( style )[1] = ( color );                            \
        ( x ) = Hud_DrawGlyph( ( c ), ( x ), ( y ), ( style ) ); \
    } while ( 0 )

/*
==================
Pause_DrawTextTyped
==================
*/
/* name: inferred from its body, which draws g_typedLength characters of text, one more per call,
 * with the newest character highlighted */
void Pause_DrawTextTyped( const unsigned char *text, short x, short y )
{
    unsigned char style[RGB_CHANNELS];
    short         originX;
    short         i;
    unsigned char color;

    if ( text[g_typedLength] != 0 )
        g_typedLength++;
    originX = x;
    for ( i = 0; i < g_typedLength; i++ ) {
        int character = text[i];

        color = TEXT_FAINT;
        if ( character == ' ' ) {
            x += HUD_SPACE_WIDTH;
        } else if ( character == '\n' ) {
            x = originX;
            y += HUD_LINE_HEIGHT;
        } else {
            DRAW_GLYPH( character, x, y, style, color, text[g_typedLength] != 0 && i == g_typedLength - 1 );
        }
    }
}

/*
==================
Pause_TextWordWidth

Pixel width of a text line up to its first space or carriage return.
==================
*/
/* name: inferred from that */
short Pause_TextWordWidth( const u_char *text )
{
    short stop = 0;
    short total = 0;

    do {
        u_char ch = *text++;
        if ( !ch )
            break;
        switch ( ch ) {
        case '!':
        case '$':
        case '%':
        case '&':
            ++text;
            break;
        case '\r':
        case ' ':
            stop = -1;
            break;
        case '\n':
        case '#':
            break;
        default:
            total = (short)( total + g_glyphWidths[ch] );
            break;
        }
    } while ( !stop );
    return total;
}

/*
==================
Pause_DrawMissionTextWord

Draws the next word of the text at g_textCursor, handling the inline codes.
==================
*/
/* name: inferred from Pause_DrawMissionText, its only caller */
void Pause_DrawMissionTextWord( void )
{
    short  stop = 0;
    u_char c;

    do {
        c = *g_textCursor;
        if ( c == 0 )
            return;
        g_textCursor++;
        switch ( c ) {
        case '\n':
        case '#':
            break;
        case '!':
        case '$':
        case '%':
            g_textCursor++;
            break;
        case ' ':
            stop = -1;
            g_textX += MISSION_SPACE_WIDTH;
            break;
        case '\r':
            stop = -1;
            c = *g_textCursor;
            while ( c ) {
                switch ( c ) {
                case '\n':
                case '#':
                    g_textCursor++;
                    c = *g_textCursor;
                    break;
                case '!':
                case '$':
                case '%':
                    g_textCursor += 2;
                    c = *g_textCursor;
                    break;
                case '\r':
                    g_textX = g_textLeft - MISSION_SPACE_WIDTH;
                    g_textCursor++;
                    g_textY += MISSION_PARAGRAPH_HEIGHT;
                    c = 0;
                    break;
                default:
                    c = 0;
                    break;
                }
            }
            g_textX += MISSION_SPACE_WIDTH;
            break;
        case '&':
            c = *g_textCursor++;
            c -= '0';
            if ( c == 0 )
                g_textStyle[1] = TEXT_DIM;
            else
                g_textStyle[1] = TEXT_WHITE;
            break;
        default:
            g_textX = Hud_DrawGlyph( c, g_textX, g_textY, g_textStyle );
            break;
        }
    } while ( stop == 0 );
}

/*
==================
Pause_DrawMissionText

Lays out the text buffer g_missionText word by word inside the options panel.
==================
*/
/* name: inferred from the "Mission" option, which shows it */
void Pause_DrawMissionText( void )
{
    short  stop;
    u_char c;

    g_textLeft = MISSION_LEFT;
    g_textY = PANEL_TOP;
    g_textX = MISSION_LEFT;
    g_8d270 = MISSION_TEXT_MARK;
    g_textCursor = (u_char *)g_missionText;
    g_textStyle[0] = 0;
    g_textStyle[1] = TEXT_DIM;
    g_textStyle[2] = 0;
    stop = 0;
    do {
        c = *g_textCursor;
        if ( c == 0 )
            break;
        switch ( c ) {
        case '\n':
        case '\r':
        case ' ':
        case '#':
            g_textCursor++;
            break;
        case '!':
        case '$':
        case '%':
            g_textCursor += 2;
            break;
        default:
            stop = -1;
            break;
        }
    } while ( stop == 0 );
    while ( *g_textCursor ) {
        if ( (short)( g_textX + Pause_TextWordWidth( g_textCursor ) ) >= MISSION_RIGHT ) {
            g_textX = g_textLeft;
            g_textY += MISSION_LINE_HEIGHT;
        }
        if ( g_textY >= MISSION_BOTTOM )
            break;
        Pause_DrawMissionTextWord();
    }
}

/*
==================
Pause_DrawOptionBar

Option highlight bar: a 98x18 textured quad tinted by green.
==================
*/
/* name: inferred from that */
void Pause_DrawOptionBar( int x, int y, int green )
{
    POLY_FT4 *poly;
    u_long   *uv;
    short     right;
    short     bottom;

    uv = (u_long *)&g_optionBarUV;
    right = x + OPTION_BAR_WIDTH;
    bottom = y + OPTION_BAR_HEIGHT;
    poly = g_spriteWorkspace;
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
    setPolyFT4( poly );
    setRGB0( poly, 0, green, 0 );
    addPrim( g_renderBuffer, g_spriteWorkspace );
    g_spriteWorkspace = poly + 1;
}

/*
==================
Pause_DrawMapArrow

Blinking player arrow on the map overview.
==================
*/
/* name: inferred from that */
void Pause_DrawMapArrow( void )
{
    POLY_F3 *poly;
    int      x;
    int      y;
    int      sx;
    int      sy;

    if ( g_frameCount & MAP_BLINK ) {
        x = ( g_player.position.vx + g_levelMap.offsetX ) >> MAP_CELL_SHIFT;
        y = ( g_player.position.vz + g_levelMap.offsetZ ) >> MAP_CELL_SHIFT;
        x -= g_mapCenterX - MAP_ZOOM_RADIUS;
        y -= g_mapCenterZ - MAP_ZOOM_RADIUS;
        if ( x >= 0 && x < MAP_ZOOM_CELLS && y >= 0 && y < MAP_ZOOM_CELLS ) {
            poly = g_spriteWorkspace;
            sx = x * MAP_ZOOM_SCALE + MAP_ZOOM_X;
            sy = y * MAP_ZOOM_SCALE + MAP_ZOOM_Y;
            poly->x0 = sx + g_arrowTip.vx;
            poly->y0 = sy + g_arrowTip.vz;
            poly->x1 = sx + g_arrowLeft.vx;
            poly->y1 = sy + g_arrowLeft.vz;
            poly->x2 = sx + g_arrowRight.vx;
            poly->y2 = sy + g_arrowRight.vz;
            setRGB0( poly, MAP_PLAYER_R, MAP_PLAYER_G, MAP_PLAYER_B );
            setPolyF3( poly );
            addPrim( &g_renderBuffer[OT_BACK_SLOT], poly );
            poly++;
            g_spriteWorkspace = poly;
        }
    }
}

/*
==================
Pause_DrawMap

Map overview page: player blip or arrow, the map texture quad (scrolled
when zoomed) and its dark backing tile.
==================
*/
/* name: inferred from that */
void Pause_DrawMap( void )
{
    short     ox;
    short     oy;
    TILE     *tile;
    POLY_FT4 *poly;
    u_long   *uv;
    u_char    u;
    u_char    v;

    ox = MAP_PANEL_X - ( g_levelMap.width >> 1 );
    oy = MAP_PANEL_Y - ( g_levelMap.height >> 1 );
    if ( g_pauseState != PAUSE_MAP ) {
        if ( g_frameCount & MAP_BLINK ) {
            tile = g_spriteWorkspace;
            tile->x0 = ox + ( ( g_player.position.vx + g_levelMap.offsetX ) >> MAP_CELL_SHIFT ) - 1;
            tile->y0 = oy + ( ( g_player.position.vz + g_levelMap.offsetZ ) >> MAP_CELL_SHIFT ) - 1;
            tile->w = MAP_BLIP_SIZE;
            tile->h = MAP_BLIP_SIZE;
            setRGB0( tile, MAP_PLAYER_R, MAP_PLAYER_G, MAP_PLAYER_B );
            setTile( tile );
            addPrim( &g_renderBuffer[OT_BACK_SLOT], tile );
            tile++;
            g_spriteWorkspace = tile;
        }
    } else {
        Pause_DrawMapArrow();
    }
    poly = g_spriteWorkspace;
    uv = (u_long *)&g_mapUV;
    if ( g_pauseState == PAUSE_MAP ) {
        poly->x0 = MAP_ZOOM_X - 2;
        poly->y0 = MAP_ZOOM_Y - 2;
        poly->x1 = poly->x0 + MAP_ZOOM_SIZE;
        poly->y2 = poly->y0 + MAP_ZOOM_SIZE;
    } else {
        poly->x0 = ox;
        poly->y0 = oy;
        poly->x1 = ox + g_levelMap.width - 1;
        poly->y2 = oy + g_levelMap.height - 1;
    }
    poly->y1 = poly->y0;
    poly->x2 = poly->x0;
    poly->x3 = poly->x1;
    poly->y3 = poly->y2;
    *(u_long *)&poly->u0 = *uv++;
    *(u_long *)&poly->u1 = *uv++;
    *(u_long *)&poly->u2 = *uv++;
    *(u_long *)&poly->u3 = *uv;
    if ( g_pauseState == PAUSE_MAP ) {
        u = poly->u0;
        v = poly->v0;
        if ( g_mapCenterX > MAP_ZOOM_RADIUS )
            poly->u0 = poly->u2 = u + ( g_mapCenterX - MAP_ZOOM_RADIUS );
        if ( g_mapCenterX < poly->u1 - MAP_ZOOM_RADIUS )
            poly->u1 = poly->u3 = u + ( g_mapCenterX + MAP_ZOOM_RADIUS );
        if ( g_mapCenterZ > MAP_ZOOM_RADIUS )
            poly->v0 = poly->v1 = v + ( g_mapCenterZ - MAP_ZOOM_RADIUS );
        if ( g_mapCenterZ < poly->v2 - MAP_ZOOM_RADIUS )
            poly->v2 = poly->v3 = v + ( g_mapCenterZ + MAP_ZOOM_RADIUS );
    }
    setRGB0( poly, MAP_TINT_R, MAP_TINT_G, MAP_TINT_B );
    setPolyFT4( poly );
    addPrim( &g_renderBuffer[OT_BACK_SLOT], poly );
    poly++;
    g_spriteWorkspace = poly;
    tile = (TILE *)poly;
    setRGB0( tile, 0, MAP_BACKING_G, 0 );
    setTile( tile );
    tile->x0 = MAP_PANEL_X - MAP_TEXTURE_SIZE / 2 - 1;
    tile->y0 = MAP_PANEL_Y - MAP_TEXTURE_SIZE / 2 - 1;
    tile->w = MAP_TEXTURE_SIZE + 1;
    tile->h = MAP_TEXTURE_SIZE + 1;
    addPrim( &g_renderBuffer[OT_BACK_SLOT], tile );
    tile++;
    g_spriteWorkspace = tile;
}

/*
==================
Pause_DrawItemPage

Item page: spinning item model, then the ammunition count with its own
spinning model for the weapon pages.
==================
*/
/* name: inferred from that */
void Pause_DrawItemPage( short page )
{
    u_char         style[TEXT_STYLE_SIZE] = { 0, TEXT_NORMAL, 0, 0 };
    mapCell_t      cell;
    dispatchRow_t *model;
    short          count;

    cell.palette = 1;
    switch ( page ) {
    case OPTION_AUTO_MAPPER:
        Pause_DrawMap();
        model = g_objectModels[PICKUP_AUTO_MAPPER];
        g_translation.vx = ITEM_VIEW_AUTO_MAPPER_X;
        g_translation.vy = ITEM_VIEW_AUTO_MAPPER_Y;
        g_translation.vz = ITEM_VIEW_AUTO_MAPPER_Z;
        break;
    case OPTION_LAMP:
        g_translation.vx = ITEM_VIEW_LAMP_X;
        g_translation.vy = ITEM_VIEW_LAMP_Y;
        g_translation.vz = ITEM_VIEW_LAMP_Z;
        if ( g_level >= LEVEL_211_LIVING_QUARTERS )
            model = g_objectModels[PICKUP_NIGHT_VISION];
        else
            model = g_objectModels[PICKUP_SHOULDER_LAMP];
        break;
    case OPTION_PISTOL:
        model = g_objectModels[PICKUP_PISTOL];
        g_translation.vx = ITEM_VIEW_PISTOL_X;
        g_translation.vy = ITEM_VIEW_PISTOL_Y;
        g_translation.vz = ITEM_VIEW_PISTOL_Z;
        break;
    case OPTION_SHOTGUN:
        model = g_objectModels[PICKUP_SHOTGUN];
        g_translation.vx = ITEM_VIEW_SHOTGUN_X;
        g_translation.vy = ITEM_VIEW_SHOTGUN_Y;
        g_translation.vz = ITEM_VIEW_SHOTGUN_Z;
        break;
    case OPTION_FLAMETHROWER:
        model = g_objectModels[PICKUP_FLAMETHROWER];
        g_translation.vx = ITEM_VIEW_FLAMETHROWER_X;
        g_translation.vy = ITEM_VIEW_FLAMETHROWER_Y;
        g_translation.vz = ITEM_VIEW_FLAMETHROWER_Z;
        break;
    case OPTION_PULSE_RIFLE:
        model = g_objectModels[PICKUP_PULSE_RIFLE];
        g_translation.vx = ITEM_VIEW_PULSE_RIFLE_X;
        g_translation.vy = ITEM_VIEW_PULSE_RIFLE_Y;
        g_translation.vz = ITEM_VIEW_PULSE_RIFLE_Z;
        break;
    case OPTION_SMART_GUN:
        model = g_objectModels[PICKUP_SMART_GUN];
        g_translation.vx = ITEM_VIEW_SMART_GUN_X;
        g_translation.vy = ITEM_VIEW_SMART_GUN_Y;
        g_translation.vz = ITEM_VIEW_SMART_GUN_Z;
        break;
    case OPTION_BATTERIES:
        g_translation.vx = ITEM_VIEW_BATTERY_X;
        g_translation.vy = ITEM_VIEW_BATTERY_Y;
        g_translation.vz = ITEM_VIEW_BATTERY_Z;
        model = g_objectModels[PICKUP_BATTERY];
        break;
    default:
        return;
    }
    PushMatrix();
    g_modelUvs = g_pickupUvs;
    RotMatrix( &g_modelRotation, &g_modelMatrix );
    SetRotMatrix( &g_modelMatrix );
    TransMatrix( &g_modelMatrix, &g_translation );
    SetTransMatrix( &g_modelMatrix );
    Model_DrawMidInCell( model, &cell );
    g_modelRotation.vy = ( g_modelRotation.vy + ITEM_MODEL_SPIN ) & ANGLE_MASK;
    if ( page == OPTION_PISTOL ) {
        g_ammoModelRotation.vz = ANGLE_180;
        g_translation.vx = AMMO_VIEW_PISTOL_X;
        g_translation.vy = AMMO_VIEW_PISTOL_Y;
        g_translation.vz = AMMO_VIEW_PISTOL_Z;
        model = g_objectModels[PICKUP_PISTOL_CLIP];
        count = g_player.pistolAmmo + g_player.pistolClips * PISTOL_CLIP_ROUNDS;
    } else if ( page == OPTION_SHOTGUN ) {
        model = g_objectModels[PICKUP_SHOTGUN_CARTRIDGES];
        count = g_player.shotgunAmmo;
        g_ammoModelRotation.vz = ANGLE_180;
        g_translation.vx = AMMO_VIEW_SHOTGUN_X;
        g_translation.vy = AMMO_VIEW_SHOTGUN_Y;
        g_translation.vz = AMMO_VIEW_SHOTGUN_Z;
    } else if ( page == OPTION_FLAMETHROWER ) {
        model = g_objectModels[PICKUP_FLAMETHROWER_FUEL];
        count = g_player.flamethrowerFuel;
        g_ammoModelRotation.vz = ANGLE_270;
        g_translation.vx = AMMO_VIEW_FLAMETHROWER_X;
        g_translation.vy = AMMO_VIEW_FLAMETHROWER_Y;
        g_translation.vz = AMMO_VIEW_FLAMETHROWER_Z;
    } else if ( page == OPTION_PULSE_RIFLE ) {
        g_ammoModelRotation.vz = ANGLE_270;
        g_translation.vx = AMMO_VIEW_PULSE_RIFLE_X;
        g_translation.vy = AMMO_VIEW_PULSE_RIFLE_Y;
        g_translation.vz = AMMO_VIEW_PULSE_RIFLE_Z;
        model = g_objectModels[PICKUP_PULSE_RIFLE_CLIP];
        count = g_player.pulseRifleAmmo + g_player.pulseRifleClips * PULSE_RIFLE_CLIP_ROUNDS;
    } else if ( page == OPTION_SMART_GUN ) {
        g_ammoModelRotation.vz = ANGLE_270;
        g_translation.vx = AMMO_VIEW_SMART_GUN_X;
        g_translation.vy = AMMO_VIEW_SMART_GUN_Y;
        g_translation.vz = AMMO_VIEW_SMART_GUN_Z;
        model = g_objectModels[PICKUP_SMART_GUN_AMMO];
        count = g_player.smartGunAmmo + g_player.smartGunClips * SMART_GUN_CLIP_ROUNDS;
    } else {
        g_modelUvs = g_spriteUvs;
        PopMatrix();
        return;
    }
    if ( count == 0 ) {
        Pause_DrawHUDText( (const u_char *)"No ammo available", AMMO_NONE_X, PANEL_TOP, TEXT_NORMAL );
    } else {
        Hud_DrawNumber( AMMO_COUNT_X, PANEL_TOP, count, style );
        Pause_DrawHUDText( (const u_char *)"rounds available", AMMO_TEXT_X, PANEL_TOP, TEXT_NORMAL );
    }
    RotMatrix( &g_ammoModelRotation, &g_modelMatrix );
    SetRotMatrix( &g_modelMatrix );
    TransMatrix( &g_modelMatrix, &g_translation );
    SetTransMatrix( &g_modelMatrix );
    Model_DrawMidInCell( model, &cell );
    g_ammoModelRotation.vy += AMMO_MODEL_SPIN;
    g_modelUvs = g_spriteUvs;
    PopMatrix();
}

/*
==================
Pause_DrawSoundOptions

Volume page: SFX and CD volume bars, EXIT GAME and its confirmation.
==================
*/
/* name: inferred from g_soundOptions, which it draws */
void Pause_DrawSoundOptions( void )
{
    char          sfxBar[VOLUME_BAR_SIZE];
    char          cdBar[VOLUME_BAR_SIZE];
    menuOption_t *option;
    short         i;
    short         y = PANEL_TOP;
    short         x = SOUND_OPTIONS_X;

    for ( i = 0; i < g_sfxVolume; i++ )
        sfxBar[i] = '*';
    sfxBar[i] = 0;
    for ( i = 0; i < g_cddaVolume; i++ )
        cdBar[i] = '*';
    cdBar[i] = 0;
    option = g_soundOptions;
    for ( i = 0; i < SOUND_OPTION_COUNT; i++, option++ ) {
        if ( g_pauseState >= PAUSE_SOUND && i == g_soundOption && ( ( g_frameCount >> OPTION_BLINK_SHIFT ) & 1 ) )
            Pause_DrawHUDText( (const u_char *)option->label, x, y, option->value ? TEXT_BRIGHT : TEXT_DIM );
        else
            Pause_DrawHUDText( (const u_char *)option->label, x, y, option->value ? TEXT_DIM : TEXT_DARK );
        Pause_DrawOptionBar( x - 2, y - 2, option->value ? OPTION_BAR_LIT : OPTION_BAR_UNLIT );
        if ( i == SOUND_SFX_VOLUME && g_sfxVolume )
            Pause_DrawHUDText( (u_char *)sfxBar, x, y + SOUND_BAR_OFFSET_Y, option->value ? TEXT_NORMAL : TEXT_DIM );
        else if ( i == SOUND_CDDA_VOLUME && g_cddaVolume )
            Pause_DrawHUDText( (u_char *)cdBar, x, y + SOUND_BAR_OFFSET_Y, option->value ? TEXT_NORMAL : TEXT_DIM );
        y += SOUND_OPTION_SPACING;
    }
    if ( g_pauseState == PAUSE_EXIT_CONFIRM ) {
        y -= SOUND_BAR_OFFSET_Y;
        Pause_DrawHUDText( (const u_char *)"ARE YOU SURE ?", x, y, TEXT_DIM );
        Pause_DrawHUDText( (const u_char *)( g_exitConfirmed ? "Yes" : "No" ), x + EXIT_ANSWER_X, y, TEXT_BRIGHT );
    }
}

/*
==================
Pause_Draw

Pause options screen: types the option list out, then draws it with the
highlighted option and its availability line.
==================
*/
/* name: inferred from that */
void Pause_Draw( void )
{
    menuOption_t *option;
    short         x;
    short         y;
    u_char        style[TEXT_STYLE_SIZE] = { 0, TEXT_NORMAL, 0, 0 };
    short         i;
    short         selected;
    short         blink;

    if ( g_pauseState == PAUSE_TYPING ) {
        x = OPTIONS_X;
        y = PANEL_TOP;
        option = g_pauseOptions;
        for ( i = 0; i < g_pauseOption; y += OPTION_SPACING, i++, option++ ) {
            Pause_DrawHUDText( option->label, x, y, option->value ? TEXT_DIM : TEXT_DARK );
            Pause_DrawOptionBar( x - 2, y - 2, option->value ? OPTION_BAR_LIT : OPTION_BAR_UNLIT );
        }
        Pause_DrawTextTyped( option->label, x, y );
        Pause_DrawOptionBar( x - 2, y - 2, option->value ? OPTION_BAR_LIT : OPTION_BAR_UNLIT );
        if ( option->label[g_typedLength] == 0 ) {
            g_typedLength = 0;
            if ( ++g_pauseOption >= OPTION_COUNT ) {
                g_pauseOption = 0;
                g_pauseState = PAUSE_BROWSING;
            }
        }
        Pause_DrawMap();
        return;
    }
    x = OPTIONS_X;
    y = PANEL_TOP;
    option = g_pauseOptions;
    blink = ( g_frameCount >> OPTION_BLINK_SHIFT ) & 1;
    for ( i = 0; i < OPTION_COUNT; y += OPTION_SPACING, i++, option++ ) {
        if ( g_pauseState == PAUSE_MAP && i == g_pauseOption )
            Pause_DrawHUDText( option->label, x, y, option->value ? TEXT_BRIGHT : TEXT_DIM );
        else if ( g_pauseState >= PAUSE_SOUND ) {
            if ( i != g_pauseOption )
                Pause_DrawHUDText( option->label, x, y, option->value ? TEXT_DIM : TEXT_DARK );
            else
                Pause_DrawHUDText( option->label, x, y, option->value ? TEXT_BRIGHT : TEXT_DIM );
        } else if ( i == g_pauseOption && blink )
            Pause_DrawHUDText( option->label, x, y, option->value ? TEXT_BRIGHT : TEXT_DIM );
        else
            Pause_DrawHUDText( option->label, x, y, option->value ? TEXT_DIM : TEXT_DARK );
        Pause_DrawOptionBar( x - 2, y - 2, option->value ? OPTION_BAR_LIT : OPTION_BAR_UNLIT );
    }
    Pause_DrawItemPage( g_pauseOption );
    option = &g_pauseOptions[g_pauseOption];
    if ( g_pauseOption < OPTION_MISSION ) {
        if ( option->value == 0 ) {
            selected = 0;
            switch ( g_pauseOption ) {
            case OPTION_AUTO_MAPPER:
            case OPTION_BATTERIES:
                break;
            case OPTION_LAMP:
                if ( g_player.lampTimer || g_player.nightVisionTimer ) {
                    selected = -1;
                    Pause_DrawHUDText( "Selected", STATUS_X, STATUS_Y, TEXT_NORMAL );
                }
                break;
            case OPTION_PISTOL:
                selected = -1;
                Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                break;
            case OPTION_SHOTGUN:
                if ( g_player.flags & PLAYER_HAS_SHOTGUN ) {
                    selected = -1;
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                }
                break;
            case OPTION_FLAMETHROWER:
                if ( g_player.flags & PLAYER_HAS_FLAMETHROWER ) {
                    selected = -1;
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                }
                break;
            case OPTION_PULSE_RIFLE:
                if ( g_player.flags & PLAYER_HAS_PULSE_RIFLE ) {
                    selected = -1;
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                }
                break;
            case OPTION_SMART_GUN:
                if ( g_player.flags & PLAYER_HAS_SMART_GUN ) {
                    selected = -1;
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                }
                break;
            }
            if ( selected == 0 )
                Pause_DrawHUDText( "Not available", STATUS_NOT_AVAILABLE_X, STATUS_Y, TEXT_NORMAL );
        } else {
            selected = 0;
            switch ( g_pauseOption ) {
            case OPTION_AUTO_MAPPER:
                if ( g_pauseState == PAUSE_MAP )
                    selected = -1;
                else
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                break;
            case OPTION_LAMP:
                if ( g_player.lampTimer || g_player.nightVisionTimer ) {
                    selected = -1;
                } else {
                    if ( g_player.lampCharges )
                        Hud_DrawNumber( STATUS_COUNT_X, STATUS_Y, g_player.lampCharges, style );
                    else
                        Hud_DrawNumber( STATUS_COUNT_X, STATUS_Y, g_player.nightVisionCharges, style );
                    Pause_DrawHUDText( "Available", STATUS_COUNT_TEXT_X, STATUS_Y, TEXT_NORMAL );
                }
                break;
            case OPTION_PISTOL:
                if ( g_player.weapon == WEAPON_PISTOL )
                    selected = -1;
                else
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                break;
            case OPTION_SHOTGUN:
                if ( g_player.weapon == WEAPON_SHOTGUN )
                    selected = -1;
                else
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                break;
            case OPTION_FLAMETHROWER:
                if ( g_player.weapon == WEAPON_FLAMETHROWER )
                    selected = -1;
                else
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                break;
            case OPTION_PULSE_RIFLE:
                if ( g_player.weapon == WEAPON_PULSE_RIFLE )
                    selected = -1;
                else
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                break;
            case OPTION_SMART_GUN:
                if ( g_player.weapon == WEAPON_SMART_GUN )
                    selected = -1;
                else
                    Pause_DrawHUDText( "Available", STATUS_X, STATUS_Y, TEXT_NORMAL );
                break;
            case OPTION_BATTERIES:
                Hud_DrawNumber( STATUS_COUNT_X, STATUS_Y, g_player.batteries, style );
                Pause_DrawHUDText( "Available", STATUS_COUNT_TEXT_X, STATUS_Y, TEXT_NORMAL );
                break;
            }
            if ( selected )
                Pause_DrawHUDText( "Selected", STATUS_X, STATUS_Y, TEXT_NORMAL );
        }
    } else if ( g_pauseOption == OPTION_MISSION ) {
        Pause_DrawMissionText();
    } else if ( g_pauseOption == OPTION_SOUND ) {
        Pause_DrawSoundOptions();
    }
}

/*
==================
Pause_Select

Applies the highlighted pause option: map view, lamp, weapon select or exit.
==================
*/
/* name: inferred from its body, which applies the highlighted pause option */
void Pause_Select( void )
{
    menuOption_t *option = &g_pauseOptions[g_pauseOption];

    switch ( g_pauseOption ) {
    case OPTION_AUTO_MAPPER:
        if ( option->value ) {
            g_pauseState = PAUSE_MAP;
            g_mapCenterX = ( g_player.position.vx + g_levelMap.offsetX ) >> MAP_CELL_SHIFT;
            g_mapCenterZ = ( g_player.position.vz + g_levelMap.offsetZ ) >> MAP_CELL_SHIFT;
            if ( g_mapCenterX < MAP_ZOOM_RADIUS )
                g_mapCenterX = MAP_ZOOM_RADIUS;
            if ( g_mapCenterX > g_levelMap.width - MAP_ZOOM_RADIUS )
                g_mapCenterX = g_levelMap.width - MAP_ZOOM_RADIUS;
            if ( g_mapCenterZ < MAP_ZOOM_RADIUS )
                g_mapCenterZ = MAP_ZOOM_RADIUS;
            if ( g_mapCenterZ > g_levelMap.height - MAP_ZOOM_RADIUS )
                g_mapCenterZ = g_levelMap.height - MAP_ZOOM_RADIUS;
        }
        break;
    case OPTION_LAMP:
        if ( option->value && g_player.lampTimer == 0 && g_player.nightVisionTimer == 0 ) {
            if ( g_player.lampCharges ) {
                if ( --g_player.lampCharges == 0 )
                    option->value = 0;
                g_gameFlags |= GAME_FLAG_LAMP | GAME_FLAG_LAMP_FADE_IN;
                g_player.lampTimer = PLAYER_LAMP_TIME;
            } else if ( g_player.nightVisionCharges ) {
                if ( --g_player.nightVisionCharges == 0 )
                    option->value = 0;
                g_gameFlags |= GAME_FLAG_NIGHT_VISION | GAME_FLAG_LAMP_FADE_IN;
                g_player.nightVisionTimer = PLAYER_NIGHT_VISION_TIME;
            }
        }
        break;
    case OPTION_PISTOL:
        if ( g_player.weapon != WEAPON_PISTOL )
            Weapon_SelectSlot( WEAPON_PISTOL );
        break;
    case OPTION_SHOTGUN:
        if ( option->value && g_player.weapon != WEAPON_SHOTGUN )
            Weapon_SelectSlot( WEAPON_SHOTGUN );
        break;
    case OPTION_FLAMETHROWER:
        if ( option->value && g_player.weapon != WEAPON_FLAMETHROWER )
            Weapon_SelectSlot( WEAPON_FLAMETHROWER );
        break;
    case OPTION_PULSE_RIFLE:
        if ( option->value && g_player.weapon != WEAPON_PULSE_RIFLE )
            Weapon_SelectSlot( WEAPON_PULSE_RIFLE );
        break;
    case OPTION_SMART_GUN:
        if ( option->value && g_player.weapon != WEAPON_SMART_GUN )
            Weapon_SelectSlot( WEAPON_SMART_GUN );
        break;
    case OPTION_SOUND:
        g_soundOption = SOUND_SFX_VOLUME;
        g_pauseState = PAUSE_SOUND;
        break;
    }
}

/*
==================
Pause_SelectSoundOptions

Steps the highlighted volume, or asks to confirm EXIT GAME.
==================
*/
/* name: inferred from g_soundOptions */
void Pause_SelectSoundOptions( void )
{
    switch ( g_soundOption ) {
    case SOUND_SFX_VOLUME:
        ++g_sfxVolume;
        if ( g_sfxVolume > VOLUME_MAX )
            g_sfxVolume = 0;
        Menu_SetSoundVolume( g_sfxVolume );
        break;
    case SOUND_CDDA_VOLUME:
        ++g_cddaVolume;
        if ( g_cddaVolume > VOLUME_MAX )
            g_cddaVolume = 0;
        Menu_SetMusicVolume( g_cddaVolume );
        break;
    case SOUND_EXIT_GAME:
        g_pauseState = PAUSE_EXIT_CONFIRM;
        g_exitConfirmed = 0;
        break;
    }
}

/*
==================
Pause_UpdateExitConfirm

Left/right toggle the answer; X answers. Returns -1 to leave the game.
==================
*/
/* name: inferred from "ARE YOU SURE ?" */
int Pause_UpdateExitConfirm( void )
{
    int buttons = g_padPressed;
    if ( ( buttons & PADLleft ) || ( buttons & PADLright ) ) {
        g_exitConfirmed = !g_exitConfirmed;
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
        return 0;
    }
    if ( buttons & PADRdown ) {
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
        if ( g_exitConfirmed ) {
            g_gameFlags |= GAME_FLAG_RESET;
            return -1;
        }
        g_pauseState = PAUSE_SOUND;
    }
    return 0;
}

/*
==================
Pause_SwapDisplayBuffers

Waits for drawing, shows the finished buffer and starts the other one.
==================
*/
/* name: inferred from that */
void Pause_SwapDisplayBuffers( void )
{
    g_graphicsBusy = -1;
    if ( g_graphicsBusy != 0 ) {
        while ( g_graphicsBusy != 0 ) {
        }
    }
    DrawSync( 0 );
    PutDispEnv( &g_display->disp );
    DrawOTag( &g_renderBuffer[OT_LENGTH - 1] );
    g_display = g_display == g_renderWorkspace ? g_displayDefault : g_renderWorkspace;
    g_renderBuffer = g_display->ot;
    g_spriteWorkspace = g_display->prims;
    ClearOTagR( g_renderBuffer, OT_LENGTH );
    AddPrim( &g_renderBuffer[OT_LENGTH - 1], &g_display->draw );
}

/*
==================
Pause_Run

The pause menu loop: reads the pad, draws the menu and handles input per state
until Start, an exit, or Sys_ResetComboCheck ends it.
==================
*/
/* name: inferred from its body, which runs the pause menu loop */
void Pause_Run( void )
{
    short stop;
    short delay;
    int   sound;
    short changed;

    stop = 0;
    Music_Pause();
    delay = 0;
    Snd_MuteAll();
    sound = -1;
    Pause_Init();
    do {
        g_padPrevious = g_padHeld;
        g_padHeld = PadRead( 0 );
        g_padPressed = ~g_padPrevious & g_padHeld;
        if ( Sys_ResetComboCheck( g_padHeld ) )
            stop = -1;
        Pause_Draw();
        Pause_SwapDisplayBuffers();
        Music_Poll();
        if ( delay > 0 && --delay == 0 ) {
            Snd_Stop( sound );
            sound = -1;
        }
        changed = 0;
        if ( g_pauseState == PAUSE_BROWSING ) {
            if ( g_padPressed & PADLup ) {
                Menu_PlayMenuSound( MENU_SOUND_MOVE );
                if ( --g_pauseOption < 0 )
                    g_pauseOption = OPTION_COUNT - 1;
            } else if ( g_padPressed & PADLdown ) {
                Menu_PlayMenuSound( MENU_SOUND_MOVE );
                if ( ++g_pauseOption >= OPTION_COUNT )
                    g_pauseOption = 0;
            } else if ( g_padPressed & PADLright || g_padPressed & PADRdown ) {
                Menu_PlayMenuSound( MENU_SOUND_MOVE );
                Pause_Select();
            }
        } else if ( g_pauseState == PAUSE_MAP ) {
            if ( g_padHeld & PADLup ) {
                if ( g_mapCenterZ > MAP_ZOOM_RADIUS ) {
                    changed = -1;
                    g_mapCenterZ--;
                }
            } else if ( g_padHeld & PADLdown ) {
                if ( g_mapCenterZ < g_levelMap.height - MAP_ZOOM_RADIUS ) {
                    changed = -1;
                    g_mapCenterZ++;
                }
            }
            if ( g_padHeld & PADLleft ) {
                if ( g_mapCenterX > MAP_ZOOM_RADIUS ) {
                    changed = -1;
                    g_mapCenterX--;
                }
            } else if ( g_padHeld & PADLright ) {
                if ( g_mapCenterX < g_levelMap.width - MAP_ZOOM_RADIUS ) {
                    changed = -1;
                    g_mapCenterX++;
                }
            }
            if ( g_padPressed & PADRdown ) {
                Menu_PlayMenuSound( MENU_SOUND_MOVE );
                g_pauseState = PAUSE_BROWSING;
            }
        } else if ( g_pauseState == PAUSE_SOUND ) {
            if ( g_padPressed & PADLup ) {
                Menu_PlayMenuSound( MENU_SOUND_MOVE );
                if ( --g_soundOption < 0 )
                    g_soundOption = SOUND_OPTION_COUNT - 1;
            } else if ( g_padPressed & PADLdown ) {
                Menu_PlayMenuSound( MENU_SOUND_MOVE );
                if ( ++g_soundOption >= SOUND_OPTION_COUNT )
                    g_soundOption = 0;
            } else if ( g_padPressed & PADLleft ) {
                Menu_PlayMenuSound( MENU_SOUND_MOVE );
                g_pauseState = PAUSE_BROWSING;
            } else if ( g_padPressed & PADRdown ) {
                Pause_SelectSoundOptions();
                Menu_PlayMenuSound( MENU_SOUND_MOVE );
            }
        } else if ( g_pauseState == PAUSE_EXIT_CONFIRM ) {
            stop = Pause_UpdateExitConfirm();
        }
        if ( changed ) {
            if ( sound == -1 ) {
                sound = Snd_Play( SOUND_MAP_SCROLL, -1, SPU_VOLUME_MAX );
                delay = MAP_SCROLL_SOUND_TIME;
            }
        } else {
            Snd_Stop( sound );
            sound = -1;
        }
        if ( stop == 0 )
            stop = g_padPressed & PADstart;
    } while ( stop == 0 );
    Snd_Stop( sound );
    Snd_UnmuteAll();
    Music_Unpause();
}
