/*
 * Front-end menu entries and the message box (src/frontend.c owns both).
 * Names without evidence are marked invented.
 */
#ifndef MENU_H
#define MENU_H

/* menuEntry_t.flags. names invented, from Menu_RunMenu unless noted */
#define MENU_ENTRY_SELECTABLE 0x1 /* the cursor stops on it */
#define MENU_ENTRY_VISIBLE 0x2
#define MENU_ENTRY_DISABLED 0x4      /* drawn in the disabled colours */
#define MENU_ENTRY_HEADING 0x8       /* set on screen headings only; no reader found */
#define MENU_ENTRY_SILENT 0x10       /* changing or choosing it plays no menu sound */
#define MENU_ENTRY_SPACE_DOUBLE 0x20 /* the next line is 40 pixels down instead of 20 */
#define MENU_ENTRY_SPACE_TRIPLE 0x40 /* the next line is 60 pixels down */
#define MENU_ENTRY_SMALL 0x80        /* drawn with Text_DrawTextMultiline; the next line is 13 pixels down */
#define MENU_ENTRY_ACTIVE ( MENU_ENTRY_SELECTABLE | MENU_ENTRY_VISIBLE )
#define MENU_ENTRY_GREYED ( MENU_ENTRY_VISIBLE | MENU_ENTRY_DISABLED ) /* Menu_PollCardStatus without a card */

/* Menu_PlayMenuSound samples. names invented: Menu_RunMenu plays 0 on moves and changes, 1 on choosing */
enum {
    MENU_SOUND_MOVE = 0,
    MENU_SOUND_SELECT = 1
};

/* Menu_RunMenu's line spacing: MENU_LINE_SPACING, or as the entry's MENU_ENTRY_SPACE_* or
 * MENU_ENTRY_SMALL flag says. names invented */
#define MENU_LINE_SPACING 20
#define MENU_LINE_SPACING_DOUBLE 40
#define MENU_LINE_SPACING_TRIPLE 60
#define MENU_LINE_SPACING_SMALL 13

/* An entry callback: format (destination, entry, task), select or change (entry). */
typedef void ( *menuCallback_t )();

/* One line of a menu screen; the menu code walks an array of them. */
typedef struct menuEntry_s {
    unsigned short flags;
    /* Top row of the screen, read from the first entry by the renderer
     * (lhu 2(base) at 0x800182ac); name invented. */
    short              y;
    short              value;
    short              limit;
    const char        *label;
    const char *const *choices;
    menuCallback_t     callback0;
    menuCallback_t     callback1;
    menuCallback_t     callback2;
} menuEntry_t;

/* The message box state at 0x8006be7c; names invented. */
typedef struct messageBox_s {
    short x, y;
    short column, row;
    short length;
    short position;
    short timer;
    short scroll;
    short choice;
    short unknown_12; /* identity-TODO: no load or store of 0x8006be8e in retail (absolute and base-relative scan); no runtime access in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime), the briefing message boxes included */
    short line;
    short counter;
    short page;
    short unknown_1a; /* identity-TODO: no load or store of 0x8006be96 in retail (absolute and base-relative scan); no runtime access in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime), the briefing message boxes included */
    const char *text;
} messageBox_t;

/* messageBox_t.scroll: typing the text, waiting with a full page ('#' lines past the counter),
 * pausing ('%'); idle when the text is done. names invented */
enum {
    MESSAGE_SCROLL_IDLE = -1,
    MESSAGE_SCROLL_TYPING = 0,
    MESSAGE_SCROLL_PAGE_FULL = 1,
    MESSAGE_SCROLL_PAUSED = 2
};

/* Menu_OpenMessageBox modes (Menu_ShowLoadingScreen, Menu_ShowContinuePrompt). names invented, from
 * the text each sets up */
enum {
    MESSAGE_BOX_LOADING = 2,  /* the loading screen and the mission brief header */
    MESSAGE_BOX_CONTINUE = 7, /* the continue prompt */
    MESSAGE_BOX_PASSWORD = 8, /* the password text */
    MESSAGE_BOX_MISSION = 9   /* the level's mission text */
};

/* g_titleIntroState: whether Menu_TitleSequenceTask plays the intro movies. names invented:
 * Menu_Task sets TITLE_INTRO_REPLAY at the demo list's title slot, Menu_TitleSequenceTask skips
 * the movies at TITLE_INTRO_SEEN and sets it once the title picture is shown */
enum {
    TITLE_INTRO_FIRST = 0,
    TITLE_INTRO_REPLAY = 1,
    TITLE_INTRO_SEEN = 2
};

/* Front-end screens: Menu_Task runs the one in g_menuScreen. Names invented
 * from each state's handler in Menu_Task and the callbacks that enter it. */
enum menuScreen_t {
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

/* Two-byte string: an opening brace and its terminator. */
typedef struct {
    char brace, zero;
} token_t;

extern menuEntry_t g_optionsMenu[];
extern menuEntry_t g_controlMethodMenu[];

/* name: inferred from its body, which types the message box text (run from Sys_VBlankHandler) */
extern void Menu_MessageBoxType( void );

/* src/frontend.c */
extern short Menu_ModeInTable( short mode );

/* src/frontend.c */
extern short Menu_LevelComplete( int level );
extern void Menu_AdvanceMessageBox( void );
/* level: passed by main, unused */
extern void Menu_PlayDeathMovie( int level );
extern void Menu_PlayStoryMovie( short mode );
extern void Menu_ShowContinuePrompt( int mode );
extern void Menu_ShowLoadingScreen( int mode );
extern void Menu_StartTitleSequence( void );
extern short Menu_GetMusicVolume( void );
extern short Menu_GetSoundVolume( void );
extern void Menu_SetMusicVolume( int value );
extern void Menu_SetSoundVolume( int value );
extern short Menu_GetControlMethod( void );
extern void Menu_OptionsSetControlMethod( short value );

/* A pause menu line: its value and its label (src/pause_menu.c g_pauseOptions, g_soundOptions) */
typedef struct menuOption_s {
    short       value;
    const char *label;
} menuOption_t;

/* src/pause_menu.c */
extern void Pause_Run( void );

#endif
