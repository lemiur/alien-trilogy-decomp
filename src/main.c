/* main.c -- boot, level loop and the services it starts: random numbers, the frame and backdrop
 * tasks, VRAM slots, level loading, demo playback, pad input, the heap, CD file loading, texture
 * pages, CLUT rows, the vblank handler and the display buffers */
/* Retail .text 0x8001380c..0x80015cb0, the first module of the image (Tasker.c follows). One
 * translation unit: its .sdata (0x8008cd74..0x8008cdb8: g_idx, the mode masks, g_actionIndex,
 * g_cheatFlags, g_resetHoldStart, g_heapSnapshotCurrent, then Sys_FatalError's "%s\n") leads
 * .sdata, its .rdata (Heap_Alloc's and Sys_FatalError's strings at 0x800101e0) leads .rdata and its
 * .data (g_clutSets, g_pageSets, the random seed, g_fileTable at 0x8006a578) leads .data. g_idx
 * (Demo_ReadInput) precedes Sys_SetButtonLayout's masks and g_resetHoldStart (Sys_ResetComboCheck)
 * follows g_actionIndex, so 0x8001380c..0x800147f4 is one module; nothing marks a boundary before
 * Tasker.c. Name invented. */
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

#define ABS( x ) ( ( x ) >= 0 ? ( x ) : -( x ) )

/* Words of the random generator state (g_randomState, the seed s_randomSeed). name: invented */
#define RANDOM_STATE_WORDS 6

/* Frames Start+Select must be held before Sys_ResetComboCheck resets. name: invented */
#define RESET_HOLD_FRAMES 120

/* Sys_PollPad auto-repeat of a held pad state. names: invented */
#define PAD_REPEAT_DELAY 20    /* g_padHoldFrames saturates here; repeats start once it is reached */
#define PAD_REPEAT_INTERVAL 11 /* frames between repeats while held */

/* Sys_DrawBossBackdrop: Sys_GameDisplayTask's TASK.state words and the brightness range it ramps
 * through. names: invented */
#define BACKDROP_STATE_LEVEL 2   /* green level of the gradient */
#define BACKDROP_STATE_FALLING 3 /* nonzero while the level ramps down */
#define BACKDROP_LEVEL_MAX 96
#define BACKDROP_LEVEL_MIN 16

/* Level sprite pages (PAGE_SET_SPRITES) Sys_ReserveVRAMSlots locks on the Queen levels.
 * names: invented */
#define QUEEN_PAGE_A 1
#define QUEEN_PAGE_B 2
#define QUEEN_PAGE_C 4
#define QUEEN_PAGE_D 5

/* Sys_FatalError's screen: a dark red background and the debug font's text window.
 * names: invented */
#define ERROR_BACKGROUND_RED 64
#define ERROR_FONT_X 960 /* FntLoad: the font's texture page in VRAM */
#define ERROR_FONT_Y 256
#define ERROR_TEXT_X 16
#define ERROR_TEXT_Y 16
#define ERROR_TEXT_HEIGHT 200
#define ERROR_TEXT_LENGTH 512 /* FntOpen's character buffer */

/* Draw environments' texture page: the 8-bit page at x 512 (GetTPage x). name: invented */
#define DRAW_ENV_TPAGE_X 512

/* Runtime 3.3 functions called without an SDK prototype. */
extern void SetBlockFill( void * );

/* Other modules. */
extern void Sys_LevelParseMap( levelMap_t *level );
extern void Sys_LightsTask( TASK * );

/* Globals of other modules. */
/* name: inferred from use: Sys_Random steps it, Sys_RandomSeed loads it */
extern unsigned int    g_randomState[RANDOM_STATE_WORDS];
extern unsigned short *g_tab;
extern int             g_bossLevel;
/* name: runtime evidence, analysis/runtime/globals.json: DpqColor depth of the level-start fade, 0x1000 down to 0 by 0x100 a frame */
extern long g_fadeDepth;
/* Observable counter used by the heap-overflow diagnostic. */
extern volatile int g_cdReadCount;
extern int          g_displayMode;
/* name: runtime evidence, analysis/runtime/globals.json: the two display buffers g_display alternates between */
extern displayBuffer_t *g_displayBuffers;
/* Drawing environments for the two buffers; names invented. */
extern DRAWENV g_drawEnvA;
extern DRAWENV g_drawEnvB;

/* Functions of this module referenced before their definitions. */
void Sys_DrawBossBackdrop( TASK *task );
void Sys_GameDisplayTask( TASK *task );
void Sys_VRAMSlotsClearUsers( void );
void Sys_ReleaseVRAMSlots( void );
void Sys_PlayLevel( void );
void Sys_RunFrontend( void );
void Sys_InstallVBlankHandler( void );
void Sys_ClearVRAM( unsigned char red, unsigned char green, unsigned char blue );
void Sys_InitCD( void );
void Sys_InitDisplayBuffers( void );
void Sys_StartLevel( int level );
void Sys_CDSeekFile( int n );
int Sys_CDReadCallback( void );
void Sys_VBlankHandler( void );

short g_idx = 0;

/* Stack size the startup module (SN 2MBYTE.OBJ) reads before it calls main. */
unsigned long _stacksize = STACK_SIZE;

/* The four d-pad bits (up, down, left, right). No code in the image reads them.
 * name invented, from its values */
short g_dpadButtons[] = { PADLup, PADLdown, PADLleft, PADLright };

/* Pad bit of each player action; Sys_SetButtonLayout stores one set per control method.
 * names: runtime evidence, analysis/runtime/globals.json (button presses at level
 * start: use/look-down, weapon change/look-up, fire, strafe, run, look, grenade). */
short g_buttonUse = PADRup;
short g_buttonWeapon = PADRdown;
short g_buttonFire = PADRleft;
/* identity-TODO: g_8cd8a..g_8cd90 sit among the action masks but Sys_SetButtonLayout never
 * stores them and no jal, pointer word, lui/lo pair or $gp operand in retail reaches it; no runtime change (analysis/runtime/globals.json). */
short g_8cd8a = PADstart;
short g_8cd8c = 0;
short g_8cd8e = 0;
short g_8cd90 = PADselect;
short g_buttonStrafeRight = PADR1;
short g_buttonRun = PADR2;
short g_buttonStrafeLeft = PADL1;
short g_buttonLook = PADL2;
short g_buttonGrenade = PADRright;

/* name: inferred from use: the origin, a point Geo_PointHeadingXZ and Actor_LineOfSight measure from */
SVECTOR g_zeroVector = { 0, 0, 0, 0 };

short g_actionIndex = -1;
/* name: inferred from use: Menu_CheckPassword sets its bits from the cheat codes; shots skip ammo use under 0x20, Timer_Start does nothing under 4 */
unsigned int g_cheatFlags = CHEAT_FLAGS_DEFAULT;

/* name: runtime evidence, analysis/runtime/globals.json: frame at which the Start+Select reset combination was last released */
int g_resetHoldStart = 0;

short g_heapSnapshotCurrent = -1;

/* Strings of Heap_Alloc and Sys_FatalError. Each array's extent includes its alignment
 * padding; each is placed by its own address. identity-TODO: possibly string
 * literals in the original; named here because tools/section_layout.py bounds the
 * start of .rdata by the first datum a unit map names (0x800101e0). */
#define HEAP_OVERFLOW_SIZE 16
#define ERROR_TITLE_SIZE 20
#define ERROR_FORMAT_SIZE 4
const char s_heapOverflow[HEAP_OVERFLOW_SIZE] = "Out of memory";
const char s_errorTitle[ERROR_TITLE_SIZE] = "INTERNAL ERROR: ";
const char s_errorFormat[ERROR_FORMAT_SIZE] = "%s\n";

/* CLUT runs reserved at boot (Sys_InitCLUTRows); the rows are allocated then. */
clutSet_t g_clutSets[CLUT_SET_COUNT] = { { 0, 480, 0, 32, 0 }, { 256, 480, 1, 32, 0 } };

/* Texture page grids reserved at boot (Sys_InitTexturePages); the pages are allocated then. */
texturePageSet_t g_pageSets[PAGE_SET_COUNT] = { { 320, 0, 512, 512, 128, 128, 0, 12, 0 },
                                                { 512, 0, 1024, 512, 256, 256, 0, 5, 0 },
                                                { 640, 256, 896, 512, 256, 256, 1, 4, 0 },
                                                { 896, 256, 1024, 512, 128, 64, 0, 8, 0 } };

/* Six-word seed Sys_InitRandom loads into g_randomState. name: inferred from that */
static unsigned int s_randomSeed[RANDOM_STATE_WORDS] = { 0xabcd0123, 0x10, 0xf0ad0000, 0xda0e0, 1, 0x1aa01001 };

/* Every file the game loads from CD, by index; Sys_LoadFile fills the rest of an entry. */
heapFile_t g_fileTable[FILE_TABLE_COUNT] = {
    { "\\GFX\\OBJ3D.BND;1" },         { "\\GFX\\PULSE.BND;1" },      { "\\GFX\\MM9.BND;1" },
    { "\\GFX\\SHOTGUN.BND;1" },       { "\\GFX\\FLAME.BND;1" },      { "\\GFX\\SMART.BND;1" },
    { "\\NME\\DOG.BND;1" },           { "\\NME\\HUGGER.BND;1" },     { "\\NME\\WAR.BND;1" },
    { "\\NME\\EGGS.BND;1" },          { "\\NME\\HANDLER.BND;1" },    { "\\NME\\SYNTH.BND;1" },
    { "\\NME\\GUARD.BND;1" },         { "\\NME\\SOLDIER.BND;1" },    { "\\NME\\BURSTER.BND;1" },
    { "\\NME\\BAMBI.BND;1" },         { "\\NME\\FINGERS.BND;1" },    { "\\NME\\WARCEIL.BND;1" },
    { "\\NME\\QUEEN.BND;1" },         { "\\NME\\DOGCEIL.BND;1" },    { "\\NME\\COLONIST.BND;1" },
    { "\\SECT11\\111GFX.BIN;1" },     { "\\SECT11\\L111LEV.MAP;1" }, { "\\SECT11\\112GFX.BIN;1" },
    { "\\SECT11\\L112LEV.MAP;1" },    { "\\SECT11\\113GFX.BIN;1" },  { "\\SECT11\\L113LEV.MAP;1" },
    { "\\SECT11\\114GFX.BIN;1" },     { "\\SECT11\\L114LEV.MAP;1" }, { "\\SECT11\\115GFX.BIN;1" },
    { "\\SECT11\\L115LEV.MAP;1" },    { "\\SECT11\\121GFX.BIN;1" },  { "\\SECT11\\L121LEV.MAP;1" },
    { "\\SECT11\\122GFX.BIN;1" },     { "\\SECT11\\L122LEV.MAP;1" }, { "\\SECT11\\123GFX.BIN;1" },
    { "\\SECT11\\L123LEV.MAP;1" },    { "\\SECT11\\131GFX.BIN;1" },  { "\\SECT11\\L131LEV.MAP;1" },
    { "\\SECT11\\132GFX.BIN;1" },     { "\\SECT11\\L132LEV.MAP;1" }, { "\\SECT12\\141GFX.BIN;1" },
    { "\\SECT12\\L141LEV.MAP;1" },    { "\\SECT12\\151GFX.BIN;1" },  { "\\SECT12\\L151LEV.MAP;1" },
    { "\\SECT12\\152GFX.BIN;1" },     { "\\SECT12\\L152LEV.MAP;1" }, { "\\SECT12\\153GFX.BIN;1" },
    { "\\SECT12\\L153LEV.MAP;1" },    { "\\SECT12\\154GFX.BIN;1" },  { "\\SECT12\\L154LEV.MAP;1" },
    { "\\SECT12\\155GFX.BIN;1" },     { "\\SECT12\\L155LEV.MAP;1" }, { "\\SECT12\\161GFX.BIN;1" },
    { "\\SECT12\\L161LEV.MAP;1" },    { "\\SECT12\\162GFX.BIN;1" },  { "\\SECT12\\L162LEV.MAP;1" },
    { "\\SECT21\\211GFX.BIN;1" },     { "\\SECT21\\L211LEV.MAP;1" }, { "\\SECT21\\212GFX.BIN;1" },
    { "\\SECT21\\L212LEV.MAP;1" },    { "\\SECT21\\213GFX.BIN;1" },  { "\\SECT21\\L213LEV.MAP;1" },
    { "\\SECT21\\222GFX.BIN;1" },     { "\\SECT21\\L222LEV.MAP;1" }, { "\\SECT21\\231GFX.BIN;1" },
    { "\\SECT21\\L231LEV.MAP;1" },    { "\\SECT21\\232GFX.BIN;1" },  { "\\SECT21\\L232LEV.MAP;1" },
    { "\\SECT22\\242GFX.BIN;1" },     { "\\SECT22\\L242LEV.MAP;1" }, { "\\SECT22\\243GFX.BIN;1" },
    { "\\SECT22\\L243LEV.MAP;1" },    { "\\SECT22\\262GFX.BIN;1" },  { "\\SECT22\\L262LEV.MAP;1" },
    { "\\SECT22\\263GFX.BIN;1" },     { "\\SECT22\\L263LEV.MAP;1" }, { "\\SECT31\\311GFX.BIN;1" },
    { "\\SECT31\\L311LEV.MAP;1" },    { "\\SECT31\\321GFX.BIN;1" },  { "\\SECT31\\L321LEV.MAP;1" },
    { "\\SECT31\\331GFX.BIN;1" },     { "\\SECT31\\L331LEV.MAP;1" }, { "\\SECT31\\322GFX.BIN;1" },
    { "\\SECT31\\L322LEV.MAP;1" },    { "\\SECT32\\351GFX.BIN;1" },  { "\\SECT32\\L351LEV.MAP;1" },
    { "\\SECT32\\352GFX.BIN;1" },     { "\\SECT32\\L352LEV.MAP;1" }, { "\\SECT31\\323GFX.BIN;1" },
    { "\\SECT31\\L323LEV.MAP;1" },    { "\\SECT32\\361GFX.BIN;1" },  { "\\SECT32\\L361LEV.MAP;1" },
    { "\\SECT31\\324GFX.BIN;1" },     { "\\SECT31\\L324LEV.MAP;1" }, { "\\SECT32\\371GFX.BIN;1" },
    { "\\SECT32\\L371LEV.MAP;1" },    { "\\SECT32\\353GFX.BIN;1" },  { "\\SECT32\\L353LEV.MAP;1" },
    { "\\SECT31\\325GFX.BIN;1" },     { "\\SECT31\\L325LEV.MAP;1" }, { "\\SECT32\\381GFX.BIN;1" },
    { "\\SECT32\\L381LEV.MAP;1" },    { "\\SECT32\\391GFX.BIN;1" },  { "\\SECT32\\L391LEV.MAP;1" },
    { "\\GFX\\PICKGFX.BND;1" },       { "\\GFX\\PICKMOD.BND;1" },    { "\\LANGUAGE\\PNL0GFXU.BND;1" },
    { "\\LANGUAGE\\PNL1GFXU.BND;1" }, { "\\GFX\\LOGOSGFX.BND;1" },   { "\\GFX\\FONT1GFX.BND;1" },
    { "\\GFX\\EXPLGFX.BND;1" },       { "\\SFX\\PERMSFX.BIN;1" },    { "\\SFX\\111SFX.BIN;1" },
    { "\\SFX\\112SFX.BIN;1" },        { "\\SFX\\113SFX.BIN;1" },     { "\\SFX\\122SFX.BIN;1" },
    { "\\SFX\\131SFX.BIN;1" },        { "\\SFX\\114SFX.BIN;1" },     { "\\SFX\\141SFX.BIN;1" },
    { "\\SFX\\115SFX.BIN;1" },        { "\\SFX\\154SFX.BIN;1" },     { "\\SFX\\155SFX.BIN;1" },
    { "\\SFX\\161SFX.BIN;1" },        { "\\SFX\\162SFX.BIN;1" },     { "\\SFX\\211SFX.BIN;1" },
    { "\\SFX\\212SFX.BIN;1" },        { "\\SFX\\213SFX.BIN;1" },     { "\\SFX\\222SFX.BIN;1" },
    { "\\SFX\\242SFX.BIN;1" },        { "\\SFX2\\231SFX.BIN;1" },    { "\\SFX2\\232SFX.BIN;1" },
    { "\\SFX2\\243SFX.BIN;1" },       { "\\SFX2\\262SFX.BIN;1" },    { "\\SFX2\\263SFX.BIN;1" },
    { "\\SFX2\\311SFX.BIN;1" },       { "\\SFX2\\321SFX.BIN;1" },    { "\\SFX2\\331SFX.BIN;1" },
    { "\\SFX2\\351SFX.BIN;1" },       { "\\SFX2\\352SFX.BIN;1" },    { "\\SFX2\\371SFX.BIN;1" },
    { "\\SFX2\\353SFX.BIN;1" },       { "\\SFX2\\381SFX.BIN;1" },    { "\\SFX2\\391SFX.BIN;1" },
    { "\\SFX2\\STARTSFX.BIN;1" },     { "\\STR\\INTRO.STR;1" },      { "\\STR\\PROBE.STR;1" },
    { "\\STR\\ACCLAIM.STR;1" },       { "\\STR\\DEATH1.STR;1" },     { "\\STR\\DEATH2.STR;1" },
    { "\\STR\\DEATH3.STR;1" },        { "\\STR\\DEATH4.STR;1" },     { "\\STR\\DEATH5.STR;1" },
    { "\\STR\\DEATH6.STR;1" },        { "\\STR\\LEGAL.STR;1" },      { "\\STR\\S1_2.STR;1" },
    { "\\STR\\S2_3.STR;1" },          { "\\STR\\GAMEOVER.STR;1" },   { "\\STR\\CREDITS.STR;1" },
    { "\\STR\\FOX.STR;1" },           { "\\GFX\\COLONY.BS;1" },      { "\\GFX\\PRISHOLD.BS;1" },
    { "\\GFX\\BONESHIP.BS;1" },       { "\\GFX\\ICONS.IFF;1" },      { "\\GFX\\PIRATE.IFF;1" },
    { "\\LANGUAGE\\MISSIONU.TXT;1" }, { "\\GFX\\CREDITS.TXT;1" },    { "\\GFX\\DEMO111.BIN;1" },
    { "\\GFX\\DEMO211.BIN;1" },       { "\\GFX\\DEMO311.BIN;1" },    { "\\GFX\\CONTROL.BND;1" }
};

/*
==================
Sys_SetButtonLayout
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_SetButtonLayout( short mode )
{
    switch ( mode ) {
    case CONTROL_METHOD_FIRE_CROSS:
        g_buttonWeapon = PADRup;
        g_buttonUse = PADRright;
        g_buttonFire = PADRdown;
        g_buttonGrenade = PADRleft;
        g_buttonStrafeLeft = PADL1;
        g_buttonStrafeRight = PADR1;
        g_buttonLook = PADL2;
        g_buttonRun = PADR2;
        break;
    case CONTROL_METHOD_FIRE_TRIANGLE:
        g_buttonWeapon = PADRright;
        g_buttonUse = PADRleft;
        g_buttonFire = PADRup;
        g_buttonGrenade = PADRdown;
        g_buttonStrafeLeft = PADL2;
        g_buttonStrafeRight = PADR2;
        g_buttonLook = PADL1;
        g_buttonRun = PADR1;
        break;
    case CONTROL_METHOD_FIRE_CIRCLE:
        g_buttonWeapon = PADRleft;
        g_buttonUse = PADRdown;
        g_buttonFire = PADRright;
        g_buttonGrenade = PADRup;
        g_buttonStrafeLeft = PADL2;
        g_buttonStrafeRight = PADR2;
        g_buttonLook = PADL1;
        g_buttonRun = PADR1;
        break;
    default:
        g_buttonWeapon = PADRdown;
        g_buttonUse = PADRup;
        g_buttonFire = PADRleft;
        g_buttonGrenade = PADRright;
        g_buttonStrafeLeft = PADL1;
        g_buttonStrafeRight = PADR1;
        g_buttonLook = PADL2;
        g_buttonRun = PADR2;
        break;
    }
}

/*
==================
Sys_ResetComboCheck
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
short Sys_ResetComboCheck( int buttons )
{
    if ( ( buttons & PADstart ) && ( buttons & PADselect ) ) {
        if ( ABS( g_frameCount - g_resetHoldStart ) > RESET_HOLD_FRAMES ) {
            g_gameFlags |= GAME_FLAG_RESET;
            return -1;
        }
    } else {
        g_resetHoldStart = g_frameCount;
    }
    return 0;
}

/*
==================
Sys_RandomSeed
==================
*/
/* name: inferred from its body, which copies a six-word seed into the random state */
void Sys_RandomSeed( const unsigned int *source )
{
    short i;
    for ( i = 0; i < RANDOM_STATE_WORDS; ++i )
        g_randomState[i] = source[i];
}

/*
==================
Sys_InitRandom
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_InitRandom( void )
{
    Sys_RandomSeed( s_randomSeed );
}

/*
==================
Sys_Random
==================
*/
/* name: inferred from its body, which steps the generator state and returns the new value */
unsigned int Sys_Random( void )
{
    short        i;
    unsigned int oldValue = g_randomState[RANDOM_STATE_WORDS - 2];
    unsigned int total = oldValue + g_randomState[1];
    unsigned int incremented;
    if ( total < oldValue )
        total++;
    incremented = g_randomState[RANDOM_STATE_WORDS - 1] + 1;
    g_randomState[0] = total + incremented;

    for ( i = 0; i < RANDOM_STATE_WORDS - 1; i++ )
        g_randomState[i + 1] = g_randomState[i];
    return g_randomState[0];
}

/*
==================
Sys_MenuDisplayTask
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_MenuDisplayTask( TASK *task )
{
    displayBuffer_t *display;
    if ( task->state[0] == 1 ) {
        g_graphicsBusy = -1;
        while ( g_graphicsBusy != 0 ) {
        }
        DrawSync( 0 );
        PutDispEnv( &g_display->disp );
        DrawOTag( &g_renderBuffer[OT_LENGTH - 1] );
        display = g_display == g_renderWorkspace ? g_displayDefault : g_renderWorkspace;
        g_display = display;
        g_renderBuffer = display->ot;
        g_spriteWorkspace = display->prims;
        ClearOTagR( g_renderBuffer, OT_LENGTH );
        AddPrim( &g_renderBuffer[OT_LENGTH - 1], &g_display->draw );
    }
    g_nextTask = task->Next;
}

/*
==================
Sys_DrawBossBackdrop

Pulsing green backdrop: a full-screen semi-transparent gradient whose
brightness ramps between BACKDROP_LEVEL_MIN and BACKDROP_LEVEL_MAX.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_DrawBossBackdrop( TASK *task )
{
    CVECTOR  top;
    CVECTOR  bottom;
    POLY_G4 *poly;

    if ( g_bossLevel == 0 )
        return;
    top.r = 0;
    top.g = task->state[BACKDROP_STATE_LEVEL];
    top.b = task->state[BACKDROP_STATE_LEVEL] >> 1;
    bottom.r = 0;
    bottom.g = task->state[BACKDROP_STATE_LEVEL];
    bottom.b = task->state[BACKDROP_STATE_LEVEL] >> 1;
    DpqColor( &top, g_fadeDepth, &top );
    DpqColor( &bottom, g_fadeDepth, &bottom );
    poly = g_spriteWorkspace;
    setRGB0( poly, 0, 0, 0 );
    setRGB1( poly, 0, 0, 0 );
    *(u_long *)&poly->r2 = *(u_long *)&top;
    *(u_long *)&poly->r3 = *(u_long *)&bottom;
    setPolyG4( poly );
    setSemiTrans( poly, 1 );
    setXY4( poly, 0, 0, SCREEN_WIDTH - 1, 0, 0, SCREEN_HEIGHT - 1, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1 );
    addPrim( g_renderBuffer, poly );
    g_spriteWorkspace = poly + 1;
    if ( task->state[BACKDROP_STATE_FALLING] == 0 ) {
        if ( ++task->state[BACKDROP_STATE_LEVEL] > BACKDROP_LEVEL_MAX ) {
            task->state[BACKDROP_STATE_LEVEL] = BACKDROP_LEVEL_MAX;
            task->state[BACKDROP_STATE_FALLING] = 1;
        }
    } else {
        if ( (int)--task->state[BACKDROP_STATE_LEVEL] < BACKDROP_LEVEL_MIN ) {
            task->state[BACKDROP_STATE_LEVEL] = BACKDROP_LEVEL_MIN;
            task->state[BACKDROP_STATE_FALLING] = 0;
        }
    }
}

/*
==================
Sys_GameDisplayTask
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_GameDisplayTask( TASK *task )
{
    displayBuffer_t *display;
    Sys_DrawBossBackdrop( task );
    Tex_FlushUploadQueue();
    task->state[1] = g_frameCount - task->state[0];
    g_graphicsBusy = -1;
    if ( g_graphicsBusy != 0 ) {
        while ( g_graphicsBusy != 0 ) {
        }
    }
    if ( task->state[1] == 0 ) {
        g_graphicsBusy = -1;
        while ( g_graphicsBusy != 0 ) {
        }
    }
    task->state[0] = g_frameCount;
    DrawSync( 0 );
    PutDispEnv( &g_display->disp );
    DrawOTag( &g_renderBuffer[OT_LENGTH - 1] );
    display = g_display == g_renderWorkspace ? g_displayDefault : g_renderWorkspace;
    g_display = display;
    g_renderBuffer = display->ot;
    g_spriteWorkspace = display->prims;
    ClearOTagR( g_renderBuffer, OT_LENGTH );
    AddPrim( &g_renderBuffer[OT_LENGTH - 1], &g_display->draw );
    g_nextTask = task->Next;
}

/*
==================
Sys_ReleaseVRAMSlots
==================
*/
/* name: inferred from its body, which frees the unlocked slots of the first list and every slot of the second */
void Sys_ReleaseVRAMSlots( void )
{
    short          count;
    texturePage_t *slot;

    count = g_pageSets[PAGE_SET_SPRITES].count;
    slot = g_pageSets[PAGE_SET_SPRITES].pages;
    while ( count != 0 ) {
        if ( ( slot->reserved & 1 ) == 0 ) {
            slot->users = 0;
            slot->pixels = 0;
        }
        count--;
        slot++;
    }

    count = g_pageSets[PAGE_SET_SMALL_SPRITES].count;
    slot = g_pageSets[PAGE_SET_SMALL_SPRITES].pages;
    while ( count != 0 ) {
        slot->reserved = 0;
        slot->users = 0;
        slot->pixels = 0;
        count--;
        slot++;
    }
}

/*
==================
Sys_VRAMSlotsClearUsers
==================
*/
/* name: inferred from its body, which clears the users of every unlocked slot */
void Sys_VRAMSlotsClearUsers( void )
{
    short          count;
    texturePage_t *slot;

    count = g_pageSets[PAGE_SET_SPRITES].count;
    slot = g_pageSets[PAGE_SET_SPRITES].pages;
    while ( count != 0 ) {
        if ( ( slot->reserved & 1 ) == 0 )
            slot->users = 0;
        count--;
        slot++;
    }

    count = g_pageSets[PAGE_SET_SMALL_SPRITES].count;
    slot = g_pageSets[PAGE_SET_SMALL_SPRITES].pages;
    while ( count != 0 ) {
        if ( ( slot->reserved & 1 ) == 0 )
            slot->users = 0;
        count--;
        slot++;
    }
}

/*
==================
Sys_UnloadLevel
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_UnloadLevel( void )
{
    short mode = g_actionIndex;
    if ( mode != -1 ) {
        Heap_Release( mode );
        Snd_FreeBank( -1 );
        g_actionIndex = -1;
    }
}

/*
==================
Sys_LoadLevel
==================
*/
/* name: inferred from its body, which marks the heap and loads the level's sound bank, sprites, map, models and (demo mode) demo input; DOS port: string-only match at 0x10640, which uses the same \GFX\DEMO111.BIN..DEMO311.BIN names */
void Sys_LoadLevel( void )
{
    heapFile_t *file;

    Sys_UnloadLevel();
    g_actionIndex = Heap_Mark();
    AllocUnpackBuffers();
    Snd_Init();
    if ( g_level >= LEVEL_311_TUNNELS )
        Sprite_LoadPanelSpritesAlternate();
    Snd_SelectLevelBank( g_level );
    Sprite_LoadLevelPanelSprites();
    Menu_AdvanceMessageBox();
    LoadLevelMapFile( &g_levelMap );
    Sys_LevelParseMap( &g_levelMap );
    Door_PrepareModels();
    Lift_PrepareModels();
    Sprite_LoadLevelSprites();
    if ( g_gameFlags & GAME_FLAG_DEMO ) {
        file = 0;
        switch ( g_level ) {
        case LEVEL_111_ENTRANCE:
            file = &g_fileTable[FILE_DEMO111_BIN];
            break;
        case LEVEL_211_LIVING_QUARTERS:
            file = &g_fileTable[FILE_DEMO211_BIN];
            break;
        case LEVEL_311_TUNNELS:
            file = &g_fileTable[FILE_DEMO311_BIN];
            break;
        }
        if ( file != 0 ) {
            file->buffer = 0;
            file->loadedSize = 0;
            Sys_LoadFile( file );
            g_tab = file->buffer;
        }
    }
    Menu_AdvanceMessageBox();
}

/*
==================
Sys_ReserveVRAMSlots
==================
*/
/* name: inferred from its body, which locks VRAM slots 1, 2, 4 and 5 on levels 11, 21 and 34 and frees them on every other level */
void Sys_ReserveVRAMSlots( void )
{
    texturePage_t *slots = g_pageSets[PAGE_SET_SPRITES].pages;
    int            level = g_level;
    texturePage_t *slot1 = slots + QUEEN_PAGE_A;
    texturePage_t *slot2 = slots + QUEEN_PAGE_B;
    texturePage_t *slot4 = slots + QUEEN_PAGE_C;
    texturePage_t *slot5 = slots + QUEEN_PAGE_D;

    if ( level == LEVEL_162_QUEENS_LAIR || level == LEVEL_263_QUEENS_LAIR || level == LEVEL_391_QUEENS_LAIR ) {
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

/*
==================
Sys_StartLevel

Sets up a level: loads it, resets every subsystem and creates the level tasks.
main passes the level; the body reads g_level.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_StartLevel( int level )
{
    TASK *last;

    SetRCnt( RCntCNT1, HBLANK_COUNTER_TARGET, RCntMdNOINTR );
    StartRCnt( RCntCNT1 );
    Sys_InitRandom();
    g_bossLevel = ( g_level == LEVEL_162_QUEENS_LAIR || g_level == LEVEL_263_QUEENS_LAIR || g_level == LEVEL_391_QUEENS_LAIR );
    Player_BindWeaponSpriteSlot();
    Sys_ReserveVRAMSlots();
    Sys_LoadLevel();
    g_viewPosition.vx = 0;
    g_viewPosition.vy = 0;
    g_viewPosition.vz = 0;
    g_viewPitch.vx = 0;
    g_viewPitch.vy = 0;
    g_viewPitch.vz = 0;
    g_viewRotation.vx = 0;
    g_viewRotation.vy = 0;
    g_viewRotation.vz = 0;
    Sys_ReleaseVRAMSlots();
    TaskerInit();
    ResetLevelCells( &g_levelMap );
    Tex_ResetUploadQueue();
    Hud_ResetMessages();
    Particle_InitPool();
    Debris_InitPanelPool();
    ClearFlowField();
    Actor_InitPool();
    Light_Load();
    Trigger_LoadLevelTriggers();
    Lift_Spawn();
    Door_Init();
    Object_Init();
    Object_InitLevelObjects();
    Actor_Init();
    Command_StartSequencer();
    Timer_Start( g_level );
    Menu_AdvanceMessageBox();
    TaskCreate( TASK_PRIORITY_LIGHTS, Sys_LightsTask );
    TaskCreate( TASK_PRIORITY_PLAYER, Player_TaskStart );
    TaskCreate( TASK_PRIORITY_VIEW, ViewRenderTask );
    TaskCreate( TASK_PRIORITY_PICKUPS, Object_PickupsTask );
    TaskCreate( TASK_PRIORITY_PARTICLES, Particle_Task );
    TaskCreate( TASK_PRIORITY_ACTORS_DRAW, Actor_DrawTask );
    TaskCreate( TASK_PRIORITY_COMMANDS, Command_SequencerTask );
    last = TaskCreate( TASK_PRIORITY_DISPLAY, Sys_GameDisplayTask );
    last->state[0] = 0;
    last->state[BACKDROP_STATE_LEVEL] = 0;
    last->state[BACKDROP_STATE_FALLING] = 0;
}

/*
==================
Demo_ReadInput
==================
*/
/* name: inferred from its body, which returns the next recorded pad word of the demo (900 at most) */
int Demo_ReadInput( void )
{
    int   r = DEMO_INPUT_END;
    short i = g_idx;
    if ( i < DEMO_INPUT_LENGTH ) {
        g_idx = i + 1;
        r = g_tab[i];
    }
    return r;
}

/*
==================
Sys_PlayLevel

One play session of the current level: runs frames until the level is left
(GAME_FLAG_LEVEL_COMPLETE, GAME_FLAG_RESET, GAME_FLAG_DEATH_DONE) or a demo ends (GAME_FLAG_RESET),
fading in and out. In demo mode (GAME_FLAG_DEMO) input comes from Demo_ReadInput and the difficulty
is forced to DIFFICULTY_ACID_REIGN for the run.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_PlayLevel( void )
{
    short difficulty = 0;

    if ( g_gameFlags & GAME_FLAG_DEMO ) {
        difficulty = Menu_GetDifficulty();
        Menu_SetDifficulty( DIFFICULTY_ACID_REIGN );
        g_gameFlags &= ~GAME_FLAG_HARD;
        Sys_SetButtonLayout( 0 );
        g_idx = 0;
    }
    g_fadeDepth = FADE_DEPTH_FULL;
    g_gameFlags |= GAME_FLAG_FADE;
    Music_PlayLevel( g_level );
    g_graphicsBusy = -1;
    g_padPressed = 0;
    g_padPrevious = 0;
    g_padHeld = 0;
    if ( g_graphicsBusy != 0 ) {
        while ( g_graphicsBusy != 0 ) {
        }
    }
    if ( !( g_gameFlags & ( GAME_FLAG_LEVEL_COMPLETE | GAME_FLAG_RESET | GAME_FLAG_DEATH_DONE ) ) ) {
        do {
            Music_Poll();
            if ( g_gameFlags & GAME_FLAG_DEMO ) {
                g_padPrevious = g_padHeld;
                g_padHeld = Demo_ReadInput();
                g_padPressed = ~g_padPrevious & g_padHeld;
                if ( g_padHeld == DEMO_INPUT_END || PadRead( 0 ) )
                    g_gameFlags |= GAME_FLAG_RESET;
                if ( g_gameFlags & GAME_FLAG_RESET )
                    break;
            } else {
                g_padPrevious = g_padHeld;
                g_padHeld = PadRead( 0 );
                g_padPressed = ~g_padPrevious & g_padHeld;
                Sys_ResetComboCheck( g_padHeld );
            }
            if ( g_gameFlags & GAME_FLAG_FADE ) {
                g_fadeDepth -= FADE_DEPTH_STEP;
                if ( g_fadeDepth < 0 )
                    g_fadeDepth = 0;
            }
            TaskDispatch();
            if ( ( g_gameFlags & GAME_FLAG_FADE ) && g_fadeDepth == 0 )
                g_gameFlags &= ~GAME_FLAG_FADE;
            Sys_VRAMSlotsClearUsers();
            if ( g_gameFlags & GAME_FLAG_PAUSE ) {
                Pause_Run();
                g_gameFlags &= ~GAME_FLAG_PAUSE;
            }
        } while ( !( g_gameFlags & GAME_FLAG_RESET )
                  && !( g_gameFlags & ( GAME_FLAG_LEVEL_COMPLETE | GAME_FLAG_RESET | GAME_FLAG_DEATH_DONE ) ) );
    }
    Sys_CDSeekFile( FILE_COLONY_BS );
    g_graphicsBusy = -1;
    while ( g_graphicsBusy != 0 ) {
    }
    g_gameFlags |= GAME_FLAG_FADE;
    while ( g_gameFlags & GAME_FLAG_FADE ) {
        g_fadeDepth += FADE_DEPTH_STEP;
        TaskDispatch();
        if ( g_fadeDepth > FADE_DEPTH_FULL )
            g_gameFlags &= ~GAME_FLAG_FADE;
    }
    if ( g_gameFlags & GAME_FLAG_DEMO )
        Menu_SetDifficulty( difficulty );
}

/*
==================
Sys_PollPad

Input-poll state step.
==================
*/
/* name: inferred from its body, which reads the pad and derives pressed, held and auto-repeat bits */
void Sys_PollPad( void )
{
    short state = g_padHoldFrames;
    int   current;
    if ( state == 0 ) {
        g_padRepeat = g_padPressed;
    } else if ( state == PAD_REPEAT_DELAY && g_padRepeatClock >= PAD_REPEAT_INTERVAL ) {
        g_padRepeatClock = 0;
        g_padRepeat = g_padHeld;
    } else {
        g_padRepeat = 0;
    }
    g_padPrevious = g_padHeld;
    current = PadRead( 0 );
    g_padHeld = current;
    g_padPressed = ( ~g_padPrevious ) & current;
    if ( g_padPrevious == current ) {
        g_padHoldFrames++;
        if ( g_padHoldFrames > PAD_REPEAT_DELAY )
            g_padHoldFrames = PAD_REPEAT_DELAY;
        g_padRepeatClock++;
    } else {
        g_padHoldFrames = 0;
        g_padRepeatClock = 0;
    }
}

/*
==================
Sys_ResetPad
==================
*/
/* name: inferred from its body, which clears the auto-repeat state */
void Sys_ResetPad( void )
{
    g_padRepeat = 0;
    g_padRepeatClock = 0;
    g_padHoldFrames = 0;
}

/*
==================
Sys_RunFrontend
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_RunFrontend( void )
{
    TaskerInit();
    Menu_StartTitleSequence();
    g_gameFlags = 0;
    Sys_ResetPad();
    while ( ( g_gameFlags & GAME_FLAG_MENU_DONE ) == 0 ) {
        Sys_PollPad();
        TaskDispatch();
    }
    TaskerInit();
}

/*
==================
main

Boot, then run the front end and levels for ever. GCC 2.5.7 emits the call to
__main (the start of SN's 2MBYTE.OBJ at 0x800588bc) at the top of main itself.
==================
*/
int main( void )
{
    short slot;
    short next;

    Sys_InitRandom();
    ResetGraph( 0 );
    SetGraphDebug( 0 );
    SetDispMask( 0 );
    InitGeom();
    SetGeomOffset( SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 );
    SetGeomScreen( PROJECTION_DISTANCE );
    SetVideoMode( 0 );
    Sys_InstallVBlankHandler();
    PadInit( 0 );
    Sys_ClearVRAM( 0, 0, 0 );
    Heap_Init();
    Sys_InitCD();
    Card_Init();
    Sys_InitDisplayBuffers();
    Music_Enable();
    Snd_Enable();
    Snd_Init();
    Music_Init();
    Sys_InitCLUTRows( &g_clutSets[CLUT_SET_SPRITES] );
    Sys_InitCLUTRows( &g_clutSets[CLUT_SET_SPRITE_SHEETS] );
    Sys_InitTexturePages( &g_pageSets[PAGE_SET_SPRITES] );
    Sys_InitTexturePages( &g_pageSets[PAGE_SET_SMALL_SPRITES] );
    Sys_InitTexturePages( &g_pageSets[PAGE_SET_MENU] );
    Sys_InitTexturePages( &g_pageSets[PAGE_SET_SPRITE_SHEETS] );
    TaskerAlloc();
    Particle_PoolAlloc();
    Debris_PanelPoolAlloc();
    Actor_PoolAlloc();
    Tex_UploadPoolAlloc();
    Lift_Alloc();
    Object_LoadPickupModels();
    Object_LoadLevelObjectModels();
    Door_Alloc();
    Sprite_ReserveMenuTextures();
    SetDispMask( 1 );
    g_level = 0;
    for ( ;; ) {
        Player_Reset();
        do {
            slot = Heap_Mark();
            Sys_RunFrontend();
            Heap_Release( slot );
        } while ( g_level == -1 );
        Sprite_LoadSets();
        Hud_InitFontWidths();
        g_gameFlags &= ~GAME_FLAG_RESTORED;
        while ( g_level < LEVEL_COUNT && !( g_gameFlags & ( GAME_FLAG_RESET | GAME_FLAG_DEATH_DONE ) ) ) {
            Password_StoreLevel( g_level );
            Menu_ShowLoadingScreen( g_level );
            Sys_StartLevel( g_level );
            Menu_ShowContinuePrompt( g_level );
            Sys_PlayLevel();
            Snd_StopAll();
            TaskerInit();
            Sys_UnloadLevel();
            if ( g_gameFlags & GAME_FLAG_DEATH_DONE ) {
                Menu_PlayDeathMovie( g_level );
                Password_Restore();
            } else if ( g_gameFlags & GAME_FLAG_LEVEL_COMPLETE ) {
                g_gameFlags &= GAME_FLAG_HARD;
                next = Menu_LevelComplete( g_level );
                if ( next == g_level ) {
                    Password_Restore();
                } else {
                    g_level = next;
                    if ( next == LEVEL_211_LIVING_QUARTERS )
                        Menu_PlayStoryMovie( MOVIE_S1_2 );
                    if ( g_level == LEVEL_311_TUNNELS )
                        Menu_PlayStoryMovie( MOVIE_S2_3 );
                }
            }
        }
        if ( g_level == LEVEL_COUNT && !CHEAT_IS_ACTIVE( CHEAT_FLAG_CHEATS_MENU ) )
            Menu_PlayStoryMovie( MOVIE_GAMEOVER );
    }
}

/*
==================
Heap_Init
==================
*/
/* name: inferred from its body, which claims the largest malloc block below HEAP_SIZE_MAX as the heap and clears the snapshots */
void Heap_Init( void )
{
    unsigned int size;
    void        *memory;
    short        i;
    size = HEAP_SIZE_MAX;
    do {
        size -= HEAP_SIZE_STEP;
        memory = malloc( size );
    } while ( memory == 0 );
    g_heapSize = size;
    g_heapAllocation = memory;
    g_heapNext = (unsigned char *)memory;
    g_heapUsed = 0;
    g_heapFree = size;
    for ( i = 0; i < HEAP_SNAPSHOT_COUNT; i++ ) {
        g_heapSnapshots[i].next = 0;
        g_heapSnapshots[i].used = 0;
        g_heapSnapshots[i].free = 0;
    }
    g_heapSnapshotCurrent = Heap_Mark();
}

/*
==================
Heap_Alloc
==================
*/
/* name: inferred from its body, which takes word-aligned bytes from the heap or stops with an overflow error; DOS port: strong code-similarity match at 0x11874, which prints 'MEMMGR: No memory. %ld bytes requested' */
void *Heap_Alloc( int bytes )
{
    unsigned char *oldLow;
    int            aligned = ( bytes + HEAP_ALIGN - 1 ) & -HEAP_ALIGN;
    if ( g_heapFree >= (unsigned int)aligned ) {
        oldLow = g_heapNext;
        g_heapFree -= aligned;
        g_heapNext += aligned;
        g_heapUsed += aligned;
        return oldLow;
    }
    Sys_FatalError( s_heapOverflow );
    return 0;
}

/*
==================
Heap_Free
==================
*/
/* name: inferred from its body, which returns the most recently allocated bytes to the heap */
void Heap_Free( int bytes )
{
    int aligned = ( bytes + HEAP_ALIGN - 1 ) & -HEAP_ALIGN;
    g_heapNext -= aligned;
    g_heapUsed -= aligned;
    g_heapFree += aligned;
}

/*
==================
Heap_Mark
==================
*/
/* name: inferred from its body, which saves the heap state in a free snapshot slot */
int Heap_Mark( void )
{
    short freeSlot = -1;
    short i;

    for ( i = 0; i < HEAP_SNAPSHOT_COUNT; i++ ) {
        if ( g_heapSnapshots[i].next == 0 ) {
            freeSlot = i;
            break;
        }
    }
    if ( freeSlot == -1 )
        return -1;
    g_heapSnapshots[freeSlot].next = (unsigned int)g_heapNext;
    g_heapSnapshots[freeSlot].used = g_heapUsed;
    g_heapSnapshots[freeSlot].free = g_heapFree;
    return freeSlot;
}

/*
==================
Heap_Release
==================
*/
/* name: inferred from its body, which restores a heap snapshot and frees later snapshots */
void Heap_Release( short slot )
{
    if ( slot != -1 ) {
        g_heapNext = (unsigned char *)g_heapSnapshots[slot].next;
        g_heapUsed = g_heapSnapshots[slot].used;
        g_heapFree = g_heapSnapshots[slot].free;
        while ( slot < HEAP_SNAPSHOT_COUNT ) {
            g_heapSnapshots[slot].next = 0;
            g_heapSnapshots[slot].used = 0;
            g_heapSnapshots[slot].free = 0;
            slot++;
        }
    }
}

/*
==================
Sys_FatalError
==================
*/
/* name: inferred from its body, which shows "INTERNAL ERROR: " and the message, then hangs in VSync */
void Sys_FatalError( const char *message )
{
    DRAWENV draw;
    DISPENV display;
    void   *buffer;

    SetDefDrawEnv( &draw, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT );
    SetDefDispEnv( &display, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT );
    draw.isbg = 0;
    setRGB0( &draw, ERROR_BACKGROUND_RED, 0, 0 );
    SetDispMask( 1 );
    PutDispEnv( &display );
    PutDrawEnv( &draw );
    FntLoad( ERROR_FONT_X, ERROR_FONT_Y );
    buffer = FntOpen( ERROR_TEXT_X, ERROR_TEXT_Y, SCREEN_WIDTH, ERROR_TEXT_HEIGHT, 0, ERROR_TEXT_LENGTH );
    SetDumpFnt( buffer );
    FntPrint( s_errorTitle );
    FntPrint( s_errorFormat, message );
    FntFlush( buffer );
    for ( ;; )
        VSync( 0 );
}

/*
==================
Sys_CDReadCallback
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Sys_CDReadCallback( void )
{
    return ++g_cdReadCount;
}

/*
==================
Sys_InitCD
==================
*/
/* name: inferred from its body, which resets the callbacks, initialises the CD and looks up files 160..162 */
void Sys_InitCD( void )
{
    heapFile_t *file;

    ResetCallback();
    CdInit();
    CdSetDebug( 0 );
    g_cdReadCount = 0;
    CdReadCallback( Sys_CDReadCallback );
    file = &g_fileTable[FILE_COLONY_BS];
    CdSearchFile( &file->file, (char *)file->name );
    file = &g_fileTable[FILE_PRISHOLD_BS];
    CdSearchFile( &file->file, (char *)file->name );
    file = &g_fileTable[FILE_BONESHIP_BS];
    CdSearchFile( &file->file, (char *)file->name );
}

/*
==================
Sys_LoadFile
==================
*/
/* name: inferred from its body, which loads a CD file table entry into a heap buffer */
void Sys_LoadFile( heapFile_t *file )
{
    CdlFILE *cd = &file->file;

    g_cdReadCount = 0;
    CdSearchFile( cd, (char *)file->name );
    CdControlB( CdlSeekL, (unsigned char *)&cd->pos, 0 );
    file->loadedSize = cd->size;
    if ( file->buffer == 0 )
        file->buffer = Heap_Alloc( file->loadedSize );
    CdRead( ( file->loadedSize + CD_SECTOR_SIZE - 1 ) / CD_SECTOR_SIZE, file->buffer, CdlModeSpeed );
    while ( CdReadSync( 1, 0 ) > 0 )
        VSync( 0 );
}

/*
==================
Sys_LoadFileStart
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Sys_LoadFileStart( heapFile_t *file )
{
    CdlFILE *cd = &file->file;

    g_cdReadCount = 0;
    CdSearchFile( cd, (char *)file->name );
    CdControl( CdlSeekL, (unsigned char *)&cd->pos, 0 );
    file->loadedSize = cd->size;
    if ( file->buffer == 0 )
        file->buffer = Heap_Alloc( file->loadedSize );
    CdRead( ( file->loadedSize + CD_SECTOR_SIZE - 1 ) / CD_SECTOR_SIZE, file->buffer, CdlModeSpeed );
}

/*
==================
Sys_CDSeekFile
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sys_CDSeekFile( int n )
{
    CdControl( CdlSeekL, (unsigned char *)&g_fileTable[n].file.pos, 0 );
}

/*
==================
Sys_InitTexturePages
==================
*/
/* name: inferred from its body, which allocates and fills a grid of texture pages */
void Sys_InitTexturePages( texturePageSet_t *set )
{
    texturePage_t *page = set->pages;
    short          x, y;
    short          i;

    if ( page == 0 ) {
        page = set->pages = (texturePage_t *)Heap_Alloc( set->count * sizeof( texturePage_t ) );
    }
    x = set->x;
    y = set->y;
    for ( i = 0; i < set->count; i++, page++ ) {
        if ( set->mode == 0 ) {
            page->tpage = GetTPage( 1, 0, x, y );
            page->x = x;
            page->y = y;
            page->reserved = 0;
            page->users = 0;
            page->pixels = 0;
            x += set->xStep >> 1;
            if ( x >= set->xEnd ) {
                x = set->x;
                y += set->yStep;
                if ( y >= set->yEnd )
                    y = set->y;
            }
        } else {
            page->tpage = GetTPage( 0, 0, x, y );
            page->x = x;
            page->y = y;
            page->reserved = 0;
            page->users = 0;
            page->pixels = 0;
            x += set->xStep >> 2;
            if ( x >= set->xEnd ) {
                x = set->x;
                y += set->yStep;
                if ( y >= set->yEnd )
                    y = set->y;
            }
        }
    }
}

/*
==================
Sys_ClearVRAM
==================
*/
/* name: inferred from its body, which fills all of VRAM with one colour through a block-fill packet */
void Sys_ClearVRAM( unsigned char red, unsigned char green, unsigned char blue )
{
    blockFillPacket_t area;

    SetBlockFill( &area );
    area.x = 0;
    area.y = 0;
    area.width = VRAM_WIDTH - 1;
    area.height = VRAM_HEIGHT - 1;
    area.red = red;
    area.green = green;
    area.blue = blue;
    DrawPrim( &area );
    DrawSync( 0 );
}

/*
==================
Sys_FindFreeTexturePage
==================
*/
/* name: inferred from its body, which returns the first unused texture page of a set */
texturePage_t *Sys_FindFreeTexturePage( texturePageSet_t *l )
{
    short          n = l->count;
    texturePage_t *p = l->pages;
    while ( n ) {
        if ( !p->users )
            return p;
        n--;
        p++;
    }
    return 0;
}

/*
==================
Sys_VBlankHandler
==================
*/
/* name: inferred from its body, which OpenEvent installs it on root counter 3 (vertical blank); it counts frames and steps the message box */
void Sys_VBlankHandler( void )
{
    g_frameCount++;
    g_graphicsBusy = 0;
    g_soundClock++;
    if ( g_displayMode != DISPLAY_MODE_NORMAL ) {
        if ( g_displayMode == DISPLAY_MODE_MESSAGE_BOX )
            Menu_MessageBoxType();
        else
            g_displayMode = DISPLAY_MODE_NORMAL;
    }
}

/*
==================
Sys_SetDisplayMode
==================
*/
/* name: inferred from its body, which sets g_displayMode inside a critical section */
void Sys_SetDisplayMode( int mode )
{
    EnterCriticalSection();
    g_displayMode = (short)mode;
    ExitCriticalSection();
}

/*
==================
Sys_InstallVBlankHandler
==================
*/
/* name: inferred from its body, which opens and enables the root counter 3 event with Sys_VBlankHandler */
void Sys_InstallVBlankHandler( void )
{
    g_frameCount = 0;
    g_graphicsBusy = -1;
    Sys_SetDisplayMode( DISPLAY_MODE_NORMAL );
    EnterCriticalSection();
    EnableEvent( OpenEvent( RCntCNT3, EvSpINT, EvMdINTR, (eventHandler_t)Sys_VBlankHandler ) );
    SetRCnt( RCntCNT3, 1, RCntMdINTR );
    StartRCnt( RCntCNT3 );
    ExitCriticalSection();
}

/*
==================
Sys_InitCLUTRows
==================
*/
/* name: inferred from its body, which allocates a strip of CLUT rows and computes their CLUT ids */
void Sys_InitCLUTRows( clutSet_t *set )
{
    short          i;
    unsigned short y;
    unsigned short x;
    clutRow_t     *row;
    row = set->rows;
    if ( row == 0 ) {
        row = set->rows = (clutRow_t *)Heap_Alloc( set->count * sizeof( clutRow_t ) );
    }
    y = set->y;
    x = set->x;
    for ( i = 0; i < set->count; i++, row++ ) {
        row->clut = GetClut( (short)x, (short)y );
        row->x = x;
        row->y = y++;
        row->inUse = 0;
    }
}

/*
==================
Sys_UploadCLUTRow
==================
*/
/* name: inferred from its body, which loads one 16-colour CLUT row into VRAM */
void Sys_UploadCLUTRow( clutRow_t *row, unsigned long *pixels )
{
    RECT r;
    r.x = row->x;
    r.y = row->y;
    r.w = CLUT_ROW_WIDTH;
    r.h = 1;
    LoadImage( &r, pixels );
}

/*
==================
Sys_FindFreeCLUTRow
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
clutRow_t *Sys_FindFreeCLUTRow( clutSet_t *set )
{
    short      n = set->count;
    clutRow_t *p = set->rows;
    while ( n ) {
        if ( !( p->inUse & 1 ) )
            return p;
        n--;
        p++;
    }
    return 0;
}

/*
==================
Sys_InitDisplayBuffers

Allocates the two display buffers and sets up a 320x240 double buffer
(drawing at y 0 and 240), then makes the first buffer current.
==================
*/
/* name: inferred from its body, which allocates the two display buffers and sets up the 320x240 double buffer */
void Sys_InitDisplayBuffers( void )
{
    g_renderWorkspace = g_displayBuffers = (displayBuffer_t *)Heap_Alloc( 2 * sizeof( displayBuffer_t ) );
    g_displayDefault = g_renderWorkspace + 1;
    SetDefDrawEnv( &g_drawEnvA, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT );
    g_drawEnvA.tpage = GetTPage( 1, 0, DRAW_ENV_TPAGE_X, 0 );
    g_drawEnvA.dtd = 1;
    g_drawEnvA.isbg = 1;
    setRGB0( &g_drawEnvA, 0, 0, 0 );
    setRECT( &g_drawEnvA.clip, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT );
    SetDrawEnv( &g_renderWorkspace->draw, &g_drawEnvA );
    SetDefDispEnv( &g_renderWorkspace->disp, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT );
    SetDefDrawEnv( &g_drawEnvB, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT );
    g_drawEnvB.tpage = GetTPage( 1, 0, DRAW_ENV_TPAGE_X, 0 );
    g_drawEnvB.dtd = 1;
    g_drawEnvB.isbg = 1;
    setRGB0( &g_drawEnvB, 0, 0, 0 );
    setRECT( &g_drawEnvB.clip, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT );
    SetDrawEnv( &g_displayDefault->draw, &g_drawEnvB );
    SetDefDispEnv( &g_displayDefault->disp, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT );
    SetFarColor( 0, 0, 0 );
    SetFogNear( FOG_NEAR, PROJECTION_DISTANCE );
    ClearOTagR( g_renderWorkspace->ot, OT_LENGTH );
    ClearOTagR( g_displayDefault->ot, OT_LENGTH );
    PutDispEnv( &g_renderWorkspace->disp );
    g_display = g_renderWorkspace;
    g_renderBuffer = g_display->ot;
    g_spriteWorkspace = g_display->prims;
}

/*
==================
Sys_SetupDoubleBuffer
==================
*/
/* name: inferred from its body, which sets up double buffering: buffer A draws in first
 * and shows second, buffer B the reverse. Retail writes B's dfe in both halves. */
void Sys_SetupDoubleBuffer( RECT *first, RECT *second, u_char isbg, u_char dfe )
{
    SetDefDrawEnv( &g_drawEnvA, first->x, first->y, first->w, first->h );
    g_drawEnvA.tpage = GetTPage( 1, 0, DRAW_ENV_TPAGE_X, 0 );
    g_drawEnvA.dtd = 1;
    g_drawEnvB.dfe = dfe;
    g_drawEnvA.isbg = isbg;
    setRGB0( &g_drawEnvA, 0, 0, 0 );
    g_drawEnvA.clip.x = first->x;
    g_drawEnvA.clip.y = first->y;
    g_drawEnvA.clip.w = first->w;
    g_drawEnvA.clip.h = first->h;
    SetDrawEnv( &g_renderWorkspace->draw, &g_drawEnvA );
    SetDefDispEnv( &g_renderWorkspace->disp, second->x, second->y, second->w, second->h );
    SetDefDrawEnv( &g_drawEnvB, second->x, second->y, second->w, second->h );
    g_drawEnvB.tpage = GetTPage( 1, 0, DRAW_ENV_TPAGE_X, 0 );
    g_drawEnvB.dtd = 1;
    g_drawEnvB.dfe = dfe;
    g_drawEnvB.isbg = isbg;
    setRGB0( &g_drawEnvB, 0, 0, 0 );
    g_drawEnvB.clip.x = second->x;
    g_drawEnvB.clip.y = second->y;
    g_drawEnvB.clip.w = second->w;
    g_drawEnvB.clip.h = second->h;
    SetDrawEnv( &g_displayDefault->draw, &g_drawEnvB );
    SetDefDispEnv( &g_displayDefault->disp, first->x, first->y, first->w, first->h );
}
