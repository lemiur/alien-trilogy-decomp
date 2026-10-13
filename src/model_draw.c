/* Model row drawing: the per-primitive handlers that the three row dispatch tables
 * (g_near_draw_table, g_far_draw_table, g_mid_draw_table, .data 0x8006d308..0x8006d4ac) point at, and
 * the walkers that run a model's rows through them. Retail .text
 * 0x8001d464..0x8001fca8: every function in the range is a table entry or a walker,
 * and the tables follow this module's predecessor's .data (g_menu_resource_ids).
 * Handlers not yet matched are absent. Name invented.
 */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LIBETC.H>
#include <DISPLAY.H>
#include <LEVEL.H>
#include <SPRITEUV.H>
#include <MODEL.H>
#include <VIEW.H>
#include <COMMAND.H>

/* Handlers in the hand-written GTE/renderer region. */
extern void emit_ft4_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_mirror_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_nocull_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_nocull_mirror_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_abe_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_abe_mirror_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_nocull(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_nocull_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_abe(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft4_abe_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void subdiv_ft4(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void subdiv_ft4_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void subdiv_ft4_nocull(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void subdiv_ft4_nocull_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void subdiv_ft4_abe(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void subdiv_ft4_abe_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void subdiv_f4_abe(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft3_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft3_alt_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft3_nocull_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft3_alt_nocull_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_f4_abe_fog(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft3(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft3_alt(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft3_nocull(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_ft3_alt_nocull(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
extern void emit_f4_abe(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);
/* Not matched yet; signature from its table slot, g_near_draw_table entry 1 (name: analysis/runtime/functions.json) */
extern void draw_near_ft3(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv);

typedef struct {
    SVECTOR *vertex0, *vertex1, *vertex2;
} Triangle;
extern long ot_index_tri(void);


typedef struct TransformInputPrefix { SVECTOR *vertex0, *vertex1, *vertex2; } TransformInputPrefix;
typedef union PacketWord { long value; long xy; CVECTOR color; } PacketWord;
typedef struct PacketPrefix { PacketWord word[sizeof(POLY_FT3) / sizeof(PacketWord)]; } PacketPrefix;

/* Words of a textured packet (POLY_FT3, and POLY_FT4's first eight): the tag, then colour and
 * code, then each vertex's screen position and its UV (with the CLUT, then the tpage, in the
 * first two). names: invented */
enum PacketWordIndex {
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
typedef struct SubdivScratch {
    SVECTOR grid[SUBDIV_GRID_COUNT];
    long sxy[SUBDIV_GRID_COUNT];
    u_short sz[SUBDIV_GRID_COUNT];
    u_short flag[SUBDIV_GRID_COUNT];
    u_short p[SUBDIV_GRID_COUNT];
} SubdivScratch;
#define SUBDIV_SCRATCH ((SubdivScratch *)getScratchAddr(0))

/* The near-triangle split: the row's three corners and the midpoints of its edges, as
 * grid points, row by row: A AB B, AC BC, C. names: invented */
enum TriGridPoint {
    TRI_GRID_A,     /* vertices[2] */
    TRI_GRID_AB,
    TRI_GRID_B,     /* vertices[1] */
    TRI_GRID_AC,
    TRI_GRID_BC,
    TRI_GRID_C,     /* vertices[0] */
    TRI_GRID_POINTS
};
/* A split piece is drawn only past this screen depth; its ordering-table entry is its depth
 * shifted down by OT_DEPTH_SHIFT (DISPLAY.H). names: invented */
#define TRI_NEAR_DEPTH 32
static SVECTOR *g_spad_grid = SUBDIV_SCRATCH->grid;
static long *g_spad_sxy = SUBDIV_SCRATCH->sxy;
static u_short *g_spad_sz = SUBDIV_SCRATCH->sz;
static u_short *g_spad_flag = SUBDIV_SCRATCH->flag;
static u_short *g_spad_p = SUBDIV_SCRATCH->p;

/* A textured packet seen as either primitive or as words. */
typedef union TexturedPacket {
    POLY_FT3 ft3;
    POLY_FT4 ft4;
    u_long word[sizeof(POLY_FT4) / sizeof(u_long)];
} TexturedPacket;

/* Quad handlers: transform the four vertices into the next packet's
 * screen coordinates and draw only when some vertex is on screen. */
/* name: runtime evidence, analysis/runtime/functions.json */
void draw_near_ft4(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    POLY_FT4 *poly = g_sprite_workspace;

    if (RotAverageNclip4(row->vertices[3], row->vertices[2], row->vertices[1], row->vertices[0],
                         (long *)&poly->x0, (long *)&poly->x1,
                         (long *)&poly->x3, (long *)&poly->x2,
                         &g_gte_interpolation, &g_transform_result, &g_gte_flag) > 0) {
        if (poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0)
            return;
        if (poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH)
            return;
        if (poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0)
            return;
        if (poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT)
            return;
    }
    subdiv_ft4(row, palette, uv);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void draw_near_ft4_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    POLY_FT4 *poly = g_sprite_workspace;

    if (RotAverageNclip4(row->vertices[3], row->vertices[2], row->vertices[1], row->vertices[0],
                         (long *)&poly->x0, (long *)&poly->x1,
                         (long *)&poly->x3, (long *)&poly->x2,
                         &g_gte_interpolation, &g_transform_result, &g_gte_flag) > 0) {
        if (poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0)
            return;
        if (poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH)
            return;
        if (poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0)
            return;
        if (poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT)
            return;
    }
    subdiv_ft4_mirror(row, palette, uv);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft3_alt(void)
{
}

/* name: runtime evidence, analysis/runtime/functions.json */
void draw_near_ft4_nocull(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    POLY_FT4 *poly = g_sprite_workspace;

    g_transform_result = RotTransPers4(row->vertices[3], row->vertices[2], row->vertices[1], row->vertices[0],
                                       (long *)&poly->x0, (long *)&poly->x1,
                                       (long *)&poly->x3, (long *)&poly->x2,
                                       &g_gte_interpolation, &g_gte_flag);
    if (poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0)
        return;
    if (poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH)
        return;
    if (poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0)
        return;
    if (poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT)
        return;
    subdiv_ft4_nocull(row, palette, uv);
}

/* Triangle handlers: a near textured triangle is split at its edge midpoints into one
 * quad and two triangles, each drawn only when its depth is past TRI_NEAR_DEPTH. */
/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft3_nocull(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    TexturedPacket *poly = g_sprite_workspace;
    u_long *ot;
    int u;
    int v;
    int u0;
    int v0;

    g_transform_result = RotTransPers3(row->vertices[2], row->vertices[1], row->vertices[0],
                                       (long *)&poly->ft3.x0, (long *)&poly->ft3.x1,
                                       (long *)&poly->ft3.x2, &g_gte_interpolation, &g_gte_flag);
    if (poly->ft3.x0 < 0 && poly->ft3.x1 < 0 && poly->ft3.x2 < 0)
        return;
    if (poly->ft3.x0 >= SCREEN_WIDTH && poly->ft3.x1 >= SCREEN_WIDTH && poly->ft3.x2 >= SCREEN_WIDTH)
        return;
    if (poly->ft3.y0 < 0 && poly->ft3.y1 < 0 && poly->ft3.y2 < 0)
        return;
    if (poly->ft3.y0 >= SCREEN_HEIGHT && poly->ft3.y1 >= SCREEN_HEIGHT && poly->ft3.y2 >= SCREEN_HEIGHT)
        return;
    ((long *)&g_spad_grid[TRI_GRID_A])[0] = ((long *)row->vertices[2])[0];
    ((long *)&g_spad_grid[TRI_GRID_A])[1] = ((long *)row->vertices[2])[1];
    ((long *)&g_spad_grid[TRI_GRID_B])[0] = ((long *)row->vertices[1])[0];
    ((long *)&g_spad_grid[TRI_GRID_B])[1] = ((long *)row->vertices[1])[1];
    ((long *)&g_spad_grid[TRI_GRID_C])[0] = ((long *)row->vertices[0])[0];
    ((long *)&g_spad_grid[TRI_GRID_C])[1] = ((long *)row->vertices[0])[1];
    LoadAverageShort12(&g_spad_grid[TRI_GRID_A], &g_spad_grid[TRI_GRID_B], ONE / 2, ONE / 2, &g_spad_grid[TRI_GRID_AB]);
    LoadAverageShort12(&g_spad_grid[TRI_GRID_A], &g_spad_grid[TRI_GRID_C], ONE / 2, ONE / 2, &g_spad_grid[TRI_GRID_AC]);
    LoadAverageShort12(&g_spad_grid[TRI_GRID_B], &g_spad_grid[TRI_GRID_C], ONE / 2, ONE / 2, &g_spad_grid[TRI_GRID_BC]);
    RotTransPersN(g_spad_grid, (DVECTOR *)g_spad_sxy, g_spad_sz, g_spad_p, g_spad_flag, TRI_GRID_POINTS);
    poly->word[PACKET_WORD_UV0] = ((u_long *)uv)[0];
    poly->word[PACKET_WORD_UV1] = ((u_long *)uv)[1];
    poly->word[PACKET_WORD_UV2] = ((u_long *)uv)[2];
    /* The UV midpoints of the split edges; each block scopes its own temporaries. */
    {
        short d = poly->ft3.u1 - (u0 = poly->ft3.u0);
        short ad;
        if (d >= 0)
            ad = d;
        else
            ad = -d;
        u = u0 + ((short)(ad + 1) >> 1);
    }
    {
        short d = poly->ft3.v2 - (v0 = poly->ft3.v0);
        short ad;
        if (d >= 0)
            ad = d;
        else
            ad = -d;
        v = v0 + ((short)(ad + 1) >> 1);
    }
    if ((short)g_spad_sz[TRI_GRID_A] > TRI_NEAR_DEPTH) {
        ot = g_render_buffer + ((short)g_spad_sz[TRI_GRID_A] >> OT_DEPTH_SHIFT);
        *(long *)&poly->ft3.x0 = g_spad_sxy[TRI_GRID_A];
        *(long *)&poly->ft3.x1 = g_spad_sxy[TRI_GRID_AB];
        *(long *)&poly->ft3.x2 = g_spad_sxy[TRI_GRID_AC];
        *(long *)&poly->ft4.x3 = g_spad_sxy[TRI_GRID_BC];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ((u_long *)uv)[0];
        poly->word[PACKET_WORD_UV1] = ((u_long *)uv)[1];
        poly->word[PACKET_WORD_UV2] = ((u_long *)uv)[2];
        poly->ft4.u1 = poly->ft4.u3 = u;
        poly->ft4.v2 = poly->ft4.v3 = v;
        setPolyFT4(&poly->ft4);
        addPrim(ot, &poly->ft4);
        poly = (TexturedPacket *)(&poly->ft4 + 1);
    }
    if ((short)g_spad_sz[TRI_GRID_AB] > TRI_NEAR_DEPTH) {
        ot = g_render_buffer + ((short)g_spad_sz[TRI_GRID_AB] >> OT_DEPTH_SHIFT);
        *(long *)&poly->ft3.x0 = g_spad_sxy[TRI_GRID_AB];
        *(long *)&poly->ft3.x1 = g_spad_sxy[TRI_GRID_B];
        *(long *)&poly->ft3.x2 = g_spad_sxy[TRI_GRID_BC];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ((u_long *)uv)[0];
        poly->word[PACKET_WORD_UV1] = ((u_long *)uv)[1];
        poly->word[PACKET_WORD_UV2] = ((u_long *)uv)[2];
        poly->ft3.u2 = poly->ft3.u0 = u;
        poly->ft3.v2 = v;
        setPolyFT3(&poly->ft3);
        addPrim(ot, &poly->ft3);
        poly = (TexturedPacket *)(&poly->ft3 + 1);
    }
    if ((short)g_spad_sz[TRI_GRID_AC] > TRI_NEAR_DEPTH) {
        ot = g_render_buffer + ((short)g_spad_sz[TRI_GRID_AC] >> OT_DEPTH_SHIFT);
        *(long *)&poly->ft3.x0 = g_spad_sxy[TRI_GRID_AC];
        *(long *)&poly->ft3.x1 = g_spad_sxy[TRI_GRID_BC];
        *(long *)&poly->ft3.x2 = g_spad_sxy[TRI_GRID_C];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ((u_long *)uv)[0];
        poly->word[PACKET_WORD_UV1] = ((u_long *)uv)[1];
        poly->word[PACKET_WORD_UV2] = ((u_long *)uv)[2];
        poly->ft3.v0 = poly->ft3.v1 = v;
        poly->ft3.u1 = u;
        setPolyFT3(&poly->ft3);
        addPrim(ot, &poly->ft3);
        poly = (TexturedPacket *)(&poly->ft3 + 1);
    }
    g_sprite_workspace = poly;
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_nocull_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    POLY_FT4 *poly = g_sprite_workspace;

    g_transform_result = RotTransPers4(row->vertices[3], row->vertices[2], row->vertices[1], row->vertices[0],
                                       (long *)&poly->x0, (long *)&poly->x1,
                                       (long *)&poly->x3, (long *)&poly->x2,
                                       &g_gte_interpolation, &g_gte_flag);
    if (poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0)
        return;
    if (poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH)
        return;
    if (poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0)
        return;
    if (poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT)
        return;
    subdiv_ft4_nocull_mirror(row, palette, uv);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft3_alt_nocull(void)
{
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_by_state(DispatchRow *row, PaletteEntry *palette)
{
    draw_near_ft4(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    draw_near_ft4_mirror(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_abe(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    POLY_FT4 *poly = g_sprite_workspace;

    g_transform_result = RotTransPers4(row->vertices[3], row->vertices[2], row->vertices[1], row->vertices[0],
                                       (long *)&poly->x0, (long *)&poly->x1,
                                       (long *)&poly->x3, (long *)&poly->x2,
                                       &g_gte_interpolation, &g_gte_flag);
    if (poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0)
        return;
    if (poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH)
        return;
    if (poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0)
        return;
    if (poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT)
        return;
    subdiv_ft4_abe(row, palette, uv);
}

/* draw_near_ft3_nocull drawing semi-transparent primitives. */
/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft3_abe(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    TexturedPacket *poly = g_sprite_workspace;
    u_long *ot;
    int u;
    int v;
    int u0;
    int v0;

    g_transform_result = RotTransPers3(row->vertices[2], row->vertices[1], row->vertices[0],
                                       (long *)&poly->ft3.x0, (long *)&poly->ft3.x1,
                                       (long *)&poly->ft3.x2, &g_gte_interpolation, &g_gte_flag);
    if (poly->ft3.x0 < 0 && poly->ft3.x1 < 0 && poly->ft3.x2 < 0)
        return;
    if (poly->ft3.x0 >= SCREEN_WIDTH && poly->ft3.x1 >= SCREEN_WIDTH && poly->ft3.x2 >= SCREEN_WIDTH)
        return;
    if (poly->ft3.y0 < 0 && poly->ft3.y1 < 0 && poly->ft3.y2 < 0)
        return;
    if (poly->ft3.y0 >= SCREEN_HEIGHT && poly->ft3.y1 >= SCREEN_HEIGHT && poly->ft3.y2 >= SCREEN_HEIGHT)
        return;
    ((long *)&g_spad_grid[TRI_GRID_A])[0] = ((long *)row->vertices[2])[0];
    ((long *)&g_spad_grid[TRI_GRID_A])[1] = ((long *)row->vertices[2])[1];
    ((long *)&g_spad_grid[TRI_GRID_B])[0] = ((long *)row->vertices[1])[0];
    ((long *)&g_spad_grid[TRI_GRID_B])[1] = ((long *)row->vertices[1])[1];
    ((long *)&g_spad_grid[TRI_GRID_C])[0] = ((long *)row->vertices[0])[0];
    ((long *)&g_spad_grid[TRI_GRID_C])[1] = ((long *)row->vertices[0])[1];
    LoadAverageShort12(&g_spad_grid[TRI_GRID_A], &g_spad_grid[TRI_GRID_B], ONE / 2, ONE / 2, &g_spad_grid[TRI_GRID_AB]);
    LoadAverageShort12(&g_spad_grid[TRI_GRID_A], &g_spad_grid[TRI_GRID_C], ONE / 2, ONE / 2, &g_spad_grid[TRI_GRID_AC]);
    LoadAverageShort12(&g_spad_grid[TRI_GRID_B], &g_spad_grid[TRI_GRID_C], ONE / 2, ONE / 2, &g_spad_grid[TRI_GRID_BC]);
    RotTransPersN(g_spad_grid, (DVECTOR *)g_spad_sxy, g_spad_sz, g_spad_p, g_spad_flag, TRI_GRID_POINTS);
    poly->word[PACKET_WORD_UV0] = ((u_long *)uv)[0];
    poly->word[PACKET_WORD_UV1] = ((u_long *)uv)[1];
    poly->word[PACKET_WORD_UV2] = ((u_long *)uv)[2];
    /* The UV midpoints of the split edges; each block scopes its own temporaries. */
    {
        short d = poly->ft3.u1 - (u0 = poly->ft3.u0);
        short ad;
        if (d >= 0)
            ad = d;
        else
            ad = -d;
        u = u0 + ((short)(ad + 1) >> 1);
    }
    {
        short d = poly->ft3.v2 - (v0 = poly->ft3.v0);
        short ad;
        if (d >= 0)
            ad = d;
        else
            ad = -d;
        v = v0 + ((short)(ad + 1) >> 1);
    }
    if ((short)g_spad_sz[TRI_GRID_A] > TRI_NEAR_DEPTH) {
        ot = g_render_buffer + ((short)g_spad_sz[TRI_GRID_A] >> OT_DEPTH_SHIFT);
        *(long *)&poly->ft3.x0 = g_spad_sxy[TRI_GRID_A];
        *(long *)&poly->ft3.x1 = g_spad_sxy[TRI_GRID_AB];
        *(long *)&poly->ft3.x2 = g_spad_sxy[TRI_GRID_AC];
        *(long *)&poly->ft4.x3 = g_spad_sxy[TRI_GRID_BC];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ((u_long *)uv)[0];
        poly->word[PACKET_WORD_UV1] = ((u_long *)uv)[1];
        poly->word[PACKET_WORD_UV2] = ((u_long *)uv)[2];
        poly->ft4.u1 = poly->ft4.u3 = u;
        poly->ft4.v2 = poly->ft4.v3 = v;
        setPolyFT4(&poly->ft4);
        setSemiTrans(&poly->ft4, 1);
        addPrim(ot, &poly->ft4);
        poly = (TexturedPacket *)(&poly->ft4 + 1);
    }
    if ((short)g_spad_sz[TRI_GRID_AB] > TRI_NEAR_DEPTH) {
        ot = g_render_buffer + ((short)g_spad_sz[TRI_GRID_AB] >> OT_DEPTH_SHIFT);
        *(long *)&poly->ft3.x0 = g_spad_sxy[TRI_GRID_AB];
        *(long *)&poly->ft3.x1 = g_spad_sxy[TRI_GRID_B];
        *(long *)&poly->ft3.x2 = g_spad_sxy[TRI_GRID_BC];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ((u_long *)uv)[0];
        poly->word[PACKET_WORD_UV1] = ((u_long *)uv)[1];
        poly->word[PACKET_WORD_UV2] = ((u_long *)uv)[2];
        poly->ft3.u2 = poly->ft3.u0 = u;
        poly->ft3.v2 = v;
        setPolyFT3(&poly->ft3);
        setSemiTrans(&poly->ft3, 1);
        addPrim(ot, &poly->ft3);
        poly = (TexturedPacket *)(&poly->ft3 + 1);
    }
    if ((short)g_spad_sz[TRI_GRID_AC] > TRI_NEAR_DEPTH) {
        ot = g_render_buffer + ((short)g_spad_sz[TRI_GRID_AC] >> OT_DEPTH_SHIFT);
        *(long *)&poly->ft3.x0 = g_spad_sxy[TRI_GRID_AC];
        *(long *)&poly->ft3.x1 = g_spad_sxy[TRI_GRID_BC];
        *(long *)&poly->ft3.x2 = g_spad_sxy[TRI_GRID_C];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ((u_long *)uv)[0];
        poly->word[PACKET_WORD_UV1] = ((u_long *)uv)[1];
        poly->word[PACKET_WORD_UV2] = ((u_long *)uv)[2];
        poly->ft3.v0 = poly->ft3.v1 = v;
        poly->ft3.u1 = u;
        setPolyFT3(&poly->ft3);
        setSemiTrans(&poly->ft3, 1);
        addPrim(ot, &poly->ft3);
        poly = (TexturedPacket *)(&poly->ft3 + 1);
    }
    g_sprite_workspace = poly;
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_f4_abe(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    POLY_F4 *poly = g_sprite_workspace;

    g_transform_result = RotTransPers4(row->vertices[3], row->vertices[2], row->vertices[1], row->vertices[0],
                                       (long *)&poly->x0, (long *)&poly->x1,
                                       (long *)&poly->x3, (long *)&poly->x2,
                                       &g_gte_interpolation, &g_gte_flag);
    if (poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0)
        return;
    if (poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH)
        return;
    if (poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0)
        return;
    if (poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT)
        return;
    subdiv_f4_abe(row, palette, uv);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_nocull_by_state(DispatchRow *row, PaletteEntry *palette)
{
    draw_near_ft4_nocull(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_nocull_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    draw_near_ft4_nocull_mirror(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_abe_by_state(DispatchRow *row, PaletteEntry *palette)
{
    draw_near_ft4_abe(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_abe_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    draw_near_ft4_abe_mirror(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_ft4_abe_mirror(DispatchRow *row, PaletteEntry *palette, SpriteUV *uv)
{
    POLY_FT4 *poly = g_sprite_workspace;

    g_transform_result = RotTransPers4(row->vertices[3], row->vertices[2], row->vertices[1], row->vertices[0],
                                       (long *)&poly->x0, (long *)&poly->x1,
                                       (long *)&poly->x3, (long *)&poly->x2,
                                       &g_gte_interpolation, &g_gte_flag);
    if (poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0)
        return;
    if (poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH)
        return;
    if (poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0)
        return;
    if (poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT)
        return;
    subdiv_ft4_abe_mirror(row, palette, uv);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_near_none(void)
{
}

/* name: runtime evidence, analysis/runtime/functions.json */
void model_draw_near(DispatchRow *row)
{
    do {
        g_near_draw_table[row->selector](row,
            &g_palette[row->flags & ROW_PALETTE_MASK],
            &g_model_uvs[row->count]);
    } while ((row++->flags & ROW_FLAG_LAST) == 0);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void model_draw_near_in_cell(DispatchRow *row, MapCell *cell)
{
    PaletteEntry *palette = &g_palette[cell->palette];
    do {
        g_near_draw_table[row->selector](row, palette,
            &g_model_uvs[row->count]);
    } while ((row++->flags & ROW_FLAG_LAST) == 0);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_far_ft4_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_fog(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_far_ft4_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_mirror_fog(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_far_ft3_abe(TransformInputPrefix *input, PacketWord *source, long *record)
{
    PacketPrefix *packet = (PacketPrefix *)g_sprite_workspace;
    long vertex = RotTransPers3(input->vertex2, input->vertex1, input->vertex0,
                                &packet->word[PACKET_WORD_XY0].xy, &packet->word[PACKET_WORD_XY1].xy,
                                &packet->word[PACKET_WORD_XY2].xy, &g_gte_interpolation, &g_gte_flag);
    long byte_offset = vertex << 2;
    g_transform_result = byte_offset;
    if (byte_offset < DEPTH_CUE_START) {
        packet->word[PACKET_WORD_COLOR].value = source->value;
    } else {
        byte_offset -= DEPTH_CUE_START;
        g_transform_result = byte_offset;
        DpqColor(&source->color, byte_offset, &packet->word[PACKET_WORD_COLOR].color);
    }
    packet->word[PACKET_WORD_UV0].value = record[0];
    packet->word[PACKET_WORD_UV1].value = record[1];
    packet->word[PACKET_WORD_UV2].value = record[2];
    {
        record = (long *)g_render_buffer + ot_index_tri();
        setPolyFT3(packet);
        setSemiTrans(packet, 1);
        addPrim(record, packet);
        packet++;
        g_sprite_workspace = packet;
    }
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_far_ft4_nocull_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_nocull_fog(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_far_ft4_nocull_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_nocull_mirror_fog(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_far_ft4_abe_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_abe_fog(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_far_ft4_abe_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_abe_mirror_fog(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_far_none(void)
{
}

/* name: runtime evidence, analysis/runtime/functions.json */
void model_draw_far(DispatchRow *row)
{
    do {
        g_far_draw_table[row->selector](row,
            &g_palette[row->flags & ROW_PALETTE_MASK],
            &g_model_uvs[row->count]);
    } while ((row++->flags & ROW_FLAG_LAST) == 0);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void model_draw_far_in_cell(DispatchRow *row, MapCell *cell)
{
    PaletteEntry *palette = &g_palette[cell->palette];
    do {
        g_far_draw_table[row->selector](row, palette,
            &g_model_uvs[row->count]);
    } while ((row++->flags & ROW_FLAG_LAST) == 0);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_mid_ft4_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_mid_ft4_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_mirror(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_mid_ft3_abe(Triangle *triangle, u_long *color, u_long *uv)
{
    POLY_FT3 *poly = g_sprite_workspace;

    g_transform_result = RotTransPers3(triangle->vertex2, triangle->vertex1, triangle->vertex0,
                                       (long *)&poly->x0, (long *)&poly->x1, (long *)&poly->x2,
                                       &g_gte_interpolation, &g_gte_flag);
    *(u_long *)&poly->r0 = *color;
    *(u_long *)&poly->u0 = uv[0];
    *(u_long *)&poly->u1 = uv[1];
    *(u_long *)&poly->u2 = uv[2];
    uv = &g_render_buffer[ot_index_tri()];
    setPolyFT3(poly);
    setSemiTrans(poly, 1);
    addPrim(uv, poly);
    poly++;
    g_sprite_workspace = poly;
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_mid_ft4_nocull_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_nocull(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_mid_ft4_nocull_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_nocull_mirror(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_mid_ft4_abe_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_abe(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_mid_ft4_abe_mirror_by_state(DispatchRow *row, PaletteEntry *palette)
{
    emit_ft4_abe_mirror(row, palette, &g_model_uvs[g_command_states[row->count].value]);
}

/* name: static evidence, analysis/runtime/functions.json */
void draw_mid_none(void)
{
}

/* name: runtime evidence, analysis/runtime/functions.json */
void model_draw_mid(DispatchRow *row)
{
    do {
        g_mid_draw_table[row->selector](row,
            &g_palette[row->flags & ROW_PALETTE_MASK],
            &g_model_uvs[row->count]);
    } while ((row++->flags & ROW_FLAG_LAST) == 0);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void model_draw_mid_in_cell(DispatchRow *row, MapCell *cell)
{
    PaletteEntry *palette = &g_palette[cell->palette];
    do {
        g_mid_draw_table[row->selector](row, palette,
                    &g_model_uvs[row->count]);
    } while ((row++->flags & ROW_FLAG_LAST) == 0);
}

/* The three row dispatch tables, indexed by DispatchRow.selector (<MODEL.H>): 6d308 for
 * model_draw_near/model_draw_near_in_cell, 6d394 for model_draw_far/model_draw_far_in_cell (particle draws entries 4 and 12), 6d420
 * for model_draw_mid/model_draw_mid_in_cell. Unused selectors hold the empty handlers. */
/* name: analysis/runtime/globals.json (round 2) */
/* name: analysis/runtime/globals.json (round 2) */
DispatchFn g_near_draw_table[] = {
    draw_near_ft4, draw_near_ft3, draw_near_ft4_mirror, draw_near_ft3_alt, draw_near_ft4_nocull,
    draw_near_ft3_nocull, draw_near_ft4_nocull_mirror, draw_near_ft3_alt_nocull, draw_near_ft4_by_state, draw_near_none,
    draw_near_ft4_mirror_by_state, draw_near_none, draw_near_ft4_abe, draw_near_ft3_abe, draw_near_f4_abe,
    draw_near_none, draw_near_none, draw_near_none, draw_near_none, draw_near_none,
    draw_near_none, draw_near_none, draw_near_none, draw_near_none, draw_near_none,
    draw_near_none, draw_near_ft4_nocull_by_state, draw_near_none, draw_near_ft4_nocull_mirror_by_state, draw_near_none,
    draw_near_ft4_abe_by_state, draw_near_none, draw_near_ft4_abe_mirror_by_state, draw_near_none, draw_near_ft4_abe_mirror
};

/* name: analysis/runtime/globals.json (round 2) */
/* name: analysis/runtime/globals.json (round 2) */
DispatchFn g_far_draw_table[] = {
    emit_ft4_fog, emit_ft3_fog, emit_ft4_mirror_fog, emit_ft3_alt_fog, emit_ft4_nocull_fog,
    emit_ft3_nocull_fog, emit_ft4_nocull_mirror_fog, emit_ft3_alt_nocull_fog, draw_far_ft4_by_state, draw_far_none,
    draw_far_ft4_mirror_by_state, draw_far_none, emit_ft4_abe_fog, draw_far_ft3_abe, emit_f4_abe_fog,
    draw_far_none, draw_far_none, draw_far_none, draw_far_none, draw_far_none,
    draw_far_none, draw_far_none, draw_far_none, draw_far_none, draw_far_none,
    draw_far_none, draw_far_ft4_nocull_by_state, draw_far_none, draw_far_ft4_nocull_mirror_by_state, draw_far_none,
    draw_far_ft4_abe_by_state, draw_far_none, draw_far_ft4_abe_mirror_by_state, draw_far_none, emit_ft4_abe_mirror_fog
};

/* name: analysis/runtime/globals.json (round 2) */
/* name: analysis/runtime/globals.json (round 2) */
DispatchFn g_mid_draw_table[] = {
    emit_ft4, emit_ft3, emit_ft4_mirror, emit_ft3_alt, emit_ft4_nocull,
    emit_ft3_nocull, emit_ft4_nocull_mirror, emit_ft3_alt_nocull, draw_mid_ft4_by_state, draw_mid_none,
    draw_mid_ft4_mirror_by_state, draw_mid_none, emit_ft4_abe, draw_mid_ft3_abe, emit_f4_abe,
    draw_mid_none, draw_mid_none, draw_mid_none, draw_mid_none, draw_mid_none,
    draw_mid_none, draw_mid_none, draw_mid_none, draw_mid_none, draw_mid_none,
    draw_mid_none, draw_mid_ft4_nocull_by_state, draw_mid_none, draw_mid_ft4_nocull_mirror_by_state, draw_mid_none,
    draw_mid_ft4_abe_by_state, draw_mid_none, draw_mid_ft4_abe_mirror_by_state, draw_mid_none, emit_ft4_abe_mirror
};
