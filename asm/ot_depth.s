/*
 * ot_depth.s - ordering-table depth of the polygon just transformed by the GTE.
 *
 * Hand-written assembly (AGENTS.md, "Hand-written assembly"): GTE/renderer region only.
 * Built by tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/ot_depth.json.
 *
 * Both routines read the GTE screen-z FIFO left behind by the caller's last perspective
 * transform and return the ordering-table index of the farthest vertex. They are leaf
 * routines: no stack frame, no memory access, only v0, v1 and t1..t3 are written. The
 * same "max z >> OT_SHIFT" sequence is written out inline in every polygon emitter of
 * poly_emit.s; these two are the out-of-line form that C code calls.
 */
#include "gte.h"

	.text
	.set	noreorder	/* every delay slot below is written out by hand */

/*
 * Model_OTIndexTri - ordering-table index of a triangle
 *
 * Purpose:     return max(SZ1, SZ2, SZ3) >> OT_SHIFT, the OT entry of the farthest of
 *              the three vertices of the last RTPT (RotTransPers3 and its variants push
 *              the three screen z values into SZ1..SZ3).
 * Inputs:      GTE SZ1, SZ2, SZ3 (set by the caller's RotTransPers3).
 * Outputs:     v0 = OT index (largest z / 32).
 * Clobbers:    v0, v1, t1, t2. Preserves every other CPU register; touches no memory.
 * Scratchpad:  none.
 * Callers:     Model_DrawFarFT3ABE (0x8001f5f0) and Model_DrawMidFT3ABE (0x8001f9dc), both C, after RotTransPers3:
 *              "ot = &g_renderBuffer->ot[Model_OTIndexTri()]".
 * Callees:     none.
 */
	.globl	Model_OTIndexTri
Model_OTIndexTri:
	mfc2	v0, C2_SZ1		/* v0 = z of vertex 0 */
	mfc2	t1, C2_SZ2		/* t1 = z of vertex 1 */
	mfc2	t2, C2_SZ3		/* t2 = z of vertex 2 */
	nop				/* GTE transfer delay: t2 is not valid in the next slot */

	/* v0 = max(v0, t1); v1 is the comparison flag */
	slt	v1, v0, t1
	beqz	v1, $tri_have_max01
	nop				/* branch delay: nothing to do */
	move	v0, t1
$tri_have_max01:
	/* v0 = max(v0, t2) */
	slt	v1, v0, t2
	beqz	v1, $tri_have_max
	nop				/* branch delay: nothing to do */
	move	v0, t2
$tri_have_max:
	jr	ra
	sra	v0, v0, OT_SHIFT	/* delay slot (real work): return z / 32 */

/*
 * Model_OTIndexQuad - ordering-table index of a quad
 *
 * Purpose:     return max(SZ0, SZ1, SZ2, SZ3) >> OT_SHIFT, the OT entry of the farthest
 *              of the four vertices of a quad transformed as RTPT + RTPS (RotTransPers4:
 *              the RTPS pushes the first vertex's z down into SZ0).
 * Inputs:      GTE SZ0..SZ3.
 * Outputs:     v0 = OT index (largest z / 32).
 * Clobbers:    v0, v1, t1, t2, t3. Preserves every other CPU register; touches no memory.
 * Scratchpad:  none.
 * Callers:     none found: no jal, jump or pointer to 0x80048f8c exists in the executable.
 * Callees:     none.
 */
	.globl	Model_OTIndexQuad
Model_OTIndexQuad:
	mfc2	v0, C2_SZ0		/* v0 = z of vertex 0 */
	mfc2	t1, C2_SZ1		/* t1 = z of vertex 1 */
	mfc2	t2, C2_SZ2		/* t2 = z of vertex 2 */
	mfc2	t3, C2_SZ3		/* t3 = z of vertex 3 */
	nop				/* GTE transfer delay: t3 is not valid in the next slot */

	/* v0 = max(v0, t1) */
	slt	v1, v0, t1
	beqz	v1, $quad_have_max01
	nop				/* branch delay: nothing to do */
	move	v0, t1
$quad_have_max01:
	/* v0 = max(v0, t2) */
	slt	v1, v0, t2
	beqz	v1, $quad_have_max012
	nop				/* branch delay: nothing to do */
	move	v0, t2
$quad_have_max012:
	/* v0 = max(v0, t3) */
	slt	v1, v0, t3
	beqz	v1, $quad_have_max
	nop				/* branch delay: nothing to do */
	move	v0, t3
$quad_have_max:
	jr	ra
	sra	v0, v0, OT_SHIFT	/* delay slot (real work): return z / 32 */
