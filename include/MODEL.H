/*
 * Model primitive rows and the per-model info block they are drawn with. Each row is
 * drawn by one of the dispatch tables at 0x8006d308, 0x8006d394 and 0x8006d420, indexed
 * by selector, with a 16-byte palette entry and a 16-byte UV record. A model is lit
 * with the palette entry of the map cell it stands in (mapCell_t.palette).
 * Level faces (levelMap_t.faces, mapCell_t.face) and door models (Door_Draw) are the same record.
 * Requires <sys/types.h>, <LIBGTE.H> and <LEVEL.H>; the packet views below need <LIBGPU.H> (included). Names without evidence are marked invented.
 */
#ifndef MODEL_H
#define MODEL_H

#include <LIBGPU.H>
#include <SPRITEUV.H>

/* Vertex slots of a row: four, or three with the last unused (QUAD_VERTEX_* in VIEW.H order them).
 * name: invented */
#define ROW_VERTICES 4

typedef struct dispatchRow_s {
    SVECTOR *vertices[ROW_VERTICES]; /* level faces hold vertex indices until ViewRenderTask or Door_FixupModelFaces fixes them up */
    u_short count;                   /* invented name: index of the row's UV record in g_modelUvs */
    u_char  flags;                   /* bit 7 ends the row list, bits 0..6 pick the palette entry */
    u_char  selector;                /* dispatch-table index; Door_FixupModelFaces sets 0..2 from the face kind */
} dispatchRow_t;

/* A row as a BND model stores it: vertex indices into the model's vertices and the 16-bit
 * face kind over dispatchRow_t.flags and .selector. The faces fixups rewrite each modelRow_t in
 * place from its file view to its dispatchRow_t view. names: invented */
typedef struct fileRow_s {
    int     vertices[ROW_VERTICES]; /* indices into the vertices after the rows */
    u_short count;                  /* dispatchRow_t.count, unchanged */
    u_short kind;                   /* FACE_KIND_*, ROW_FLAG_LAST on the last row */
} fileRow_t;

typedef union modelRow_s {
    fileRow_t     file;
    dispatchRow_t row;
} modelRow_t;

/* dispatchRow_t.flags and .selector values (invented names): bit 7 ends a row list, the low
 * bits pick the palette entry; selectors 0 and 2 draw four-vertex rows, 1 three-vertex rows. */
#define ROW_FLAG_LAST 0x80
#define ROW_PALETTE_MASK 0x7f
enum {
    ROW_SELECT_QUAD = 0,
    ROW_SELECT_TRI = 1,
    ROW_SELECT_QUAD_ALT = 2,
    ROW_SELECT_QUAD_NOCULL = 4,     /* textured quad drawn from both sides (debris shards) */
    ROW_SELECT_QUAD_SEMITRANS = 12, /* semi-transparent textured quad (wall panels) */
    ROW_SELECT_TALL = 14            /* every row of the tall level object model (Object_FixupLevelObjectModel) */
};
/* The palette entry door, lift and object models are lit with (the fixups of their rows), and
 * the offset to the tall level object model's entry. names: invented */
#define MODEL_PALETTE 10
#define MODEL_PALETTE_TALL_OFFSET 8

/* Face kind of a BND model row as the file stores it: the halfword over dispatchRow_t.flags and
 * .selector (bit 7 ends the list) until the faces fixups split it into flags and a selector.
 * PICKMOD.BND and OBJ3D.BND hold only FACE_KIND_QUAD, FACE_KIND_TRIANGLE (vertices[3] is -1)
 * and FACE_KIND_QUAD_MIRROR (drawn by ROW_SELECT_QUAD_ALT, the mirrored quad handlers); the
 * fixups also take kind FACE_KIND_QUAD_OTHER as a quad and FACE_KIND_TRIANGLE..
 * FACE_KIND_TRIANGLE_LAST as triangles. names: invented */
enum {
    FACE_KIND_QUAD = 0,
    FACE_KIND_QUAD_OTHER = 1, /* identity-TODO: not in the shipped models */
    FACE_KIND_TRIANGLE = 2,
    FACE_KIND_TRIANGLE_LAST = 10,
    FACE_KIND_QUAD_MIRROR = 11
};

/* A BND file (Object_LoadPickupModels, Object_LoadLevelObjectModels): a header, a BND_COUNT_DIGITS-digit
 * decimal model count, then per model a separator byte, a BND_INDEX_DIGITS-digit model number, a
 * 32-bit big-endian length (BND_LENGTH_SIZE bytes) and the modelBlock_t. names: invented */
#define BND_HEADER_SIZE 8
#define BND_COUNT_DIGITS 4
#define BND_INDEX_DIGITS 3
#define BND_LENGTH_SIZE 4

/* View depth (and width) bands of a model draw: beyond MODEL_CULL_DISTANCE it is not drawn,
 * from MODEL_FAR_DISTANCE it uses the far rows, below MODEL_NEAR_DISTANCE (or the closer
 * MODEL_CLOSE_DISTANCE for tall and unscaled level objects) the near rows. names: invented */
#define MODEL_CULL_DISTANCE 0x2000
#define MODEL_FAR_DISTANCE 0x1000
#define MODEL_NEAR_DISTANCE 0x800
#define MODEL_CLOSE_DISTANCE 0x400

/*
 * 16-byte palette entry: four colours a model is lit with. Light_Init fills entry i from
 * light record i; mapCell_t.palette and dispatchRow_t.flags & 0x7f index the table.
 * Requires <LIBGTE.H> for CVECTOR.
 */
#define PALETTE_COLORS 4 /* name: invented */
typedef struct paletteEntry_s {
    CVECTOR colors[PALETTE_COLORS];
} paletteEntry_t;

/* name: inferred from use: the palette entries models are lit with (dispatchRow_t.flags, mapCell_t.palette index it) */
extern paletteEntry_t *g_palette;

/*
 * A model in a BND chunk: a 0x14-byte header, the rows, then the vertices the rows index
 * (Object_LoadPickupModels passes rows and vertices to Object_FixupModelFaces; Door_PrepareModels and Lift_PrepareModels read the
 * same layout through byte pointers).
 */
#define MODEL_TAG_SIZE 4 /* name: invented */
typedef struct modelBlock_s {
    char tag[MODEL_TAG_SIZE]; /* "OBJ1" in every model of the shipped BND files (file evidence; not read) */
    u_long unknown_04[2]; /* identity-TODO: no runtime reader at level load or in play (analysis/runtime); the first word is 0 in every shipped model, the second 0 or a 0x005axxxx value (OBJ3D.BND and the level MAP models; inferred: an address in the exporting tool's memory) */
    int rowCount;         /* invented name */
    int vertexCount; /* invented name: the vertices after the rows; block size = 0x14 + 20 x rowCount + 8 x vertexCount in every shipped model (not read) */
    modelRow_t rows[1]; /* rowCount rows, then the vertices */
} modelBlock_t;

/* A row drawer: called with one row, its palette entry and its 16-byte UV record. The
 * handlers take what they use of those (some none), so the type leaves the arguments open. */
typedef void ( *dispatchFn_t )();

/* name: inferred from use: the UV records rows are textured with (dispatchRow_t.count indexes it);
 * the draw code points it at g_spriteUvs, g_panelUvs or g_pickupUvs */
extern spriteUV_t *g_modelUvs;

/* The three row dispatch tables, indexed by dispatchRow_t.selector. */
extern dispatchFn_t g_nearDrawTable[];
extern dispatchFn_t g_farDrawTable[];
extern dispatchFn_t g_midDrawTable[];

/* Row-list draws: each draws rows through one dispatch table up to the row whose flags
 * has bit 7 set, lit by the row's own palette entry or by the map cell's. */
extern void Model_DrawNear( dispatchRow_t *row );
extern void Model_DrawNearInCell( dispatchRow_t *row, struct mapCell_s *cell );
extern void Model_DrawFar( dispatchRow_t *row );
extern void Model_DrawFarInCell( dispatchRow_t *row, struct mapCell_s *cell );
extern void Model_DrawMid( dispatchRow_t *row );
extern void Model_DrawMidInCell( dispatchRow_t *row, struct mapCell_s *cell );

/* debris panel UV records (debris.c allocates 0x800 bytes) */
extern spriteUV_t *g_debrisUvs;

/* Packet views of the row handlers (model_draw.c) */
typedef union packetWord_s {
    long    value;
    long    xy;
    CVECTOR color;
} packetWord_t;
typedef struct packetPrefix_s {
    packetWord_t word[sizeof( POLY_FT3 ) / sizeof( packetWord_t )];
} packetPrefix_t;

/* Words of a textured packet (POLY_FT3, and POLY_FT4's first eight): the tag, then colour and
 * code, then each vertex's screen position and its UV (with the CLUT, then the tpage, in the
 * first two). names: invented */
enum packetWordIndex_t {
    PACKET_WORD_COLOR = 1,
    PACKET_WORD_XY0,
    PACKET_WORD_UV0,
    PACKET_WORD_XY1,
    PACKET_WORD_UV1,
    PACKET_WORD_XY2,
    PACKET_WORD_UV2
};

/* Scratchpad work areas of the near-triangle subdivision (.sdata 0x8008cf88..0x8008cf9c;
 * the layout is in asm/subdiv.h). names invented: the 3x2 vertex grid (corners and edge
 * midpoints), its screen coordinates, depths, interpolation values and flags. The
 * arrays are sized for the 5x5 grid of the hand-written renderers (SPAD_* in asm/subdiv.h). */
#define SUBDIV_GRID_COUNT 25
typedef struct subdivScratch_s {
    SVECTOR grid[SUBDIV_GRID_COUNT];
    long    sxy[SUBDIV_GRID_COUNT];
    u_short sz[SUBDIV_GRID_COUNT];
    u_short flag[SUBDIV_GRID_COUNT];
    u_short p[SUBDIV_GRID_COUNT];
} subdivScratch_t;

/* The near-triangle split: the row's three corners and the midpoints of its edges, as
 * grid points, row by row: A AB B, AC BC, C. names: invented */
enum triGridPoint_t {
    TRI_GRID_A, /* vertices[2] */
    TRI_GRID_AB,
    TRI_GRID_B, /* vertices[1] */
    TRI_GRID_AC,
    TRI_GRID_BC,
    TRI_GRID_C, /* vertices[0] */
    TRI_GRID_POINTS
};

/* A textured packet seen as either primitive or as words. */
typedef union texturedPacket_s {
    POLY_FT3 ft3;
    POLY_FT4 ft4;
    u_long   word[sizeof( POLY_FT4 ) / sizeof( u_long )];
} texturedPacket_t;

#endif
