/*
 * Model_SubdivF4ABE.s - quad-subdivision renderer:
 *             flat quad (POLY_F4), semi-transparent, no back-face culling.
 *
 * Hand-written assembly (AGENTS.md, "Hand-written assembly"): GTE/renderer region only.
 * Built by tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by
 * config/Model_SubdivF4ABE.json. One of seven renderers (subdiv_*.s) that share the
 * scratchpad layout and the building blocks of subdiv.h; they differ in primitive
 * type, culling, texture mirroring and semi-transparency, and otherwise follow the
 * same plan instruction for instruction.
 */
#include "subdiv.h"

	.text
	.set	noreorder

/*
 * Model_SubdivF4ABE - draw one near flat quad (POLY_F4), semi-transparent, no back-face culling,
 *             subdivided
 *
 * Purpose:    split the quad into a 3x3 vertex grid (2x2 sub-quads), or a 5x5 grid (4x4
 *             sub-quads, as four 2x2 quadrants) when any corner is nearer than
 *             SUBDIV_NEAR_Z; transform the grid with one RotTransPersN call; drop the quad
 *             (or quadrant) whose centre is nearer than SUBDIV_CLIP_Z; emit every sub-quad
 *             as a POLY_F4 into the ordering table at its farthest corner.
 * Inputs:     a0 = pointer to the quad's four SVECTOR pointers v[0..3];
 *             a1 = pointer to its colour word (r,g,b,code; the code byte is replaced);
 *             a2 = unused (flat quad);
 *             GTE SZ0..SZ3 = the corners' screen z, left by the caller's RotTransPers4;
 *             g_spriteWorkspace = next free primitive, g_renderBuffer = OT base.
 * Outputs:    the sub-quad primitives, their OT links, g_spriteWorkspace advanced.
 * Clobbers:   v0, v1, a0..a3, t0..t8, AT, the GTE, scratchpad 0x1f800000..0x1f800205.
 * Preserves:  s0..s7 and ra, saved in the scratchpad (SPAD_SAVE_*), not on the stack;
 *             sp (an 80-byte frame holds only the two stacked RotTransPersN arguments).
 * Scratchpad: layout in subdiv.h: SPAD_GRID, SPAD_SXY, SPAD_SZ, SPAD_FLAG, SPAD_P,
 *             SPAD_SAVE_*, SPAD_COLOUR (no texture fields).
 * Live across calls: a3 is reloaded after RotTransPersN (it is that call's fourth
 *             argument); nothing in t registers survives a call here.
 * Callers:    Model_DrawNearF4ABE (jal at 0x8001f078; not yet named in any map), after its
 *             RotTransPers4 and an all-corners-off-screen test.
 * Callees:    RotTransPersN (Runtime Library 3.3 libgte).
 */
	.globl	Model_SubdivF4ABE
Model_SubdivF4ABE:

	/* Prologue: ra and s0..s7 go to the scratchpad; the stack frame only holds */
	/* the two stacked arguments of RotTransPersN. Keep the colour word there too. */
	addiu	sp, sp, -80			/* frame: only the stacked RotTransPersN arguments at 16(sp), 20(sp) */
	li	a3, SCRATCHPAD			/* a3 = scratchpad base */
	lw	v0, 0(a1)			/* v0 = colour word (a1 points at it) */
	sw	ra, SPAD_SAVE_RA(a3)		/* save ra in the scratchpad */
	sw	s0, SPAD_SAVE_S0(a3)		/* save s0 in the scratchpad */
	sw	s1, SPAD_SAVE_S1(a3)		/* save s1 in the scratchpad */
	sw	s2, SPAD_SAVE_S2(a3)		/* save s2 in the scratchpad */
	sw	s3, SPAD_SAVE_S3(a3)		/* save s3 in the scratchpad */
	sw	s4, SPAD_SAVE_S4(a3)		/* save s4 in the scratchpad */
	sw	s5, SPAD_SAVE_S5(a3)		/* save s5 in the scratchpad */
	sw	s6, SPAD_SAVE_S6(a3)		/* save s6 in the scratchpad */
	sw	s7, SPAD_SAVE_S7(a3)		/* save s7 in the scratchpad */
	sw	v0, SPAD_COLOUR(a3)		/* keep the colour word */

	/* Nearest corner: v0 = min(SZ0..SZ3), the screen z of the four corners as the */
	/* caller's RotTransPers4 left them in the GTE */
	mfc2	v0, C2_SZ0			/* v0 = z of the caller corner 0 (from its RotTransPers4) */
	mfc2	t2, C2_SZ1			/* t2 = z of the caller corner 1 (from its RotTransPers4) */
	mfc2	t3, C2_SZ2			/* t3 = z of the caller corner 2 (from its RotTransPers4) */
	mfc2	t4, C2_SZ3			/* t4 = z of the caller corner 3 (from its RotTransPers4) */
	.set	noat
	slt	AT, t2, v0			/* AT as scratch: t2 < v0 ? */
	beqz	AT, 1f				/* keep v0 unless the other is nearer */
	nop					/* branch delay: nothing to do */
	move	v0, t2
1:
	slt	AT, t3, v0			/* AT as scratch: t3 < v0 ? */
	beqz	AT, 1f				/* keep v0 unless the other is nearer */
	nop					/* branch delay: nothing to do */
	move	v0, t3
1:
	slt	AT, t4, v0			/* AT as scratch: t4 < v0 ? */
	beqz	AT, 1f				/* keep v0 unless the other is nearer */
	nop					/* branch delay: nothing to do */
	move	v0, t4

	/* Near quads (any corner below SUBDIV_NEAR_Z) take the 5x5 grid */
1:
	slti	v0, v0, SUBDIV_NEAR_Z		/* v0 = nearest corner z < SUBDIV_NEAR_Z */
	bnez	v0, $53eb4_grid5		/* near: take the 5x5 grid */
	nop					/* branch delay: nothing to do */

	/* ==== 3x3 grid: two sub-quads per side ==== */

	/* Corners: t0..t3 = vertex pointers; grid row 0 runs v[3] -> v[2], row 2 runs */
	/* v[0] -> v[1] (the order RotTransPers4 gives the primitive corners) */
	lw	t0, 12(a0)			/* t0 = vertex pointer v[3] */
	lw	t1, 8(a0)			/* t1 = vertex pointer v[2] */
	lw	t2, 0(a0)			/* t2 = vertex pointer v[0] */
	lw	t3, 4(a0)			/* t3 = vertex pointer v[1] */
	.set	at
	COPY_VERTEX(t0, GRID3(0,0))		/* grid (0,0) = v[3] */
	COPY_VERTEX(t1, GRID3(0,2))		/* grid (0,2) = v[2] */
	COPY_VERTEX(t2, GRID3(2,0))		/* grid (2,0) = v[0] */
	COPY_VERTEX(t3, GRID3(2,2))		/* grid (2,2) = v[1] */

	/* Edge midpoints, then the centre (midpoint of a diagonal) */
	/* grid (0,1) = midpoint of grid (0,0) and grid (0,2) */
	move	a0, t0
	move	a1, t1
	addiu	a2, a3, GRID3(0,1)
	MIDPOINT
	/* grid (1,0) = midpoint of grid (0,0) and grid (2,0) */
	move	a0, t0
	move	a1, t2
	addiu	a2, a3, GRID3(1,0)
	MIDPOINT
	/* grid (1,2) = midpoint of grid (0,2) and grid (2,2) */
	move	a0, t1
	move	a1, t3
	addiu	a2, a3, GRID3(1,2)
	MIDPOINT
	/* grid (2,1) = midpoint of grid (2,0) and grid (2,2) */
	move	a0, t2
	move	a1, t3
	addiu	a2, a3, GRID3(2,1)
	MIDPOINT
	/* grid (1,1) = midpoint of grid (0,0) and grid (2,2) */
	move	a0, t0
	move	a1, t3
	addiu	a2, a3, GRID3(1,1)
	MIDPOINT

	/* Transform the whole grid: RotTransPersN(grid, sxy, sz, p, flag, count) */
	move	a0, a3				/* a0 = grid (first RotTransPersN argument) */
	addiu	a1, a3, SXY3(0,0)		/* a1 = sxy output array */
	addiu	a2, a3, SZ3(0,0)		/* a2 = sz output array */
	addiu	v0, a3, SPAD_FLAG		/* v0 = flag output array */
	addiu	a3, a3, SPAD_P			/* a3 = p (depth-cue) output array */
	li	v1, GRID3_COUNT			/* v1 = 3x3 = 9 vertices */
	sw	v0, 16(sp)			/* fifth argument: flag array */
	jal	RotTransPersN			/* RTPS per vertex: sxy, sz (and p, flag) for the grid */
	sw	v1, 20(sp)			/* delay slot (real work): sixth argument: vertex count */
	li	a3, SCRATCHPAD			/* reload a3: it was the fourth RotTransPersN argument */
	nop					/* spare slot (as written) */

	/* Draw nothing when the centre is too close */
	lh	a0, SZ3(1,1)(a3)		/* screen z of (1,1) */
	nop					/* load delay */
	slti	v0, a0, SUBDIV_CLIP_Z		/* v0 = centre z < SUBDIV_CLIP_Z (too close) */
	bnez	v0, $53eb4_return		/* too close: draw nothing */

	/* Sub-quad emission registers (see subdiv.h) */
	/* (its first instruction fills the delay slot of the branch above) */
	lw	s4, SPAD_COLOUR(a3)		/* delay slot (real work): s4 = colour word for every sub-quad */
	lw	s5, g_spriteWorkspace		/* s5 = next free primitive (macro: lui + lw) */
	li	s6, POLY_F4_LEN			/* s6 = tag length of every sub-quad */
	li	s7, CODE_POLY_F4_ABE		/* s7 = GPU code of every sub-quad */
	lw	t8, g_renderBuffer		/* t8 = OT base (macro: lui + lw) */

	/* Sub-quad rows 0..1, columns 0..1 */
	QUAD_OT(SZ3(0,0), SZ3(0,1), SZ3(1,0), SZ3(1,1))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY3(0,0), SXY3(0,1), SXY3(1,0), SXY3(1,1))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 0..1, columns 1..2 */
	QUAD_OT(SZ3(0,1), SZ3(0,2), SZ3(1,1), SZ3(1,2))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY3(0,1), SXY3(0,2), SXY3(1,1), SXY3(1,2))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 1..2, columns 0..1 */
	QUAD_OT(SZ3(1,0), SZ3(1,1), SZ3(2,0), SZ3(2,1))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY3(1,0), SXY3(1,1), SXY3(2,0), SXY3(2,1))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 1..2, columns 1..2 */
	QUAD_OT(SZ3(1,1), SZ3(1,2), SZ3(2,1), SZ3(2,2))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY3(1,1), SXY3(1,2), SXY3(2,1), SXY3(2,2))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	j	$53eb4_publish			/* 3x3 grid done */
	addiu	s5, s5, POLY_F4_SIZE		/* delay slot (real work): next primitive */

	/* ==== 5x5 grid: four sub-quads per side, drawn as four 2x2 quadrants ==== */

	/* Corners, as for the 3x3 grid */
$53eb4_grid5:
	lw	t0, 12(a0)			/* t0 = vertex pointer v[3] */
	lw	t1, 8(a0)			/* t1 = vertex pointer v[2] */
	lw	t2, 0(a0)			/* t2 = vertex pointer v[0] */
	lw	t3, 4(a0)			/* t3 = vertex pointer v[1] */
	COPY_VERTEX(t0, GRID5(0,0))		/* grid (0,0) = v[3] */
	COPY_VERTEX(t1, GRID5(0,4))		/* grid (0,4) = v[2] */
	COPY_VERTEX(t2, GRID5(4,0))		/* grid (4,0) = v[0] */
	COPY_VERTEX(t3, GRID5(4,4))		/* grid (4,4) = v[1] */

	/* Outer edge midpoints and centre first; then the inner grid lines from them */
	/* grid (0,2) = midpoint of grid (0,0) and grid (0,4) */
	move	a0, t0
	move	a1, t1
	addiu	a2, a3, GRID5(0,2)
	MIDPOINT
	/* grid (2,0) = midpoint of grid (0,0) and grid (4,0) */
	move	a0, t0
	move	a1, t2
	addiu	a2, a3, GRID5(2,0)
	MIDPOINT
	/* grid (2,4) = midpoint of grid (0,4) and grid (4,4) */
	move	a0, t1
	move	a1, t3
	addiu	a2, a3, GRID5(2,4)
	MIDPOINT
	/* grid (4,2) = midpoint of grid (4,0) and grid (4,4) */
	move	a0, t2
	move	a1, t3
	addiu	a2, a3, GRID5(4,2)
	MIDPOINT
	/* grid (2,2) = midpoint of grid (0,0) and grid (4,4) */
	move	a0, t0
	move	a1, t3
	addiu	a2, a3, GRID5(2,2)
	MIDPOINT
	/* grid (0,1) = midpoint of grid (0,0) and grid (0,2) */
	move	a0, t0
	addiu	a1, a3, GRID5(0,2)
	addiu	a2, a3, GRID5(0,1)
	MIDPOINT
	/* grid (1,0) = midpoint of grid (0,0) and grid (2,0) */
	move	a0, t0
	addiu	a1, a3, GRID5(2,0)
	addiu	a2, a3, GRID5(1,0)
	MIDPOINT
	/* grid (1,2) = midpoint of grid (0,2) and grid (2,2) */
	addiu	a0, a3, GRID5(0,2)
	addiu	a1, a3, GRID5(2,2)
	addiu	a2, a3, GRID5(1,2)
	MIDPOINT
	/* grid (2,1) = midpoint of grid (2,0) and grid (2,2) */
	addiu	a0, a3, GRID5(2,0)
	addiu	a1, a3, GRID5(2,2)
	addiu	a2, a3, GRID5(2,1)
	MIDPOINT
	/* grid (1,1) = midpoint of grid (0,0) and grid (2,2) */
	move	a0, t0
	addiu	a1, a3, GRID5(2,2)
	addiu	a2, a3, GRID5(1,1)
	MIDPOINT
	/* grid (0,3) = midpoint of grid (0,2) and grid (0,4) */
	addiu	a0, a3, GRID5(0,2)
	move	a1, t1
	addiu	a2, a3, GRID5(0,3)
	MIDPOINT
	/* grid (1,4) = midpoint of grid (0,4) and grid (2,4) */
	move	a0, t1
	addiu	a1, a3, GRID5(2,4)
	addiu	a2, a3, GRID5(1,4)
	MIDPOINT
	/* grid (2,3) = midpoint of grid (2,2) and grid (2,4) */
	addiu	a0, a3, GRID5(2,2)
	addiu	a1, a3, GRID5(2,4)
	addiu	a2, a3, GRID5(2,3)
	MIDPOINT
	/* grid (1,3) = midpoint of grid (0,2) and grid (2,4) */
	addiu	a0, a3, GRID5(0,2)
	addiu	a1, a3, GRID5(2,4)
	addiu	a2, a3, GRID5(1,3)
	MIDPOINT
	/* grid (3,0) = midpoint of grid (2,0) and grid (4,0) */
	addiu	a0, a3, GRID5(2,0)
	addiu	a1, a3, GRID5(4,0)
	addiu	a2, a3, GRID5(3,0)
	MIDPOINT
	/* grid (3,2) = midpoint of grid (2,2) and grid (4,2) */
	addiu	a0, a3, GRID5(2,2)
	addiu	a1, a3, GRID5(4,2)
	addiu	a2, a3, GRID5(3,2)
	MIDPOINT
	/* grid (3,1) = midpoint of grid (2,0) and grid (4,2) */
	addiu	a0, a3, GRID5(2,0)
	addiu	a1, a3, GRID5(4,2)
	addiu	a2, a3, GRID5(3,1)
	MIDPOINT
	/* grid (4,1) = midpoint of grid (4,0) and grid (4,2) */
	addiu	a0, a3, GRID5(4,0)
	addiu	a1, a3, GRID5(4,2)
	addiu	a2, a3, GRID5(4,1)
	MIDPOINT
	/* grid (4,3) = midpoint of grid (4,2) and grid (4,4) */
	addiu	a0, a3, GRID5(4,2)
	addiu	a1, a3, GRID5(4,4)
	addiu	a2, a3, GRID5(4,3)
	MIDPOINT
	/* grid (3,3) = midpoint of grid (2,2) and grid (4,4) */
	addiu	a0, a3, GRID5(2,2)
	addiu	a1, a3, GRID5(4,4)
	addiu	a2, a3, GRID5(3,3)
	MIDPOINT
	/* grid (3,4) = midpoint of grid (2,4) and grid (4,4) */
	addiu	a0, a3, GRID5(2,4)
	addiu	a1, a3, GRID5(4,4)
	addiu	a2, a3, GRID5(3,4)
	MIDPOINT

	/* Transform the whole grid: RotTransPersN(grid, sxy, sz, p, flag, count) */
	move	a0, a3				/* a0 = grid (first RotTransPersN argument) */
	addiu	a1, a3, SXY5(0,0)		/* a1 = sxy output array */
	addiu	a2, a3, SZ5(0,0)		/* a2 = sz output array */
	addiu	v0, a3, SPAD_FLAG		/* v0 = flag output array */
	addiu	a3, a3, SPAD_P			/* a3 = p (depth-cue) output array */
	li	v1, GRID5_COUNT			/* v1 = 5x5 = 25 vertices */
	sw	v0, 16(sp)			/* fifth argument: flag array */
	jal	RotTransPersN			/* RTPS per vertex: sxy, sz (and p, flag) for the grid */
	sw	v1, 20(sp)			/* delay slot (real work): sixth argument: vertex count */
	li	a3, SCRATCHPAD			/* reload a3: it was the fourth RotTransPersN argument */
	nop					/* spare slot (as written) */

	/* Sub-quad emission registers (see subdiv.h) */
	lw	s4, SPAD_COLOUR(a3)		/* s4 = colour word for every sub-quad */
	lw	s5, g_spriteWorkspace		/* s5 = next free primitive (macro: lui + lw) */
	li	s6, POLY_F4_LEN			/* s6 = tag length of every sub-quad */
	li	s7, CODE_POLY_F4_ABE		/* s7 = GPU code of every sub-quad */
	lw	t8, g_renderBuffer		/* t8 = OT base (macro: lui + lw) */

	/* Quadrant rows 0..2, columns 0..2: skip it when its centre (1,1) is too */
	/* close */
	lh	t4, SZ5(1,1)(a3)		/* screen z of (1,1) */
	nop					/* load delay */
	slti	v0, t4, SUBDIV_CLIP_Z		/* v0 = centre z < SUBDIV_CLIP_Z (too close) */
	bnez	v0, $53eb4_quadrant2		/* too close: skip this quadrant */
	nop					/* branch delay: nothing to do */

	/* Sub-quad rows 0..1, columns 0..1 */
	QUAD_OT(SZ5(0,0), SZ5(0,1), SZ5(1,0), SZ5(1,1))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(0,0), SXY5(0,1), SXY5(1,0), SXY5(1,1))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 0..1, columns 1..2 */
	QUAD_OT(SZ5(0,1), SZ5(0,2), SZ5(1,1), SZ5(1,2))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(0,1), SXY5(0,2), SXY5(1,1), SXY5(1,2))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 1..2, columns 0..1 */
	QUAD_OT(SZ5(1,0), SZ5(1,1), SZ5(2,0), SZ5(2,1))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(1,0), SXY5(1,1), SXY5(2,0), SXY5(2,1))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 1..2, columns 1..2 */
	QUAD_OT(SZ5(1,1), SZ5(1,2), SZ5(2,1), SZ5(2,2))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(1,1), SXY5(1,2), SXY5(2,1), SXY5(2,2))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Quadrant rows 0..2, columns 2..4: skip it when its centre (1,3) is too */
	/* close */
$53eb4_quadrant2:
	lh	t4, SZ5(1,3)(a3)		/* screen z of (1,3) */
	nop					/* load delay */
	slti	v0, t4, SUBDIV_CLIP_Z		/* v0 = centre z < SUBDIV_CLIP_Z (too close) */
	bnez	v0, $53eb4_quadrant3		/* too close: skip this quadrant */
	nop					/* branch delay: nothing to do */

	/* Sub-quad rows 0..1, columns 2..3 */
	QUAD_OT(SZ5(0,2), SZ5(0,3), SZ5(1,2), SZ5(1,3))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(0,2), SXY5(0,3), SXY5(1,2), SXY5(1,3))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 0..1, columns 3..4 */
	QUAD_OT(SZ5(0,3), SZ5(0,4), SZ5(1,3), SZ5(1,4))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(0,3), SXY5(0,4), SXY5(1,3), SXY5(1,4))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 1..2, columns 2..3 */
	QUAD_OT(SZ5(1,2), SZ5(1,3), SZ5(2,2), SZ5(2,3))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(1,2), SXY5(1,3), SXY5(2,2), SXY5(2,3))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 1..2, columns 3..4 */
	QUAD_OT(SZ5(1,3), SZ5(1,4), SZ5(2,3), SZ5(2,4))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(1,3), SXY5(1,4), SXY5(2,3), SXY5(2,4))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Quadrant rows 2..4, columns 0..2: skip it when its centre (3,1) is too */
	/* close */
$53eb4_quadrant3:
	lh	t4, SZ5(3,1)(a3)		/* screen z of (3,1) */
	nop					/* load delay */
	slti	v0, t4, SUBDIV_CLIP_Z		/* v0 = centre z < SUBDIV_CLIP_Z (too close) */
	bnez	v0, $53eb4_quadrant4		/* too close: skip this quadrant */
	nop					/* branch delay: nothing to do */

	/* Sub-quad rows 2..3, columns 0..1 */
	QUAD_OT(SZ5(2,0), SZ5(2,1), SZ5(3,0), SZ5(3,1))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(2,0), SXY5(2,1), SXY5(3,0), SXY5(3,1))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 2..3, columns 1..2 */
	QUAD_OT(SZ5(2,1), SZ5(2,2), SZ5(3,1), SZ5(3,2))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(2,1), SXY5(2,2), SXY5(3,1), SXY5(3,2))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 3..4, columns 0..1 */
	QUAD_OT(SZ5(3,0), SZ5(3,1), SZ5(4,0), SZ5(4,1))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(3,0), SXY5(3,1), SXY5(4,0), SXY5(4,1))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 3..4, columns 1..2 */
	QUAD_OT(SZ5(3,1), SZ5(3,2), SZ5(4,1), SZ5(4,2))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(3,1), SXY5(3,2), SXY5(4,1), SXY5(4,2))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Quadrant rows 2..4, columns 2..4: skip it when its centre (3,3) is too */
	/* close */
$53eb4_quadrant4:
	lh	t4, SZ5(3,3)(a3)		/* screen z of (3,3) */
	nop					/* load delay */
	slti	v0, t4, SUBDIV_CLIP_Z		/* v0 = centre z < SUBDIV_CLIP_Z (too close) */
	bnez	v0, $53eb4_publish		/* too close: skip this quadrant */
	nop					/* branch delay: nothing to do */

	/* Sub-quad rows 2..3, columns 2..3 */
	QUAD_OT(SZ5(2,2), SZ5(2,3), SZ5(3,2), SZ5(3,3))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(2,2), SXY5(2,3), SXY5(3,2), SXY5(3,3))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 2..3, columns 3..4 */
	QUAD_OT(SZ5(2,3), SZ5(2,4), SZ5(3,3), SZ5(3,4))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(2,3), SXY5(2,4), SXY5(3,3), SXY5(3,4))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 3..4, columns 2..3 */
	QUAD_OT(SZ5(3,2), SZ5(3,3), SZ5(4,2), SZ5(4,3))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(3,2), SXY5(3,3), SXY5(4,2), SXY5(4,3))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Sub-quad rows 3..4, columns 3..4 */
	QUAD_OT(SZ5(3,3), SZ5(3,4), SZ5(4,3), SZ5(4,4))	/* t4 = OT entry of the farthest corner */
	QUAD_XY_F4(SXY5(3,3), SXY5(3,4), SXY5(4,3), SXY5(4,4))	/* x0..x3,y0..y3 (POLY_F4) */
	QUAD_ADDPRIM				/* colour, tag, code; link into the OT */
	addiu	s5, s5, POLY_F4_SIZE		/* next primitive */

	/* Publish the allocation pointer */
$53eb4_publish:
	sw	s5, g_spriteWorkspace		/* macro: lui AT, then sw through AT */

	/* Restore ra and s0..s7 from the scratchpad and return */
$53eb4_return:
	lw	ra, SPAD_SAVE_RA(a3)		/* restore ra */
	lw	s0, SPAD_SAVE_S0(a3)		/* restore s0 */
	lw	s1, SPAD_SAVE_S1(a3)		/* restore s1 */
	lw	s2, SPAD_SAVE_S2(a3)		/* restore s2 */
	lw	s3, SPAD_SAVE_S3(a3)		/* restore s3 */
	lw	s4, SPAD_SAVE_S4(a3)		/* restore s4 */
	lw	s5, SPAD_SAVE_S5(a3)		/* restore s5 */
	lw	s6, SPAD_SAVE_S6(a3)		/* restore s6 */
	lw	s7, SPAD_SAVE_S7(a3)		/* restore s7 */
	jr	ra				/* return */
	addiu	sp, sp, 80			/* delay slot (real work): pop the frame */
