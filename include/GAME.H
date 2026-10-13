/*
 * Game-wide state shared by the front end, the level loop and the game code.
 * Names invented; each is marked with the use it is inferred from.
 */
#ifndef GAME_H
#define GAME_H

/* name: inferred from use: main loads, plays and advances levels by it; the password and saves record it */
extern int g_level;
/* name: inferred from use: 0x20 player dead, 0x40 death finished, 4 level end, 0x48 ends main's level loop, 0x20000 demo */
extern unsigned int g_game_flags;
/* g_game_flags bits. names: inferred from use (the setters and testers of each bit); the reset
 * and lamp bits from runtime evidence, analysis/runtime/globals.json (Start+Select held 0x79
 * frames, death, and pokes of the lamp bits at level start). */
#define GAME_FLAG_LEVEL_COMPLETE 0x4    /* level triggers and the countdown end the level; main runs level_complete */
#define GAME_FLAG_RESET         0x8     /* leave the level for the title (Start+Select, pause menu exit, demo end) */
#define GAME_FLAG_MENU_DONE     0x10    /* the front end (title menus, results screen) is left; frontend_run waits for it */
#define GAME_FLAG_PLAYER_DEAD   0x20    /* player health reached 0; player_tick runs the death */
#define GAME_FLAG_DEATH_DONE    0x40    /* the death sequence finished; main plays the death movie */
#define GAME_FLAG_FADE          0x80    /* g_fade_depth is fading the screen in or out */
#define GAME_FLAG_LAMP          0x100   /* shoulder lamp on */
#define GAME_FLAG_NIGHT_VISION  0x200   /* night vision on */
#define GAME_FLAG_FLASH         0x400   /* a shot (player_fire) or explosion lit the scene: the weapon sprite takes the unshaded palette */
#define GAME_FLAG_LAMP_FADE_IN  0x800   /* g_lamp_level rising */
#define GAME_FLAG_LAMP_FADE_OUT 0x1000  /* g_lamp_level falling */
#define GAME_FLAG_PAUSE         0x2000  /* Start pressed in play; level_play runs the pause menu */
#define GAME_FLAG_RESTORED      0x4000  /* the game was started from a password or a memory card save */
#define GAME_FLAG_HARD          0x8000  /* difficulty option 2, "Xenomania" (start_game): dead actors of class 4 come back */
#define GAME_FLAG_BEST_GRADE    0x10000 /* the last level of a section was finished with grade 3 */
#define GAME_FLAG_DEMO          0x20000 /* attract-mode demo: input comes from read_demo_input */

/* g_level values. names: the level names (g_level_names strings): section number and title */
enum LevelId {
    LEVEL_111_ENTRANCE,                /* 0 */
    LEVEL_112_OUTER_COMPLEX,           /* 1 */
    LEVEL_113_AMMUNITION_DUMP,         /* 2 */
    LEVEL_122_RECREATION_ROOMS,        /* 3 */
    LEVEL_131_MED_LAB,                 /* 4 */
    LEVEL_114_AMMUNITION_DUMP_2,       /* 5 */
    LEVEL_141_GARAGE,                  /* 6 */
    LEVEL_115_AMMUNITION_DUMP_3,       /* 7 */
    LEVEL_154_ATMOSPHERE_PROCESSOR,    /* 8 */
    LEVEL_155_SECURITY_CATWALKS,       /* 9 */
    LEVEL_161_ATMOSPHERE_BASEMENT,     /* 10 */
    LEVEL_162_QUEENS_LAIR,             /* 11 */
    LEVEL_211_LIVING_QUARTERS,         /* 12 */
    LEVEL_212_CANTEEN,                 /* 13 */
    LEVEL_213_MEETING_TOWER,           /* 14 */
    LEVEL_222_LEADWORKS,               /* 15 */
    LEVEL_242_TUNNELS_AND_DUCTS_1,     /* 16 */
    LEVEL_231_MINING_AND_SMELTING,     /* 17 */
    LEVEL_232_FURNACE_CONTROLS,        /* 18 */
    LEVEL_243_TUNNELS_AND_DUCTS_2,     /* 19 */
    LEVEL_262_LEAD_MOULD,              /* 20 */
    LEVEL_263_QUEENS_LAIR,             /* 21 */
    LEVEL_311_TUNNELS,                 /* 22 */
    LEVEL_321_PILOTS_CHAMBERS_1,       /* 23 */
    LEVEL_331_CANYONS_AND_CATACOMBS,   /* 24 */
    LEVEL_322_PILOTS_CHAMBERS_2,       /* 25 */
    LEVEL_351_SECRETS,                 /* 26 */
    LEVEL_352_INORGANICS_1,            /* 27 */
    LEVEL_323_PILOTS_CHAMBERS_3,       /* 28 */
    LEVEL_371_DROP_LIFTS,              /* 29 */
    LEVEL_353_INORGANICS_2,            /* 30 */
    LEVEL_324_PILOTS_CHAMBERS_4,       /* 31 */
    LEVEL_381_EGG_CHAMBERS,            /* 32 */
    LEVEL_325_PILOTS_CHAMBERS_5,       /* 33 */
    LEVEL_391_QUEENS_LAIR,             /* 34 */
    LEVEL_COUNT
};

/* name: inferred from use: check_password sets its bits from the cheat codes; shots skip ammo use under 0x20, countdown_start does nothing under 4 */
extern unsigned int g_cheat_flags;
/* g_cheat_flags bits. names: the cheats menu labels (g_cheats_menu strings) for the bits it
 * sets, otherwise inferred from use. check_password sets bits from the cheat codes
 * "1GOTPINK8CIDBOOTSON", "DYINGTONIGHT", "BOXOVTRICKS" and "OBLIVIONTRIP". */
#define CHEAT_FLAG_NO_CLIP        0x1   /* name: inferred from use: player movement ignores blocked cells */
#define CHEAT_FLAG_INDESTRUCTIBLE 0x4   /* "Player indestructible: "; OBLIVIONTRIP */
#define CHEAT_FLAG_FLOOR_SNAP     0x8   /* name: inferred from use: player movement takes every floor height */
#define CHEAT_FLAG_DEBUG          0x10  /* name: runtime evidence: the debug display (usage bars, frame time; doors not drawn) */
#define CHEAT_FLAG_UNLIMITED_AMMO 0x20  /* "Unlimited ammunition: "; BOXOVTRICKS */
#define CHEAT_FLAG_CHEATS_MENU    0x80  /* name: inferred from use: the main menu shows the cheats entry; no ending movie; 1GOTPINK8CIDBOOTSON */
#define CHEAT_FLAG_ACTORS_WAKE    0x100 /* name: runtime evidence: idle actors wake only while set */
#define CHEAT_FLAG_DRAW_LIFTS     0x200 /* name: runtime evidence: read only by lift_draw; lifts are not drawn while clear */
#define CHEAT_FLAG_GIMME_WEAPONS  0x400 /* "Gimme weapons: "; DYINGTONIGHT */
/* nonzero while the CHEAT_FLAG_* bit flag is set */
#define CHEAT_IS_ACTIVE(flag) (g_cheat_flags & (flag))
/* g_cheat_flags' initial value, which the cheats menu also ORs in */
#define CHEAT_FLAGS_DEFAULT       (CHEAT_FLAG_ACTORS_WAKE | CHEAT_FLAG_DRAW_LIFTS)

/* read_demo_input: the demo records at most this many pad words, then returns DEMO_INPUT_END. names: invented */
#define DEMO_INPUT_LENGTH 900
#define DEMO_INPUT_END    0xffff
/* name: inferred from use: vblank_handler increments it every vertical blank */
extern volatile int g_frame_count;
/* name: inferred from use: set before drawing, cleared by vblank_handler; the buffer swap waits for 0 */
extern volatile int g_graphics_busy;
/* name: inferred from use: set_death_type stores it and play_death_movie switches on it */
extern short g_death_type;

/* Entries of an array (a compile-time count). name: invented */
#define ARRAY_COUNT(array) ((int)(sizeof(array) / sizeof((array)[0])))
/* g_death_type values: the mode of the last hit player_damage / player_damage_health took
 * (player_hit, player_hit_from); play_death_movie picks the death movie by it.
 * names: inferred from the callers (actor_melee_hit, hugger_cling_task, particle_hit_player,
 * explosion_damage); runtime evidence: 3 plays DEATH4.STR */
enum DeathType {
    DEATH_WARRIOR,      /* 0: warrior melee (DEATH2.STR) */
    DEATH_DOG,          /* 1: dog and bambi melee (DEATH5.STR) */
    DEATH_QUEEN,        /* 2: the Queen's melee (DEATH3.STR) */
    DEATH_SHOT,         /* 3: an enemy bullet (DEATH4.STR); armour plays SOUND_SHOT_IMPACT */
    DEATH_FACEHUGGER,   /* 4: facehugger and chestburster bites, a clinging facehugger (DEATH1.STR) */
    DEATH_HAZARD        /* 5: explosions, jets, acid and synthetic blood, enemy grenades; also any other value (DEATH6.STR) */
};

/* Pad input (src/main.c pad_poll, pad_reset). */
/* name: inferred from use: pad_poll stores the buttons newly down this frame */
extern unsigned int g_pad_pressed;
/* name: inferred from use: pad_poll stores the PadRead result */
extern unsigned int g_pad_held;
/* name: inferred from use: pad_poll keeps the previous frame's held buttons */
extern unsigned int g_pad_previous;
/* name: inferred from use: pad_poll stores pressed buttons, then held buttons every 11 frames after 20 */
extern unsigned int g_pad_repeat;
/* name: inferred from use: pad_poll counts frames the buttons stay unchanged, up to 20 */
extern short g_pad_hold_frames;
/* name: inferred from use: pad_poll counts frames between auto-repeats */
extern short g_pad_repeat_clock;

extern void pad_poll(void);
extern void pad_reset(void);

/* Pad bit of each player action, set per control method by set_button_layout (src/main.c) */
extern short g_button_use, g_button_weapon, g_button_fire, g_button_run, g_button_look;
extern short g_button_strafe_left, g_button_strafe_right, g_button_grenade;
/* src/main.c: -1 (and flag 8) once buttons 0x800 and 0x100 are held for 120 frames */
extern short reset_combo_check(int buttons);

/* src/countdown_timer.c */
extern void countdown_start(short mode);

/* src/main.c: the game's random number generator */
extern unsigned int random(void);

/* get_difficulty values: the options menu's difficulty choice (g_options_menu[3].value), an
 * index into g_difficulty_choices. names: the choice strings in table order (0x8006c1d8:
 * "Acid Reign", "Raging Terror", "Xenomania"); Xenomania also sets GAME_FLAG_HARD */
enum Difficulty {
    DIFFICULTY_ACID_REIGN,     /* 0 */
    DIFFICULTY_RAGING_TERROR,  /* 1 */
    DIFFICULTY_XENOMANIA       /* 2 */
};

/* src/frontend.c: the selected difficulty (an enum Difficulty value) */
extern short get_difficulty(void);

struct TASK;

/* src/frontend.c */
extern void set_difficulty(short value);
extern void set_death_type(short value);

/* set_button_layout modes: the control method (the options menu's controller picture, recorded
 * in the password by get_control_method). names: invented, from the button each one fires with */
enum ControlMethod {
    CONTROL_METHOD_FIRE_SQUARE,   /* 0: the default (PADRleft fires) */
    CONTROL_METHOD_FIRE_CROSS,    /* 1: PADRdown fires */
    CONTROL_METHOD_FIRE_TRIANGLE, /* 2: PADRup fires */
    CONTROL_METHOD_FIRE_CIRCLE,   /* 3: PADRright fires */
    CONTROL_METHOD_COUNT
};

/* src/main.c */
extern void set_button_layout(short mode);
extern void menu_display_task(struct TASK *task);
extern void level_unload(void);
extern void cd_seek_file(int n);

#endif
