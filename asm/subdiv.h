/*
 * subdiv.h - scratchpad layout and building blocks of the quad-subdivision renderers
 * (subdiv_*.s, 0x8004a6b4..0x8005533b).
 *
 * Each renderer draws one textured (or flat) quad that is near the camera by cutting it
 * into a grid of smaller quads, so that the PlayStation's affine texture mapping and the
 * GTE's z clipping behave better close up:
 *   - 3x3 vertex grid (2x2 sub-quads) when every corner is at z >= SUBDIV_NEAR_Z;
 *   - 5x5 vertex grid (4x4 sub-quads, handled as four 2x2 quadrants) when any corner is
 *     nearer than that.
 * The grid is built in the scratchpad, transformed in one RotTransPersN call, and each
 * sub-quad becomes a primitive with interpolated u,v.
 */
#include "gte.h"

/* ------------------------------------------------------------------ scratchpad layout
 * The renderers keep everything in the 1 KB scratchpad at 0x1f800000 (a3 holds its base
 * for the whole routine, reloaded after every call that takes a3 as an argument).
 */
#define SCRATCHPAD	0x1f800000

#define SPAD_GRID	0x000	/* SVECTOR grid[25]: x,y,z,pad per vertex, row-major */
#define SPAD_SXY	0x0c8	/* long sxy[25]: screen x,y from RotTransPersN */
#define SPAD_SZ		0x12c	/* short sz[25]: screen z from RotTransPersN */
#define SPAD_FLAG	0x15e	/* RotTransPersN flag output (not read here) */
#define SPAD_P		0x190	/* RotTransPersN depth-cue output (not read here) */
#define SPAD_SAVE_RA	0x1c4	/* callee-saved registers: ra, then s0..s7 */
#define SPAD_SAVE_S0	0x1c8
#define SPAD_SAVE_S1	0x1cc
#define SPAD_SAVE_S2	0x1d0
#define SPAD_SAVE_S3	0x1d4
#define SPAD_SAVE_S4	0x1d8
#define SPAD_SAVE_S5	0x1dc
#define SPAD_SAVE_S6	0x1e0
#define SPAD_SAVE_S7	0x1e4
#define SPAD_COLOUR	0x1e8	/* the quad's colour word (r,g,b,code) */
#define SPAD_TEXREC	0x1ec	/* pointer to the caller's 16-byte texture record */
#define SPAD_TEX	0x1f0	/* copy of that record: u0v0+clut, u1v1+tpage, u2v2, u3v3 */
#define SPAD_U_STEP	0x200	/* 5x5 only: u at 1/4, 1/2, 3/4 of the u0..u1 span (bytes) */
#define SPAD_V_STEP	0x203	/* 5x5 only: v at 1/4, 1/2, 3/4 of the v0..v2 span (bytes) */

/* Grid addressing: vertex (r,c) of a 3x3 or 5x5 grid, row-major, r and c from 0 */
#define GRID3(r,c)	(SPAD_GRID+8*((r)*3+(c)))
#define GRID5(r,c)	(SPAD_GRID+8*((r)*5+(c)))
#define SXY3(r,c)	(SPAD_SXY+4*((r)*3+(c)))
#define SXY5(r,c)	(SPAD_SXY+4*((r)*5+(c)))
#define SZ3(r,c)	(SPAD_SZ+2*((r)*3+(c)))
#define SZ5(r,c)	(SPAD_SZ+2*((r)*5+(c)))

/* Distances (screen z) that steer the subdivision */
#define SUBDIV_NEAR_Z	0x100	/* nearest corner below this: 5x5 grid instead of 3x3 */
#define SUBDIV_CLIP_Z	0x20	/* (sub)quad centre below this: too close, not drawn */
#define GRID3_COUNT	9
#define GRID5_COUNT	25

/* u,v bytes of a textured quad primitive (Runtime Library 3.3 POLY_FT4) */
#define P_U0	12
#define P_V0	13
#define P_U1	20
#define P_V1	21
#define P_U2	28
#define P_V2	29
#define P_U3	36
#define P_V3	37

/* ------------------------------------------------------------------ building blocks
 * The renderers repeat a handful of fixed instruction sequences many times; they are
 * written here once as macros. Each expands to exactly the instructions shown (one line
 * per macro, ';' separated; the numeric labels "1:" / "1f" are local to the expansion).
 * Register contract inside the sub-quad emitters:
 *   a3 = SCRATCHPAD, s0..s3 = texture record words, s4 = colour word,
 *   s5 = next free primitive, s6 = tag length, s7 = GPU code, t8 = OT base.
 */

/*
 * COPY_VERTEX(src, dst): copy the SVECTOR at src (a vertex pointer register) to the grid
 * slot dst: x,y as one word, z as a halfword (the pad is not copied). Uses t4, t5.
 */
#define COPY_VERTEX(src, dst) \
	lw	t4, 0(src) ; \
	lh	t5, 4(src) ; \
	sw	t4, dst(a3) ; \
	sh	t5, dst+4(a3)

/*
 * MIDPOINT: *a2 = (*a0 + *a1) / 2 for the x, y and z of two SVECTORs (arithmetic shift,
 * so it rounds towards minus infinity). The caller sets a0, a1 and a2 just before.
 * Uses s0..s5, which are free at that point: the texture and colour registers are
 * loaded after the grid is built.
 */
#define MIDPOINT \
	lh	s0, 0(a0) ; lh	s1, 2(a0) ; lh	s2, 4(a0) ; \
	lh	s3, 0(a1) ; lh	s4, 2(a1) ; lh	s5, 4(a1) ; \
	add	s0, s0, s3 ; add	s1, s1, s4 ; add	s2, s2, s5 ; \
	sra	s0, s0, 1 ; sra	s1, s1, 1 ; sra	s2, s2, 1 ; \
	sh	s0, 0(a2) ; sh	s1, 2(a2) ; sh	s2, 4(a2)

/*
 * QUAD_OT(z0, z1, z2, z3): t4 = &ot[max(sz) >> OT_SHIFT] for the four screen-z slots
 * (scratchpad offsets) of a sub-quad; t8 is the OT base. AT is the comparison scratch.
 * Uses t4..t7.
 */
#define QUAD_OT(z0, z1, z2, z3) \
	lh	t4, z0(a3) ; lh	t5, z1(a3) ; lh	t6, z2(a3) ; lh	t7, z3(a3) ; \
	.set	noat ; \
	slt	AT, t4, t5 ; beqz	AT, 1f ; nop ; move	t4, t5 ; 1: \
	slt	AT, t4, t6 ; beqz	AT, 1f ; nop ; move	t4, t6 ; 1: \
	slt	AT, t4, t7 ; beqz	AT, 1f ; nop ; move	t4, t7 ; 1: \
	.set	at ; \
	sra	t4, t4, OT_SHIFT ; sll	t4, t4, 2 ; addu	t4, t4, t8

/*
 * QUAD_XY(p0, p1, p2, p3): copy four screen x,y words (scratchpad offsets) into the
 * primitive at s5 as x0,y0 .. x3,y3 of a POLY_FT4. QUAD_XY_F4 does the same for the
 * tighter POLY_F4 layout. Uses t0..t3.
 */
#define QUAD_XY(p0, p1, p2, p3) \
	lw	t0, p0(a3) ; lw	t1, p1(a3) ; lw	t2, p2(a3) ; lw	t3, p3(a3) ; \
	sw	t0, P_XY0(s5) ; sw	t1, P_XY1(s5) ; sw	t2, P_XY2(s5) ; sw	t3, P_XY3(s5)
#define QUAD_XY_F4(p0, p1, p2, p3) \
	lw	t0, p0(a3) ; lw	t1, p1(a3) ; lw	t2, p2(a3) ; lw	t3, p3(a3) ; \
	sw	t0, 8(s5) ; sw	t1, 12(s5) ; sw	t2, 16(s5) ; sw	t3, 20(s5)

/*
 * QUAD_UVWORDS: the four texture record words into the primitive's uv0+clut, uv1+tpage,
 * uv2, uv3 words. The caller then overwrites the u or v bytes that this sub-quad
 * interpolates.
 */
#define QUAD_UVWORDS \
	sw	s0, P_UV0(s5) ; sw	s1, P_UV1(s5) ; sw	s2, P_UV2(s5) ; sw	s3, P_UV3(s5)

/*
 * QUAD_ADDPRIM: addPrim(t4, s5) with the colour word, tag length and GPU code:
 * prim->tag = *ot, *ot = prim, the length byte also into the OT entry's top byte (as
 * poly_emit.s does), then colour, length and code. Uses t0.
 */
#define QUAD_ADDPRIM \
	lw	t0, 0(t4) ; sw	s5, 0(t4) ; sw	t0, P_TAG(s5) ; sw	s4, P_RGB0(s5) ; \
	sb	s6, P_LEN(t4) ; sb	s6, P_LEN(s5) ; sb	s7, P_CODE(s5)

/*
 * UV_STEP(a, b, shift): t0 = byte a, t1 = (|byte b - byte a| + 1) >> shift, both bytes
 * from the scratchpad copy of the texture record. The caller adds t1 to t0 once (3x3:
 * shift 1, the midpoint) or three times (5x5: shift 2, the quarter points). The
 * absolute value means a mirrored record still yields a positive step from its first
 * corner.
 */
#define UV_STEP(a, b, shift) \
	lbu	t0, a(a3) ; lbu	t1, b(a3) ; nop ; \
	sub	t1, t1, t0 ; bgez	t1, 1f ; nop ; neg	t1, t1 ; 1: \
	addiu	t1, t1, 1 ; sra	t1, t1, shift
