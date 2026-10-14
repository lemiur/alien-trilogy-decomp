/*
 * poly_emit.s - GTE polygon emitters: transform, cull, depth-cue and add one primitive
 * to the ordering table.
 *
 * Hand-written assembly (AGENTS.md, "Hand-written assembly"): GTE/renderer region only.
 * Built by tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/poly_emit.json.
 *
 * Twenty-two routines in two groups of eleven. Each group is reached through its own
 * function table, with one C wrapper per table entry:
 *   - 0x8006d394.. (g_farDrawTable): colour depth-cued with DpqColor; the
 *     primitive is built through a3;
 *   - 0x8006d420.. (g_dispatch_table, indexed by Model_DrawMidInCell): colour word stored unchanged,
 *     fetched on entry; the primitive is built through a0.
 * Within a group the variants are: textured quad or triangle, with or without back-face
 * culling, plain or left-right mirrored texture, triangles taking texture corners 0,1,2
 * or 3,1,2 (the two halves of a quad), and semi-transparent quads (textured and flat).
 *
 * Every routine follows the same plan:
 *   1. call a libgte "_nom" transform (RTPT, plus RTPS for a quad), which leaves screen
 *      x,y in the SXY FIFO and z in the SZ FIFO; culling variants test NCLIP;
 *   2. take the primitive at g_spriteWorkspace and store the screen coordinates;
 *   3. OT entry = g_renderBuffer + 4 * (max z >> OT_SHIFT), the computation of
 *      Model_OTIndexTri / Model_OTIndexQuad (ot_depth.s) written inline;
 *   4. colour, addPrim, GPU code and length, texture words;
 *   5. g_spriteWorkspace += primitive size.
 * The libgte routines called here use only a0..a3, v0, v1 and the GTE, so the emitters
 * keep values in t registers across those calls, which compiled C would not do.
 *
 * ".set noreorder": every delay slot is written out. ASPSX's "lw reg, symbol" macro
 * expands to lui reg + lw (no AT); "sw reg, symbol" to lui AT + sw through AT. AT is
 * therefore left enabled except where the code itself uses it as a comparison scratch.
 */
#include "gte.h"

/* POLY_F4 (Runtime Library 3.3 libgpu.h): no texture words, so the corners pack tighter */
#define F4_XY0		8
#define F4_XY1		12
#define F4_XY2		16
#define F4_XY3		20

	.text
	.set	noreorder

/*
 * Model_EmitFT4Fog - emit a POLY_FT4 (textured quad)
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), drop the polygon when it faces away (NCLIP <= 0), fill a POLY_FT4
 *             at the primitive allocation pointer g_spriteWorkspace, depth-cue its colour
 *             with DpqColor beyond z = DEPTH_CUE_START, link it into the ordering table
 *             g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t5/t6.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO (NormalClipS leaves v1 alone).
 * Scratchpad: none.
 * Callers:    g_farDrawTable[0] (pointer at 0x8006d394); jal from Model_DrawFarFT4ByState
 *             (0x8001f4d8).
 * Callees:    RotTransPers4_nom, NormalClipS, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4Fog
Model_EmitFT4Fog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t0, a1			/* t0 = colour pointer */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Back-face cull: emit only when the signed screen area is positive */
	jal	NormalClipS		/* NCLIP on SXY0..SXY2, v0 = MAC0 */
	nop				/* branch delay: nothing to do */
	.set	noat
	slt	AT, zero, v0		/* AT as scratch: area > 0 ? */
	beqz	AT, $48fd8_done		/* facing away: emit nothing */
	.set	at

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive; macro: its lui fills */
					/* the beqz delay slot (harmless when taken) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a3)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a3)
	sw	t3, P_XY3(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $48fd8_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$48fd8_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $48fd8_max1
	li	t6, CODE_POLY_FT4	/* delay slot (real work): GPU code */
	move	v0, t3
$48fd8_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $48fd8_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$48fd8_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $48fd8_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$48fd8_linked
	nop				/* branch delay: nothing to do */
$48fd8_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$48fd8_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a3)		/* tag length */
	sb	t6, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)
	sw	t3, P_UV3(a3)

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT4_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$48fd8_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT3Fog - emit a POLY_FT3 (textured triangle)
 *
 * Purpose:    transform the three vertices with RotNclip3_nom (RTPT), drop the polygon when it
 *             faces away (NCLIP <= 0), fill a POLY_FT3 at the primitive allocation pointer
 *             g_spriteWorkspace, depth-cue its colour with DpqColor beyond z =
 *             DEPTH_CUE_START, link it into the ordering table g_renderBuffer at its farthest
 *             z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 3 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t4/t5.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[1] (pointer at 0x8006d398).
 * Callees:    RotNclip3_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT3Fog
Model_EmitFT3Fog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t1, a2			/* t1 = texture record */
	move	t0, a1			/* t0 = colour pointer */

	/* RotNclip3_nom(v[2], v[1], v[0]) */
	lw	a2, 0(a0)
	lw	a1, 4(a0)
	lw	a0, 8(a0)
	jal	RotNclip3_nom		/* RTPT, then NCLIP */
	nop				/* branch delay: nothing to do */

	/* Back-face cull: emit only when the signed screen area is positive */
	mfc2	v0, C2_MAC0		/* v0 = NCLIP result */
	nop				/* GTE transfer delay */
	.set	noat
	slt	AT, zero, v0		/* AT as scratch: area > 0 ? */
	beqz	AT, $4910c_done		/* facing away: emit nothing */
	.set	at

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive; macro: its lui fills */
					/* the beqz delay slot (harmless when taken) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	t2, P_XY0(a3)
	sw	t3, P_XY1(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ1
	mfc2	t2, C2_SZ2
	mfc2	t3, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4910c_max0
	li	t4, POLY_FT3_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4910c_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4910c_max1
	li	t5, CODE_POLY_FT3	/* delay slot (real work): GPU code */
	move	v0, t3
$4910c_max1:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $4910c_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$4910c_linked
	nop				/* branch delay: nothing to do */
$4910c_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$4910c_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t4, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t4, P_LEN(a3)		/* tag length */
	sb	t5, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT3_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4910c_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4MirrorFog - emit a POLY_FT4 (textured quad), texture mirrored left-right
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), drop the polygon when it faces away (NCLIP <= 0), fill a POLY_FT4
 *             at the primitive allocation pointer g_spriteWorkspace, depth-cue its colour
 *             with DpqColor beyond z = DEPTH_CUE_START, link it into the ordering table
 *             g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t5/t6.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO (NormalClipS leaves v1 alone).
 * Scratchpad: none.
 * Callers:    g_farDrawTable[2] (pointer at 0x8006d39c); jal from Model_DrawFarFT4MirrorByState
 *             (0x8001f51c).
 * Callees:    RotTransPers4_nom, NormalClipS, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4MirrorFog
Model_EmitFT4MirrorFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t0, a1			/* t0 = colour pointer */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Back-face cull: emit only when the signed screen area is positive */
	jal	NormalClipS		/* NCLIP on SXY0..SXY2, v0 = MAC0 */
	nop				/* branch delay: nothing to do */
	.set	noat
	slt	AT, zero, v0		/* AT as scratch: area > 0 ? */
	beqz	AT, $4921c_done		/* facing away: emit nothing */
	.set	at

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive; macro: its lui fills */
					/* the beqz delay slot (harmless when taken) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a3)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a3)
	sw	t3, P_XY3(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4921c_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4921c_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4921c_max1
	li	t6, CODE_POLY_FT4	/* delay slot (real work): GPU code */
	move	v0, t3
$4921c_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $4921c_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$4921c_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $4921c_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$4921c_linked
	nop				/* branch delay: nothing to do */
$4921c_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$4921c_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a3)		/* tag length */
	sb	t6, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)
	sw	t3, P_UV3(a3)

	/* Mirror left-right: swap u of the left (0,2) and right (1,3) corners */
	lb	t4, P_UV0(a3)		/* t4 = u0 */
	lb	t5, P_UV1(a3)		/* t5 = u1 */
	sb	t4, P_UV3(a3)		/* u3 = u0 */
	sb	t4, P_UV1(a3)		/* u1 = u0 */
	sb	t5, P_UV0(a3)		/* u0 = u1 */
	sb	t5, P_UV2(a3)		/* u2 = u1 */

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT4_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4921c_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT3AltFog - emit a POLY_FT3 (textured triangle), texture corners 3,1,2
 *
 * Purpose:    transform the three vertices with RotNclip3_nom (RTPT), drop the polygon when it
 *             faces away (NCLIP <= 0), fill a POLY_FT3 at the primitive allocation pointer
 *             g_spriteWorkspace, depth-cue its colour with DpqColor beyond z =
 *             DEPTH_CUE_START, link it into the ordering table g_renderBuffer at its farthest
 *             z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 3 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t4/t5.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[3] (pointer at 0x8006d3a0).
 * Callees:    RotNclip3_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT3AltFog
Model_EmitFT3AltFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t1, a2			/* t1 = texture record */
	move	t0, a1			/* t0 = colour pointer */

	/* RotNclip3_nom(v[2], v[1], v[0]) */
	lw	a2, 0(a0)
	lw	a1, 4(a0)
	lw	a0, 8(a0)
	jal	RotNclip3_nom		/* RTPT, then NCLIP */
	nop				/* branch delay: nothing to do */

	/* Back-face cull: emit only when the signed screen area is positive */
	mfc2	v0, C2_MAC0		/* v0 = NCLIP result */
	nop				/* GTE transfer delay */
	.set	noat
	slt	AT, zero, v0		/* AT as scratch: area > 0 ? */
	beqz	AT, $49368_done		/* facing away: emit nothing */
	.set	at

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive; macro: its lui fills */
					/* the beqz delay slot (harmless when taken) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	t2, P_XY0(a3)
	sw	t3, P_XY1(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ1
	mfc2	t2, C2_SZ2
	mfc2	t3, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $49368_max0
	li	t4, POLY_FT3_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$49368_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $49368_max1
	li	t5, CODE_POLY_FT3	/* delay slot (real work): GPU code */
	move	v0, t3
$49368_max1:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $49368_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$49368_linked
	nop				/* branch delay: nothing to do */
$49368_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$49368_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t4, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t4, P_LEN(a3)		/* tag length */
	sb	t5, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv) */
	lw	t0, 12(t1)		/* record word 3 (u3v3) as u0v0 */
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT3_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$49368_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4NoCullFog - emit a POLY_FT4 (textured quad)
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_FT4 at the primitive allocation pointer
 *             g_spriteWorkspace, depth-cue its colour with DpqColor beyond z =
 *             DEPTH_CUE_START, link it into the ordering table g_renderBuffer at its farthest
 *             z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t5/t6.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[4] (pointer at 0x8006d3a4); jal from Model_DrawFarFT4NoCullByState
 *             (0x8001f6a0).
 * Callees:    RotTransPers4_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4NoCullFog
Model_EmitFT4NoCullFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t0, a1			/* t0 = colour pointer */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a3)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a3)
	sw	t3, P_XY3(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $49478_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$49478_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $49478_max1
	li	t6, CODE_POLY_FT4	/* delay slot (real work): GPU code */
	move	v0, t3
$49478_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $49478_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$49478_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $49478_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$49478_linked
	nop				/* branch delay: nothing to do */
$49478_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$49478_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a3)		/* tag length */
	sb	t6, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)
	sw	t3, P_UV3(a3)

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT4_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$49478_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT3NoCullFog - emit a POLY_FT3 (textured triangle)
 *
 * Purpose:    transform the three vertices with RotTransPers3_nom (RTPT), fill a POLY_FT3 at
 *             the primitive allocation pointer g_spriteWorkspace, depth-cue its colour with
 *             DpqColor beyond z = DEPTH_CUE_START, link it into the ordering table
 *             g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 3 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t4/t5.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[5] (pointer at 0x8006d3a8).
 * Callees:    RotTransPers3_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT3NoCullFog
Model_EmitFT3NoCullFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t1, a2			/* t1 = texture record */
	move	t0, a1			/* t0 = colour pointer */

	/* RotTransPers3_nom(v[2], v[1], v[0]) */
	lw	a2, 0(a0)
	lw	a1, 4(a0)
	lw	a0, 8(a0)
	jal	RotTransPers3_nom	/* RTPT */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	t2, P_XY0(a3)
	sw	t3, P_XY1(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ1
	mfc2	t2, C2_SZ2
	mfc2	t3, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4959c_max0
	li	t4, POLY_FT3_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4959c_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4959c_max1
	li	t5, CODE_POLY_FT3	/* delay slot (real work): GPU code */
	move	v0, t3
$4959c_max1:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $4959c_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$4959c_linked
	nop				/* branch delay: nothing to do */
$4959c_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$4959c_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t4, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t4, P_LEN(a3)		/* tag length */
	sb	t5, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT3_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4959c_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4NoCullMirrorFog - emit a POLY_FT4 (textured quad), texture mirrored left-right
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_FT4 at the primitive allocation pointer
 *             g_spriteWorkspace, depth-cue its colour with DpqColor beyond z =
 *             DEPTH_CUE_START, link it into the ordering table g_renderBuffer at its farthest
 *             z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t5/t6.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[6] (pointer at 0x8006d3ac); jal from Model_DrawFarFT4NoCullMirrorByState
 *             (0x8001f6e4).
 * Callees:    RotTransPers4_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4NoCullMirrorFog
Model_EmitFT4NoCullMirrorFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t0, a1			/* t0 = colour pointer */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a3)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a3)
	sw	t3, P_XY3(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4969c_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4969c_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4969c_max1
	li	t6, CODE_POLY_FT4	/* delay slot (real work): GPU code */
	move	v0, t3
$4969c_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $4969c_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$4969c_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $4969c_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$4969c_linked
	nop				/* branch delay: nothing to do */
$4969c_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$4969c_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a3)		/* tag length */
	sb	t6, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)
	sw	t3, P_UV3(a3)

	/* Mirror left-right: swap u of the left (0,2) and right (1,3) corners */
	lb	t4, P_UV0(a3)		/* t4 = u0 */
	lb	t5, P_UV1(a3)		/* t5 = u1 */
	sb	t4, P_UV3(a3)		/* u3 = u0 */
	sb	t4, P_UV1(a3)		/* u1 = u0 */
	sb	t5, P_UV0(a3)		/* u0 = u1 */
	sb	t5, P_UV2(a3)		/* u2 = u1 */

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT4_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4969c_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT3AltNoCullFog - emit a POLY_FT3 (textured triangle), texture corners 3,1,2
 *
 * Purpose:    transform the three vertices with RotTransPers3_nom (RTPT), fill a POLY_FT3 at
 *             the primitive allocation pointer g_spriteWorkspace, depth-cue its colour with
 *             DpqColor beyond z = DEPTH_CUE_START, link it into the ordering table
 *             g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 3 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t4/t5.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[7] (pointer at 0x8006d3b0).
 * Callees:    RotTransPers3_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT3AltNoCullFog
Model_EmitFT3AltNoCullFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t1, a2			/* t1 = texture record */
	move	t0, a1			/* t0 = colour pointer */

	/* RotTransPers3_nom(v[2], v[1], v[0]) */
	lw	a2, 0(a0)
	lw	a1, 4(a0)
	lw	a0, 8(a0)
	jal	RotTransPers3_nom	/* RTPT */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	t2, P_XY0(a3)
	sw	t3, P_XY1(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ1
	mfc2	t2, C2_SZ2
	mfc2	t3, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $497d8_max0
	li	t4, POLY_FT3_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$497d8_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $497d8_max1
	li	t5, CODE_POLY_FT3	/* delay slot (real work): GPU code */
	move	v0, t3
$497d8_max1:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $497d8_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$497d8_linked
	nop				/* branch delay: nothing to do */
$497d8_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$497d8_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t4, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t4, P_LEN(a3)		/* tag length */
	sb	t5, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv) */
	lw	t0, 12(t1)		/* record word 3 (u3v3) as u0v0 */
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT3_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$497d8_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4ABEFog - emit a POLY_FT4 (textured quad), semi-transparent
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_FT4 at the primitive allocation pointer
 *             g_spriteWorkspace, depth-cue its colour with DpqColor beyond z =
 *             DEPTH_CUE_START, link it into the ordering table g_renderBuffer at its farthest
 *             z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t5/t6.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[12] (pointer at 0x8006d3c4); jal from Model_DrawFarFT4ABEByState
 *             (0x8001f728).
 * Callees:    RotTransPers4_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4ABEFog
Model_EmitFT4ABEFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t0, a1			/* t0 = colour pointer */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a3)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a3)
	sw	t3, P_XY3(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $498d8_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$498d8_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $498d8_max1
	li	t6, CODE_POLY_FT4_ABE	/* delay slot (real work): GPU code */
	move	v0, t3
$498d8_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $498d8_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$498d8_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $498d8_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$498d8_linked
	nop				/* branch delay: nothing to do */
$498d8_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$498d8_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a3)		/* tag length */
	sb	t6, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)
	sw	t3, P_UV3(a3)

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT4_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$498d8_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitF4ABEFog - emit a POLY_F4 (flat quad), semi-transparent
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_F4 at the primitive allocation pointer
 *             g_spriteWorkspace, depth-cue its colour with DpqColor beyond z =
 *             DEPTH_CUE_START, link it into the ordering table g_renderBuffer at its farthest
 *             z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = unused (flat polygon: no texture record).
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t5/t6.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[14] (pointer at 0x8006d3cc).
 * Callees:    RotTransPers4_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitF4ABEFog
Model_EmitF4ABEFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t0, a1			/* t0 = colour pointer */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, F4_XY0(a3)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, F4_XY1(a3)
	sw	t3, F4_XY3(a3)
	sw	t4, F4_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $499fc_max0
	li	t5, POLY_F4_LEN		/* delay slot (real work): tag length */
	move	v0, t2
$499fc_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $499fc_max1
	li	t6, CODE_POLY_F4_ABE	/* delay slot (real work): GPU code */
	move	v0, t3
$499fc_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $499fc_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$499fc_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $499fc_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$499fc_linked
	nop				/* branch delay: nothing to do */
$499fc_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$499fc_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a3)		/* tag length */
	sb	t6, P_CODE(a3)		/* GPU code */

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_F4_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$499fc_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4ABEMirrorFog - emit a POLY_FT4 (textured quad), semi-transparent, texture mirrored left-right
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_FT4 at the primitive allocation pointer
 *             g_spriteWorkspace, depth-cue its colour with DpqColor beyond z =
 *             DEPTH_CUE_START, link it into the ordering table g_renderBuffer at its farthest
 *             z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the CVECTOR colour (r,g,b,code), before depth cueing.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t7, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour pointer) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             Across DpqColor (a0..a2 only): v0 (OT entry), a3 (primitive), t5/t6.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_farDrawTable[34] (pointer at 0x8006d41c); jal from Model_DrawFarFT4ABEMirrorByState
 *             (0x8001f76c).
 * Callees:    RotTransPers4_nom, DpqColor (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4ABEMirrorFog
Model_EmitFT4ABEMirrorFog:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	move	t0, a1			/* t0 = colour pointer */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a3, g_spriteWorkspace	/* a3 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a3)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a3)
	sw	t3, P_XY3(a3)
	sw	t4, P_XY2(a3)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $49afc_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$49afc_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $49afc_max1
	li	t6, CODE_POLY_FT4_ABE	/* delay slot (real work): GPU code */
	move	v0, t3
$49afc_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $49afc_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$49afc_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	move	a1, v0			/* a1 = z, for the depth cue */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	/* Colour: DpqColor beyond DEPTH_CUE_START, else copied */
	slti	t7, a1, DEPTH_CUE_START
	bnez	t7, $49afc_near
	move	a0, t0			/* delay slot (real work): colour pointer */
	addiu	a1, a1, -DEPTH_CUE_START	/* depth past the cue start */
	jal	DpqColor		/* DPCS: blend towards the far colour */
	addiu	a2, a3, P_RGB0		/* delay slot (real work): &prim->r0 */
	j	$49afc_linked
	nop				/* branch delay: nothing to do */
$49afc_near:
	lw	t2, 0(a0)		/* colour word */
	nop				/* load delay */
	sw	t2, P_RGB0(a3)
$49afc_linked:

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a3, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a3)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a3)		/* tag length */
	sb	t6, P_CODE(a3)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a3)
	sw	t4, P_UV1(a3)
	sw	t2, P_UV2(a3)
	sw	t3, P_UV3(a3)

	/* Mirror left-right: swap u of the left (0,2) and right (1,3) corners */
	lb	t4, P_UV0(a3)		/* t4 = u0 */
	lb	t5, P_UV1(a3)		/* t5 = u1 */
	sb	t4, P_UV3(a3)		/* u3 = u0 */
	sb	t4, P_UV1(a3)		/* u1 = u0 */
	sb	t5, P_UV0(a3)		/* u0 = u1 */
	sb	t5, P_UV2(a3)		/* u2 = u1 */

	/* Advance the primitive allocation pointer */
	addiu	a3, a3, POLY_FT4_SIZE
	sw	a3, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$49afc_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4 - emit a POLY_FT4 (textured quad)
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), drop the polygon when it faces away (NCLIP <= 0), fill a POLY_FT4
 *             at the primitive allocation pointer g_spriteWorkspace, store its colour word
 *             unchanged, link it into the ordering table g_renderBuffer at its farthest z,
 *             and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO (NormalClipS leaves v1 alone).
 * Scratchpad: none.
 * Callers:    g_dispatch_table[0] (pointer at 0x8006d420), called by Model_DrawMidInCell; jal from Model_DrawMidFT4ByState
 *             (0x8001f8e8).
 * Callees:    RotTransPers4_nom, NormalClipS (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4
Model_EmitFT4:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Back-face cull: emit only when the signed screen area is positive */
	jal	NormalClipS		/* NCLIP on SXY0..SXY2, v0 = MAC0 */
	nop				/* branch delay: nothing to do */
	.set	noat
	slt	AT, zero, v0		/* AT as scratch: area > 0 ? */
	beqz	AT, $49c38_done		/* facing away: emit nothing */
	.set	at

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive; macro: its lui fills */
					/* the beqz delay slot (harmless when taken) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a0)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a0)
	sw	t3, P_XY3(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $49c38_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$49c38_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $49c38_max1
	li	t6, CODE_POLY_FT4	/* delay slot (real work): GPU code */
	move	v0, t3
$49c38_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $49c38_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$49c38_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a0)		/* tag length */
	sb	t6, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)
	sw	t3, P_UV3(a0)

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT4_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$49c38_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT3 - emit a POLY_FT3 (textured triangle)
 *
 * Purpose:    transform the three vertices with RotNclip3_nom (RTPT), drop the polygon when it
 *             faces away (NCLIP <= 0), fill a POLY_FT3 at the primitive allocation pointer
 *             g_spriteWorkspace, store its colour word unchanged, link it into the ordering
 *             table g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 3 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[1] (pointer at 0x8006d424), called by Model_DrawMidInCell.
 * Callees:    RotNclip3_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT3
Model_EmitFT3:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotNclip3_nom(v[2], v[1], v[0]) */
	lw	a2, 0(a0)
	lw	a1, 4(a0)
	lw	a0, 8(a0)
	jal	RotNclip3_nom		/* RTPT, then NCLIP */
	nop				/* branch delay: nothing to do */

	/* Back-face cull: emit only when the signed screen area is positive */
	mfc2	v0, C2_MAC0		/* v0 = NCLIP result */
	nop				/* GTE transfer delay */
	.set	noat
	slt	AT, zero, v0		/* AT as scratch: area > 0 ? */
	beqz	AT, $49d40_done		/* facing away: emit nothing */
	.set	at

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive; macro: its lui fills */
					/* the beqz delay slot (harmless when taken) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	t2, P_XY0(a0)
	sw	t3, P_XY1(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ1
	mfc2	t2, C2_SZ2
	mfc2	t3, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $49d40_max0
	li	t4, POLY_FT3_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$49d40_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $49d40_max1
	li	t5, CODE_POLY_FT3	/* delay slot (real work): GPU code */
	move	v0, t3
$49d40_max1:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t4, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t4, P_LEN(a0)		/* tag length */
	sb	t5, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT3_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$49d40_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4Mirror - emit a POLY_FT4 (textured quad), texture mirrored left-right
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), drop the polygon when it faces away (NCLIP <= 0), fill a POLY_FT4
 *             at the primitive allocation pointer g_spriteWorkspace, store its colour word
 *             unchanged, link it into the ordering table g_renderBuffer at its farthest z,
 *             and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO (NormalClipS leaves v1 alone).
 * Scratchpad: none.
 * Callers:    g_dispatch_table[2] (pointer at 0x8006d428), called by Model_DrawMidInCell; jal from Model_DrawMidFT4MirrorByState
 *             (0x8001f92c).
 * Callees:    RotTransPers4_nom, NormalClipS (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4Mirror
Model_EmitFT4Mirror:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Back-face cull: emit only when the signed screen area is positive */
	jal	NormalClipS		/* NCLIP on SXY0..SXY2, v0 = MAC0 */
	nop				/* branch delay: nothing to do */
	.set	noat
	slt	AT, zero, v0		/* AT as scratch: area > 0 ? */
	beqz	AT, $49e24_done		/* facing away: emit nothing */
	.set	at

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive; macro: its lui fills */
					/* the beqz delay slot (harmless when taken) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a0)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a0)
	sw	t3, P_XY3(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $49e24_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$49e24_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $49e24_max1
	li	t6, CODE_POLY_FT4	/* delay slot (real work): GPU code */
	move	v0, t3
$49e24_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $49e24_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$49e24_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a0)		/* tag length */
	sb	t6, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)
	sw	t3, P_UV3(a0)

	/* Mirror left-right: swap u of the left (0,2) and right (1,3) corners */
	lb	t4, P_UV0(a0)		/* t4 = u0 */
	lb	t5, P_UV1(a0)		/* t5 = u1 */
	sb	t4, P_UV3(a0)		/* u3 = u0 */
	sb	t4, P_UV1(a0)		/* u1 = u0 */
	sb	t5, P_UV0(a0)		/* u0 = u1 */
	sb	t5, P_UV2(a0)		/* u2 = u1 */

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT4_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$49e24_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT3Alt - emit a POLY_FT3 (textured triangle), texture corners 3,1,2
 *
 * Purpose:    transform the three vertices with RotNclip3_nom (RTPT), drop the polygon when it
 *             faces away (NCLIP <= 0), fill a POLY_FT3 at the primitive allocation pointer
 *             g_spriteWorkspace, store its colour word unchanged, link it into the ordering
 *             table g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 3 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[3] (pointer at 0x8006d42c), called by Model_DrawMidInCell.
 * Callees:    RotNclip3_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT3Alt
Model_EmitFT3Alt:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotNclip3_nom(v[2], v[1], v[0]) */
	lw	a2, 0(a0)
	lw	a1, 4(a0)
	lw	a0, 8(a0)
	jal	RotNclip3_nom		/* RTPT, then NCLIP */
	nop				/* branch delay: nothing to do */

	/* Back-face cull: emit only when the signed screen area is positive */
	mfc2	v0, C2_MAC0		/* v0 = NCLIP result */
	nop				/* GTE transfer delay */
	.set	noat
	slt	AT, zero, v0		/* AT as scratch: area > 0 ? */
	beqz	AT, $49f44_done		/* facing away: emit nothing */
	.set	at

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive; macro: its lui fills */
					/* the beqz delay slot (harmless when taken) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	t2, P_XY0(a0)
	sw	t3, P_XY1(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ1
	mfc2	t2, C2_SZ2
	mfc2	t3, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $49f44_max0
	li	t4, POLY_FT3_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$49f44_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $49f44_max1
	li	t5, CODE_POLY_FT3	/* delay slot (real work): GPU code */
	move	v0, t3
$49f44_max1:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t4, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t4, P_LEN(a0)		/* tag length */
	sb	t5, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv) */
	lw	t0, 12(t1)		/* record word 3 (u3v3) as u0v0 */
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT3_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$49f44_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4NoCull - emit a POLY_FT4 (textured quad)
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_FT4 at the primitive allocation pointer
 *             g_spriteWorkspace, store its colour word unchanged, link it into the ordering
 *             table g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[4] (pointer at 0x8006d430), called by Model_DrawMidInCell; jal from Model_DrawMidFT4NoCullByState
 *             (0x8001fa8c).
 * Callees:    RotTransPers4_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4NoCull
Model_EmitFT4NoCull:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a0)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a0)
	sw	t3, P_XY3(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4a028_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4a028_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4a028_max1
	li	t6, CODE_POLY_FT4	/* delay slot (real work): GPU code */
	move	v0, t3
$4a028_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $4a028_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$4a028_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a0)		/* tag length */
	sb	t6, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)
	sw	t3, P_UV3(a0)

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT4_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4a028_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT3NoCull - emit a POLY_FT3 (textured triangle)
 *
 * Purpose:    transform the three vertices with RotTransPers3_nom (RTPT), fill a POLY_FT3 at
 *             the primitive allocation pointer g_spriteWorkspace, store its colour word
 *             unchanged, link it into the ordering table g_renderBuffer at its farthest z,
 *             and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 3 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[5] (pointer at 0x8006d434), called by Model_DrawMidInCell.
 * Callees:    RotTransPers3_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT3NoCull
Model_EmitFT3NoCull:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers3_nom(v[2], v[1], v[0]) */
	lw	a2, 0(a0)
	lw	a1, 4(a0)
	lw	a0, 8(a0)
	jal	RotTransPers3_nom	/* RTPT */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	t2, P_XY0(a0)
	sw	t3, P_XY1(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ1
	mfc2	t2, C2_SZ2
	mfc2	t3, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4a120_max0
	li	t4, POLY_FT3_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4a120_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4a120_max1
	li	t5, CODE_POLY_FT3	/* delay slot (real work): GPU code */
	move	v0, t3
$4a120_max1:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t4, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t4, P_LEN(a0)		/* tag length */
	sb	t5, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT3_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4a120_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4NoCullMirror - emit a POLY_FT4 (textured quad), texture mirrored left-right
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_FT4 at the primitive allocation pointer
 *             g_spriteWorkspace, store its colour word unchanged, link it into the ordering
 *             table g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[6] (pointer at 0x8006d438), called by Model_DrawMidInCell; jal from Model_DrawMidFT4NoCullMirrorByState
 *             (0x8001fad0).
 * Callees:    RotTransPers4_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4NoCullMirror
Model_EmitFT4NoCullMirror:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a0)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a0)
	sw	t3, P_XY3(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4a1f4_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4a1f4_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4a1f4_max1
	li	t6, CODE_POLY_FT4	/* delay slot (real work): GPU code */
	move	v0, t3
$4a1f4_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $4a1f4_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$4a1f4_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a0)		/* tag length */
	sb	t6, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)
	sw	t3, P_UV3(a0)

	/* Mirror left-right: swap u of the left (0,2) and right (1,3) corners */
	lb	t4, P_UV0(a0)		/* t4 = u0 */
	lb	t5, P_UV1(a0)		/* t5 = u1 */
	sb	t4, P_UV3(a0)		/* u3 = u0 */
	sb	t4, P_UV1(a0)		/* u1 = u0 */
	sb	t5, P_UV0(a0)		/* u0 = u1 */
	sb	t5, P_UV2(a0)		/* u2 = u1 */

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT4_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4a1f4_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT3AltNoCull - emit a POLY_FT3 (textured triangle), texture corners 3,1,2
 *
 * Purpose:    transform the three vertices with RotTransPers3_nom (RTPT), fill a POLY_FT3 at
 *             the primitive allocation pointer g_spriteWorkspace, store its colour word
 *             unchanged, link it into the ordering table g_renderBuffer at its farthest z,
 *             and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 3 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[7] (pointer at 0x8006d43c), called by Model_DrawMidInCell.
 * Callees:    RotTransPers3_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT3AltNoCull
Model_EmitFT3AltNoCull:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers3_nom(v[2], v[1], v[0]) */
	lw	a2, 0(a0)
	lw	a1, 4(a0)
	lw	a0, 8(a0)
	jal	RotTransPers3_nom	/* RTPT */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	t2, P_XY0(a0)
	sw	t3, P_XY1(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ1
	mfc2	t2, C2_SZ2
	mfc2	t3, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4a304_max0
	li	t4, POLY_FT3_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4a304_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4a304_max1
	li	t5, CODE_POLY_FT3	/* delay slot (real work): GPU code */
	move	v0, t3
$4a304_max1:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t4, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t4, P_LEN(a0)		/* tag length */
	sb	t5, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv) */
	lw	t0, 12(t1)		/* record word 3 (u3v3) as u0v0 */
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT3_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4a304_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4ABE - emit a POLY_FT4 (textured quad), semi-transparent
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_FT4 at the primitive allocation pointer
 *             g_spriteWorkspace, store its colour word unchanged, link it into the ordering
 *             table g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[12] (pointer at 0x8006d450), called by Model_DrawMidInCell; jal from
 *             Model_DrawMidFT4ABEByState (0x8001fb14).
 * Callees:    RotTransPers4_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4ABE
Model_EmitFT4ABE:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a0)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a0)
	sw	t3, P_XY3(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4a3d8_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4a3d8_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4a3d8_max1
	li	t6, CODE_POLY_FT4_ABE	/* delay slot (real work): GPU code */
	move	v0, t3
$4a3d8_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $4a3d8_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$4a3d8_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a0)		/* tag length */
	sb	t6, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)
	sw	t3, P_UV3(a0)

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT4_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4a3d8_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitF4ABE - emit a POLY_F4 (flat quad), semi-transparent
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_F4 at the primitive allocation pointer
 *             g_spriteWorkspace, store its colour word unchanged, link it into the ordering
 *             table g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = unused (flat polygon: no texture record).
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[14] (pointer at 0x8006d458), called by Model_DrawMidInCell.
 * Callees:    RotTransPers4_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitF4ABE
Model_EmitF4ABE:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, F4_XY0(a0)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, F4_XY1(a0)
	sw	t3, F4_XY3(a0)
	sw	t4, F4_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4a4d0_max0
	li	t5, POLY_F4_LEN		/* delay slot (real work): tag length */
	move	v0, t2
$4a4d0_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4a4d0_max1
	li	t6, CODE_POLY_F4_ABE	/* delay slot (real work): GPU code */
	move	v0, t3
$4a4d0_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $4a4d0_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$4a4d0_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a0)		/* tag length */
	sb	t6, P_CODE(a0)		/* GPU code */

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_F4_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4a4d0_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */

/*
 * Model_EmitFT4ABEMirror - emit a POLY_FT4 (textured quad), semi-transparent, texture mirrored left-right
 *
 * Purpose:    transform the four vertices with RotTransPers4_nom (RTPT on three, then RTPS on
 *             the fourth), fill a POLY_FT4 at the primitive allocation pointer
 *             g_spriteWorkspace, store its colour word unchanged, link it into the ordering
 *             table g_renderBuffer at its farthest z, and advance g_spriteWorkspace.
 * Inputs:     a0 = pointer to 4 SVECTOR pointers; the corners are taken in reverse order
 *             (the last vertex becomes x0,y0).
 *             a1 = pointer to the colour word (r,g,b,code), stored as is.
 *             a2 = pointer to a 16-byte texture record: u0v0+clut, u1v1+tpage, u2v2, u3v3.
 *             g_spriteWorkspace = next free primitive; g_renderBuffer = ordering table.
 * Outputs:    none in registers; the primitive, the OT entry and g_spriteWorkspace.
 * Clobbers:   v0, v1, a0..a3, t0..t6, AT ($1), the GTE.
 * Preserves:  s0..s7, sp; ra is saved in a 24-byte frame around the calls.
 * Live across calls: t0 (colour word) and t1 (texture record) across the libgte
 *             calls, which use only a0..a3, v0, v1 and the GTE.
 *             v1 = SXY of the last vertex, which RotTransPers4_nom reads from SXY0 before
 *             its RTPS pushes it out of the FIFO.
 * Scratchpad: none.
 * Callers:    g_dispatch_table[34] (pointer at 0x8006d4a8), called by Model_DrawMidInCell; jal from
 *             Model_DrawMidFT4ABEMirrorByState (0x8001fb58).
 * Callees:    RotTransPers4_nom (Runtime Library 3.3 libgte).
 */
	.globl	Model_EmitFT4ABEMirror
Model_EmitFT4ABEMirror:
	addiu	sp, sp, -24		/* frame: argument save area + ra */
	sw	ra, 16(sp)
	lw	t0, 0(a1)		/* t0 = colour word */
	move	t1, a2			/* t1 = texture record */

	/* RotTransPers4_nom(v[3], v[2], v[1], v[0]) */
	lw	a3, 0(a0)
	lw	a2, 4(a0)
	lw	a1, 8(a0)
	lw	a0, 12(a0)
	jal	RotTransPers4_nom	/* RTPT on a0..a2, RTPS on a3 */
	nop				/* branch delay: nothing to do */

	/* Screen x,y from the SXY FIFO into the primitive */
	lw	a0, g_spriteWorkspace	/* a0 = primitive (lui+lw macro) */
	mfc2	t2, C2_SXY0
	mfc2	t3, C2_SXY1
	mfc2	t4, C2_SXY2
	sw	v1, P_XY0(a0)		/* last vertex (v1 from RotTransPers4_nom) */
	sw	t2, P_XY1(a0)
	sw	t3, P_XY3(a0)
	sw	t4, P_XY2(a0)

	/* v0 = largest screen z; two delay slots load the tag length and GPU code */
	mfc2	v0, C2_SZ0
	mfc2	t2, C2_SZ1
	mfc2	t3, C2_SZ2
	mfc2	t4, C2_SZ3
	.set	noat
	slt	AT, v0, t2		/* AT as scratch: v0 < t2 ? */
	beqz	AT, $4a5a4_max0
	li	t5, POLY_FT4_LEN	/* delay slot (real work): tag length */
	move	v0, t2
$4a5a4_max0:
	slt	AT, v0, t3		/* AT as scratch: v0 < t3 ? */
	beqz	AT, $4a5a4_max1
	li	t6, CODE_POLY_FT4_ABE	/* delay slot (real work): GPU code */
	move	v0, t3
$4a5a4_max1:
	slt	AT, v0, t4		/* AT as scratch: v0 < t4 ? */
	beqz	AT, $4a5a4_max2
	nop				/* branch delay: nothing to do */
	move	v0, t4
$4a5a4_max2:
	.set	at

	/* v0 = &ot[z >> OT_SHIFT] */
	sra	v0, v0, OT_SHIFT
	lw	v1, g_renderBuffer	/* v1 = OT base (lui+lw macro) */
	sll	v0, v0, 2
	addu	v0, v0, v1

	sw	t0, P_RGB0(a0)		/* colour word, unchanged */

	/* addPrim: link in front of the OT entry, then tag length and GPU code */
	lw	v1, 0(v0)		/* v1 = old OT entry */
	sw	a0, 0(v0)		/* OT entry -> primitive */
	sw	v1, P_TAG(a0)		/* primitive -> old entry */
	sb	t5, P_LEN(v0)		/* length also into the OT entry top byte */
	sb	t5, P_LEN(a0)		/* tag length */
	sb	t6, P_CODE(a0)		/* GPU code */

	/* Texture words (uv + clut, uv + tpage, uv, uv) */
	lw	t0, 0(t1)
	lw	t4, 4(t1)
	lw	t2, 8(t1)
	lw	t3, 12(t1)
	sw	t0, P_UV0(a0)
	sw	t4, P_UV1(a0)
	sw	t2, P_UV2(a0)
	sw	t3, P_UV3(a0)

	/* Mirror left-right: swap u of the left (0,2) and right (1,3) corners */
	lb	t4, P_UV0(a0)		/* t4 = u0 */
	lb	t5, P_UV1(a0)		/* t5 = u1 */
	sb	t4, P_UV3(a0)		/* u3 = u0 */
	sb	t4, P_UV1(a0)		/* u1 = u0 */
	sb	t5, P_UV0(a0)		/* u0 = u1 */
	sb	t5, P_UV2(a0)		/* u2 = u1 */

	/* Advance the primitive allocation pointer */
	addiu	a0, a0, POLY_FT4_SIZE
	sw	a0, g_spriteWorkspace	/* macro: lui AT, then sw through AT */
$4a5a4_done:
	lw	ra, 16(sp)
	nop				/* load delay */
	jr	ra
	addiu	sp, sp, 24		/* delay slot (real work): pop the frame */
