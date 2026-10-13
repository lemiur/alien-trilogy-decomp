/* Boot, level loop and the services it starts: random numbers, the frame and
 * backdrop tasks, VRAM slots, level loading, demo playback, pad input, the heap,
 * CD file loading, texture pages, CLUT rows, the vblank handler and the display
 * buffers. Retail .text 0x8001380c..0x80015cb0, the first module of the image
 * (Tasker.c follows). One translation unit: its .sdata (0x8008cd74..0x8008cdb8:
 * g_idx, the mode masks, g_action_index, g_cheat_flags, g_reset_hold_start,
 * g_heap_snapshot_current, then fatal_error's "%s\n") leads .sdata, its .rdata
 * (heap_alloc's and fatal_error's strings at 0x800101e0) leads .rdata and its
 * .data (g_clut_sets, g_page_sets, the random seed, g_file_table at 0x8006a578)
 * leads .data. g_idx (read_demo_input) precedes set_button_layout's masks and g_reset_hold_start
 * (reset_combo_check) follows g_action_index, so 0x8001380c..0x800147f4 is one module;
 * nothing marks a boundary before Tasker.c. Name invented.
 */
#include <sys/types.h>
#include <MALLOC.H>
#include <LIBETC.H>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LIBCD.H>
#include <KERNEL.H>
#include <LIBAPI.H>
#include <LEVEL.H>
#include <TASK.H>
#include <TRACK.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <ACTOR.H>
#include <MODEL.H>
#include <PARTICLE.H>
#include <MEMCARD.H>
#include <HEAP.H>
#include <HEAPFILE.H>
#include <AUDIO.H>
#include <GAME.H>
#include <VIEW.H>
#include <DISPLAY.H>
#include <TEXTURE.H>
#include <TEXT.H>
#include <HUD.H>
#include <SAVE.H>
#include <MENU.H>
#include <CAMERA.H>
#include <COMMAND.H>
#include <DEBRIS.H>
#include <MOVIE.H>

#define ABS(x) ((x) >= 0 ? (x) : -(x))

/* Words of the random generator state (g_random_state, the seed s_13a1c). name: invented */
#define RANDOM_STATE_WORDS 6

/* Frames Start+Select must be held before reset_combo_check resets. name: invented */
#define RESET_HOLD_FRAMES 120

/* pad_poll auto-repeat of a held pad state. names: invented */
#define PAD_REPEAT_DELAY 20    /* g_pad_hold_frames saturates here; repeats start once it is reached */
#define PAD_REPEAT_INTERVAL 11 /* frames between repeats while held */

/* boss_backdrop_draw: game_display_task's TASK.state words and the brightness range it ramps
 * through. names: invented */
#define BACKDROP_STATE_LEVEL 2   /* green level of the gradient */
#define BACKDROP_STATE_FALLING 3 /* nonzero while the level ramps down */
#define BACKDROP_LEVEL_MAX 96
#define BACKDROP_LEVEL_MIN 16

/* Level sprite pages (PAGE_SET_SPRITES) vram_slots_reserve locks on the Queen levels.
 * names: invented */
#define QUEEN_PAGE_A 1
#define QUEEN_PAGE_B 2
#define QUEEN_PAGE_C 4
#define QUEEN_PAGE_D 5

/* fatal_error's screen: a dark red background and the debug font's text window.
 * names: invented */
#define ERROR_BACKGROUND_RED 64
#define ERROR_FONT_X 960        /* FntLoad: the font's texture page in VRAM */
#define ERROR_FONT_Y 256
#define ERROR_TEXT_X 16
#define ERROR_TEXT_Y 16
#define ERROR_TEXT_HEIGHT 200
#define ERROR_TEXT_LENGTH 512   /* FntOpen's character buffer */

/* Draw environments' texture page: the 8-bit page at x 512 (GetTPage x). name: invented */
#define DRAW_ENV_TPAGE_X 512

/* Runtime 3.3 functions called without an SDK prototype. */
extern void SetBlockFill(void *);

/* Other modules. */
extern void level_parse_map(LevelMap *level);
extern void lights_task(TASK *);

/* Globals of other modules. */
/* name: inferred from use: random steps it, random_seed loads it */
extern unsigned int g_random_state[RANDOM_STATE_WORDS];
extern unsigned short *g_tab;
extern int g_boss_level;
/* name: runtime evidence, analysis/runtime/globals.json: DpqColor depth of the level-start fade, 0x1000 down to 0 by 0x100 a frame */
extern long g_fade_depth;
/* Observable counter used by the heap-overflow diagnostic. */
extern volatile int g_cd_read_count;
extern int g_display_mode;
/* name: runtime evidence, analysis/runtime/globals.json: the two display buffers g_display alternates between */
extern DisplayBuffer *g_display_buffers;
/* Drawing environments for the two buffers; names invented. */
extern DRAWENV g_draw_env_a;
extern DRAWENV g_draw_env_b;

/* Functions of this module referenced before their definitions. */
void boss_backdrop_draw(TASK *task);
void game_display_task(TASK *task);
void vram_slots_clear_users(void);
void vram_slots_release(void);
void level_play(void);
void frontend_run(void);
void install_vblank_handler(void);
void clear_vram(unsigned char red, unsigned char green, unsigned char blue);
void init_cd(void);
void init_display_buffers(void);
void level_start(int level);
void cd_seek_file(int n);
int cd_read_callback(void);
void vblank_handler(void);

short g_idx = 0;

/* Stack size the startup module (SN 2MBYTE.OBJ) reads before it calls main. */
unsigned long _stacksize = STACK_SIZE;

/* The four d-pad bits (up, down, left, right). No code in the image reads them. */
short g_8cd7c[] = { PADLup, PADLdown, PADLleft, PADLright };

/* Pad bit of each player action; set_button_layout stores one set per control method.
 * names: runtime evidence, analysis/runtime/globals.json (button presses at level
 * start: use/look-down, weapon change/look-up, fire, strafe, run, look, grenade). */
short g_button_use = PADRup;
short g_button_weapon = PADRdown;
short g_button_fire = PADRleft;
short g_8cd8a = PADstart;
short g_8cd8c = 0;
short g_8cd8e = 0;
short g_8cd90 = PADselect;
short g_button_strafe_right = PADR1;
short g_button_run = PADR2;
short g_button_strafe_left = PADL1;
short g_button_look = PADL2;
short g_button_grenade = PADRright;

/* name: inferred from use: the origin, a point point_heading_xz and actor_line_of_sight measure from */
SVECTOR g_zero_vector = { 0, 0, 0, 0 };

short g_action_index = -1;
/* name: inferred from use: check_password sets its bits from the cheat codes; shots skip ammo use under 0x20, countdown_start does nothing under 4 */
unsigned int g_cheat_flags = CHEAT_FLAGS_DEFAULT;

/* name: runtime evidence, analysis/runtime/globals.json: frame at which the Start+Select reset combination was last released */
int g_reset_hold_start = 0;

short g_heap_snapshot_current = -1;

/* Strings of heap_alloc and fatal_error. Each array's extent includes its alignment
 * padding; each is placed by its own address. identity-TODO: possibly string
 * literals in the original; named here because tools/section_layout.py bounds the
 * start of .rdata by the first datum a unit map names (0x800101e0). */
const char s_heap_overflow[16] = "Out of memory";
const char s_error_title[20] = "INTERNAL ERROR: ";
const char s_error_format[4] = "%s\n";

/* CLUT runs reserved at boot (init_clut_rows); the rows are allocated then. */
ClutSet g_clut_sets[CLUT_SET_COUNT] = {
    { 0, 480, 0, 32, 0 },
    { 256, 480, 1, 32, 0 }
};

/* Texture page grids reserved at boot (init_texture_pages); the pages are allocated then. */
TexturePageSet g_page_sets[PAGE_SET_COUNT] = {
    { 320, 0, 512, 512, 128, 128, 0, 12, 0 },
    { 512, 0, 1024, 512, 256, 256, 0, 5, 0 },
    { 640, 256, 896, 512, 256, 256, 1, 4, 0 },
    { 896, 256, 1024, 512, 128, 64, 0, 8, 0 }
};

/* Six-word seed random_init loads into g_random_state. */
static unsigned int s_13a1c[RANDOM_STATE_WORDS] = { 0xabcd0123, 0x10, 0xf0ad0000, 0xda0e0, 1, 0x1aa01001 };

/* Every file the game loads from CD, by index; load_file fills the rest of an entry. */
HeapFile g_file_table[FILE_TABLE_COUNT] = {
    { "\\GFX\\OBJ3D.BND;1" },
    { "\\GFX\\PULSE.BND;1" },
    { "\\GFX\\MM9.BND;1" },
    { "\\GFX\\SHOTGUN.BND;1" },
    { "\\GFX\\FLAME.BND;1" },
    { "\\GFX\\SMART.BND;1" },
    { "\\NME\\DOG.BND;1" },
    { "\\NME\\HUGGER.BND;1" },
    { "\\NME\\WAR.BND;1" },
    { "\\NME\\EGGS.BND;1" },
    { "\\NME\\HANDLER.BND;1" },
    { "\\NME\\SYNTH.BND;1" },
    { "\\NME\\GUARD.BND;1" },
    { "\\NME\\SOLDIER.BND;1" },
    { "\\NME\\BURSTER.BND;1" },
    { "\\NME\\BAMBI.BND;1" },
    { "\\NME\\FINGERS.BND;1" },
    { "\\NME\\WARCEIL.BND;1" },
    { "\\NME\\QUEEN.BND;1" },
    { "\\NME\\DOGCEIL.BND;1" },
    { "\\NME\\COLONIST.BND;1" },
    { "\\SECT11\\111GFX.BIN;1" },
    { "\\SECT11\\L111LEV.MAP;1" },
    { "\\SECT11\\112GFX.BIN;1" },
    { "\\SECT11\\L112LEV.MAP;1" },
    { "\\SECT11\\113GFX.BIN;1" },
    { "\\SECT11\\L113LEV.MAP;1" },
    { "\\SECT11\\114GFX.BIN;1" },
    { "\\SECT11\\L114LEV.MAP;1" },
    { "\\SECT11\\115GFX.BIN;1" },
    { "\\SECT11\\L115LEV.MAP;1" },
    { "\\SECT11\\121GFX.BIN;1" },
    { "\\SECT11\\L121LEV.MAP;1" },
    { "\\SECT11\\122GFX.BIN;1" },
    { "\\SECT11\\L122LEV.MAP;1" },
    { "\\SECT11\\123GFX.BIN;1" },
    { "\\SECT11\\L123LEV.MAP;1" },
    { "\\SECT11\\131GFX.BIN;1" },
    { "\\SECT11\\L131LEV.MAP;1" },
    { "\\SECT11\\132GFX.BIN;1" },
    { "\\SECT11\\L132LEV.MAP;1" },
    { "\\SECT12\\141GFX.BIN;1" },
    { "\\SECT12\\L141LEV.MAP;1" },
    { "\\SECT12\\151GFX.BIN;1" },
    { "\\SECT12\\L151LEV.MAP;1" },
    { "\\SECT12\\152GFX.BIN;1" },
    { "\\SECT12\\L152LEV.MAP;1" },
    { "\\SECT12\\153GFX.BIN;1" },
    { "\\SECT12\\L153LEV.MAP;1" },
    { "\\SECT12\\154GFX.BIN;1" },
    { "\\SECT12\\L154LEV.MAP;1" },
    { "\\SECT12\\155GFX.BIN;1" },
    { "\\SECT12\\L155LEV.MAP;1" },
    { "\\SECT12\\161GFX.BIN;1" },
    { "\\SECT12\\L161LEV.MAP;1" },
    { "\\SECT12\\162GFX.BIN;1" },
    { "\\SECT12\\L162LEV.MAP;1" },
    { "\\SECT21\\211GFX.BIN;1" },
    { "\\SECT21\\L211LEV.MAP;1" },
    { "\\SECT21\\212GFX.BIN;1" },
    { "\\SECT21\\L212LEV.MAP;1" },
    { "\\SECT21\\213GFX.BIN;1" },
    { "\\SECT21\\L213LEV.MAP;1" },
    { "\\SECT21\\222GFX.BIN;1" },
    { "\\SECT21\\L222LEV.MAP;1" },
    { "\\SECT21\\231GFX.BIN;1" },
    { "\\SECT21\\L231LEV.MAP;1" },
    { "\\SECT21\\232GFX.BIN;1" },
    { "\\SECT21\\L232LEV.MAP;1" },
    { "\\SECT22\\242GFX.BIN;1" },
    { "\\SECT22\\L242LEV.MAP;1" },
    { "\\SECT22\\243GFX.BIN;1" },
    { "\\SECT22\\L243LEV.MAP;1" },
    { "\\SECT22\\262GFX.BIN;1" },
    { "\\SECT22\\L262LEV.MAP;1" },
    { "\\SECT22\\263GFX.BIN;1" },
    { "\\SECT22\\L263LEV.MAP;1" },
    { "\\SECT31\\311GFX.BIN;1" },
    { "\\SECT31\\L311LEV.MAP;1" },
    { "\\SECT31\\321GFX.BIN;1" },
    { "\\SECT31\\L321LEV.MAP;1" },
    { "\\SECT31\\331GFX.BIN;1" },
    { "\\SECT31\\L331LEV.MAP;1" },
    { "\\SECT31\\322GFX.BIN;1" },
    { "\\SECT31\\L322LEV.MAP;1" },
    { "\\SECT32\\351GFX.BIN;1" },
    { "\\SECT32\\L351LEV.MAP;1" },
    { "\\SECT32\\352GFX.BIN;1" },
    { "\\SECT32\\L352LEV.MAP;1" },
    { "\\SECT31\\323GFX.BIN;1" },
    { "\\SECT31\\L323LEV.MAP;1" },
    { "\\SECT32\\361GFX.BIN;1" },
    { "\\SECT32\\L361LEV.MAP;1" },
    { "\\SECT31\\324GFX.BIN;1" },
    { "\\SECT31\\L324LEV.MAP;1" },
    { "\\SECT32\\371GFX.BIN;1" },
    { "\\SECT32\\L371LEV.MAP;1" },
    { "\\SECT32\\353GFX.BIN;1" },
    { "\\SECT32\\L353LEV.MAP;1" },
    { "\\SECT31\\325GFX.BIN;1" },
    { "\\SECT31\\L325LEV.MAP;1" },
    { "\\SECT32\\381GFX.BIN;1" },
    { "\\SECT32\\L381LEV.MAP;1" },
    { "\\SECT32\\391GFX.BIN;1" },
    { "\\SECT32\\L391LEV.MAP;1" },
    { "\\GFX\\PICKGFX.BND;1" },
    { "\\GFX\\PICKMOD.BND;1" },
    { "\\LANGUAGE\\PNL0GFXU.BND;1" },
    { "\\LANGUAGE\\PNL1GFXU.BND;1" },
    { "\\GFX\\LOGOSGFX.BND;1" },
    { "\\GFX\\FONT1GFX.BND;1" },
    { "\\GFX\\EXPLGFX.BND;1" },
    { "\\SFX\\PERMSFX.BIN;1" },
    { "\\SFX\\111SFX.BIN;1" },
    { "\\SFX\\112SFX.BIN;1" },
    { "\\SFX\\113SFX.BIN;1" },
    { "\\SFX\\122SFX.BIN;1" },
    { "\\SFX\\131SFX.BIN;1" },
    { "\\SFX\\114SFX.BIN;1" },
    { "\\SFX\\141SFX.BIN;1" },
    { "\\SFX\\115SFX.BIN;1" },
    { "\\SFX\\154SFX.BIN;1" },
    { "\\SFX\\155SFX.BIN;1" },
    { "\\SFX\\161SFX.BIN;1" },
    { "\\SFX\\162SFX.BIN;1" },
    { "\\SFX\\211SFX.BIN;1" },
    { "\\SFX\\212SFX.BIN;1" },
    { "\\SFX\\213SFX.BIN;1" },
    { "\\SFX\\222SFX.BIN;1" },
    { "\\SFX\\242SFX.BIN;1" },
    { "\\SFX2\\231SFX.BIN;1" },
    { "\\SFX2\\232SFX.BIN;1" },
    { "\\SFX2\\243SFX.BIN;1" },
    { "\\SFX2\\262SFX.BIN;1" },
    { "\\SFX2\\263SFX.BIN;1" },
    { "\\SFX2\\311SFX.BIN;1" },
    { "\\SFX2\\321SFX.BIN;1" },
    { "\\SFX2\\331SFX.BIN;1" },
    { "\\SFX2\\351SFX.BIN;1" },
    { "\\SFX2\\352SFX.BIN;1" },
    { "\\SFX2\\371SFX.BIN;1" },
    { "\\SFX2\\353SFX.BIN;1" },
    { "\\SFX2\\381SFX.BIN;1" },
    { "\\SFX2\\391SFX.BIN;1" },
    { "\\SFX2\\STARTSFX.BIN;1" },
    { "\\STR\\INTRO.STR;1" },
    { "\\STR\\PROBE.STR;1" },
    { "\\STR\\ACCLAIM.STR;1" },
    { "\\STR\\DEATH1.STR;1" },
    { "\\STR\\DEATH2.STR;1" },
    { "\\STR\\DEATH3.STR;1" },
    { "\\STR\\DEATH4.STR;1" },
    { "\\STR\\DEATH5.STR;1" },
    { "\\STR\\DEATH6.STR;1" },
    { "\\STR\\LEGAL.STR;1" },
    { "\\STR\\S1_2.STR;1" },
    { "\\STR\\S2_3.STR;1" },
    { "\\STR\\GAMEOVER.STR;1" },
    { "\\STR\\CREDITS.STR;1" },
    { "\\STR\\FOX.STR;1" },
    { "\\GFX\\COLONY.BS;1" },
    { "\\GFX\\PRISHOLD.BS;1" },
    { "\\GFX\\BONESHIP.BS;1" },
    { "\\GFX\\ICONS.IFF;1" },
    { "\\GFX\\PIRATE.IFF;1" },
    { "\\LANGUAGE\\MISSIONU.TXT;1" },
    { "\\GFX\\CREDITS.TXT;1" },
    { "\\GFX\\DEMO111.BIN;1" },
    { "\\GFX\\DEMO211.BIN;1" },
    { "\\GFX\\DEMO311.BIN;1" },
    { "\\GFX\\CONTROL.BND;1" }
};

/* name: runtime evidence, analysis/runtime/functions.json */
void set_button_layout(short mode) {
    if (mode == CONTROL_METHOD_FIRE_TRIANGLE) goto mode_two;
    if (mode < CONTROL_METHOD_FIRE_CIRCLE) { if (mode == CONTROL_METHOD_FIRE_CROSS) goto mode_one; goto default_mode; }
    if (mode == CONTROL_METHOD_FIRE_CIRCLE) goto mode_three;
    goto default_mode;
mode_one:
    g_button_weapon=PADRup; g_button_use=PADRright; g_button_fire=PADRdown; g_button_grenade=PADRleft;
    g_button_strafe_left=PADL1; g_button_strafe_right=PADR1; g_button_look=PADL2; g_button_run=PADR2; return;
mode_two:
    g_button_weapon=PADRright; g_button_use=PADRleft; g_button_fire=PADRup; g_button_grenade=PADRdown;
    g_button_strafe_left=PADL2; g_button_strafe_right=PADR2; g_button_look=PADL1; g_button_run=PADR1; return;
mode_three:
    g_button_weapon=PADRleft; g_button_use=PADRdown; g_button_fire=PADRright; g_button_grenade=PADRup;
    g_button_strafe_left=PADL2; g_button_strafe_right=PADR2; g_button_look=PADL1; g_button_run=PADR1; return;
default_mode:
    g_button_weapon=PADRdown; g_button_use=PADRup; g_button_fire=PADRleft; g_button_grenade=PADRright;
    g_button_strafe_left=PADL1; g_button_strafe_right=PADR1; g_button_look=PADL2; g_button_run=PADR2;
}

/* name: runtime evidence, analysis/runtime/functions.json */
short reset_combo_check(int buttons)
{
    if ((buttons & PADstart) && (buttons & PADselect)) {
        if (ABS(g_frame_count - g_reset_hold_start) > RESET_HOLD_FRAMES) {
            g_game_flags |= GAME_FLAG_RESET;
            return -1;
        }
    } else {
        g_reset_hold_start = g_frame_count;
    }
    return 0;
}

/* name: inferred from its body, which copies a six-word seed into the random state */
void random_seed(const unsigned int *source)
{
    short i;
    for (i = 0; i < RANDOM_STATE_WORDS; ++i)
        g_random_state[i] = source[i];
}

/* name: runtime evidence, analysis/runtime/functions.json */
void random_init(void) { random_seed(s_13a1c); }

/* name: inferred from its body, which steps the generator state and returns the new value */
unsigned int random(void)
{
    short i;
    unsigned int old_value = g_random_state[RANDOM_STATE_WORDS - 2];
    unsigned int total = old_value + g_random_state[1];
    unsigned int incremented;
    if (total < old_value)
        total++;
    incremented = g_random_state[RANDOM_STATE_WORDS - 1] + 1;
    g_random_state[0] = total + incremented;

    for (i = 0; i < RANDOM_STATE_WORDS - 1; i++)
        g_random_state[i + 1] = g_random_state[i];
    return g_random_state[0];
}

/* name: runtime evidence, analysis/runtime/functions.json */
void menu_display_task(TASK *task)
{
    DisplayBuffer *display;
    if (task->state[0] == 1) {
        g_graphics_busy = -1;
        while (g_graphics_busy != 0) {
        }
        DrawSync(0);
        PutDispEnv(&g_display->disp);
        DrawOTag(&g_render_buffer[OT_LENGTH - 1]);
        display = g_display == g_render_workspace ? g_display_default : g_render_workspace;
        g_display = display;
        g_render_buffer = display->ot;
        g_sprite_workspace = display->prims;
        ClearOTagR(g_render_buffer, OT_LENGTH);
        AddPrim(&g_render_buffer[OT_LENGTH - 1],
                &g_display->draw);
    }
    g_next_task = task->Next;
}

/* Pulsing green backdrop: a full-screen semi-transparent gradient whose
 * brightness ramps between BACKDROP_LEVEL_MIN and BACKDROP_LEVEL_MAX. */
/* name: runtime evidence, analysis/runtime/functions.json */
void boss_backdrop_draw(TASK *task)
{
    CVECTOR top;
    CVECTOR bottom;
    POLY_G4 *poly;

    if (g_boss_level == 0)
        return;
    top.r = 0;
    top.g = task->state[BACKDROP_STATE_LEVEL];
    top.b = task->state[BACKDROP_STATE_LEVEL] >> 1;
    bottom.r = 0;
    bottom.g = task->state[BACKDROP_STATE_LEVEL];
    bottom.b = task->state[BACKDROP_STATE_LEVEL] >> 1;
    DpqColor(&top, g_fade_depth, &top);
    DpqColor(&bottom, g_fade_depth, &bottom);
    poly = g_sprite_workspace;
    setRGB0(poly, 0, 0, 0);
    setRGB1(poly, 0, 0, 0);
    *(u_long *)&poly->r2 = *(u_long *)&top;
    *(u_long *)&poly->r3 = *(u_long *)&bottom;
    setPolyG4(poly);
    setSemiTrans(poly, 1);
    setXY4(poly, 0, 0, SCREEN_WIDTH - 1, 0, 0, SCREEN_HEIGHT - 1, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    addPrim(g_render_buffer, poly);
    g_sprite_workspace = poly + 1;
    if (task->state[BACKDROP_STATE_FALLING] == 0) {
        if (++task->state[BACKDROP_STATE_LEVEL] > BACKDROP_LEVEL_MAX) {
            task->state[BACKDROP_STATE_LEVEL] = BACKDROP_LEVEL_MAX;
            task->state[BACKDROP_STATE_FALLING] = 1;
        }
    } else {
        if ((int)--task->state[BACKDROP_STATE_LEVEL] < BACKDROP_LEVEL_MIN) {
            task->state[BACKDROP_STATE_LEVEL] = BACKDROP_LEVEL_MIN;
            task->state[BACKDROP_STATE_FALLING] = 0;
        }
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void game_display_task(TASK *task)
{
    DisplayBuffer *display;
    boss_backdrop_draw(task);
    upload_queue_flush();
    task->state[1] = g_frame_count - task->state[0];
    g_graphics_busy = -1;
    if (g_graphics_busy != 0) {
        while (g_graphics_busy != 0) {
        }
    }
    if (task->state[1] == 0) {
        g_graphics_busy = -1;
        while (g_graphics_busy != 0) {
        }
    }
    task->state[0] = g_frame_count;
    DrawSync(0);
    PutDispEnv(&g_display->disp);
    DrawOTag(&g_render_buffer[OT_LENGTH - 1]);
    display = g_display == g_render_workspace ? g_display_default : g_render_workspace;
    g_display = display;
    g_render_buffer = display->ot;
    g_sprite_workspace = display->prims;
    ClearOTagR(g_render_buffer, OT_LENGTH);
    AddPrim(&g_render_buffer[OT_LENGTH - 1],
            &g_display->draw);
    g_next_task = task->Next;
}

/* name: inferred from its body, which frees the unlocked slots of the first list and every slot of the second */
void vram_slots_release(void)
{
    short count;
    TexturePage *slot;

    count = g_page_sets[PAGE_SET_SPRITES].count;
    slot = g_page_sets[PAGE_SET_SPRITES].pages;
    while (count != 0) {
        if ((slot->reserved & 1) == 0) {
            slot->users = 0;
            slot->pixels = 0;
        }
        count--;
        slot++;
    }

    count = g_page_sets[PAGE_SET_SMALL_SPRITES].count;
    slot = g_page_sets[PAGE_SET_SMALL_SPRITES].pages;
    while (count != 0) {
        slot->reserved = 0;
        slot->users = 0;
        slot->pixels = 0;
        count--;
        slot++;
    }
}

/* name: inferred from its body, which clears the users of every unlocked slot */
void vram_slots_clear_users(void)
{
    short count;
    TexturePage *slot;

    count = g_page_sets[PAGE_SET_SPRITES].count;
    slot = g_page_sets[PAGE_SET_SPRITES].pages;
    while (count != 0) {
        if ((slot->reserved & 1) == 0)
            slot->users = 0;
        count--;
        slot++;
    }

    count = g_page_sets[PAGE_SET_SMALL_SPRITES].count;
    slot = g_page_sets[PAGE_SET_SMALL_SPRITES].pages;
    while (count != 0) {
        if ((slot->reserved & 1) == 0)
            slot->users = 0;
        count--;
        slot++;
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void level_unload(void)
{
    short mode = g_action_index;
    if (mode != -1) {
        heap_release(mode);
        audio_free_bank(-1);
        g_action_index = -1;
    }
}

/* name: inferred from its body, which marks the heap and loads the level's sound bank, sprites, map, models and (demo mode) demo input; string-only BSim pair dos 0x10640 shares its \GFX\DEMO111.BIN..DEMO311.BIN names */
void level_load(void)
{
    HeapFile *file;

    level_unload();
    g_action_index = heap_mark();
    unpack_buffers_alloc();
    audio_init();
    if (g_level >= LEVEL_311_TUNNELS)
        panel_sprites_load_alternate();
    audio_select_level_bank(g_level);
    level_panel_sprites_load();
    message_box_advance();
    level_map_file_load(&g_level_map);
    level_parse_map(&g_level_map);
    door_models_prepare();
    lift_models_prepare();
    level_sprites_load();
    if (g_game_flags & GAME_FLAG_DEMO) {
        file = 0;
        switch (g_level) {
        case LEVEL_111_ENTRANCE:
            file = &g_file_table[FILE_DEMO111_BIN];
            break;
        case LEVEL_211_LIVING_QUARTERS:
            file = &g_file_table[FILE_DEMO211_BIN];
            break;
        case LEVEL_311_TUNNELS:
            file = &g_file_table[FILE_DEMO311_BIN];
            break;
        }
        if (file != 0) {
            file->buffer = 0;
            file->loaded_size = 0;
            load_file(file);
            g_tab = file->buffer;
        }
    }
    message_box_advance();
}

/* name: inferred from its body, which locks VRAM slots 1, 2, 4 and 5 on levels 11, 21 and 34 and frees them on every other level */
void vram_slots_reserve(void)
{
    TexturePage *slots = g_page_sets[PAGE_SET_SPRITES].pages;
    int level = g_level;
    TexturePage *slot1 = slots + QUEEN_PAGE_A;
    TexturePage *slot2 = slots + QUEEN_PAGE_B;
    TexturePage *slot4 = slots + QUEEN_PAGE_C;
    TexturePage *slot5 = slots + QUEEN_PAGE_D;

    if (level == LEVEL_162_QUEENS_LAIR || level == LEVEL_263_QUEENS_LAIR || level == LEVEL_391_QUEENS_LAIR) {
        slot1->users = 1;
        slot1->reserved = 1;
        slot2->users = 1;
        slot2->reserved = 1;
        slot4->users = 1;
        slot4->reserved = 1;
        slot5->users = 1;
        slot5->reserved = 1;
    } else {
        slots[QUEEN_PAGE_A].users = 0;
        slots[QUEEN_PAGE_A].reserved = 0;
        slots[QUEEN_PAGE_B].users = 0;
        slots[QUEEN_PAGE_B].reserved = 0;
        slots[QUEEN_PAGE_C].users = 0;
        slots[QUEEN_PAGE_C].reserved = 0;
        slots[QUEEN_PAGE_D].users = 0;
        slots[QUEEN_PAGE_D].reserved = 0;
    }
}

/* Sets up a level: loads it, resets every subsystem and creates the level tasks.
 * main passes the level; the body reads g_level. */
/* name: runtime evidence, analysis/runtime/functions.json */
void level_start(int level)
{
    TASK *last;

    SetRCnt(RCntCNT1, HBLANK_COUNTER_TARGET, RCntMdNOINTR);
    StartRCnt(RCntCNT1);
    random_init();
    g_boss_level = (g_level == LEVEL_162_QUEENS_LAIR || g_level == LEVEL_263_QUEENS_LAIR || g_level == LEVEL_391_QUEENS_LAIR);
    weapon_sprite_bind_slot();
    vram_slots_reserve();
    level_load();
    g_view_position.vx = 0; g_view_position.vy = 0; g_view_position.vz = 0;
    g_view_pitch.vx = 0; g_view_pitch.vy = 0; g_view_pitch.vz = 0;
    g_view_rotation.vx = 0; g_view_rotation.vy = 0; g_view_rotation.vz = 0;
    vram_slots_release();
    TaskerInit();
    level_cells_reset(&g_level_map);
    upload_queue_reset();
    hud_messages_reset();
    particle_pool_init();
    panel_pool_init();
    flow_field_clear();
    actor_pool_init();
    lights_load();
    level_triggers_load();
    lifts_spawn();
    doors_init();
    map_objects_init();
    level_objects_init();
    actors_init();
    command_sequencer_start();
    countdown_start(g_level);
    message_box_advance();
    TaskCreate(TASK_PRIORITY_LIGHTS, lights_task);
    TaskCreate(TASK_PRIORITY_PLAYER, player_task_start);
    TaskCreate(TASK_PRIORITY_VIEW, view_render_task);
    TaskCreate(TASK_PRIORITY_PICKUPS, pickups_task);
    TaskCreate(TASK_PRIORITY_PARTICLES, particles_task);
    TaskCreate(TASK_PRIORITY_ACTORS_DRAW, actors_draw_task);
    TaskCreate(TASK_PRIORITY_COMMANDS, command_sequencer_task);
    last = TaskCreate(TASK_PRIORITY_DISPLAY, game_display_task);
    last->state[0] = 0;
    last->state[BACKDROP_STATE_LEVEL] = 0;
    last->state[BACKDROP_STATE_FALLING] = 0;
}

/* name: inferred from its body, which returns the next recorded pad word of the demo (900 at most) */
int read_demo_input(void) { int r = DEMO_INPUT_END; short i = g_idx; if (i < DEMO_INPUT_LENGTH) { g_idx = i + 1; r = g_tab[i]; } return r; }

/* One play session of the current level: runs frames until the level is
 * left (GAME_FLAG_LEVEL_COMPLETE, GAME_FLAG_RESET, GAME_FLAG_DEATH_DONE) or a demo ends
 * (GAME_FLAG_RESET), fading in and out. In demo mode (GAME_FLAG_DEMO) input comes from read_demo_input and the difficulty is
 * forced to DIFFICULTY_ACID_REIGN for the run. */
/* name: runtime evidence, analysis/runtime/functions.json */
void level_play(void)
{
    short difficulty = 0;

    if (g_game_flags & GAME_FLAG_DEMO) {
        difficulty = get_difficulty();
        set_difficulty(DIFFICULTY_ACID_REIGN);
        g_game_flags &= ~GAME_FLAG_HARD;
        set_button_layout(0);
        g_idx = 0;
    }
    g_fade_depth = FADE_DEPTH_FULL;
    g_game_flags |= GAME_FLAG_FADE;
    music_play_level(g_level);
    g_graphics_busy = -1;
    g_pad_pressed = 0;
    g_pad_previous = 0;
    g_pad_held = 0;
    if (g_graphics_busy != 0) {
        while (g_graphics_busy != 0) {
        }
    }
    if (!(g_game_flags & (GAME_FLAG_LEVEL_COMPLETE | GAME_FLAG_RESET | GAME_FLAG_DEATH_DONE))) {
        do {
            music_poll();
            if (g_game_flags & GAME_FLAG_DEMO) {
                g_pad_previous = g_pad_held;
                g_pad_held = read_demo_input();
                g_pad_pressed = ~g_pad_previous & g_pad_held;
                if (g_pad_held == DEMO_INPUT_END || PadRead(0))
                    g_game_flags |= GAME_FLAG_RESET;
                if (g_game_flags & GAME_FLAG_RESET)
                    break;
            } else {
                g_pad_previous = g_pad_held;
                g_pad_held = PadRead(0);
                g_pad_pressed = ~g_pad_previous & g_pad_held;
                reset_combo_check(g_pad_held);
            }
            if (g_game_flags & GAME_FLAG_FADE) {
                g_fade_depth -= FADE_DEPTH_STEP;
                if (g_fade_depth < 0)
                    g_fade_depth = 0;
            }
            TaskDispatch();
            if ((g_game_flags & GAME_FLAG_FADE) && g_fade_depth == 0)
                g_game_flags &= ~GAME_FLAG_FADE;
            vram_slots_clear_users();
            if (g_game_flags & GAME_FLAG_PAUSE) {
                pause_menu_run();
                g_game_flags &= ~GAME_FLAG_PAUSE;
            }
        } while (!(g_game_flags & GAME_FLAG_RESET) && !(g_game_flags & (GAME_FLAG_LEVEL_COMPLETE | GAME_FLAG_RESET | GAME_FLAG_DEATH_DONE)));
    }
    cd_seek_file(FILE_COLONY_BS);
    g_graphics_busy = -1;
    while (g_graphics_busy != 0) {
    }
    g_game_flags |= GAME_FLAG_FADE;
    while (g_game_flags & GAME_FLAG_FADE) {
        g_fade_depth += FADE_DEPTH_STEP;
        TaskDispatch();
        if (g_fade_depth > FADE_DEPTH_FULL)
            g_game_flags &= ~GAME_FLAG_FADE;
    }
    if (g_game_flags & GAME_FLAG_DEMO)
        set_difficulty(difficulty);
}

/* Input-poll state step. */
/* name: inferred from its body, which reads the pad and derives pressed, held and auto-repeat bits */
void pad_poll(void)
{
    short state = g_pad_hold_frames;
    int current;
    if (state == 0) {
        g_pad_repeat = g_pad_pressed;
    } else if (state == PAD_REPEAT_DELAY && g_pad_repeat_clock >= PAD_REPEAT_INTERVAL) {
        g_pad_repeat_clock = 0;
        g_pad_repeat = g_pad_held;
    } else {
        g_pad_repeat = 0;
    }
    g_pad_previous = g_pad_held;
    current = PadRead(0);
    g_pad_held = current;
    g_pad_pressed = (~g_pad_previous) & current;
    if (g_pad_previous == current) {
        g_pad_hold_frames++;
        if (g_pad_hold_frames > PAD_REPEAT_DELAY)
            g_pad_hold_frames = PAD_REPEAT_DELAY;
        g_pad_repeat_clock++;
    } else {
        g_pad_hold_frames = 0;
        g_pad_repeat_clock = 0;
    }
}

/* name: inferred from its body, which clears the auto-repeat state */
void pad_reset(void)
{
    g_pad_repeat = 0;
    g_pad_repeat_clock = 0;
    g_pad_hold_frames = 0;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void frontend_run(void)
{
    TaskerInit();
    start_title_sequence();
    g_game_flags = 0;
    pad_reset();
    while ((g_game_flags & GAME_FLAG_MENU_DONE) == 0) {
        pad_poll();
        TaskDispatch();
    }
    TaskerInit();
}

/* Boot, then run the front end and levels for ever. GCC 2.5.7 emits the call to
 * __main (the start of SN's 2MBYTE.OBJ at 0x800588bc) at the top of main itself. */
int main(void)
{
    short slot;
    short next;

    random_init();
    ResetGraph(0);
    SetGraphDebug(0);
    SetDispMask(0);
    InitGeom();
    SetGeomOffset(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    SetGeomScreen(PROJECTION_DISTANCE);
    SetVideoMode(0);
    install_vblank_handler();
    PadInit(0);
    clear_vram(0, 0, 0);
    init_heap();
    init_cd();
    card_init();
    init_display_buffers();
    music_enable();
    audio_enable();
    audio_init();
    music_init();
    init_clut_rows(&g_clut_sets[CLUT_SET_SPRITES]);
    init_clut_rows(&g_clut_sets[CLUT_SET_SPRITE_SHEETS]);
    init_texture_pages(&g_page_sets[PAGE_SET_SPRITES]);
    init_texture_pages(&g_page_sets[PAGE_SET_SMALL_SPRITES]);
    init_texture_pages(&g_page_sets[PAGE_SET_MENU]);
    init_texture_pages(&g_page_sets[PAGE_SET_SPRITE_SHEETS]);
    TaskerAlloc();
    particle_pool_alloc();
    panel_pool_alloc();
    actor_pool_alloc();
    upload_pool_alloc();
    lifts_alloc();
    pickup_models_load();
    level_object_models_load();
    doors_alloc();
    menu_textures_reserve();
    SetDispMask(1);
    g_level = 0;
    for (;;) {
        player_reset();
        do {
            slot = heap_mark();
            frontend_run();
            heap_release(slot);
        } while (g_level == -1);
        sprite_sets_load();
        font_init_widths();
        g_game_flags &= ~GAME_FLAG_RESTORED;
        while (g_level < LEVEL_COUNT && !(g_game_flags & (GAME_FLAG_RESET | GAME_FLAG_DEATH_DONE))) {
            password_store_level(g_level);
            show_loading_screen(g_level);
            level_start(g_level);
            show_continue_prompt(g_level);
            level_play();
            sound_stop_all();
            TaskerInit();
            level_unload();
            if (g_game_flags & GAME_FLAG_DEATH_DONE) {
                play_death_movie(g_level);
                password_restore();
            } else if (g_game_flags & GAME_FLAG_LEVEL_COMPLETE) {
                g_game_flags &= GAME_FLAG_HARD;
                next = level_complete(g_level);
                if (next == g_level) {
                    password_restore();
                } else {
                    g_level = next;
                    if (next == LEVEL_211_LIVING_QUARTERS)
                        play_story_movie(MOVIE_S1_2);
                    if (g_level == LEVEL_311_TUNNELS)
                        play_story_movie(MOVIE_S2_3);
                }
            }
        }
        if (g_level == LEVEL_COUNT && !CHEAT_IS_ACTIVE(CHEAT_FLAG_CHEATS_MENU))
            play_story_movie(MOVIE_GAMEOVER);
    }
}

/* name: inferred from its body, which claims the largest malloc block below HEAP_SIZE_MAX as the heap and clears the snapshots */
void init_heap(void)
{
    unsigned int size;
    void *memory;
    short i;
    size = HEAP_SIZE_MAX;
    do {
        size -= HEAP_SIZE_STEP;
        memory = malloc(size);
    } while (memory == 0);
    g_heap_size = size;
    g_heap_allocation = memory;
    g_heap_next = (unsigned char *)memory;
    g_heap_used = 0;
    g_heap_free = size;
    for (i = 0; i < HEAP_SNAPSHOT_COUNT; i++) {
        g_heap_snapshots[i].next = 0;
        g_heap_snapshots[i].used = 0;
        g_heap_snapshots[i].free = 0;
    }
    g_heap_snapshot_current = heap_mark();
}

/* name: inferred from its body, which takes word-aligned bytes from the heap or stops with an overflow error; BSim pair dos 0x11874 (strong) prints 'MEMMGR: No memory. %ld bytes requested' */
void *heap_alloc(int bytes)
{
    unsigned char *old_low;
    int aligned = (bytes + HEAP_ALIGN - 1) & -HEAP_ALIGN;
    if (g_heap_free >= (unsigned int)aligned) {
        old_low = g_heap_next;
        g_heap_free -= aligned;
        g_heap_next += aligned;
        g_heap_used += aligned;
        return old_low;
    }
    fatal_error(s_heap_overflow);
    return 0;
}

/* name: inferred from its body, which returns the most recently allocated bytes to the heap */
void heap_free(int bytes)
{
    int aligned = (bytes + HEAP_ALIGN - 1) & -HEAP_ALIGN;
    g_heap_next -= aligned;
    g_heap_used -= aligned;
    g_heap_free += aligned;
}

/* name: inferred from its body, which saves the heap state in a free snapshot slot */
int heap_mark(void)
{
    short free_slot = -1;
    short i;
    i = 0;
find_slot:
    if (g_heap_snapshots[i].next == 0)
        goto found_slot;
    i++;
    if (i < HEAP_SNAPSHOT_COUNT)
        goto find_slot;
    goto capture;
capture:
    if (free_slot == -1)
        return -1;
    g_heap_snapshots[free_slot].next = (unsigned int)g_heap_next;
    g_heap_snapshots[free_slot].used = g_heap_used;
    g_heap_snapshots[free_slot].free = g_heap_free;
    return free_slot;
found_slot:
    free_slot = i;
    goto capture;
}

/* name: inferred from its body, which restores a heap snapshot and frees later snapshots */
void heap_release(short slot)
{
    if (slot != -1) {
        g_heap_next = (unsigned char *)g_heap_snapshots[slot].next;
        g_heap_used = g_heap_snapshots[slot].used;
        g_heap_free = g_heap_snapshots[slot].free;
        while (slot < HEAP_SNAPSHOT_COUNT) {
            g_heap_snapshots[slot].next = 0;
            g_heap_snapshots[slot].used = 0;
            g_heap_snapshots[slot].free = 0;
            slot++;
        }
    }
}

/* name: inferred from its body, which shows "INTERNAL ERROR: " and the message, then hangs in VSync */
void fatal_error(const char *message)
{
    DRAWENV draw;
    DISPENV display;
    void *buffer;

    SetDefDrawEnv(&draw, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDispEnv(&display, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    draw.isbg = 0;
    setRGB0(&draw, ERROR_BACKGROUND_RED, 0, 0);
    SetDispMask(1);
    PutDispEnv(&display);
    PutDrawEnv(&draw);
    FntLoad(ERROR_FONT_X, ERROR_FONT_Y);
    buffer = FntOpen(ERROR_TEXT_X, ERROR_TEXT_Y, SCREEN_WIDTH, ERROR_TEXT_HEIGHT, 0, ERROR_TEXT_LENGTH);
    SetDumpFnt(buffer);
    FntPrint(s_error_title);
    FntPrint(s_error_format, message);
    FntFlush(buffer);
    for (;;) VSync(0);
}

/* name: runtime evidence, analysis/runtime/functions.json */
int cd_read_callback(void)
{
    return ++g_cd_read_count;
}

/* name: inferred from its body, which resets the callbacks, initialises the CD and looks up files 160..162 */
void init_cd(void)
{
    HeapFile *file;

    ResetCallback();
    CdInit();
    CdSetDebug(0);
    g_cd_read_count = 0;
    CdReadCallback(cd_read_callback);
    file = &g_file_table[FILE_COLONY_BS];
    CdSearchFile(&file->file, (char *)file->name);
    file = &g_file_table[FILE_PRISHOLD_BS];
    CdSearchFile(&file->file, (char *)file->name);
    file = &g_file_table[FILE_BONESHIP_BS];
    CdSearchFile(&file->file, (char *)file->name);
}

/* name: inferred from its body, which loads a CD file table entry into a heap buffer */
void load_file(HeapFile *file)
{
    CdlFILE *cd = &file->file;

    g_cd_read_count = 0;
    CdSearchFile(cd, (char *)file->name);
    CdControlB(CdlSeekL, (unsigned char *)&cd->pos, 0);
    file->loaded_size = cd->size;
    if (file->buffer == 0)
        file->buffer = heap_alloc(file->loaded_size);
    CdRead((file->loaded_size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE, file->buffer, CdlModeSpeed);
    while (CdReadSync(1, 0) > 0)
        VSync(0);
}

/* name: static evidence, analysis/runtime/functions.json */
void load_file_start(HeapFile *file)
{
    CdlFILE *cd = &file->file;

    g_cd_read_count = 0;
    CdSearchFile(cd, (char *)file->name);
    CdControl(CdlSeekL, (unsigned char *)&cd->pos, 0);
    file->loaded_size = cd->size;
    if (file->buffer == 0)
        file->buffer = heap_alloc(file->loaded_size);
    CdRead((file->loaded_size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE, file->buffer, CdlModeSpeed);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void cd_seek_file(int n) { CdControl(CdlSeekL, (unsigned char *)&g_file_table[n].file.pos, 0); }

/* name: inferred from its body, which allocates and fills a grid of texture pages */
void init_texture_pages(TexturePageSet *set)
{
    TexturePage *page = set->pages;
    short x, y;
    short i;

    if (page == 0) {
        page = set->pages = (TexturePage *)heap_alloc(set->count * sizeof(TexturePage));
    }
    x = set->x;
    y = set->y;
    for (i = 0; i < set->count; i++, page++) {
        if (set->mode == 0) {
            page->tpage = GetTPage(1, 0, x, y);
            page->x = x;
            page->y = y;
            page->reserved = 0;
            page->users = 0;
            page->pixels = 0;
            x += set->x_step >> 1;
            if (x >= set->x_end) {
                x = set->x;
                y += set->y_step;
                if (y >= set->y_end)
                    y = set->y;
            }
        } else {
            page->tpage = GetTPage(0, 0, x, y);
            page->x = x;
            page->y = y;
            page->reserved = 0;
            page->users = 0;
            page->pixels = 0;
            x += set->x_step >> 2;
            if (x >= set->x_end) {
                x = set->x;
                y += set->y_step;
                if (y >= set->y_end)
                    y = set->y;
            }
        }
    }
}

typedef struct BlockFillPacket {
    unsigned long tag;
    unsigned char red, green, blue, code;
    unsigned short x, y, width, height;
} BlockFillPacket;

/* name: inferred from its body, which fills all of VRAM with one colour through a block-fill packet */
void clear_vram(unsigned char red, unsigned char green, unsigned char blue)
{
    BlockFillPacket area;

    SetBlockFill(&area);
    area.x = 0;
    area.y = 0;
    area.width = VRAM_WIDTH - 1;
    area.height = VRAM_HEIGHT - 1;
    area.red = red;
    area.green = green;
    area.blue = blue;
    DrawPrim(&area);
    DrawSync(0);
}

/* name: inferred from its body, which returns the first unused texture page of a set */
TexturePage *find_free_texture_page(TexturePageSet *l) { short n = l->count; TexturePage *p = l->pages; while (n) { if (!p->users) return p; n--; p++; } return 0; }

/* name: inferred from its body, which OpenEvent installs it on root counter 3 (vertical blank); it counts frames and steps the message box */
void vblank_handler(void)
{
    g_frame_count++;
    g_graphics_busy = 0;
    g_sound_clock++;
    if (g_display_mode != DISPLAY_MODE_NORMAL) {
        if (g_display_mode == DISPLAY_MODE_MESSAGE_BOX)
            message_box_type();
        else
            g_display_mode = DISPLAY_MODE_NORMAL;
    }
}

/* name: inferred from its body, which sets g_display_mode inside a critical section */
void set_display_mode(int mode)
{
    EnterCriticalSection();
    g_display_mode = (short)mode;
    ExitCriticalSection();
}

/* name: inferred from its body, which opens and enables the root counter 3 event with vblank_handler */
void install_vblank_handler(void)
{
    g_frame_count = 0;
    g_graphics_busy = -1;
    set_display_mode(DISPLAY_MODE_NORMAL);
    EnterCriticalSection();
    EnableEvent(OpenEvent(RCntCNT3, EvSpINT, EvMdINTR, (EventHandler)vblank_handler));
    SetRCnt(RCntCNT3, 1, RCntMdINTR);
    StartRCnt(RCntCNT3);
    ExitCriticalSection();
}

/* name: inferred from its body, which allocates a strip of CLUT rows and computes their CLUT ids */
void init_clut_rows(ClutSet *set)
{
    short i;
    unsigned short y;
    unsigned short x;
    ClutRow *row;
    row = set->rows;
    if (row == 0) {
        row = set->rows = (ClutRow *)heap_alloc(set->count * sizeof(ClutRow));
    }
    y = set->y;
    x = set->x;
    for (i = 0; i < set->count; i++, row++) {
        row->clut = GetClut((short)x, (short)y);
        row->x = x;
        row->y = y++;
        row->in_use = 0;
    }
}

/* name: inferred from its body, which loads one 16-colour CLUT row into VRAM */
void upload_clut_row(ClutRow *row, unsigned long *pixels) { RECT r; r.x = row->x; r.y = row->y; r.w = CLUT_ROW_WIDTH; r.h = 1; LoadImage(&r, pixels); }

/* name: static evidence, analysis/runtime/functions.json */
ClutRow *find_free_clut_row(ClutSet *set) { short n = set->count; ClutRow *p = set->rows; while (n) { if (!(p->in_use & 1)) return p; n--; p++; } return 0; }

/* Allocates the two display buffers and sets up a 320x240 double buffer
 * (drawing at y 0 and 240), then makes the first buffer current. */
/* name: inferred from its body, which allocates the two display buffers and sets up the 320x240 double buffer */
void init_display_buffers(void)
{
    g_render_workspace = g_display_buffers = (DisplayBuffer *)heap_alloc(2 * sizeof(DisplayBuffer));
    g_display_default = g_render_workspace + 1;
    SetDefDrawEnv(&g_draw_env_a, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    g_draw_env_a.tpage = GetTPage(1, 0, DRAW_ENV_TPAGE_X, 0);
    g_draw_env_a.dtd = 1;
    g_draw_env_a.isbg = 1;
    setRGB0(&g_draw_env_a, 0, 0, 0);
    setRECT(&g_draw_env_a.clip, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDrawEnv(&g_render_workspace->draw, &g_draw_env_a);
    SetDefDispEnv(&g_render_workspace->disp, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDrawEnv(&g_draw_env_b, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    g_draw_env_b.tpage = GetTPage(1, 0, DRAW_ENV_TPAGE_X, 0);
    g_draw_env_b.dtd = 1;
    g_draw_env_b.isbg = 1;
    setRGB0(&g_draw_env_b, 0, 0, 0);
    setRECT(&g_draw_env_b.clip, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDrawEnv(&g_display_default->draw, &g_draw_env_b);
    SetDefDispEnv(&g_display_default->disp, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetFarColor(0, 0, 0);
    SetFogNear(FOG_NEAR, PROJECTION_DISTANCE);
    ClearOTagR(g_render_workspace->ot, OT_LENGTH);
    ClearOTagR(g_display_default->ot, OT_LENGTH);
    PutDispEnv(&g_render_workspace->disp);
    g_display = g_render_workspace;
    g_render_buffer = g_display->ot;
    g_sprite_workspace = g_display->prims;
}

/* name: inferred from its body, which sets up double buffering: buffer A draws in first
 * and shows second, buffer B the reverse. Retail writes B's dfe in both halves. */
void setup_double_buffer(RECT *first, RECT *second, u_char isbg, u_char dfe)
{
    SetDefDrawEnv(&g_draw_env_a, first->x, first->y, first->w, first->h);
    g_draw_env_a.tpage = GetTPage(1, 0, DRAW_ENV_TPAGE_X, 0);
    g_draw_env_a.dtd = 1;
    g_draw_env_b.dfe = dfe;
    g_draw_env_a.isbg = isbg;
    setRGB0(&g_draw_env_a, 0, 0, 0);
    g_draw_env_a.clip.x = first->x;
    g_draw_env_a.clip.y = first->y;
    g_draw_env_a.clip.w = first->w;
    g_draw_env_a.clip.h = first->h;
    SetDrawEnv(&g_render_workspace->draw, &g_draw_env_a);
    SetDefDispEnv(&g_render_workspace->disp, second->x, second->y, second->w, second->h);
    SetDefDrawEnv(&g_draw_env_b, second->x, second->y, second->w, second->h);
    g_draw_env_b.tpage = GetTPage(1, 0, DRAW_ENV_TPAGE_X, 0);
    g_draw_env_b.dtd = 1;
    g_draw_env_b.dfe = dfe;
    g_draw_env_b.isbg = isbg;
    setRGB0(&g_draw_env_b, 0, 0, 0);
    g_draw_env_b.clip.x = second->x;
    g_draw_env_b.clip.y = second->y;
    g_draw_env_b.clip.w = second->w;
    g_draw_env_b.clip.h = second->h;
    SetDrawEnv(&g_display_default->draw, &g_draw_env_b);
    SetDefDispEnv(&g_display_default->disp, first->x, first->y, first->w, first->h);
}
