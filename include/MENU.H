/*
 * Front-end menu entries and the message box (src/frontend.c owns both).
 * Names without evidence are marked invented.
 */
#ifndef MENU_H
#define MENU_H

/* MenuEntry.flags. names invented, from run_menu unless noted */
#define MENU_ENTRY_SELECTABLE 0x1 /* the cursor stops on it */
#define MENU_ENTRY_VISIBLE 0x2
#define MENU_ENTRY_DISABLED 0x4 /* drawn in the disabled colours */
#define MENU_ENTRY_HEADING 0x8 /* set on screen headings only; no reader found */
#define MENU_ENTRY_SILENT 0x10 /* changing or choosing it plays no menu sound */
#define MENU_ENTRY_SPACE_DOUBLE 0x20 /* the next line is 40 pixels down instead of 20 */
#define MENU_ENTRY_SPACE_TRIPLE 0x40 /* the next line is 60 pixels down */
#define MENU_ENTRY_SMALL 0x80 /* drawn with draw_text_multiline; the next line is 13 pixels down */
#define MENU_ENTRY_ACTIVE (MENU_ENTRY_SELECTABLE | MENU_ENTRY_VISIBLE)
#define MENU_ENTRY_GREYED (MENU_ENTRY_VISIBLE | MENU_ENTRY_DISABLED) /* poll_card_status without a card */

/* play_menu_sound samples. names invented: run_menu plays 0 on moves and changes, 1 on choosing */
enum {
    MENU_SOUND_MOVE = 0,
    MENU_SOUND_SELECT = 1
};

/* run_menu's line spacing: MENU_LINE_SPACING, or as the entry's MENU_ENTRY_SPACE_* or
 * MENU_ENTRY_SMALL flag says. names invented */
#define MENU_LINE_SPACING 20
#define MENU_LINE_SPACING_DOUBLE 40
#define MENU_LINE_SPACING_TRIPLE 60
#define MENU_LINE_SPACING_SMALL 13

/* An entry callback: format (destination, entry, task), select or change (entry). */
typedef void (*MenuCallback)();

/* One line of a menu screen; the menu code walks an array of them. */
typedef struct MenuEntry {
    unsigned short flags;
    /* Top row of the screen, read from the first entry by the renderer
     * (lhu 2(base) at 0x800182ac); name invented. */
    short y;
    short value;
    short limit;
    const char *label;
    const char *const *choices;
    MenuCallback callback0;
    MenuCallback callback1;
    MenuCallback callback2;
} MenuEntry;

/* The message box state at 0x8006be7c; names invented. */
typedef struct MessageBox {
    short x, y;
    short column, row;
    short length;
    short position;
    short timer;
    short scroll;
    short choice;
    short unknown_12; /* identity-TODO: no load or store of 0x8006be8e in retail (absolute and base-relative scan); no runtime access in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime, round 5), the briefing message boxes included */
    short line;
    short counter;
    short page;
    short unknown_1a; /* identity-TODO: no load or store of 0x8006be96 in retail (absolute and base-relative scan); no runtime access in title, options, new game, briefing, play with combat, pause, death and level end (analysis/runtime, round 5), the briefing message boxes included */
    const char *text;
} MessageBox;

/* MessageBox.scroll: typing the text, waiting with a full page ('#' lines past the counter),
 * pausing ('%'); idle when the text is done. names invented */
enum {
    MESSAGE_SCROLL_IDLE = -1,
    MESSAGE_SCROLL_TYPING = 0,
    MESSAGE_SCROLL_PAGE_FULL = 1,
    MESSAGE_SCROLL_PAUSED = 2
};

/* open_message_box modes (show_loading_screen, show_continue_prompt). names invented, from
 * the text each sets up */
enum {
    MESSAGE_BOX_LOADING = 2,  /* the loading screen and the mission brief header */
    MESSAGE_BOX_CONTINUE = 7, /* the continue prompt */
    MESSAGE_BOX_PASSWORD = 8, /* the password text */
    MESSAGE_BOX_MISSION = 9   /* the level's mission text */
};

/* g_title_intro_state: whether title_sequence_task plays the intro movies. names invented:
 * menu_task sets TITLE_INTRO_REPLAY at the demo list's title slot, title_sequence_task skips
 * the movies at TITLE_INTRO_SEEN and sets it once the title picture is shown */
enum {
    TITLE_INTRO_FIRST = 0,
    TITLE_INTRO_REPLAY = 1,
    TITLE_INTRO_SEEN = 2
};

extern MenuEntry g_options_menu[];
extern MenuEntry g_control_method_menu[];

/* name: inferred from its body, which types the message box text (run from vblank_handler) */
extern void message_box_type(void);

/* src/frontend.c */
extern short mode_in_table(short mode);

/* src/frontend.c */
extern short level_complete(int level);
extern void message_box_advance(void);
/* level: passed by main, unused */
extern void play_death_movie(int level);
extern void play_story_movie(short mode);
extern void show_continue_prompt(int mode);
extern void show_loading_screen(int mode);
extern void start_title_sequence(void);
extern short get_music_volume(void);
extern short get_sound_volume(void);
extern void set_music_volume(int value);
extern void set_sound_volume(int value);
extern short get_control_method(void);
extern void options_set_control_method(short value);

/* src/pause_menu.c */
extern void pause_menu_run(void);

#endif
