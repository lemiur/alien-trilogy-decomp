/* Front end: menus, memory card screens, password entry, message box,
 * title and results screens. One translation unit: its .sdata runs from
 * 0x8008cdb8 to 0x8008cf84 and its .text at least from 0x80015f2c to
 * 0x8001bfa0. Functions not yet matched are absent; their data are holes.
 */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LIBETC.H>
#include <LIBPRESS.H>
#include <STRINGS.H>
#include <KERNEL.H>
#include <LIBAPI.H>
#include <MEMCARD.H>
#include <CONVERT.H>
#include <LEVEL.H>
#include <CAMERA.H>
#include <RESULT.H>
#include <SPRITEUV.H>
#include <TASK.H>
#include <MENU.H>
#include <DISPLAY.H>
#include <HEAP.H>
#include <AUDIO.H>
#include <GAME.H>
#include <TEXTURE.H>
#include <TEXT.H>
#include <HUD.H>
#include <SAVE.H>


/* Front-end screens: menu_task runs the one in g_menu_screen. Names invented
 * from each state's handler in menu_task and the callbacks that enter it. */
enum MenuScreen {
    MENU_NONE = 0,
    MENU_PASSWORD_ENTRY = 1,
    MENU_PASSWORD_KEYBOARD = 2,
    MENU_CONTROL_METHOD = 3,
    MENU_START_GAME = 4,
    MENU_FADE_OUT = 5,
    MENU_ENTER_GAME = 6,
    MENU_MAIN = 7,
    MENU_OPTIONS = 8,
    MENU_CHEATS = 9,
    MENU_LOAD_CARD = 10,
    MENU_SAVE_CARD = 11,
    MENU_LOAD_SCAN = 12,
    MENU_SAVE_CHECK = 13,
    MENU_DELETE_SCAN = 14,
    MENU_DELETE_SELECT = 15,
    MENU_DELETE_CONFIRM = 16,
    MENU_FORMAT_CONFIRM = 17,
    MENU_CREDITS = 19,
    MENU_CREDITS_END = 20,
    MENU_LEVEL_PASSWORD = 21,
    MENU_SAVE_SLOTS = 22,
    MENU_LOAD_SLOTS = 23,
    MENU_OVERWRITE_CONFIRM = 24,
    MENU_CONTINUE = 25,
    MENU_CONTINUE_FADE = 26,
    MENU_LEAVE = 27,
    MENU_RESULTS = 28
};



#include <LIBCD.H>
#include <HEAPFILE.H>
#include <MOVIE.H>
#include <TRACK.H>
#include <SPRITE.H>
#include <ACTOR.H>

/* The cheat codes are stored XORed with this key (decode_cheat_code). name invented */
#define CHEAT_CODE_KEY 0x1f
/* Top rows of the main menu with and without its "Cheats" entry, and the row of the control
 * method screen's "Exit" (drawn by draw_exit_label). names invented */
#define MAIN_MENU_Y 185
#define MAIN_MENU_Y_CHEATS 170
#define CONTROL_MENU_EXIT_Y 190
/* compute_level_results: g_level_target_classes entries past a level's target classes. name invented */
#define TARGET_CLASS_NONE 0xff
/* build_result_colors: one colour per percentage 0..100. name invented */
#define RESULT_COLOR_STEPS 101
/* load_mdec_picture decodes and uploads the picture in 16x16 tiles. name invented */
#define MDEC_TILE_SIZE 16
/* identity-TODO: the size start_menu_tasks allocates for g_8cedc, which no matched code reads */
#define MENU_8CEDC_SIZE 0x1e0

/* Entries of each menu screen (the MenuEntry arrays below). names: from the entry labels */
enum { MAIN_START, MAIN_OPTIONS, MAIN_CHEATS };
enum {
    OPTIONS_SOUND, OPTIONS_EFFECTS_VOLUME, OPTIONS_MUSIC_VOLUME, OPTIONS_DIFFICULTY,
    OPTIONS_CONTROLLER, OPTIONS_PASSWORD, OPTIONS_CARD, OPTIONS_CAMERA_SWAY, OPTIONS_CREDITS,
    OPTIONS_EXIT
};
enum {
    CHEATS_LEVEL, CHEATS_INDESTRUCTIBLE, CHEATS_UNLIMITED_AMMO, CHEATS_VIDEO_MODE,
    CHEATS_GIMME_WEAPONS, CHEATS_EXIT
};
enum { PASSWORD_MENU_HEADING, PASSWORD_MENU_TEXT, PASSWORD_MENU_CONTINUE, PASSWORD_MENU_CARD };
enum { CONTROL_HEADING, CONTROL_PICTURE, CONTROL_EXIT };
/* g_save_card_menu and g_load_card_menu; CARD_MENU_TRANSFER is "Save game" or "Load game" */
enum {
    CARD_MENU_HEADING, CARD_MENU_CARD, CARD_MENU_TRANSFER, CARD_MENU_DELETE, CARD_MENU_FORMAT,
    CARD_MENU_EXIT, CARD_MENU_COUNT
};
/* The yes/no screens: g_card_format_menu, g_card_delete_file_menu, g_card_overwrite_menu */
enum { CONFIRM_HEADING, CONFIRM_SUBJECT, CONFIRM_YES, CONFIRM_NO };
enum { DELETE_MENU_HEADING, DELETE_MENU_FILE, DELETE_MENU_EXIT };
/* g_card_load_menu and g_card_save_menu: entry n + 1 is save slot n, then "Exit" */
#define CARD_SLOT_MENU_EXIT (SAVE_SLOT_COUNT + 1)
/* g_menu_cursors: the cursor each screen re-enters at. names invented */
enum {
    MENU_CURSOR_MAIN, MENU_CURSOR_OPTIONS, MENU_CURSOR_CONTROL, MENU_CURSOR_CHEATS,
    MENU_CURSOR_CARD, MENU_CURSOR_COUNT = 6
};
/* The card choice (g_card_choices "None available", "Left", "Right"): a choice is the
 * CARD_PRESENT_* bit of its port. names: from the choice strings */
enum { CARD_CHOICE_NONE, CARD_CHOICE_LEFT, CARD_CHOICE_RIGHT, CARD_CHOICE_COUNT };
/* g_card_status before the first poll, unequal to every card_present_mask result. name invented */
#define CARD_STATUS_UNKNOWN (-2)
/* The Sound entry's choices (g_sound_mode_choices). names: from the choice strings */
enum { SOUND_MODE_EFFECTS, SOUND_MODE_MUSIC, SOUND_MODE_BOTH, SOUND_MODE_NONE };
/* The cheat codes in g_name_table, by the cheat each sets (check_password). names: from those flags */
enum {
    CHEAT_CODE_CHEATS_MENU, CHEAT_CODE_GIMME_WEAPONS, CHEAT_CODE_UNLIMITED_AMMO,
    CHEAT_CODE_INDESTRUCTIBLE
};
#define CHEAT_CODE_BUFFER_SIZE 64
/* A level password: LEVEL_CODE_PREFIX and two digits of the level. names invented */
#define LEVEL_CODE_PREFIX "G0LVL"
#define LEVEL_CODE_PREFIX_LENGTH (sizeof LEVEL_CODE_PREFIX - 1)
#define LEVEL_CODE_TEXT_SIZE 4
/* Front-end task priorities (<TASK.H> TaskPriority lists the game's). names invented */
/* Music tracks of the menus and the credits. names invented */
#define MUSIC_TRACK_MENU 2
#define MUSIC_TRACK_CREDITS 8
/* g_mode_table, g_demo_levels and g_level_target_classes sizes. names invented */
#define MODE_TABLE_SIZE 8
#define DEMO_LEVEL_COUNT 7
#define TARGET_CLASSES_PER_LEVEL 5
/* menu_task: main-menu frames before a demo starts. name invented */
#define DEMO_IDLE_FRAMES 1800
/* Sprite records of the menu pictures (load_sprite_textures loads a file's pictures from a
 * record on; draw_picture draws two records). names invented, from the screens showing them */
enum {
    MENU_PICTURE_TITLE = 5,
    MENU_PICTURE_MENU = 7,
    MENU_PICTURE_RESULTS = 10,
    MENU_PICTURE_BACKGROUND = 12,
    MENU_PICTURE_CONTROLLERS = 14,
    MENU_PICTURE_LOADING = 15,
    MENU_PICTURE_COUNT = 16
};
/* Ordering-table slots of the percentage bars (the menu pictures use OT_BACK_SLOT). names invented */
#define MENU_OT_BAR (OT_LENGTH - 8)
#define MENU_OT_BAR_FRAME (OT_LENGTH - 10)
/* Full shade of a picture or glyph. name invented */
#define MENU_SHADE_FULL 255
/* draw_picture: a 240-pixel square panel and the rest of the screen; draw_picture_panels and
 * draw_translucent_panels: 256-pixel panels placed PANEL_STEP apart. names invented */
#define PICTURE_PANEL_WIDTH 240
#define PICTURE_LAST_PANEL_WIDTH (SCREEN_WIDTH - PICTURE_PANEL_WIDTH)
#define PANEL_WIDTH 256
#define PANEL_STEP (PANEL_WIDTH - 1)
#define PANEL_LAST_WIDTH (SCREEN_WIDTH - PANEL_WIDTH)
/* Picture fades: title_picture_task up to TITLE_SHADE_MAX, menu_fade_task up to
 * MENU_FADE_SHADE_MAX (entering the next screen from MENU_FADE_SHADE_READY), both by
 * PICTURE_FADE_STEP and down to PICTURE_FADE_MIN. names invented */
#define TITLE_SHADE_MAX 140
#define MENU_FADE_SHADE_MAX 75
#define MENU_FADE_SHADE_READY 51
#define PICTURE_FADE_STEP 4
#define PICTURE_FADE_MIN 7
/* fade_step phases and shade steps. names invented */
enum { FADE_IN, FADE_HOLD, FADE_OUT, FADE_DONE };
#define FADE_IN_STEP 4
#define FADE_OUT_STEP 6
#define FADE_SHADE_MAX 200
/* highlight_pulse_task: the highlight's red and blue pulse between these. names invented */
#define HIGHLIGHT_START 200
#define HIGHLIGHT_GREEN 240
#define HIGHLIGHT_MAX 250
#define HIGHLIGHT_MIN 100
/* title_sequence_task steps. names invented, from each step's work */
enum {
    TITLE_START = 0,
    TITLE_RESET = 1,
    TITLE_WAIT = 2,
    TITLE_LOAD = 3,
    TITLE_PAUSE = 4,
    TITLE_BLANK = 5,
    TITLE_LEGAL = 6,
    TITLE_FOX = 7,
    TITLE_ACCLAIM = 8,
    TITLE_PROBE = 10,
    TITLE_INTRO = 11,
    TITLE_PICTURE_START = 12,
    TITLE_PICTURE = 13,
    TITLE_PICTURE_WAIT = 14
};
#define TITLE_SEQUENCE_TICKS 120
#define TITLE_PICTURE_TICKS 60
/* run_menu: a window of up to MENU_WINDOW_LINES entries around the cursor; R1 and R2 change
 * values by MENU_STEP_FAST and MENU_STEP_FASTER with the cheats menu on. names invented */
#define MENU_TEXT_SIZE 128
#define MENU_WINDOW_HALF 5
#define MENU_WINDOW_LINES 10
#define MENU_WINDOW_LIMIT (MENU_WINDOW_LINES + 1)
#define MENU_CENTRE_X (SCREEN_WIDTH / 2)
#define MENU_STEP_FAST 10
#define MENU_STEP_FASTER 100
/* The volume entries' steps scale to the audio volume by this. name invented */
#define MENU_VOLUME_SCALE 10
/* Card screens: polling intervals, attempts at each card operation, the used blocks at which
 * a card is full, the title characters shown, and the rows of their messages. names invented */
#define CARD_POLL_INTERVAL 10
#define CARD_POLL_SLOW 20
#define CARD_ATTEMPTS 5
#define CARD_BLOCKS_FULL 14
#define CARD_TITLE_SHOWN 24
#define CARD_MESSAGE_Y 66
#define SAVE_MESSAGE_Y 70
#define PASSWORD_MESSAGE_Y 160
/* show_message: frames a message stays. name invented */
#define MENU_MESSAGE_TICKS 200
/* format_card_slot: "Section   - Level   " with the digits at these columns; section numbers.
 * names invented */
#define SLOT_TEXT_SECTION 8
#define SLOT_TEXT_LEVEL 18
enum { SECTION_1 = 1, SECTION_2, SECTION_3 };
#define SECTION_2_LEVELS (LEVEL_311_TUNNELS - LEVEL_211_LIVING_QUARTERS)
/* The controller picture of the control method screen. names invented */
#define CONTROLLER_LEFT 32
#define CONTROLLER_TOP 55
#define CONTROLLER_RIGHT 286
#define CONTROLLER_BOTTOM 181
#define CONTROLLER_SHADE_DIM 110
/* The password: the row of its text on the entry screen and the keyboard screen, its lines of
 * PASSWORD_LINE_LENGTH letters. names invented */
#define PASSWORD_TEXT_Y 80
#define KEYBOARD_PASSWORD_Y 180
#define PASSWORD_LINE_LENGTH 16
#define PASSWORD_LINE_SIZE (PASSWORD_LINE_LENGTH + 1)
/* The password keyboard: KEYBOARD_ROWS rows of KEYBOARD_COLUMNS keys (g_password_key_characters),
 * the last row three wide keys; positions, spacing and the cursor brackets. names invented */
#define PASSWORD_KEY_COUNT 36
#define KEYBOARD_ROWS 4
#define KEYBOARD_COLUMNS 11
#define KEYBOARD_LAST_ROW (KEYBOARD_ROWS - 1)
#define KEYBOARD_LAST_COLUMN (KEYBOARD_COLUMNS - 1)
enum { KEY_DELETE = 33, KEY_ACCEPT, KEY_QUIT };
#define WIDE_KEY_COUNT 3
/* password_keyboard_column: columns below WIDE_KEY_SPLIT_LEFT land on Delete, below
 * WIDE_KEY_SPLIT_RIGHT on Accept, the rest on Quit. names invented */
#define WIDE_KEY_SPLIT_LEFT 3
#define WIDE_KEY_SPLIT_RIGHT 7
#define KEY_TEXT_SIZE 16
#define KEYBOARD_TOP 50
#define KEYBOARD_LEFT 30
#define KEY_SPACING 24
#define WIDE_KEY_SPACING 96
#define KEY_BRACKET_OFFSET 12
/* The mission assessment screen (update_mission_assessment, draw_percentage_bar). names invented */
#define ASSESSMENT_TEXT_SIZE 208
#define ASSESSMENT_TITLE_Y 24
#define ASSESSMENT_PROMPT_Y 210
#define ASSESSMENT_LABEL_X 26
#define ASSESSMENT_BAR_X 100
#define ASSESSMENT_PERCENT_X 110
#define ASSESSMENT_ALIENS_BAR_Y 58
#define ASSESSMENT_ALIENS_PERCENT_Y 62
#define ASSESSMENT_ALIENS_Y 70
#define ASSESSMENT_SECRETS_BAR_Y 109
#define ASSESSMENT_SECRETS_PERCENT_Y 113
#define ASSESSMENT_SECRETS_Y 121
#define ASSESSMENT_MISSION_BAR_Y 160
#define ASSESSMENT_MISSION_PERCENT_Y 164
#define ASSESSMENT_MISSION_Y 172
/* The tally ticks every TALLY_TICK_MASK + 1 frames at this pitch. names invented */
#define TALLY_TICK_MASK 7
#define TALLY_TICK_PITCH 3000
/* A percentage bar: segments of BAR_SEGMENT_PERCENT (g_percentage_bar_widths), dimmed to
 * 1 / BAR_DIM at the top, inside a BAR_FRAME_WIDTH x BAR_FRAME_HEIGHT frame. names invented */
#define PERCENT_MAX 100
#define PERCENT_LIMIT (PERCENT_MAX + 1)
#define BAR_SEGMENT_PERCENT 10
#define BAR_SEGMENT_LAST (BAR_SEGMENT_PERCENT - 1)
#define BAR_INSET 5
#define BAR_HEIGHT 34
#define BAR_DIM 4
#define BAR_FRAME_WIDTH 178
#define BAR_FRAME_HEIGHT 35
/* LevelResult.cells: RESULT_COLOR_STEPS colours of RESULT_COLOR_SIZE bytes; build_result_colors
 * ramps red down from RAMP_RED_FADE_START and green up from RAMP_GREEN_LEAD below par.
 * names invented */
#define RESULT_COLOR_SIZE 4
#define RAMP_MAX 255
#define RAMP_RED_FADE_START 76
#define RAMP_GREEN_LEAD 10
#define RAMP_STEP 15
#define RAMP_STEP_FLOOR 16
#define RAMP_STEP_CEILING 240
/* level_complete: the par percentage of each section and the grades. names invented */
#define PAR_SECTION_1 15
#define PAR_SECTION_2 25
#define PAR_SECTION_3 45
#define GRADE_GOOD_PERCENT 85
enum { GRADE_FAILED, GRADE_PASSED, GRADE_GOOD, GRADE_BEST };
/* The message box: its left margin and line height, the cell of '$' indents and spaces,
 * the save rectangle cell, '%' pauses in units of MESSAGE_PAUSE_TICKS, the blinking cursor,
 * and the colour entry of button glyphs. names invented */
#define MESSAGE_BOX_LEFT 10
#define MESSAGE_BOX_LINE_HEIGHT 10
#define MESSAGE_BOX_CELL 10
#define MESSAGE_BOX_BRIEF_Y 16
#define MESSAGE_BOX_MISSION_Y 48
#define MESSAGE_CELL_WIDTH 8
#define MESSAGE_PAUSE_TICKS 10
#define MESSAGE_CURSOR_BLINK 8
#define MESSAGE_PAGE_BUTTON 3
/* The loading screen's "* Loading data *" and the continue prompt: their row, colour and
 * the strip of the prompt kept below the second buffer. names invented */
#define PROMPT_Y 220
#define LOADING_TEXT_X 16
#define LOADING_TEXT_RED 168
#define LOADING_TEXT_GREEN 128
#define LOADING_TEXT_BLUE 128
#define CONTINUE_STRIP_Y 460
#define CONTINUE_STRIP_HEIGHT 18
/* load_mission_text: the last byte of g_mission_text. name invented */
#define MISSION_TEXT_LAST (MISSION_TEXT_SIZE - 1)
/* Credits: a line task starts at row CREDITS_LINE_START and moves up a row a frame, fading in
 * from CREDITS_FADE_IN_FROM and out below CREDITS_FADE_OUT_FROM; credits_task starts a line
 * every CREDITS_LINE_INTERVAL frames. names invented */
#define CREDITS_LINE_START 272
#define CREDITS_LINE_END 5
#define CREDITS_FADE_IN_FROM 181
#define CREDITS_FADE_IN_BASE 240
#define CREDITS_FADE_OUT_FROM 20
#define CREDITS_FADE_OUT_SCALE 12
#define CREDITS_LINE_INTERVAL 16
#define CREDITS_FIRST_DELAY 18
#define CREDITS_END_LENGTH (sizeof "END" - 1)




/* Two-byte string: an opening brace and its terminator. */
typedef struct { char brace, zero; } Token;


/* Runtime library and other units. */
extern void format_card_slot(short slot, char *destination, int second, void *context);
extern void title_sequence_task(TASK *);

/* name: inferred from use: scan_card_slots fills one entry per save slot through save_slot_levels (-1 when empty) and save_card_slot stores the saved level */
extern short g_card_slot_levels[];
/* name: runtime evidence, analysis/runtime/globals.json: run_menu points it at g_text_color or
 * g_highlight_color for each entry it draws */
extern const unsigned char *g_menu_entry_color;
/* name: inferred from use: read_card_directory stores the title of each save it finds on the card; the delete menu lists them */
extern const char *g_card_block_names[];
extern int g_6c07c;
/* Sprite texture words (u0v0+clut, u1v1+tpage, u2v2, u3v3) copied into
 * POLY_FT4 packets by draw_controller_picture and filled by sprite_file_load; name invented. */
/* The CD file table: 171 entries, each a name and a CdlFILE. */
/* name: runtime evidence, analysis/runtime/globals.json: the MoveImage source rect 240 lines
 * below the message box, where the screen under the box is kept */
extern RECT g_message_box_save_rect;
/* name: runtime evidence, analysis/runtime/globals.json: sprite_file_load fills it from \GFX\ICONS.IFF (file 0xa3) */
extern unsigned char g_icon_uvs[];

/* Functions of this unit referenced before their definitions. */
extern char *format_label(char *destination, const MenuEntry *entry);
extern void credits_start(void);
extern void format_choice(char *destination, MenuEntry *entry, void *context);
extern void format_volume_bar(char *destination, MenuEntry *entry);
extern void format_card_choice(char *destination, MenuEntry *entry, void *context);
extern void draw_password_entry(char *destination);
extern void format_card_slot_heading(char *destination);
extern char *format_card_file_name(char *destination);
extern void format_decimal(char *destination, short value);
extern short update_mission_assessment(void);
extern void title_picture_task(TASK *task);
extern void draw_exit_label(char *destination, void *unused, TASK *record);
extern void draw_controller_picture(char *destination, MenuEntry *choice, TASK *state);
extern void menu_back_to_next_state(void *unused, TASK *record);
extern void menu_back(void *unused, TASK *record);
extern void open_options_menu(void *unused, TASK *record);
extern void start_card_save(void *unused, TASK *record);
extern void start_card_load(void *unused, TASK *record);
extern void start_card_delete(void *unused, TASK *record);
extern void open_card_format_menu(void *unused, TASK *record);
extern void select_card_file(void *unused, TASK *record);
extern void select_card_save_slot(void *context, TASK *record);
extern void open_cheats_menu(void *unused, TASK *record);
extern void open_password_entry(void *unused, TASK *record);
extern void open_card_menu(void *unused, TASK *record);
extern void open_control_method_menu(void *unused, TASK *record);
extern void select_video_mode(const MenuEntry *choice);
extern void start_game(void);
extern void start_new_game(void);
extern void continue_game(void *unused, TASK *record);
extern void select_music_volume(void *unused, TASK *record);
extern void select_sound_volume(void *unused, TASK *record);
extern void change_music_volume(const MenuEntry *choice);
extern void update_card_choice(MenuEntry *choice);
extern void change_sound_volume(const MenuEntry *choice);
extern int change_sound_mode();
extern void open_credits(void *unused, TASK *record);
extern void format_selected_card(void);
extern void delete_selected_card_file(void);
extern void show_message(char *message, int unused, int y);
extern void menu_message_task(TASK *);
extern void load_card_slot(int selection);
extern void format_card_slot_0(int first, int second, void *context);
extern void format_card_slot_1(int first, int second, void *context);
extern void format_card_slot_2(int first, int second, void *context);
extern void format_card_slot_3(int first, int second, void *context);
extern void format_selected_card_slot(int first, int second, void *context);
extern void load_card_slot_0(void);
extern void load_card_slot_1(void);
extern void load_card_slot_2(void);
extern void load_card_slot_3(void);
extern void save_card_slot(short mode);
extern void confirm_overwrite(void *unused);
extern void draw_password_text(int unused, int y);
extern void load_mission_text(unsigned short index);
extern void open_message_box(short level, short mode);
extern void wait_message_box(void);
extern void show_message_box(short wait);
extern void load_mdec_picture(short index, RECT *area);
extern void load_sprite_textures(int id, short index);
extern void draw_picture(short index, u_char shade);
extern void menu_fade_task(TASK *task);
extern void highlight_pulse_task(TASK *task);
extern void clear_vram_area(void);
extern void start_menu_tasks(void);
extern short percentage(short denominator, short value);
extern short compute_level_results(LevelResult *result, short level);
extern short f_1b674(short value);
extern void build_result_colors(LevelResult *result);
extern void credits_line_task(TASK *node);
extern unsigned short poll_card_status(short timer, short interval);
extern short selected_card_port(void);
extern short decode_cheat_code(short index, char *destination);
extern short check_password(void);
extern int password_keyboard_column(short value, short mode);
extern short read_card_directory(const char *title, MenuCallback select);

/* Initialized data, in retail order. GCC emits an initializer's string
 * literals in reverse element order: those of eight bytes or less go to
 * .sdata, longer ones to .rdata. */

static char s_no_message[] = "NO MESSAGE";
static unsigned char s_loading_data[] = "* Loading data *";
static unsigned char s_press_to_continue[] = "Press !0 button to Continue...";
/* mission briefing header */
/* name: analysis/runtime/globals.json (round 2) */
static char g_mission_brief_header[] = "&1Incoming data transfer %2.%2.%2.\r\r&1Mission brief:\r";

/* The message box: idle (scroll and choice -1) until open_message_box. */
static MessageBox g_message_box = { 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0 };

/* mode_in_table searches all eight entries for the requested mode. */
short g_mode_table[MODE_TABLE_SIZE] = { 2, 5, 7, 9, 16, 19, 27, 30 };

/* Message box text colours and the matching second colours draw_glyph_now takes, by text page
 * (MessageBox.page); entry 3 also highlights the "press" glyph. names: invented */
/* name: analysis/runtime/globals.json (round 2) */
static unsigned char g_message_box_colours[][4] = {
    { 0, 90, 0, 0 }, { 0, 255, 0, 0 }, { 0, 80, 0, 0 }, { 200, 200, 200, 0 }, { 80, 0, 0, 0 }
};
/* name: analysis/runtime/globals.json (round 2) */
static unsigned char g_message_box_shadow_colours[][4] = {
    { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 200, 200, 200, 0 }, { 150, 150, 150, 0 }
};

/* Picture pages and CLUTs of the 16 menu pictures (load_sprite_textures); the last is
 * the loading screen's (show_loading_screen). */
static TexturePage g_texture_records[MENU_PICTURE_COUNT] = {
    { 0, 0x280, 0x0, 0, 1, 0 }, { 0, 0x300, 0x0, 0, 1, 0 },
    { 0, 0x380, 0x0, 0, 1, 0 }, { 0, 0x0, 0x100, 0, 1, 0 },
    { 0, 0x80, 0x100, 0, 1, 0 }, { 0, 0x180, 0x100, 0, 1, 0 },
    { 0, 0x200, 0x100, 0, 1, 0 }, { 0, 0x300, 0x100, 0, 1, 0 },
    { 0, 0x380, 0x100, 0, 1, 0 }, { 0, 0x140, 0x0, 0, 1, 0 },
    { 0, 0x140, 0x0, 0, 1, 0 }, { 0, 0x140, 0x100, 0, 1, 0 },
    { 0, 0x240, 0x0, 0, 1, 0 }, { 0, 0x340, 0x0, 0, 1, 0 },
    { 0, 0x300, 0x0, 0, 1, 0 }, { 0, 0x380, 0x0, 0, 1, 0 }
};
static ClutRow g_palette_records[MENU_PICTURE_COUNT] = {
    { 0, 0, 0xf2, 0 }, { 0, 0, 0xf3, 0 }, { 0, 0, 0xf4, 0 }, { 0, 0, 0xf5, 0 },
    { 0, 0, 0xf6, 0 }, { 0, 0, 0x1fc, 0 }, { 0, 0, 0x1fd, 0 }, { 0, 0, 0x1fe, 0 },
    { 0, 0, 0x1ff, 0 }, { 0, 0, 0xfc, 0 }, { 0, 0, 0x1fe, 0 }, { 0, 0, 0x1ff, 0 },
    { 0, 0, 0x1fc, 0 }, { 0, 0, 0x1fd, 0 }, { 0, 0, 0x1fa, 0 }, { 0, 0, 0x1fb, 0 }
};

/* name: inferred from use: menu_task loads the next demo level from it */
static short g_demo_levels[DEMO_LEVEL_COUNT] = { -1, 0, -1, 12, -1, 22, 0 };

/* Per level, the target classes compute_level_results counts (indices of Player.tallies and
 * LevelScreen.kind_counts; identity-TODO: no kind enum yet), TARGET_CLASS_NONE = none. */
/* name: analysis/runtime/globals.json (round 2) */
static unsigned char g_level_target_classes[][TARGET_CLASSES_PER_LEVEL] = {
    { 0x14, 0x19, 0x17, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 0 */
    { 0x16, 0x18, 0x1b, 0x1a, TARGET_CLASS_NONE }, /* level 1 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 2 */
    { 0x16, 0x18, 0x1b, 0x1a, TARGET_CLASS_NONE }, /* level 3 */
    { 0x23, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 4 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 5 */
    { 0x14, 0x19, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 6 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 7 */
    { 0x0d, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 8 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 9 */
    { 0x0a, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 10 */
    { 0x07, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 11 */
    { 0x15, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 12 */
    { 0x23, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 13 */
    { 0x22, 0x21, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 14 */
    { 0x13, 0x11, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 15 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 16 */
    { 0x14, 0x19, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 17 */
    { 0x12, 0x10, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 18 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 19 */
    { 0x21, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 20 */
    { 0x07, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 21 */
    { 0x22, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 22 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 23 */
    { 0x05, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 24 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 25 */
    { 0x05, 0x23, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 26 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 27 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 28 */
    { 0x0d, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 29 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 30 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 31 */
    { 0x01, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 32 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 33 */
    { 0x07, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }  /* level 34 */
};

/* Level-select choices of the cheats menu. */
const char *const g_level_names[] = {
    "111 Entrance", "112 Outer complex", "113 Ammunition dump",
    "122 Recreation rooms", "131 Med. lab", "114 Ammunition dump 2",
    "141 Garage", "115 Ammunition dump 3", "154 Atmosphere processor",
    "155 Security catwalks", "161 Atmosphere basement", "162 Queens lair",
    "211 Living quarters", "212 Canteen", "213 Meeting tower",
    "222 Leadworks", "242 Tunnels and ducts 1", "231 Mining and smelting",
    "232 Furnace controls", "243 Tunnels and ducts 2", "262 Lead mould",
    "263 Queens lair", "311 Tunnels", "321 Pilots chambers 1",
    "331 Canyons and catacombs", "322 Pilots chambers 2", "351 Secrets",
    "352 Inorganics 1", "323 Pilots chambers 3", "371 Drop lifts",
    "353 Inorganics 2", "324 Pilots chambers 4", "381 Egg chambers",
    "325 Pilots chambers 5", "391 Queens lair"
};

/* name: inferred from use: start_menu_tasks creates it with the render callback menu_display_task and the screen-clear routines pause it through pad[0] */
TASK *g_render_task = 0;
/* name: inferred from use: run_menu draws the entry under the cursor in it and highlight_pulse_task pulses it */
unsigned char g_highlight_color[4] = {128, 255, 128, 0};
/* name: inferred from use: run_menu draws every other enabled entry in it */
unsigned char g_text_color[4] = {50, 200, 50, 0};
/* name: inferred from use: run_menu uses it for entries with flag 4 (greyed out) */
unsigned char g_disabled_text_color[4] = {30, 30, 30, 0};
/* name: inferred from use: run_menu uses it for a greyed-out entry under the cursor */
unsigned char g_disabled_highlight_color[4] = {60, 60, 60, 0};
/* name: inferred from use: menu_message_task draws the notification text in it */
unsigned char g_message_color[4] = {255, 60, 0, 0};
/* name: inferred from use: check_password sets it from a level password, menu_task from the demo list and the cheats menu, start_new_game starts that level */
short g_start_level = 0;
/* name: inferred from use: poll_card_status stores the memory card poll result (card_present_mask) here */
short g_card_status = 0;
/* name: inferred from use: poll_card_status compares it with the new poll result */
short g_card_status_previous = -1;
/* name: inferred from use: the delete and save-slot callbacks store the chosen file or slot here */
short g_card_file_index = -1;
/* name: inferred from use: menu_task switches on it to run the current screen */
unsigned int g_menu_screen = MENU_NONE;
/* name: inferred from use: menu_task compares it with g_menu_screen to detect a screen change */
int g_menu_screen_previous = 0;
/* name: inferred from use: menu_fade_task enters this state once the fade-in completes */
int g_menu_next_state = MENU_NONE;
/* name: inferred from use: the Exit and error paths return to this state */
int g_menu_return_state = MENU_NONE;
/* name: inferred from use: selected_card_port reads the save menu when it is set and the load menu otherwise */
short g_card_menu_saving = 0;
/* Front-end sequence flag: menu_task sets 1 at the end of the demo list, title_sequence_task tests 2. */
/* name: analysis/runtime/globals.json (round 2) */
short g_title_intro_state = 0;
/* name: inferred from use: menu_task counts main-menu frames here and starts a demo after 1800 */
short g_menu_idle_frames = 0;
/* name: inferred from use: menu_task steps it through the seven-entry demo level list */
unsigned char g_demo_index = 0;
/* name: inferred from use: menu_task starts the menu picture and music once and then sets it */
short g_menu_music_started = 0;

/* Choice lists. Unsized declarators place each array in .data; GCC 2.5.7
 * sizes an unsized array only after choosing the section, so -G8 does not
 * apply to the two-entry lists. */
const char *const g_sound_mode_choices[] = { "SFX", "Music", "SFX and music", "None" };
const char *const g_boolean_choices[] = { "OFF", "ON" };
const char *const g_difficulty_choices[] = {
    "Acid Reign {", "Raging Terror {{", "Xenomania {{{"
};

/* Debug track and movie lists with no remaining reader. g_track_numbers
 * pairs with g_track_names entry for entry; -1 is "Stop". */
const char *const g_track_names[] = {
    "Stop", "Track 2", "Track 3", "Track 4", "Track 5", "Track 6",
    "Track 7", "Track 8", "Track 9", "Track 10", "Track 11", "Track 12",
    "Track 13"
};
short g_track_numbers[] = { -1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24 };
const char *const g_movie_names[] = {
    "Probe", "Intro", "Death1", "Death2", "Death3", "Death4", "Death5",
    "Death6", "Acclaim", "Legal", "Bridge1", "Bridge2", "Game Over"
};

const char *const g_card_choices[] = { "None available", "Left", "Right" };
const char *const g_video_choices[] = { "NTSC", "PAL" };

/* Saved cursor values restored into menu records on entry. */
short g_menu_cursors[MENU_CURSOR_COUNT] = {0};

/* name: inferred from use: show_message stores its task, menu_message_close ends it */
TASK *g_menu_message_task = 0;
/* name: inferred from use: start_menu_tasks allocates it and read_card_directory reads the card directory into it */
struct DIRENTRY *g_card_files = 0;
void *g_8cedc = 0;
/* name: inferred from use: start_menu_tasks allocates it and read_card_directory reads each save header into it */
void *g_card_headers = 0;
/* name: inferred from use: menu_task stores the directory count from read_card_directory here */
short g_card_file_count = 0;
/* Label pointers; menu_task passes g_label_delete_file to read_card_directory.
 * names: runtime evidence, analysis/runtime/globals.json (never written; each points at its text) */
const char *g_label_delete_file = "DELETE FILE";
const char *g_label_load_file = "LOAD FILE";
const char *g_label_exit = "Exit";
const char *g_label_unreadable_file = "Unreadable file";
/* name: inferred from use: password_keyboard_input appends and deletes password characters at this index */
short g_password_length = 0;
/* name: inferred from use: set_sound_mode switches the audio setup when this mode changes */
short g_sound_mode = SOUND_MODE_BOTH;

/* Password keyboard: 36 keys. */
char g_password_key_characters[PASSWORD_KEY_COUNT] = "BCDFGHJKLMNPQRSTVWXYZ0123456789?*xyz";

/* One array per menu screen; the menu code passes each base and count. */
MenuEntry g_options_menu[] = {
    { MENU_ENTRY_ACTIVE, 26, 2, 4, "Sound: ", g_sound_mode_choices,
      (MenuCallback)format_choice, 0, (MenuCallback)change_sound_mode },
    { MENU_ENTRY_ACTIVE, 0, 5, 11, "Sound effects volume ", 0,
      (MenuCallback)format_volume_bar, (MenuCallback)select_sound_volume, (MenuCallback)change_sound_volume },
    { MENU_ENTRY_ACTIVE, 0, 5, 11, "Music volume ", 0,
      (MenuCallback)format_volume_bar, (MenuCallback)select_music_volume, (MenuCallback)change_music_volume },
    { MENU_ENTRY_ACTIVE, 0, 1, 3, "Difficulty: ", g_difficulty_choices,
      (MenuCallback)format_choice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Controller options", 0,
      (MenuCallback)format_label, (MenuCallback)open_control_method_menu, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Enter password", 0,
      (MenuCallback)format_label, (MenuCallback)open_password_entry, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Memory Card management", 0,
      (MenuCallback)format_label, (MenuCallback)open_card_menu, 0 },
    { MENU_ENTRY_ACTIVE, 0, 1, 2, "Camera sway ", g_boolean_choices,
      (MenuCallback)format_choice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Display credits", 0,
      (MenuCallback)format_label, (MenuCallback)open_credits, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0,
      (MenuCallback)format_label, (MenuCallback)menu_back_to_next_state, 0 }
};

MenuEntry g_cheats_menu[] = {
    { MENU_ENTRY_ACTIVE, 28, 0, 35, "Level: ", g_level_names,
      (MenuCallback)format_choice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 2, "Player indestructible: ", g_boolean_choices,
      (MenuCallback)format_choice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 2, "Unlimited ammunition: ", g_boolean_choices,
      (MenuCallback)format_choice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 2, "Video Mode: ", g_video_choices,
      (MenuCallback)format_choice, 0, (MenuCallback)select_video_mode },
    { MENU_ENTRY_ACTIVE, 0, 0, 2, "Gimme weapons: ", g_boolean_choices,
      (MenuCallback)format_choice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0,
      (MenuCallback)format_label, (MenuCallback)menu_back_to_next_state, 0 }
};

MenuEntry g_main_menu[] = {
    { MENU_ENTRY_ACTIVE, MAIN_MENU_Y, 0, 0, "Start game", 0,
      (MenuCallback)format_label, (MenuCallback)start_new_game, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Options", 0,
      (MenuCallback)format_label, (MenuCallback)open_options_menu, 0 },
    { 0, 0, 0, 0, "Cheats", 0,
      (MenuCallback)format_label, (MenuCallback)open_cheats_menu, 0 }
};

MenuEntry g_password_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "LEVEL PASSWORD", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_SPACE_TRIPLE, 0, 0, 0, 0, 0,
      (MenuCallback)draw_password_entry, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Continue game", 0,
      (MenuCallback)format_label, (MenuCallback)continue_game, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Memory Card management", 0,
      (MenuCallback)format_label, (MenuCallback)open_card_menu, 0 }
};

MenuEntry g_control_method_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_TRIPLE, 30, 0, 0, "SELECT CONTROL METHOD", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_ACTIVE | MENU_ENTRY_SPACE_TRIPLE, CONTROL_MENU_EXIT_Y, 1, 4, 0, 0,
      (MenuCallback)draw_controller_picture, 0, 0 },
    { MENU_ENTRY_ACTIVE | MENU_ENTRY_SPACE_TRIPLE, 0, 0, 0, 0, 0,
      (MenuCallback)draw_exit_label, (MenuCallback)open_options_menu, 0 }
};

MenuEntry g_password_prompt_menu[] = {
    { MENU_ENTRY_ACTIVE, 200, 0, 0, "Enter password", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0,
      (MenuCallback)format_label, (MenuCallback)open_options_menu, 0 }
};

MenuEntry g_save_card_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "MEMORY CARD OPTIONS", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 4, "Selected Memory Card: ", g_card_choices,
      (MenuCallback)format_card_choice, 0, (MenuCallback)update_card_choice },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Save game", 0,
      (MenuCallback)format_label, (MenuCallback)start_card_save, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Delete block from Memory Card", 0,
      (MenuCallback)format_label, (MenuCallback)start_card_delete, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Format Memory Card", 0,
      (MenuCallback)format_label, (MenuCallback)open_card_format_menu, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0,
      (MenuCallback)format_label, (MenuCallback)menu_back_to_next_state, 0 }
};

MenuEntry g_load_card_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "MEMORY CARD OPTIONS", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 4, "Selected Memory Card: ", g_card_choices,
      (MenuCallback)format_card_choice, 0, (MenuCallback)update_card_choice },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Load game", 0,
      (MenuCallback)format_label, (MenuCallback)start_card_load, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Delete block from Memory Card", 0,
      (MenuCallback)format_label, (MenuCallback)start_card_delete, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Format Memory Card", 0,
      (MenuCallback)format_label, (MenuCallback)open_card_format_menu, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0,
      (MenuCallback)format_label, (MenuCallback)open_options_menu, 0 }
};

/* Card prompts; octal characters are the controller-button glyphs. */
MenuEntry g_card_format_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING, 50, 0, 0, "FORMAT MEMORY CARD IN", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 4, 0, 0,
      (MenuCallback)format_card_slot_heading, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 0, "YES", 0,
      (MenuCallback)format_label, (MenuCallback)format_selected_card, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "NO", 0,
      (MenuCallback)format_label, (MenuCallback)menu_back, 0 }
};

MenuEntry g_card_delete_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_TRIPLE, 50, 0, 0, "SELECT BLOCK TO DELETE", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, g_card_block_names,
      (MenuCallback)format_card_file_name, (MenuCallback)select_card_file, 0 },
    { MENU_ENTRY_ACTIVE | MENU_ENTRY_SPACE_TRIPLE, 0, 0, 0, "Exit", 0,
      (MenuCallback)format_label, (MenuCallback)menu_back, 0 },
    { MENU_ENTRY_VISIBLE, 0, 0, 0, "Use \376 and \375 to select block", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_VISIBLE, 0, 0, 0, "Press \377 to delete block", 0,
      (MenuCallback)format_label, 0, 0 }
};

MenuEntry g_card_delete_file_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_TRIPLE, 50, 0, 0, "DELETE FILE", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, 0, 0,
      (MenuCallback)format_card_file_name, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 0, "YES", 0,
      (MenuCallback)format_label, (MenuCallback)delete_selected_card_file, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "NO", 0,
      (MenuCallback)format_label, (MenuCallback)start_card_delete, 0 }
};

MenuEntry g_card_overwrite_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_TRIPLE, 50, 0, 0, "UPDATE SAVE GAME", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING, 50, 0, 0, 0, 0,
      (MenuCallback)format_selected_card_slot, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 0, "YES", 0,
      (MenuCallback)format_label, (MenuCallback)confirm_overwrite, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "NO", 0,
      (MenuCallback)format_label, (MenuCallback)start_card_save, 0 }
};

MenuEntry g_card_load_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "SELECT GAME TO LOAD", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0,
      (MenuCallback)format_card_slot_0, (MenuCallback)load_card_slot_0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0,
      (MenuCallback)format_card_slot_1, (MenuCallback)load_card_slot_1, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0,
      (MenuCallback)format_card_slot_2, (MenuCallback)load_card_slot_2, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0,
      (MenuCallback)format_card_slot_3, (MenuCallback)load_card_slot_3, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0,
      (MenuCallback)format_label, (MenuCallback)menu_back, 0 }
};

MenuEntry g_card_save_menu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "SELECT GAME SAVE POSITION", 0,
      (MenuCallback)format_label, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0,
      (MenuCallback)format_card_slot_0, (MenuCallback)select_card_save_slot, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0,
      (MenuCallback)format_card_slot_1, (MenuCallback)select_card_save_slot, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0,
      (MenuCallback)format_card_slot_2, (MenuCallback)select_card_save_slot, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0,
      (MenuCallback)format_card_slot_3, (MenuCallback)select_card_save_slot, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0,
      (MenuCallback)format_label, (MenuCallback)menu_back, 0 }
};

/* The request play_menu_sound fills: full volume, centre balance, default pitch. */
/* name: inferred from use: play_menu_sound stores the sample in it and passes it to sound_play_request */
PlayRequest g_menu_sound_request = { 0, SPU_VOLUME_MAX / 2, 0, -1, 0 };

/* The cheat codes, each XORed with CHEAT_CODE_KEY (decode_cheat_code). */
static unsigned char g_name_table[][22] = {
    ".X/KO.QT'\\.[]//KL/Q2",
    "[F.QXK/Q.XWK2",
    "]/G/IKM.\\TL2",
    "/]S.I./QKM.O",
    "K/Y.Q.LW/YY2"
};
/* Sprite of each controller layout (draw_controller_picture), less 14. */
/* name: analysis/runtime/globals.json (round 2) */
static short g_controller_layout_sprites[] = { 0, 2, 3, 1 };
/* Widths of the ten percentage bar segments (draw_percentage_bar). */
/* name: analysis/runtime/globals.json (round 2) */
static short g_percentage_bar_widths[] = { 18, 18, 17, 17, 17, 16, 16, 17, 18, 18, 0 };

/* name: inferred from its body, which plays a sample through the menu PlayRequest */
void play_menu_sound(int sample)
{
    PlayRequest *request = &g_menu_sound_request;
    request->sample = (short)sample;
    sound_play_request(request);
}

/* name: inferred from its body, which copies the entry's label (the format callback of plain menu entries) */
char *format_label(char *destination, const MenuEntry *entry)
{
    return strcpy(destination, entry->label);
}

/* name: inferred from its body, which polls card_present_mask every interval frames and enables or greys out the card entries */
unsigned short poll_card_status(short timer, short interval)
{
    unsigned short i;

    timer++;
    if (timer > interval) {
        timer = 0;
        g_card_status_previous = g_card_status;
        g_card_status = card_present_mask();
        if (g_card_status_previous != g_card_status) {
            switch (g_card_status) {
            case 0:
                g_options_menu[OPTIONS_CARD].flags = MENU_ENTRY_GREYED;
                g_password_menu[PASSWORD_MENU_CARD].flags = MENU_ENTRY_GREYED;
                g_load_card_menu[CARD_MENU_CARD].flags = MENU_ENTRY_VISIBLE;
                g_load_card_menu[CARD_MENU_EXIT].flags = MENU_ENTRY_ACTIVE;
                for (i = CARD_MENU_TRANSFER; i < CARD_MENU_EXIT; i++)
                    g_load_card_menu[(short)i].flags = MENU_ENTRY_GREYED;
                for (i = CARD_MENU_TRANSFER; i < CARD_MENU_EXIT; i++)
                    g_save_card_menu[(short)i].flags = MENU_ENTRY_GREYED;
                break;
            case CARD_PRESENT_PORT0:
            case CARD_PRESENT_PORT1:
            case CARD_PRESENT_BOTH:
                g_options_menu[OPTIONS_CARD].flags = MENU_ENTRY_ACTIVE;
                g_password_menu[PASSWORD_MENU_CARD].flags = MENU_ENTRY_ACTIVE;
                for (i = CARD_MENU_CARD; i < CARD_MENU_COUNT; i++)
                    g_load_card_menu[(short)i].flags = MENU_ENTRY_ACTIVE;
                for (i = CARD_MENU_TRANSFER; i < CARD_MENU_EXIT; i++)
                    g_save_card_menu[(short)i].flags = MENU_ENTRY_ACTIVE;
                break;
            }
        }
    }
    return timer;
}

/* name: inferred from its body, which returns 0 or 1 for the card chosen in the save or load menu, -1 for none */
short selected_card_port(void)
{
    short mode;
    short result = -1;
    if (g_card_menu_saving != 0)
        mode = g_save_card_menu[CARD_MENU_CARD].value;
    else
        mode = g_load_card_menu[CARD_MENU_CARD].value;
    if (mode == CARD_CHOICE_LEFT)
        result = 0;
    if (mode == CARD_CHOICE_RIGHT)
        result = 1;
    return result;
}

/* name: inferred from its body, which writes "LEFT"/"RIGHT" and " MEMORY CARD SLOT" */
void format_card_slot_heading(char *destination)
{
    short mode = selected_card_port();
    switch (mode) {
    case 0:
        memcpy(destination, "LEFT", sizeof "LEFT");
        break;
    case 1:
        memcpy(destination, "RIGHT", sizeof "RIGHT");
        break;
    default:
        destination[0] = 0;
        return;
    }
    strcat(destination, " MEMORY CARD SLOT");
}

/* name: inferred from its body, which XORs entry index of g_name_table with CHEAT_CODE_KEY into the buffer */
short decode_cheat_code(short index, char *destination)
{
    unsigned char *source = g_name_table[index];
    short count = 0;
    while (*source != 0) {
        *destination++ = *source++ ^ CHEAT_CODE_KEY;
        count++;
    }
    return count;
}

/* name: inferred from its body, which compares the entered text with the cheat codes and the level prefix */
short check_password(void)
{
    char code_text[LEVEL_CODE_TEXT_SIZE];
    char code[CHEAT_CODE_BUFFER_SIZE];
    unsigned int old_flags = g_cheat_flags;
    short result;

    if (strncmp(g_password_text, code, decode_cheat_code(CHEAT_CODE_CHEATS_MENU, code)) == 0)
        g_cheat_flags |= CHEAT_FLAG_CHEATS_MENU;
    if (strncmp(g_password_text, code, decode_cheat_code(CHEAT_CODE_GIMME_WEAPONS, code)) == 0)
        g_cheat_flags |= CHEAT_FLAG_GIMME_WEAPONS;
    if (strncmp(g_password_text, code, decode_cheat_code(CHEAT_CODE_UNLIMITED_AMMO, code)) == 0)
        g_cheat_flags |= CHEAT_FLAG_UNLIMITED_AMMO;
    if (strncmp(g_password_text, code, decode_cheat_code(CHEAT_CODE_INDESTRUCTIBLE, code)) == 0)
        g_cheat_flags |= CHEAT_FLAG_INDESTRUCTIBLE;
    result = old_flags != g_cheat_flags;
    if (strncmp(g_password_text, LEVEL_CODE_PREFIX, LEVEL_CODE_PREFIX_LENGTH) == 0) {
        code_text[LEVEL_CODE_TEXT_SIZE - 1] = 0;
        code_text[0] = g_password_text[LEVEL_CODE_PREFIX_LENGTH];
        code_text[1] = g_password_text[LEVEL_CODE_PREFIX_LENGTH + 1];
        g_start_level = atoi(code_text);
        result = -1;
    }
    return result;
}

/* name: inferred from its body, which writes value as up to four decimal digits without leading zeros */
void format_decimal(char *destination, short value)
{
    unsigned char digit;
    short length;
    short started;

    strcpy(destination, "0");
    length = 0;
    started = 0;
    digit = value / 1000;
    if (digit != 0 || started) {
        destination[length++] = digit + '0';
        started = 1;
    }
    value -= digit * 1000;
    digit = value / 100;
    if (digit != 0 || started) {
        destination[length++] = digit + '0';
        started = 1;
    }
    value -= digit * 100;
    digit = value / 10;
    if (digit != 0 || started) {
        destination[length++] = digit + '0';
        started = 1;
    }
    value -= digit * 10;
    digit = value;
    if (digit != 0 || started)
        destination[length++] = digit + '0';
    if (length != 0)
        destination[length] = 0;
    else
        destination[1] = 0;
}

/* Formats the entry's label followed by its value as up to four decimal digits
 * without leading zeros (the digits of format_decimal). number is 8 bytes and
 * the "00000000" copy writes 9: retail's 16-byte frame holds the 8-byte buffer
 * and one 4-byte slot that the copy's terminator lands in. */
/* name: inferred from its body, which appends the decimal value to the label */
void format_value_label(char *destination, MenuEntry *entry)
{
    char number[8];
    short value;
    unsigned char digit;
    short length;
    short started;

    strcpy(number, "00000000");
    strcpy(destination, entry->label);
    value = entry->value;
    length = 0;
    started = 0;
    digit = value / 1000;
    if (digit != 0 || started) {
        number[length++] = digit + '0';
        started = 1;
    }
    value -= digit * 1000;
    digit = value / 100;
    if (digit != 0 || started) {
        number[length++] = digit + '0';
        started = 1;
    }
    value -= digit * 100;
    digit = value / 10;
    if (digit != 0 || started) {
        number[length++] = digit + '0';
        started = 1;
    }
    value -= digit * 10;
    digit = value;
    if (digit != 0 || started)
        number[length++] = digit + '0';
    if (length != 0)
        number[length] = 0;
    else
        number[1] = 0;
    strcat(destination, number);
}

/* name: inferred from its body, which draws "Exit" in the selected or normal colour */
void draw_exit_label(char *destination, void *unused, TASK *request)
{
    const unsigned char *color = g_text_color;

    if (request->state[0] == CONTROL_EXIT)
        color = g_highlight_color;
    draw_text_centered("Exit", 0, CONTROL_MENU_EXIT_Y, color);
    destination[0] = 0;
}

/* name: inferred from its body, which copies the selected card file's title */
char *format_card_file_name(char *destination)
{
    g_card_file_index = g_card_delete_menu[DELETE_MENU_FILE].value;
    return strcpy(destination, g_card_block_names[g_card_delete_menu[DELETE_MENU_FILE].value]);
}

/* name: inferred from its body, which appends one '{' per volume step to the label */
void format_volume_bar(char *dst, MenuEntry *item)
{
    Token token;
    short i;
    token.brace = '{';
    token.zero = 0;
    strcpy(dst, item->label);
    for (i = 0; i < item->value; i++)
        strcat(dst, (char *)&token);
}

/* The formatter call at 0x8001834c passes a third menu-context argument.
 * This basic choice formatter does not use that context. */
/* name: inferred from its body, which appends the selected choice to the label */
void format_choice(char *destination, MenuEntry *layout, void *context)
{
    *destination = '\0';
    if (layout->label != 0) {
        strcpy(destination, layout->label);
    }
    if (layout->choices != 0) {
        strcat(destination, layout->choices[layout->value]);
    }
}

/* name: inferred from its body, which draws the selected control method's controller sprite */
void draw_controller_picture(char *destination, MenuEntry *choice, TASK *state)
{
    POLY_FT4 *poly;
    u_long *uv;
    u_char shade;
    short sprite;

    shade = CONTROLLER_SHADE_DIM;
    if (state->state[0] == CONTROL_PICTURE)
        shade = MENU_SHADE_FULL;
    sprite = g_controller_layout_sprites[choice->value] + MENU_PICTURE_CONTROLLERS;
    poly = g_sprite_workspace;
    setXY4(poly, CONTROLLER_LEFT, CONTROLLER_TOP, CONTROLLER_RIGHT, CONTROLLER_TOP, CONTROLLER_LEFT, CONTROLLER_BOTTOM, CONTROLLER_RIGHT, CONTROLLER_BOTTOM);
    uv = (u_long *)&g_sprite_uvs[sprite];
    *(u_long *)&poly->u0 = *uv++;
    *(u_long *)&poly->u1 = *uv++;
    *(u_long *)&poly->u2 = *uv++;
    *(u_long *)&poly->u3 = *uv;
    setRGB0(poly, shade, shade, shade);
    setPolyFT4(poly);
    addPrim(&g_render_buffer[OT_BACK_SLOT], poly);
    g_sprite_workspace = poly + 1;
    destination[0] = 0;
}

/* name: inferred from its body, which forces the card choice to a present card before formatting */
void format_card_choice(char *destination, MenuEntry *layout, void *context)
{
    if (g_card_status == CARD_PRESENT_BOTH) {
        if (layout->value == CARD_CHOICE_NONE)
            layout->value = CARD_CHOICE_LEFT;
    } else {
        layout->value = g_card_status;
    }
    format_choice(destination, layout, context);
}

/* name: inferred from its body, which draws the password text through draw_password_text */
void draw_password_entry(char *destination)
{
    draw_password_text(0, PASSWORD_TEXT_Y);
    destination[0] = ' ';
    destination[1] = 0;
}

/* name: inferred from its body, which draws one gouraud segment per 10% */
void draw_percentage_bar(short percent, short x, short y)
{
    short remaining;
    unsigned char *color[2];
    short index[2];
    short segments;
    short i;
    short px;
    short width;
    POLY_G4 *poly;
    POLY_FT4 *frame;
    u_long *uv;

    color[0] = g_text_color;
    color[1] = g_text_color;
    remaining = percent;
    px = x + BAR_INSET;
    segments = remaining / BAR_SEGMENT_PERCENT;
    for (i = 0; i < segments + 1; i++) {
        if (remaining <= 0)
            break;
        if (g_level_result->cells) {
            index[0] = i * BAR_SEGMENT_PERCENT;
            index[1] = i * BAR_SEGMENT_PERCENT + BAR_SEGMENT_LAST;
            if (index[0] > PERCENT_MAX)
                index[0] = PERCENT_MAX;
            if (index[1] > PERCENT_MAX)
                index[1] = PERCENT_MAX;
            color[0] = g_level_result->cells + index[0] * RESULT_COLOR_SIZE;
            color[1] = g_level_result->cells + index[1] * RESULT_COLOR_SIZE;
        }
        poly = g_sprite_workspace;
        if (remaining >= BAR_SEGMENT_PERCENT)
            setXY4(poly, px, y, px + g_percentage_bar_widths[i], y, px, y + BAR_HEIGHT, px + g_percentage_bar_widths[i], y + BAR_HEIGHT);
        else {
            width = remaining * BAR_SEGMENT_PERCENT * g_percentage_bar_widths[i] / PERCENT_MAX;
            setXY4(poly, px, y, px + width, y, px, y + BAR_HEIGHT, px + width, y + BAR_HEIGHT);
        }
        px += g_percentage_bar_widths[i];
        remaining -= BAR_SEGMENT_PERCENT;
        setRGB0(poly, color[0][0] / BAR_DIM, color[0][1] / BAR_DIM, color[0][2] / BAR_DIM);
        setRGB1(poly, color[1][0] / BAR_DIM, color[1][1] / BAR_DIM, color[1][2] / BAR_DIM);
        setRGB2(poly, color[0][0], color[0][1], color[0][2]);
        setRGB3(poly, color[1][0], color[1][1], color[1][2]);
        setPolyG4(poly);
        addPrim(&g_render_buffer[MENU_OT_BAR], poly);
        g_sprite_workspace = poly + 1;
    }
    frame = g_sprite_workspace;
    setXY4(frame, x, y, x + BAR_FRAME_WIDTH, y, x, y + BAR_FRAME_HEIGHT, x + BAR_FRAME_WIDTH, y + BAR_FRAME_HEIGHT);
    uv = (u_long *)&g_menu_glyph_uvs[MENU_GLYPH_BAR_FRAME];
    *(u_long *)&frame->u0 = *uv++;
    *(u_long *)&frame->u1 = *uv++;
    *(u_long *)&frame->u2 = *uv++;
    *(u_long *)&frame->u3 = *uv;
    setRGB0(frame, MENU_SHADE_FULL, MENU_SHADE_FULL, MENU_SHADE_FULL);
    setPolyFT4(frame);
    SetSemiTrans(frame, 1);
    SetShadeTex(frame, 1);
    addPrim(&g_render_buffer[MENU_OT_BAR_FRAME], frame);
    g_sprite_workspace = frame + 1;
}

/* name: inferred from its body, which counts and draws the three result percentages */
short update_mission_assessment(void)
{
    static short blink = 0;
    char text[ASSESSMENT_TEXT_SIZE];
    short done;
    unsigned char *color;

    draw_text_centered("MISSION ASSESSMENT", 0, ASSESSMENT_TITLE_Y, g_text_color);
    done = 0;
    if (g_level_result->shown[0] < g_level_result->kills)
        g_level_result->shown[0]++;
    else if (g_level_result->shown[1] < g_level_result->secrets)
        g_level_result->shown[1]++;
    else if (g_level_result->shown[2] < g_level_result->targets)
        g_level_result->shown[2]++;
    else
        done = -1;

    color = g_text_color;
    if (g_level_result->cells)
        color = g_level_result->cells + g_level_result->shown[0] * RESULT_COLOR_SIZE;
    format_decimal(text, g_level_result->shown[0]);
    strcat(text, "%");
    draw_text(text, ASSESSMENT_PERCENT_X, ASSESSMENT_ALIENS_PERCENT_Y, color);
    draw_text("Aliens:", ASSESSMENT_LABEL_X, ASSESSMENT_ALIENS_Y, color);
    draw_percentage_bar(g_level_result->shown[0], ASSESSMENT_BAR_X, ASSESSMENT_ALIENS_BAR_Y);

    if (g_level_result->cells)
        color = g_level_result->cells + g_level_result->shown[1] * RESULT_COLOR_SIZE;
    format_decimal(text, g_level_result->shown[1]);
    strcat(text, "%");
    draw_text(text, ASSESSMENT_PERCENT_X, ASSESSMENT_SECRETS_PERCENT_Y, color);
    draw_text("Secrets:", ASSESSMENT_LABEL_X, ASSESSMENT_SECRETS_Y, color);
    draw_percentage_bar(g_level_result->shown[1], ASSESSMENT_BAR_X, ASSESSMENT_SECRETS_BAR_Y);

    if (g_level_result->cells)
        color = g_level_result->cells + g_level_result->shown[2] * RESULT_COLOR_SIZE;
    format_decimal(text, g_level_result->shown[2]);
    strcat(text, "%");
    draw_text(text, ASSESSMENT_PERCENT_X, ASSESSMENT_MISSION_PERCENT_Y, color);
    draw_text("Mission:", ASSESSMENT_LABEL_X, ASSESSMENT_MISSION_Y, color);
    draw_percentage_bar(g_level_result->shown[2], ASSESSMENT_BAR_X, ASSESSMENT_MISSION_BAR_Y);

    if (done) {
        draw_text_centered("Press \377 button to continue", 0, ASSESSMENT_PROMPT_Y, g_text_color);
        done = 0;
        if ((g_pad_pressed & PADstart) || (g_pad_pressed & PADRdown))
            done = -1;
    } else {
        blink = (blink + 1) & TALLY_TICK_MASK;
        if (blink == 0)
            sound_play(SOUND_TALLY_TICK, TALLY_TICK_PITCH, SPU_VOLUME_MAX);
    }
    return done;
}

/* name: inferred from its body, which enters g_menu_next_state at the first saved cursor */
void menu_back_to_next_state(void *unused, TASK *record) { g_menu_screen = g_menu_next_state; record->state[0] = g_menu_cursors[MENU_CURSOR_MAIN]; }

/* name: inferred from its body, which enters g_menu_return_state at the first saved cursor */
void menu_back(void *unused, TASK *record) { g_menu_screen = g_menu_return_state; record->state[0] = g_menu_cursors[MENU_CURSOR_MAIN]; }

/* name: inferred from its body, which enters state 8, the options screen */
void open_options_menu(void *unused, TASK *record)
{
    record->state[0] = g_menu_cursors[MENU_CURSOR_OPTIONS];
    g_menu_screen = MENU_OPTIONS;
    record->state[3] = 0;
    g_card_status = card_present_mask();
}

/* name: inferred from its body, which enters state 7, the main menu */
void open_main_menu(void *unused, TASK *record) { g_menu_screen = MENU_MAIN; record->state[0] = g_menu_cursors[MENU_CURSOR_MAIN]; }

/* name: inferred from its body, which enters state 13, the free-space check before saving */
void start_card_save(void *unused, TASK *record) { g_menu_screen = MENU_SAVE_CHECK; record->state[0] = 0; }

/* name: inferred from its body, which enters state 12, the save scan before loading */
void start_card_load(void *unused, TASK *record) { g_menu_screen = MENU_LOAD_SCAN; record->state[0] = 0; }

/* name: inferred from its body, which enters state 14, the directory read before deleting */
void start_card_delete(void *unused, TASK *record) { g_menu_screen = MENU_DELETE_SCAN; record->state[0] = 0; }

/* name: inferred from its body, which enters state 17, the format confirmation */
void open_card_format_menu(void *unused, TASK *record) { g_menu_screen = MENU_FORMAT_CONFIRM; record->state[0] = CONFIRM_NO; }

/* name: inferred from its body, which stores the chosen file and enters state 16, the delete confirmation */
void select_card_file(void *unused, TASK *record)
{
    unsigned short value = (unsigned short)record->state[0];
    g_menu_screen = MENU_DELETE_CONFIRM;
    record->state[0] = CONFIRM_NO;
    g_card_file_index = (short)(value - 1);
}

/* name: inferred from its body, which an empty slot saves at once, a used one asks to overwrite (state 24) */
void select_card_save_slot(void *context, TASK *record)
{
    short index = -1 + *(unsigned short *)&record->state[0];
    int mode = g_card_slot_levels[index];
    g_card_file_index = index;
    if (mode == -1) {
        confirm_overwrite(context);
    } else {
        g_menu_screen = MENU_OVERWRITE_CONFIRM;
        record->state[0] = CONFIRM_NO;
    }
}

/* name: inferred from its body, which enters state 9, the cheats screen */
void open_cheats_menu(void *unused, TASK *record) { g_menu_screen = MENU_CHEATS; record->state[0] = g_menu_cursors[MENU_CURSOR_CHEATS]; }

/* name: inferred from its body, which enters state 1, the password keyboard */
void open_password_entry(void *unused, TASK *record) { g_menu_screen = MENU_PASSWORD_ENTRY; record->state[0] = 0; }

/* name: inferred from its body, which enters the card menu when a card is present */
void open_card_menu(void *unused, TASK *record)
{
    if (g_card_status) {
        g_menu_screen = g_menu_return_state;
        record->state[0] = g_menu_cursors[MENU_CURSOR_CARD];
    }
}

/* name: inferred from its body, which enters state 3, the control method screen */
void open_control_method_menu(void *unused, TASK *record) { g_menu_screen = MENU_CONTROL_METHOD; record->state[0] = g_menu_cursors[MENU_CURSOR_CONTROL]; }

/* name: inferred from its body, which calls SetVideoMode with the chosen entry */
void select_video_mode(const MenuEntry *choice)
{
    if (choice->value == 0)
        SetVideoMode(MODE_NTSC);
    else
        SetVideoMode(MODE_PAL);
}

/* name: inferred from its body, which applies the options and leaves the menus (state 4) */
void start_game(void)
{
    g_game_flags &= ~GAME_FLAG_HARD;
    if (g_options_menu[OPTIONS_DIFFICULTY].value == DIFFICULTY_XENOMANIA)
        g_game_flags |= GAME_FLAG_HARD;
    g_menu_screen = MENU_START_GAME;
    set_button_layout(g_control_method_menu[CONTROL_PICTURE].value);
    mode_control_set(g_options_menu[OPTIONS_CAMERA_SWAY].value);
}

/* name: inferred from its body, which starts the game at g_start_level */
void start_new_game(void)
{
    g_level = (int)g_start_level;
    start_game();
}

/* name: inferred from its body, which enters state 25 to continue */
void continue_game(void *unused, TASK *record) { g_menu_screen = MENU_CONTINUE; }

/* name: static evidence, analysis/runtime/functions.json */
void select_music_volume(void *unused, TASK *record)
{
}

/* name: runtime evidence, analysis/runtime/functions.json */
void select_sound_volume(void *unused, TASK *record)
{
}

void f_1760c(MenuEntry *record)
{
    g_6c07c = record->value;
}

/* name: inferred from its body, which sets the music volume from the entry value */
void change_music_volume(const MenuEntry *choice)
{
    music_set_volume(choice->value * MENU_VOLUME_SCALE);
}

/* name: inferred from its body, which sets the music volume and the options-menu value */
void set_music_volume(int value)
{
    music_set_volume(value * MENU_VOLUME_SCALE);
    g_options_menu[OPTIONS_MUSIC_VOLUME].value = (short)value;
}

/* name: inferred from its body, which keeps the card choice on a present card */
void update_card_choice(MenuEntry *choice)
{
    short mode = g_card_status;
    if (mode == CARD_PRESENT_BOTH) {
        if (choice->value >= CARD_CHOICE_COUNT)
            choice->value = CARD_CHOICE_LEFT;
        else if (choice->value <= 0)
            choice->value = CARD_CHOICE_RIGHT;
    } else {
        choice->value = mode;
    }
}

/* name: inferred from its body, which sets the effects volume from the entry value */
void change_sound_volume(const MenuEntry *choice)
{
    sound_set_volume(choice->value * MENU_VOLUME_SCALE);
}

/* name: inferred from its body, which sets the effects volume and the options-menu value */
void set_sound_volume(int value) { sound_set_volume(value * MENU_VOLUME_SCALE); g_options_menu[OPTIONS_EFFECTS_VOLUME].value = value; }

/* name: inferred from its body, which returns the options-menu effects volume */
short get_sound_volume(void) { return g_options_menu[OPTIONS_EFFECTS_VOLUME].value; }

/* name: inferred from its body, which returns the options-menu music volume */
short get_music_volume(void) { return g_options_menu[OPTIONS_MUSIC_VOLUME].value; }

/* name: inferred from its body, which returns the options menu's Difficulty entry value */
short get_difficulty(void) { return g_options_menu[OPTIONS_DIFFICULTY].value; }

/* name: inferred from its body, which sets the options menu's Difficulty entry value */
void set_difficulty(short value) { g_options_menu[OPTIONS_DIFFICULTY].value = value; }

/* name: inferred from its body, which returns the control-method menu's entry value */
short get_control_method(void) { return g_control_method_menu[CONTROL_PICTURE].value; }

void options_set_control_method(short value) { g_control_method_menu[CONTROL_PICTURE].value = value; set_button_layout(value); }

/* name: inferred from its body, which switches the audio setup when the sound mode changes */
int set_sound_mode(int value)
{
    short mode = (short)value;
    if (mode == g_sound_mode) return 1;
    g_sound_mode = mode;
    if (mode != SOUND_MODE_MUSIC) {
        if (mode < SOUND_MODE_BOTH) {
            if (mode == SOUND_MODE_EFFECTS) goto mode_zero;
            goto fallback;
        }
        if (mode == SOUND_MODE_BOTH) goto mode_two;
        goto fallback;
    }
    goto mode_one;
mode_zero:
    music_disable();
    goto shared;
mode_one:
    music_enable();
    goto tail;
mode_two:
    music_enable();
shared:
    audio_enable();
    audio_init();
    return audio_load_bank(FILE_111SFX_BIN, SAMPLE_LEVEL_FIRST);
fallback:
    music_disable();
tail:
    return audio_disable();
}

/* name: inferred from its body, the Sound entry's change callback, which applies the entry value through set_sound_mode */
int change_sound_mode(MenuEntry *entry)
{
    return set_sound_mode(entry->value);
}

/* name: inferred from its body, which plays the movie the entry value selects */
void play_selected_movie(MenuEntry *entry)
{
    level_unload();
    movie_play(entry->value);
}

/* name: inferred from its body, which clears both buffers with the render task stopped */
void blank_screen(void)
{
    RECT first;
    RECT second;

    g_render_task->state[0] = 0;
    first.x = 0;
    first.y = 0;
    first.w = MOVIE_SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = MOVIE_SCREEN_WIDTH;
    second.y = 0;
    second.w = MOVIE_SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;

    VSync(0);
    SetDispMask(0);
    ClearImage(&first, 0, 0, 0);
    ClearImage(&second, 0, 0, 0);
    DrawSync(0);
    VSync(0);
    SetDispMask(1);
}

/* name: inferred from its body, which clears 960x240 and moves the 320x240 front buffer */
void clear_display_area(void)
{
    RECT first;
    RECT second;

    first.x = 0;
    first.y = 0;
    first.w = MOVIE_SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = MOVIE_SCREEN_WIDTH;
    second.y = 0;
    second.w = MOVIE_SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;

    VSync(0);
    SetDispMask(0);
    ClearImage(&first, 0, 0, 0);
    ClearImage(&second, 0, 0, 0);
    DrawSync(0);

    first.x = 0;
    first.y = 0;
    first.w = SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = SCREEN_WIDTH;
    second.y = 0;
    second.w = SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;
    setup_double_buffer(&first, &second, 1, 0);

    VSync(0);
    SetDispMask(1);
    g_render_task->state[0] = 1;
}

/* name: inferred from its body, which enters state 19 and starts the credits */
void open_credits(void *unused, TASK *record)
{
    g_menu_screen = MENU_CREDITS;
    record->state[0] = 0;
    credits_start();
}

/* name: inferred from its body, which formats the selected card and reports it */
void format_selected_card(void)
{
    short selected = selected_card_port();
    if (selected != -1) {
        card_format(selected);
        show_message("Memory Card formatted", 0, CARD_MESSAGE_Y);
    }
    g_menu_screen = g_menu_return_state;
    g_menu_screen_previous = g_menu_return_state;
}

/* name: inferred from its body, which deletes the selected file from the selected card */
void delete_selected_card_file(void)
{
    short mode = selected_card_port();
    if (mode != -1)
        card_delete_file(g_card_files[g_card_file_index].name, mode);
    g_menu_screen = MENU_DELETE_SCAN;
}

/* name: inferred from its body, which draws show_message's text until it times out */
void menu_message_task(TASK *node)
{
    const char *message = (const char *)*(volatile int *)&node->state[3];
    if (node->state[1] != 0 || node->state[0] == 0) {
        TaskDie(node);
    } else {
        draw_text_centered(message, 0, node->state[2], (const unsigned char *)&g_message_color);
        node->state[0]--;
        g_next_task = node->Next;
    }
}

/* name: inferred from its body, which ends the current menu message task */
void menu_message_close(void)
{
    if (g_menu_message_task != 0)
        g_menu_message_task->state[1] = -1;
    g_menu_message_task = 0;
}

/* name: inferred from its body, which starts a notification task with the text */
void show_message(char *message, int unused, int y)
{
    TASK *entry;
    menu_message_close();
    entry = TaskCreate(TASK_PRIORITY_MENU, menu_message_task);
    if (entry != 0) {
        entry->state[0] = MENU_MESSAGE_TICKS;
        entry->state[1] = 0;
        entry->state[2] = (short)y;
        entry->state[3] = (int)message;
        g_menu_message_task = entry;
    }
}

/* name: inferred from its body, which loads a save slot from the selected card, retrying five times */
void load_card_slot(int selection)
{
    short selected = (short)selection;
    short current = selected_card_port();
    short attempts = 0;
    int status;

    if (current != -1) {
        do {
            status = load_from_card(selected, current);
            attempts++;
        } while (status != 0 && attempts < CARD_ATTEMPTS);

        if (status == 0) {
            g_game_flags |= GAME_FLAG_RESTORED;
            start_game();
        } else {
            show_message("Error loading from card", 0, CARD_MESSAGE_Y);
        }
    }
}

/* name: static evidence, analysis/runtime/functions.json */
void format_card_slot(short slot, char *destination, int second, void *context)
{
    short level = g_card_slot_levels[slot];
    short section;
    short stage;
    char number[4];
    if (level == -1) {
        strcpy(destination, "Unused");
    } else {
        strcpy(destination, "Section   - Level   ");
        section = SECTION_2;
        stage = level - LEVEL_211_LIVING_QUARTERS;
        if (level < LEVEL_211_LIVING_QUARTERS) {
            section = SECTION_1;
            stage = level;
        }
        if (level >= LEVEL_311_TUNNELS) {
            section = SECTION_3;
            stage = level - LEVEL_311_TUNNELS;
        }
        destination[SLOT_TEXT_SECTION] = section + '0';
        format_decimal(destination + SLOT_TEXT_LEVEL, stage + 1);
    }
}

/* name: inferred from its body, which formats save slot 0's row */
void format_card_slot_0(int first, int second, void *context) { format_card_slot(0, first, second, context); }

/* name: inferred from its body, which formats save slot 1's row */
void format_card_slot_1(int first, int second, void *context) { format_card_slot(1, first, second, context); }

/* name: inferred from its body, which formats save slot 2's row */
void format_card_slot_2(int first, int second, void *context) { format_card_slot(2, first, second, context); }

/* name: inferred from its body, which formats save slot 3's row */
void format_card_slot_3(int first, int second, void *context) { format_card_slot(3, first, second, context); }

/* name: inferred from its body, which formats the row of the chosen slot */
void format_selected_card_slot(int first, int second, void *context)
{
    format_card_slot(g_card_file_index, first, second, context);
}

/* name: inferred from its body, which loads save slot 0 */
void load_card_slot_0(void) { load_card_slot(0); }

/* name: inferred from its body, which loads save slot 1 */
void load_card_slot_1(void) { load_card_slot(1); }

/* name: inferred from its body, which loads save slot 2 */
void load_card_slot_2(void) { load_card_slot(2); }

/* name: inferred from its body, which loads save slot 3 */
void load_card_slot_3(void) { load_card_slot(3); }

/* name: inferred from its body, which reads the slot levels and enables the used slots */
short scan_card_slots(void)
{
    short slot = selected_card_port();
    short tries;
    short i;
    int result;

    if (slot != -1) {
        tries = 0;
        do {
            result = save_slot_levels(g_card_slot_levels, slot);
            tries++;
        } while (result != 0 && tries < CARD_ATTEMPTS);
        for (i = 1; i < CARD_SLOT_MENU_EXIT; i++) {
            if (g_card_slot_levels[i - 1] == -1)
                g_card_load_menu[i].flags = MENU_ENTRY_GREYED;
            else
                g_card_load_menu[i].flags = MENU_ENTRY_ACTIVE;
        }
        if (result == 0)
            return -1;
    }
    return 0;
}

/* name: inferred from its body, which saves to a slot on the selected card, retrying five times */
void save_card_slot(short mode)
{
    short slot = selected_card_port();
    short attempts = 0;
    int result;
    if (slot != -1) {
        while (attempts < CARD_ATTEMPTS) {
            result = save_to_card(mode, slot);
            attempts++;
            if (!result)
                break;
        }
        if (result == 0) {
            show_message("Game Saved", 0, SAVE_MESSAGE_Y);
            g_card_slot_levels[mode] = g_level;
        } else {
            show_message("Error saving to card", 0, SAVE_MESSAGE_Y);
        }
    }
}

/* name: inferred from its body, which saves to the chosen slot after the overwrite prompt */
void confirm_overwrite(void *unused)
{
    save_card_slot(g_card_file_index);
    g_menu_screen = MENU_SAVE_SLOTS;
    g_menu_screen_previous = MENU_SAVE_SLOTS;
}

/* name: inferred from its body, which draws a menu screen and handles its input */
void run_menu(MenuEntry *menu, short count, TASK *record)
{
    char text[MENU_TEXT_SIZE];
    MenuEntry *entry;
    short first;
    short last;
    short i;
    short y;
    short step;
    short width;
    const unsigned char *color;

    while (!(menu[record->state[0]].flags & MENU_ENTRY_SELECTABLE)) {
        record->state[0]++;
        if (record->state[0] >= count)
            record->state[0] = 0;
    }
    last = (short)record->state[0] + MENU_WINDOW_HALF;
    first = (short)record->state[0] - MENU_WINDOW_HALF;
    if (first < 0) {
        last = count < MENU_WINDOW_LIMIT ? count : MENU_WINDOW_LINES;
        first = 0;
    }
    if (last > count) {
        first = count < MENU_WINDOW_LINES ? 0 : count - MENU_WINDOW_LINES;
        last = count;
    }
    y = menu[0].y;
    entry = menu;
    entry += first;
    for (i = first; i < last; entry++, i++) {
        if (!(entry->flags & MENU_ENTRY_VISIBLE))
            continue;
        if (entry->flags & MENU_ENTRY_DISABLED) {
            color = g_disabled_text_color;
            if (record->state[0] == i)
                color = g_disabled_highlight_color;
        } else {
            color = g_text_color;
            if (record->state[0] == i)
                color = g_highlight_color;
        }
        g_menu_entry_color = color;
        entry->callback0(text, entry, record);
        if (strlen(text)) {
            width = text_width(text) >> 1;
            if (entry->flags & MENU_ENTRY_SMALL)
                draw_text_multiline(text, MENU_CENTRE_X - width, y, g_menu_entry_color);
            else
                draw_text(text, MENU_CENTRE_X - width, y, g_menu_entry_color);
            if (entry->flags & MENU_ENTRY_SPACE_DOUBLE)
                y += MENU_LINE_SPACING_DOUBLE;
            else if (entry->flags & MENU_ENTRY_SPACE_TRIPLE)
                y += MENU_LINE_SPACING_TRIPLE;
            else if (entry->flags & MENU_ENTRY_SMALL)
                y += MENU_LINE_SPACING_SMALL;
            else
                y += MENU_LINE_SPACING;
        }
    }
    if (g_pad_pressed & PADLup) {
        play_menu_sound(MENU_SOUND_MOVE);
        do {
            if (record->state[0] == 0)
                record->state[0] = count - 1;
            else
                record->state[0]--;
        } while (!(menu[record->state[0]].flags & MENU_ENTRY_SELECTABLE));
    }
    if (g_pad_pressed & PADLdown) {
        play_menu_sound(MENU_SOUND_MOVE);
        do {
            record->state[0]++;
            if (record->state[0] >= count)
                record->state[0] = 0;
        } while (!(menu[record->state[0]].flags & MENU_ENTRY_SELECTABLE));
    }
    entry = &menu[record->state[0]];
    step = 1;
    if (CHEAT_IS_ACTIVE(CHEAT_FLAG_CHEATS_MENU)) {
        if (g_pad_held & PADR1)
            step = MENU_STEP_FAST;
        if (g_pad_held & PADR2)
            step = MENU_STEP_FASTER;
    }
    if (g_pad_repeat & PADLleft) {
        if (!(entry->flags & MENU_ENTRY_SILENT))
            play_menu_sound(MENU_SOUND_MOVE);
        entry->value -= step;
        if (entry->value < 0)
            entry->value = entry->limit - 1;
        if (entry->callback2)
            entry->callback2(entry, record);
    }
    if (g_pad_repeat & PADLright) {
        if (!(entry->flags & MENU_ENTRY_SILENT))
            play_menu_sound(MENU_SOUND_MOVE);
        entry->value += step;
        if (entry->value >= entry->limit)
            entry->value = 0;
        if (entry->callback2)
            entry->callback2(entry, record);
    }
    if ((g_pad_pressed & PADstart) || (g_pad_pressed & PADRdown)) {
        if (entry->callback1) {
            if (!(entry->flags & MENU_ENTRY_SILENT))
                play_menu_sound(MENU_SOUND_SELECT);
            entry->callback1(entry, record);
        }
    }
}

/* name: inferred from its body, which fills the password text with '-' */
void clear_password(void)
{
    short i;
    g_password_length = 0;
    for (i = 0; i < PASSWORD_LENGTH; i++)
        g_password_text[i] = '-';
    g_password_text[PASSWORD_LENGTH] = 0;
}

/* name: inferred from its body, which draws the password text centred in 16-character lines */
void draw_password_text(int unused, int y)
{
    short length = (short)strlen((const char *)g_password_text);
    short offset = 0;
    void *draw_state = g_text_color;
    char line[PASSWORD_LINE_SIZE];

    while (length > 0) {
        int count = (short)length;
        int width;
        int x;

        if (count >= PASSWORD_LINE_SIZE)
            count = PASSWORD_LINE_LENGTH;
        strncpy(line, (const char *)g_password_text + offset, count);
        line[count] = 0;
        width = text_width(line);
        x = (SCREEN_WIDTH - width) / 2;
        draw_text(line, x, (short)y, draw_state);
        length -= count;
        offset += count;
        y += MENU_LINE_SPACING;
    }
}

/* Draws the password keyboard: 36 keys in rows of 11, the last three
 * being Delete/Accept/Quit, with the cursor key highlighted and
 * bracketed. */
/* name: inferred from its body, which draws the 36 password keys with the cursor */
void draw_password_keyboard(void)
{
    char text[KEY_TEXT_SIZE];
    short cursor = g_password_prompt_menu[0].value;
    short selected_width = 0;
    short key = 0;
    short x;
    short y = KEYBOARD_TOP;
    short row;
    short column;
    short width;
    const unsigned char *color;

    for (row = 0; row < KEYBOARD_ROWS; row++, y += KEY_SPACING) {
        x = KEYBOARD_LEFT;
        for (column = 0; column < KEYBOARD_COLUMNS; column++, key++) {
            if ((unsigned short)key >= PASSWORD_KEY_COUNT)
                break;
            switch (key) {
            case KEY_DELETE:
                strcpy(text, "Delete ");
                break;
            case KEY_ACCEPT:
                strcpy(text, "Accept ");
                break;
            case KEY_QUIT:
                strcpy(text, "Quit ");
                break;
            default:
                text[0] = g_password_key_characters[key];
                text[1] = 0;
                break;
            }
            width = key >= KEY_DELETE ? text_width(text) - KEY_BRACKET_OFFSET : 0;
            if (cursor == key) {
                color = g_highlight_color;
                selected_width = width;
            } else {
                color = g_text_color;
            }
            draw_text(text, x, y, color);
            x += key >= KEY_DELETE ? WIDE_KEY_SPACING : KEY_SPACING;
        }
    }
    y = g_password_prompt_menu[0].value / KEYBOARD_COLUMNS;
    column = g_password_prompt_menu[0].value % KEYBOARD_COLUMNS;
    if (cursor >= KEY_DELETE)
        x = column * WIDE_KEY_SPACING + KEYBOARD_LEFT;
    else
        x = column * KEY_SPACING + KEYBOARD_LEFT;
    y = y * KEY_SPACING + KEYBOARD_TOP;
    color = g_highlight_color;
    draw_text("[", x - KEY_BRACKET_OFFSET, y, color);
    draw_text("]", x + selected_width + KEY_BRACKET_OFFSET, y, color);
    draw_password_text(0, KEYBOARD_PASSWORD_Y);
}

/* name: inferred from its body, which clamps the cursor column on the short last row */
int password_keyboard_column(short value, short mode)
{
    if (mode < KEYBOARD_LAST_ROW)
        return value;
    if (value < WIDE_KEY_SPLIT_RIGHT)
        return value >= WIDE_KEY_SPLIT_LEFT;
    return 2;
}

/* name: inferred from its body, which moves the keyboard cursor and handles the keys */
void password_keyboard_input(TASK *record)
{
    MenuEntry *entry = g_password_prompt_menu;
    short column = entry->value % KEYBOARD_COLUMNS;
    short row = entry->value / KEYBOARD_COLUMNS;
    int wrap;

    if (g_pad_repeat & PADLleft) {
        column--;
        if (row >= KEYBOARD_LAST_ROW) {
            if (column < 0)
                column = 2;
        } else if (column < 0) {
            column = KEYBOARD_LAST_COLUMN;
        }
        play_menu_sound(MENU_SOUND_MOVE);
    }
    if (g_pad_repeat & PADLright) {
        column++;
        if (row >= KEYBOARD_LAST_ROW)
            wrap = column < WIDE_KEY_COUNT;
        else
            wrap = column < KEYBOARD_COLUMNS;
        if (!wrap)
            column = 0;
        play_menu_sound(MENU_SOUND_MOVE);
    }
    if (g_pad_repeat & PADLup) {
        row--;
        if (row < 0)
            row = KEYBOARD_LAST_ROW;
        column = password_keyboard_column(column, row);
        play_menu_sound(MENU_SOUND_MOVE);
    }
    if (g_pad_repeat & PADLdown) {
        row++;
        if (row >= KEYBOARD_ROWS)
            row = 0;
        column = password_keyboard_column(column, row);
        play_menu_sound(MENU_SOUND_MOVE);
    }
    entry->value = column + row * KEYBOARD_COLUMNS;
    if ((g_pad_pressed & PADstart) || (g_pad_pressed & PADRdown)) {
        play_menu_sound(MENU_SOUND_MOVE);
        switch (g_password_key_characters[entry->value]) {
        default:
            if (g_password_length < PASSWORD_LENGTH) {
                g_password_text[g_password_length] = g_password_key_characters[entry->value];
                g_password_length++;
            }
            break;
        case 'x':
            if (g_password_length > 0)
                g_password_length--;
            g_password_text[g_password_length] = '-';
            break;
        case 'y':
            if (password_decode() == 0) {
                play_menu_sound(MENU_SOUND_SELECT);
                start_game();
                g_game_flags |= GAME_FLAG_RESTORED;
            } else if (check_password()) {
                show_message("CHEATS ACTIVATED", 0, PASSWORD_MESSAGE_Y);
            } else {
                play_menu_sound(MENU_SOUND_SELECT);
                show_message("INVALID PASSWORD", 0, PASSWORD_MESSAGE_Y);
            }
            break;
        case 'z':
            open_options_menu(entry, record);
            break;
        }
    }
}

/* name: inferred from its body, which lists this game's saves on the selected card */
short read_card_directory(const char *title, MenuCallback select)
{
    struct DIRENTRY *record = g_card_files;
    CardHeader *header = g_card_headers;
    short device = selected_card_port();
    short count = card_count_files(g_card_files, "*", device);
    short i;
    short j;
    short saves;

    if (count == 0)
        return 0;
    for (i = 0; i < CARD_DIRECTORY_SIZE; i++)
        g_card_block_names[i] = 0;
    saves = 0;
    for (i = 0; i < count; record++, header++, i++) {
        if (card_read_header(record->name, header, device) == 0) {
            g_card_block_names[saves] = (const char *)header->title.text;
            saves++;
            for (j = 0; j < CARD_TITLE_SHOWN; j++) {
                if ((unsigned char)(header->title.text[j] - ' ') > 'z' - ' ')
                    header->title.text[j] = '?';
            }
            header->title.text[CARD_TITLE_SHOWN] = 0;
        }
    }
    g_card_delete_menu[DELETE_MENU_FILE].limit = saves;
    g_card_delete_menu[DELETE_MENU_FILE].value = 0;
    g_card_file_index = 0;
    return count;
}

/* name: inferred from its body, which runs the front-end screens by g_menu_screen */
void menu_task(TASK *task)
{
    RECT first;
    RECT second;
    short sectors;

    music_poll();
    if (g_menu_music_started == 0) {
        load_sprite_textures(FILE_CONTROL_BND, MENU_PICTURE_CONTROLLERS);
        music_play_track(MUSIC_TRACK_MENU);
        g_menu_music_started = -1;
    }
    if (PadRead(0) != 0)
        g_menu_idle_frames = 0;
    if (g_menu_screen_previous != g_menu_screen) {
        g_menu_idle_frames = 0;
        menu_message_close();
        g_menu_screen_previous = g_menu_screen;
    } else {
        if (g_menu_screen == MENU_MAIN)
            g_menu_idle_frames++;
        else
            g_menu_idle_frames = 0;
        if (g_menu_idle_frames > DEMO_IDLE_FRAMES) {
            g_demo_index++;
            g_menu_idle_frames = 0;
            if (g_demo_index >= DEMO_LEVEL_COUNT)
                g_demo_index = 0;
            g_start_level = g_demo_levels[g_demo_index];
            g_level = g_start_level;
            if (g_start_level == -1)
                g_title_intro_state = TITLE_INTRO_REPLAY;
            g_menu_screen = MENU_START_GAME;
            g_menu_screen_previous = MENU_START_GAME;
            g_game_flags = (g_game_flags & ~GAME_FLAG_HARD) | GAME_FLAG_DEMO;
        }
    }
    switch (g_menu_screen) {
    case MENU_NONE:
        break;
    case MENU_CREDITS_END:
        g_menu_screen = MENU_OPTIONS;
        music_play_track(MUSIC_TRACK_MENU);
        break;
    case MENU_CREDITS:
        if ((g_pad_pressed & PADstart) || (g_pad_pressed & PADRdown))
            g_menu_screen = MENU_CREDITS_END;
        break;
    case MENU_RESULTS:
        if (update_mission_assessment()) {
            if (g_level_result->grade)
                g_menu_screen = g_menu_next_state = MENU_LEVEL_PASSWORD;
            else
                g_menu_screen = MENU_CONTINUE;
        }
        break;
    case MENU_LEVEL_PASSWORD:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_SLOW);
        run_menu(g_password_menu, ARRAY_COUNT(g_password_menu), (TASK *)task);
        break;
    case MENU_SAVE_CARD:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_INTERVAL);
        run_menu(g_save_card_menu, ARRAY_COUNT(g_save_card_menu), (TASK *)task);
        break;
    case MENU_LOAD_CARD:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_INTERVAL);
        g_menu_cursors[MENU_CURSOR_CARD] = task->state[0];
        run_menu(g_load_card_menu, ARRAY_COUNT(g_load_card_menu), (TASK *)task);
        break;
    case MENU_LOAD_SCAN:
        if (scan_card_slots()) {
            g_menu_screen = MENU_LOAD_SLOTS;
        } else {
            show_message("No save game data on Memory Card", 0, CARD_MESSAGE_Y);
            g_menu_screen = g_menu_return_state;
            g_menu_screen_previous = g_menu_return_state;
        }
        break;
    case MENU_SAVE_CHECK:
        if (g_card_files != 0) {
            card_find_files(g_card_files, "*", selected_card_port(), &sectors);
            if (sectors >= CARD_BLOCKS_FULL) {
                g_menu_screen = g_menu_return_state;
                g_menu_screen_previous = g_menu_return_state;
                show_message("Card full. Insert new card.", 0, CARD_MESSAGE_Y);
            } else {
                scan_card_slots();
                g_menu_screen = MENU_SAVE_SLOTS;
            }
        }
        break;
    case MENU_OVERWRITE_CONFIRM:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_INTERVAL);
        if (g_card_status != g_card_status_previous)
            g_menu_screen = g_menu_return_state;
        else
            run_menu(g_card_overwrite_menu, ARRAY_COUNT(g_card_overwrite_menu), (TASK *)task);
        break;
    case MENU_LOAD_SLOTS:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_INTERVAL);
        if (g_card_status != g_card_status_previous)
            g_menu_screen = g_menu_return_state;
        else
            run_menu(g_card_load_menu, ARRAY_COUNT(g_card_load_menu), (TASK *)task);
        break;
    case MENU_SAVE_SLOTS:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_INTERVAL);
        if (g_card_status != g_card_status_previous)
            g_menu_screen = g_menu_return_state;
        else
            run_menu(g_card_save_menu, ARRAY_COUNT(g_card_save_menu), (TASK *)task);
        break;
    case MENU_DELETE_SCAN:
        g_card_file_index = -1;
        g_card_file_count = read_card_directory(g_label_delete_file, (MenuCallback)select_card_file);
        if (g_card_file_count != 0) {
            g_menu_screen = MENU_DELETE_SELECT;
        } else {
            g_menu_screen = g_menu_return_state;
            g_menu_screen_previous = g_menu_return_state;
            show_message("No blocks on Memory Card", 0, CARD_MESSAGE_Y);
        }
        break;
    case MENU_DELETE_CONFIRM:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_INTERVAL);
        if (g_card_status != g_card_status_previous)
            g_menu_screen = g_menu_return_state;
        else
            run_menu(g_card_delete_file_menu, ARRAY_COUNT(g_card_delete_file_menu), (TASK *)task);
        break;
    case MENU_FORMAT_CONFIRM:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_INTERVAL);
        if (g_card_status != g_card_status_previous)
            g_menu_screen = g_menu_return_state;
        else
            run_menu(g_card_format_menu, ARRAY_COUNT(g_card_format_menu), (TASK *)task);
        break;
    case MENU_DELETE_SELECT:
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_INTERVAL);
        if (g_card_status != g_card_status_previous) {
            g_card_file_index = -1;
            g_menu_screen = g_menu_return_state;
        } else {
            run_menu(g_card_delete_menu, ARRAY_COUNT(g_card_delete_menu), (TASK *)task);
        }
        break;
    case MENU_PASSWORD_ENTRY:
        clear_password();
        g_menu_screen = MENU_PASSWORD_KEYBOARD;
        break;
    case MENU_PASSWORD_KEYBOARD:
        password_keyboard_input((TASK *)task);
        draw_password_keyboard();
        break;
    case MENU_CONTROL_METHOD:
        run_menu(g_control_method_menu, ARRAY_COUNT(g_control_method_menu), (TASK *)task);
        break;
    case MENU_CONTINUE:
        g_menu_screen = MENU_CONTINUE_FADE;
        break;
    case MENU_START_GAME:
        g_menu_screen = MENU_FADE_OUT;
        cd_seek_file(FILE_COLONY_BS);
        break;
    case MENU_ENTER_GAME:
        if ((g_game_flags & GAME_FLAG_DEMO) || CHEAT_IS_ACTIVE(CHEAT_FLAG_GIMME_WEAPONS))
            password_set_full_record(g_start_level);
    case MENU_LEAVE:
        first.x = 0;
        first.y = 0;
        first.w = SCREEN_WIDTH;
        first.h = SCREEN_HEIGHT;
        second.x = 0;
        second.y = SCREEN_HEIGHT;
        second.w = SCREEN_WIDTH;
        second.h = SCREEN_HEIGHT;
        ClearImage(&first, 0, 0, 0);
        ClearImage(&second, 0, 0, 0);
        DrawSync(0);
        setup_double_buffer(&first, &second, 1, 0);
        VSync(0);
        g_game_flags |= GAME_FLAG_MENU_DONE;
        break;
    case MENU_MAIN:
        if (CHEAT_IS_ACTIVE(CHEAT_FLAG_CHEATS_MENU)) {
            g_main_menu[MAIN_START].y = MAIN_MENU_Y_CHEATS;
            g_main_menu[MAIN_CHEATS].flags = MENU_ENTRY_ACTIVE;
        } else {
            g_main_menu[MAIN_START].y = MAIN_MENU_Y;
            g_main_menu[MAIN_CHEATS].flags = 0;
        }
        run_menu(g_main_menu, ARRAY_COUNT(g_main_menu), (TASK *)task);
        break;
    case MENU_OPTIONS:
        g_menu_cursors[MENU_CURSOR_OPTIONS] = task->state[0];
        run_menu(g_options_menu, ARRAY_COUNT(g_options_menu), (TASK *)task);
        set_sound_mode(g_options_menu[OPTIONS_SOUND].value);
        task->state[3] = poll_card_status(task->state[3], CARD_POLL_SLOW);
        break;
    case MENU_CHEATS:
        run_menu(g_cheats_menu, ARRAY_COUNT(g_cheats_menu), (TASK *)task);
        g_cheat_flags = CHEAT_FLAG_CHEATS_MENU;
        g_start_level = g_cheats_menu[CHEATS_LEVEL].value;
        if (g_cheats_menu[CHEATS_INDESTRUCTIBLE].value)
            g_cheat_flags |= CHEAT_FLAG_INDESTRUCTIBLE;
        if (g_cheats_menu[CHEATS_UNLIMITED_AMMO].value)
            g_cheat_flags |= CHEAT_FLAG_UNLIMITED_AMMO;
        if (g_cheats_menu[CHEATS_GIMME_WEAPONS].value)
            g_cheat_flags |= CHEAT_FLAG_GIMME_WEAPONS;
        g_cheat_flags |= CHEAT_FLAGS_DEFAULT;
        break;
    }
    g_next_task = task->Next;
}

/* name: inferred from its body, which extracts the level's text from MISSIONU.TXT */
void load_mission_text(unsigned short index)
{
    unsigned short slot = heap_mark();
    short remaining;
    short searching;
    char *cursor;
    char *start;
    int size;
    HeapFile *file;

    file = &g_file_table[FILE_MISSIONU_TXT];
    file->loaded_size = 0;
    file->buffer = 0;
    load_file(file);
    remaining = index * 2 + 1;
    g_mission_text[0] = 0;
    cursor = file->buffer;
    searching = -1;
    size = file->loaded_size;
    start = cursor;
    do {
        if (*cursor == '*') {
            if (--remaining < 0) {
                *cursor = 0;
                strncpy(g_mission_text, start, MISSION_TEXT_SIZE);
                g_mission_text[MISSION_TEXT_LAST] = 0;
                searching = 0;
            } else {
                start = cursor + 1;
            }
        }
        if (--size <= 0)
            searching = 0;
        cursor++;
    } while (searching);
    heap_release(slot);
}

/* Multi-statement macro (name invented): reads the digit that follows a control character.
 * Only the '$' case uses it: its do { } while (0) gives retail's addressing of x and position
 * through the text pointer's address register, and the other cases match open-coded. */
#define NEXT_DIGIT(c) do { \
    (c) = *g_message_box.text++; \
    (c) -= '0'; \
} while (0)

/* name: inferred from its body, which types the message box text one character every other frame.
 * Control
 * characters: 0 ends, 13 starts a new line, '!' draws button glyph N,
 * '#' counts a line (pausing when the page is full), '$' indents N
 * cells, '%' pauses N*10 frames, '&' selects colour N. */
void message_box_type(void)
{
    unsigned char c;

    switch (g_message_box.scroll) {
    default:
        g_message_box.scroll = MESSAGE_SCROLL_IDLE;
        g_message_box.length = 0;
        break;
    case MESSAGE_SCROLL_TYPING:
        if (g_message_box.length <= 0) {
            g_message_box.scroll++;
            return;
        }
        if (++g_message_box.position & 1)
            return;
        g_message_box.length--;
        c = *g_message_box.text;
        g_message_box.text++;
        g_message_box_save_rect.x = g_message_box.x;
        g_message_box_save_rect.y = g_message_box.y + SCREEN_HEIGHT;
        MoveImage(&g_message_box_save_rect, g_message_box.x, g_message_box.y);
        switch (c) {
        case '#':
            if (++g_message_box.line > g_message_box.counter)
                g_message_box.scroll = MESSAGE_SCROLL_PAGE_FULL;
            break;
        case '\r':
            g_message_box.x = MESSAGE_BOX_LEFT;
            g_message_box.position = 0;
            g_message_box_save_rect.x = 0;
            g_message_box_save_rect.y = 0;
            g_message_box.y += MESSAGE_BOX_LINE_HEIGHT;
            break;
        case '!':
            c = *g_message_box.text++;
            c -= '0';
            g_message_box.x = draw_glyph_now(c + CHAR_BUTTON_ICONS, g_message_box.x, g_message_box.y, g_message_box_colours[MESSAGE_PAGE_BUTTON], g_message_box_shadow_colours[g_message_box.page]);
            break;
        case '%':
            c = *g_message_box.text++;
            c -= '0';
            g_message_box.scroll = MESSAGE_SCROLL_PAUSED;
            g_message_box.timer = c * MESSAGE_PAUSE_TICKS;
            break;
        case '$':
            NEXT_DIGIT(c);
            g_message_box.position = 0;
            g_message_box_save_rect.x = 0;
            g_message_box_save_rect.y = 0;
            g_message_box.x += c * MESSAGE_CELL_WIDTH;
            break;
        case '&':
            c = *g_message_box.text++;
            c -= '0';
            g_message_box.page = c;
            break;
        case 0:
            g_message_box.length = 0;
            break;
        case ' ':
            g_message_box.x += MESSAGE_CELL_WIDTH;
            break;
        default:
            g_message_box.x = draw_glyph_now(c, g_message_box.x, g_message_box.y, g_message_box_colours[g_message_box.page], g_message_box_shadow_colours[g_message_box.page]);
            break;
        case '\n':
            break;
        }
        if (g_message_box.position & MESSAGE_CURSOR_BLINK)
            draw_glyph_now('*', g_message_box.x, g_message_box.y, g_message_box_colours[0], g_message_box_shadow_colours[0]);
        break;
    case MESSAGE_SCROLL_PAUSED:
        if (--g_message_box.timer <= 0)
            g_message_box.scroll = MESSAGE_SCROLL_TYPING;
    case MESSAGE_SCROLL_PAGE_FULL:
        if (++g_message_box.position & MESSAGE_CURSOR_BLINK) {
            draw_glyph_now('*', g_message_box.x, g_message_box.y, g_message_box_colours[0], g_message_box_shadow_colours[0]);
        } else {
            g_message_box_save_rect.x = g_message_box.x;
            g_message_box_save_rect.y = g_message_box.y + SCREEN_HEIGHT;
            MoveImage(&g_message_box_save_rect, g_message_box.x, g_message_box.y);
        }
        if (g_message_box.scroll != MESSAGE_SCROLL_PAUSED && g_message_box.line <= g_message_box.counter)
            g_message_box.scroll = MESSAGE_SCROLL_TYPING;
        break;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void message_box_advance(void)
{
    ++g_message_box.counter;
}

/* Opens the message box for a mode: 2 shows the loading screen with the
 * level's picture, 7 the continue prompt, 8 the card text and 9 the
 * level's mission text. */
/* name: inferred from its body, which sets up the message box for a mode */
void open_message_box(short level, short mode)
{
    DISPENV disp;
    DRAWENV draw;
    RECT rect;
    unsigned char color[4];
    short x = LOADING_TEXT_X;
    short index;
    const unsigned char *p;

    color[0] = LOADING_TEXT_RED;
    color[1] = LOADING_TEXT_GREEN;
    color[2] = LOADING_TEXT_BLUE;
    switch (mode) {
    case 1: /* identity-TODO: modes 1 and 3..6 set nothing up */
    case 3:
    case 4:
    case 5:
    case 6:
        break;
    case MESSAGE_BOX_LOADING:
        g_message_box.x = MESSAGE_BOX_LEFT;
        g_message_box.y = MESSAGE_BOX_BRIEF_Y;
        g_message_box.text = g_mission_brief_header;
        setRECT(&rect, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
        index = FILE_PRISHOLD_BS;
        if (level < LEVEL_211_LIVING_QUARTERS)
            index = FILE_COLONY_BS;
        if (level >= LEVEL_311_TUNNELS)
            index = FILE_BONESHIP_BS;
        load_mdec_picture(index, &rect);
        SetDefDrawEnv(&draw, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        SetDefDispEnv(&disp, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        VSync(0);
        MoveImage(&rect, 0, 0);
        PutDispEnv(&disp);
        PutDrawEnv(&draw);
        SetDispMask(1);
        DrawSync(0);
        for (p = s_loading_data; *p != 0; p++)
            x = draw_glyph_now(*p, x, PROMPT_Y, color, g_message_box_shadow_colours[0]);
        break;
    case MESSAGE_BOX_CONTINUE:
        setRECT(&rect, 0, CONTINUE_STRIP_Y, SCREEN_WIDTH, CONTINUE_STRIP_HEIGHT);
        MoveImage(&rect, 0, PROMPT_Y);
        for (p = s_press_to_continue; *p != 0; p++) {
            if (*p == '!') {
                p++;
                x = draw_glyph_now(*p - '0' + CHAR_BUTTON_ICONS, x, PROMPT_Y, g_message_box_colours[MESSAGE_PAGE_BUTTON], g_message_box_shadow_colours[0]);
            } else {
                x = draw_glyph_now(*p, x, PROMPT_Y, color, g_message_box_shadow_colours[0]);
            }
        }
        return;
    case MESSAGE_BOX_PASSWORD:
        g_message_box.text = g_password_text;
        break;
    case MESSAGE_BOX_MISSION:
        load_mission_text(level);
        if (g_mission_text[0] == 0)
            g_message_box.text = s_no_message;
        else
            g_message_box.text = g_mission_text;
        g_message_box.x = MESSAGE_BOX_LEFT;
        g_message_box.y = MESSAGE_BOX_MISSION_Y;
        break;
    }
    g_message_box_save_rect.x = 0;
    g_message_box_save_rect.y = 0;
    g_message_box_save_rect.w = MESSAGE_BOX_CELL;
    g_message_box_save_rect.h = MESSAGE_BOX_CELL;
    g_message_box.length = strlen(g_message_box.text);
    g_message_box.timer = 0;
    g_message_box.position = 0;
    g_message_box.scroll = MESSAGE_SCROLL_TYPING;
    g_message_box.line = 0;
    g_message_box.counter = 0;
    g_message_box.column = 0;
    g_message_box.row = 0;
    g_message_box.choice = -1;
    g_message_box.page = 0;
    set_display_mode(DISPLAY_MODE_MESSAGE_BOX);
}

/* name: inferred from its body, which waits until the message box text is printed */
void wait_message_box(void)
{
    while (g_message_box.length != 0)
        VSync(0);
}

/* name: inferred from its body, which waits for VSync and moves the message box picture on screen */
void show_message_box(short wait)
{
    short x, y;
    wait_message_box();
    if (!(g_game_flags & GAME_FLAG_DEMO) && wait) {
        while (!(PadRead(0) & PADRdown))
            ;
    }
    x = g_message_box.x;
    y = g_message_box.y;
    g_message_box_save_rect.x = x;
    g_message_box_save_rect.y = y + SCREEN_HEIGHT;
    MoveImage(&g_message_box_save_rect, x, y);
    set_display_mode(DISPLAY_MODE_NORMAL);
}

/* Decodes an MDEC image file and uploads it to area in 16x16 tiles. */
/* name: inferred from its body, which decodes an MDEC file and uploads it in 16x16 tiles */
void load_mdec_picture(short index, RECT *area)
{
    short slot = heap_mark();
    unsigned long *tile = (unsigned long *)heap_alloc(MDEC_TILE_SIZE * MDEC_TILE_SIZE * sizeof(u_short));
    HeapFile *file;
    unsigned long *bitstream;
    unsigned long *runlevel;
    RECT rect;
    short x;
    short y;
    int size = MDEC_TILE_SIZE * MDEC_TILE_SIZE / 2;

    if (tile != 0) {
        file = &g_file_table[index];
        file->loaded_size = 0;
        file->buffer = 0;
        load_file(file);
        bitstream = file->buffer;
        DecDCTReset(0);
        runlevel = (unsigned long *)heap_alloc(DecDCTBufSize(bitstream) * sizeof(unsigned long));
        if (runlevel != 0) {
            DecDCTvlc(bitstream, runlevel);
            DecDCTin(runlevel, 0);
            VSync(0);
            SetDispMask(0);
            rect.w = MDEC_TILE_SIZE;
            rect.h = MDEC_TILE_SIZE;
            for (x = 0; x < area->w; x += MDEC_TILE_SIZE) {
                for (y = 0; y < area->h; y += MDEC_TILE_SIZE) {
                    DecDCTout(tile, size);
                    rect.x = x + area->x;
                    rect.y = y + area->y;
                    DecDCToutSync(0);
                    LoadImage(&rect, tile);
                    DrawSync(0);
                }
            }
        }
    }
    heap_release(slot);
}

/* name: inferred from its body, which sets up the texture pages and CLUTs and loads a sprite file */
void load_sprite_textures(int id, short index)
{
    TexturePage *textures[MENU_PICTURE_COUNT];
    ClutRow *palettes[MENU_PICTURE_COUNT];
    short i = 0;
    do {
        textures[i] = &g_texture_records[i];
        palettes[i] = &g_palette_records[i];
        textures[i]->tpage = GetTPage(TPAGE_MODE_8BIT, TPAGE_ABR_ADD_QUARTER, textures[i]->x, textures[i]->y);
        palettes[i]->clut = GetClut(palettes[i]->x, palettes[i]->y);
        i++;
    } while ((unsigned short)i < MENU_PICTURE_COUNT);
    sprite_file_load(id, &textures[index], &palettes[index], &g_sprite_uvs[index]);
}

/* Draws count 256-pixel-wide semi-transparent quads from sprite records index
 * onwards, shaded by shade; with two panels the second is 64 wide. No caller
 * in retail. */
/* name: inferred from its body, the POLY_FT4 counterpart of draw_picture_panels */
void draw_translucent_panels(short index, u_char shade, short count)
{
    short panel;
    POLY_FT4 *poly;
    u_long *uv;

    for (panel = 0; panel < count; panel++) {
        poly = g_sprite_workspace;
        uv = (u_long *)&g_sprite_uvs[index + panel];
        if (count == 2 && panel == 1) {
            int x0 = PANEL_WIDTH, x1 = SCREEN_WIDTH, y0 = 0, y1 = SCREEN_HEIGHT;

            setXY4(poly, x0, y0, x1, y0, x0, y1, x1, y1);
        } else {
            int x0 = panel * PANEL_WIDTH, y0 = 0, y1 = SCREEN_HEIGHT;

            setXY4(poly, x0, y0, x0 + PANEL_STEP, y0, x0, y1, x0 + PANEL_STEP, y1);
        }
        *(u_long *)&poly->u0 = *uv++;
        *(u_long *)&poly->u1 = *uv++;
        *(u_long *)&poly->u2 = *uv++;
        *(u_long *)&poly->u3 = *uv;
        setRGB0(poly, shade, shade, shade);
        setPolyFT4(poly);
        SetSemiTrans(poly, 1);
        addPrim(&g_render_buffer[OT_BACK_SLOT], poly);
        g_sprite_workspace = poly + 1;
    }
}

/* Draws count 256-pixel-wide sprite panels from sprite records index
 * onwards, shaded by shade; with two panels the second is 64 wide. */
/* name: inferred from its body, which draws count 256-wide sprite panels */
void draw_picture_panels(short index, u_char shade, short count)
{
    short panel;
    DR_MODE *mode;
    SPRT *sprite;

    for (panel = 0; panel < count; panel++) {
        mode = g_sprite_workspace;
        SetDrawMode(mode, 0, 1, g_sprite_uvs[index + panel].tpage, 0);
        sprite = g_sprite_workspace = mode + 1;
        if (count == 2 && panel == 1) {
            sprite->w = PANEL_LAST_WIDTH;
            sprite->h = SCREEN_HEIGHT;
        } else {
            sprite->w = PANEL_WIDTH;
            sprite->h = SCREEN_HEIGHT;
        }
        sprite->x0 = panel * PANEL_STEP;
        sprite->y0 = 0;
        sprite->u0 = 0;
        sprite->v0 = 0;
        sprite->clut = g_sprite_uvs[index + panel].clut;
        setRGB0(sprite, shade, shade, shade);
        SetSprt(sprite);
        addPrim(&g_render_buffer[OT_BACK_SLOT], sprite);
        addPrim(&g_render_buffer[OT_BACK_SLOT], mode);
        g_sprite_workspace = sprite + 1;
    }
}

/* Draws the two-panel 320x240 picture in sprite records index and
 * index + 1, shaded by shade. */
/* name: inferred from its body, which draws a two-panel 320x240 picture */
void draw_picture(short index, u_char shade)
{
    short panel;
    DR_MODE *mode;
    SPRT *sprite;

    for (panel = 0; panel < 2; panel++) {
        mode = g_sprite_workspace;
        SetDrawMode(mode, 0, 1, g_sprite_uvs[index + panel].tpage, 0);
        sprite = g_sprite_workspace = mode + 1;
        switch (panel) {
        case 0:
            sprite->w = PICTURE_PANEL_WIDTH;
            sprite->h = SCREEN_HEIGHT;
            sprite->x0 = 0;
            sprite->y0 = 0;
            break;
        case 1:
            sprite->w = PICTURE_LAST_PANEL_WIDTH;
            sprite->h = SCREEN_HEIGHT;
            sprite->x0 = PICTURE_PANEL_WIDTH;
            sprite->y0 = 0;
            break;
        }
        sprite->u0 = 0;
        sprite->v0 = 0;
        sprite->clut = g_sprite_uvs[index + panel].clut;
        setRGB0(sprite, shade, shade, shade);
        SetSprt(sprite);
        addPrim(&g_render_buffer[OT_BACK_SLOT], sprite);
        addPrim(&g_render_buffer[OT_BACK_SLOT], mode);
        g_sprite_workspace = sprite + 1;
    }
}

/* name: inferred from its body, which steps a fade in, hold and fade out */
int fade_step(TASK *task, short duration, short minimum)
{
    int state;

    switch (task->state[2]) {
    case FADE_IN:
        state = FADE_IN;
        task->state[1] += FADE_IN_STEP;
        if (task->state[1] > FADE_SHADE_MAX) {
            task->state[3] = 0;
            task->state[2]++;
        }
        break;
    case FADE_HOLD:
        state = FADE_HOLD;
        task->state[3]++;
        if (task->state[3] > minimum) {
            if ((g_pad_pressed & PADstart) || (g_pad_pressed & PADRdown))
                task->state[2]++;
        }
        if (task->state[3] > duration)
            task->state[2]++;
        break;
    case FADE_OUT:
        state = FADE_OUT;
        task->state[1] -= FADE_OUT_STEP;
        if (task->state[1] <= 0) {
            task->state[1] = 0;
            state = FADE_DONE;
            task->state[2]++;
        }
        break;
    default:
        state = FADE_DONE;
        break;
    }
    return state;
}

/* name: inferred from its body, which fades the title picture for the current state */
void title_picture_task(TASK *task)
{
    static short picture = MENU_PICTURE_TITLE;
    short index = MENU_PICTURE_TITLE;
    short shade;

    if (g_menu_screen != MENU_FADE_OUT && task->state[0] < TITLE_SHADE_MAX)
        task->state[0] += PICTURE_FADE_STEP;
    shade = task->state[0];
    switch (g_menu_screen) {
    case MENU_FADE_OUT:
        index = picture;
        if (task->state[0] >= PICTURE_FADE_MIN) {
            task->state[0] -= PICTURE_FADE_STEP;
        } else {
            task->state[0] = 0;
            g_menu_screen = MENU_ENTER_GAME;
        }
        break;
    case MENU_START_GAME:
    case MENU_ENTER_GAME:
        index = picture;
        break;
    case MENU_MAIN:
        break;
    default:
        index = MENU_PICTURE_MENU;
        break;
    }
    draw_picture(index, shade);
    picture = index;
    g_next_task = task->Next;
}

/* name: inferred from its body, which fades the menu picture in and out */
void menu_fade_task(TASK *task)
{
    if (g_menu_screen == MENU_CONTINUE_FADE || g_menu_screen == MENU_LEAVE) {
        if (task->state[0] >= PICTURE_FADE_MIN) {
            task->state[0] -= PICTURE_FADE_STEP;
        } else {
            task->state[0] = 0;
            g_menu_screen = MENU_LEAVE;
        }
    } else if (task->state[0] < MENU_FADE_SHADE_MAX) {
        task->state[0] += PICTURE_FADE_STEP;
        if (task->state[0] >= MENU_FADE_SHADE_READY)
            g_menu_screen = g_menu_next_state;
    }
    draw_picture(MENU_PICTURE_BACKGROUND, task->state[0]);
    g_next_task = task->Next;
}

/* name: inferred from its body, which clears the front and back areas */
void clear_screens(RECT *front, RECT *back)
{
    short separate = 0;

    if (front->x != back->x || front->y != back->y)
        separate = 1;
    VSync(0);
    ClearImage(front, 0, 0, 0);
    if (separate)
        ClearImage(back, 0, 0, 0);
    DrawSync(0);
    setup_double_buffer(front, back, 0, 1);
    VSync(0);
}

/* name: inferred from its body, which pulses g_highlight_color */
void highlight_pulse_task(TASK *task)
{
    if (task->state[0] != 0) {
        g_highlight_color[0] += 2;
        g_highlight_color[2] += 2;
        if (g_highlight_color[0] > HIGHLIGHT_MAX)
            task->state[0] = 0;
    } else {
        g_highlight_color[0] -= 2;
        g_highlight_color[2] -= 2;
        if (g_highlight_color[0] < HIGHLIGHT_MIN)
            task->state[0] = 1;
    }
    g_next_task = task->Next;
}

/* name: inferred from its body, which clears 960x240 of VRAM */
void clear_vram_area(void)
{
    RECT first;
    RECT second;

    first.x = 0; first.y = 0; first.w = MOVIE_SCREEN_WIDTH; first.h = SCREEN_HEIGHT;
    second.x = MOVIE_SCREEN_WIDTH; second.y = 0; second.w = MOVIE_SCREEN_WIDTH; second.h = SCREEN_HEIGHT;
    VSync(0);
    SetDispMask(0);
    ClearImage(&first, 0, 0, 0);
    ClearImage(&second, 0, 0, 0);
    DrawSync(0);
    VSync(0);
    SetDispMask(1);
}

/* title_sequence_task is the title sequence task: the intro movies, then the title picture, then menu_task */
/* name: runtime evidence, analysis/runtime/functions.json */
void title_sequence_task(TASK *task)
{
    short status;
    RECT unused[2]; /* rule exception (config/exceptions.json): retail's frame reserves 16 unused bytes */

    switch (task->state[0]) {
    case TITLE_START:
        task->state[0]++;
        break;
    case TITLE_RESET:
        task->state[1] = 0;
        task->state[2] = 0;
        task->state[0]++;
        break;
    case TITLE_WAIT:
        task->state[0]++;
        break;
    case TITLE_LOAD:
        load_sprite_textures(FILE_LOGOSGFX_BND, MENU_PICTURE_TITLE);
        font_load();
        if (g_title_intro_state == TITLE_INTRO_SEEN)
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0]++;
        break;
    case TITLE_PAUSE:
        task->state[0]++;
        break;
    case TITLE_BLANK:
        blank_screen();
        task->state[0]++;
        break;
    case TITLE_LEGAL:
        status = movie_play(MOVIE_LEGAL);
        blank_screen();
        if (status == MOVIE_ABORTED)
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0]++;
        break;
    case TITLE_FOX:
        clear_vram_area();
        status = movie_play(MOVIE_FOX_TITLE);
        if (status == MOVIE_ABORTED)
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0]++;
        break;
    case TITLE_ACCLAIM:
        clear_vram_area();
        status = movie_play(MOVIE_ACCLAIM);
        if (status == MOVIE_ABORTED)
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0] = TITLE_PROBE;
        break;
    case TITLE_PROBE:
        clear_vram_area();
        status = movie_play(MOVIE_PROBE);
        if (status == MOVIE_ABORTED)
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0]++;
        break;
    case TITLE_INTRO:
        blank_screen();
        movie_play(MOVIE_INTRO);
        task->state[0]++;
        break;
    case TITLE_PICTURE_START:
        clear_display_area();
        task->state[1] = 0;
        task->state[2] = 0;
        task->state[0]++;
        break;
    case TITLE_PICTURE: {
        TASK *picture = TaskCreate(TASK_PRIORITY_MENU, title_picture_task);

        picture->state[0] = 0;
        picture->state[1] = 0;
        task->state[1] = TITLE_PICTURE_TICKS;
        task->state[0]++;
        break;
    }
    case TITLE_PICTURE_WAIT:
        if (task->state[1] == 0) {
            task->state[0] = 0;
            task->callback = menu_task;
            g_title_intro_state = TITLE_INTRO_SEEN;
            g_menu_music_started = 0;
        } else {
            task->state[1]--;
        }
        break;
    }
    g_next_task = task->Next;
}

/* name: inferred from its body, which creates the render and pulse tasks and the card buffers */
void start_menu_tasks(void)
{
    TASK *task;
    /* forget the credits text (\GFX\CREDITS.TXT) so it is loaded again */
    g_file_table[FILE_CREDITS_TXT].loaded_size = 0;
    g_file_table[FILE_CREDITS_TXT].buffer = 0;
    task = TaskCreate(TASK_PRIORITY_OVERLAY, menu_display_task);
    g_render_task = task;
    task->state[0] = 1;
    task = TaskCreate(TASK_PRIORITY_MENU_FADE, highlight_pulse_task);
    task->state[0] = 0;
    g_highlight_color[1] = HIGHLIGHT_GREEN;
    g_highlight_color[0] = HIGHLIGHT_START;
    g_highlight_color[2] = HIGHLIGHT_START;
    g_card_status = CARD_STATUS_UNKNOWN;
    g_card_status_previous = -1;
    poll_card_status(1, 0);
    g_card_file_index = -1;
    g_menu_idle_frames = 0;
    g_8cedc = heap_alloc(MENU_8CEDC_SIZE);
    g_card_files = (struct DIRENTRY *)heap_alloc(CARD_DIRECTORY_SIZE * sizeof(struct DIRENTRY));
    g_card_headers = heap_alloc(CARD_DIRECTORY_SIZE * sizeof(CardHeader));
}

/* name: inferred from its body, which creates the front-end sequence task */
void start_title_sequence(void)
{
    TASK *task;
    start_menu_tasks();
    task = TaskCreate(TASK_PRIORITY_MENU, title_sequence_task);
    task->state[1] = 0;
    task->state[2] = 0;
    task->state[3] = TITLE_SEQUENCE_TICKS;
    task->state[0] = 0;
    g_menu_screen = MENU_MAIN;
    g_menu_next_state = MENU_MAIN;
    g_menu_return_state = MENU_LOAD_CARD;
    g_card_menu_saving = 0;
    if (!CHEAT_IS_ACTIVE(CHEAT_FLAG_CHEATS_MENU))
        g_start_level = 0;
}

/* name: inferred from its body, which creates the menu task for the results and save screens */
void start_results_menu(void)
{
    TASK *task;
    start_menu_tasks();
    task = TaskCreate(TASK_PRIORITY_MENU, menu_task);
    if (task != 0) {
        task->state[0] = 0;
        task->state[1] = 0;
        task->state[2] = 0;
        task->state[3] = 0;
    }
    g_menu_screen = MENU_FADE_OUT;
    g_menu_next_state = MENU_RESULTS;
    g_menu_return_state = MENU_SAVE_CARD;
    g_card_menu_saving = -1;
    task = TaskCreate(TASK_PRIORITY_MENU_FADE, menu_fade_task);
    if (task != 0) {
        task->state[0] = 0;
        task->state[1] = 0;
    }
    load_sprite_textures(FILE_LOGOSGFX_BND, MENU_PICTURE_RESULTS);
}

/* name: inferred from its body, which shows the level picture and the mission text */
void show_loading_screen(int mode)
{
    TexturePage *texture;
    ClutRow *palette;
    ClutRow *pal;
    unsigned short page;
    texture = &g_texture_records[MENU_PICTURE_LOADING];
    palette = &g_palette_records[MENU_PICTURE_LOADING];
    page = GetTPage(TPAGE_MODE_8BIT, TPAGE_ABR_HALF, g_texture_records[MENU_PICTURE_LOADING].x, g_texture_records[MENU_PICTURE_LOADING].y);
    pal = palette;
    texture->tpage = page;
    palette->clut = GetClut(pal->x, pal->y);
    open_message_box(mode, MESSAGE_BOX_LOADING);
    sprite_file_load(FILE_ICONS_IFF, &texture, &palette, g_icon_uvs);
    show_message_box(0);
    open_message_box(mode, MESSAGE_BOX_MISSION);
}

/* name: inferred from its body, which shows the continue prompt in the message box */
void show_continue_prompt(int mode)
{
    show_message_box(0);
    open_message_box(mode, MESSAGE_BOX_CONTINUE);
    show_message_box(-1);
}

/* name: inferred from its body, which returns value as a percentage of denominator, at most 100 */
short percentage(short denominator, short value)
{
    short quotient;

    if (value == 0)
        return 0;
    quotient = (value * 25) * 4 / denominator;
    if (quotient >= PERCENT_LIMIT)
        quotient = PERCENT_MAX;
    return quotient;
}

/* name: inferred from its body, which fills the kills, secrets and targets percentages */
short compute_level_results(LevelResult *result, short level)
{
    short i;
    short found = 0;
    short secrets;
    short total;
    short tally;
    short target_total;
    short target_tally;
    unsigned char class;

    g_player.tallies[TALLY_IDENTITY_TAGS] = g_player.identity_tags;
    result->shown[0] = 0;
    result->shown[1] = 0;
    result->shown[2] = 0;
    secrets = g_level_map.secret_count;
    for (i = 1; i < ARRAY_COUNT(g_secrets_found); i++) {
        if (g_secrets_found[i])
            found++;
    }
    if (secrets == 0) {
        secrets = 1;
        found = 1;
    }
    tally = 0;
    total = 0;
    for (i = 1; i < ACTOR_KIND_INERT; i++) {
        tally += g_player.tallies[i];
        total += g_level_map.kind_counts[i];
    }
    g_level_map.kind_counts[TALLY_TOTAL] = total;
    g_player.tallies[TALLY_TOTAL] = tally;
    target_total = 0;
    target_tally = 0;
    for (i = 0; i < TARGET_CLASSES_PER_LEVEL; i++) {
        class = g_level_target_classes[level][i];
        if (class != TARGET_CLASS_NONE) {
            target_tally += g_player.tallies[class];
            target_total += g_level_map.kind_counts[class];
        }
    }
    if (target_total == 0) {
        target_total = 1;
        target_tally = 1;
    }
    result->targets = percentage(target_total, target_tally);
    result->kills = percentage(total, tally);
    result->secrets = percentage(secrets, found);
    return result->targets;
}

/* name: runtime evidence, analysis/runtime/functions.json */
short mode_in_table(short mode)
{
    unsigned short index;
    for (index = 0; index < MODE_TABLE_SIZE; index++) {
        if (g_mode_table[(short)index] == mode)
            return -1;
    }
    return 0;
}

short f_1b674(short value)
{
    short result;
    switch (value) {
    case LEVEL_321_PILOTS_CHAMBERS_1:
    case LEVEL_322_PILOTS_CHAMBERS_2:
    case LEVEL_323_PILOTS_CHAMBERS_3:
    case LEVEL_324_PILOTS_CHAMBERS_4:
    case LEVEL_325_PILOTS_CHAMBERS_5:
        result = -1;
        break;
    default:
        result = 0;
        break;
    }
    return result;
}

/* name: inferred from its body, which fills the per-percent colour ramp of the results */
void build_result_colors(LevelResult *grid)
{
    unsigned char current[3];
    short i;
    unsigned char *base;
    unsigned char *last;
    grid->cells = heap_alloc(RESULT_COLOR_STEPS * RESULT_COLOR_SIZE);
    if (grid->cells == 0)
        return;
    current[0] = RAMP_MAX;
    current[1] = 0;
    current[2] = 0;
    base = grid->cells;
    last = base + 2;
    i = 0;
    do {
        if (current[0] != 0 && i >= RAMP_RED_FADE_START) {
            current[0] -= RAMP_STEP;
            if (current[0] < RAMP_STEP_FLOOR)
                current[0] = 0;
        }
        if (current[1] != RAMP_MAX && grid->par - RAMP_GREEN_LEAD < i) {
            current[1] += RAMP_STEP;
            if (current[1] >= RAMP_STEP_CEILING)
                current[1] = RAMP_MAX;
        }
        base[0] = current[0];
        last[-1] = current[1];
        last[0] = current[2];
        base += RESULT_COLOR_SIZE;
        last = (unsigned char *)((unsigned int)last + RESULT_COLOR_SIZE);
        ++i;
    } while (i < RESULT_COLOR_STEPS);
}

/* Level complete: grade the run, pick the next level, run the results
 * screen until it is dismissed, and return the next level. */
/* name: inferred from its body, which grades the level and runs the results screen */
short level_complete(int level)
{
    LevelResult result;
    short next = level;
    int grade = GRADE_FAILED;
    short last = mode_in_table(level + 1);
    short par;
    short percent;
    short slot;
    short saved_mode;

    par = PAR_SECTION_1;
    if (level >= LEVEL_311_TUNNELS)
        par = PAR_SECTION_3;
    if ((unsigned int)(level - LEVEL_211_LIVING_QUARTERS) < SECTION_2_LEVELS)
        par = PAR_SECTION_2;
    g_level_result = &result;
    percent = compute_level_results(&result, level);
    g_level_result->par = par;
    if (percent < par)
        grade = GRADE_FAILED;
    if (percent > par && percent < GRADE_GOOD_PERCENT)
        grade = GRADE_PASSED;
    if (percent >= GRADE_GOOD_PERCENT) {
        if (last) {
            g_game_flags |= GAME_FLAG_BEST_GRADE;
            grade = GRADE_BEST;
        } else {
            grade = GRADE_GOOD;
        }
    }
    if (grade) {
        next++;
        if (last && grade != GRADE_BEST)
            next++;
    }
    if (next != level)
        password_store_level(next);
    g_level_result->grade = grade;
    slot = heap_mark();
    start_results_menu();
    pad_reset();
    g_game_flags &= ~GAME_FLAG_MENU_DONE;
    if (mode_in_table(level) || f_1b674(level))
        g_menu_next_state = MENU_LEVEL_PASSWORD;
    build_result_colors(g_level_result);
    saved_mode = g_level;
    g_level = next;
    while (!(g_game_flags & GAME_FLAG_MENU_DONE)) {
        pad_poll();
        TaskDispatch();
    }
    TaskerInit();
    heap_release(slot);
    g_level = saved_mode;
    return next;
}

/* name: inferred from its body, which plays a movie between cleared screens */
void play_story_movie(short mode)
{
    RECT first;
    RECT second;
    g_render_task->state[0] = 0;
    first.x = 0; first.y = 0; first.w = MOVIE_SCREEN_WIDTH; first.h = SCREEN_HEIGHT;
    second.x = 0; second.y = SCREEN_HEIGHT; second.w = MOVIE_SCREEN_WIDTH; second.h = SCREEN_HEIGHT;
    VSync(0);
    SetDispMask(0);
    ClearImage(&first, 0, 0, 0);
    ClearImage(&second, 0, 0, 0);
    DrawSync(0);
    movie_play(mode);
    VSync(0);
    SetDispMask(0);
    ClearImage(&first, 0, 0, 0);
    ClearImage(&second, 0, 0, 0);
    DrawSync(0);
    VSync(0);
    first.x = 0; first.y = 0; first.w = SCREEN_WIDTH; first.h = SCREEN_HEIGHT;
    second.x = 0; second.y = SCREEN_HEIGHT; second.w = SCREEN_WIDTH; second.h = SCREEN_HEIGHT;
    setup_double_buffer(&first, &second, 1, 0);
    VSync(0);
    SetDispMask(1);
    g_render_task->state[0] = 1;
}

/* name: inferred from its body, which plays the death movie for g_death_type; main passes
 * the level, which it does not read */
void play_death_movie(int level)
{
    RECT first;
    RECT second;
    short mode;

    switch (g_death_type) {
    case DEATH_WARRIOR:
        mode = MOVIE_DEATH2;
        break;
    case DEATH_DOG:
        mode = MOVIE_DEATH5;
        break;
    case DEATH_QUEEN:
        mode = MOVIE_DEATH3;
        break;
    case DEATH_SHOT:
        mode = MOVIE_DEATH4;
        break;
    case DEATH_FACEHUGGER:
        mode = MOVIE_DEATH1;
        break;
    case DEATH_HAZARD:
    default:
        mode = MOVIE_DEATH6;
        break;
    }
    g_render_task->state[0] = 0;
    first.x = 0; first.y = 0; first.w = MOVIE_SCREEN_WIDTH; first.h = SCREEN_HEIGHT;
    second.x = MOVIE_SCREEN_WIDTH; second.y = 0; second.w = MOVIE_SCREEN_WIDTH; second.h = SCREEN_HEIGHT;
    VSync(0);
    SetDispMask(0);
    ClearImage(&first, 0, 0, 0);
    ClearImage(&second, 0, 0, 0);
    DrawSync(0);
    movie_play(mode);
    clear_vram_area();
    first.x = 0; first.y = 0; first.w = SCREEN_WIDTH; first.h = SCREEN_HEIGHT;
    second.x = SCREEN_WIDTH; second.y = 0; second.w = SCREEN_WIDTH; second.h = SCREEN_HEIGHT;
    setup_double_buffer(&first, &second, 1, 0);
    VSync(0);
    SetDispMask(1);
    g_render_task->state[0] = 1;
}

/* name: inferred from its body, which stores g_death_type */
void set_death_type(short value) { g_death_type = value; }

/* name: inferred from its body, which fades one credits line in and out */
void credits_line_task(TASK *node)
{
    unsigned int count = node->state[0];
    unsigned int remaining;
    unsigned int color;
    const char *text;
    unsigned char rgb[3];
    if (count < CREDITS_LINE_END)
        goto fallback;
    color = (unsigned int)g_menu_screen;
    if (color == MENU_CREDITS)
        goto active;
fallback:
    TaskDie(node);
    return;
active:
    remaining = count - 1;
    node->state[0] = remaining;
    text = (const char *)node->state[2];
    color = MENU_SHADE_FULL;
    if (remaining >= CREDITS_FADE_IN_FROM) {
        /* Preserve the unsigned halfword fade value before scaling it;
         * this expression also keeps the two fade branches independent. */
        color = ((unsigned int)(unsigned short)(CREDITS_FADE_IN_BASE - (unsigned short)node->state[0]) << 16) >> 14;
    } else {
        if (remaining < CREDITS_FADE_OUT_FROM)
            color = (short)node->state[0] * CREDITS_FADE_OUT_SCALE;
    }
    rgb[0] = color;
    rgb[1] = color;
    rgb[2] = color;
    draw_text_centered(text, 0, node->state[0], rgb);
    g_next_task = node->Next;
}

/* name: inferred from its body, which starts one credits line task per text line until "END" */
void credits_task(TASK *node)
{
    char *text = (char *)node->state[1];
    if (g_menu_screen != MENU_CREDITS || strncmp(text + 1, "END", CREDITS_END_LENGTH) == 0) {
        TaskDie(node);
        g_menu_screen = MENU_CREDITS_END;
        return;
    }
    if (node->state[0] == 0) {
        TASK *created = TaskCreate(TASK_PRIORITY_MENU, credits_line_task);
        if (created != 0) {
            created->state[0] = CREDITS_LINE_START;
            created->state[1] = 0;
            created->state[2] = node->state[1];
            while (*text != 0)
                text++;
            node->state[1] = (int)(text + 1);
            node->state[0] = CREDITS_LINE_INTERVAL;
        }
    } else {
        unsigned int remaining = node->state[0] - 1;
        node->state[0] = remaining;
    }
    g_next_task = node->Next;
}

/* Loads a text file from the CD file table once, turning carriage returns into
 * terminators, and returns its buffer. */
/* name: static evidence, analysis/runtime/functions.json */
char *load_text_file(short index)
{
    HeapFile *file = &g_file_table[index];
    unsigned int old_size = file->loaded_size;
    if (old_size == 0) {
        /* Preserve the signed-short counter from the target; its valid size range is unknown. */
        short count;
        char *cursor;
        file->loaded_size = 0;
        file->buffer = 0;
        load_file(file);
        cursor = (char *)file->buffer;
        count = 0;
        if (old_size < file->loaded_size) {
            do {
                if (*cursor == '\r')
                    *cursor = 0;
                ++count;
                ++cursor;
            } while (count < file->loaded_size);
        }
    }
    return (char *)file->buffer;
}

/* Starts the credits: loads the credits text (FILE_CREDITS_TXT) and runs credits_task over it. */
/* name: static evidence, analysis/runtime/functions.json */
void credits_start(void)
{
    void *buffer = load_text_file(FILE_CREDITS_TXT);
    TASK *node;
    if (buffer != 0) {
        node = TaskCreate(TASK_PRIORITY_MENU, credits_task);
        if (node != 0) {
            node->state[0] = CREDITS_FIRST_DELAY;
            node->state[1] = (int)buffer;
            music_play_track(MUSIC_TRACK_CREDITS);
        }
    }
}
