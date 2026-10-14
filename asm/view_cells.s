/*
 * view_cells.s - draw the map cells inside the view cone, ring by ring
 *
 * Hand-written assembly (AGENTS.md, "Hand-written assembly"): GTE/renderer region only.
 * Built by tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/view_cells.json.
 * Unlike the GTE routines this one keeps a conventional stack frame; it sits at the end of
 * the renderer code, just before the sprite animation tables (sprite_anims.s).
 */
#include <asm.h>

	.text
	.set	noreorder

/*
 * Model_DrawVisibleCells - walk the precomputed rays around the player and draw every visible cell
 *
 * Purpose:    for rings 0..14 around the player's map cell, scan the rays whose angle lies
 *             in a cone around the view angle (g_viewRotation.vy, 0x1000 = one turn): +-0x4ff for
 *             rings 0 and 1, +-0x27f for rings 2..7, +-0x1ff for rings 8..14. Each ray names
 *             a cell at (player x - dx, player z + dz). A cell inside the map whose zone
 *             bits meet the zone mask is marked seen (flags bit 0), adds its zone bits to
 *             the mask (unless it is a type-6 cell whose bytes 13 and 11 differ by less
 *             than 8), and has its face list drawn: rings 0..1 through Model_DrawNear (dispatch
 *             table 0x8006d308), rings 2..7 through Model_DrawMid (g_dispatch_table, the plain
 *             emitters of poly_emit.s), rings 8..14 through Model_DrawFar (the depth-cued
 *             emitters). The widened mask is stored back to g_mask.
 * Inputs:     g_viewCellsLevel = map header (+0x68 width, +0x6a height, +0x78 cell array);
 *             g_viewCellsBuckets = bucket table: per ring, 32 halfword ray indices by angle / 128;
 *             g_viewCellsRays = ray table, 6-byte records { s16 angle, s16 dx, s16 dz } sorted by
 *             angle; g_playerCellX, g_playerCellZ = player cell x, z; g_viewRotation.vy = view angle;
 *             g_mask = zone mask.
 *             Cell (16 bytes): +0 face list (dispatchRow_t *), +7 flags, +8 zone bits,
 *             +0xa type, +0xb and +0xd bytes compared for type 6.
 * Outputs:    g_mask; the cells' flags; primitives from the face renderers.
 * Clobbers:   v0, v1, a0..a2, t0..t4, hi/lo, and whatever the face renderers clobber.
 * Preserves:  s0..s7, ra (stack), sp.
 * Stack:      0x14 cone start, 0x18 ring, 0x24 ray pointer, 0x28 cone end (across calls);
 *             0x2c..0x4c saved s7..s0 and ra.
 * Scratchpad: none directly (the face renderers use it).
 * Callers:    jal at 0x80020a60 (C, after marking the player's own cell seen).
 * Callees:    Model_DrawNear, Model_DrawMid, Model_DrawFar (C face-list dispatchers).
 */
	.globl	Model_DrawVisibleCells
Model_DrawVisibleCells:

	/* Prologue: save ra and s0..s7 on the stack */
	addiu	sp, sp, -80		/* frame: s0..s7, ra and four locals */
	sw	ra, 76(sp)		/* save ra */
	sw	s0, 68(sp)		/* save s0 */
	sw	s1, 72(sp)		/* save s1 */
	sw	s2, 64(sp)		/* save s2 */
	sw	s3, 60(sp)		/* save s3 */
	sw	s4, 56(sp)		/* save s4 */
	sw	s5, 52(sp)		/* save s5 */
	sw	s6, 48(sp)		/* save s6 */
	sw	s7, 44(sp)		/* save s7 */

	/* Long-lived state: */
	/*   s0 = map header (then its cell array), s1 = ring/angle bucket table, */
	/*   s2 = ray table, s3/s4 = player cell x/z, s5 = zone mask (g_mask), */
	/*   s6/s7 = map width/height */
	lw	s0, g_viewCellsLevel		/* s0 = map header (macro: lui + lw) */
	lw	s1, g_viewCellsBuckets		/* s1 = ring/angle bucket table (macro: lui + lw) */
	lw	s2, g_viewCellsRays		/* s2 = ray table (macro: lui + lw) */
	lw	s3, g_playerCellX		/* s3 = player cell x (macro: lui + lw) */
	lw	s4, g_playerCellZ		/* s4 = player cell z (macro: lui + lw) */
	lh	s5, g_visibleCellMask		/* s5 = zone mask (macro: lui + lh) */
	lh	s6, 104(s0)		/* s6 = map width (cells) */
	lh	s7, 106(s0)		/* s7 = map height (cells) */

	/* ==== Ring 0: cone of +-0x4ff around the view angle; faces drawn by Model_DrawNear ==== */

	/* Cone: t3 = angle - 0x4ff, t4 = angle + 0x4ff, both shifted up a turn when the */
	/* start is negative, so the scan below never wraps */
	lh	t4, g_viewRotation+2	/* t4 = view angle, g_viewRotation.vy (macro: lui + lh) */
	lw	s0, 120(s0)		/* s0 = cell array (16 bytes per cell, row-major) */
	move	t3, t4			/* t3 = view angle too */
	addi	t4, t4, 1279		/* t4 = cone end: angle + 0x4ff */
	addi	t3, t3, -1279		/* t3 = cone start: angle - 0x4ff */
	bgez	t3, $5533c_553c4	/* start not negative: keep the cone as it is */
	nop				/* branch delay: nothing to do */
	addi	t4, t4, 4096		/* start below 0: shift the cone up a full turn */
	addi	t3, t3, 4096		/* (0x1000 = 360 degrees) */
$5533c_553c4:
	sw	t4, 40(sp)		/* keep the cone end (t4) across the call below */

	/* First ray of the ring whose angle can be inside the cone (bucket table) */
	move	t2, t3
	sra	t2, t2, 7		/* t2 = cone start / 128: one of 32 angle buckets */
	sll	t2, t2, 1		/* halfword index */
	addu	a0, s1, t2		/* a0 = &bucket[0][cone start] (ring 0 row) */
	lh	t0, 0(a0)		/* t0 = first ray of that bucket */
	nop				/* load delay */
	sll	t0, t0, 1
	sll	t1, t0, 1
	addu	t0, t0, t1		/* t0 = ray * 6 (6-byte ray records) */
	addu	t2, s2, t0		/* t2 = &ray[first]: { s16 angle, s16 dx, s16 dz } */

	/* Skip rays before the cone start */
$5533c_553f0:
	lh	t0, 0(t2)		/* t0 = ray angle */
	nop				/* load delay */
	slt	v0, t0, t3		/* before the cone start? */
	beqz	v0, $5533c_5540c	/* not before the start: begin the scan */
	nop				/* branch delay: nothing to do */
	j	$5533c_553f0		/* loop */
	addiu	t2, t2, 6		/* delay slot (real work): next ray */

	/* Scan rays up to the cone end; each ray names one cell (dx, dz from the player) */
$5533c_5540c:
	slt	v0, t4, t0		/* past the cone end? */
	bnez	v0, $5533c_554cc	/* past the cone end: this ring is done */
	lh	t0, 2(t2)		/* delay slot (real work): t0 = ray dx */
	lh	t1, 4(t2)		/* t1 = ray dz */
	sub	t0, s3, t0		/* x = player x - dx */
	add	t1, s4, t1		/* z = player z + dz */
	sltu	v0, t0, s6		/* 0 <= x < width (unsigned test catches x < 0) */
	beqz	v0, $5533c_554b8	/* off the map: next ray */
	sltu	v0, t1, s7		/* delay slot (real work): 0 <= z < height */
	beqz	v0, $5533c_554b8	/* off the map: next ray */
	mult	t1, s6			/* delay slot (real work): z * width */
	mflo	t1
	sll	t0, t0, 4		/* x * 16 */
	sll	t1, t1, 4		/* z * width * 16 */
	addu	v0, s0, t0
	addu	v0, v0, t1		/* v0 = &cell[z][x] */
	lh	t1, 8(v0)		/* t1 = the cell's zone bits */
	lw	a0, 0(v0)		/* a0 = the cell's face list (dispatchRow_t *), or 0 */

	/* Visible only if it lies in a zone already seen. A type-6 cell whose bytes 13 */
	/* and 11 differ by less than 8 (a nearly closed door, by the look of it) is */
	/* drawn but does not pass its zones on */
	and	t0, t1, s5		/* in a zone seen so far? */
	beqz	t0, $5533c_554b8	/* zone not seen: next ray */
	lbu	a1, 10(v0)		/* delay slot (real work): a1 = cell type */
	li	a2, 6			/* type 6? */
	bne	a1, a2, $5533c_5548c	/* not type 6 */
	lbu	a1, 11(v0)		/* delay slot (real work): a1 = byte 11 */
	lbu	a2, 13(v0)		/* a2 = byte 13 */
	nop				/* load delay */
	sub	a1, a2, a1
	li	a2, 8
	sltu	a1, a1, a2		/* byte 13 - byte 11 < 8 ? */
	beqz	a1, $5533c_5548c	/* gap of 8 or more: keep its zones */
	nop				/* branch delay: nothing to do */
	move	t1, zero		/* then this cell passes no zone bits on */

	/* Mark it seen, widen the zone mask, and draw its faces if it has any */
$5533c_5548c:
	lbu	a2, 7(v0)
	nop				/* load delay */
	ori	a2, a2, 1
	sb	a2, 7(v0)		/* mark the cell seen (flags bit 0) */
	nop				/* spare slot (as written) */
	beqz	a0, $5533c_554b8	/* no faces: next ray */
	or	s5, s5, t1		/* delay slot (real work): zones seen |= the cell's zones */
	jal	Model_DrawNear			/* draw the cell's faces (a0 = face list) */
	sw	t2, 36(sp)		/* delay slot (real work): keep the ray pointer across the call */
	lw	t2, 36(sp)		/* restore the ray pointer */
	lw	t4, 40(sp)		/* restore the cone end */
$5533c_554b8:
	addi	t2, t2, 6		/* next ray */
	lh	t0, 0(t2)		/* t0 = ray angle */
	nop				/* load delay */
	j	$5533c_5540c		/* loop */
	nop				/* branch delay: nothing to do */

	/* ==== Ring 1: the same cone; faces drawn by Model_DrawNear ==== */
$5533c_554cc:
	lh	t4, g_viewRotation+2	/* t4 = view angle, g_viewRotation.vy (macro: lui + lh) */
	li	v1, 1			/* v1 = ring 1 */
	move	t3, t4			/* t3 = view angle too */
	addi	t4, t4, 1279		/* t4 = cone end: angle + 0x4ff */
	addi	t3, t3, -1279		/* t3 = cone start: angle - 0x4ff */
	bgez	t3, $5533c_554f4	/* start not negative: keep the cone as it is */
	nop				/* branch delay: nothing to do */
	addi	t4, t4, 4096		/* start below 0: shift the cone up a full turn */
	addi	t3, t3, 4096		/* (0x1000 = 360 degrees) */
$5533c_554f4:
	sw	t4, 40(sp)		/* keep the cone end (t4) across the call below */
	sw	t3, 20(sp)		/* keep the cone start for the next ring */
	move	t2, t3
	sra	t2, t2, 7		/* t2 = cone start / 128: one of 32 angle buckets */
	sll	t2, t2, 1		/* halfword index */
	sll	t1, v1, 6		/* t1 = ring * 64: 32 halfwords per ring */
	addu	a0, s1, t1
	addu	a0, a0, t2		/* a0 = &bucket[ring][cone start] */
	lh	t0, 0(a0)		/* t0 = first ray of that bucket */
	sw	v1, 24(sp)		/* keep the ring number across the calls */
	sll	t0, t0, 1
	sll	t1, t0, 1
	addu	t0, t0, t1		/* t0 = ray * 6 (6-byte ray records) */
	addu	t2, s2, t0		/* t2 = &ray[first]: { s16 angle, s16 dx, s16 dz } */
$5533c_5552c:
	lh	t0, 0(t2)		/* t0 = ray angle */
	nop				/* load delay */
	slt	v0, t0, t3		/* before the cone start? */
	beqz	v0, $5533c_55548	/* not before the start: begin the scan */
	nop				/* branch delay: nothing to do */
	j	$5533c_5552c		/* loop */
	addiu	t2, t2, 6		/* delay slot (real work): next ray */
$5533c_55548:
	slt	v0, t4, t0		/* past the cone end? */
	bnez	v0, $5533c_55608	/* past the cone end: this ring is done */
	lh	t0, 2(t2)		/* delay slot (real work): t0 = ray dx */
	lh	t1, 4(t2)		/* t1 = ray dz */
	sub	t0, s3, t0		/* x = player x - dx */
	add	t1, s4, t1		/* z = player z + dz */
	sltu	v0, t0, s6		/* 0 <= x < width (unsigned test catches x < 0) */
	beqz	v0, $5533c_555f4	/* off the map: next ray */
	sltu	v0, t1, s7		/* delay slot (real work): 0 <= z < height */
	beqz	v0, $5533c_555f4	/* off the map: next ray */
	mult	t1, s6			/* delay slot (real work): z * width */
	mflo	t1
	sll	t0, t0, 4		/* x * 16 */
	sll	t1, t1, 4		/* z * width * 16 */
	addu	v0, s0, t0
	addu	v0, v0, t1		/* v0 = &cell[z][x] */
	lh	t1, 8(v0)		/* t1 = the cell's zone bits */
	lw	a0, 0(v0)		/* a0 = the cell's face list (dispatchRow_t *), or 0 */
	and	t0, t1, s5		/* in a zone seen so far? */
	beqz	t0, $5533c_555f4	/* zone not seen: next ray */
	lbu	a1, 10(v0)		/* delay slot (real work): a1 = cell type */
	li	a2, 6			/* type 6? */
	bne	a1, a2, $5533c_555c8	/* not type 6 */
	lbu	a1, 11(v0)		/* delay slot (real work): a1 = byte 11 */
	lbu	a2, 13(v0)		/* a2 = byte 13 */
	nop				/* load delay */
	sub	a1, a2, a1
	li	a2, 8
	sltu	a1, a1, a2		/* byte 13 - byte 11 < 8 ? */
	beqz	a1, $5533c_555c8	/* gap of 8 or more: keep its zones */
	nop				/* branch delay: nothing to do */
	move	t1, zero		/* then this cell passes no zone bits on */
$5533c_555c8:
	lbu	a2, 7(v0)
	nop				/* load delay */
	ori	a2, a2, 1
	sb	a2, 7(v0)		/* mark the cell seen (flags bit 0) */
	nop				/* spare slot (as written) */
	beqz	a0, $5533c_555f4	/* no faces: next ray */
	or	s5, s5, t1		/* delay slot (real work): zones seen |= the cell's zones */
	jal	Model_DrawNear			/* draw the cell's faces (a0 = face list) */
	sw	t2, 36(sp)		/* delay slot (real work): keep the ray pointer across the call */
	lw	t2, 36(sp)		/* restore the ray pointer */
	lw	t4, 40(sp)		/* restore the cone end */
$5533c_555f4:
	addi	t2, t2, 6		/* next ray */
	lh	t0, 0(t2)		/* t0 = ray angle */
	nop				/* load delay */
	j	$5533c_55548		/* loop */
	nop				/* branch delay: nothing to do */

	/* ==== Rings 2..7: cone of +-0x27f; faces drawn by Model_DrawMid (plain emitters) ==== */
$5533c_55608:
	lh	t4, g_viewRotation+2	/* t4 = view angle, g_viewRotation.vy (macro: lui + lh) */
	li	v1, 2			/* v1 = ring 2 */
	move	t3, t4			/* t3 = view angle too */
	addi	t4, t4, 639		/* t4 = cone end: angle + 0x27f */
	addi	t3, t3, -639		/* t3 = cone start: angle - 0x27f */
	bgez	t3, $5533c_55630	/* start not negative: keep the cone as it is */
	nop				/* branch delay: nothing to do */
	addi	t4, t4, 4096		/* start below 0: shift the cone up a full turn */
	addi	t3, t3, 4096		/* (0x1000 = 360 degrees) */
$5533c_55630:
	sw	t4, 40(sp)		/* keep the cone end (t4) across the call below */
	sw	t3, 20(sp)		/* keep the cone start for the next ring */

	/* Per ring: first ray from the bucket table row of this ring */
$5533c_55638:
	move	t2, t3
	sra	t2, t2, 7		/* t2 = cone start / 128: one of 32 angle buckets */
	sll	t2, t2, 1		/* halfword index */
	sll	t1, v1, 6		/* t1 = ring * 64: 32 halfwords per ring */
	addu	a0, s1, t1
	addu	a0, a0, t2		/* a0 = &bucket[ring][cone start] */
	lh	t0, 0(a0)		/* t0 = first ray of that bucket */
	sw	v1, 24(sp)		/* keep the ring number across the calls */
	sll	t0, t0, 1
	sll	t1, t0, 1
	addu	t0, t0, t1		/* t0 = ray * 6 (6-byte ray records) */
	addu	t2, s2, t0		/* t2 = &ray[first]: { s16 angle, s16 dx, s16 dz } */

	/* Skip rays before the cone start */
$5533c_55668:
	lh	t0, 0(t2)		/* t0 = ray angle */
	nop				/* load delay */
	slt	v0, t0, t3		/* before the cone start? */
	beqz	v0, $5533c_55684	/* not before the start: begin the scan */
	nop				/* branch delay: nothing to do */
	j	$5533c_55668		/* loop */
	addiu	t2, t2, 6		/* delay slot (real work): next ray */

	/* Scan rays up to the cone end (as in ring 0) */
$5533c_55684:
	slt	v0, t4, t0		/* past the cone end? */
	bnez	v0, $5533c_55744	/* past the cone end: this ring is done */
	lh	t0, 2(t2)		/* delay slot (real work): t0 = ray dx */
	lh	t1, 4(t2)		/* t1 = ray dz */
	sub	t0, s3, t0		/* x = player x - dx */
	add	t1, s4, t1		/* z = player z + dz */
	sltu	v0, t0, s6		/* 0 <= x < width (unsigned test catches x < 0) */
	beqz	v0, $5533c_55730	/* off the map: next ray */
	sltu	v0, t1, s7		/* delay slot (real work): 0 <= z < height */
	beqz	v0, $5533c_55730	/* off the map: next ray */
	mult	t1, s6			/* delay slot (real work): z * width */
	mflo	t1
	sll	t0, t0, 4		/* x * 16 */
	sll	t1, t1, 4		/* z * width * 16 */
	addu	v0, s0, t0
	addu	v0, v0, t1		/* v0 = &cell[z][x] */
	lh	t1, 8(v0)		/* t1 = the cell's zone bits */
	lw	a0, 0(v0)		/* a0 = the cell's face list (dispatchRow_t *), or 0 */
	and	t0, t1, s5		/* in a zone seen so far? */
	beqz	t0, $5533c_55730	/* zone not seen: next ray */
	lbu	a1, 10(v0)		/* delay slot (real work): a1 = cell type */
	li	a2, 6			/* type 6? */
	bne	a1, a2, $5533c_55704	/* not type 6 */
	lbu	a1, 11(v0)		/* delay slot (real work): a1 = byte 11 */
	lbu	a2, 13(v0)		/* a2 = byte 13 */
	nop				/* load delay */
	sub	a1, a2, a1
	li	a2, 8
	sltu	a1, a1, a2		/* byte 13 - byte 11 < 8 ? */
	beqz	a1, $5533c_55704	/* gap of 8 or more: keep its zones */
	nop				/* branch delay: nothing to do */
	move	t1, zero		/* then this cell passes no zone bits on */
$5533c_55704:
	lbu	a2, 7(v0)
	nop				/* load delay */
	ori	a2, a2, 1
	sb	a2, 7(v0)		/* mark the cell seen (flags bit 0) */
	nop				/* spare slot (as written) */
	beqz	a0, $5533c_55730	/* no faces: next ray */
	or	s5, s5, t1		/* delay slot (real work): zones seen |= the cell's zones */
	jal	Model_DrawMid			/* draw the cell's faces (a0 = face list) */
	sw	t2, 36(sp)		/* delay slot (real work): keep the ray pointer across the call */
	lw	t2, 36(sp)		/* restore the ray pointer */
	lw	t4, 40(sp)		/* restore the cone end */
$5533c_55730:
	addi	t2, t2, 6		/* next ray */
	lh	t0, 0(t2)		/* t0 = ray angle */
	nop				/* load delay */
	j	$5533c_55684		/* loop */
	nop				/* branch delay: nothing to do */

	/* Next ring */
$5533c_55744:
	lw	v1, 24(sp)		/* ring number */
	lw	t3, 20(sp)		/* cone start (unchanged for these rings) */
	addiu	v1, v1, 1		/* next ring */
	slti	v0, v1, 8		/* rings 2..7 */
	bnez	v0, $5533c_55638	/* more rings */
	nop				/* branch delay: nothing to do */

	/* ==== Rings 8..14: cone of +-0x1ff; faces drawn by Model_DrawFar (depth-cued emitters) ==== */
	lh	t4, g_viewRotation+2	/* t4 = view angle, g_viewRotation.vy (macro: lui + lh) */
	nop				/* load delay */
	move	t3, t4			/* t3 = view angle too */
	addi	t4, t4, 511		/* t4 = cone end: angle + 0x1ff */
	addi	t3, t3, -511		/* t3 = cone start: angle - 0x1ff */
	bgez	t3, $5533c_55784	/* start not negative: keep the cone as it is */
	nop				/* branch delay: nothing to do */
	addi	t4, t4, 4096		/* start below 0: shift the cone up a full turn */
	addi	t3, t3, 4096		/* (0x1000 = 360 degrees) */
$5533c_55784:
	sw	t4, 40(sp)		/* keep the cone end (t4) across the call below */
	sw	t3, 20(sp)		/* keep the cone start for the next ring */

	/* Per ring: first ray from the bucket table row of this ring */
$5533c_5578c:
	move	t2, t3
	sra	t2, t2, 7		/* t2 = cone start / 128: one of 32 angle buckets */
	sll	t2, t2, 1		/* halfword index */
	sll	t1, v1, 6		/* t1 = ring * 64: 32 halfwords per ring */
	addu	a0, s1, t1
	addu	a0, a0, t2		/* a0 = &bucket[ring][cone start] */
	lh	t0, 0(a0)		/* t0 = first ray of that bucket */
	sw	v1, 24(sp)		/* keep the ring number across the calls */
	sll	t0, t0, 1
	sll	t1, t0, 1
	addu	t0, t0, t1		/* t0 = ray * 6 (6-byte ray records) */
	addu	t2, s2, t0		/* t2 = &ray[first]: { s16 angle, s16 dx, s16 dz } */

	/* Skip rays before the cone start */
$5533c_557bc:
	lh	t0, 0(t2)		/* t0 = ray angle */
	nop				/* load delay */
	slt	v0, t0, t3		/* before the cone start? */
	beqz	v0, $5533c_557d8	/* not before the start: begin the scan */
	nop				/* branch delay: nothing to do */
	j	$5533c_557bc		/* loop */
	addiu	t2, t2, 6		/* delay slot (real work): next ray */

	/* Scan rays up to the cone end (as in ring 0) */
$5533c_557d8:
	slt	v0, t4, t0		/* past the cone end? */
	bnez	v0, $5533c_55898	/* past the cone end: this ring is done */
	lh	t0, 2(t2)		/* delay slot (real work): t0 = ray dx */
	lh	t1, 4(t2)		/* t1 = ray dz */
	sub	t0, s3, t0		/* x = player x - dx */
	add	t1, s4, t1		/* z = player z + dz */
	sltu	v0, t0, s6		/* 0 <= x < width (unsigned test catches x < 0) */
	beqz	v0, $5533c_55884	/* off the map: next ray */
	sltu	v0, t1, s7		/* delay slot (real work): 0 <= z < height */
	beqz	v0, $5533c_55884	/* off the map: next ray */
	mult	t1, s6			/* delay slot (real work): z * width */
	mflo	t1
	sll	t0, t0, 4		/* x * 16 */
	sll	t1, t1, 4		/* z * width * 16 */
	addu	v0, s0, t0
	addu	v0, v0, t1		/* v0 = &cell[z][x] */
	lh	t1, 8(v0)		/* t1 = the cell's zone bits */
	lw	a0, 0(v0)		/* a0 = the cell's face list (dispatchRow_t *), or 0 */
	and	t0, t1, s5		/* in a zone seen so far? */
	beqz	t0, $5533c_55884	/* zone not seen: next ray */
	lbu	a1, 10(v0)		/* delay slot (real work): a1 = cell type */
	li	a2, 6			/* type 6? */
	bne	a1, a2, $5533c_55858	/* not type 6 */
	lbu	a1, 11(v0)		/* delay slot (real work): a1 = byte 11 */
	lbu	a2, 13(v0)		/* a2 = byte 13 */
	nop				/* load delay */
	sub	a1, a2, a1
	li	a2, 8
	sltu	a1, a1, a2		/* byte 13 - byte 11 < 8 ? */
	beqz	a1, $5533c_55858	/* gap of 8 or more: keep its zones */
	nop				/* branch delay: nothing to do */
	move	t1, zero		/* then this cell passes no zone bits on */
$5533c_55858:
	lbu	a2, 7(v0)
	nop				/* load delay */
	ori	a2, a2, 1
	sb	a2, 7(v0)		/* mark the cell seen (flags bit 0) */
	nop				/* spare slot (as written) */
	beqz	a0, $5533c_55884	/* no faces: next ray */
	or	s5, s5, t1		/* delay slot (real work): zones seen |= the cell's zones */
	jal	Model_DrawFar			/* draw the cell's faces (a0 = face list) */
	sw	t2, 36(sp)		/* delay slot (real work): keep the ray pointer across the call */
	lw	t2, 36(sp)		/* restore the ray pointer */
	lw	t4, 40(sp)		/* restore the cone end */
$5533c_55884:
	addi	t2, t2, 6		/* next ray */
	lh	t0, 0(t2)		/* t0 = ray angle */
	nop				/* load delay */
	j	$5533c_557d8		/* loop */
	nop				/* branch delay: nothing to do */

	/* Next ring */
$5533c_55898:
	lw	v1, 24(sp)		/* ring number */
	lw	t3, 20(sp)		/* cone start (unchanged for these rings) */
	addiu	v1, v1, 1		/* next ring */
	slti	v0, v1, 15		/* rings 8..14 */
	bnez	v0, $5533c_5578c	/* more rings */
	nop				/* branch delay: nothing to do */

	/* Store the widened zone mask for the next frame, restore and return */
	sh	s5, g_visibleCellMask		/* macro: lui AT, then sh through AT */
	lw	ra, 76(sp)		/* restore ra */
	lw	s0, 68(sp)		/* restore s0 */
	lw	s1, 72(sp)		/* restore s1 */
	lw	s2, 64(sp)		/* restore s2 */
	lw	s3, 60(sp)		/* restore s3 */
	lw	s4, 56(sp)		/* restore s4 */
	lw	s5, 52(sp)		/* restore s5 */
	lw	s6, 48(sp)		/* restore s6 */
	lw	s7, 44(sp)		/* restore s7 */
	nop				/* load delay */
	jr	ra			/* return */
	addiu	sp, sp, 80		/* delay slot (real work): pop the frame */
