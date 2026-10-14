/* model_draw.c -- model row drawing: the per-primitive handlers that the three row dispatch tables
 * (g_nearDrawTable, g_farDrawTable, g_midDrawTable, .data 0x8006d308..0x8006d4ac) point at, and the
 * walkers that run a model's rows through them */
/* Retail .text 0x8001d464..0x8001fca8: every function in the range is a table entry or a walker,
 * and the tables follow this module's predecessor's .data (g_menuResourceIds). Name invented. */
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
extern void Model_EmitFT4Fog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4MirrorFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4NoCullFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4NoCullMirrorFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4ABEFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4ABEMirrorFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4Mirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4NoCull( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4NoCullMirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4ABE( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT4ABEMirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_SubdivFT4( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_SubdivFT4Mirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_SubdivFT4NoCull( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_SubdivFT4NoCullMirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_SubdivFT4ABE( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_SubdivFT4ABEMirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_SubdivF4ABE( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT3Fog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT3AltFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT3NoCullFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT3AltNoCullFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitF4ABEFog( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT3( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT3Alt( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT3NoCull( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitFT3AltNoCull( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
extern void Model_EmitF4ABE( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );
/* Signature from its table slot, g_nearDrawTable entry 1 (name: analysis/runtime/functions.json) */
extern void Model_DrawNearFT3( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv );

extern long Model_OTIndexTri( void );

/* The scratchpad's start, where the near-triangle subdivision keeps its work areas */
#define SUBDIV_SCRATCH ( (subdivScratch_t *)getScratchAddr( 0 ) )

/* A split piece is drawn only past this screen depth; its ordering-table entry is its depth
 * shifted down by OT_DEPTH_SHIFT (DISPLAY.H). names: invented */
#define TRI_NEAR_DEPTH 32
static SVECTOR *g_spadGrid = SUBDIV_SCRATCH->grid;
static long    *g_spadSxy = SUBDIV_SCRATCH->sxy;
static u_short *g_spadSz = SUBDIV_SCRATCH->sz;
static u_short *g_spadFlag = SUBDIV_SCRATCH->flag;
static u_short *g_spadP = SUBDIV_SCRATCH->p;

/*
==================
Model_DrawNearFT4

Quad handlers: transform the four vertices into the next packet's
screen coordinates and draw only when some vertex is on screen.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    POLY_FT4 *poly = g_spriteWorkspace;

    if ( RotAverageNclip4( row->vertices[QUAD_VERTEX_BOTTOM_LEFT], row->vertices[QUAD_VERTEX_BOTTOM_RIGHT],
                           row->vertices[QUAD_VERTEX_TOP_RIGHT], row->vertices[QUAD_VERTEX_TOP_LEFT], (long *)&poly->x0,
                           (long *)&poly->x1, (long *)&poly->x3, (long *)&poly->x2, &g_gteInterpolation, &g_transformResult, &g_gteFlag )
         > 0 ) {
        if ( poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0 )
            return;
        if ( poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH )
            return;
        if ( poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0 )
            return;
        if ( poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT )
            return;
    }
    Model_SubdivFT4( row, palette, uv );
}

/*
==================
Model_DrawNearFT4Mirror
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4Mirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    POLY_FT4 *poly = g_spriteWorkspace;

    if ( RotAverageNclip4( row->vertices[QUAD_VERTEX_BOTTOM_LEFT], row->vertices[QUAD_VERTEX_BOTTOM_RIGHT],
                           row->vertices[QUAD_VERTEX_TOP_RIGHT], row->vertices[QUAD_VERTEX_TOP_LEFT], (long *)&poly->x0,
                           (long *)&poly->x1, (long *)&poly->x3, (long *)&poly->x2, &g_gteInterpolation, &g_transformResult, &g_gteFlag )
         > 0 ) {
        if ( poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0 )
            return;
        if ( poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH )
            return;
        if ( poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0 )
            return;
        if ( poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT )
            return;
    }
    Model_SubdivFT4Mirror( row, palette, uv );
}

/*
==================
Model_DrawNearFT3Alt
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT3Alt( void )
{
}

/*
==================
Model_DrawNearFT4NoCull
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4NoCull( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    POLY_FT4 *poly = g_spriteWorkspace;

    g_transformResult =
        RotTransPers4( row->vertices[QUAD_VERTEX_BOTTOM_LEFT], row->vertices[QUAD_VERTEX_BOTTOM_RIGHT],
                       row->vertices[QUAD_VERTEX_TOP_RIGHT], row->vertices[QUAD_VERTEX_TOP_LEFT], (long *)&poly->x0,
                       (long *)&poly->x1, (long *)&poly->x3, (long *)&poly->x2, &g_gteInterpolation, &g_gteFlag );
    if ( poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0 )
        return;
    if ( poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH )
        return;
    if ( poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0 )
        return;
    if ( poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT )
        return;
    Model_SubdivFT4NoCull( row, palette, uv );
}

/*
==================
Model_DrawNearFT3NoCull

Triangle handlers: a near textured triangle is split at its edge midpoints into one
quad and two triangles, each drawn only when its depth is past TRI_NEAR_DEPTH.
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT3NoCull( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    texturedPacket_t *poly = g_spriteWorkspace;
    u_long           *ot;
    int               u;
    int               v;
    int               u0;
    int               v0;

    g_transformResult = RotTransPers3( row->vertices[2], row->vertices[1], row->vertices[0], (long *)&poly->ft3.x0,
                                       (long *)&poly->ft3.x1, (long *)&poly->ft3.x2, &g_gteInterpolation, &g_gteFlag );
    if ( poly->ft3.x0 < 0 && poly->ft3.x1 < 0 && poly->ft3.x2 < 0 )
        return;
    if ( poly->ft3.x0 >= SCREEN_WIDTH && poly->ft3.x1 >= SCREEN_WIDTH && poly->ft3.x2 >= SCREEN_WIDTH )
        return;
    if ( poly->ft3.y0 < 0 && poly->ft3.y1 < 0 && poly->ft3.y2 < 0 )
        return;
    if ( poly->ft3.y0 >= SCREEN_HEIGHT && poly->ft3.y1 >= SCREEN_HEIGHT && poly->ft3.y2 >= SCREEN_HEIGHT )
        return;
    ( (long *)&g_spadGrid[TRI_GRID_A] )[0] = ( (long *)row->vertices[2] )[0];
    ( (long *)&g_spadGrid[TRI_GRID_A] )[1] = ( (long *)row->vertices[2] )[1];
    ( (long *)&g_spadGrid[TRI_GRID_B] )[0] = ( (long *)row->vertices[1] )[0];
    ( (long *)&g_spadGrid[TRI_GRID_B] )[1] = ( (long *)row->vertices[1] )[1];
    ( (long *)&g_spadGrid[TRI_GRID_C] )[0] = ( (long *)row->vertices[0] )[0];
    ( (long *)&g_spadGrid[TRI_GRID_C] )[1] = ( (long *)row->vertices[0] )[1];
    LoadAverageShort12( &g_spadGrid[TRI_GRID_A], &g_spadGrid[TRI_GRID_B], ONE / 2, ONE / 2, &g_spadGrid[TRI_GRID_AB] );
    LoadAverageShort12( &g_spadGrid[TRI_GRID_A], &g_spadGrid[TRI_GRID_C], ONE / 2, ONE / 2, &g_spadGrid[TRI_GRID_AC] );
    LoadAverageShort12( &g_spadGrid[TRI_GRID_B], &g_spadGrid[TRI_GRID_C], ONE / 2, ONE / 2, &g_spadGrid[TRI_GRID_BC] );
    RotTransPersN( g_spadGrid, (DVECTOR *)g_spadSxy, g_spadSz, g_spadP, g_spadFlag, TRI_GRID_POINTS );
    poly->word[PACKET_WORD_UV0] = ( (u_long *)uv )[0];
    poly->word[PACKET_WORD_UV1] = ( (u_long *)uv )[1];
    poly->word[PACKET_WORD_UV2] = ( (u_long *)uv )[2];
    /* The UV midpoints of the split edges; each block scopes its own temporaries. */
    {
        short d = poly->ft3.u1 - ( u0 = poly->ft3.u0 );
        short ad;
        if ( d >= 0 )
            ad = d;
        else
            ad = -d;
        u = u0 + ( (short)( ad + 1 ) >> 1 );
    }
    {
        short d = poly->ft3.v2 - ( v0 = poly->ft3.v0 );
        short ad;
        if ( d >= 0 )
            ad = d;
        else
            ad = -d;
        v = v0 + ( (short)( ad + 1 ) >> 1 );
    }
    if ( (short)g_spadSz[TRI_GRID_A] > TRI_NEAR_DEPTH ) {
        ot = g_renderBuffer + ( (short)g_spadSz[TRI_GRID_A] >> OT_DEPTH_SHIFT );
        *(long *)&poly->ft3.x0 = g_spadSxy[TRI_GRID_A];
        *(long *)&poly->ft3.x1 = g_spadSxy[TRI_GRID_AB];
        *(long *)&poly->ft3.x2 = g_spadSxy[TRI_GRID_AC];
        *(long *)&poly->ft4.x3 = g_spadSxy[TRI_GRID_BC];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ( (u_long *)uv )[0];
        poly->word[PACKET_WORD_UV1] = ( (u_long *)uv )[1];
        poly->word[PACKET_WORD_UV2] = ( (u_long *)uv )[2];
        poly->ft4.u1 = poly->ft4.u3 = u;
        poly->ft4.v2 = poly->ft4.v3 = v;
        setPolyFT4( &poly->ft4 );
        addPrim( ot, &poly->ft4 );
        poly = (texturedPacket_t *)( &poly->ft4 + 1 );
    }
    if ( (short)g_spadSz[TRI_GRID_AB] > TRI_NEAR_DEPTH ) {
        ot = g_renderBuffer + ( (short)g_spadSz[TRI_GRID_AB] >> OT_DEPTH_SHIFT );
        *(long *)&poly->ft3.x0 = g_spadSxy[TRI_GRID_AB];
        *(long *)&poly->ft3.x1 = g_spadSxy[TRI_GRID_B];
        *(long *)&poly->ft3.x2 = g_spadSxy[TRI_GRID_BC];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ( (u_long *)uv )[0];
        poly->word[PACKET_WORD_UV1] = ( (u_long *)uv )[1];
        poly->word[PACKET_WORD_UV2] = ( (u_long *)uv )[2];
        poly->ft3.u2 = poly->ft3.u0 = u;
        poly->ft3.v2 = v;
        setPolyFT3( &poly->ft3 );
        addPrim( ot, &poly->ft3 );
        poly = (texturedPacket_t *)( &poly->ft3 + 1 );
    }
    if ( (short)g_spadSz[TRI_GRID_AC] > TRI_NEAR_DEPTH ) {
        ot = g_renderBuffer + ( (short)g_spadSz[TRI_GRID_AC] >> OT_DEPTH_SHIFT );
        *(long *)&poly->ft3.x0 = g_spadSxy[TRI_GRID_AC];
        *(long *)&poly->ft3.x1 = g_spadSxy[TRI_GRID_BC];
        *(long *)&poly->ft3.x2 = g_spadSxy[TRI_GRID_C];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ( (u_long *)uv )[0];
        poly->word[PACKET_WORD_UV1] = ( (u_long *)uv )[1];
        poly->word[PACKET_WORD_UV2] = ( (u_long *)uv )[2];
        poly->ft3.v0 = poly->ft3.v1 = v;
        poly->ft3.u1 = u;
        setPolyFT3( &poly->ft3 );
        addPrim( ot, &poly->ft3 );
        poly = (texturedPacket_t *)( &poly->ft3 + 1 );
    }
    g_spriteWorkspace = poly;
}

/*
==================
Model_DrawNearFT4NoCullMirror
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4NoCullMirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    POLY_FT4 *poly = g_spriteWorkspace;

    g_transformResult =
        RotTransPers4( row->vertices[QUAD_VERTEX_BOTTOM_LEFT], row->vertices[QUAD_VERTEX_BOTTOM_RIGHT],
                       row->vertices[QUAD_VERTEX_TOP_RIGHT], row->vertices[QUAD_VERTEX_TOP_LEFT], (long *)&poly->x0,
                       (long *)&poly->x1, (long *)&poly->x3, (long *)&poly->x2, &g_gteInterpolation, &g_gteFlag );
    if ( poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0 )
        return;
    if ( poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH )
        return;
    if ( poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0 )
        return;
    if ( poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT )
        return;
    Model_SubdivFT4NoCullMirror( row, palette, uv );
}

/*
==================
Model_DrawNearFT3AltNoCull
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT3AltNoCull( void )
{
}

/*
==================
Model_DrawNearFT4ByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4ByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_DrawNearFT4( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawNearFT4MirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4MirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_DrawNearFT4Mirror( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawNearFT4ABE
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4ABE( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    POLY_FT4 *poly = g_spriteWorkspace;

    g_transformResult =
        RotTransPers4( row->vertices[QUAD_VERTEX_BOTTOM_LEFT], row->vertices[QUAD_VERTEX_BOTTOM_RIGHT],
                       row->vertices[QUAD_VERTEX_TOP_RIGHT], row->vertices[QUAD_VERTEX_TOP_LEFT], (long *)&poly->x0,
                       (long *)&poly->x1, (long *)&poly->x3, (long *)&poly->x2, &g_gteInterpolation, &g_gteFlag );
    if ( poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0 )
        return;
    if ( poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH )
        return;
    if ( poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0 )
        return;
    if ( poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT )
        return;
    Model_SubdivFT4ABE( row, palette, uv );
}

/*
==================
Model_DrawNearFT3ABE

Model_DrawNearFT3NoCull drawing semi-transparent primitives.
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT3ABE( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    texturedPacket_t *poly = g_spriteWorkspace;
    u_long           *ot;
    int               u;
    int               v;
    int               u0;
    int               v0;

    g_transformResult = RotTransPers3( row->vertices[2], row->vertices[1], row->vertices[0], (long *)&poly->ft3.x0,
                                       (long *)&poly->ft3.x1, (long *)&poly->ft3.x2, &g_gteInterpolation, &g_gteFlag );
    if ( poly->ft3.x0 < 0 && poly->ft3.x1 < 0 && poly->ft3.x2 < 0 )
        return;
    if ( poly->ft3.x0 >= SCREEN_WIDTH && poly->ft3.x1 >= SCREEN_WIDTH && poly->ft3.x2 >= SCREEN_WIDTH )
        return;
    if ( poly->ft3.y0 < 0 && poly->ft3.y1 < 0 && poly->ft3.y2 < 0 )
        return;
    if ( poly->ft3.y0 >= SCREEN_HEIGHT && poly->ft3.y1 >= SCREEN_HEIGHT && poly->ft3.y2 >= SCREEN_HEIGHT )
        return;
    ( (long *)&g_spadGrid[TRI_GRID_A] )[0] = ( (long *)row->vertices[2] )[0];
    ( (long *)&g_spadGrid[TRI_GRID_A] )[1] = ( (long *)row->vertices[2] )[1];
    ( (long *)&g_spadGrid[TRI_GRID_B] )[0] = ( (long *)row->vertices[1] )[0];
    ( (long *)&g_spadGrid[TRI_GRID_B] )[1] = ( (long *)row->vertices[1] )[1];
    ( (long *)&g_spadGrid[TRI_GRID_C] )[0] = ( (long *)row->vertices[0] )[0];
    ( (long *)&g_spadGrid[TRI_GRID_C] )[1] = ( (long *)row->vertices[0] )[1];
    LoadAverageShort12( &g_spadGrid[TRI_GRID_A], &g_spadGrid[TRI_GRID_B], ONE / 2, ONE / 2, &g_spadGrid[TRI_GRID_AB] );
    LoadAverageShort12( &g_spadGrid[TRI_GRID_A], &g_spadGrid[TRI_GRID_C], ONE / 2, ONE / 2, &g_spadGrid[TRI_GRID_AC] );
    LoadAverageShort12( &g_spadGrid[TRI_GRID_B], &g_spadGrid[TRI_GRID_C], ONE / 2, ONE / 2, &g_spadGrid[TRI_GRID_BC] );
    RotTransPersN( g_spadGrid, (DVECTOR *)g_spadSxy, g_spadSz, g_spadP, g_spadFlag, TRI_GRID_POINTS );
    poly->word[PACKET_WORD_UV0] = ( (u_long *)uv )[0];
    poly->word[PACKET_WORD_UV1] = ( (u_long *)uv )[1];
    poly->word[PACKET_WORD_UV2] = ( (u_long *)uv )[2];
    /* The UV midpoints of the split edges; each block scopes its own temporaries. */
    {
        short d = poly->ft3.u1 - ( u0 = poly->ft3.u0 );
        short ad;
        if ( d >= 0 )
            ad = d;
        else
            ad = -d;
        u = u0 + ( (short)( ad + 1 ) >> 1 );
    }
    {
        short d = poly->ft3.v2 - ( v0 = poly->ft3.v0 );
        short ad;
        if ( d >= 0 )
            ad = d;
        else
            ad = -d;
        v = v0 + ( (short)( ad + 1 ) >> 1 );
    }
    if ( (short)g_spadSz[TRI_GRID_A] > TRI_NEAR_DEPTH ) {
        ot = g_renderBuffer + ( (short)g_spadSz[TRI_GRID_A] >> OT_DEPTH_SHIFT );
        *(long *)&poly->ft3.x0 = g_spadSxy[TRI_GRID_A];
        *(long *)&poly->ft3.x1 = g_spadSxy[TRI_GRID_AB];
        *(long *)&poly->ft3.x2 = g_spadSxy[TRI_GRID_AC];
        *(long *)&poly->ft4.x3 = g_spadSxy[TRI_GRID_BC];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ( (u_long *)uv )[0];
        poly->word[PACKET_WORD_UV1] = ( (u_long *)uv )[1];
        poly->word[PACKET_WORD_UV2] = ( (u_long *)uv )[2];
        poly->ft4.u1 = poly->ft4.u3 = u;
        poly->ft4.v2 = poly->ft4.v3 = v;
        setPolyFT4( &poly->ft4 );
        setSemiTrans( &poly->ft4, 1 );
        addPrim( ot, &poly->ft4 );
        poly = (texturedPacket_t *)( &poly->ft4 + 1 );
    }
    if ( (short)g_spadSz[TRI_GRID_AB] > TRI_NEAR_DEPTH ) {
        ot = g_renderBuffer + ( (short)g_spadSz[TRI_GRID_AB] >> OT_DEPTH_SHIFT );
        *(long *)&poly->ft3.x0 = g_spadSxy[TRI_GRID_AB];
        *(long *)&poly->ft3.x1 = g_spadSxy[TRI_GRID_B];
        *(long *)&poly->ft3.x2 = g_spadSxy[TRI_GRID_BC];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ( (u_long *)uv )[0];
        poly->word[PACKET_WORD_UV1] = ( (u_long *)uv )[1];
        poly->word[PACKET_WORD_UV2] = ( (u_long *)uv )[2];
        poly->ft3.u2 = poly->ft3.u0 = u;
        poly->ft3.v2 = v;
        setPolyFT3( &poly->ft3 );
        setSemiTrans( &poly->ft3, 1 );
        addPrim( ot, &poly->ft3 );
        poly = (texturedPacket_t *)( &poly->ft3 + 1 );
    }
    if ( (short)g_spadSz[TRI_GRID_AC] > TRI_NEAR_DEPTH ) {
        ot = g_renderBuffer + ( (short)g_spadSz[TRI_GRID_AC] >> OT_DEPTH_SHIFT );
        *(long *)&poly->ft3.x0 = g_spadSxy[TRI_GRID_AC];
        *(long *)&poly->ft3.x1 = g_spadSxy[TRI_GRID_BC];
        *(long *)&poly->ft3.x2 = g_spadSxy[TRI_GRID_C];
        poly->word[PACKET_WORD_COLOR] = *(u_long *)&palette->colors[0];
        poly->word[PACKET_WORD_UV0] = ( (u_long *)uv )[0];
        poly->word[PACKET_WORD_UV1] = ( (u_long *)uv )[1];
        poly->word[PACKET_WORD_UV2] = ( (u_long *)uv )[2];
        poly->ft3.v0 = poly->ft3.v1 = v;
        poly->ft3.u1 = u;
        setPolyFT3( &poly->ft3 );
        setSemiTrans( &poly->ft3, 1 );
        addPrim( ot, &poly->ft3 );
        poly = (texturedPacket_t *)( &poly->ft3 + 1 );
    }
    g_spriteWorkspace = poly;
}

/*
==================
Model_DrawNearF4ABE
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearF4ABE( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    POLY_F4 *poly = g_spriteWorkspace;

    g_transformResult =
        RotTransPers4( row->vertices[QUAD_VERTEX_BOTTOM_LEFT], row->vertices[QUAD_VERTEX_BOTTOM_RIGHT],
                       row->vertices[QUAD_VERTEX_TOP_RIGHT], row->vertices[QUAD_VERTEX_TOP_LEFT], (long *)&poly->x0,
                       (long *)&poly->x1, (long *)&poly->x3, (long *)&poly->x2, &g_gteInterpolation, &g_gteFlag );
    if ( poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0 )
        return;
    if ( poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH )
        return;
    if ( poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0 )
        return;
    if ( poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT )
        return;
    Model_SubdivF4ABE( row, palette, uv );
}

/*
==================
Model_DrawNearFT4NoCullByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4NoCullByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_DrawNearFT4NoCull( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawNearFT4NoCullMirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4NoCullMirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_DrawNearFT4NoCullMirror( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawNearFT4ABEByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4ABEByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_DrawNearFT4ABE( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawNearFT4ABEMirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4ABEMirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_DrawNearFT4ABEMirror( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawNearFT4ABEMirror
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearFT4ABEMirror( dispatchRow_t *row, paletteEntry_t *palette, spriteUV_t *uv )
{
    POLY_FT4 *poly = g_spriteWorkspace;

    g_transformResult =
        RotTransPers4( row->vertices[QUAD_VERTEX_BOTTOM_LEFT], row->vertices[QUAD_VERTEX_BOTTOM_RIGHT],
                       row->vertices[QUAD_VERTEX_TOP_RIGHT], row->vertices[QUAD_VERTEX_TOP_LEFT], (long *)&poly->x0,
                       (long *)&poly->x1, (long *)&poly->x3, (long *)&poly->x2, &g_gteInterpolation, &g_gteFlag );
    if ( poly->x0 < 0 && poly->x1 < 0 && poly->x2 < 0 && poly->x3 < 0 )
        return;
    if ( poly->x0 >= SCREEN_WIDTH && poly->x1 >= SCREEN_WIDTH && poly->x2 >= SCREEN_WIDTH && poly->x3 >= SCREEN_WIDTH )
        return;
    if ( poly->y0 < 0 && poly->y1 < 0 && poly->y2 < 0 && poly->y3 < 0 )
        return;
    if ( poly->y0 >= SCREEN_HEIGHT && poly->y1 >= SCREEN_HEIGHT && poly->y2 >= SCREEN_HEIGHT && poly->y3 >= SCREEN_HEIGHT )
        return;
    Model_SubdivFT4ABEMirror( row, palette, uv );
}

/*
==================
Model_DrawNearNone
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawNearNone( void )
{
}

/*
==================
Model_DrawNear
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawNear( dispatchRow_t *row )
{
    do {
        g_nearDrawTable[row->selector]( row, &g_palette[row->flags & ROW_PALETTE_MASK], &g_modelUvs[row->count] );
    } while ( ( row++->flags & ROW_FLAG_LAST ) == 0 );
}

/*
==================
Model_DrawNearInCell
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawNearInCell( dispatchRow_t *row, mapCell_t *cell )
{
    paletteEntry_t *palette = &g_palette[cell->palette];
    do {
        g_nearDrawTable[row->selector]( row, palette, &g_modelUvs[row->count] );
    } while ( ( row++->flags & ROW_FLAG_LAST ) == 0 );
}

/*
==================
Model_DrawFarFT4ByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawFarFT4ByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4Fog( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawFarFT4MirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawFarFT4MirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4MirrorFog( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawFarFT3ABE
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawFarFT3ABE( dispatchRow_t *row, packetWord_t *source, long *record )
{
    packetPrefix_t *packet = (packetPrefix_t *)g_spriteWorkspace;
    long            vertex = RotTransPers3( row->vertices[2], row->vertices[1], row->vertices[0],
                                            &packet->word[PACKET_WORD_XY0].xy, &packet->word[PACKET_WORD_XY1].xy,
                                            &packet->word[PACKET_WORD_XY2].xy, &g_gteInterpolation, &g_gteFlag );
    long            byteOffset = vertex << 2;
    g_transformResult = byteOffset;
    if ( byteOffset < DEPTH_CUE_START ) {
        packet->word[PACKET_WORD_COLOR].value = source->value;
    } else {
        byteOffset -= DEPTH_CUE_START;
        g_transformResult = byteOffset;
        DpqColor( &source->color, byteOffset, &packet->word[PACKET_WORD_COLOR].color );
    }
    packet->word[PACKET_WORD_UV0].value = record[0];
    packet->word[PACKET_WORD_UV1].value = record[1];
    packet->word[PACKET_WORD_UV2].value = record[2];
    {
        record = (long *)g_renderBuffer + Model_OTIndexTri();
        setPolyFT3( packet );
        setSemiTrans( packet, 1 );
        addPrim( record, packet );
        packet++;
        g_spriteWorkspace = packet;
    }
}

/*
==================
Model_DrawFarFT4NoCullByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawFarFT4NoCullByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4NoCullFog( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawFarFT4NoCullMirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawFarFT4NoCullMirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4NoCullMirrorFog( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawFarFT4ABEByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawFarFT4ABEByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4ABEFog( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawFarFT4ABEMirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawFarFT4ABEMirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4ABEMirrorFog( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawFarNone
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawFarNone( void )
{
}

/*
==================
Model_DrawFar
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawFar( dispatchRow_t *row )
{
    do {
        g_farDrawTable[row->selector]( row, &g_palette[row->flags & ROW_PALETTE_MASK], &g_modelUvs[row->count] );
    } while ( ( row++->flags & ROW_FLAG_LAST ) == 0 );
}

/*
==================
Model_DrawFarInCell
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawFarInCell( dispatchRow_t *row, mapCell_t *cell )
{
    paletteEntry_t *palette = &g_palette[cell->palette];
    do {
        g_farDrawTable[row->selector]( row, palette, &g_modelUvs[row->count] );
    } while ( ( row++->flags & ROW_FLAG_LAST ) == 0 );
}

/*
==================
Model_DrawMidFT4ByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawMidFT4ByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawMidFT4MirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawMidFT4MirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4Mirror( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawMidFT3ABE
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawMidFT3ABE( dispatchRow_t *row, u_long *color, u_long *uv )
{
    POLY_FT3 *poly = g_spriteWorkspace;

    g_transformResult = RotTransPers3( row->vertices[2], row->vertices[1], row->vertices[0], (long *)&poly->x0,
                                       (long *)&poly->x1, (long *)&poly->x2, &g_gteInterpolation, &g_gteFlag );
    *(u_long *)&poly->r0 = *color;
    *(u_long *)&poly->u0 = uv[0];
    *(u_long *)&poly->u1 = uv[1];
    *(u_long *)&poly->u2 = uv[2];
    uv = &g_renderBuffer[Model_OTIndexTri()];
    setPolyFT3( poly );
    setSemiTrans( poly, 1 );
    addPrim( uv, poly );
    poly++;
    g_spriteWorkspace = poly;
}

/*
==================
Model_DrawMidFT4NoCullByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawMidFT4NoCullByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4NoCull( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawMidFT4NoCullMirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawMidFT4NoCullMirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4NoCullMirror( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawMidFT4ABEByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawMidFT4ABEByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4ABE( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawMidFT4ABEMirrorByState
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawMidFT4ABEMirrorByState( dispatchRow_t *row, paletteEntry_t *palette )
{
    Model_EmitFT4ABEMirror( row, palette, &g_modelUvs[g_commandStates[row->count].value] );
}

/*
==================
Model_DrawMidNone
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Model_DrawMidNone( void )
{
}

/*
==================
Model_DrawMid
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawMid( dispatchRow_t *row )
{
    do {
        g_midDrawTable[row->selector]( row, &g_palette[row->flags & ROW_PALETTE_MASK], &g_modelUvs[row->count] );
    } while ( ( row++->flags & ROW_FLAG_LAST ) == 0 );
}

/*
==================
Model_DrawMidInCell
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Model_DrawMidInCell( dispatchRow_t *row, mapCell_t *cell )
{
    paletteEntry_t *palette = &g_palette[cell->palette];
    do {
        g_midDrawTable[row->selector]( row, palette, &g_modelUvs[row->count] );
    } while ( ( row++->flags & ROW_FLAG_LAST ) == 0 );
}

/* The three row dispatch tables, indexed by dispatchRow_t.selector (<MODEL.H>): 6d308 for
 * Model_DrawNear/Model_DrawNearInCell, 6d394 for Model_DrawFar/Model_DrawFarInCell (particle draws entries 4 and 12), 6d420
 * for Model_DrawMid/Model_DrawMidInCell. Unused selectors hold the empty handlers. */
/* name: analysis/runtime/globals.json */
/* name: analysis/runtime/globals.json */
dispatchFn_t g_nearDrawTable[] = { Model_DrawNearFT4,
                                   Model_DrawNearFT3,
                                   Model_DrawNearFT4Mirror,
                                   Model_DrawNearFT3Alt,
                                   Model_DrawNearFT4NoCull,
                                   Model_DrawNearFT3NoCull,
                                   Model_DrawNearFT4NoCullMirror,
                                   Model_DrawNearFT3AltNoCull,
                                   Model_DrawNearFT4ByState,
                                   Model_DrawNearNone,
                                   Model_DrawNearFT4MirrorByState,
                                   Model_DrawNearNone,
                                   Model_DrawNearFT4ABE,
                                   Model_DrawNearFT3ABE,
                                   Model_DrawNearF4ABE,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearNone,
                                   Model_DrawNearFT4NoCullByState,
                                   Model_DrawNearNone,
                                   Model_DrawNearFT4NoCullMirrorByState,
                                   Model_DrawNearNone,
                                   Model_DrawNearFT4ABEByState,
                                   Model_DrawNearNone,
                                   Model_DrawNearFT4ABEMirrorByState,
                                   Model_DrawNearNone,
                                   Model_DrawNearFT4ABEMirror };

/* name: analysis/runtime/globals.json */
/* name: analysis/runtime/globals.json */
dispatchFn_t g_farDrawTable[] = { Model_EmitFT4Fog,
                                  Model_EmitFT3Fog,
                                  Model_EmitFT4MirrorFog,
                                  Model_EmitFT3AltFog,
                                  Model_EmitFT4NoCullFog,
                                  Model_EmitFT3NoCullFog,
                                  Model_EmitFT4NoCullMirrorFog,
                                  Model_EmitFT3AltNoCullFog,
                                  Model_DrawFarFT4ByState,
                                  Model_DrawFarNone,
                                  Model_DrawFarFT4MirrorByState,
                                  Model_DrawFarNone,
                                  Model_EmitFT4ABEFog,
                                  Model_DrawFarFT3ABE,
                                  Model_EmitF4ABEFog,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarNone,
                                  Model_DrawFarFT4NoCullByState,
                                  Model_DrawFarNone,
                                  Model_DrawFarFT4NoCullMirrorByState,
                                  Model_DrawFarNone,
                                  Model_DrawFarFT4ABEByState,
                                  Model_DrawFarNone,
                                  Model_DrawFarFT4ABEMirrorByState,
                                  Model_DrawFarNone,
                                  Model_EmitFT4ABEMirrorFog };

/* name: analysis/runtime/globals.json */
/* name: analysis/runtime/globals.json */
dispatchFn_t g_midDrawTable[] = { Model_EmitFT4,
                                  Model_EmitFT3,
                                  Model_EmitFT4Mirror,
                                  Model_EmitFT3Alt,
                                  Model_EmitFT4NoCull,
                                  Model_EmitFT3NoCull,
                                  Model_EmitFT4NoCullMirror,
                                  Model_EmitFT3AltNoCull,
                                  Model_DrawMidFT4ByState,
                                  Model_DrawMidNone,
                                  Model_DrawMidFT4MirrorByState,
                                  Model_DrawMidNone,
                                  Model_EmitFT4ABE,
                                  Model_DrawMidFT3ABE,
                                  Model_EmitF4ABE,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidNone,
                                  Model_DrawMidFT4NoCullByState,
                                  Model_DrawMidNone,
                                  Model_DrawMidFT4NoCullMirrorByState,
                                  Model_DrawMidNone,
                                  Model_DrawMidFT4ABEByState,
                                  Model_DrawMidNone,
                                  Model_DrawMidFT4ABEMirrorByState,
                                  Model_DrawMidNone,
                                  Model_EmitFT4ABEMirror };
