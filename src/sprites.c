/* sprites.c -- sprite resources: the menu screens' texture pages and CLUT rows, the sprite file
 * loader (\GFX\*.BND and the level's sprite sets) and the sprite record list */
/* Retail .text 0x8001c588..0x8001d464, after the text module. One translation unit: its .sdata
 * (g_spriteListCount, 0x8008cf84) and .rdata (g_spriteWarn, 0x80011998) are the first after
 * frontend's, its static is the .sbss slot after the text module's (0x8008d0c0), and its .data
 * holds g_spriteResources (Sprite_LoadLevelSprites) ahead of g_menuResourceIds
 * (Sprite_LoadLevelPanelSprites), which places Sprite_LoadLevelPanelSprites and
 * Sprite_LoadLevelSprites in one module. Name invented. */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LIBCD.H>
#include <LEVEL.H>
#include <TASK.H>
#include <MODEL.H>
#include <HEAP.H>
#include <HEAPFILE.H>
#include <AUDIO.H>
#include <GAME.H>
#include <TEXTURE.H>
#include <SPRITEUV.H>
#include <SPRITE.H>
#include <TRACK.H>
#include <ANIMS.H>
#include <TEXT.H>
#include <TYPES.H>

/* The menu screens' texture pages (g_pageSets[PAGE_SET_MENU]) and CLUT rows (g_clutSets[CLUT_SET_SPRITES]). name invented */
#define MENU_RESOURCE_COUNT 5
/* Pages of g_pageSets[PAGE_SET_SPRITE_SHEETS]: the menu font (src/text.c), then the sprite sets of Sprite_LoadSets.
 * names invented */
enum {
    SPRITE_PAGE_FONT,
    SPRITE_PAGE_PICKUPS,
    SPRITE_PAGE_PANEL,
    SPRITE_PAGE_EXPLOSIONS
};
/* First rows of g_clutSets[CLUT_SET_SPRITE_SHEETS] given to the sprite sets (row 0 is the menu font's). names invented */
#define CLUT_ROW_EXPLOSIONS 1
#define CLUT_ROW_PANEL 4
#define CLUT_ROW_PICKUPS 13
/* Rows of g_clutSets[CLUT_SET_SPRITES] that Sprite_LoadLevelSprites hands to the level's sprite resources
 * (rows below are the menu screens'). names: as the CLUT pointers (<TEXTURE.H>) */
enum {
    CLUT_ROW_PULSE_RIFLE = 6,
    CLUT_ROW_PISTOL,
    CLUT_ROW_FLAMETHROWER,
    CLUT_ROW_SMART_GUN,
    CLUT_ROW_SHOTGUN,
    CLUT_ROW_EGG,
    CLUT_ROW_FACEHUGGER,
    CLUT_ROW_ALIEN,
    CLUT_ROW_HANDLER,
    CLUT_ROW_BURSTER,
    CLUT_ROW_BAMBI,
    CLUT_ROW_SOLDIER,
    CLUT_ROW_GUARD,
    CLUT_ROW_SYNTH,
    CLUT_ROW_DOG,
    CLUT_ROW_FINGERS,
    CLUT_ROW_QUEEN,
    CLUT_ROW_COLONIST
};
/* Indices of g_spriteResources. names: from each entry's file */
enum {
    SPRITE_RESOURCE_PISTOL,
    SPRITE_RESOURCE_SMART_GUN,
    SPRITE_RESOURCE_PULSE_RIFLE,
    SPRITE_RESOURCE_FLAMETHROWER,
    SPRITE_RESOURCE_SHOTGUN,
    SPRITE_RESOURCE_HUGGER,
    SPRITE_RESOURCE_FINGERS,
    SPRITE_RESOURCE_DOG,
    SPRITE_RESOURCE_DOG_CEILING,
    SPRITE_RESOURCE_WARRIOR,
    SPRITE_RESOURCE_WARRIOR_CEILING,
    SPRITE_RESOURCE_EGG,
    SPRITE_RESOURCE_HANDLER,
    SPRITE_RESOURCE_SYNTH,
    SPRITE_RESOURCE_GUARD,
    SPRITE_RESOURCE_SOLDIER,
    SPRITE_RESOURCE_BURSTER,
    SPRITE_RESOURCE_BAMBI,
    SPRITE_RESOURCE_QUEEN,
    SPRITE_RESOURCE_COLONIST,
    SPRITE_RESOURCE_COUNT
};
/* Sprite files (Sprite_LoadFile): a prefix of SPRITE_FILE_PREFIX_SIZE bytes, then the header and
 * chunks, each a 4-character tag and a big-endian size word. "TP" chunks hold a texture page
 * at SPRITE_DEPTH_* bits per pixel, "BX" chunks the sprite boxes of page NN (bytes u, v, w, h,
 * CLUT row), "CL" chunks CLUTs of 16 or 256 colours. names invented */
#define SPRITE_FILE_PREFIX_SIZE 16
#define CHUNK_TAG_SIZE 4
#define CHUNK_SIZE_FIELD 4
/* Halfword indices of a sprite file's header (spriteFileHeader_t), which follows the prefix. */
enum {
    SPRITE_HEADER_WIDTH,
    SPRITE_HEADER_HEIGHT,
    SPRITE_HEADER_DEPTH,
    SPRITE_HEADER_SPRITE_COUNT,
    SPRITE_HEADER_CLUT_COUNT,
    SPRITE_HEADER_PAGE_COUNT,
    SPRITE_HEADER_STRIDE
};
#define SPRITE_DEPTH_4BIT 4
#define SPRITE_DEPTH_8BIT 8
enum {
    SPRITE_BOX_U,
    SPRITE_BOX_V,
    SPRITE_BOX_W,
    SPRITE_BOX_H,
    SPRITE_BOX_CLUT
};
#define CLUT_4BIT_SIZE 32
#define CLUT_8BIT_SIZE 512
/* g_spriteList's capacity (Sprite_ListAdd). name invented */
#define SPRITE_LIST_MAX 128

/* name: runtime evidence, analysis/runtime/globals.json: Sprite_ReserveMenuTextures points g_palette at it for the
 * menu screens, and the level palette lives here too (Light_Init rewrites it at level load) */
extern paletteEntry_t g_palettes[];
extern texturePage_t *g_menuResourceRecords[MENU_RESOURCE_COUNT];
extern clutRow_t     *g_menuResourceData[MENU_RESOURCE_COUNT];

/* name: runtime evidence, analysis/runtime/globals.json: records in g_spriteList (35 at level 1) */
short g_spriteListCount = 0;

/* Strings of Sprite_ListAdd. Its extent includes its alignment padding. */
#define SPRITE_WARN_SIZE 36
const char g_spriteWarn[SPRITE_WARN_SIZE] = "TOO MANY SPRITES: INIT IMAGE ADDR";

/* The animations of each sprite resource file (frame lists and scripts in asm/sprite_anims.s). */
/* name: analysis/runtime/globals.json */
static trackEntry_t g_pulseRifleSet[] = { /* file 0x1 */
                                          { g_pulseRifleAnim0Frames, g_pulseRifleAnim0Script },
                                          { g_pulseRifleAnim1Frames, g_pulseRifleAnim1Script },
                                          { g_pulseRifleAnim2Frames, g_pulseRifleAnim2Script },
                                          { g_pulseRifleAnim3Frames, g_pulseRifleAnim3Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_shotgunSet[] = { /* file 0x3 */
                                       { g_shotgunAnim0Frames, g_shotgunAnim0Script },
                                       { g_shotgunAnim1Frames, g_shotgunAnim1Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_pistolSet[] = { /* file 0x2 */
                                      { g_pistolAnim0Frames, g_pistolAnim0Script },
                                      { g_pistolAnim1Frames, g_pistolAnim1Script },
                                      { g_pistolAnim2Frames, g_pistolAnim2Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_smartGunSet[] = { /* file 0x5 */
                                        { g_smartGunAnim0Frames, g_smartGunAnim0Script },
                                        { g_smartGunAnim1Frames, g_smartGunAnim1Script },
                                        { g_smartGunAnim2Frames, g_smartGunAnim2Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_flamethrowerSet[] = { /* file 0x4 */
                                            { g_flamethrowerAnim0Frames, g_flamethrowerAnim0Script },
                                            { g_flamethrowerAnim1Frames, g_flamethrowerAnim1Script },
                                            { g_flamethrowerAnim2Frames, g_flamethrowerAnim2Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_huggerSet[] = { /* file 0x7 */
                                      { g_huggerAnim0Frames, g_huggerAnim0Script },
                                      { g_huggerAnim1Frames, g_huggerAnim1Script },
                                      { g_huggerAnim2Frames, g_huggerAnim2Script },
                                      { g_huggerAnim3Frames, g_huggerAnim3Script },
                                      { g_huggerAnim4Frames, g_huggerAnim4Script },
                                      { g_huggerAnim5Frames, g_huggerAnim5Script },
                                      { g_huggerAnim6Frames, g_huggerAnim6Script },
                                      { g_huggerAnim7Frames, g_huggerAnim7Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_fingersSet[] = { /* file 0x10 */
                                       { g_fingersAnim0Frames, g_fingersAnim0Script },
                                       { g_fingersAnim1Frames, g_fingersAnim1Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_dogSet[] = { /* file 0x6 */
                                   { g_dogAnim0Frames, g_dogAnim0Script },  { g_dogAnim1Frames, g_dogAnim1Script },
                                   { g_dogAnim2Frames, g_dogAnim2Script },  { g_dogAnim3Frames, g_dogAnim3Script },
                                   { g_dogAnim4Frames, g_dogAnim4Script },  { g_dogAnim5Frames, g_dogAnim5Script },
                                   { g_dogAnim6Frames, g_dogAnim6Script },  { g_dogAnim7Frames, g_dogAnim7Script },
                                   { g_dogAnim8Frames, g_dogAnim8Script },  { g_dogAnim9Frames, g_dogAnim9Script },
                                   { g_dogAnim10Frames, g_dogAnim10Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_dogCeilingSet[] = { /* file 0x13 */
                                          { g_dogCeilingAnim0Frames, g_dogCeilingAnim0Script },
                                          { g_dogCeilingAnim1Frames, g_dogCeilingAnim1Script },
                                          { g_dogCeilingAnim2Frames, g_dogCeilingAnim2Script },
                                          { g_dogCeilingAnim3Frames, g_dogCeilingAnim3Script },
                                          { g_dogCeilingAnim4Frames, g_dogCeilingAnim4Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_warriorSet[] = { /* file 0x8 */
                                       { g_warriorAnim0Frames, g_warriorAnim0Script },
                                       { g_warriorAnim1Frames, g_warriorAnim1Script },
                                       { g_warriorAnim2Frames, g_warriorAnim2Script },
                                       { g_warriorAnim3Frames, g_warriorAnim3Script },
                                       { g_warriorAnim4Frames, g_warriorAnim4Script },
                                       { g_warriorAnim5Frames, g_warriorAnim5Script },
                                       { g_warriorAnim6Frames, g_warriorAnim6Script },
                                       { g_warriorAnim7Frames, g_warriorAnim7Script },
                                       { g_warriorAnim8Frames, g_warriorAnim8Script },
                                       { g_warriorAnim9Frames, g_warriorAnim9Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_warriorCeilingSet[] = { /* file 0x11 */
                                              { g_warriorCeilingAnim0Frames, g_warriorCeilingAnim0Script },
                                              { g_warriorCeilingAnim1Frames, g_warriorCeilingAnim1Script },
                                              { g_warriorCeilingAnim2Frames, g_warriorCeilingAnim2Script },
                                              { g_warriorCeilingAnim3Frames, g_warriorCeilingAnim3Script },
                                              { g_warriorCeilingAnim4Frames, g_warriorCeilingAnim4Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_eggSet[] = { /* file 0x9 */
                                   { g_eggAnim0Frames, g_eggAnim0Script }, { g_eggAnim1Frames, g_eggAnim1Script },
                                   { g_eggAnim2Frames, g_eggAnim2Script }, { g_eggAnim3Frames, g_eggAnim3Script },
                                   { g_eggAnim4Frames, g_eggAnim4Script }, { g_eggAnim5Frames, g_eggAnim5Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_handlerSet[] = { /* file 0xa */
                                       { g_handlerAnim0Frames, g_handlerAnim0Script },
                                       { g_handlerAnim1Frames, g_handlerAnim1Script },
                                       { g_handlerAnim2Frames, g_handlerAnim2Script },
                                       { g_handlerAnim3Frames, g_handlerAnim3Script },
                                       { g_handlerAnim4Frames, g_handlerAnim4Script },
                                       { g_handlerAnim5Frames, g_handlerAnim5Script },
                                       { g_handlerAnim6Frames, g_handlerAnim6Script },
                                       { g_handlerAnim7Frames, g_handlerAnim7Script },
                                       { g_handlerAnim8Frames, g_handlerAnim8Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t
    g_synthSet[] = { /* file 0xb */
                     { g_synthAnim0Frames, g_synthAnim0Script },   { g_synthAnim1Frames, g_synthAnim1Script },
                     { g_synthAnim2Frames, g_synthAnim2Script },   { g_synthAnim3Frames, g_synthAnim3Script },
                     { g_synthAnim4Frames, g_synthAnim4Script },   { g_synthAnim5Frames, g_synthAnim5Script },
                     { g_synthAnim6Frames, g_synthAnim6Script },   { g_synthAnim7Frames, g_synthAnim7Script },
                     { g_synthAnim8Frames, g_synthAnim8Script },   { g_synthAnim9Frames, g_synthAnim9Script },
                     { g_synthAnim10Frames, g_synthAnim10Script }, { g_synthAnim11Frames, g_synthAnim11Script },
                     { g_synthAnim12Frames, g_synthAnim12Script }
    };
/* name: analysis/runtime/globals.json */
static trackEntry_t g_guardSet[] = { /* file 0xc */
                                     { g_guardAnim0Frames, g_guardAnim0Script },
                                     { g_guardAnim1Frames, g_guardAnim1Script },
                                     { g_guardAnim2Frames, g_guardAnim2Script },
                                     { g_guardAnim3Frames, g_guardAnim3Script },
                                     { g_guardAnim4Frames, g_guardAnim4Script },
                                     { g_guardAnim5Frames, g_guardAnim5Script },
                                     { g_guardAnim6Frames, g_guardAnim6Script },
                                     { g_guardAnim7Frames, g_guardAnim7Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_soldierSet[] = { /* file 0xd */
                                       { g_soldierAnim0Frames, g_soldierAnim0Script },
                                       { g_soldierAnim1Frames, g_soldierAnim1Script },
                                       { g_soldierAnim2Frames, g_soldierAnim2Script },
                                       { g_soldierAnim3Frames, g_soldierAnim3Script },
                                       { g_soldierAnim4Frames, g_soldierAnim4Script },
                                       { g_soldierAnim5Frames, g_soldierAnim5Script },
                                       { g_soldierAnim6Frames, g_soldierAnim6Script },
                                       { g_soldierAnim7Frames, g_soldierAnim7Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_bursterSet[] = { /* file 0xe */
                                       { g_bursterAnim0Frames, g_bursterAnim0Script },
                                       { g_bursterAnim1Frames, g_bursterAnim1Script },
                                       { g_bursterAnim2Frames, g_bursterAnim2Script },
                                       { g_bursterAnim3Frames, g_bursterAnim3Script },
                                       { g_bursterAnim4Frames, g_bursterAnim4Script },
                                       { g_bursterAnim5Frames, g_bursterAnim5Script },
                                       { g_bursterAnim6Frames, g_bursterAnim6Script },
                                       { g_bursterAnim7Frames, g_bursterAnim7Script },
                                       { g_bursterAnim8Frames, g_bursterAnim8Script },
                                       { g_bursterAnim9Frames, g_bursterAnim9Script },
                                       { g_bursterAnim10Frames, g_bursterAnim10Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t g_bambiSet[] = { /* file 0xf */
                                     { g_bambiAnim0Frames, g_bambiAnim0Script },
                                     { g_bambiAnim1Frames, g_bambiAnim1Script },
                                     { g_bambiAnim2Frames, g_bambiAnim2Script },
                                     { g_bambiAnim3Frames, g_bambiAnim3Script },
                                     { g_bambiAnim4Frames, g_bambiAnim4Script },
                                     { g_bambiAnim5Frames, g_bambiAnim5Script },
                                     { g_bambiAnim6Frames, g_bambiAnim6Script },
                                     { g_bambiAnim7Frames, g_bambiAnim7Script },
                                     { g_bambiAnim8Frames, g_bambiAnim8Script },
                                     { g_bambiAnim9Frames, g_bambiAnim9Script }
};
/* name: analysis/runtime/globals.json */
static trackEntry_t
    g_queenSet[] = { /* file 0x12 */
                     { g_queenAnim0Frames, g_queenAnim0Script },   { g_queenAnim1Frames, g_queenAnim1Script },
                     { g_queenAnim2Frames, g_queenAnim2Script },   { g_queenAnim3Frames, g_queenAnim3Script },
                     { g_queenAnim4Frames, g_queenAnim4Script },   { g_queenAnim5Frames, g_queenAnim5Script },
                     { g_queenAnim6Frames, g_queenAnim6Script },   { g_queenAnim7Frames, g_queenAnim7Script },
                     { g_queenAnim8Frames, g_queenAnim8Script },   { g_queenAnim9Frames, g_queenAnim9Script },
                     { g_queenAnim10Frames, g_queenAnim10Script }, { g_queenAnim11Frames, g_queenAnim11Script }
    };
/* name: analysis/runtime/globals.json */
static trackEntry_t g_colonistSet[] = { /* file 0x14 */
                                        { g_colonistAnim0Frames, g_colonistAnim0Script },
                                        { g_colonistAnim1Frames, g_colonistAnim1Script },
                                        { g_colonistAnim2Frames, g_colonistAnim2Script },
                                        { g_colonistAnim3Frames, g_colonistAnim3Script },
                                        { g_colonistAnim4Frames, g_colonistAnim4Script },
                                        { g_colonistAnim5Frames, g_colonistAnim5Script },
                                        { g_colonistAnim6Frames, g_colonistAnim6Script },
                                        { g_colonistAnim7Frames, g_colonistAnim7Script },
                                        { g_colonistAnim8Frames, g_colonistAnim8Script },
                                        { g_colonistAnim9Frames, g_colonistAnim9Script },
                                        { g_colonistAnim10Frames, g_colonistAnim10Script },
                                        { g_colonistAnim11Frames, g_colonistAnim11Script },
                                        { g_colonistAnim12Frames, g_colonistAnim12Script },
                                        { g_colonistAnim13Frames, g_colonistAnim13Script },
                                        { g_colonistAnim14Frames, g_colonistAnim14Script },
                                        { g_colonistAnim15Frames, g_colonistAnim15Script },
                                        { g_colonistAnim16Frames, g_colonistAnim16Script },
                                        { g_colonistAnim17Frames, g_colonistAnim17Script },
                                        { g_colonistAnim18Frames, g_colonistAnim18Script },
                                        { g_colonistAnim19Frames, g_colonistAnim19Script },
                                        { g_colonistAnim20Frames, g_colonistAnim20Script },
                                        { g_colonistAnim21Frames, g_colonistAnim21Script },
                                        { g_colonistAnim22Frames, g_colonistAnim22Script }
};

/* The level's sprite resource files, loaded by Sprite_LoadLevelSprites through Sprite_LoadResource. */
/* name: analysis/runtime/globals.json */
static resourceFile_t g_spriteResources[SPRITE_RESOURCE_COUNT] = { { FILE_MM9_BND, g_pistolSet, &g_pistolCLUT },
                                                                   { FILE_SMART_BND, g_smartGunSet, &g_smartGunCLUT },
                                                                   { FILE_PULSE_BND, g_pulseRifleSet, &g_pulseRifleCLUT },
                                                                   { FILE_FLAME_BND, g_flamethrowerSet, &g_flamethrowerCLUT },
                                                                   { FILE_SHOTGUN_BND, g_shotgunSet, &g_shotgunCLUT },
                                                                   { FILE_HUGGER_BND, g_huggerSet, &g_facehuggerCLUT },
                                                                   { FILE_FINGERS_BND, g_fingersSet, &g_fingersCLUT },
                                                                   { FILE_DOG_BND, g_dogSet, &g_dogCLUT },
                                                                   { FILE_DOGCEIL_BND, g_dogCeilingSet, &g_dogCLUT },
                                                                   { FILE_WAR_BND, g_warriorSet, &g_alienCLUT },
                                                                   { FILE_WARCEIL_BND, g_warriorCeilingSet, &g_alienCLUT },
                                                                   { FILE_EGGS_BND, g_eggSet, &g_eggCLUT },
                                                                   { FILE_HANDLER_BND, g_handlerSet, &g_handlerCLUT },
                                                                   { FILE_SYNTH_BND, g_synthSet, &g_synthCLUT },
                                                                   { FILE_GUARD_BND, g_guardSet, &g_guardCLUT },
                                                                   { FILE_SOLDIER_BND, g_soldierSet, &g_soldierCLUT },
                                                                   { FILE_BURSTER_BND, g_bursterSet, &g_bursterCLUT },
                                                                   { FILE_BAMBI_BND, g_bambiSet, &g_bambiCLUT },
                                                                   { FILE_QUEEN_BND, g_queenSet, &g_queenCLUT },
                                                                   { FILE_COLONIST_BND, g_colonistSet, &g_colonistCLUT } };

/* Sprite_LoadLevelPanelSprites uses signed-halfword resource IDs selected by g_level.
 * The original allocation ends at the next dispatch table, 0x8006d308.
 * The final zero halfword preserves its complete aligned extent.
 */
#define MENU_RESOURCE_ID_SIZE 36 /* 35 IDs and the padding halfword. name: invented */
short g_menuResourceIds[MENU_RESOURCE_ID_SIZE] = { 21, 23, 25, 33, 37, 27, 41, 29, 49,  51, 53,  55,
                                                   57, 59, 61, 63, 69, 65, 67, 71, 73,  75, 77,  79,
                                                   81, 83, 85, 87, 89, 95, 97, 93, 101, 99, 103, 0 };

/* name: inferred from use: Sprite_LoadPanelSpritesAlternate loads the alternate panel once while it is 0 */
static short g_panelSpritesAlternate;

extern void Sprite_ListAdd( const u8 *base, spriteFrame_t *frames, s16 count );

/*
==================
Sprite_ReserveMenuTextures

Reserves the texture pages and CLUT rows used by the menu screens.
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Sprite_ReserveMenuTextures( void )
{
    unsigned short i;
    texturePage_t *pages;

    g_modelUvs = g_spriteUvs;
    g_palette = g_palettes;
    for ( i = 0; (short)i < MENU_RESOURCE_COUNT; i++ ) {
        g_menuResourceRecords[(short)i] = &g_pageSets[PAGE_SET_MENU].pages[(short)i];
        g_menuResourceData[(short)i] = &g_clutSets[CLUT_SET_SPRITES].rows[(short)i];
        g_menuResourceRecords[(short)i]->users = 1;
        g_menuResourceRecords[(short)i]->reserved = 1;
        g_menuResourceData[(short)i]->inUse = 1;
    }
    pages = g_pageSets[PAGE_SET_SPRITE_SHEETS].pages;
    g_pickupPage = &pages[SPRITE_PAGE_PICKUPS];
    pages[SPRITE_PAGE_PICKUPS].users = 1;
    g_pickupPage->reserved = 1;
    for ( i = 0; i < ARRAY_COUNT( g_pickupCluts ); i++ ) {
        g_pickupCluts[(short)i] = &g_clutSets[CLUT_SET_SPRITE_SHEETS].rows[(short)i + CLUT_ROW_PICKUPS];
        g_pickupCluts[(short)i]->inUse = 1;
    }
    pages = g_pageSets[PAGE_SET_SPRITE_SHEETS].pages;
    g_panelPage = &pages[SPRITE_PAGE_PANEL];
    pages[SPRITE_PAGE_PANEL].users = 1;
    g_panelPage->reserved = 1;
    for ( i = 0; i < ARRAY_COUNT( g_panelCluts ); i++ ) {
        g_panelCluts[(short)i] = &g_clutSets[CLUT_SET_SPRITE_SHEETS].rows[(short)i + CLUT_ROW_PANEL];
        g_panelCluts[(short)i]->inUse = 1;
    }
    pages = g_pageSets[PAGE_SET_SPRITE_SHEETS].pages;
    g_explosionPage = &pages[SPRITE_PAGE_EXPLOSIONS];
    pages[SPRITE_PAGE_EXPLOSIONS].users = 1;
    g_explosionPage->reserved = 1;
    for ( i = 0; i < ARRAY_COUNT( g_explosionCluts ); i++ ) {
        g_explosionCluts[(short)i] = &g_clutSets[CLUT_SET_SPRITE_SHEETS].rows[(short)i + CLUT_ROW_EXPLOSIONS];
        g_explosionCluts[(short)i]->inUse = 1;
    }
}
/*
==================
Sprite_LoadFile

Loads a sprite resource file: "TP" chunks upload texture pages, "BX"
chunks describe sprites on page NN, "CL" chunks upload CLUTs.
==================
*/
/* name: inferred from its body, which loads a sprite file's pixels into texture pages, its CLUTs into CLUT rows and fills the UV records */
void Sprite_LoadFile( short id, texturePage_t **pages, clutRow_t **cluts, spriteUV_t *uv )
{
    heapFile_t        *file = &g_fileTable[id];
    unsigned char     *p;
    unsigned char     *q;
    spriteFileHeader_t header;
    unsigned char      tag[CHUNK_TAG_SIZE + 1];
    unsigned int       size;
    short              page;
    short              clut;
    short              count;
    short              sprites;
    short              sheet;

    file->loadedSize = 0;
    file->buffer = 0;
    Sys_LoadFile( file );
    p = file->buffer;
    p += SPRITE_FILE_PREFIX_SIZE;
    size = Text_ReadBigEndian32( p );
    p += CHUNK_SIZE_FIELD;
    header.width = ( (short *)p )[SPRITE_HEADER_WIDTH];
    header.height = ( (short *)p )[SPRITE_HEADER_HEIGHT];
    header.depth = ( (short *)p )[SPRITE_HEADER_DEPTH];
    header.spriteCount = ( (short *)p )[SPRITE_HEADER_SPRITE_COUNT];
    header.clutCount = ( (short *)p )[SPRITE_HEADER_CLUT_COUNT];
    header.pageCount = ( (short *)p )[SPRITE_HEADER_PAGE_COUNT];
    header.stride = ( (short *)p )[SPRITE_HEADER_STRIDE];
    page = 0;
    clut = 0;
    p += size;
    while ( p < (unsigned char *)file->buffer + file->loadedSize ) {
        tag[0] = *p++;
        tag[1] = *p++;
        tag[2] = *p++;
        tag[CHUNK_TAG_SIZE - 1] = *p++;
        tag[CHUNK_TAG_SIZE] = 0;
        size = Text_ReadBigEndian32( p );
        p += CHUNK_SIZE_FIELD;
        if ( tag[0] == 'T' && tag[1] == 'P' ) {
            if ( header.depth == SPRITE_DEPTH_4BIT )
                LoadTPage( (u_long *)p, TPAGE_MODE_4BIT, 0, pages[page]->x, pages[page]->y, header.width, header.height );
            else if ( header.depth == SPRITE_DEPTH_8BIT )
                LoadTPage( (u_long *)p, TPAGE_MODE_8BIT, 0, pages[page]->x, pages[page]->y, header.width, header.height );
            else
                LoadTPage( (u_long *)p, TPAGE_MODE_16BIT, 0, pages[page]->x, pages[page]->y, header.width, header.height );
            p += size;
            page++;
        } else if ( tag[0] == 'B' && tag[1] == 'X' ) {
            sprites = *(unsigned short *)p;
            q = p + 2;
            sheet = ( tag[2] - '0' ) * DECIMAL_RADIX + ( tag[CHUNK_TAG_SIZE - 1] - '0' );
            while ( sprites-- != 0U ) {
                uv->u0 = q[SPRITE_BOX_U];
                uv->v0 = q[SPRITE_BOX_V];
                uv->u1 = uv->u0 + q[SPRITE_BOX_W];
                uv->v1 = uv->v0;
                uv->u2 = uv->u0;
                uv->v2 = uv->v0 + q[SPRITE_BOX_H];
                uv->u3 = uv->u1;
                uv->v3 = uv->v2;
                uv->pad2 = uv->clut = cluts[q[SPRITE_BOX_CLUT]]->clut;
                uv->tpage = pages[sheet]->tpage;
                uv++;
                q += header.stride;
            }
            p += size;
        } else if ( tag[0] == 'C' && tag[1] == 'L' ) {
            count = *(unsigned short *)p;
            q = p + CHUNK_SIZE_FIELD;
            if ( header.depth == SPRITE_DEPTH_4BIT ) {
                while ( count ) {
                    Sys_UploadCLUTRow( cluts[clut], (unsigned long *)q );
                    clut++;
                    q += CLUT_4BIT_SIZE;
                    count--;
                }
            } else if ( header.depth == SPRITE_DEPTH_8BIT ) {
                while ( count ) {
                    LoadClut( (u_long *)q, cluts[clut]->x, cluts[clut]->y );
                    clut++;
                    q += CLUT_8BIT_SIZE;
                    count--;
                }
            } else
                break;
            p += size;
        } else
            break;
    }
    Heap_Free( file->loadedSize );
}

/*
==================
Sprite_LoadLevelPanelSprites
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sprite_LoadLevelPanelSprites( void )
{
    Sprite_LoadFile( g_menuResourceIds[g_level], g_menuResourceRecords, g_menuResourceData, g_spriteUvs );
}

extern spriteListEntry_t g_spriteList[];
/*
==================
Sprite_LoadSets
==================
*/
/* name: inferred from its body, which loads \LANGUAGE\PNL0GFXU.BND, \GFX\PICKGFX.BND and \GFX\EXPLGFX.BND (file table entries 0x6b, 0x69, 0x6f) */
void Sprite_LoadSets( void )
{
    Sprite_LoadFile( FILE_PNL0GFXU_BND, &g_panelPage, g_panelCluts, g_panelUvs );
    Sprite_LoadFile( FILE_PICKGFX_BND, &g_pickupPage, g_pickupCluts, g_pickupUvs );
    Sprite_LoadFile( FILE_EXPLGFX_BND, &g_explosionPage, g_explosionCluts, g_explosionUvs );
    g_panelSpritesAlternate = 0;
}

/*
==================
Sprite_LoadPanelSpritesAlternate
==================
*/
/* name: inferred from its body, which loads \LANGUAGE\PNL1GFXU.BND (entry 0x6c) over the panel sprites once */
void Sprite_LoadPanelSpritesAlternate( void )
{
    if ( g_panelSpritesAlternate == 0 ) {
        Sprite_LoadFile( FILE_PNL1GFXU_BND, &g_panelPage, g_panelCluts, g_panelUvs );
        g_panelSpritesAlternate = -1;
    }
}

/*
==================
Sprite_ListAdd
==================
*/
/* name: inferred from its body, which appends a sprite record list and relocates its offsets, stopping on g_spriteWarn; DOS port: string-only match at 0x18ff8, which has the message 'TOO MANY SPRITES: INIT IMAGE ADDR' */
void Sprite_ListAdd( const u8 *base, spriteFrame_t *frames, s16 count )
{
    s16            index = g_spriteListCount;
    spriteFrame_t *cursor;
    g_spriteList[index].count = count;
    g_spriteList[index].frames = frames;
    if ( index < SPRITE_LIST_MAX )
        g_spriteListCount = index + 1;
    else
        Sys_FatalError( g_spriteWarn );
    if ( count != 0 ) {
        cursor = frames;
        do {
            cursor->pixels = (u_long *)( (u32)cursor->pixels + (u32)base );
            count--;
            ++cursor;
        } while ( count != 0 );
    }
}

/*
==================
Sprite_AddChunk
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
u8 *Sprite_AddChunk( const u8 *data, spriteFrame_t *frames, short *script )
{
    u32 size;
    data += CHUNK_TAG_SIZE;
    size = Text_ReadBigEndian32( data );
    data += CHUNK_SIZE_FIELD;
    if ( size & 1 )
        ++size;
    Sprite_ListAdd( data, frames, script[TRACK_SCRIPT_FRAMES] );
    return (u8 *)data + size;
}

/*
==================
Sprite_ResetList
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sprite_ResetList( void )
{
    s16            count;
    spriteFrame_t *cursor;
    u32            base;

    while ( --g_spriteListCount >= 0 ) {
        count = g_spriteList[g_spriteListCount].count;
        cursor = g_spriteList[g_spriteListCount].frames;
        base = (u32)cursor->pixels;
        if ( count != 0 ) {
            do {
                cursor->pixels = (u_long *)( (u32)cursor->pixels - base );
                count--;
                ++cursor;
            } while ( count != 0 );
        }
    }
    g_spriteListCount = 0;
}

/* The shift is defined for IDs 1..14, and -1 returns zero. The caller's
 * lower input bound remains unproven: zero and values below -1 have a
 * hardware-masked shift in the reference but undefined C shift behavior.
 * An explicit &31 mask adds an instruction with the pinned compiler. */
#define SPRITE_SHIFT_ID_LIMIT 15 /* f_1d0e0 takes IDs below it. name: invented */
/*
==================
f_1d0e0
==================
*/
/* identity-TODO: the bit of a 1-based id; no jal, pointer word, lui/lo pair or $gp operand in retail reaches it, so no caller shows which set it indexes. */
int f_1d0e0( short index )
{
    if ( index != -1 && index < SPRITE_SHIFT_ID_LIMIT )
        return 1 << ( index - 1 );
    return 0;
}

/* CLUT index from which Sprite_LoadResource sets the transparency bit of the weapon sets'
 * palettes (their last two colours). name invented */
#define SPRITE_WEAPON_STP_FIRST 0xfe

extern void Sprite_LoadResource( resourceFile_t *resource, short transparent, short firstOpaque );

/*
==================
Sprite_LoadLevelSprites

Loads the level's sprite resources: the fixed sets, then one per
resource flag in the level header.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Sprite_LoadLevelSprites( void )
{
    unsigned short i;
    unsigned int   resources;
    clutRow_t     *rows;

    for ( i = 0; i < ARRAY_COUNT( g_actorKindMap ); i++ )
        g_actorKindMap[(short)i] = i;
    if ( g_spriteListCount != 0 )
        Sprite_ResetList();
    rows = g_clutSets[CLUT_SET_SPRITES].rows;
    g_pulseRifleCLUT = &rows[CLUT_ROW_PULSE_RIFLE];
    g_pistolCLUT = &rows[CLUT_ROW_PISTOL];
    g_flamethrowerCLUT = &rows[CLUT_ROW_FLAMETHROWER];
    g_smartGunCLUT = &rows[CLUT_ROW_SMART_GUN];
    g_shotgunCLUT = &rows[CLUT_ROW_SHOTGUN];
    g_eggCLUT = &rows[CLUT_ROW_EGG];
    g_facehuggerCLUT = &rows[CLUT_ROW_FACEHUGGER];
    g_alienCLUT = &rows[CLUT_ROW_ALIEN];
    g_handlerCLUT = &rows[CLUT_ROW_HANDLER];
    g_bursterCLUT = &rows[CLUT_ROW_BURSTER];
    g_bambiCLUT = &rows[CLUT_ROW_BAMBI];
    g_soldierCLUT = &rows[CLUT_ROW_SOLDIER];
    g_guardCLUT = &rows[CLUT_ROW_GUARD];
    g_synthCLUT = &rows[CLUT_ROW_SYNTH];
    g_dogCLUT = &rows[CLUT_ROW_DOG];
    g_fingersCLUT = &rows[CLUT_ROW_FINGERS];
    g_queenCLUT = &rows[CLUT_ROW_QUEEN];
    g_colonistCLUT = &rows[CLUT_ROW_COLONIST];
    Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_SMART_GUN], -1, SPRITE_WEAPON_STP_FIRST );
    Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_PULSE_RIFLE], -1, SPRITE_WEAPON_STP_FIRST );
    Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_SHOTGUN], 0, 0 );
    Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_PISTOL], -1, SPRITE_WEAPON_STP_FIRST );
    Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_FLAMETHROWER], 0, 0 );
    resources = g_levelMap.resources;
    if ( resources & LEVEL_SPRITES_EGG )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_EGG], 0, 0 );
    if ( resources & LEVEL_SPRITES_HUGGER ) {
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_HUGGER], 0, 0 );
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_FINGERS], 0, 0 );
    }
    if ( resources & LEVEL_SPRITES_BURSTER )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_BURSTER], 0, 0 );
    if ( resources & LEVEL_SPRITES_COLONIST )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_COLONIST], 0, 0 );
    if ( resources & LEVEL_SPRITES_BAMBI )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_BAMBI], 0, 0 );
    if ( resources & LEVEL_SPRITES_DOG )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_DOG], 0, 0 );
    if ( resources & LEVEL_SPRITES_DOG_CEILING )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_DOG_CEILING], 0, 0 );
    if ( resources & LEVEL_SPRITES_WARRIOR )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_WARRIOR], 0, 0 );
    if ( resources & LEVEL_SPRITES_WARRIOR_CEILING )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_WARRIOR_CEILING], 0, 0 );
    if ( resources & LEVEL_SPRITES_GUARD )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_GUARD], 0, 0 );
    if ( resources & LEVEL_SPRITES_SOLDIER )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_SOLDIER], 0, 0 );
    if ( resources & LEVEL_SPRITES_SYNTH )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_SYNTH], 0, 0 );
    if ( resources & LEVEL_SPRITES_HANDLER )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_HANDLER], 0, 0 );
    if ( resources & LEVEL_SPRITES_QUEEN )
        Sprite_LoadResource( &g_spriteResources[SPRITE_RESOURCE_QUEEN], 0, 0 );
}
