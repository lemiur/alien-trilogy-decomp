/* frontend.c -- front end: menus, memory card screens, password entry, message box, title and
 * results screens */
/* One translation unit: its .sdata runs from 0x8008cdb8 to 0x8008cf84 and its .text at least from
 * 0x80015f2c to 0x8001bfa0. */
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

#include <LIBCD.H>
#include <HEAPFILE.H>
#include <MOVIE.H>
#include <TRACK.H>
#include <SPRITE.H>
#include <ACTOR.H>
#include <VIEW.H>

/* The cheat codes are stored XORed with this key (Menu_DecodeCheatCode). name invented */
#define CHEAT_CODE_KEY 0x1f
/* Top rows of the main menu with and without its "Cheats" entry, and the row of the control
 * method screen's "Exit" (drawn by Menu_DrawExitLabel). names invented */
#define MAIN_MENU_Y 185
#define MAIN_MENU_Y_CHEATS 170
#define CONTROL_MENU_EXIT_Y 190
/* Menu_ComputeLevelResults: g_levelTargetClasses entries past a level's target classes. name invented */
#define TARGET_CLASS_NONE 0xff
/* Menu_BuildResultColors: one colour per percentage 0..100. name invented */
#define RESULT_COLOR_STEPS 101
/* Menu_LoadMDECPicture decodes and uploads the picture in 16x16 tiles. name invented */
#define MDEC_TILE_SIZE 16
/* identity-TODO: the size Menu_StartMenuTasks allocates for g_8cedc, which no matched code reads */
#define MENU_8CEDC_SIZE 0x1e0

/* Entries of each menu screen (the menuEntry_t arrays below). names: from the entry labels */
enum {
    MAIN_START,
    MAIN_OPTIONS,
    MAIN_CHEATS
};
enum {
    OPTIONS_SOUND,
    OPTIONS_EFFECTS_VOLUME,
    OPTIONS_MUSIC_VOLUME,
    OPTIONS_DIFFICULTY,
    OPTIONS_CONTROLLER,
    OPTIONS_PASSWORD,
    OPTIONS_CARD,
    OPTIONS_CAMERA_SWAY,
    OPTIONS_CREDITS,
    OPTIONS_EXIT
};
enum {
    CHEATS_LEVEL,
    CHEATS_INDESTRUCTIBLE,
    CHEATS_UNLIMITED_AMMO,
    CHEATS_VIDEO_MODE,
    CHEATS_GIMME_WEAPONS,
    CHEATS_EXIT
};
enum {
    PASSWORD_MENU_HEADING,
    PASSWORD_MENU_TEXT,
    PASSWORD_MENU_CONTINUE,
    PASSWORD_MENU_CARD
};
enum {
    CONTROL_HEADING,
    CONTROL_PICTURE,
    CONTROL_EXIT
};
/* g_saveCardMenu and g_loadCardMenu; CARD_MENU_TRANSFER is "Save game" or "Load game" */
enum {
    CARD_MENU_HEADING,
    CARD_MENU_CARD,
    CARD_MENU_TRANSFER,
    CARD_MENU_DELETE,
    CARD_MENU_FORMAT,
    CARD_MENU_EXIT,
    CARD_MENU_COUNT
};
/* The yes/no screens: g_cardFormatMenu, g_cardDeleteFileMenu, g_cardOverwriteMenu */
enum {
    CONFIRM_HEADING,
    CONFIRM_SUBJECT,
    CONFIRM_YES,
    CONFIRM_NO
};
enum {
    DELETE_MENU_HEADING,
    DELETE_MENU_FILE,
    DELETE_MENU_EXIT
};
/* g_cardLoadMenu and g_cardSaveMenu: entry n + 1 is save slot n, then "Exit" */
#define CARD_SLOT_MENU_EXIT ( SAVE_SLOT_COUNT + 1 )
/* g_menuCursors: the cursor each screen re-enters at. names invented */
enum {
    MENU_CURSOR_MAIN,
    MENU_CURSOR_OPTIONS,
    MENU_CURSOR_CONTROL,
    MENU_CURSOR_CHEATS,
    MENU_CURSOR_CARD,
    MENU_CURSOR_COUNT = 6
};
/* The card choice (g_cardChoices "None available", "Left", "Right"): a choice is the
 * CARD_PRESENT_* bit of its port. names: from the choice strings */
enum {
    CARD_CHOICE_NONE,
    CARD_CHOICE_LEFT,
    CARD_CHOICE_RIGHT,
    CARD_CHOICE_COUNT
};
/* g_cardStatus before the first poll, unequal to every Card_PresentMask result. name invented */
#define CARD_STATUS_UNKNOWN ( -2 )
/* The Sound entry's choices (g_soundModeChoices). names: from the choice strings */
enum {
    SOUND_MODE_EFFECTS,
    SOUND_MODE_MUSIC,
    SOUND_MODE_BOTH,
    SOUND_MODE_NONE
};
/* The cheat codes in g_nameTable, by the cheat each sets (Menu_CheckPassword). names: from those flags */
enum {
    CHEAT_CODE_CHEATS_MENU,
    CHEAT_CODE_GIMME_WEAPONS,
    CHEAT_CODE_UNLIMITED_AMMO,
    CHEAT_CODE_INDESTRUCTIBLE
};
#define CHEAT_CODE_BUFFER_SIZE 64
/* Bytes per stored cheat code in g_nameTable. name invented */
#define CHEAT_CODE_SIZE 22
/* A level password: LEVEL_CODE_PREFIX and two digits of the level. names invented */
#define LEVEL_CODE_PREFIX "G0LVL"
#define LEVEL_CODE_PREFIX_LENGTH ( sizeof LEVEL_CODE_PREFIX - 1 )
#define LEVEL_CODE_TEXT_SIZE 4
/* Front-end task priorities (<TASK.H> taskPriority_t lists the game's). names invented */
/* Music tracks of the menus and the credits. names invented */
#define MUSIC_TRACK_MENU 2
#define MUSIC_TRACK_CREDITS 8
/* g_modeTable, g_demoLevels and g_levelTargetClasses sizes. names invented */
#define MODE_TABLE_SIZE 8
#define DEMO_LEVEL_COUNT 7
#define TARGET_CLASSES_PER_LEVEL 5
/* Menu_Task: main-menu frames before a demo starts. name invented */
#define DEMO_IDLE_FRAMES 1800
/* Sprite records of the menu pictures (Menu_LoadSpriteTextures loads a file's pictures from a
 * record on; Menu_DrawPicture draws two records). names invented, from the screens showing them */
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
#define MENU_OT_BAR ( OT_LENGTH - 8 )
#define MENU_OT_BAR_FRAME ( OT_LENGTH - 10 )
/* Full shade of a picture or glyph. name invented */
#define MENU_SHADE_FULL 255
/* Menu_DrawPicture: a 240-pixel square panel and the rest of the screen; Menu_DrawPicturePanels and
 * Menu_DrawTranslucentPanels: 256-pixel panels placed PANEL_STEP apart. names invented */
#define PICTURE_PANEL_WIDTH 240
#define PICTURE_LAST_PANEL_WIDTH ( SCREEN_WIDTH - PICTURE_PANEL_WIDTH )
#define PANEL_WIDTH 256
#define PANEL_STEP ( PANEL_WIDTH - 1 )
#define PANEL_LAST_WIDTH ( SCREEN_WIDTH - PANEL_WIDTH )
/* Picture fades: Menu_TitlePictureTask up to TITLE_SHADE_MAX, Menu_FadeTask up to
 * MENU_FADE_SHADE_MAX (entering the next screen from MENU_FADE_SHADE_READY), both by
 * PICTURE_FADE_STEP and down to PICTURE_FADE_MIN. names invented */
#define TITLE_SHADE_MAX 140
#define MENU_FADE_SHADE_MAX 75
#define MENU_FADE_SHADE_READY 51
#define PICTURE_FADE_STEP 4
#define PICTURE_FADE_MIN 7
/* Menu_StepFade phases and shade steps. names invented */
enum {
    FADE_IN,
    FADE_HOLD,
    FADE_OUT,
    FADE_DONE
};
#define FADE_IN_STEP 4
#define FADE_OUT_STEP 6
#define FADE_SHADE_MAX 200
/* Menu_StepFade's task words: the shade, the phase and the hold counter. names invented */
#define FADE_STATE_SHADE 1
#define FADE_STATE_PHASE 2
#define FADE_STATE_TICKS 3
/* Menu_HighlightPulseTask: the highlight's red and blue pulse between these. names invented */
#define HIGHLIGHT_START 200
#define HIGHLIGHT_GREEN 240
#define HIGHLIGHT_MAX 250
#define HIGHLIGHT_MIN 100
/* Menu_TitleSequenceTask steps. names invented, from each step's work */
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
/* Menu_RunMenu: a window of up to MENU_WINDOW_LINES entries around the cursor; R1 and R2 change
 * values by MENU_STEP_FAST and MENU_STEP_FASTER with the cheats menu on. names invented */
#define MENU_TEXT_SIZE 128
#define MENU_WINDOW_HALF 5
#define MENU_WINDOW_LINES 10
#define MENU_WINDOW_LIMIT ( MENU_WINDOW_LINES + 1 )
#define MENU_CENTRE_X ( SCREEN_WIDTH / 2 )
#define MENU_STEP_FAST 10
#define MENU_STEP_FASTER 100
/* The volume entries' steps scale to the audio volume by this. name invented */
#define MENU_VOLUME_SCALE 10
/* A menu colour: red, green, blue and an unused fourth byte. name invented */
#define MENU_COLOR_BYTES 4
/* Card screens: polling intervals, attempts at each card operation, the used blocks at which
 * a card is full, the title characters shown, and the rows of their messages. names invented */
#define CARD_POLL_INTERVAL 10
#define CARD_POLL_SLOW 20
/* Menu_Task's task word holding the Menu_PollCardStatus timer. name invented */
#define MENU_STATE_CARD_POLL 3
#define CARD_ATTEMPTS 5
#define CARD_BLOCKS_FULL 14
#define CARD_TITLE_SHOWN 24
#define CARD_MESSAGE_Y 66
#define SAVE_MESSAGE_Y 70
#define PASSWORD_MESSAGE_Y 160
/* Menu_ShowMessage: frames a message stays. name invented */
#define MENU_MESSAGE_TICKS 200
/* Menu_MessageTask's task words: frames left, the close request (Menu_CloseMessage), the row
 * and the text. names invented */
#define MESSAGE_STATE_TICKS 0
#define MESSAGE_STATE_CLOSED 1
#define MESSAGE_STATE_Y 2
#define MESSAGE_STATE_TEXT 3
/* Menu_FormatCardSlot: "Section   - Level   " with the digits at these columns; section numbers.
 * names invented */
#define SLOT_TEXT_SECTION 8
#define SLOT_TEXT_LEVEL 18
enum {
    SECTION_1 = 1,
    SECTION_2,
    SECTION_3
};
#define SECTION_2_LEVELS ( LEVEL_311_TUNNELS - LEVEL_211_LIVING_QUARTERS )
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
#define PASSWORD_LINE_SIZE ( PASSWORD_LINE_LENGTH + 1 )
/* The password keyboard: KEYBOARD_ROWS rows of KEYBOARD_COLUMNS keys (g_passwordKeyCharacters),
 * the last row three wide keys; positions, spacing and the cursor brackets. names invented */
#define PASSWORD_KEY_COUNT 36
#define KEYBOARD_ROWS 4
#define KEYBOARD_COLUMNS 11
#define KEYBOARD_LAST_ROW ( KEYBOARD_ROWS - 1 )
#define KEYBOARD_LAST_COLUMN ( KEYBOARD_COLUMNS - 1 )
enum {
    KEY_DELETE = 33,
    KEY_ACCEPT,
    KEY_QUIT
};
#define WIDE_KEY_COUNT 3
/* Menu_PasswordKeyboardColumn: columns below WIDE_KEY_SPLIT_LEFT land on Delete, below
 * WIDE_KEY_SPLIT_RIGHT on Accept, the rest on Quit. names invented */
#define WIDE_KEY_SPLIT_LEFT 3
#define WIDE_KEY_SPLIT_RIGHT 7
#define KEY_TEXT_SIZE 16
#define KEYBOARD_TOP 50
#define KEYBOARD_LEFT 30
#define KEY_SPACING 24
#define WIDE_KEY_SPACING 96
#define KEY_BRACKET_OFFSET 12
/* The mission assessment screen (Menu_UpdateMissionAssessment, Menu_DrawPercentageBar). names invented */
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
/* A percentage bar: segments of BAR_SEGMENT_PERCENT (g_percentageBarWidths), dimmed to
 * 1 / BAR_DIM at the top, inside a BAR_FRAME_WIDTH x BAR_FRAME_HEIGHT frame. names invented */
#define PERCENT_MAX 100
#define PERCENT_LIMIT ( PERCENT_MAX + 1 )
#define BAR_SEGMENT_PERCENT 10
#define BAR_SEGMENT_LAST ( BAR_SEGMENT_PERCENT - 1 )
#define BAR_INSET 5
#define BAR_HEIGHT 34
#define BAR_DIM 4
#define BAR_FRAME_WIDTH 178
#define BAR_FRAME_HEIGHT 35
/* levelResult_t.colors: RESULT_COLOR_STEPS colours; Menu_BuildResultColors
 * ramps red down from RAMP_RED_FADE_START and green up from RAMP_GREEN_LEAD below par.
 * names invented */
#define RAMP_MAX 255
#define RAMP_RED_FADE_START 76
#define RAMP_GREEN_LEAD 10
#define RAMP_STEP 15
#define RAMP_STEP_FLOOR 16
#define RAMP_STEP_CEILING 240
/* Menu_LevelComplete: the par percentage of each section and the grades. names invented */
#define PAR_SECTION_1 15
#define PAR_SECTION_2 25
#define PAR_SECTION_3 45
#define GRADE_GOOD_PERCENT 85
enum {
    GRADE_FAILED,
    GRADE_PASSED,
    GRADE_GOOD,
    GRADE_BEST
};
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
/* Menu_LoadMissionText: the last byte of g_missionText. name invented */
#define MISSION_TEXT_LAST ( MISSION_TEXT_SIZE - 1 )
/* Credits: a line task starts at row CREDITS_LINE_START and moves up a row a frame, fading in
 * from CREDITS_FADE_IN_FROM and out below CREDITS_FADE_OUT_FROM; Menu_CreditsTask starts a line
 * every CREDITS_LINE_INTERVAL frames. names invented */
#define CREDITS_LINE_START 272
#define CREDITS_LINE_END 5
#define CREDITS_FADE_IN_FROM 181
#define CREDITS_FADE_IN_BASE 240
#define CREDITS_FADE_IN_SHIFT 2 /* the fade-in shade is four times the rows travelled */
#define CREDITS_FADE_OUT_FROM 20
#define CREDITS_FADE_OUT_SCALE 12
#define CREDITS_LINE_INTERVAL 16
#define CREDITS_FIRST_DELAY 18
#define CREDITS_END_LENGTH ( sizeof "END" - 1 )


/* Runtime library and other units. */
extern void Menu_FormatCardSlot( short slot, char *destination, int second, void *context );
extern void Menu_TitleSequenceTask( TASK * );

/* name: inferred from use: Menu_ScanCardSlots fills one entry per save slot through Password_SaveSlotLevels (-1 when empty) and Menu_SaveCardSlot stores the saved level */
extern short g_cardSlotLevels[];
/* name: runtime evidence, analysis/runtime/globals.json: Menu_RunMenu points it at g_textColor or
 * g_highlightColor for each entry it draws */
extern const unsigned char *g_menuEntryColor;
/* name: inferred from use: Menu_ReadCardDirectory stores the title of each save it finds on the card; the delete menu lists them */
extern const char *g_cardBlockNames[];
/* identity-TODO: 8 bytes of .data in the gap after g_demoLevels; written only by f_1760c, never read; holds
 * 0x800 at runtime (analysis/runtime/globals.json) */
extern int g_6c07c;
/* Sprite texture words (u0v0+clut, u1v1+tpage, u2v2, u3v3) copied into
 * POLY_FT4 packets by Menu_DrawControllerPicture and filled by Sprite_LoadFile; name invented. */
/* The CD file table: 171 entries, each a name and a CdlFILE. */
/* name: runtime evidence, analysis/runtime/globals.json: the MoveImage source rect 240 lines
 * below the message box, where the screen under the box is kept */
extern RECT g_messageBoxSaveRect;
/* name: runtime evidence, analysis/runtime/globals.json: Sprite_LoadFile fills it from \GFX\ICONS.IFF (file 0xa3) */
extern unsigned char g_iconUvs[];

/* Functions of this unit referenced before their definitions. */
extern char *Menu_FormatLabel( char *destination, const menuEntry_t *entry );
extern void Menu_StartCredits( void );
extern void Menu_FormatChoice( char *destination, menuEntry_t *entry, void *context );
extern void Menu_FormatVolumeBar( char *destination, menuEntry_t *entry );
extern void Menu_FormatCardChoice( char *destination, menuEntry_t *entry, void *context );
extern void Menu_DrawPasswordEntry( char *destination );
extern void Menu_FormatCardSlotHeading( char *destination );
extern char *Menu_FormatCardFileName( char *destination );
extern void Menu_FormatDecimal( char *destination, short value );
extern short Menu_UpdateMissionAssessment( void );
extern void Menu_TitlePictureTask( TASK *task );
extern void Menu_DrawExitLabel( char *destination, void *unused, TASK *record );
extern void Menu_DrawControllerPicture( char *destination, menuEntry_t *choice, TASK *state );
extern void Menu_BackToNextState( void *unused, TASK *record );
extern void Menu_Back( void *unused, TASK *record );
extern void Menu_OpenOptionsMenu( void *unused, TASK *record );
extern void Menu_StartCardSave( void *unused, TASK *record );
extern void Menu_StartCardLoad( void *unused, TASK *record );
extern void Menu_StartCardDelete( void *unused, TASK *record );
extern void Menu_OpenCardFormatMenu( void *unused, TASK *record );
extern void Menu_SelectCardFile( void *unused, TASK *record );
extern void Menu_SelectCardSaveSlot( void *context, TASK *record );
extern void Menu_OpenCheatsMenu( void *unused, TASK *record );
extern void Menu_OpenPasswordEntry( void *unused, TASK *record );
extern void Menu_OpenCardMenu( void *unused, TASK *record );
extern void Menu_OpenControlMethodMenu( void *unused, TASK *record );
extern void Menu_SelectVideoMode( const menuEntry_t *choice );
extern void Menu_StartGame( void );
extern void Menu_StartNewGame( void );
extern void Menu_ContinueGame( void *unused, TASK *record );
extern void Menu_SelectMusicVolume( void *unused, TASK *record );
extern void Menu_SelectSoundVolume( void *unused, TASK *record );
extern void Menu_ChangeMusicVolume( const menuEntry_t *choice );
extern void Menu_UpdateCardChoice( menuEntry_t *choice );
extern void Menu_ChangeSoundVolume( const menuEntry_t *choice );
extern int Menu_ChangeSoundMode();
extern void Menu_OpenCredits( void *unused, TASK *record );
extern void Menu_FormatSelectedCard( void );
extern void Menu_DeleteSelectedCardFile( void );
extern void Menu_ShowMessage( char *message, int unused, int y );
extern void Menu_MessageTask( TASK * );
extern void Menu_LoadCardSlot( int selection );
extern void Menu_FormatCardSlot0( int first, int second, void *context );
extern void Menu_FormatCardSlot1( int first, int second, void *context );
extern void Menu_FormatCardSlot2( int first, int second, void *context );
extern void Menu_FormatCardSlot3( int first, int second, void *context );
extern void Menu_FormatSelectedCardSlot( int first, int second, void *context );
extern void Menu_LoadCardSlot0( void );
extern void Menu_LoadCardSlot1( void );
extern void Menu_LoadCardSlot2( void );
extern void Menu_LoadCardSlot3( void );
extern void Menu_SaveCardSlot( short mode );
extern void Menu_ConfirmOverwrite( void *unused );
extern void Menu_DrawPasswordText( int unused, int y );
extern void Menu_LoadMissionText( unsigned short index );
extern void Menu_OpenMessageBox( short level, short mode );
extern void Menu_WaitMessageBox( void );
extern void Menu_ShowMessageBox( short wait );
extern void Menu_LoadMDECPicture( short index, RECT *area );
extern void Menu_LoadSpriteTextures( int id, short index );
extern void Menu_DrawPicture( short index, u_char shade );
extern void Menu_FadeTask( TASK *task );
extern void Menu_HighlightPulseTask( TASK *task );
extern void Menu_ClearVRAMArea( void );
extern void Menu_StartMenuTasks( void );
extern short Menu_Percentage( short denominator, short value );
extern short Menu_ComputeLevelResults( levelResult_t *result, short level );
extern short Menu_IsPilotsChambersLevel( short value );
extern void Menu_BuildResultColors( levelResult_t *result );
extern void Menu_CreditsLineTask( TASK *node );
extern unsigned short Menu_PollCardStatus( short timer, short interval );
extern short Menu_SelectedCardPort( void );
extern short Menu_DecodeCheatCode( short index, char *destination );
extern short Menu_CheckPassword( void );
extern int Menu_PasswordKeyboardColumn( short value, short mode );
extern short Menu_ReadCardDirectory( const char *title, menuCallback_t select );

/* Initialized data, in retail order. GCC emits an initializer's string
 * literals in reverse element order: those of eight bytes or less go to
 * .sdata, longer ones to .rdata. */

static char          s_noMessage[] = "NO MESSAGE";
static unsigned char s_loadingData[] = "* Loading data *";
static unsigned char s_pressToContinue[] = "Press !0 button to Continue...";
/* mission briefing header */
/* name: analysis/runtime/globals.json */
static char g_missionBriefHeader[] = "&1Incoming data transfer %2.%2.%2.\r\r&1Mission brief:\r";

/* The message box: idle (scroll and choice -1) until Menu_OpenMessageBox. */
static messageBox_t g_messageBox = { 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0 };

/* Menu_ModeInTable searches all eight entries for the requested mode. */
short g_modeTable[MODE_TABLE_SIZE] = { 2, 5, 7, 9, 16, 19, 27, 30 };

/* Message box text colours and the matching second colours Hud_DrawGlyphNow takes, by text page
 * (messageBox_t.page); entry 3 also highlights the "press" glyph. names: invented */
/* name: analysis/runtime/globals.json */
static unsigned char g_messageBoxColours[][MENU_COLOR_BYTES] = {
    { 0, 90, 0, 0 }, { 0, 255, 0, 0 }, { 0, 80, 0, 0 }, { 200, 200, 200, 0 }, { 80, 0, 0, 0 }
};
/* name: analysis/runtime/globals.json */
static unsigned char g_messageBoxShadowColours[][MENU_COLOR_BYTES] = {
    { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 200, 200, 200, 0 }, { 150, 150, 150, 0 }
};

/* Picture pages and CLUTs of the 16 menu pictures (Menu_LoadSpriteTextures); the last is
 * the loading screen's (Menu_ShowLoadingScreen). */
static texturePage_t g_textureRecords[MENU_PICTURE_COUNT] = {
    { 0, 0x280, 0x0, 0, 1, 0 },   { 0, 0x300, 0x0, 0, 1, 0 },   { 0, 0x380, 0x0, 0, 1, 0 },
    { 0, 0x0, 0x100, 0, 1, 0 },   { 0, 0x80, 0x100, 0, 1, 0 },  { 0, 0x180, 0x100, 0, 1, 0 },
    { 0, 0x200, 0x100, 0, 1, 0 }, { 0, 0x300, 0x100, 0, 1, 0 }, { 0, 0x380, 0x100, 0, 1, 0 },
    { 0, 0x140, 0x0, 0, 1, 0 },   { 0, 0x140, 0x0, 0, 1, 0 },   { 0, 0x140, 0x100, 0, 1, 0 },
    { 0, 0x240, 0x0, 0, 1, 0 },   { 0, 0x340, 0x0, 0, 1, 0 },   { 0, 0x300, 0x0, 0, 1, 0 },
    { 0, 0x380, 0x0, 0, 1, 0 }
};
static clutRow_t g_paletteRecords[MENU_PICTURE_COUNT] = { { 0, 0, 0xf2, 0 },  { 0, 0, 0xf3, 0 },  { 0, 0, 0xf4, 0 },
                                                          { 0, 0, 0xf5, 0 },  { 0, 0, 0xf6, 0 },  { 0, 0, 0x1fc, 0 },
                                                          { 0, 0, 0x1fd, 0 }, { 0, 0, 0x1fe, 0 }, { 0, 0, 0x1ff, 0 },
                                                          { 0, 0, 0xfc, 0 },  { 0, 0, 0x1fe, 0 }, { 0, 0, 0x1ff, 0 },
                                                          { 0, 0, 0x1fc, 0 }, { 0, 0, 0x1fd, 0 }, { 0, 0, 0x1fa, 0 },
                                                          { 0, 0, 0x1fb, 0 } };

/* name: inferred from use: Menu_Task loads the next demo level from it */
static short g_demoLevels[DEMO_LEVEL_COUNT] = { -1, 0, -1, 12, -1, 22, 0 };

/* Per level, the target classes Menu_ComputeLevelResults counts (indices of Player.tallies and
 * LevelScreen.kindCounts; identity-TODO: no kind enum yet), TARGET_CLASS_NONE = none. */
/* name: analysis/runtime/globals.json */
static unsigned char g_levelTargetClasses[][TARGET_CLASSES_PER_LEVEL] = {
    { 0x14, 0x19, 0x17, TARGET_CLASS_NONE, TARGET_CLASS_NONE },                                        /* level 0 */
    { 0x16, 0x18, 0x1b, 0x1a, TARGET_CLASS_NONE },                                                     /* level 1 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 2 */
    { 0x16, 0x18, 0x1b, 0x1a, TARGET_CLASS_NONE },                                                     /* level 3 */
    { 0x23, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 4 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 5 */
    { 0x14, 0x19, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },                           /* level 6 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 7 */
    { 0x0d, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 8 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 9 */
    { 0x0a, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 10 */
    { 0x07, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 11 */
    { 0x15, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 12 */
    { 0x23, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 13 */
    { 0x22, 0x21, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },                           /* level 14 */
    { 0x13, 0x11, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },                           /* level 15 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 16 */
    { 0x14, 0x19, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },                           /* level 17 */
    { 0x12, 0x10, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },                           /* level 18 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 19 */
    { 0x21, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 20 */
    { 0x07, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 21 */
    { 0x22, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 22 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 23 */
    { 0x05, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 24 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 25 */
    { 0x05, 0x23, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },                           /* level 26 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 27 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 28 */
    { 0x0d, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 29 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 30 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 31 */
    { 0x01, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE },              /* level 32 */
    { TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }, /* level 33 */
    { 0x07, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE, TARGET_CLASS_NONE }               /* level 34 */
};

/* Level-select choices of the cheats menu. */
const char *const g_levelNames[] = { "111 Entrance",
                                     "112 Outer complex",
                                     "113 Ammunition dump",
                                     "122 Recreation rooms",
                                     "131 Med. lab",
                                     "114 Ammunition dump 2",
                                     "141 Garage",
                                     "115 Ammunition dump 3",
                                     "154 Atmosphere processor",
                                     "155 Security catwalks",
                                     "161 Atmosphere basement",
                                     "162 Queens lair",
                                     "211 Living quarters",
                                     "212 Canteen",
                                     "213 Meeting tower",
                                     "222 Leadworks",
                                     "242 Tunnels and ducts 1",
                                     "231 Mining and smelting",
                                     "232 Furnace controls",
                                     "243 Tunnels and ducts 2",
                                     "262 Lead mould",
                                     "263 Queens lair",
                                     "311 Tunnels",
                                     "321 Pilots chambers 1",
                                     "331 Canyons and catacombs",
                                     "322 Pilots chambers 2",
                                     "351 Secrets",
                                     "352 Inorganics 1",
                                     "323 Pilots chambers 3",
                                     "371 Drop lifts",
                                     "353 Inorganics 2",
                                     "324 Pilots chambers 4",
                                     "381 Egg chambers",
                                     "325 Pilots chambers 5",
                                     "391 Queens lair" };

/* name: inferred from use: Menu_StartMenuTasks creates it with the render callback Sys_MenuDisplayTask and the screen-clear routines pause it through pad[0] */
TASK *g_renderTask = 0;
/* name: inferred from use: Menu_RunMenu draws the entry under the cursor in it and Menu_HighlightPulseTask pulses it */
unsigned char g_highlightColor[MENU_COLOR_BYTES] = { 128, 255, 128, 0 };
/* name: inferred from use: Menu_RunMenu draws every other enabled entry in it */
unsigned char g_textColor[MENU_COLOR_BYTES] = { 50, 200, 50, 0 };
/* name: inferred from use: Menu_RunMenu uses it for entries with flag 4 (greyed out) */
unsigned char g_disabledTextColor[MENU_COLOR_BYTES] = { 30, 30, 30, 0 };
/* name: inferred from use: Menu_RunMenu uses it for a greyed-out entry under the cursor */
unsigned char g_disabledHighlightColor[MENU_COLOR_BYTES] = { 60, 60, 60, 0 };
/* name: inferred from use: Menu_MessageTask draws the notification text in it */
unsigned char g_messageColor[MENU_COLOR_BYTES] = { 255, 60, 0, 0 };
/* name: inferred from use: Menu_CheckPassword sets it from a level password, Menu_Task from the demo list and the cheats menu, Menu_StartNewGame starts that level */
short g_startLevel = 0;
/* name: inferred from use: Menu_PollCardStatus stores the memory card poll result (Card_PresentMask) here */
short g_cardStatus = 0;
/* name: inferred from use: Menu_PollCardStatus compares it with the new poll result */
short g_cardStatusPrevious = -1;
/* name: inferred from use: the delete and save-slot callbacks store the chosen file or slot here */
short g_cardFileIndex = -1;
/* name: inferred from use: Menu_Task switches on it to run the current screen */
unsigned int g_menuScreen = MENU_NONE;
/* name: inferred from use: Menu_Task compares it with g_menuScreen to detect a screen change */
int g_menuScreenPrevious = 0;
/* name: inferred from use: Menu_FadeTask enters this state once the fade-in completes */
int g_menuNextState = MENU_NONE;
/* name: inferred from use: the Exit and error paths return to this state */
int g_menuReturnState = MENU_NONE;
/* name: inferred from use: Menu_SelectedCardPort reads the save menu when it is set and the load menu otherwise */
short g_cardMenuSaving = 0;
/* Front-end sequence flag: Menu_Task sets 1 at the end of the demo list, Menu_TitleSequenceTask tests 2. */
/* name: analysis/runtime/globals.json */
short g_titleIntroState = 0;
/* name: inferred from use: Menu_Task counts main-menu frames here and starts a demo after 1800 */
short g_menuIdleFrames = 0;
/* name: inferred from use: Menu_Task steps it through the seven-entry demo level list */
unsigned char g_demoIndex = 0;
/* name: inferred from use: Menu_Task starts the menu picture and music once and then sets it */
short g_menuMusicStarted = 0;

/* Choice lists. Unsized declarators place each array in .data; GCC 2.5.7
 * sizes an unsized array only after choosing the section, so -G8 does not
 * apply to the two-entry lists. */
const char *const g_soundModeChoices[] = { "SFX", "Music", "SFX and music", "None" };
const char *const g_booleanChoices[] = { "OFF", "ON" };
const char *const g_difficultyChoices[] = { "Acid Reign {", "Raging Terror {{", "Xenomania {{{" };

/* Debug track and movie lists with no remaining reader. g_trackNumbers
 * pairs with g_trackNames entry for entry; -1 is "Stop". */
const char *const g_trackNames[] = { "Stop",    "Track 2", "Track 3",  "Track 4",  "Track 5",  "Track 6", "Track 7",
                                     "Track 8", "Track 9", "Track 10", "Track 11", "Track 12", "Track 13" };
short             g_trackNumbers[] = { -1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24 };
const char *const g_movieNames[] = { "Probe",  "Intro",   "Death1", "Death2",  "Death3",  "Death4",   "Death5",
                                     "Death6", "Acclaim", "Legal",  "Bridge1", "Bridge2", "Game Over" };

const char *const g_cardChoices[] = { "None available", "Left", "Right" };
const char *const g_videoChoices[] = { "NTSC", "PAL" };

/* Saved cursor values restored into menu records on entry. */
short g_menuCursors[MENU_CURSOR_COUNT] = { 0 };

/* name: inferred from use: Menu_ShowMessage stores its task, Menu_CloseMessage ends it */
TASK *g_menuMessageTask = 0;
/* name: inferred from use: Menu_StartMenuTasks allocates it and Menu_ReadCardDirectory reads the card directory into it */
struct DIRENTRY *g_cardFiles = 0;
/* identity-TODO: Menu_StartMenuTasks stores a MENU_8CEDC_SIZE allocation here; no load of it in the
 * EXE (static gp/lui scan) and no read watchpoint hit (analysis/runtime/constants.json item 9) */
void *g_8cedc = 0;
/* name: inferred from use: Menu_StartMenuTasks allocates it and Menu_ReadCardDirectory reads each save header into it */
void *g_cardHeaders = 0;
/* name: inferred from use: Menu_Task stores the directory count from Menu_ReadCardDirectory here */
short g_cardFileCount = 0;
/* Label pointers; Menu_Task passes g_labelDeleteFile to Menu_ReadCardDirectory.
 * names: runtime evidence, analysis/runtime/globals.json (never written; each points at its text) */
const char *g_labelDeleteFile = "DELETE FILE";
const char *g_labelLoadFile = "LOAD FILE";
const char *g_labelExit = "Exit";
const char *g_labelUnreadableFile = "Unreadable file";
/* name: inferred from use: Menu_PasswordKeyboardInput appends and deletes password characters at this index */
short g_passwordLength = 0;
/* name: inferred from use: Menu_SetSoundMode switches the audio setup when this mode changes */
short g_soundMode = SOUND_MODE_BOTH;

/* Password keyboard: 36 keys. */
char g_passwordKeyCharacters[PASSWORD_KEY_COUNT] = "BCDFGHJKLMNPQRSTVWXYZ0123456789?*xyz";

/* One array per menu screen; the menu code passes each base and count. */
menuEntry_t g_optionsMenu[] = {
    { MENU_ENTRY_ACTIVE, 26, 2, 4, "Sound: ", g_soundModeChoices, (menuCallback_t)Menu_FormatChoice, 0, (menuCallback_t)Menu_ChangeSoundMode },
    { MENU_ENTRY_ACTIVE, 0, 5, 11, "Sound effects volume ", 0, (menuCallback_t)Menu_FormatVolumeBar,
      (menuCallback_t)Menu_SelectSoundVolume, (menuCallback_t)Menu_ChangeSoundVolume },
    { MENU_ENTRY_ACTIVE, 0, 5, 11, "Music volume ", 0, (menuCallback_t)Menu_FormatVolumeBar,
      (menuCallback_t)Menu_SelectMusicVolume, (menuCallback_t)Menu_ChangeMusicVolume },
    { MENU_ENTRY_ACTIVE, 0, 1, 3, "Difficulty: ", g_difficultyChoices, (menuCallback_t)Menu_FormatChoice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Controller options", 0, (menuCallback_t)Menu_FormatLabel,
      (menuCallback_t)Menu_OpenControlMethodMenu, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Enter password", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenPasswordEntry, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Memory Card management", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenCardMenu, 0 },
    { MENU_ENTRY_ACTIVE, 0, 1, 2, "Camera sway ", g_booleanChoices, (menuCallback_t)Menu_FormatChoice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Display credits", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenCredits, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_BackToNextState, 0 }
};

menuEntry_t g_cheatsMenu[] = {
    { MENU_ENTRY_ACTIVE, 28, 0, 35, "Level: ", g_levelNames, (menuCallback_t)Menu_FormatChoice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 2, "Player indestructible: ", g_booleanChoices, (menuCallback_t)Menu_FormatChoice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 2, "Unlimited ammunition: ", g_booleanChoices, (menuCallback_t)Menu_FormatChoice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 2, "Video Mode: ", g_videoChoices, (menuCallback_t)Menu_FormatChoice, 0, (menuCallback_t)Menu_SelectVideoMode },
    { MENU_ENTRY_ACTIVE, 0, 0, 2, "Gimme weapons: ", g_booleanChoices, (menuCallback_t)Menu_FormatChoice, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_BackToNextState, 0 }
};

menuEntry_t g_mainMenu[] = {
    { MENU_ENTRY_ACTIVE, MAIN_MENU_Y, 0, 0, "Start game", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_StartNewGame, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Options", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenOptionsMenu, 0 },
    { 0, 0, 0, 0, "Cheats", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenCheatsMenu, 0 }
};

menuEntry_t g_passwordMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "LEVEL PASSWORD", 0,
      (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_SPACE_TRIPLE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_DrawPasswordEntry, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Continue game", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_ContinueGame, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Memory Card management", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenCardMenu, 0 }
};

menuEntry_t g_controlMethodMenu[] = { { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_TRIPLE, 30, 0, 0,
                                        "SELECT CONTROL METHOD", 0, (menuCallback_t)Menu_FormatLabel, 0, 0 },
                                      { MENU_ENTRY_ACTIVE | MENU_ENTRY_SPACE_TRIPLE, CONTROL_MENU_EXIT_Y, 1, 4, 0, 0,
                                        (menuCallback_t)Menu_DrawControllerPicture, 0, 0 },
                                      { MENU_ENTRY_ACTIVE | MENU_ENTRY_SPACE_TRIPLE, 0, 0, 0, 0, 0,
                                        (menuCallback_t)Menu_DrawExitLabel, (menuCallback_t)Menu_OpenOptionsMenu, 0 } };

menuEntry_t g_passwordPromptMenu[] = {
    { MENU_ENTRY_ACTIVE, 200, 0, 0, "Enter password", 0, (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenOptionsMenu, 0 }
};

menuEntry_t g_saveCardMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "MEMORY CARD OPTIONS", 0,
      (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 4, "Selected Memory Card: ", g_cardChoices, (menuCallback_t)Menu_FormatCardChoice, 0,
      (menuCallback_t)Menu_UpdateCardChoice },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Save game", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_StartCardSave, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Delete block from Memory Card", 0, (menuCallback_t)Menu_FormatLabel,
      (menuCallback_t)Menu_StartCardDelete, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Format Memory Card", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenCardFormatMenu, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_BackToNextState, 0 }
};

menuEntry_t g_loadCardMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "MEMORY CARD OPTIONS", 0,
      (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 4, "Selected Memory Card: ", g_cardChoices, (menuCallback_t)Menu_FormatCardChoice, 0,
      (menuCallback_t)Menu_UpdateCardChoice },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Load game", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_StartCardLoad, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Delete block from Memory Card", 0, (menuCallback_t)Menu_FormatLabel,
      (menuCallback_t)Menu_StartCardDelete, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Format Memory Card", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenCardFormatMenu, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_OpenOptionsMenu, 0 }
};

/* Card prompts; octal characters are the controller-button glyphs. */
menuEntry_t g_cardFormatMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING, 50, 0, 0, "FORMAT MEMORY CARD IN", 0, (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 4, 0, 0,
      (menuCallback_t)Menu_FormatCardSlotHeading, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 0, "YES", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_FormatSelectedCard, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "NO", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_Back, 0 }
};

menuEntry_t g_cardDeleteMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_TRIPLE, 50, 0, 0, "SELECT BLOCK TO DELETE", 0,
      (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, g_cardBlockNames, (menuCallback_t)Menu_FormatCardFileName, (menuCallback_t)Menu_SelectCardFile, 0 },
    { MENU_ENTRY_ACTIVE | MENU_ENTRY_SPACE_TRIPLE, 0, 0, 0, "Exit", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_Back, 0 },
    { MENU_ENTRY_VISIBLE, 0, 0, 0, "Use \376 and \375 to select block", 0, (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_VISIBLE, 0, 0, 0, "Press \377 to delete block", 0, (menuCallback_t)Menu_FormatLabel, 0, 0 }
};

menuEntry_t g_cardDeleteFileMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_TRIPLE, 50, 0, 0, "DELETE FILE", 0,
      (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, 0, 0,
      (menuCallback_t)Menu_FormatCardFileName, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 0, "YES", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_DeleteSelectedCardFile, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "NO", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_StartCardDelete, 0 }
};

menuEntry_t g_cardOverwriteMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_TRIPLE, 50, 0, 0, "UPDATE SAVE GAME", 0,
      (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING, 50, 0, 0, 0, 0, (menuCallback_t)Menu_FormatSelectedCardSlot, 0, 0 },
    { MENU_ENTRY_ACTIVE, 40, 0, 0, "YES", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_ConfirmOverwrite, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "NO", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_StartCardSave, 0 }
};

menuEntry_t g_cardLoadMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "SELECT GAME TO LOAD", 0,
      (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_FormatCardSlot0, (menuCallback_t)Menu_LoadCardSlot0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_FormatCardSlot1, (menuCallback_t)Menu_LoadCardSlot1, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_FormatCardSlot2, (menuCallback_t)Menu_LoadCardSlot2, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_FormatCardSlot3, (menuCallback_t)Menu_LoadCardSlot3, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_Back, 0 }
};

menuEntry_t g_cardSaveMenu[] = {
    { MENU_ENTRY_VISIBLE | MENU_ENTRY_HEADING | MENU_ENTRY_SPACE_DOUBLE, 50, 0, 0, "SELECT GAME SAVE POSITION", 0,
      (menuCallback_t)Menu_FormatLabel, 0, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_FormatCardSlot0, (menuCallback_t)Menu_SelectCardSaveSlot, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_FormatCardSlot1, (menuCallback_t)Menu_SelectCardSaveSlot, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_FormatCardSlot2, (menuCallback_t)Menu_SelectCardSaveSlot, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, 0, 0, (menuCallback_t)Menu_FormatCardSlot3, (menuCallback_t)Menu_SelectCardSaveSlot, 0 },
    { MENU_ENTRY_ACTIVE, 0, 0, 0, "Exit", 0, (menuCallback_t)Menu_FormatLabel, (menuCallback_t)Menu_Back, 0 }
};

/* The request Menu_PlayMenuSound fills: full volume, centre balance, default pitch. */
/* name: inferred from use: Menu_PlayMenuSound stores the sample in it and passes it to Snd_PlayRequest */
playRequest_t g_menuSoundRequest = { 0, SPU_VOLUME_MAX / 2, 0, -1, 0 };

/* The cheat codes, each XORed with CHEAT_CODE_KEY (Menu_DecodeCheatCode). */
static unsigned char g_nameTable[][CHEAT_CODE_SIZE] = { ".X/KO.QT'\\.[]//KL/Q2", "[F.QXK/Q.XWK2", "]/G/IKM.\\TL2",
                                                        "/]S.I./QKM.O", "K/Y.Q.LW/YY2" };
/* Sprite of each controller layout (Menu_DrawControllerPicture), less 14. */
/* name: analysis/runtime/globals.json */
static short g_controllerLayoutSprites[] = { 0, 2, 3, 1 };
/* Widths of the ten percentage bar segments (Menu_DrawPercentageBar). */
/* name: analysis/runtime/globals.json */
static short g_percentageBarWidths[] = { 18, 18, 17, 17, 17, 16, 16, 17, 18, 18, 0 };

/*
==================
Menu_PlayMenuSound
==================
*/
/* name: inferred from its body, which plays a sample through the menu playRequest_t */
void Menu_PlayMenuSound( int sample )
{
    playRequest_t *request = &g_menuSoundRequest;
    request->sample = (short)sample;
    Snd_PlayRequest( request );
}

/*
==================
Menu_FormatLabel
==================
*/
/* name: inferred from its body, which copies the entry's label (the format callback of plain menu entries) */
char *Menu_FormatLabel( char *destination, const menuEntry_t *entry )
{
    return strcpy( destination, entry->label );
}

/*
==================
Menu_PollCardStatus
==================
*/
/* name: inferred from its body, which polls Card_PresentMask every interval frames and enables or greys out the card entries */
unsigned short Menu_PollCardStatus( short timer, short interval )
{
    unsigned short i;

    timer++;
    if ( timer > interval ) {
        timer = 0;
        g_cardStatusPrevious = g_cardStatus;
        g_cardStatus = Card_PresentMask();
        if ( g_cardStatusPrevious != g_cardStatus ) {
            switch ( g_cardStatus ) {
            case 0:
                g_optionsMenu[OPTIONS_CARD].flags = MENU_ENTRY_GREYED;
                g_passwordMenu[PASSWORD_MENU_CARD].flags = MENU_ENTRY_GREYED;
                g_loadCardMenu[CARD_MENU_CARD].flags = MENU_ENTRY_VISIBLE;
                g_loadCardMenu[CARD_MENU_EXIT].flags = MENU_ENTRY_ACTIVE;
                for ( i = CARD_MENU_TRANSFER; i < CARD_MENU_EXIT; i++ )
                    g_loadCardMenu[(short)i].flags = MENU_ENTRY_GREYED;
                for ( i = CARD_MENU_TRANSFER; i < CARD_MENU_EXIT; i++ )
                    g_saveCardMenu[(short)i].flags = MENU_ENTRY_GREYED;
                break;
            case CARD_PRESENT_PORT0:
            case CARD_PRESENT_PORT1:
            case CARD_PRESENT_BOTH:
                g_optionsMenu[OPTIONS_CARD].flags = MENU_ENTRY_ACTIVE;
                g_passwordMenu[PASSWORD_MENU_CARD].flags = MENU_ENTRY_ACTIVE;
                for ( i = CARD_MENU_CARD; i < CARD_MENU_COUNT; i++ )
                    g_loadCardMenu[(short)i].flags = MENU_ENTRY_ACTIVE;
                for ( i = CARD_MENU_TRANSFER; i < CARD_MENU_EXIT; i++ )
                    g_saveCardMenu[(short)i].flags = MENU_ENTRY_ACTIVE;
                break;
            }
        }
    }
    return timer;
}

/*
==================
Menu_SelectedCardPort
==================
*/
/* name: inferred from its body, which returns 0 or 1 for the card chosen in the save or load menu, -1 for none */
short Menu_SelectedCardPort( void )
{
    short mode;
    short result = -1;
    if ( g_cardMenuSaving != 0 )
        mode = g_saveCardMenu[CARD_MENU_CARD].value;
    else
        mode = g_loadCardMenu[CARD_MENU_CARD].value;
    if ( mode == CARD_CHOICE_LEFT )
        result = 0;
    if ( mode == CARD_CHOICE_RIGHT )
        result = 1;
    return result;
}

/*
==================
Menu_FormatCardSlotHeading
==================
*/
/* name: inferred from its body, which writes "LEFT"/"RIGHT" and " MEMORY CARD SLOT" */
void Menu_FormatCardSlotHeading( char *destination )
{
    short mode = Menu_SelectedCardPort();
    switch ( mode ) {
    case 0:
        memcpy( destination, "LEFT", sizeof "LEFT" );
        break;
    case 1:
        memcpy( destination, "RIGHT", sizeof "RIGHT" );
        break;
    default:
        destination[0] = 0;
        return;
    }
    strcat( destination, " MEMORY CARD SLOT" );
}

/*
==================
Menu_DecodeCheatCode
==================
*/
/* name: inferred from its body, which XORs entry index of g_nameTable with CHEAT_CODE_KEY into the buffer */
short Menu_DecodeCheatCode( short index, char *destination )
{
    unsigned char *source = g_nameTable[index];
    short          count = 0;
    while ( *source != 0 ) {
        *destination++ = *source++ ^ CHEAT_CODE_KEY;
        count++;
    }
    return count;
}

/*
==================
Menu_CheckPassword
==================
*/
/* name: inferred from its body, which compares the entered text with the cheat codes and the level prefix */
short Menu_CheckPassword( void )
{
    char         codeText[LEVEL_CODE_TEXT_SIZE];
    char         code[CHEAT_CODE_BUFFER_SIZE];
    unsigned int oldFlags = g_cheatFlags;
    short        result;

    if ( strncmp( g_passwordText, code, Menu_DecodeCheatCode( CHEAT_CODE_CHEATS_MENU, code ) ) == 0 )
        g_cheatFlags |= CHEAT_FLAG_CHEATS_MENU;
    if ( strncmp( g_passwordText, code, Menu_DecodeCheatCode( CHEAT_CODE_GIMME_WEAPONS, code ) ) == 0 )
        g_cheatFlags |= CHEAT_FLAG_GIMME_WEAPONS;
    if ( strncmp( g_passwordText, code, Menu_DecodeCheatCode( CHEAT_CODE_UNLIMITED_AMMO, code ) ) == 0 )
        g_cheatFlags |= CHEAT_FLAG_UNLIMITED_AMMO;
    if ( strncmp( g_passwordText, code, Menu_DecodeCheatCode( CHEAT_CODE_INDESTRUCTIBLE, code ) ) == 0 )
        g_cheatFlags |= CHEAT_FLAG_INDESTRUCTIBLE;
    result = oldFlags != g_cheatFlags;
    if ( strncmp( g_passwordText, LEVEL_CODE_PREFIX, LEVEL_CODE_PREFIX_LENGTH ) == 0 ) {
        codeText[LEVEL_CODE_TEXT_SIZE - 1] = 0;
        codeText[0] = g_passwordText[LEVEL_CODE_PREFIX_LENGTH];
        codeText[1] = g_passwordText[LEVEL_CODE_PREFIX_LENGTH + 1];
        g_startLevel = atoi( codeText );
        result = -1;
    }
    return result;
}

/*
==================
Menu_FormatDecimal
==================
*/
/* name: inferred from its body, which writes value as up to four decimal digits without leading zeros */
void Menu_FormatDecimal( char *destination, short value )
{
    unsigned char digit;
    short         length;
    short         started;

    strcpy( destination, "0" );
    length = 0;
    started = 0;
    digit = value / DECIMAL_RADIX_CUBED;
    if ( digit != 0 || started ) {
        destination[length++] = digit + '0';
        started = 1;
    }
    value -= digit * DECIMAL_RADIX_CUBED;
    digit = value / DECIMAL_RADIX_SQUARED;
    if ( digit != 0 || started ) {
        destination[length++] = digit + '0';
        started = 1;
    }
    value -= digit * DECIMAL_RADIX_SQUARED;
    digit = value / DECIMAL_RADIX;
    if ( digit != 0 || started ) {
        destination[length++] = digit + '0';
        started = 1;
    }
    value -= digit * DECIMAL_RADIX;
    digit = value;
    if ( digit != 0 || started )
        destination[length++] = digit + '0';
    if ( length != 0 )
        destination[length] = 0;
    else
        destination[1] = 0;
}

/* Digit buffer of the decimal formatters (Menu_FormatValueLabel). name: invented */
#define MENU_NUMBER_SIZE 8

/*
==================
Menu_FormatValueLabel

Formats the entry's label followed by its value as up to four decimal digits
without leading zeros (the digits of Menu_FormatDecimal). number is 8 bytes and
the "00000000" copy writes 9: retail's 16-byte frame holds the 8-byte buffer
and one 4-byte slot that the copy's terminator lands in.
==================
*/
/* name: inferred from its body, which appends the decimal value to the label */
void Menu_FormatValueLabel( char *destination, menuEntry_t *entry )
{
    char          number[MENU_NUMBER_SIZE];
    short         value;
    unsigned char digit;
    short         length;
    short         started;

    strcpy( number, "00000000" );
    strcpy( destination, entry->label );
    value = entry->value;
    length = 0;
    started = 0;
    digit = value / DECIMAL_RADIX_CUBED;
    if ( digit != 0 || started ) {
        number[length++] = digit + '0';
        started = 1;
    }
    value -= digit * DECIMAL_RADIX_CUBED;
    digit = value / DECIMAL_RADIX_SQUARED;
    if ( digit != 0 || started ) {
        number[length++] = digit + '0';
        started = 1;
    }
    value -= digit * DECIMAL_RADIX_SQUARED;
    digit = value / DECIMAL_RADIX;
    if ( digit != 0 || started ) {
        number[length++] = digit + '0';
        started = 1;
    }
    value -= digit * DECIMAL_RADIX;
    digit = value;
    if ( digit != 0 || started )
        number[length++] = digit + '0';
    if ( length != 0 )
        number[length] = 0;
    else
        number[1] = 0;
    strcat( destination, number );
}

/*
==================
Menu_DrawExitLabel
==================
*/
/* name: inferred from its body, which draws "Exit" in the selected or normal colour */
void Menu_DrawExitLabel( char *destination, void *unused, TASK *request )
{
    const unsigned char *color = g_textColor;

    if ( request->state[0] == CONTROL_EXIT )
        color = g_highlightColor;
    Text_DrawTextCentered( "Exit", 0, CONTROL_MENU_EXIT_Y, color );
    destination[0] = 0;
}

/*
==================
Menu_FormatCardFileName
==================
*/
/* name: inferred from its body, which copies the selected card file's title */
char *Menu_FormatCardFileName( char *destination )
{
    g_cardFileIndex = g_cardDeleteMenu[DELETE_MENU_FILE].value;
    return strcpy( destination, g_cardBlockNames[g_cardDeleteMenu[DELETE_MENU_FILE].value] );
}

/*
==================
Menu_FormatVolumeBar
==================
*/
/* name: inferred from its body, which appends one '{' per volume step to the label */
void Menu_FormatVolumeBar( char *dst, menuEntry_t *item )
{
    token_t token;
    short   i;
    token.brace = '{';
    token.zero = 0;
    strcpy( dst, item->label );
    for ( i = 0; i < item->value; i++ )
        strcat( dst, (char *)&token );
}

/*
==================
Menu_FormatChoice

The formatter call at 0x8001834c passes a third menu-context argument.
This basic choice formatter does not use that context.
==================
*/
/* name: inferred from its body, which appends the selected choice to the label */
void Menu_FormatChoice( char *destination, menuEntry_t *layout, void *context )
{
    *destination = '\0';
    if ( layout->label != 0 ) {
        strcpy( destination, layout->label );
    }
    if ( layout->choices != 0 ) {
        strcat( destination, layout->choices[layout->value] );
    }
}

/*
==================
Menu_DrawControllerPicture
==================
*/
/* name: inferred from its body, which draws the selected control method's controller sprite */
void Menu_DrawControllerPicture( char *destination, menuEntry_t *choice, TASK *state )
{
    POLY_FT4 *poly;
    u_long   *uv;
    u_char    shade;
    short     sprite;

    shade = CONTROLLER_SHADE_DIM;
    if ( state->state[0] == CONTROL_PICTURE )
        shade = MENU_SHADE_FULL;
    sprite = g_controllerLayoutSprites[choice->value] + MENU_PICTURE_CONTROLLERS;
    poly = g_spriteWorkspace;
    setXY4( poly, CONTROLLER_LEFT, CONTROLLER_TOP, CONTROLLER_RIGHT, CONTROLLER_TOP, CONTROLLER_LEFT, CONTROLLER_BOTTOM,
            CONTROLLER_RIGHT, CONTROLLER_BOTTOM );
    uv = (u_long *)&g_spriteUvs[sprite];
    *(u_long *)&poly->u0 = *uv++;
    *(u_long *)&poly->u1 = *uv++;
    *(u_long *)&poly->u2 = *uv++;
    *(u_long *)&poly->u3 = *uv;
    setRGB0( poly, shade, shade, shade );
    setPolyFT4( poly );
    addPrim( &g_renderBuffer[OT_BACK_SLOT], poly );
    g_spriteWorkspace = poly + 1;
    destination[0] = 0;
}

/*
==================
Menu_FormatCardChoice
==================
*/
/* name: inferred from its body, which forces the card choice to a present card before formatting */
void Menu_FormatCardChoice( char *destination, menuEntry_t *layout, void *context )
{
    if ( g_cardStatus == CARD_PRESENT_BOTH ) {
        if ( layout->value == CARD_CHOICE_NONE )
            layout->value = CARD_CHOICE_LEFT;
    } else {
        layout->value = g_cardStatus;
    }
    Menu_FormatChoice( destination, layout, context );
}

/*
==================
Menu_DrawPasswordEntry
==================
*/
/* name: inferred from its body, which draws the password text through Menu_DrawPasswordText */
void Menu_DrawPasswordEntry( char *destination )
{
    Menu_DrawPasswordText( 0, PASSWORD_TEXT_Y );
    destination[0] = ' ';
    destination[1] = 0;
}

/*
==================
Menu_DrawPercentageBar
==================
*/
/* name: inferred from its body, which draws one gouraud segment per 10% */
void Menu_DrawPercentageBar( short percent, short x, short y )
{
    short          remaining;
    unsigned char *color[2];
    short          index[2];
    short          segments;
    short          i;
    short          px;
    short          width;
    POLY_G4       *poly;
    POLY_FT4      *frame;
    u_long        *uv;

    color[0] = g_textColor;
    color[1] = g_textColor;
    remaining = percent;
    px = x + BAR_INSET;
    segments = remaining / BAR_SEGMENT_PERCENT;
    for ( i = 0; i < segments + 1; i++ ) {
        if ( remaining <= 0 )
            break;
        if ( g_levelResult->colors ) {
            index[0] = i * BAR_SEGMENT_PERCENT;
            index[1] = i * BAR_SEGMENT_PERCENT + BAR_SEGMENT_LAST;
            if ( index[0] > PERCENT_MAX )
                index[0] = PERCENT_MAX;
            if ( index[1] > PERCENT_MAX )
                index[1] = PERCENT_MAX;
            color[0] = &g_levelResult->colors[index[0]].r;
            color[1] = &g_levelResult->colors[index[1]].r;
        }
        poly = g_spriteWorkspace;
        if ( remaining >= BAR_SEGMENT_PERCENT )
            setXY4( poly, px, y, px + g_percentageBarWidths[i], y, px, y + BAR_HEIGHT, px + g_percentageBarWidths[i], y + BAR_HEIGHT );
        else {
            width = remaining * BAR_SEGMENT_PERCENT * g_percentageBarWidths[i] / PERCENT_MAX;
            setXY4( poly, px, y, px + width, y, px, y + BAR_HEIGHT, px + width, y + BAR_HEIGHT );
        }
        px += g_percentageBarWidths[i];
        remaining -= BAR_SEGMENT_PERCENT;
        setRGB0( poly, color[0][0] / BAR_DIM, color[0][1] / BAR_DIM, color[0][2] / BAR_DIM );
        setRGB1( poly, color[1][0] / BAR_DIM, color[1][1] / BAR_DIM, color[1][2] / BAR_DIM );
        setRGB2( poly, color[0][0], color[0][1], color[0][2] );
        setRGB3( poly, color[1][0], color[1][1], color[1][2] );
        setPolyG4( poly );
        addPrim( &g_renderBuffer[MENU_OT_BAR], poly );
        g_spriteWorkspace = poly + 1;
    }
    frame = g_spriteWorkspace;
    setXY4( frame, x, y, x + BAR_FRAME_WIDTH, y, x, y + BAR_FRAME_HEIGHT, x + BAR_FRAME_WIDTH, y + BAR_FRAME_HEIGHT );
    uv = (u_long *)&g_menuGlyphUvs[MENU_GLYPH_BAR_FRAME];
    *(u_long *)&frame->u0 = *uv++;
    *(u_long *)&frame->u1 = *uv++;
    *(u_long *)&frame->u2 = *uv++;
    *(u_long *)&frame->u3 = *uv;
    setRGB0( frame, MENU_SHADE_FULL, MENU_SHADE_FULL, MENU_SHADE_FULL );
    setPolyFT4( frame );
    SetSemiTrans( frame, 1 );
    SetShadeTex( frame, 1 );
    addPrim( &g_renderBuffer[MENU_OT_BAR_FRAME], frame );
    g_spriteWorkspace = frame + 1;
}

/*
==================
Menu_UpdateMissionAssessment
==================
*/
/* name: inferred from its body, which counts and draws the three result percentages */
short Menu_UpdateMissionAssessment( void )
{
    static short   blink = 0;
    char           text[ASSESSMENT_TEXT_SIZE];
    short          done;
    unsigned char *color;

    Text_DrawTextCentered( "MISSION ASSESSMENT", 0, ASSESSMENT_TITLE_Y, g_textColor );
    done = 0;
    if ( g_levelResult->shown[0] < g_levelResult->kills )
        g_levelResult->shown[0]++;
    else if ( g_levelResult->shown[1] < g_levelResult->secrets )
        g_levelResult->shown[1]++;
    else if ( g_levelResult->shown[2] < g_levelResult->targets )
        g_levelResult->shown[2]++;
    else
        done = -1;

    color = g_textColor;
    if ( g_levelResult->colors )
        color = &g_levelResult->colors[g_levelResult->shown[0]].r;
    Menu_FormatDecimal( text, g_levelResult->shown[0] );
    strcat( text, "%" );
    Text_DrawText( text, ASSESSMENT_PERCENT_X, ASSESSMENT_ALIENS_PERCENT_Y, color );
    Text_DrawText( "Aliens:", ASSESSMENT_LABEL_X, ASSESSMENT_ALIENS_Y, color );
    Menu_DrawPercentageBar( g_levelResult->shown[0], ASSESSMENT_BAR_X, ASSESSMENT_ALIENS_BAR_Y );

    if ( g_levelResult->colors )
        color = &g_levelResult->colors[g_levelResult->shown[1]].r;
    Menu_FormatDecimal( text, g_levelResult->shown[1] );
    strcat( text, "%" );
    Text_DrawText( text, ASSESSMENT_PERCENT_X, ASSESSMENT_SECRETS_PERCENT_Y, color );
    Text_DrawText( "Secrets:", ASSESSMENT_LABEL_X, ASSESSMENT_SECRETS_Y, color );
    Menu_DrawPercentageBar( g_levelResult->shown[1], ASSESSMENT_BAR_X, ASSESSMENT_SECRETS_BAR_Y );

    if ( g_levelResult->colors )
        color = &g_levelResult->colors[g_levelResult->shown[2]].r;
    Menu_FormatDecimal( text, g_levelResult->shown[2] );
    strcat( text, "%" );
    Text_DrawText( text, ASSESSMENT_PERCENT_X, ASSESSMENT_MISSION_PERCENT_Y, color );
    Text_DrawText( "Mission:", ASSESSMENT_LABEL_X, ASSESSMENT_MISSION_Y, color );
    Menu_DrawPercentageBar( g_levelResult->shown[2], ASSESSMENT_BAR_X, ASSESSMENT_MISSION_BAR_Y );

    if ( done ) {
        Text_DrawTextCentered( "Press \377 button to continue", 0, ASSESSMENT_PROMPT_Y, g_textColor );
        done = 0;
        if ( ( g_padPressed & PADstart ) || ( g_padPressed & PADRdown ) )
            done = -1;
    } else {
        blink = ( blink + 1 ) & TALLY_TICK_MASK;
        if ( blink == 0 )
            Snd_Play( SOUND_TALLY_TICK, TALLY_TICK_PITCH, SPU_VOLUME_MAX );
    }
    return done;
}

/*
==================
Menu_BackToNextState
==================
*/
/* name: inferred from its body, which enters g_menuNextState at the first saved cursor */
void Menu_BackToNextState( void *unused, TASK *record )
{
    g_menuScreen = g_menuNextState;
    record->state[0] = g_menuCursors[MENU_CURSOR_MAIN];
}

/*
==================
Menu_Back
==================
*/
/* name: inferred from its body, which enters g_menuReturnState at the first saved cursor */
void Menu_Back( void *unused, TASK *record )
{
    g_menuScreen = g_menuReturnState;
    record->state[0] = g_menuCursors[MENU_CURSOR_MAIN];
}

/*
==================
Menu_OpenOptionsMenu
==================
*/
/* name: inferred from its body, which enters state 8, the options screen */
void Menu_OpenOptionsMenu( void *unused, TASK *record )
{
    record->state[0] = g_menuCursors[MENU_CURSOR_OPTIONS];
    g_menuScreen = MENU_OPTIONS;
    record->state[MENU_STATE_CARD_POLL] = 0;
    g_cardStatus = Card_PresentMask();
}

/*
==================
Menu_OpenMainMenu
==================
*/
/* name: inferred from its body, which enters state 7, the main menu */
void Menu_OpenMainMenu( void *unused, TASK *record )
{
    g_menuScreen = MENU_MAIN;
    record->state[0] = g_menuCursors[MENU_CURSOR_MAIN];
}

/*
==================
Menu_StartCardSave
==================
*/
/* name: inferred from its body, which enters state 13, the free-space check before saving */
void Menu_StartCardSave( void *unused, TASK *record )
{
    g_menuScreen = MENU_SAVE_CHECK;
    record->state[0] = 0;
}

/*
==================
Menu_StartCardLoad
==================
*/
/* name: inferred from its body, which enters state 12, the save scan before loading */
void Menu_StartCardLoad( void *unused, TASK *record )
{
    g_menuScreen = MENU_LOAD_SCAN;
    record->state[0] = 0;
}

/*
==================
Menu_StartCardDelete
==================
*/
/* name: inferred from its body, which enters state 14, the directory read before deleting */
void Menu_StartCardDelete( void *unused, TASK *record )
{
    g_menuScreen = MENU_DELETE_SCAN;
    record->state[0] = 0;
}

/*
==================
Menu_OpenCardFormatMenu
==================
*/
/* name: inferred from its body, which enters state 17, the format confirmation */
void Menu_OpenCardFormatMenu( void *unused, TASK *record )
{
    g_menuScreen = MENU_FORMAT_CONFIRM;
    record->state[0] = CONFIRM_NO;
}

/*
==================
Menu_SelectCardFile
==================
*/
/* name: inferred from its body, which stores the chosen file and enters state 16, the delete confirmation */
void Menu_SelectCardFile( void *unused, TASK *record )
{
    unsigned short value = (unsigned short)record->state[0];
    g_menuScreen = MENU_DELETE_CONFIRM;
    record->state[0] = CONFIRM_NO;
    g_cardFileIndex = (short)( value - 1 );
}

/*
==================
Menu_SelectCardSaveSlot
==================
*/
/* name: inferred from its body, which an empty slot saves at once, a used one asks to overwrite (state 24) */
void Menu_SelectCardSaveSlot( void *context, TASK *record )
{
    short index = (unsigned short)record->state[0] - 1;
    g_cardFileIndex = index;
    if ( g_cardSlotLevels[index] == -1 ) {
        Menu_ConfirmOverwrite( context );
    } else {
        g_menuScreen = MENU_OVERWRITE_CONFIRM;
        record->state[0] = CONFIRM_NO;
    }
}

/*
==================
Menu_OpenCheatsMenu
==================
*/
/* name: inferred from its body, which enters state 9, the cheats screen */
void Menu_OpenCheatsMenu( void *unused, TASK *record )
{
    g_menuScreen = MENU_CHEATS;
    record->state[0] = g_menuCursors[MENU_CURSOR_CHEATS];
}

/*
==================
Menu_OpenPasswordEntry
==================
*/
/* name: inferred from its body, which enters state 1, the password keyboard */
void Menu_OpenPasswordEntry( void *unused, TASK *record )
{
    g_menuScreen = MENU_PASSWORD_ENTRY;
    record->state[0] = 0;
}

/*
==================
Menu_OpenCardMenu
==================
*/
/* name: inferred from its body, which enters the card menu when a card is present */
void Menu_OpenCardMenu( void *unused, TASK *record )
{
    if ( g_cardStatus ) {
        g_menuScreen = g_menuReturnState;
        record->state[0] = g_menuCursors[MENU_CURSOR_CARD];
    }
}

/*
==================
Menu_OpenControlMethodMenu
==================
*/
/* name: inferred from its body, which enters state 3, the control method screen */
void Menu_OpenControlMethodMenu( void *unused, TASK *record )
{
    g_menuScreen = MENU_CONTROL_METHOD;
    record->state[0] = g_menuCursors[MENU_CURSOR_CONTROL];
}

/*
==================
Menu_SelectVideoMode
==================
*/
/* name: inferred from its body, which calls SetVideoMode with the chosen entry */
void Menu_SelectVideoMode( const menuEntry_t *choice )
{
    if ( choice->value == 0 )
        SetVideoMode( MODE_NTSC );
    else
        SetVideoMode( MODE_PAL );
}

/*
==================
Menu_StartGame
==================
*/
/* name: inferred from its body, which applies the options and leaves the menus (state 4) */
void Menu_StartGame( void )
{
    g_gameFlags &= ~GAME_FLAG_HARD;
    if ( g_optionsMenu[OPTIONS_DIFFICULTY].value == DIFFICULTY_XENOMANIA )
        g_gameFlags |= GAME_FLAG_HARD;
    g_menuScreen = MENU_START_GAME;
    Sys_SetButtonLayout( g_controlMethodMenu[CONTROL_PICTURE].value );
    Player_SetModeControl( g_optionsMenu[OPTIONS_CAMERA_SWAY].value );
}

/*
==================
Menu_StartNewGame
==================
*/
/* name: inferred from its body, which starts the game at g_startLevel */
void Menu_StartNewGame( void )
{
    g_level = (int)g_startLevel;
    Menu_StartGame();
}

/*
==================
Menu_ContinueGame
==================
*/
/* name: inferred from its body, which enters state 25 to continue */
void Menu_ContinueGame( void *unused, TASK *record )
{
    g_menuScreen = MENU_CONTINUE;
}

/*
==================
Menu_SelectMusicVolume
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Menu_SelectMusicVolume( void *unused, TASK *record )
{
}

/*
==================
Menu_SelectSoundVolume
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Menu_SelectSoundVolume( void *unused, TASK *record )
{
}

/*
==================
f_1760c
==================
*/
/* identity-TODO: an entry change callback (stores the entry value) that no menu table, caller or
 * pointer references; not executed in any recorded runtime scenario (analysis/runtime/functions.json) */
void f_1760c( menuEntry_t *record )
{
    g_6c07c = record->value;
}

/*
==================
Menu_ChangeMusicVolume
==================
*/
/* name: inferred from its body, which sets the music volume from the entry value */
void Menu_ChangeMusicVolume( const menuEntry_t *choice )
{
    Music_SetVolume( choice->value * MENU_VOLUME_SCALE );
}

/*
==================
Menu_SetMusicVolume
==================
*/
/* name: inferred from its body, which sets the music volume and the options-menu value */
void Menu_SetMusicVolume( int value )
{
    Music_SetVolume( value * MENU_VOLUME_SCALE );
    g_optionsMenu[OPTIONS_MUSIC_VOLUME].value = (short)value;
}

/*
==================
Menu_UpdateCardChoice
==================
*/
/* name: inferred from its body, which keeps the card choice on a present card */
void Menu_UpdateCardChoice( menuEntry_t *choice )
{
    short mode = g_cardStatus;
    if ( mode == CARD_PRESENT_BOTH ) {
        if ( choice->value >= CARD_CHOICE_COUNT )
            choice->value = CARD_CHOICE_LEFT;
        else if ( choice->value <= 0 )
            choice->value = CARD_CHOICE_RIGHT;
    } else {
        choice->value = mode;
    }
}

/*
==================
Menu_ChangeSoundVolume
==================
*/
/* name: inferred from its body, which sets the effects volume from the entry value */
void Menu_ChangeSoundVolume( const menuEntry_t *choice )
{
    Snd_SetVolume( choice->value * MENU_VOLUME_SCALE );
}

/*
==================
Menu_SetSoundVolume
==================
*/
/* name: inferred from its body, which sets the effects volume and the options-menu value */
void Menu_SetSoundVolume( int value )
{
    Snd_SetVolume( value * MENU_VOLUME_SCALE );
    g_optionsMenu[OPTIONS_EFFECTS_VOLUME].value = value;
}

/*
==================
Menu_GetSoundVolume
==================
*/
/* name: inferred from its body, which returns the options-menu effects volume */
short Menu_GetSoundVolume( void )
{
    return g_optionsMenu[OPTIONS_EFFECTS_VOLUME].value;
}

/*
==================
Menu_GetMusicVolume
==================
*/
/* name: inferred from its body, which returns the options-menu music volume */
short Menu_GetMusicVolume( void )
{
    return g_optionsMenu[OPTIONS_MUSIC_VOLUME].value;
}

/*
==================
Menu_GetDifficulty
==================
*/
/* name: inferred from its body, which returns the options menu's Difficulty entry value */
short Menu_GetDifficulty( void )
{
    return g_optionsMenu[OPTIONS_DIFFICULTY].value;
}

/*
==================
Menu_SetDifficulty
==================
*/
/* name: inferred from its body, which sets the options menu's Difficulty entry value */
void Menu_SetDifficulty( short value )
{
    g_optionsMenu[OPTIONS_DIFFICULTY].value = value;
}

/*
==================
Menu_GetControlMethod
==================
*/
/* name: inferred from its body, which returns the control-method menu's entry value */
short Menu_GetControlMethod( void )
{
    return g_controlMethodMenu[CONTROL_PICTURE].value;
}

/*
==================
Menu_OptionsSetControlMethod
==================
*/
void Menu_OptionsSetControlMethod( short value )
{
    g_controlMethodMenu[CONTROL_PICTURE].value = value;
    Sys_SetButtonLayout( value );
}

/*
==================
Menu_SetSoundMode
==================
*/
/* name: inferred from its body, which switches the audio setup when the sound mode changes */
int Menu_SetSoundMode( short mode )
{
    if ( mode == g_soundMode )
        return 1;
    g_soundMode = mode;
    switch ( mode ) {
    case SOUND_MODE_EFFECTS:
        Music_Disable();
        Snd_Enable();
        Snd_Init();
        return Snd_LoadBank( FILE_111SFX_BIN, SAMPLE_LEVEL_FIRST );
    case SOUND_MODE_MUSIC:
        Music_Enable();
        return Snd_Disable();
    case SOUND_MODE_BOTH:
        Music_Enable();
        Snd_Enable();
        Snd_Init();
        return Snd_LoadBank( FILE_111SFX_BIN, SAMPLE_LEVEL_FIRST );
    default:
        Music_Disable();
        return Snd_Disable();
    }
}

/*
==================
Menu_ChangeSoundMode
==================
*/
/* name: inferred from its body, the Sound entry's change callback, which applies the entry value through Menu_SetSoundMode */
int Menu_ChangeSoundMode( menuEntry_t *entry )
{
    return Menu_SetSoundMode( entry->value );
}

/*
==================
Menu_PlaySelectedMovie
==================
*/
/* name: inferred from its body, which plays the movie the entry value selects */
void Menu_PlaySelectedMovie( menuEntry_t *entry )
{
    Sys_UnloadLevel();
    Movie_Play( entry->value );
}

/*
==================
Menu_BlankScreen
==================
*/
/* name: inferred from its body, which clears both buffers with the render task stopped */
void Menu_BlankScreen( void )
{
    RECT first;
    RECT second;

    g_renderTask->state[0] = 0;
    first.x = 0;
    first.y = 0;
    first.w = MOVIE_SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = MOVIE_SCREEN_WIDTH;
    second.y = 0;
    second.w = MOVIE_SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;

    VSync( 0 );
    SetDispMask( 0 );
    ClearImage( &first, 0, 0, 0 );
    ClearImage( &second, 0, 0, 0 );
    DrawSync( 0 );
    VSync( 0 );
    SetDispMask( 1 );
}

/*
==================
Menu_ClearDisplayArea
==================
*/
/* name: inferred from its body, which clears 960x240 and moves the 320x240 front buffer */
void Menu_ClearDisplayArea( void )
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

    VSync( 0 );
    SetDispMask( 0 );
    ClearImage( &first, 0, 0, 0 );
    ClearImage( &second, 0, 0, 0 );
    DrawSync( 0 );

    first.x = 0;
    first.y = 0;
    first.w = SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = SCREEN_WIDTH;
    second.y = 0;
    second.w = SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;
    Sys_SetupDoubleBuffer( &first, &second, 1, 0 );

    VSync( 0 );
    SetDispMask( 1 );
    g_renderTask->state[0] = 1;
}

/*
==================
Menu_OpenCredits
==================
*/
/* name: inferred from its body, which enters state 19 and starts the credits */
void Menu_OpenCredits( void *unused, TASK *record )
{
    g_menuScreen = MENU_CREDITS;
    record->state[0] = 0;
    Menu_StartCredits();
}

/*
==================
Menu_FormatSelectedCard
==================
*/
/* name: inferred from its body, which formats the selected card and reports it */
void Menu_FormatSelectedCard( void )
{
    short selected = Menu_SelectedCardPort();
    if ( selected != -1 ) {
        Card_Format( selected );
        Menu_ShowMessage( "Memory Card formatted", 0, CARD_MESSAGE_Y );
    }
    g_menuScreen = g_menuReturnState;
    g_menuScreenPrevious = g_menuReturnState;
}

/*
==================
Menu_DeleteSelectedCardFile
==================
*/
/* name: inferred from its body, which deletes the selected file from the selected card */
void Menu_DeleteSelectedCardFile( void )
{
    short mode = Menu_SelectedCardPort();
    if ( mode != -1 )
        Card_DeleteFile( g_cardFiles[g_cardFileIndex].name, mode );
    g_menuScreen = MENU_DELETE_SCAN;
}

/*
==================
Menu_MessageTask
==================
*/
/* name: inferred from its body, which draws Menu_ShowMessage's text until it times out */
void Menu_MessageTask( TASK *node )
{
    const char *message = (const char *)node->state[MESSAGE_STATE_TEXT];
    if ( node->state[MESSAGE_STATE_CLOSED] != 0 || node->state[MESSAGE_STATE_TICKS] == 0 ) {
        TaskDie( node );
    } else {
        Text_DrawTextCentered( message, 0, node->state[MESSAGE_STATE_Y], (const unsigned char *)&g_messageColor );
        node->state[MESSAGE_STATE_TICKS]--;
        g_nextTask = node->Next;
    }
}

/*
==================
Menu_CloseMessage
==================
*/
/* name: inferred from its body, which ends the current menu message task */
void Menu_CloseMessage( void )
{
    if ( g_menuMessageTask != 0 )
        g_menuMessageTask->state[MESSAGE_STATE_CLOSED] = -1;
    g_menuMessageTask = 0;
}

/*
==================
Menu_ShowMessage
==================
*/
/* name: inferred from its body, which starts a notification task with the text */
void Menu_ShowMessage( char *message, int unused, int y )
{
    TASK *entry;
    Menu_CloseMessage();
    entry = TaskCreate( TASK_PRIORITY_MENU, Menu_MessageTask );
    if ( entry != 0 ) {
        entry->state[MESSAGE_STATE_TICKS] = MENU_MESSAGE_TICKS;
        entry->state[MESSAGE_STATE_CLOSED] = 0;
        entry->state[MESSAGE_STATE_Y] = (short)y;
        entry->state[MESSAGE_STATE_TEXT] = (int)message;
        g_menuMessageTask = entry;
    }
}

/*
==================
Menu_LoadCardSlot
==================
*/
/* name: inferred from its body, which loads a save slot from the selected card, retrying five times */
void Menu_LoadCardSlot( int selection )
{
    short selected = (short)selection;
    short current = Menu_SelectedCardPort();
    short attempts = 0;
    int   status;

    if ( current != -1 ) {
        do {
            status = Password_LoadFromCard( selected, current );
            attempts++;
        } while ( status != 0 && attempts < CARD_ATTEMPTS );

        if ( status == 0 ) {
            g_gameFlags |= GAME_FLAG_RESTORED;
            Menu_StartGame();
        } else {
            Menu_ShowMessage( "Error loading from card", 0, CARD_MESSAGE_Y );
        }
    }
}

/*
==================
Menu_FormatCardSlot
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Menu_FormatCardSlot( short slot, char *destination, int second, void *context )
{
    short level = g_cardSlotLevels[slot];
    short section;
    short stage;
    /* RULE-EXCEPTION: R17-dead-local, retail's frame holds 2-8 unused bytes (sweep: char[1] and none give 0x20, char[12] gives 0x30); char number[MENU_NUMBER_SIZE] (8 bytes) is the digit buffer the sibling formatter Menu_FormatValueLabel declares for the same four-digit decimal; ruled out: Menu_FormatDecimal is a jal, the two strcpy builtins expand to register block moves (no local gives the smaller frame), no macro or aggregate temporary */
    char number[MENU_NUMBER_SIZE];
    if ( level == -1 ) {
        strcpy( destination, "Unused" );
    } else {
        strcpy( destination, "Section   - Level   " );
        section = SECTION_2;
        stage = level - LEVEL_211_LIVING_QUARTERS;
        if ( level < LEVEL_211_LIVING_QUARTERS ) {
            section = SECTION_1;
            stage = level;
        }
        if ( level >= LEVEL_311_TUNNELS ) {
            section = SECTION_3;
            stage = level - LEVEL_311_TUNNELS;
        }
        destination[SLOT_TEXT_SECTION] = section + '0';
        Menu_FormatDecimal( destination + SLOT_TEXT_LEVEL, stage + 1 );
    }
}

/*
==================
Menu_FormatCardSlot0
==================
*/
/* name: inferred from its body, which formats save slot 0's row */
void Menu_FormatCardSlot0( int first, int second, void *context )
{
    Menu_FormatCardSlot( 0, first, second, context );
}

/*
==================
Menu_FormatCardSlot1
==================
*/
/* name: inferred from its body, which formats save slot 1's row */
void Menu_FormatCardSlot1( int first, int second, void *context )
{
    Menu_FormatCardSlot( 1, first, second, context );
}

/*
==================
Menu_FormatCardSlot2
==================
*/
/* name: inferred from its body, which formats save slot 2's row */
void Menu_FormatCardSlot2( int first, int second, void *context )
{
    Menu_FormatCardSlot( 2, first, second, context );
}

/*
==================
Menu_FormatCardSlot3
==================
*/
/* name: inferred from its body, which formats save slot 3's row */
void Menu_FormatCardSlot3( int first, int second, void *context )
{
    Menu_FormatCardSlot( SAVE_SLOT_LAST, first, second, context );
}

/*
==================
Menu_FormatSelectedCardSlot
==================
*/
/* name: inferred from its body, which formats the row of the chosen slot */
void Menu_FormatSelectedCardSlot( int first, int second, void *context )
{
    Menu_FormatCardSlot( g_cardFileIndex, first, second, context );
}

/*
==================
Menu_LoadCardSlot0
==================
*/
/* name: inferred from its body, which loads save slot 0 */
void Menu_LoadCardSlot0( void )
{
    Menu_LoadCardSlot( 0 );
}

/*
==================
Menu_LoadCardSlot1
==================
*/
/* name: inferred from its body, which loads save slot 1 */
void Menu_LoadCardSlot1( void )
{
    Menu_LoadCardSlot( 1 );
}

/*
==================
Menu_LoadCardSlot2
==================
*/
/* name: inferred from its body, which loads save slot 2 */
void Menu_LoadCardSlot2( void )
{
    Menu_LoadCardSlot( 2 );
}

/*
==================
Menu_LoadCardSlot3
==================
*/
/* name: inferred from its body, which loads save slot 3 */
void Menu_LoadCardSlot3( void )
{
    Menu_LoadCardSlot( SAVE_SLOT_LAST );
}

/*
==================
Menu_ScanCardSlots
==================
*/
/* name: inferred from its body, which reads the slot levels and enables the used slots */
short Menu_ScanCardSlots( void )
{
    short slot = Menu_SelectedCardPort();
    short tries;
    short i;
    int   result;

    if ( slot != -1 ) {
        tries = 0;
        do {
            result = Password_SaveSlotLevels( g_cardSlotLevels, slot );
            tries++;
        } while ( result != 0 && tries < CARD_ATTEMPTS );
        for ( i = 1; i < CARD_SLOT_MENU_EXIT; i++ ) {
            if ( g_cardSlotLevels[i - 1] == -1 )
                g_cardLoadMenu[i].flags = MENU_ENTRY_GREYED;
            else
                g_cardLoadMenu[i].flags = MENU_ENTRY_ACTIVE;
        }
        if ( result == 0 )
            return -1;
    }
    return 0;
}

/*
==================
Menu_SaveCardSlot
==================
*/
/* name: inferred from its body, which saves to a slot on the selected card, retrying five times */
void Menu_SaveCardSlot( short mode )
{
    short slot = Menu_SelectedCardPort();
    short attempts = 0;
    int   result;
    if ( slot != -1 ) {
        while ( attempts < CARD_ATTEMPTS ) {
            result = Password_SaveToCard( mode, slot );
            attempts++;
            if ( !result )
                break;
        }
        if ( result == 0 ) {
            Menu_ShowMessage( "Game Saved", 0, SAVE_MESSAGE_Y );
            g_cardSlotLevels[mode] = g_level;
        } else {
            Menu_ShowMessage( "Error saving to card", 0, SAVE_MESSAGE_Y );
        }
    }
}

/*
==================
Menu_ConfirmOverwrite
==================
*/
/* name: inferred from its body, which saves to the chosen slot after the overwrite prompt */
void Menu_ConfirmOverwrite( void *unused )
{
    Menu_SaveCardSlot( g_cardFileIndex );
    g_menuScreen = MENU_SAVE_SLOTS;
    g_menuScreenPrevious = MENU_SAVE_SLOTS;
}

/*
==================
Menu_RunMenu
==================
*/
/* name: inferred from its body, which draws a menu screen and handles its input */
void Menu_RunMenu( menuEntry_t *menu, short count, TASK *record )
{
    char                 text[MENU_TEXT_SIZE];
    menuEntry_t         *entry;
    short                first;
    short                last;
    short                i;
    short                y;
    short                step;
    short                width;
    const unsigned char *color;

    while ( !( menu[record->state[0]].flags & MENU_ENTRY_SELECTABLE ) ) {
        record->state[0]++;
        if ( record->state[0] >= count )
            record->state[0] = 0;
    }
    last = (short)record->state[0] + MENU_WINDOW_HALF;
    first = (short)record->state[0] - MENU_WINDOW_HALF;
    if ( first < 0 ) {
        last = count < MENU_WINDOW_LIMIT ? count : MENU_WINDOW_LINES;
        first = 0;
    }
    if ( last > count ) {
        first = count < MENU_WINDOW_LINES ? 0 : count - MENU_WINDOW_LINES;
        last = count;
    }
    y = menu[0].y;
    entry = menu;
    entry += first;
    for ( i = first; i < last; entry++, i++ ) {
        if ( !( entry->flags & MENU_ENTRY_VISIBLE ) )
            continue;
        if ( entry->flags & MENU_ENTRY_DISABLED ) {
            color = g_disabledTextColor;
            if ( record->state[0] == i )
                color = g_disabledHighlightColor;
        } else {
            color = g_textColor;
            if ( record->state[0] == i )
                color = g_highlightColor;
        }
        g_menuEntryColor = color;
        entry->callback0( text, entry, record );
        if ( strlen( text ) ) {
            width = Text_Width( text ) >> 1;
            if ( entry->flags & MENU_ENTRY_SMALL )
                Text_DrawTextMultiline( text, MENU_CENTRE_X - width, y, g_menuEntryColor );
            else
                Text_DrawText( text, MENU_CENTRE_X - width, y, g_menuEntryColor );
            if ( entry->flags & MENU_ENTRY_SPACE_DOUBLE )
                y += MENU_LINE_SPACING_DOUBLE;
            else if ( entry->flags & MENU_ENTRY_SPACE_TRIPLE )
                y += MENU_LINE_SPACING_TRIPLE;
            else if ( entry->flags & MENU_ENTRY_SMALL )
                y += MENU_LINE_SPACING_SMALL;
            else
                y += MENU_LINE_SPACING;
        }
    }
    if ( g_padPressed & PADLup ) {
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
        do {
            if ( record->state[0] == 0 )
                record->state[0] = count - 1;
            else
                record->state[0]--;
        } while ( !( menu[record->state[0]].flags & MENU_ENTRY_SELECTABLE ) );
    }
    if ( g_padPressed & PADLdown ) {
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
        do {
            record->state[0]++;
            if ( record->state[0] >= count )
                record->state[0] = 0;
        } while ( !( menu[record->state[0]].flags & MENU_ENTRY_SELECTABLE ) );
    }
    entry = &menu[record->state[0]];
    step = 1;
    if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_CHEATS_MENU ) ) {
        if ( g_padHeld & PADR1 )
            step = MENU_STEP_FAST;
        if ( g_padHeld & PADR2 )
            step = MENU_STEP_FASTER;
    }
    if ( g_padRepeat & PADLleft ) {
        if ( !( entry->flags & MENU_ENTRY_SILENT ) )
            Menu_PlayMenuSound( MENU_SOUND_MOVE );
        entry->value -= step;
        if ( entry->value < 0 )
            entry->value = entry->limit - 1;
        if ( entry->callback2 )
            entry->callback2( entry, record );
    }
    if ( g_padRepeat & PADLright ) {
        if ( !( entry->flags & MENU_ENTRY_SILENT ) )
            Menu_PlayMenuSound( MENU_SOUND_MOVE );
        entry->value += step;
        if ( entry->value >= entry->limit )
            entry->value = 0;
        if ( entry->callback2 )
            entry->callback2( entry, record );
    }
    if ( ( g_padPressed & PADstart ) || ( g_padPressed & PADRdown ) ) {
        if ( entry->callback1 ) {
            if ( !( entry->flags & MENU_ENTRY_SILENT ) )
                Menu_PlayMenuSound( MENU_SOUND_SELECT );
            entry->callback1( entry, record );
        }
    }
}

/*
==================
Menu_ClearPassword
==================
*/
/* name: inferred from its body, which fills the password text with '-' */
void Menu_ClearPassword( void )
{
    short i;
    g_passwordLength = 0;
    for ( i = 0; i < PASSWORD_LENGTH; i++ )
        g_passwordText[i] = '-';
    g_passwordText[PASSWORD_LENGTH] = 0;
}

/*
==================
Menu_DrawPasswordText
==================
*/
/* name: inferred from its body, which draws the password text centred in 16-character lines */
void Menu_DrawPasswordText( int unused, int y )
{
    short length = (short)strlen( (const char *)g_passwordText );
    short offset = 0;
    void *drawState = g_textColor;
    char  line[PASSWORD_LINE_SIZE];

    while ( length > 0 ) {
        int count = (short)length;
        int width;
        int x;

        if ( count >= PASSWORD_LINE_SIZE )
            count = PASSWORD_LINE_LENGTH;
        strncpy( line, (const char *)g_passwordText + offset, count );
        line[count] = 0;
        width = Text_Width( line );
        x = ( SCREEN_WIDTH - width ) / 2;
        Text_DrawText( line, x, (short)y, drawState );
        length -= count;
        offset += count;
        y += MENU_LINE_SPACING;
    }
}

/*
==================
Menu_DrawPasswordKeyboard

Draws the password keyboard: 36 keys in rows of 11, the last three
being Delete/Accept/Quit, with the cursor key highlighted and
bracketed.
==================
*/
/* name: inferred from its body, which draws the 36 password keys with the cursor */
void Menu_DrawPasswordKeyboard( void )
{
    char                 text[KEY_TEXT_SIZE];
    short                cursor = g_passwordPromptMenu[0].value;
    short                selectedWidth = 0;
    short                key = 0;
    short                x;
    short                y = KEYBOARD_TOP;
    short                row;
    short                column;
    short                width;
    const unsigned char *color;

    for ( row = 0; row < KEYBOARD_ROWS; row++, y += KEY_SPACING ) {
        x = KEYBOARD_LEFT;
        for ( column = 0; column < KEYBOARD_COLUMNS; column++, key++ ) {
            if ( (unsigned short)key >= PASSWORD_KEY_COUNT )
                break;
            switch ( key ) {
            case KEY_DELETE:
                strcpy( text, "Delete " );
                break;
            case KEY_ACCEPT:
                strcpy( text, "Accept " );
                break;
            case KEY_QUIT:
                strcpy( text, "Quit " );
                break;
            default:
                text[0] = g_passwordKeyCharacters[key];
                text[1] = 0;
                break;
            }
            width = key >= KEY_DELETE ? Text_Width( text ) - KEY_BRACKET_OFFSET : 0;
            if ( cursor == key ) {
                color = g_highlightColor;
                selectedWidth = width;
            } else {
                color = g_textColor;
            }
            Text_DrawText( text, x, y, color );
            x += key >= KEY_DELETE ? WIDE_KEY_SPACING : KEY_SPACING;
        }
    }
    y = g_passwordPromptMenu[0].value / KEYBOARD_COLUMNS;
    column = g_passwordPromptMenu[0].value % KEYBOARD_COLUMNS;
    if ( cursor >= KEY_DELETE )
        x = column * WIDE_KEY_SPACING + KEYBOARD_LEFT;
    else
        x = column * KEY_SPACING + KEYBOARD_LEFT;
    y = y * KEY_SPACING + KEYBOARD_TOP;
    color = g_highlightColor;
    Text_DrawText( "[", x - KEY_BRACKET_OFFSET, y, color );
    Text_DrawText( "]", x + selectedWidth + KEY_BRACKET_OFFSET, y, color );
    Menu_DrawPasswordText( 0, KEYBOARD_PASSWORD_Y );
}

/*
==================
Menu_PasswordKeyboardColumn
==================
*/
/* name: inferred from its body, which clamps the cursor column on the short last row */
int Menu_PasswordKeyboardColumn( short value, short mode )
{
    if ( mode < KEYBOARD_LAST_ROW )
        return value;
    if ( value < WIDE_KEY_SPLIT_RIGHT )
        return value >= WIDE_KEY_SPLIT_LEFT;
    return 2;
}

/*
==================
Menu_PasswordKeyboardInput
==================
*/
/* name: inferred from its body, which moves the keyboard cursor and handles the keys */
void Menu_PasswordKeyboardInput( TASK *record )
{
    menuEntry_t *entry = g_passwordPromptMenu;
    short        column = entry->value % KEYBOARD_COLUMNS;
    short        row = entry->value / KEYBOARD_COLUMNS;
    int          wrap;

    if ( g_padRepeat & PADLleft ) {
        column--;
        if ( row >= KEYBOARD_LAST_ROW ) {
            if ( column < 0 )
                column = 2;
        } else if ( column < 0 ) {
            column = KEYBOARD_LAST_COLUMN;
        }
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
    }
    if ( g_padRepeat & PADLright ) {
        column++;
        if ( row >= KEYBOARD_LAST_ROW )
            wrap = column < WIDE_KEY_COUNT;
        else
            wrap = column < KEYBOARD_COLUMNS;
        if ( !wrap )
            column = 0;
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
    }
    if ( g_padRepeat & PADLup ) {
        row--;
        if ( row < 0 )
            row = KEYBOARD_LAST_ROW;
        column = Menu_PasswordKeyboardColumn( column, row );
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
    }
    if ( g_padRepeat & PADLdown ) {
        row++;
        if ( row >= KEYBOARD_ROWS )
            row = 0;
        column = Menu_PasswordKeyboardColumn( column, row );
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
    }
    entry->value = column + row * KEYBOARD_COLUMNS;
    if ( ( g_padPressed & PADstart ) || ( g_padPressed & PADRdown ) ) {
        Menu_PlayMenuSound( MENU_SOUND_MOVE );
        switch ( g_passwordKeyCharacters[entry->value] ) {
        default:
            if ( g_passwordLength < PASSWORD_LENGTH ) {
                g_passwordText[g_passwordLength] = g_passwordKeyCharacters[entry->value];
                g_passwordLength++;
            }
            break;
        case 'x':
            if ( g_passwordLength > 0 )
                g_passwordLength--;
            g_passwordText[g_passwordLength] = '-';
            break;
        case 'y':
            if ( Password_Decode() == 0 ) {
                Menu_PlayMenuSound( MENU_SOUND_SELECT );
                Menu_StartGame();
                g_gameFlags |= GAME_FLAG_RESTORED;
            } else if ( Menu_CheckPassword() ) {
                Menu_ShowMessage( "CHEATS ACTIVATED", 0, PASSWORD_MESSAGE_Y );
            } else {
                Menu_PlayMenuSound( MENU_SOUND_SELECT );
                Menu_ShowMessage( "INVALID PASSWORD", 0, PASSWORD_MESSAGE_Y );
            }
            break;
        case 'z':
            Menu_OpenOptionsMenu( entry, record );
            break;
        }
    }
}

/*
==================
Menu_ReadCardDirectory
==================
*/
/* name: inferred from its body, which lists this game's saves on the selected card */
short Menu_ReadCardDirectory( const char *title, menuCallback_t select )
{
    struct DIRENTRY *record = g_cardFiles;
    cardHeader_t    *header = g_cardHeaders;
    short            device = Menu_SelectedCardPort();
    short            count = Card_CountFiles( g_cardFiles, "*", device );
    short            i;
    short            j;
    short            saves;

    if ( count == 0 )
        return 0;
    for ( i = 0; i < CARD_DIRECTORY_SIZE; i++ )
        g_cardBlockNames[i] = 0;
    saves = 0;
    for ( i = 0; i < count; record++, header++, i++ ) {
        if ( Card_ReadHeader( record->name, header, device ) == 0 ) {
            g_cardBlockNames[saves] = (const char *)header->title.text;
            saves++;
            for ( j = 0; j < CARD_TITLE_SHOWN; j++ ) {
                if ( (unsigned char)( header->title.text[j] - ' ' ) > 'z' - ' ' )
                    header->title.text[j] = '?';
            }
            header->title.text[CARD_TITLE_SHOWN] = 0;
        }
    }
    g_cardDeleteMenu[DELETE_MENU_FILE].limit = saves;
    g_cardDeleteMenu[DELETE_MENU_FILE].value = 0;
    g_cardFileIndex = 0;
    return count;
}

/*
==================
Menu_Task
==================
*/
/* name: inferred from its body, which runs the front-end screens by g_menuScreen */
void Menu_Task( TASK *task )
{
    RECT  first;
    RECT  second;
    short sectors;

    Music_Poll();
    if ( g_menuMusicStarted == 0 ) {
        Menu_LoadSpriteTextures( FILE_CONTROL_BND, MENU_PICTURE_CONTROLLERS );
        Music_PlayTrack( MUSIC_TRACK_MENU );
        g_menuMusicStarted = -1;
    }
    if ( PadRead( 0 ) != 0 )
        g_menuIdleFrames = 0;
    if ( g_menuScreenPrevious != g_menuScreen ) {
        g_menuIdleFrames = 0;
        Menu_CloseMessage();
        g_menuScreenPrevious = g_menuScreen;
    } else {
        if ( g_menuScreen == MENU_MAIN )
            g_menuIdleFrames++;
        else
            g_menuIdleFrames = 0;
        if ( g_menuIdleFrames > DEMO_IDLE_FRAMES ) {
            g_demoIndex++;
            g_menuIdleFrames = 0;
            if ( g_demoIndex >= DEMO_LEVEL_COUNT )
                g_demoIndex = 0;
            g_startLevel = g_demoLevels[g_demoIndex];
            g_level = g_startLevel;
            if ( g_startLevel == -1 )
                g_titleIntroState = TITLE_INTRO_REPLAY;
            g_menuScreen = MENU_START_GAME;
            g_menuScreenPrevious = MENU_START_GAME;
            g_gameFlags = ( g_gameFlags & ~GAME_FLAG_HARD ) | GAME_FLAG_DEMO;
        }
    }
    switch ( g_menuScreen ) {
    case MENU_NONE:
        break;
    case MENU_CREDITS_END:
        g_menuScreen = MENU_OPTIONS;
        Music_PlayTrack( MUSIC_TRACK_MENU );
        break;
    case MENU_CREDITS:
        if ( ( g_padPressed & PADstart ) || ( g_padPressed & PADRdown ) )
            g_menuScreen = MENU_CREDITS_END;
        break;
    case MENU_RESULTS:
        if ( Menu_UpdateMissionAssessment() ) {
            if ( g_levelResult->grade )
                g_menuScreen = g_menuNextState = MENU_LEVEL_PASSWORD;
            else
                g_menuScreen = MENU_CONTINUE;
        }
        break;
    case MENU_LEVEL_PASSWORD:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_SLOW );
        Menu_RunMenu( g_passwordMenu, ARRAY_COUNT( g_passwordMenu ), (TASK *)task );
        break;
    case MENU_SAVE_CARD:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_INTERVAL );
        Menu_RunMenu( g_saveCardMenu, ARRAY_COUNT( g_saveCardMenu ), (TASK *)task );
        break;
    case MENU_LOAD_CARD:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_INTERVAL );
        g_menuCursors[MENU_CURSOR_CARD] = task->state[0];
        Menu_RunMenu( g_loadCardMenu, ARRAY_COUNT( g_loadCardMenu ), (TASK *)task );
        break;
    case MENU_LOAD_SCAN:
        if ( Menu_ScanCardSlots() ) {
            g_menuScreen = MENU_LOAD_SLOTS;
        } else {
            Menu_ShowMessage( "No save game data on Memory Card", 0, CARD_MESSAGE_Y );
            g_menuScreen = g_menuReturnState;
            g_menuScreenPrevious = g_menuReturnState;
        }
        break;
    case MENU_SAVE_CHECK:
        if ( g_cardFiles != 0 ) {
            Card_FindFiles( g_cardFiles, "*", Menu_SelectedCardPort(), &sectors );
            if ( sectors >= CARD_BLOCKS_FULL ) {
                g_menuScreen = g_menuReturnState;
                g_menuScreenPrevious = g_menuReturnState;
                Menu_ShowMessage( "Card full. Insert new card.", 0, CARD_MESSAGE_Y );
            } else {
                Menu_ScanCardSlots();
                g_menuScreen = MENU_SAVE_SLOTS;
            }
        }
        break;
    case MENU_OVERWRITE_CONFIRM:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_INTERVAL );
        if ( g_cardStatus != g_cardStatusPrevious )
            g_menuScreen = g_menuReturnState;
        else
            Menu_RunMenu( g_cardOverwriteMenu, ARRAY_COUNT( g_cardOverwriteMenu ), (TASK *)task );
        break;
    case MENU_LOAD_SLOTS:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_INTERVAL );
        if ( g_cardStatus != g_cardStatusPrevious )
            g_menuScreen = g_menuReturnState;
        else
            Menu_RunMenu( g_cardLoadMenu, ARRAY_COUNT( g_cardLoadMenu ), (TASK *)task );
        break;
    case MENU_SAVE_SLOTS:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_INTERVAL );
        if ( g_cardStatus != g_cardStatusPrevious )
            g_menuScreen = g_menuReturnState;
        else
            Menu_RunMenu( g_cardSaveMenu, ARRAY_COUNT( g_cardSaveMenu ), (TASK *)task );
        break;
    case MENU_DELETE_SCAN:
        g_cardFileIndex = -1;
        g_cardFileCount = Menu_ReadCardDirectory( g_labelDeleteFile, (menuCallback_t)Menu_SelectCardFile );
        if ( g_cardFileCount != 0 ) {
            g_menuScreen = MENU_DELETE_SELECT;
        } else {
            g_menuScreen = g_menuReturnState;
            g_menuScreenPrevious = g_menuReturnState;
            Menu_ShowMessage( "No blocks on Memory Card", 0, CARD_MESSAGE_Y );
        }
        break;
    case MENU_DELETE_CONFIRM:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_INTERVAL );
        if ( g_cardStatus != g_cardStatusPrevious )
            g_menuScreen = g_menuReturnState;
        else
            Menu_RunMenu( g_cardDeleteFileMenu, ARRAY_COUNT( g_cardDeleteFileMenu ), (TASK *)task );
        break;
    case MENU_FORMAT_CONFIRM:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_INTERVAL );
        if ( g_cardStatus != g_cardStatusPrevious )
            g_menuScreen = g_menuReturnState;
        else
            Menu_RunMenu( g_cardFormatMenu, ARRAY_COUNT( g_cardFormatMenu ), (TASK *)task );
        break;
    case MENU_DELETE_SELECT:
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_INTERVAL );
        if ( g_cardStatus != g_cardStatusPrevious ) {
            g_cardFileIndex = -1;
            g_menuScreen = g_menuReturnState;
        } else {
            Menu_RunMenu( g_cardDeleteMenu, ARRAY_COUNT( g_cardDeleteMenu ), (TASK *)task );
        }
        break;
    case MENU_PASSWORD_ENTRY:
        Menu_ClearPassword();
        g_menuScreen = MENU_PASSWORD_KEYBOARD;
        break;
    case MENU_PASSWORD_KEYBOARD:
        Menu_PasswordKeyboardInput( (TASK *)task );
        Menu_DrawPasswordKeyboard();
        break;
    case MENU_CONTROL_METHOD:
        Menu_RunMenu( g_controlMethodMenu, ARRAY_COUNT( g_controlMethodMenu ), (TASK *)task );
        break;
    case MENU_CONTINUE:
        g_menuScreen = MENU_CONTINUE_FADE;
        break;
    case MENU_START_GAME:
        g_menuScreen = MENU_FADE_OUT;
        Sys_CDSeekFile( FILE_COLONY_BS );
        break;
    case MENU_ENTER_GAME:
        if ( ( g_gameFlags & GAME_FLAG_DEMO ) || CHEAT_IS_ACTIVE( CHEAT_FLAG_GIMME_WEAPONS ) )
            Password_SetFullRecord( g_startLevel );
    case MENU_LEAVE:
        first.x = 0;
        first.y = 0;
        first.w = SCREEN_WIDTH;
        first.h = SCREEN_HEIGHT;
        second.x = 0;
        second.y = SCREEN_HEIGHT;
        second.w = SCREEN_WIDTH;
        second.h = SCREEN_HEIGHT;
        ClearImage( &first, 0, 0, 0 );
        ClearImage( &second, 0, 0, 0 );
        DrawSync( 0 );
        Sys_SetupDoubleBuffer( &first, &second, 1, 0 );
        VSync( 0 );
        g_gameFlags |= GAME_FLAG_MENU_DONE;
        break;
    case MENU_MAIN:
        if ( CHEAT_IS_ACTIVE( CHEAT_FLAG_CHEATS_MENU ) ) {
            g_mainMenu[MAIN_START].y = MAIN_MENU_Y_CHEATS;
            g_mainMenu[MAIN_CHEATS].flags = MENU_ENTRY_ACTIVE;
        } else {
            g_mainMenu[MAIN_START].y = MAIN_MENU_Y;
            g_mainMenu[MAIN_CHEATS].flags = 0;
        }
        Menu_RunMenu( g_mainMenu, ARRAY_COUNT( g_mainMenu ), (TASK *)task );
        break;
    case MENU_OPTIONS:
        g_menuCursors[MENU_CURSOR_OPTIONS] = task->state[0];
        Menu_RunMenu( g_optionsMenu, ARRAY_COUNT( g_optionsMenu ), (TASK *)task );
        Menu_SetSoundMode( g_optionsMenu[OPTIONS_SOUND].value );
        task->state[MENU_STATE_CARD_POLL] = Menu_PollCardStatus( task->state[MENU_STATE_CARD_POLL], CARD_POLL_SLOW );
        break;
    case MENU_CHEATS:
        Menu_RunMenu( g_cheatsMenu, ARRAY_COUNT( g_cheatsMenu ), (TASK *)task );
        g_cheatFlags = CHEAT_FLAG_CHEATS_MENU;
        g_startLevel = g_cheatsMenu[CHEATS_LEVEL].value;
        if ( g_cheatsMenu[CHEATS_INDESTRUCTIBLE].value )
            g_cheatFlags |= CHEAT_FLAG_INDESTRUCTIBLE;
        if ( g_cheatsMenu[CHEATS_UNLIMITED_AMMO].value )
            g_cheatFlags |= CHEAT_FLAG_UNLIMITED_AMMO;
        if ( g_cheatsMenu[CHEATS_GIMME_WEAPONS].value )
            g_cheatFlags |= CHEAT_FLAG_GIMME_WEAPONS;
        g_cheatFlags |= CHEAT_FLAGS_DEFAULT;
        break;
    }
    g_nextTask = task->Next;
}

/*
==================
Menu_LoadMissionText
==================
*/
/* name: inferred from its body, which extracts the level's text from MISSIONU.TXT */
void Menu_LoadMissionText( unsigned short index )
{
    unsigned short slot = Heap_Mark();
    short          remaining;
    short          searching;
    char          *cursor;
    char          *start;
    int            size;
    heapFile_t    *file;

    file = &g_fileTable[FILE_MISSIONU_TXT];
    file->loadedSize = 0;
    file->buffer = 0;
    Sys_LoadFile( file );
    remaining = index * 2 + 1;
    g_missionText[0] = 0;
    cursor = file->buffer;
    searching = -1;
    size = file->loadedSize;
    start = cursor;
    do {
        if ( *cursor == '*' ) {
            if ( --remaining < 0 ) {
                *cursor = 0;
                strncpy( g_missionText, start, MISSION_TEXT_SIZE );
                g_missionText[MISSION_TEXT_LAST] = 0;
                searching = 0;
            } else {
                start = cursor + 1;
            }
        }
        if ( --size <= 0 )
            searching = 0;
        cursor++;
    } while ( searching );
    Heap_Release( slot );
}

/* Multi-statement macro (name invented): reads the digit that follows a control character.
 * Only the '$' case uses it: its do { } while (0) gives retail's addressing of x and position
 * through the text pointer's address register, and the other cases match open-coded. */
#define NEXT_DIGIT( c )               \
    do {                              \
        ( c ) = *g_messageBox.text++; \
        ( c ) -= '0';                 \
    } while ( 0 )

/*
==================
Menu_MessageBoxType
==================
*/
/* name: inferred from its body, which types the message box text one character every other frame.
 * Control
 * characters: 0 ends, 13 starts a new line, '!' draws button glyph N,
 * '#' counts a line (pausing when the page is full), '$' indents N
 * cells, '%' pauses N*10 frames, '&' selects colour N. */
/* MATCH-PENDING: R17, NEXT_DIGIT do { } while (0) macro used at '$' only while '!', '%' and '&' open-code the same read.
 * Retail's '$' addresses position and x off the &g_messageBox.text register (lhu/sh -0x1c, -0x12), which only a loop
 * construct around the read gives (loop-hoisted address pseudo); NEXT_DIGIT at all four sites is 32 bytes short, a
 * one-expression read or inline next_digit(&text) at '$' 16 bytes long. No helper or macro found in the Probe lineage source */
void Menu_MessageBoxType( void )
{
    unsigned char c;

    switch ( g_messageBox.scroll ) {
    default:
        g_messageBox.scroll = MESSAGE_SCROLL_IDLE;
        g_messageBox.length = 0;
        break;
    case MESSAGE_SCROLL_TYPING:
        if ( g_messageBox.length <= 0 ) {
            g_messageBox.scroll++;
            return;
        }
        if ( ++g_messageBox.position & 1 )
            return;
        g_messageBox.length--;
        c = *g_messageBox.text;
        g_messageBox.text++;
        g_messageBoxSaveRect.x = g_messageBox.x;
        g_messageBoxSaveRect.y = g_messageBox.y + SCREEN_HEIGHT;
        MoveImage( &g_messageBoxSaveRect, g_messageBox.x, g_messageBox.y );
        switch ( c ) {
        case '#':
            if ( ++g_messageBox.line > g_messageBox.counter )
                g_messageBox.scroll = MESSAGE_SCROLL_PAGE_FULL;
            break;
        case '\r':
            g_messageBox.x = MESSAGE_BOX_LEFT;
            g_messageBox.position = 0;
            g_messageBoxSaveRect.x = 0;
            g_messageBoxSaveRect.y = 0;
            g_messageBox.y += MESSAGE_BOX_LINE_HEIGHT;
            break;
        case '!':
            c = *g_messageBox.text++;
            c -= '0';
            g_messageBox.x = Hud_DrawGlyphNow( c + CHAR_BUTTON_ICONS, g_messageBox.x, g_messageBox.y,
                                               g_messageBoxColours[MESSAGE_PAGE_BUTTON],
                                               g_messageBoxShadowColours[g_messageBox.page] );
            break;
        case '%':
            c = *g_messageBox.text++;
            c -= '0';
            g_messageBox.scroll = MESSAGE_SCROLL_PAUSED;
            g_messageBox.timer = c * MESSAGE_PAUSE_TICKS;
            break;
        case '$':
            NEXT_DIGIT( c );
            g_messageBox.position = 0;
            g_messageBoxSaveRect.x = 0;
            g_messageBoxSaveRect.y = 0;
            g_messageBox.x += c * MESSAGE_CELL_WIDTH;
            break;
        case '&':
            c = *g_messageBox.text++;
            c -= '0';
            g_messageBox.page = c;
            break;
        case 0:
            g_messageBox.length = 0;
            break;
        case ' ':
            g_messageBox.x += MESSAGE_CELL_WIDTH;
            break;
        default:
            g_messageBox.x = Hud_DrawGlyphNow( c, g_messageBox.x, g_messageBox.y, g_messageBoxColours[g_messageBox.page],
                                               g_messageBoxShadowColours[g_messageBox.page] );
            break;
        case '\n':
            break;
        }
        if ( g_messageBox.position & MESSAGE_CURSOR_BLINK )
            Hud_DrawGlyphNow( '*', g_messageBox.x, g_messageBox.y, g_messageBoxColours[0], g_messageBoxShadowColours[0] );
        break;
    case MESSAGE_SCROLL_PAUSED:
        if ( --g_messageBox.timer <= 0 )
            g_messageBox.scroll = MESSAGE_SCROLL_TYPING;
    case MESSAGE_SCROLL_PAGE_FULL:
        if ( ++g_messageBox.position & MESSAGE_CURSOR_BLINK ) {
            Hud_DrawGlyphNow( '*', g_messageBox.x, g_messageBox.y, g_messageBoxColours[0], g_messageBoxShadowColours[0] );
        } else {
            g_messageBoxSaveRect.x = g_messageBox.x;
            g_messageBoxSaveRect.y = g_messageBox.y + SCREEN_HEIGHT;
            MoveImage( &g_messageBoxSaveRect, g_messageBox.x, g_messageBox.y );
        }
        if ( g_messageBox.scroll != MESSAGE_SCROLL_PAUSED && g_messageBox.line <= g_messageBox.counter )
            g_messageBox.scroll = MESSAGE_SCROLL_TYPING;
        break;
    }
}

/*
==================
Menu_AdvanceMessageBox
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Menu_AdvanceMessageBox( void )
{
    ++g_messageBox.counter;
}

/*
==================
Menu_OpenMessageBox

Opens the message box for a mode: 2 shows the loading screen with the
level's picture, 7 the continue prompt, 8 the card text and 9 the
level's mission text.
==================
*/
/* name: inferred from its body, which sets up the message box for a mode */
void Menu_OpenMessageBox( short level, short mode )
{
    DISPENV              disp;
    DRAWENV              draw;
    RECT                 rect;
    unsigned char        color[MENU_COLOR_BYTES];
    short                x = LOADING_TEXT_X;
    short                index;
    const unsigned char *p;

    color[0] = LOADING_TEXT_RED;
    color[1] = LOADING_TEXT_GREEN;
    color[2] = LOADING_TEXT_BLUE;
    switch ( mode ) {
    case 1: /* identity-TODO: mode 1 sets nothing up, nor do 3..6 (no case: retail's jump table
             * sends them to the end of the switch); no caller passes any of them */
        break;
    case MESSAGE_BOX_LOADING:
        g_messageBox.x = MESSAGE_BOX_LEFT;
        g_messageBox.y = MESSAGE_BOX_BRIEF_Y;
        g_messageBox.text = g_missionBriefHeader;
        setRECT( &rect, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT );
        index = FILE_PRISHOLD_BS;
        if ( level < LEVEL_211_LIVING_QUARTERS )
            index = FILE_COLONY_BS;
        if ( level >= LEVEL_311_TUNNELS )
            index = FILE_BONESHIP_BS;
        Menu_LoadMDECPicture( index, &rect );
        SetDefDrawEnv( &draw, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT );
        SetDefDispEnv( &disp, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT );
        VSync( 0 );
        MoveImage( &rect, 0, 0 );
        PutDispEnv( &disp );
        PutDrawEnv( &draw );
        SetDispMask( 1 );
        DrawSync( 0 );
        for ( p = s_loadingData; *p != 0; p++ )
            x = Hud_DrawGlyphNow( *p, x, PROMPT_Y, color, g_messageBoxShadowColours[0] );
        break;
    case MESSAGE_BOX_CONTINUE:
        setRECT( &rect, 0, CONTINUE_STRIP_Y, SCREEN_WIDTH, CONTINUE_STRIP_HEIGHT );
        MoveImage( &rect, 0, PROMPT_Y );
        for ( p = s_pressToContinue; *p != 0; p++ ) {
            if ( *p == '!' ) {
                p++;
                x = Hud_DrawGlyphNow( *p - '0' + CHAR_BUTTON_ICONS, x, PROMPT_Y,
                                      g_messageBoxColours[MESSAGE_PAGE_BUTTON], g_messageBoxShadowColours[0] );
            } else {
                x = Hud_DrawGlyphNow( *p, x, PROMPT_Y, color, g_messageBoxShadowColours[0] );
            }
        }
        return;
    case MESSAGE_BOX_PASSWORD:
        g_messageBox.text = g_passwordText;
        break;
    case MESSAGE_BOX_MISSION:
        Menu_LoadMissionText( level );
        if ( g_missionText[0] == 0 )
            g_messageBox.text = s_noMessage;
        else
            g_messageBox.text = g_missionText;
        g_messageBox.x = MESSAGE_BOX_LEFT;
        g_messageBox.y = MESSAGE_BOX_MISSION_Y;
        break;
    }
    g_messageBoxSaveRect.x = 0;
    g_messageBoxSaveRect.y = 0;
    g_messageBoxSaveRect.w = MESSAGE_BOX_CELL;
    g_messageBoxSaveRect.h = MESSAGE_BOX_CELL;
    g_messageBox.length = strlen( g_messageBox.text );
    g_messageBox.timer = 0;
    g_messageBox.position = 0;
    g_messageBox.scroll = MESSAGE_SCROLL_TYPING;
    g_messageBox.line = 0;
    g_messageBox.counter = 0;
    g_messageBox.column = 0;
    g_messageBox.row = 0;
    g_messageBox.choice = -1;
    g_messageBox.page = 0;
    Sys_SetDisplayMode( DISPLAY_MODE_MESSAGE_BOX );
}

/*
==================
Menu_WaitMessageBox
==================
*/
/* name: inferred from its body, which waits until the message box text is printed */
void Menu_WaitMessageBox( void )
{
    while ( g_messageBox.length != 0 )
        VSync( 0 );
}

/*
==================
Menu_ShowMessageBox
==================
*/
/* name: inferred from its body, which waits for VSync and moves the message box picture on screen */
void Menu_ShowMessageBox( short wait )
{
    short x, y;
    Menu_WaitMessageBox();
    if ( !( g_gameFlags & GAME_FLAG_DEMO ) && wait ) {
        while ( !( PadRead( 0 ) & PADRdown ) )
            ;
    }
    x = g_messageBox.x;
    y = g_messageBox.y;
    g_messageBoxSaveRect.x = x;
    g_messageBoxSaveRect.y = y + SCREEN_HEIGHT;
    MoveImage( &g_messageBoxSaveRect, x, y );
    Sys_SetDisplayMode( DISPLAY_MODE_NORMAL );
}

/*
==================
Menu_LoadMDECPicture

Decodes an MDEC image file and uploads it to area in 16x16 tiles.
==================
*/
/* name: inferred from its body, which decodes an MDEC file and uploads it in 16x16 tiles */
void Menu_LoadMDECPicture( short index, RECT *area )
{
    short          slot = Heap_Mark();
    unsigned long *tile = (unsigned long *)Heap_Alloc( MDEC_TILE_SIZE * MDEC_TILE_SIZE * sizeof( u_short ) );
    heapFile_t    *file;
    unsigned long *bitstream;
    unsigned long *runlevel;
    RECT           rect;
    short          x;
    short          y;
    int            size = MDEC_TILE_SIZE * MDEC_TILE_SIZE / 2;

    if ( tile != 0 ) {
        file = &g_fileTable[index];
        file->loadedSize = 0;
        file->buffer = 0;
        Sys_LoadFile( file );
        bitstream = file->buffer;
        DecDCTReset( 0 );
        runlevel = (unsigned long *)Heap_Alloc( DecDCTBufSize( bitstream ) * sizeof( unsigned long ) );
        if ( runlevel != 0 ) {
            DecDCTvlc( bitstream, runlevel );
            DecDCTin( runlevel, 0 );
            VSync( 0 );
            SetDispMask( 0 );
            rect.w = MDEC_TILE_SIZE;
            rect.h = MDEC_TILE_SIZE;
            for ( x = 0; x < area->w; x += MDEC_TILE_SIZE ) {
                for ( y = 0; y < area->h; y += MDEC_TILE_SIZE ) {
                    DecDCTout( tile, size );
                    rect.x = x + area->x;
                    rect.y = y + area->y;
                    DecDCToutSync( 0 );
                    LoadImage( &rect, tile );
                    DrawSync( 0 );
                }
            }
        }
    }
    Heap_Release( slot );
}

/*
==================
Menu_LoadSpriteTextures
==================
*/
/* name: inferred from its body, which sets up the texture pages and CLUTs and loads a sprite file */
void Menu_LoadSpriteTextures( int id, short index )
{
    texturePage_t *textures[MENU_PICTURE_COUNT];
    clutRow_t     *palettes[MENU_PICTURE_COUNT];
    short          i = 0;
    do {
        textures[i] = &g_textureRecords[i];
        palettes[i] = &g_paletteRecords[i];
        textures[i]->tpage = GetTPage( TPAGE_MODE_8BIT, TPAGE_ABR_ADD_QUARTER, textures[i]->x, textures[i]->y );
        palettes[i]->clut = GetClut( palettes[i]->x, palettes[i]->y );
        i++;
    } while ( (unsigned short)i < MENU_PICTURE_COUNT );
    Sprite_LoadFile( id, &textures[index], &palettes[index], &g_spriteUvs[index] );
}

/*
==================
Menu_DrawTranslucentPanels

Draws count 256-pixel-wide semi-transparent quads from sprite records index
onwards, shaded by shade; with two panels the second is 64 wide. No caller
in retail.
==================
*/
/* name: inferred from its body, the POLY_FT4 counterpart of Menu_DrawPicturePanels */
void Menu_DrawTranslucentPanels( short index, u_char shade, short count )
{
    short     panel;
    POLY_FT4 *poly;
    u_long   *uv;

    for ( panel = 0; panel < count; panel++ ) {
        poly = g_spriteWorkspace;
        uv = (u_long *)&g_spriteUvs[index + panel];
        if ( count == 2 && panel == 1 ) {
            int x0 = PANEL_WIDTH, x1 = SCREEN_WIDTH, y0 = 0, y1 = SCREEN_HEIGHT;

            setXY4( poly, x0, y0, x1, y0, x0, y1, x1, y1 );
        } else {
            int x0 = panel * PANEL_WIDTH, y0 = 0, y1 = SCREEN_HEIGHT;

            setXY4( poly, x0, y0, x0 + PANEL_STEP, y0, x0, y1, x0 + PANEL_STEP, y1 );
        }
        *(u_long *)&poly->u0 = *uv++;
        *(u_long *)&poly->u1 = *uv++;
        *(u_long *)&poly->u2 = *uv++;
        *(u_long *)&poly->u3 = *uv;
        setRGB0( poly, shade, shade, shade );
        setPolyFT4( poly );
        SetSemiTrans( poly, 1 );
        addPrim( &g_renderBuffer[OT_BACK_SLOT], poly );
        g_spriteWorkspace = poly + 1;
    }
}

/*
==================
Menu_DrawPicturePanels

Draws count 256-pixel-wide sprite panels from sprite records index
onwards, shaded by shade; with two panels the second is 64 wide.
==================
*/
/* name: inferred from its body, which draws count 256-wide sprite panels */
void Menu_DrawPicturePanels( short index, u_char shade, short count )
{
    short    panel;
    DR_MODE *mode;
    SPRT    *sprite;

    for ( panel = 0; panel < count; panel++ ) {
        mode = g_spriteWorkspace;
        SetDrawMode( mode, 0, 1, g_spriteUvs[index + panel].tpage, 0 );
        sprite = g_spriteWorkspace = mode + 1;
        if ( count == 2 && panel == 1 ) {
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
        sprite->clut = g_spriteUvs[index + panel].clut;
        setRGB0( sprite, shade, shade, shade );
        SetSprt( sprite );
        addPrim( &g_renderBuffer[OT_BACK_SLOT], sprite );
        addPrim( &g_renderBuffer[OT_BACK_SLOT], mode );
        g_spriteWorkspace = sprite + 1;
    }
}

/*
==================
Menu_DrawPicture

Draws the two-panel 320x240 picture in sprite records index and
index + 1, shaded by shade.
==================
*/
/* name: inferred from its body, which draws a two-panel 320x240 picture */
void Menu_DrawPicture( short index, u_char shade )
{
    short    panel;
    DR_MODE *mode;
    SPRT    *sprite;

    for ( panel = 0; panel < 2; panel++ ) {
        mode = g_spriteWorkspace;
        SetDrawMode( mode, 0, 1, g_spriteUvs[index + panel].tpage, 0 );
        sprite = g_spriteWorkspace = mode + 1;
        switch ( panel ) {
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
        sprite->clut = g_spriteUvs[index + panel].clut;
        setRGB0( sprite, shade, shade, shade );
        SetSprt( sprite );
        addPrim( &g_renderBuffer[OT_BACK_SLOT], sprite );
        addPrim( &g_renderBuffer[OT_BACK_SLOT], mode );
        g_spriteWorkspace = sprite + 1;
    }
}

/*
==================
Menu_StepFade
==================
*/
/* name: inferred from its body, which steps a fade in, hold and fade out */
int Menu_StepFade( TASK *task, short duration, short minimum )
{
    int state;

    switch ( task->state[FADE_STATE_PHASE] ) {
    case FADE_IN:
        state = FADE_IN;
        task->state[FADE_STATE_SHADE] += FADE_IN_STEP;
        if ( task->state[FADE_STATE_SHADE] > FADE_SHADE_MAX ) {
            task->state[FADE_STATE_TICKS] = 0;
            task->state[FADE_STATE_PHASE]++;
        }
        break;
    case FADE_HOLD:
        state = FADE_HOLD;
        task->state[FADE_STATE_TICKS]++;
        if ( task->state[FADE_STATE_TICKS] > minimum ) {
            if ( ( g_padPressed & PADstart ) || ( g_padPressed & PADRdown ) )
                task->state[FADE_STATE_PHASE]++;
        }
        if ( task->state[FADE_STATE_TICKS] > duration )
            task->state[FADE_STATE_PHASE]++;
        break;
    case FADE_OUT:
        state = FADE_OUT;
        task->state[FADE_STATE_SHADE] -= FADE_OUT_STEP;
        if ( task->state[FADE_STATE_SHADE] <= 0 ) {
            task->state[FADE_STATE_SHADE] = 0;
            state = FADE_DONE;
            task->state[FADE_STATE_PHASE]++;
        }
        break;
    default:
        state = FADE_DONE;
        break;
    }
    return state;
}

/*
==================
Menu_TitlePictureTask
==================
*/
/* name: inferred from its body, which fades the title picture for the current state */
void Menu_TitlePictureTask( TASK *task )
{
    static short picture = MENU_PICTURE_TITLE;
    short        index = MENU_PICTURE_TITLE;
    short        shade;

    if ( g_menuScreen != MENU_FADE_OUT && task->state[0] < TITLE_SHADE_MAX )
        task->state[0] += PICTURE_FADE_STEP;
    shade = task->state[0];
    switch ( g_menuScreen ) {
    case MENU_FADE_OUT:
        index = picture;
        if ( task->state[0] >= PICTURE_FADE_MIN ) {
            task->state[0] -= PICTURE_FADE_STEP;
        } else {
            task->state[0] = 0;
            g_menuScreen = MENU_ENTER_GAME;
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
    Menu_DrawPicture( index, shade );
    picture = index;
    g_nextTask = task->Next;
}

/*
==================
Menu_FadeTask
==================
*/
/* name: inferred from its body, which fades the menu picture in and out */
void Menu_FadeTask( TASK *task )
{
    if ( g_menuScreen == MENU_CONTINUE_FADE || g_menuScreen == MENU_LEAVE ) {
        if ( task->state[0] >= PICTURE_FADE_MIN ) {
            task->state[0] -= PICTURE_FADE_STEP;
        } else {
            task->state[0] = 0;
            g_menuScreen = MENU_LEAVE;
        }
    } else if ( task->state[0] < MENU_FADE_SHADE_MAX ) {
        task->state[0] += PICTURE_FADE_STEP;
        if ( task->state[0] >= MENU_FADE_SHADE_READY )
            g_menuScreen = g_menuNextState;
    }
    Menu_DrawPicture( MENU_PICTURE_BACKGROUND, task->state[0] );
    g_nextTask = task->Next;
}

/*
==================
Menu_ClearScreens
==================
*/
/* name: inferred from its body, which clears the front and back areas */
void Menu_ClearScreens( RECT *front, RECT *back )
{
    short separate = 0;

    if ( front->x != back->x || front->y != back->y )
        separate = 1;
    VSync( 0 );
    ClearImage( front, 0, 0, 0 );
    if ( separate )
        ClearImage( back, 0, 0, 0 );
    DrawSync( 0 );
    Sys_SetupDoubleBuffer( front, back, 0, 1 );
    VSync( 0 );
}

/*
==================
Menu_HighlightPulseTask
==================
*/
/* name: inferred from its body, which pulses g_highlightColor */
void Menu_HighlightPulseTask( TASK *task )
{
    if ( task->state[0] != 0 ) {
        g_highlightColor[0] += 2;
        g_highlightColor[2] += 2;
        if ( g_highlightColor[0] > HIGHLIGHT_MAX )
            task->state[0] = 0;
    } else {
        g_highlightColor[0] -= 2;
        g_highlightColor[2] -= 2;
        if ( g_highlightColor[0] < HIGHLIGHT_MIN )
            task->state[0] = 1;
    }
    g_nextTask = task->Next;
}

/*
==================
Menu_ClearVRAMArea
==================
*/
/* name: inferred from its body, which clears 960x240 of VRAM */
void Menu_ClearVRAMArea( void )
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
    VSync( 0 );
    SetDispMask( 0 );
    ClearImage( &first, 0, 0, 0 );
    ClearImage( &second, 0, 0, 0 );
    DrawSync( 0 );
    VSync( 0 );
    SetDispMask( 1 );
}

/*
==================
Menu_TitleSequenceTask

Menu_TitleSequenceTask is the title sequence task: the intro movies, then the title picture, then
Menu_Task
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Menu_TitleSequenceTask( TASK *task )
{
    short status;
    /* RULE-EXCEPTION: R17-dead-local, retail's frame holds 9-16 unused bytes (sweep: 8 and 17 miss); 16 = the two RECTs (first, second) that Menu_BlankScreen/Menu_ClearDisplayArea/Menu_ClearVRAMArea declare; ruled out: all three are jal calls in retail, no inline helper or macro here, no aggregate temporary (switch of calls and state stores only), no counterpart in the Probe lineage source */
    RECT unused[2];

    switch ( task->state[0] ) {
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
        Menu_LoadSpriteTextures( FILE_LOGOSGFX_BND, MENU_PICTURE_TITLE );
        Text_LoadFont();
        if ( g_titleIntroState == TITLE_INTRO_SEEN )
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0]++;
        break;
    case TITLE_PAUSE:
        task->state[0]++;
        break;
    case TITLE_BLANK:
        Menu_BlankScreen();
        task->state[0]++;
        break;
    case TITLE_LEGAL:
        status = Movie_Play( MOVIE_LEGAL );
        Menu_BlankScreen();
        if ( status == MOVIE_ABORTED )
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0]++;
        break;
    case TITLE_FOX:
        Menu_ClearVRAMArea();
        status = Movie_Play( MOVIE_FOX_TITLE );
        if ( status == MOVIE_ABORTED )
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0]++;
        break;
    case TITLE_ACCLAIM:
        Menu_ClearVRAMArea();
        status = Movie_Play( MOVIE_ACCLAIM );
        if ( status == MOVIE_ABORTED )
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0] = TITLE_PROBE;
        break;
    case TITLE_PROBE:
        Menu_ClearVRAMArea();
        status = Movie_Play( MOVIE_PROBE );
        if ( status == MOVIE_ABORTED )
            task->state[0] = TITLE_PICTURE_START;
        else
            task->state[0]++;
        break;
    case TITLE_INTRO:
        Menu_BlankScreen();
        Movie_Play( MOVIE_INTRO );
        task->state[0]++;
        break;
    case TITLE_PICTURE_START:
        Menu_ClearDisplayArea();
        task->state[1] = 0;
        task->state[2] = 0;
        task->state[0]++;
        break;
    case TITLE_PICTURE: {
        TASK *picture = TaskCreate( TASK_PRIORITY_MENU, Menu_TitlePictureTask );

        picture->state[0] = 0;
        picture->state[1] = 0;
        task->state[1] = TITLE_PICTURE_TICKS;
        task->state[0]++;
        break;
    }
    case TITLE_PICTURE_WAIT:
        if ( task->state[1] == 0 ) {
            task->state[0] = 0;
            task->callback = Menu_Task;
            g_titleIntroState = TITLE_INTRO_SEEN;
            g_menuMusicStarted = 0;
        } else {
            task->state[1]--;
        }
        break;
    }
    g_nextTask = task->Next;
}

/*
==================
Menu_StartMenuTasks
==================
*/
/* name: inferred from its body, which creates the render and pulse tasks and the card buffers */
void Menu_StartMenuTasks( void )
{
    TASK *task;
    /* forget the credits text (\GFX\CREDITS.TXT) so it is loaded again */
    g_fileTable[FILE_CREDITS_TXT].loadedSize = 0;
    g_fileTable[FILE_CREDITS_TXT].buffer = 0;
    task = TaskCreate( TASK_PRIORITY_OVERLAY, Sys_MenuDisplayTask );
    g_renderTask = task;
    task->state[0] = 1;
    task = TaskCreate( TASK_PRIORITY_MENU_FADE, Menu_HighlightPulseTask );
    task->state[0] = 0;
    g_highlightColor[1] = HIGHLIGHT_GREEN;
    g_highlightColor[0] = HIGHLIGHT_START;
    g_highlightColor[2] = HIGHLIGHT_START;
    g_cardStatus = CARD_STATUS_UNKNOWN;
    g_cardStatusPrevious = -1;
    Menu_PollCardStatus( 1, 0 );
    g_cardFileIndex = -1;
    g_menuIdleFrames = 0;
    g_8cedc = Heap_Alloc( MENU_8CEDC_SIZE );
    g_cardFiles = (struct DIRENTRY *)Heap_Alloc( CARD_DIRECTORY_SIZE * sizeof( struct DIRENTRY ) );
    g_cardHeaders = Heap_Alloc( CARD_DIRECTORY_SIZE * sizeof( cardHeader_t ) );
}

/*
==================
Menu_StartTitleSequence
==================
*/
/* name: inferred from its body, which creates the front-end sequence task */
void Menu_StartTitleSequence( void )
{
    TASK *task;
    Menu_StartMenuTasks();
    task = TaskCreate( TASK_PRIORITY_MENU, Menu_TitleSequenceTask );
    task->state[FADE_STATE_SHADE] = 0;
    task->state[FADE_STATE_PHASE] = 0;
    task->state[FADE_STATE_TICKS] = TITLE_SEQUENCE_TICKS;
    task->state[0] = 0;
    g_menuScreen = MENU_MAIN;
    g_menuNextState = MENU_MAIN;
    g_menuReturnState = MENU_LOAD_CARD;
    g_cardMenuSaving = 0;
    if ( !CHEAT_IS_ACTIVE( CHEAT_FLAG_CHEATS_MENU ) )
        g_startLevel = 0;
}

/*
==================
Menu_StartResultsMenu
==================
*/
/* name: inferred from its body, which creates the menu task for the results and save screens */
void Menu_StartResultsMenu( void )
{
    TASK *task;
    Menu_StartMenuTasks();
    task = TaskCreate( TASK_PRIORITY_MENU, Menu_Task );
    if ( task != 0 ) {
        task->state[0] = 0;
        task->state[1] = 0;
        task->state[2] = 0;
        task->state[MENU_STATE_CARD_POLL] = 0;
    }
    g_menuScreen = MENU_FADE_OUT;
    g_menuNextState = MENU_RESULTS;
    g_menuReturnState = MENU_SAVE_CARD;
    g_cardMenuSaving = -1;
    task = TaskCreate( TASK_PRIORITY_MENU_FADE, Menu_FadeTask );
    if ( task != 0 ) {
        task->state[0] = 0;
        task->state[1] = 0;
    }
    Menu_LoadSpriteTextures( FILE_LOGOSGFX_BND, MENU_PICTURE_RESULTS );
}

/*
==================
Menu_ShowLoadingScreen
==================
*/
/* name: inferred from its body, which shows the level picture and the mission text */
void Menu_ShowLoadingScreen( int mode )
{
    texturePage_t *texture;
    clutRow_t     *palette;
    clutRow_t     *pal;
    unsigned short page;
    texture = &g_textureRecords[MENU_PICTURE_LOADING];
    palette = &g_paletteRecords[MENU_PICTURE_LOADING];
    page = GetTPage( TPAGE_MODE_8BIT, TPAGE_ABR_HALF, g_textureRecords[MENU_PICTURE_LOADING].x,
                     g_textureRecords[MENU_PICTURE_LOADING].y );
    pal = palette;
    texture->tpage = page;
    palette->clut = GetClut( pal->x, pal->y );
    Menu_OpenMessageBox( mode, MESSAGE_BOX_LOADING );
    Sprite_LoadFile( FILE_ICONS_IFF, &texture, &palette, g_iconUvs );
    Menu_ShowMessageBox( 0 );
    Menu_OpenMessageBox( mode, MESSAGE_BOX_MISSION );
}

/*
==================
Menu_ShowContinuePrompt
==================
*/
/* name: inferred from its body, which shows the continue prompt in the message box */
void Menu_ShowContinuePrompt( int mode )
{
    Menu_ShowMessageBox( 0 );
    Menu_OpenMessageBox( mode, MESSAGE_BOX_CONTINUE );
    Menu_ShowMessageBox( -1 );
}

/*
==================
Menu_Percentage
==================
*/
/* name: inferred from its body, which returns value as a percentage of denominator, at most 100 */
short Menu_Percentage( short denominator, short value )
{
    short quotient;

    if ( value == 0 )
        return 0;
    quotient = value * PERCENT_MAX / denominator;
    if ( quotient >= PERCENT_LIMIT )
        quotient = PERCENT_MAX;
    return quotient;
}

/*
==================
Menu_ComputeLevelResults
==================
*/
/* name: inferred from its body, which fills the kills, secrets and targets percentages */
short Menu_ComputeLevelResults( levelResult_t *result, short level )
{
    short i;
    short found = 0;
    short secrets;
    short total;
    short tally;
    short targetTotal;
    short targetTally;
    unsigned char class;

    g_player.tallies[TALLY_IDENTITY_TAGS] = g_player.identityTags;
    result->shown[0] = 0;
    result->shown[1] = 0;
    result->shown[2] = 0;
    secrets = g_levelMap.secretCount;
    for ( i = 1; i < ARRAY_COUNT( g_secretsFound ); i++ ) {
        if ( g_secretsFound[i] )
            found++;
    }
    if ( secrets == 0 ) {
        secrets = 1;
        found = 1;
    }
    tally = 0;
    total = 0;
    for ( i = 1; i < ACTOR_KIND_INERT; i++ ) {
        tally += g_player.tallies[i];
        total += g_levelMap.kindCounts[i];
    }
    g_levelMap.kindCounts[TALLY_TOTAL] = total;
    g_player.tallies[TALLY_TOTAL] = tally;
    targetTotal = 0;
    targetTally = 0;
    for ( i = 0; i < TARGET_CLASSES_PER_LEVEL; i++ ) {
        class = g_levelTargetClasses[level][i];
        if ( class != TARGET_CLASS_NONE ) {
            targetTally += g_player.tallies[class];
            targetTotal += g_levelMap.kindCounts[class];
        }
    }
    if ( targetTotal == 0 ) {
        targetTotal = 1;
        targetTally = 1;
    }
    result->targets = Menu_Percentage( targetTotal, targetTally );
    result->kills = Menu_Percentage( total, tally );
    result->secrets = Menu_Percentage( secrets, found );
    return result->targets;
}

/*
==================
Menu_ModeInTable
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
short Menu_ModeInTable( short mode )
{
    unsigned short index;
    for ( index = 0; index < MODE_TABLE_SIZE; index++ ) {
        if ( g_modeTable[(short)index] == mode )
            return -1;
    }
    return 0;
}

/*
==================
Menu_IsPilotsChambersLevel
==================
*/
/* name: inferred from its body, which returns -1 for the five Pilot's Chambers levels; Menu_LevelComplete
 * then goes to the level password screen */
short Menu_IsPilotsChambersLevel( short value )
{
    short result;
    switch ( value ) {
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

/*
==================
Menu_BuildResultColors
==================
*/
/* name: inferred from its body, which fills the per-percent colour ramp of the results */
void Menu_BuildResultColors( levelResult_t *grid )
{
    unsigned char current[RGB_CHANNELS];
    short         i;
    CVECTOR      *color;
    grid->colors = Heap_Alloc( RESULT_COLOR_STEPS * sizeof( CVECTOR ) );
    if ( grid->colors == 0 )
        return;
    current[0] = RAMP_MAX;
    current[1] = 0;
    current[2] = 0;
    color = grid->colors;
    for ( i = 0; i < RESULT_COLOR_STEPS; i++ ) {
        if ( current[0] != 0 && i >= RAMP_RED_FADE_START ) {
            current[0] -= RAMP_STEP;
            if ( current[0] < RAMP_STEP_FLOOR )
                current[0] = 0;
        }
        if ( current[1] != RAMP_MAX && grid->par - RAMP_GREEN_LEAD < i ) {
            current[1] += RAMP_STEP;
            if ( current[1] >= RAMP_STEP_CEILING )
                current[1] = RAMP_MAX;
        }
        color->r = current[0];
        color->g = current[1];
        color->b = current[2];
        color++;
    }
}

/*
==================
Menu_LevelComplete

Level complete: grade the run, pick the next level, run the results
screen until it is dismissed, and return the next level.
==================
*/
/* name: inferred from its body, which grades the level and runs the results screen */
short Menu_LevelComplete( int level )
{
    levelResult_t result;
    short         next = level;
    int           grade = GRADE_FAILED;
    short         last = Menu_ModeInTable( level + 1 );
    short         par;
    short         percent;
    short         slot;
    short         savedMode;

    par = PAR_SECTION_1;
    if ( level >= LEVEL_311_TUNNELS )
        par = PAR_SECTION_3;
    if ( (unsigned int)( level - LEVEL_211_LIVING_QUARTERS ) < SECTION_2_LEVELS )
        par = PAR_SECTION_2;
    g_levelResult = &result;
    percent = Menu_ComputeLevelResults( &result, level );
    g_levelResult->par = par;
    if ( percent < par )
        grade = GRADE_FAILED;
    if ( percent > par && percent < GRADE_GOOD_PERCENT )
        grade = GRADE_PASSED;
    if ( percent >= GRADE_GOOD_PERCENT ) {
        if ( last ) {
            g_gameFlags |= GAME_FLAG_BEST_GRADE;
            grade = GRADE_BEST;
        } else {
            grade = GRADE_GOOD;
        }
    }
    if ( grade ) {
        next++;
        if ( last && grade != GRADE_BEST )
            next++;
    }
    if ( next != level )
        Password_StoreLevel( next );
    g_levelResult->grade = grade;
    slot = Heap_Mark();
    Menu_StartResultsMenu();
    Sys_ResetPad();
    g_gameFlags &= ~GAME_FLAG_MENU_DONE;
    if ( Menu_ModeInTable( level ) || Menu_IsPilotsChambersLevel( level ) )
        g_menuNextState = MENU_LEVEL_PASSWORD;
    Menu_BuildResultColors( g_levelResult );
    savedMode = g_level;
    g_level = next;
    while ( !( g_gameFlags & GAME_FLAG_MENU_DONE ) ) {
        Sys_PollPad();
        TaskDispatch();
    }
    TaskerInit();
    Heap_Release( slot );
    g_level = savedMode;
    return next;
}

/*
==================
Menu_PlayStoryMovie
==================
*/
/* name: inferred from its body, which plays a movie between cleared screens */
void Menu_PlayStoryMovie( short mode )
{
    RECT first;
    RECT second;
    g_renderTask->state[0] = 0;
    first.x = 0;
    first.y = 0;
    first.w = MOVIE_SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = 0;
    second.y = SCREEN_HEIGHT;
    second.w = MOVIE_SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;
    VSync( 0 );
    SetDispMask( 0 );
    ClearImage( &first, 0, 0, 0 );
    ClearImage( &second, 0, 0, 0 );
    DrawSync( 0 );
    Movie_Play( mode );
    VSync( 0 );
    SetDispMask( 0 );
    ClearImage( &first, 0, 0, 0 );
    ClearImage( &second, 0, 0, 0 );
    DrawSync( 0 );
    VSync( 0 );
    first.x = 0;
    first.y = 0;
    first.w = SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = 0;
    second.y = SCREEN_HEIGHT;
    second.w = SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;
    Sys_SetupDoubleBuffer( &first, &second, 1, 0 );
    VSync( 0 );
    SetDispMask( 1 );
    g_renderTask->state[0] = 1;
}

/*
==================
Menu_PlayDeathMovie
==================
*/
/* name: inferred from its body, which plays the death movie for g_deathType; main passes
 * the level, which it does not read */
void Menu_PlayDeathMovie( int level )
{
    RECT  first;
    RECT  second;
    short mode;

    switch ( g_deathType ) {
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
    g_renderTask->state[0] = 0;
    first.x = 0;
    first.y = 0;
    first.w = MOVIE_SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = MOVIE_SCREEN_WIDTH;
    second.y = 0;
    second.w = MOVIE_SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;
    VSync( 0 );
    SetDispMask( 0 );
    ClearImage( &first, 0, 0, 0 );
    ClearImage( &second, 0, 0, 0 );
    DrawSync( 0 );
    Movie_Play( mode );
    Menu_ClearVRAMArea();
    first.x = 0;
    first.y = 0;
    first.w = SCREEN_WIDTH;
    first.h = SCREEN_HEIGHT;
    second.x = SCREEN_WIDTH;
    second.y = 0;
    second.w = SCREEN_WIDTH;
    second.h = SCREEN_HEIGHT;
    Sys_SetupDoubleBuffer( &first, &second, 1, 0 );
    VSync( 0 );
    SetDispMask( 1 );
    g_renderTask->state[0] = 1;
}

/*
==================
Menu_SetDeathType
==================
*/
/* name: inferred from its body, which stores g_deathType */
void Menu_SetDeathType( short value )
{
    g_deathType = value;
}

/*
==================
Menu_CreditsLineTask
==================
*/
/* name: inferred from its body, which fades one credits line in and out */
/* MATCH-PENDING: R17, (shade << 16) >> 14 after the (unsigned short) fade value: retail zero-extends with sll 16 / srl 14
 * where every natural spelling (fade * 4, shade <<= 2, an unsigned short fade local) folds to andi 0xffff + sll 2 and
 * the u_short local's sll 2 cross-jumps into the * 12 path (200 bytes); permuter (randomizer families, 20 min) found only
 * this split or do { } while (0) wrappers */
void Menu_CreditsLineTask( TASK *node )
{
    unsigned int  y;
    unsigned int  shade;
    const char   *text;
    unsigned char rgb[RGB_CHANNELS];

    if ( node->state[0] < CREDITS_LINE_END || g_menuScreen != MENU_CREDITS ) {
        TaskDie( node );
        return;
    }
    y = node->state[0] - 1;
    node->state[0] = y;
    text = (const char *)node->state[2];
    shade = MENU_SHADE_FULL;
    if ( y >= CREDITS_FADE_IN_FROM ) {
        shade = (unsigned short)( CREDITS_FADE_IN_BASE - node->state[0] );
        shade = ( shade << HALFWORD_SHIFT ) >> ( HALFWORD_SHIFT - CREDITS_FADE_IN_SHIFT );
    } else if ( y < CREDITS_FADE_OUT_FROM )
        shade = (short)node->state[0] * CREDITS_FADE_OUT_SCALE;
    rgb[0] = shade;
    rgb[1] = shade;
    rgb[2] = shade;
    Text_DrawTextCentered( text, 0, node->state[0], rgb );
    g_nextTask = node->Next;
}

/*
==================
Menu_CreditsTask
==================
*/
/* name: inferred from its body, which starts one credits line task per text line until "END" */
void Menu_CreditsTask( TASK *node )
{
    char *text = (char *)node->state[1];
    if ( g_menuScreen != MENU_CREDITS || strncmp( text + 1, "END", CREDITS_END_LENGTH ) == 0 ) {
        TaskDie( node );
        g_menuScreen = MENU_CREDITS_END;
        return;
    }
    if ( node->state[0] == 0 ) {
        TASK *created = TaskCreate( TASK_PRIORITY_MENU, Menu_CreditsLineTask );
        if ( created != 0 ) {
            created->state[0] = CREDITS_LINE_START;
            created->state[1] = 0;
            created->state[2] = node->state[1];
            while ( *text != 0 )
                text++;
            node->state[1] = (int)( text + 1 );
            node->state[0] = CREDITS_LINE_INTERVAL;
        }
    } else {
        unsigned int remaining = node->state[0] - 1;
        node->state[0] = remaining;
    }
    g_nextTask = node->Next;
}

/*
==================
Menu_LoadTextFile

Loads a text file from the CD file table once, turning carriage returns into
terminators, and returns its buffer.
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
char *Menu_LoadTextFile( short index )
{
    heapFile_t  *file = &g_fileTable[index];
    unsigned int oldSize = file->loadedSize;
    if ( oldSize == 0 ) {
        /* Preserve the signed-short counter from the target; its valid size range is unknown. */
        short count;
        char *cursor;
        file->loadedSize = 0;
        file->buffer = 0;
        Sys_LoadFile( file );
        cursor = (char *)file->buffer;
        count = 0;
        if ( oldSize < file->loadedSize ) {
            do {
                if ( *cursor == '\r' )
                    *cursor = 0;
                ++count;
                ++cursor;
            } while ( count < file->loadedSize );
        }
    }
    return (char *)file->buffer;
}

/*
==================
Menu_StartCredits

Starts the credits: loads the credits text (FILE_CREDITS_TXT) and runs Menu_CreditsTask over it.
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Menu_StartCredits( void )
{
    void *buffer = Menu_LoadTextFile( FILE_CREDITS_TXT );
    TASK *node;
    if ( buffer != 0 ) {
        node = TaskCreate( TASK_PRIORITY_MENU, Menu_CreditsTask );
        if ( node != 0 ) {
            node->state[0] = CREDITS_FIRST_DELAY;
            node->state[1] = (int)buffer;
            Music_PlayTrack( MUSIC_TRACK_CREDITS );
        }
    }
}
