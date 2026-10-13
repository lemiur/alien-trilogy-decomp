/* Sprite resources: the menu screens' texture pages and CLUT rows, the sprite file
 * loader (\GFX\*.BND and the level's sprite sets) and the sprite record list. Retail
 * .text 0x8001c588..0x8001d464, after the text module. One translation unit: its
 * .sdata (g_sprite_list_count, 0x8008cf84) and .rdata (g_sprite_warn, 0x80011998) are the first
 * after frontend's, its static is the .sbss slot after the text module's (0x8008d0c0),
 * and its .data holds g_sprite_resources (level_sprites_load) ahead of g_menu_resource_ids (level_panel_sprites_load), which
 * places level_panel_sprites_load and level_sprites_load in one module. Name invented.
 */
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
typedef unsigned char u8;
typedef unsigned int u32;
typedef short s16;

/* The menu screens' texture pages (g_page_sets[PAGE_SET_MENU]) and CLUT rows (g_clut_sets[CLUT_SET_SPRITES]). name invented */
#define MENU_RESOURCE_COUNT 5
/* Pages of g_page_sets[PAGE_SET_SPRITE_SHEETS]: the menu font (src/text.c), then the sprite sets of sprite_sets_load.
 * names invented */
enum {
    SPRITE_PAGE_FONT,
    SPRITE_PAGE_PICKUPS,
    SPRITE_PAGE_PANEL,
    SPRITE_PAGE_EXPLOSIONS
};
/* First rows of g_clut_sets[CLUT_SET_SPRITE_SHEETS] given to the sprite sets (row 0 is the menu font's). names invented */
#define CLUT_ROW_EXPLOSIONS 1
#define CLUT_ROW_PANEL 4
#define CLUT_ROW_PICKUPS 13
/* Rows of g_clut_sets[CLUT_SET_SPRITES] that level_sprites_load hands to the level's sprite resources
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
/* Indices of g_sprite_resources. names: from each entry's file */
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
/* Sprite files (sprite_file_load): a prefix of SPRITE_FILE_PREFIX_SIZE bytes, then the header and
 * chunks, each a 4-character tag and a big-endian size word. "TP" chunks hold a texture page
 * at SPRITE_DEPTH_* bits per pixel, "BX" chunks the sprite boxes of page NN (bytes u, v, w, h,
 * CLUT row), "CL" chunks CLUTs of 16 or 256 colours. names invented */
#define SPRITE_FILE_PREFIX_SIZE 16
#define CHUNK_TAG_SIZE 4
#define CHUNK_SIZE_FIELD 4
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
/* g_sprite_list's capacity (sprite_list_add). name invented */
#define SPRITE_LIST_MAX 128

/* name: runtime evidence, analysis/runtime/globals.json: menu_textures_reserve points g_palette at it for the
 * menu screens, and the level palette lives here too (lights_init rewrites it at level load) */
extern PaletteEntry g_palettes[];
extern TexturePage *g_menu_resource_records[MENU_RESOURCE_COUNT];
extern ClutRow *g_menu_resource_data[MENU_RESOURCE_COUNT];

/* name: runtime evidence, analysis/runtime/globals.json: records in g_sprite_list (35 at level 1) */
short g_sprite_list_count = 0;

/* Strings of sprite_list_add. Its extent includes its alignment padding. */
const char g_sprite_warn[36] = "TOO MANY SPRITES: INIT IMAGE ADDR";

/* A sprite resource file: CD table index, the animations its frames serve and its CLUT row. */
typedef struct ResourceFile {
    int file;
    TrackEntry *animations;
    ClutRow **palette;
} ResourceFile;

/* The animations of each sprite resource file (frame lists and scripts in asm/sprite_anims.s). */
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_pulse_rifle_set[] = { /* file 0x1 */
    { g_pulse_rifle_anim0_frames, g_pulse_rifle_anim0_script }, { g_pulse_rifle_anim1_frames, g_pulse_rifle_anim1_script }, { g_pulse_rifle_anim2_frames, g_pulse_rifle_anim2_script },
    { g_pulse_rifle_anim3_frames, g_pulse_rifle_anim3_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_shotgun_set[] = { /* file 0x3 */
    { g_shotgun_anim0_frames, g_shotgun_anim0_script }, { g_shotgun_anim1_frames, g_shotgun_anim1_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_pistol_set[] = { /* file 0x2 */
    { g_pistol_anim0_frames, g_pistol_anim0_script }, { g_pistol_anim1_frames, g_pistol_anim1_script }, { g_pistol_anim2_frames, g_pistol_anim2_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_smart_gun_set[] = { /* file 0x5 */
    { g_smart_gun_anim0_frames, g_smart_gun_anim0_script }, { g_smart_gun_anim1_frames, g_smart_gun_anim1_script }, { g_smart_gun_anim2_frames, g_smart_gun_anim2_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_flamethrower_set[] = { /* file 0x4 */
    { g_flamethrower_anim0_frames, g_flamethrower_anim0_script }, { g_flamethrower_anim1_frames, g_flamethrower_anim1_script }, { g_flamethrower_anim2_frames, g_flamethrower_anim2_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_hugger_set[] = { /* file 0x7 */
    { g_hugger_anim0_frames, g_hugger_anim0_script }, { g_hugger_anim1_frames, g_hugger_anim1_script }, { g_hugger_anim2_frames, g_hugger_anim2_script },
    { g_hugger_anim3_frames, g_hugger_anim3_script }, { g_hugger_anim4_frames, g_hugger_anim4_script }, { g_hugger_anim5_frames, g_hugger_anim5_script },
    { g_hugger_anim6_frames, g_hugger_anim6_script }, { g_hugger_anim7_frames, g_hugger_anim7_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_fingers_set[] = { /* file 0x10 */
    { g_fingers_anim0_frames, g_fingers_anim0_script }, { g_fingers_anim1_frames, g_fingers_anim1_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_dog_set[] = { /* file 0x6 */
    { g_dog_anim0_frames, g_dog_anim0_script }, { g_dog_anim1_frames, g_dog_anim1_script }, { g_dog_anim2_frames, g_dog_anim2_script },
    { g_dog_anim3_frames, g_dog_anim3_script }, { g_dog_anim4_frames, g_dog_anim4_script }, { g_dog_anim5_frames, g_dog_anim5_script },
    { g_dog_anim6_frames, g_dog_anim6_script }, { g_dog_anim7_frames, g_dog_anim7_script }, { g_dog_anim8_frames, g_dog_anim8_script },
    { g_dog_anim9_frames, g_dog_anim9_script }, { g_dog_anim10_frames, g_dog_anim10_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_dog_ceiling_set[] = { /* file 0x13 */
    { g_dog_ceiling_anim0_frames, g_dog_ceiling_anim0_script }, { g_dog_ceiling_anim1_frames, g_dog_ceiling_anim1_script }, { g_dog_ceiling_anim2_frames, g_dog_ceiling_anim2_script },
    { g_dog_ceiling_anim3_frames, g_dog_ceiling_anim3_script }, { g_dog_ceiling_anim4_frames, g_dog_ceiling_anim4_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_warrior_set[] = { /* file 0x8 */
    { g_warrior_anim0_frames, g_warrior_anim0_script }, { g_warrior_anim1_frames, g_warrior_anim1_script }, { g_warrior_anim2_frames, g_warrior_anim2_script },
    { g_warrior_anim3_frames, g_warrior_anim3_script }, { g_warrior_anim4_frames, g_warrior_anim4_script }, { g_warrior_anim5_frames, g_warrior_anim5_script },
    { g_warrior_anim6_frames, g_warrior_anim6_script }, { g_warrior_anim7_frames, g_warrior_anim7_script }, { g_warrior_anim8_frames, g_warrior_anim8_script },
    { g_warrior_anim9_frames, g_warrior_anim9_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_warrior_ceiling_set[] = { /* file 0x11 */
    { g_warrior_ceiling_anim0_frames, g_warrior_ceiling_anim0_script }, { g_warrior_ceiling_anim1_frames, g_warrior_ceiling_anim1_script }, { g_warrior_ceiling_anim2_frames, g_warrior_ceiling_anim2_script },
    { g_warrior_ceiling_anim3_frames, g_warrior_ceiling_anim3_script }, { g_warrior_ceiling_anim4_frames, g_warrior_ceiling_anim4_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_egg_set[] = { /* file 0x9 */
    { g_egg_anim0_frames, g_egg_anim0_script }, { g_egg_anim1_frames, g_egg_anim1_script }, { g_egg_anim2_frames, g_egg_anim2_script },
    { g_egg_anim3_frames, g_egg_anim3_script }, { g_egg_anim4_frames, g_egg_anim4_script }, { g_egg_anim5_frames, g_egg_anim5_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_handler_set[] = { /* file 0xa */
    { g_handler_anim0_frames, g_handler_anim0_script }, { g_handler_anim1_frames, g_handler_anim1_script }, { g_handler_anim2_frames, g_handler_anim2_script },
    { g_handler_anim3_frames, g_handler_anim3_script }, { g_handler_anim4_frames, g_handler_anim4_script }, { g_handler_anim5_frames, g_handler_anim5_script },
    { g_handler_anim6_frames, g_handler_anim6_script }, { g_handler_anim7_frames, g_handler_anim7_script }, { g_handler_anim8_frames, g_handler_anim8_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_synth_set[] = { /* file 0xb */
    { g_synth_anim0_frames, g_synth_anim0_script }, { g_synth_anim1_frames, g_synth_anim1_script }, { g_synth_anim2_frames, g_synth_anim2_script },
    { g_synth_anim3_frames, g_synth_anim3_script }, { g_synth_anim4_frames, g_synth_anim4_script }, { g_synth_anim5_frames, g_synth_anim5_script },
    { g_synth_anim6_frames, g_synth_anim6_script }, { g_synth_anim7_frames, g_synth_anim7_script }, { g_synth_anim8_frames, g_synth_anim8_script },
    { g_synth_anim9_frames, g_synth_anim9_script }, { g_synth_anim10_frames, g_synth_anim10_script }, { g_synth_anim11_frames, g_synth_anim11_script },
    { g_synth_anim12_frames, g_synth_anim12_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_guard_set[] = { /* file 0xc */
    { g_guard_anim0_frames, g_guard_anim0_script }, { g_guard_anim1_frames, g_guard_anim1_script }, { g_guard_anim2_frames, g_guard_anim2_script },
    { g_guard_anim3_frames, g_guard_anim3_script }, { g_guard_anim4_frames, g_guard_anim4_script }, { g_guard_anim5_frames, g_guard_anim5_script },
    { g_guard_anim6_frames, g_guard_anim6_script }, { g_guard_anim7_frames, g_guard_anim7_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_soldier_set[] = { /* file 0xd */
    { g_soldier_anim0_frames, g_soldier_anim0_script }, { g_soldier_anim1_frames, g_soldier_anim1_script }, { g_soldier_anim2_frames, g_soldier_anim2_script },
    { g_soldier_anim3_frames, g_soldier_anim3_script }, { g_soldier_anim4_frames, g_soldier_anim4_script }, { g_soldier_anim5_frames, g_soldier_anim5_script },
    { g_soldier_anim6_frames, g_soldier_anim6_script }, { g_soldier_anim7_frames, g_soldier_anim7_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_burster_set[] = { /* file 0xe */
    { g_burster_anim0_frames, g_burster_anim0_script }, { g_burster_anim1_frames, g_burster_anim1_script }, { g_burster_anim2_frames, g_burster_anim2_script },
    { g_burster_anim3_frames, g_burster_anim3_script }, { g_burster_anim4_frames, g_burster_anim4_script }, { g_burster_anim5_frames, g_burster_anim5_script },
    { g_burster_anim6_frames, g_burster_anim6_script }, { g_burster_anim7_frames, g_burster_anim7_script }, { g_burster_anim8_frames, g_burster_anim8_script },
    { g_burster_anim9_frames, g_burster_anim9_script }, { g_burster_anim10_frames, g_burster_anim10_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_bambi_set[] = { /* file 0xf */
    { g_bambi_anim0_frames, g_bambi_anim0_script }, { g_bambi_anim1_frames, g_bambi_anim1_script }, { g_bambi_anim2_frames, g_bambi_anim2_script },
    { g_bambi_anim3_frames, g_bambi_anim3_script }, { g_bambi_anim4_frames, g_bambi_anim4_script }, { g_bambi_anim5_frames, g_bambi_anim5_script },
    { g_bambi_anim6_frames, g_bambi_anim6_script }, { g_bambi_anim7_frames, g_bambi_anim7_script }, { g_bambi_anim8_frames, g_bambi_anim8_script },
    { g_bambi_anim9_frames, g_bambi_anim9_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_queen_set[] = { /* file 0x12 */
    { g_queen_anim0_frames, g_queen_anim0_script }, { g_queen_anim1_frames, g_queen_anim1_script }, { g_queen_anim2_frames, g_queen_anim2_script },
    { g_queen_anim3_frames, g_queen_anim3_script }, { g_queen_anim4_frames, g_queen_anim4_script }, { g_queen_anim5_frames, g_queen_anim5_script },
    { g_queen_anim6_frames, g_queen_anim6_script }, { g_queen_anim7_frames, g_queen_anim7_script }, { g_queen_anim8_frames, g_queen_anim8_script },
    { g_queen_anim9_frames, g_queen_anim9_script }, { g_queen_anim10_frames, g_queen_anim10_script }, { g_queen_anim11_frames, g_queen_anim11_script }
};
/* name: analysis/runtime/globals.json (round 2) */
static TrackEntry g_colonist_set[] = { /* file 0x14 */
    { g_colonist_anim0_frames, g_colonist_anim0_script }, { g_colonist_anim1_frames, g_colonist_anim1_script }, { g_colonist_anim2_frames, g_colonist_anim2_script },
    { g_colonist_anim3_frames, g_colonist_anim3_script }, { g_colonist_anim4_frames, g_colonist_anim4_script }, { g_colonist_anim5_frames, g_colonist_anim5_script },
    { g_colonist_anim6_frames, g_colonist_anim6_script }, { g_colonist_anim7_frames, g_colonist_anim7_script }, { g_colonist_anim8_frames, g_colonist_anim8_script },
    { g_colonist_anim9_frames, g_colonist_anim9_script }, { g_colonist_anim10_frames, g_colonist_anim10_script }, { g_colonist_anim11_frames, g_colonist_anim11_script },
    { g_colonist_anim12_frames, g_colonist_anim12_script }, { g_colonist_anim13_frames, g_colonist_anim13_script }, { g_colonist_anim14_frames, g_colonist_anim14_script },
    { g_colonist_anim15_frames, g_colonist_anim15_script }, { g_colonist_anim16_frames, g_colonist_anim16_script }, { g_colonist_anim17_frames, g_colonist_anim17_script },
    { g_colonist_anim18_frames, g_colonist_anim18_script }, { g_colonist_anim19_frames, g_colonist_anim19_script }, { g_colonist_anim20_frames, g_colonist_anim20_script },
    { g_colonist_anim21_frames, g_colonist_anim21_script }, { g_colonist_anim22_frames, g_colonist_anim22_script }
};

/* The level's sprite resource files, loaded by level_sprites_load through sprite_resource_load. */
/* name: analysis/runtime/globals.json (round 2) */
static ResourceFile g_sprite_resources[SPRITE_RESOURCE_COUNT] = {
    { FILE_MM9_BND, g_pistol_set, &g_pistol_clut },
    { FILE_SMART_BND, g_smart_gun_set, &g_smart_gun_clut },
    { FILE_PULSE_BND, g_pulse_rifle_set, &g_pulse_rifle_clut },
    { FILE_FLAME_BND, g_flamethrower_set, &g_flamethrower_clut },
    { FILE_SHOTGUN_BND, g_shotgun_set, &g_shotgun_clut },
    { FILE_HUGGER_BND, g_hugger_set, &g_facehugger_clut },
    { FILE_FINGERS_BND, g_fingers_set, &g_fingers_clut },
    { FILE_DOG_BND, g_dog_set, &g_dog_clut },
    { FILE_DOGCEIL_BND, g_dog_ceiling_set, &g_dog_clut },
    { FILE_WAR_BND, g_warrior_set, &g_alien_clut },
    { FILE_WARCEIL_BND, g_warrior_ceiling_set, &g_alien_clut },
    { FILE_EGGS_BND, g_egg_set, &g_egg_clut },
    { FILE_HANDLER_BND, g_handler_set, &g_handler_clut },
    { FILE_SYNTH_BND, g_synth_set, &g_synth_clut },
    { FILE_GUARD_BND, g_guard_set, &g_guard_clut },
    { FILE_SOLDIER_BND, g_soldier_set, &g_soldier_clut },
    { FILE_BURSTER_BND, g_burster_set, &g_burster_clut },
    { FILE_BAMBI_BND, g_bambi_set, &g_bambi_clut },
    { FILE_QUEEN_BND, g_queen_set, &g_queen_clut },
    { FILE_COLONIST_BND, g_colonist_set, &g_colonist_clut }
};

/* level_panel_sprites_load uses signed-halfword resource IDs selected by g_level.
 * The original allocation ends at the next dispatch table, 0x8006d308.
 * The final zero halfword preserves its complete aligned extent.
 */
short g_menu_resource_ids[36] = {
    21, 23, 25, 33, 37, 27, 41, 29,
    49, 51, 53, 55, 57, 59, 61, 63,
    69, 65, 67, 71, 73, 75, 77, 79,
    81, 83, 85, 87, 89, 95, 97, 93,
    101, 99, 103, 0
};

/* name: inferred from use: panel_sprites_load_alternate loads the alternate panel once while it is 0 */
static short g_panel_sprites_alternate;

extern void sprite_list_add(const u8 *base, SpriteFrame *frames, s16 count);

/* Reserves the texture pages and CLUT rows used by the menu screens. */
/* name: static evidence, analysis/runtime/functions.json */
void menu_textures_reserve(void)
{
    unsigned short i;
    TexturePage *pages;

    g_model_uvs = g_sprite_uvs;
    g_palette = g_palettes;
    for (i = 0; (short)i < MENU_RESOURCE_COUNT; i++) {
        g_menu_resource_records[(short)i] = &g_page_sets[PAGE_SET_MENU].pages[(short)i];
        g_menu_resource_data[(short)i] = &g_clut_sets[CLUT_SET_SPRITES].rows[(short)i];
        g_menu_resource_records[(short)i]->users = 1;
        g_menu_resource_records[(short)i]->reserved = 1;
        g_menu_resource_data[(short)i]->in_use = 1;
    }
    pages = g_page_sets[PAGE_SET_SPRITE_SHEETS].pages;
    g_pickup_page = &pages[SPRITE_PAGE_PICKUPS];
    pages[SPRITE_PAGE_PICKUPS].users = 1;
    g_pickup_page->reserved = 1;
    for (i = 0; i < ARRAY_COUNT(g_pickup_cluts); i++) {
        g_pickup_cluts[(short)i] = &g_clut_sets[CLUT_SET_SPRITE_SHEETS].rows[(short)i + CLUT_ROW_PICKUPS];
        g_pickup_cluts[(short)i]->in_use = 1;
    }
    pages = g_page_sets[PAGE_SET_SPRITE_SHEETS].pages;
    g_panel_page = &pages[SPRITE_PAGE_PANEL];
    pages[SPRITE_PAGE_PANEL].users = 1;
    g_panel_page->reserved = 1;
    for (i = 0; i < ARRAY_COUNT(g_panel_cluts); i++) {
        g_panel_cluts[(short)i] = &g_clut_sets[CLUT_SET_SPRITE_SHEETS].rows[(short)i + CLUT_ROW_PANEL];
        g_panel_cluts[(short)i]->in_use = 1;
    }
    pages = g_page_sets[PAGE_SET_SPRITE_SHEETS].pages;
    g_explosion_page = &pages[SPRITE_PAGE_EXPLOSIONS];
    pages[SPRITE_PAGE_EXPLOSIONS].users = 1;
    g_explosion_page->reserved = 1;
    for (i = 0; i < ARRAY_COUNT(g_explosion_cluts); i++) {
        g_explosion_cluts[(short)i] = &g_clut_sets[CLUT_SET_SPRITE_SHEETS].rows[(short)i + CLUT_ROW_EXPLOSIONS];
        g_explosion_cluts[(short)i]->in_use = 1;
    }
}
/* Header of a sprite resource file; names invented. */
typedef struct SpriteFileHeader {
    short width, height;
    short depth;
    short sprite_count; /* invented name: BX boxes in the file, in every shipped sprite file (not read) */
    short clut_count; /* invented name: CLUT rows of its CL chunks, in every shipped sprite file (not read) */
    short page_count; /* invented name: its TP chunks, in every shipped sprite file (not read) */
    short stride;
} SpriteFileHeader;

/* Loads a sprite resource file: "TP" chunks upload texture pages, "BX"
 * chunks describe sprites on page NN, "CL" chunks upload CLUTs. */
/* name: inferred from its body, which loads a sprite file's pixels into texture pages, its CLUTs into CLUT rows and fills the UV records */
void sprite_file_load(short id, TexturePage **pages, ClutRow **cluts, SpriteUV *uv)
{
    HeapFile *file = &g_file_table[id];
    unsigned char *p;
    unsigned char *q;
    SpriteFileHeader header;
    unsigned char tag[CHUNK_TAG_SIZE + 1];
    unsigned int size;
    short page;
    short clut;
    short count;
    short sprites;
    short sheet;

    file->loaded_size = 0;
    file->buffer = 0;
    load_file(file);
    p = file->buffer;
    p += SPRITE_FILE_PREFIX_SIZE;
    size = read_be32(p);
    p += CHUNK_SIZE_FIELD;
    header.width = ((short *)p)[0];
    header.height = ((short *)p)[1];
    header.depth = ((short *)p)[2];
    header.sprite_count = ((short *)p)[3];
    header.clut_count = ((short *)p)[4];
    header.page_count = ((short *)p)[5];
    header.stride = ((short *)p)[6];
    page = 0;
    clut = 0;
    p += size;
    while (p < (unsigned char *)file->buffer + file->loaded_size) {
        tag[0] = *p++;
        tag[1] = *p++;
        tag[2] = *p++;
        tag[3] = *p++;
        tag[CHUNK_TAG_SIZE] = 0;
        size = read_be32(p);
        p += CHUNK_SIZE_FIELD;
        if (tag[0] == 'T' && tag[1] == 'P') {
            if (header.depth == SPRITE_DEPTH_4BIT)
                LoadTPage((u_long *)p, TPAGE_MODE_4BIT, 0, pages[page]->x, pages[page]->y, header.width, header.height);
            else if (header.depth == SPRITE_DEPTH_8BIT)
                LoadTPage((u_long *)p, TPAGE_MODE_8BIT, 0, pages[page]->x, pages[page]->y, header.width, header.height);
            else
                LoadTPage((u_long *)p, TPAGE_MODE_16BIT, 0, pages[page]->x, pages[page]->y, header.width, header.height);
            p += size;
            page++;
        } else if (tag[0] == 'B' && tag[1] == 'X') {
            sprites = *(unsigned short *)p;
            q = p + 2;
            sheet = (tag[2] - '0') * 10 + (tag[3] - '0');
            while (sprites-- != 0U) {
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
        } else if (tag[0] == 'C' && tag[1] == 'L') {
            count = *(unsigned short *)p;
            q = p + CHUNK_SIZE_FIELD;
            if (header.depth == SPRITE_DEPTH_4BIT) {
                while (count) {
                    upload_clut_row(cluts[clut], (unsigned long *)q);
                    clut++;
                    q += CLUT_4BIT_SIZE;
                    count--;
                }
            } else if (header.depth == SPRITE_DEPTH_8BIT) {
                while (count) {
                    LoadClut((u_long *)q, cluts[clut]->x, cluts[clut]->y);
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
    heap_free(file->loaded_size);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void level_panel_sprites_load(void)
{
    sprite_file_load(g_menu_resource_ids[g_level], g_menu_resource_records,
            g_menu_resource_data, g_sprite_uvs);
}

typedef struct { s16 count; SpriteFrame *frames; } SpriteListEntry;
extern SpriteListEntry g_sprite_list[];
/* name: inferred from its body, which loads \LANGUAGE\PNL0GFXU.BND, \GFX\PICKGFX.BND and \GFX\EXPLGFX.BND (file table entries 0x6b, 0x69, 0x6f) */
void sprite_sets_load(void)
{
    sprite_file_load(FILE_PNL0GFXU_BND, &g_panel_page, g_panel_cluts, g_panel_uvs);
    sprite_file_load(FILE_PICKGFX_BND, &g_pickup_page, g_pickup_cluts, g_pickup_uvs);
    sprite_file_load(FILE_EXPLGFX_BND, &g_explosion_page, g_explosion_cluts, g_explosion_uvs);
    g_panel_sprites_alternate = 0;
}

/* name: inferred from its body, which loads \LANGUAGE\PNL1GFXU.BND (entry 0x6c) over the panel sprites once */
void panel_sprites_load_alternate(void)
{
    if (g_panel_sprites_alternate == 0) {
        sprite_file_load(FILE_PNL1GFXU_BND, &g_panel_page, g_panel_cluts, g_panel_uvs);
        g_panel_sprites_alternate = -1;
    }
}

/* name: inferred from its body, which appends a sprite record list and relocates its offsets, stopping on g_sprite_warn; string-only BSim pair dos 0x18ff8 shares the message 'TOO MANY SPRITES: INIT IMAGE ADDR' */
void sprite_list_add(const u8 *base, SpriteFrame *frames, s16 count)
{
    s16 index = g_sprite_list_count;
    SpriteFrame *cursor;
    g_sprite_list[index].count = count;
    g_sprite_list[index].frames = frames;
    if (index < SPRITE_LIST_MAX)
        g_sprite_list_count = index + 1;
    else
        fatal_error(g_sprite_warn);
    if (count != 0) {
        cursor = frames;
        do {
            cursor->pixels = (u_long *)((u32)cursor->pixels + (u32)base);
            count--;
            ++cursor;
        } while (count != 0);
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
u8 *sprite_chunk_add(const u8 *data, SpriteFrame *frames, short *script)
{
    u32 size;
    data += CHUNK_TAG_SIZE;
    size = read_be32(data);
    data += CHUNK_SIZE_FIELD;
    if (size & 1)
        ++size;
    sprite_list_add(data, frames, script[TRACK_SCRIPT_FRAMES]);
    return (u8 *)data + size;
}

/* name: runtime evidence, analysis/runtime/functions.json */
void sprite_list_reset(void)
{
    s16 count;
    SpriteFrame *cursor;
    u32 base;

    while (--g_sprite_list_count >= 0) {
        count = g_sprite_list[g_sprite_list_count].count;
        cursor = g_sprite_list[g_sprite_list_count].frames;
        base = (u32)cursor->pixels;
        if (count != 0) {
            do {
                cursor->pixels = (u_long *)((u32)cursor->pixels - base);
                count--;
                ++cursor;
            } while (count != 0);
        }
    }
    g_sprite_list_count = 0;
}

/* The shift is defined for IDs 1..14, and -1 returns zero. The caller's
 * lower input bound remains unproven: zero and values below -1 have a
 * hardware-masked shift in the reference but undefined C shift behavior.
 * An explicit &31 mask adds an instruction with the pinned compiler. */
int f_1d0e0(short index)
{
    if (index != -1 && index < 15)
        return 1 << (index - 1);
    return 0;
}

/* CLUT index from which sprite_resource_load sets the transparency bit of the weapon sets'
 * palettes (their last two colours). name invented */
#define SPRITE_WEAPON_STP_FIRST 0xfe

extern void sprite_resource_load(ResourceFile *resource, short transparent, short first_opaque);

/* Loads the level's sprite resources: the fixed sets, then one per
 * resource flag in the level header. */
/* name: runtime evidence, analysis/runtime/functions.json */
void level_sprites_load(void)
{
    unsigned short i;
    unsigned int resources;
    ClutRow *rows;

    for (i = 0; i < ARRAY_COUNT(g_actor_kind_map); i++)
        g_actor_kind_map[(short)i] = i;
    if (g_sprite_list_count != 0)
        sprite_list_reset();
    rows = g_clut_sets[CLUT_SET_SPRITES].rows;
    g_pulse_rifle_clut = &rows[CLUT_ROW_PULSE_RIFLE];
    g_pistol_clut = &rows[CLUT_ROW_PISTOL];
    g_flamethrower_clut = &rows[CLUT_ROW_FLAMETHROWER];
    g_smart_gun_clut = &rows[CLUT_ROW_SMART_GUN];
    g_shotgun_clut = &rows[CLUT_ROW_SHOTGUN];
    g_egg_clut = &rows[CLUT_ROW_EGG];
    g_facehugger_clut = &rows[CLUT_ROW_FACEHUGGER];
    g_alien_clut = &rows[CLUT_ROW_ALIEN];
    g_handler_clut = &rows[CLUT_ROW_HANDLER];
    g_burster_clut = &rows[CLUT_ROW_BURSTER];
    g_bambi_clut = &rows[CLUT_ROW_BAMBI];
    g_soldier_clut = &rows[CLUT_ROW_SOLDIER];
    g_guard_clut = &rows[CLUT_ROW_GUARD];
    g_synth_clut = &rows[CLUT_ROW_SYNTH];
    g_dog_clut = &rows[CLUT_ROW_DOG];
    g_fingers_clut = &rows[CLUT_ROW_FINGERS];
    g_queen_clut = &rows[CLUT_ROW_QUEEN];
    g_colonist_clut = &rows[CLUT_ROW_COLONIST];
    sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_SMART_GUN], -1, SPRITE_WEAPON_STP_FIRST);
    sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_PULSE_RIFLE], -1, SPRITE_WEAPON_STP_FIRST);
    sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_SHOTGUN], 0, 0);
    sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_PISTOL], -1, SPRITE_WEAPON_STP_FIRST);
    sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_FLAMETHROWER], 0, 0);
    resources = g_level_map.resources;
    if (resources & LEVEL_SPRITES_EGG)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_EGG], 0, 0);
    if (resources & LEVEL_SPRITES_HUGGER) {
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_HUGGER], 0, 0);
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_FINGERS], 0, 0);
    }
    if (resources & LEVEL_SPRITES_BURSTER)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_BURSTER], 0, 0);
    if (resources & LEVEL_SPRITES_COLONIST)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_COLONIST], 0, 0);
    if (resources & LEVEL_SPRITES_BAMBI)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_BAMBI], 0, 0);
    if (resources & LEVEL_SPRITES_DOG)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_DOG], 0, 0);
    if (resources & LEVEL_SPRITES_DOG_CEILING)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_DOG_CEILING], 0, 0);
    if (resources & LEVEL_SPRITES_WARRIOR)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_WARRIOR], 0, 0);
    if (resources & LEVEL_SPRITES_WARRIOR_CEILING)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_WARRIOR_CEILING], 0, 0);
    if (resources & LEVEL_SPRITES_GUARD)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_GUARD], 0, 0);
    if (resources & LEVEL_SPRITES_SOLDIER)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_SOLDIER], 0, 0);
    if (resources & LEVEL_SPRITES_SYNTH)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_SYNTH], 0, 0);
    if (resources & LEVEL_SPRITES_HANDLER)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_HANDLER], 0, 0);
    if (resources & LEVEL_SPRITES_QUEEN)
        sprite_resource_load(&g_sprite_resources[SPRITE_RESOURCE_QUEEN], 0, 0);
}
