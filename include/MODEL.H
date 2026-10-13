/*
 * Model primitive rows and the per-model info block they are drawn with. Each row is
 * drawn by one of the dispatch tables at 0x8006d308, 0x8006d394 and 0x8006d420, indexed
 * by selector, with a 16-byte palette entry and a 16-byte UV record. A model is lit
 * with the palette entry of the map cell it stands in (MapCell.palette).
 * Level faces (LevelMap.faces, MapCell.face) and door models (door_draw) are the same record.
 * Requires <sys/types.h>, <LIBGTE.H> and <LEVEL.H>. Names without evidence are marked invented.
 */
#ifndef MODEL_H
#define MODEL_H

#include <SPRITEUV.H>

typedef struct DispatchRow {
    SVECTOR *vertices[4]; /* level faces hold vertex indices until view_render_task or door_model_faces_fixup fixes them up */
    u_short count; /* invented name: index of the row's UV record in g_model_uvs */
    u_char flags; /* bit 7 ends the row list, bits 0..6 pick the palette entry */
    u_char selector; /* dispatch-table index; door_model_faces_fixup sets 0..2 from the face kind */
} DispatchRow;

/* DispatchRow.flags and .selector values (invented names): bit 7 ends a row list, the low
 * bits pick the palette entry; selectors 0 and 2 draw four-vertex rows, 1 three-vertex rows. */
#define ROW_FLAG_LAST 0x80
#define ROW_PALETTE_MASK 0x7f
enum {
    ROW_SELECT_QUAD = 0,
    ROW_SELECT_TRI = 1,
    ROW_SELECT_QUAD_ALT = 2,
    ROW_SELECT_QUAD_NOCULL = 4,    /* textured quad drawn from both sides (debris shards) */
    ROW_SELECT_QUAD_SEMITRANS = 12, /* semi-transparent textured quad (wall panels) */
    ROW_SELECT_TALL = 14 /* every row of the tall level object model (level_object_model_fixup) */
};
/* The palette entry door, lift and object models are lit with (the fixups of their rows), and
 * the offset to the tall level object model's entry. names: invented */
#define MODEL_PALETTE 10
#define MODEL_PALETTE_TALL_OFFSET 8

/* Face kind of a BND model row as the file stores it: the halfword over DispatchRow.flags and
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

/* A BND file (pickup_models_load, level_object_models_load): a header, a BND_COUNT_DIGITS-digit
 * decimal model count, then per model a separator byte, a BND_INDEX_DIGITS-digit model number, a
 * 32-bit big-endian length (BND_LENGTH_SIZE bytes) and the ModelBlock. names: invented */
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
 * 16-byte palette entry: four colours a model is lit with. lights_init fills entry i from
 * light record i; MapCell.palette and DispatchRow.flags & 0x7f index the table.
 * Requires <LIBGTE.H> for CVECTOR.
 */
typedef struct PaletteEntry {
    CVECTOR colors[4];
} PaletteEntry;

/* name: inferred from use: the palette entries models are lit with (DispatchRow.flags, MapCell.palette index it) */
extern PaletteEntry *g_palette;

/*
 * A model in a BND chunk: a 0x14-byte header, the rows, then the vertices the rows index
 * (pickup_models_load passes rows and vertices to object_model_faces_fixup; door_models_prepare and lift_models_prepare read the
 * same layout through byte pointers).
 */
typedef struct ModelBlock {
    char tag[4]; /* "OBJ1" in every model of the shipped BND files (file evidence; not read) */
    u_long unknown_04[2]; /* identity-TODO: no runtime reader at level load or in play (analysis/runtime, round 5); the first word is 0 in every shipped model, the second 0 or a 0x005axxxx value (OBJ3D.BND and the level MAP models; inferred: an address in the exporting tool's memory) */
    int row_count; /* invented name */
    int vertex_count; /* invented name: the vertices after the rows; block size = 0x14 + 20 x row_count + 8 x vertex_count in every shipped model (not read) */
    DispatchRow rows[1]; /* row_count rows, then the vertices */
} ModelBlock;

/* A row drawer: called with one row, its palette entry and its 16-byte UV record. The
 * handlers take what they use of those (some none), so the type leaves the arguments open. */
typedef void (*DispatchFn)();

/* name: inferred from use: the UV records rows are textured with (DispatchRow.count indexes it);
 * the draw code points it at g_sprite_uvs, g_panel_uvs or g_pickup_uvs */
extern SpriteUV *g_model_uvs;

/* The three row dispatch tables, indexed by DispatchRow.selector. */
extern DispatchFn g_near_draw_table[];
extern DispatchFn g_far_draw_table[];
extern DispatchFn g_mid_draw_table[];

/* Row-list draws: each draws rows through one dispatch table up to the row whose flags
 * has bit 7 set, lit by the row's own palette entry or by the map cell's. */
extern void model_draw_near(DispatchRow *row);
extern void model_draw_near_in_cell(DispatchRow *row, struct MapCell *cell);
extern void model_draw_far(DispatchRow *row);
extern void model_draw_far_in_cell(DispatchRow *row, struct MapCell *cell);
extern void model_draw_mid(DispatchRow *row);
extern void model_draw_mid_in_cell(DispatchRow *row, struct MapCell *cell);

/* debris panel UV records (debris.c allocates 0x800 bytes) */
extern SpriteUV *g_debris_uvs;

#endif
