/*
 * libgte_mtx_00.s - LIBGTE module MTX_00: matrix composition, the matrix stack, GTE matrix reads.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libgte_mtx_00.json and the
 * "libgte_mtx_00" entry of config/data.json; counted as "library assembly", never as matched C
 * and never as certified library.
 *
 * Library:     LIBGTE.LIB (Sony Runtime Library, geometry), module MTX_00.
 * Exports:     CompMatrix (.text+0x000, 352 bytes, 0x80066470), MulMatrix0 (+0x160, 268),
 *              MulRotMatrix0 (+0x26c, 228), MulRotMatrix (+0x350, 232), SetMulMatrix (+0x438,
 *              272), ApplyMatrixLV (+0x548, 352), ApplyRotMatrix (+0x6a8, 48), ScaleMatrixL
 *              (+0x6d8, 292), PushMatrix (+0x7fc, 160), PopMatrix (+0x89c, 160), ReadRotMatrix
 *              (+0x93c, 72), ReadLightMatrix (+0x984, 72), ReadColorMatrix (+0x9cc, 84).
 * Imports:     printf (0x80069880).
 * Own data:    .data 784 bytes at 0x800766f4..0x80076a03: the matrix stack and its offset, the
 *              saved return address of PushMatrix/PopMatrix, and three messages. No names in
 *              the archive: the labels are address names.
 *
 * Nearest archived version: Runtime Library 3.3 LIBGTE.LIB MTX_00 (2592 bytes .text, 784 bytes
 * .data, the same thirteen exports in the same order). Three words differ
 * (analysis/data_survey/moddiff.py), all in CompMatrix: the mfc2 at 0x80066594/98/9c read
 * IR1..3 (GTE data registers 9..11) where 3.3 reads MAC1..3 (25..27). The .data is
 * byte-identical. So the module is "Runtime 3.3 MTX_00 with CompMatrix's translation read from
 * IR1..3 instead of MAC1..3". The 3.3 object was assembled by ASMPSX (sections numbered from
 * 0xf000): the routines are hand-written GTE code, not compiled C.
 *
 * Split HI16/LO16 pairs: PushMatrix and PopMatrix load an address with the lui before a jal and
 * the addiu in its delay slot (printf's message), and with an addu between lui and addiu (the
 * stack entry). ASPSX writes HI16/LO16 relocations only for its own back-to-back macros, so
 * these halves are written with hilo.h (HI16/LO16: PsyQ patch types 26 and 30, which the tools
 * check as the HI16/LO16 halves of one target).
 *
 * MATRIX (libgte.h): short m[3][3] at 0x00 (five words: m00|m01, m02|m10, m11|m12, m20|m21,
 * m22|pad), long t[3] at 0x14. VECTOR: long vx, vy, vz. SVECTOR: short vx, vy, vz, pad.
 */
#include <asm.h>
#include "hilo.h"

#undef AT
#define AT	$1	/* <asm.h> spells it $at, which ASPSX 2.34 rejects */

	.set	noreorder	/* every delay slot below is written out by hand */
	.set	noat		/* at is used explicitly; ".set at" only around ASPSX's sw macro */

/*
 * GTE registers (cop2). Data registers for mtc2/mfc2/swc2, control registers for ctc2/cfc2.
 * The names follow Sony's later gtereg_s.h; Runtime Library 3.3 ships no assembler GTE header.
 */
#define C2_VXY0		$0	/* vector 0: x | y << 16 */
#define C2_VZ0		$1	/* vector 0: z */
#define C2_IR1		$9	/* intermediate results, 16-bit saturated */
#define C2_IR2		$10
#define C2_IR3		$11
#define C2_MAC1		$25	/* 32-bit accumulators */
#define C2_MAC2		$26
#define C2_MAC3		$27
#define C2_R11R12	$0	/* rotation matrix RT: five control words like a MATRIX's m */
#define C2_R13R21	$1
#define C2_R22R23	$2
#define C2_R31R32	$3
#define C2_R33		$4
#define C2_TRX		$5	/* translation vector TR */
#define C2_TRY		$6
#define C2_TRZ		$7
#define C2_L11L12	$8	/* light matrix LLM */
#define C2_L13L21	$9
#define C2_L22L23	$10
#define C2_L31L32	$11
#define C2_L33		$12
#define C2_RBK		$13	/* background colour BK */
#define C2_GBK		$14
#define C2_BBK		$15
#define C2_LR1LR2	$16	/* light colour matrix LCM */
#define C2_LR3LG1	$17
#define C2_LG2LG3	$18
#define C2_LB1LB2	$19
#define C2_LB3		$20
#define C2_RFC		$21	/* far colour FC */
#define C2_GFC		$22
#define C2_BFC		$23

/*
 * GTE commands (cop2 imm25): MVMVA = 0x0400012 | sf << 19 | mx << 17 | v << 15 | cv << 13 |
 * lm << 10. Here mx = 0 (RT), cv = 3 (no translation), lm = 0.
 */
#define GTE_MVMVA_RT_V0		0x0486012	/* sf=1, v=V0: IR = RT * V0 >> 12 */
#define GTE_MVMVA_RT_IR		0x041e012	/* sf=0, v=IR: MAC = RT * IR */
#define GTE_MVMVA_RT_IR_SF	0x049e012	/* sf=1, v=IR: MAC = RT * IR >> 12 */

#define MTX_STACK_MAX	0x280	/* 20 entries of 32 bytes (RT: five words, TR: three) */

	.data

/*
 * g_800766f4 - saved return address
 *
 * Purpose:     PushMatrix / PopMatrix have no stack frame; they park ra here while printf runs.
 * Layout:      one word.
 * Users:       PushMatrix, PopMatrix.
 */
g_800766f4:
	.word	0

/*
 * g_800766f8 - two unreferenced words
 *
 * Purpose:     none visible: no instruction of the module refers to them.
 * Layout:      two zero words, kept so that the offset and the stack land at their addresses.
 * Users:       none.
 */
g_800766f8:
	.word	0, 0

/*
 * g_80076700 - matrix stack offset
 *
 * Purpose:     byte offset of the next free entry of g_80076704 (0, 32, ... 0x280).
 * Layout:      one int.
 * Users:       PushMatrix, PopMatrix.
 */
g_80076700:
	.word	0

/*
 * g_80076704 - matrix stack
 *
 * Purpose:     the RT/TR pairs saved by PushMatrix.
 * Layout:      20 entries of 8 words: RT (five control words) then TRX, TRY, TRZ.
 * Users:       PushMatrix, PopMatrix.
 */
g_80076704:
	.space	MTX_STACK_MAX

/*
 * g_80076984, g_800769b5, g_800769de - messages
 *
 * Purpose:     printf texts of the stack overflow and underflow; the third is not referenced
 *              by the module.
 * Layout:      NUL-terminated strings of 49, 41 and 35 bytes, packed without alignment; three
 *              bytes of padding end the module's .data at a word boundary.
 * Users:       PushMatrix (g_80076984), PopMatrix (g_800769b5).
 */
g_80076984:
	.ascii	"Error: Can't push matrix,stack(max 20) is full!\n\000"
g_800769b5:
	.ascii	"Error: Can't pop matrix,stack is empty!\n\000"
g_800769de:
	.ascii	"This function is not implemented.\n\000"
	.byte	0, 0, 0		/* padding to the word boundary */

	.text

/*
 * CompMatrix - compose two coordinate matrices
 *
 * Purpose:     MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2) (libgte.h): m2 = m0 * m1,
 *              rotation and translation: m2.m = m0.m * m1.m, m2.t = m0.m * m1.t + m0.t.
 *              Leaves m0's rotation in the GTE rotation matrix (RT).
 * Inputs:      a0 = m0, a1 = m1, a2 = m2.
 * Outputs:     v0 = m2.
 * Clobbers:    t0-t8, at, GTE RT, V0, IR1..3, MAC1..3. Leaf routine.
 * GTE:         RT = m0.m; three MVMVA RT*V0 (sf=1) on the columns of m1.m, one on m1.t.
 * Differs:     from 3.3 in the three mfc2 after the last MVMVA: retail reads IR1..3 (the
 *              saturated 16-bit results) where 3.3 reads MAC1..3, so the translation
 *              m0.m * m1.t is clamped to -0x8000..0x7fff before m0.t is added.
 * Callers:     game code.
 */
	.globl	CompMatrix
CompMatrix:
	/* RT = the 3x3 matrix at a0: five words, nine 16-bit elements (R33 alone) */
	lw	t0, 0(a0)
	lw	t1, 4(a0)
	lw	t2, 8(a0)
	lw	t3, 0xc(a0)
	lw	t4, 0x10(a0)
	ctc2	t0, C2_R11R12
	ctc2	t1, C2_R13R21
	ctc2	t2, C2_R22R23
	ctc2	t3, C2_R31R32
	ctc2	t4, C2_R33
	/* V0 = column 0 of m1.m: (m1[0][0], m1[1][0], m1[2][0]); lhu/and/or pack the two
	   halfwords of different words into VXY0 */
	lhu	t0, 0(a1)
	lw	t1, 4(a1)
	lw	t2, 0xc(a1)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* t1 = m1[1][0] << 16 (high half of word 1) */
	or	t0, t0, t1		/* VXY0 = m1[0][0] | m1[1][0] << 16 */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE command must not follow mtc2 directly */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 1 (m1[0][1], m1[1][1], m1[2][1]), loaded while the GTE works */
	lhu	t0, 2(a1)
	lw	t1, 8(a1)
	lh	t2, 0xe(a1)
	sll	t1, t1, 0x10
	or	t0, t0, t1
	mfc2	t3, C2_IR1		/* read a result of the previous command */
	mfc2	t4, C2_IR2		/* read a result of the previous command */
	mfc2	t5, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 2 (m1[0][2], m1[1][2], m1[2][2]); column 1 results to t6..t8 */
	lhu	t0, 4(a1)
	lw	t1, 8(a1)
	lw	t2, 0x10(a1)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mfc2	t6, C2_IR1		/* read a result of the previous command */
	mfc2	t7, C2_IR2		/* read a result of the previous command */
	mfc2	t8, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* Store the product: each matrix word takes two 16-bit results (low | high << 16) */
	andi	t3, t3, 0xffff
	sll	t6, t6, 0x10
	or	t6, t6, t3
	sw	t6, 0(a2)
	andi	t5, t5, 0xffff
	sll	t8, t8, 0x10
	or	t8, t8, t5
	sw	t8, 0xc(a2)
	mfc2	t0, C2_IR1		/* read a result of the previous command */
	mfc2	t1, C2_IR2		/* read a result of the previous command */
	swc2	C2_IR3, 0x10(a2)
	/* V0 = m1.t (as 16-bit values: low halfword of t[0], t[1] << 16 / t[2]) */
	lhu	t5, 0x14(a1)
	lw	t6, 0x18(a1)
	lw	t2, 0x1c(a1)
	sll	t6, t6, 0x10
	or	t5, t5, t6
	mtc2	t5, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* rows 0 and 1 of the product, middle words */
	sll	t4, t4, 0x10
	andi	t0, t0, 0xffff
	or	t0, t0, t4
	sw	t0, 4(a2)
	andi	t7, t7, 0xffff
	sll	t1, t1, 0x10
	or	t1, t1, t7
	sw	t1, 8(a2)
	/* m2.t = (m0.m * m1.t) + m0.t; the products are read from IR1..3 (3.3: MAC1..3) */
	mfc2	t0, C2_IR1		/* IR1 (3.3: MAC1, $25) */
	mfc2	t1, C2_IR2		/* IR2 (3.3: MAC2, $26) */
	mfc2	t2, C2_IR3		/* IR3 (3.3: MAC3, $27) */
	lw	t3, 0x14(a0)
	lw	t4, 0x18(a0)
	lw	t5, 0x1c(a0)
	add	t0, t0, t3		/* add (trapping), as written */
	add	t1, t1, t4
	add	t2, t2, t5
	sw	t0, 0x14(a2)
	sw	t1, 0x18(a2)
	sw	t2, 0x1c(a2)
	move	v0, a2			/* return m2 */
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * MulMatrix0 - product of two rotation matrices
 *
 * Purpose:     MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2) (libgte.h):
 *              m2.m = m0.m * m1.m (translations untouched). Leaves m0.m in the GTE RT.
 * Inputs:      a0 = m0, a1 = m1, a2 = m2.
 * Outputs:     v0 = m2.
 * Clobbers:    t0-t8, at, GTE RT, V0, IR1..3. Leaf routine.
 * GTE:         RT = m0.m; three MVMVA RT*V0 (sf=1), one per column of m1.m.
 * Callers:     game code.
 */
	.globl	MulMatrix0
MulMatrix0:
	/* RT = the 3x3 matrix at a0: five words, nine 16-bit elements (R33 alone) */
	lw	t0, 0(a0)
	lw	t1, 4(a0)
	lw	t2, 8(a0)
	lw	t3, 0xc(a0)
	lw	t4, 0x10(a0)
	ctc2	t0, C2_R11R12
	ctc2	t1, C2_R13R21
	ctc2	t2, C2_R22R23
	ctc2	t3, C2_R31R32
	ctc2	t4, C2_R33
	/* V0 = column 0 of m1.m */
	lhu	t0, 0(a1)
	lw	t1, 4(a1)
	lw	t2, 0xc(a1)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 1 of m1.m */
	lhu	t0, 2(a1)
	lw	t1, 8(a1)
	lh	t2, 0xe(a1)
	sll	t1, t1, 0x10
	or	t0, t0, t1
	mfc2	t3, C2_IR1		/* read a result of the previous command */
	mfc2	t4, C2_IR2		/* read a result of the previous command */
	mfc2	t5, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 2 of m1.m */
	lhu	t0, 4(a1)
	lw	t1, 8(a1)
	lw	t2, 0x10(a1)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mfc2	t6, C2_IR1		/* read a result of the previous command */
	mfc2	t7, C2_IR2		/* read a result of the previous command */
	mfc2	t8, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* store the product into m2 */
	andi	t3, t3, 0xffff
	sll	t6, t6, 0x10
	or	t6, t6, t3
	sw	t6, 0(a2)
	andi	t5, t5, 0xffff
	sll	t8, t8, 0x10
	or	t8, t8, t5
	sw	t8, 0xc(a2)
	mfc2	t0, C2_IR1		/* read a result of the previous command */
	mfc2	t1, C2_IR2		/* read a result of the previous command */
	andi	t0, t0, 0xffff
	sll	t4, t4, 0x10
	or	t0, t0, t4
	sw	t0, 4(a2)
	andi	t7, t7, 0xffff
	sll	t1, t1, 0x10
	or	t1, t1, t7
	sw	t1, 8(a2)
	swc2	C2_IR3, 0x10(a2)
	move	v0, a2
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * MulRotMatrix0 - product of the current rotation matrix and a matrix
 *
 * Purpose:     MATRIX *MulRotMatrix0(MATRIX *m0, MATRIX *m1) (libgte.h): m1.m = RT * m0.m, with
 *              RT the rotation matrix already in the GTE.
 * Inputs:      a0 = m0, a1 = m1.
 * Outputs:     v0 = m1.
 * Clobbers:    t0-t8, at, GTE V0, IR1..3. Leaf routine.
 * GTE:         three MVMVA RT*V0 (sf=1), one per column of m0.m.
 * Callers:     game code.
 */
	.globl	MulRotMatrix0
MulRotMatrix0:
	/* V0 = column 0 of m0.m */
	lhu	t0, 0(a0)
	lw	t1, 4(a0)
	lw	t2, 0xc(a0)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 1 of m0.m */
	lhu	t0, 2(a0)
	lw	t1, 8(a0)
	lh	t2, 0xe(a0)
	sll	t1, t1, 0x10
	or	t0, t0, t1
	mfc2	t3, C2_IR1		/* read a result of the previous command */
	mfc2	t4, C2_IR2		/* read a result of the previous command */
	mfc2	t5, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 2 of m0.m */
	lhu	t0, 4(a0)
	lw	t1, 8(a0)
	lw	t2, 0x10(a0)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mfc2	t6, C2_IR1		/* read a result of the previous command */
	mfc2	t7, C2_IR2		/* read a result of the previous command */
	mfc2	t8, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* store the product into m1 */
	andi	t3, t3, 0xffff
	sll	t6, t6, 0x10
	or	t6, t6, t3
	sw	t6, 0(a1)
	andi	t5, t5, 0xffff
	sll	t8, t8, 0x10
	or	t8, t8, t5
	sw	t8, 0xc(a1)
	mfc2	t0, C2_IR1		/* read a result of the previous command */
	mfc2	t1, C2_IR2		/* read a result of the previous command */
	andi	t0, t0, 0xffff
	sll	t4, t4, 0x10
	or	t0, t0, t4
	sw	t0, 4(a1)
	andi	t7, t7, 0xffff
	sll	t1, t1, 0x10
	or	t1, t1, t7
	sw	t1, 8(a1)
	swc2	C2_IR3, 0x10(a1)
	move	v0, a1
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * MulRotMatrix - multiply a matrix by the current rotation matrix in place
 *
 * Purpose:     MATRIX *MulRotMatrix(MATRIX *m0) (libgte.h): m0.m = RT * m0.m.
 * Inputs:      a0 = m0.
 * Outputs:     v0 = m0.
 * Clobbers:    t0-t8, at, GTE V0, IR1..3. Leaf routine.
 * GTE:         three MVMVA RT*V0 (sf=1), one per column of m0.m.
 * Callers:     game code.
 */
	.globl	MulRotMatrix
MulRotMatrix:
	/* V0 = column 0 of m0.m */
	lw	t0, 0(a0)
	lw	t1, 4(a0)
	lw	t2, 0xc(a0)
	andi	t0, t0, 0xffff
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 1 of m0.m */
	lhu	t0, 2(a0)
	lw	t1, 8(a0)
	lh	t2, 0xe(a0)
	sll	t1, t1, 0x10
	or	t0, t0, t1
	mfc2	t3, C2_IR1		/* read a result of the previous command */
	mfc2	t4, C2_IR2		/* read a result of the previous command */
	mfc2	t5, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 2 of m0.m */
	lhu	t0, 4(a0)
	lw	t1, 8(a0)
	lw	t2, 0x10(a0)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mfc2	t6, C2_IR1		/* read a result of the previous command */
	mfc2	t7, C2_IR2		/* read a result of the previous command */
	mfc2	t8, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* store the product back into m0 */
	andi	t3, t3, 0xffff
	sll	t6, t6, 0x10
	or	t6, t6, t3
	sw	t6, 0(a0)
	andi	t5, t5, 0xffff
	sll	t8, t8, 0x10
	or	t8, t8, t5
	sw	t8, 0xc(a0)
	mfc2	t0, C2_IR1		/* read a result of the previous command */
	mfc2	t1, C2_IR2		/* read a result of the previous command */
	andi	t0, t0, 0xffff
	sll	t4, t4, 0x10
	or	t0, t0, t4
	sw	t0, 4(a0)
	andi	t7, t7, 0xffff
	sll	t1, t1, 0x10
	or	t1, t1, t7
	sw	t1, 8(a0)
	swc2	C2_IR3, 0x10(a0)
	move	v0, a0
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * SetMulMatrix - load the product of two matrices as the GTE rotation matrix
 *
 * Purpose:     MATRIX *SetMulMatrix(MATRIX *m0, MATRIX *m1) (libgte.h): RT = m0.m * m1.m.
 * Inputs:      a0 = m0, a1 = m1.
 * Outputs:     v0 = m0.
 * Clobbers:    t0-t8, at, GTE RT, V0, IR1..3. Leaf routine.
 * GTE:         RT = m0.m; three MVMVA RT*V0 (sf=1) on the columns of m1.m; RT = the result.
 * Callers:     game code.
 */
	.globl	SetMulMatrix
SetMulMatrix:
	/* RT = the 3x3 matrix at a0: five words, nine 16-bit elements (R33 alone) */
	lw	t0, 0(a0)
	lw	t1, 4(a0)
	lw	t2, 8(a0)
	lw	t3, 0xc(a0)
	lw	t4, 0x10(a0)
	ctc2	t0, C2_R11R12
	ctc2	t1, C2_R13R21
	ctc2	t2, C2_R22R23
	ctc2	t3, C2_R31R32
	ctc2	t4, C2_R33
	/* V0 = column 0 of m1.m */
	lhu	t0, 0(a1)
	lw	t1, 4(a1)
	lw	t2, 0xc(a1)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 1 of m1.m */
	lhu	t0, 2(a1)
	lw	t1, 8(a1)
	lh	t2, 0xe(a1)
	sll	t1, t1, 0x10
	or	t0, t0, t1
	mfc2	t3, C2_IR1		/* read a result of the previous command */
	mfc2	t4, C2_IR2		/* read a result of the previous command */
	mfc2	t5, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* V0 = column 2 of m1.m */
	lhu	t0, 4(a1)
	lw	t1, 8(a1)
	lw	t2, 0x10(a1)
	lui	AT, 0xffff		/* at = 0xffff0000 */
	and	t1, t1, AT		/* keep the high halfword */
	or	t0, t0, t1
	mfc2	t6, C2_IR1		/* read a result of the previous command */
	mfc2	t7, C2_IR2		/* read a result of the previous command */
	mfc2	t8, C2_IR3		/* read a result of the previous command */
	mtc2	t0, C2_VXY0
	mtc2	t2, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	/* pack the product and load it as RT */
	andi	t3, t3, 0xffff
	sll	t6, t6, 0x10
	or	t6, t6, t3
	andi	t5, t5, 0xffff
	sll	t8, t8, 0x10
	or	t8, t8, t5
	mfc2	t0, C2_IR1		/* read a result of the previous command */
	mfc2	t1, C2_IR2		/* read a result of the previous command */
	mfc2	t2, C2_IR3		/* read a result of the previous command */
	andi	t0, t0, 0xffff
	sll	t4, t4, 0x10
	or	t0, t0, t4
	andi	t7, t7, 0xffff
	sll	t1, t1, 0x10
	or	t1, t1, t7
	ctc2	t6, C2_R11R12
	ctc2	t0, C2_R13R21
	ctc2	t1, C2_R22R23
	ctc2	t8, C2_R31R32
	ctc2	t2, C2_R33
	move	v0, a0
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * ApplyMatrixLV - multiply a long vector by a matrix
 *
 * Purpose:     VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1) (libgte.h):
 *              v1 = m.m * v0 for 32-bit components. Each component x is split into a high part
 *              h = x >> 15 and a low part l = x & 0x7fff (both taken on |x| and negated back,
 *              so h and l carry x's sign); v1 = (RT * h) << 3 + (RT * l) >> 12, which is
 *              RT * x >> 12 without overflowing the 16-bit IR inputs.
 * Inputs:      a0 = m, a1 = v0, a2 = v1.
 * Outputs:     v0 = v1.
 * Clobbers:    t0-t5, GTE RT, IR1..3, MAC1..3. Leaf routine.
 * GTE:         RT = m.m; MVMVA RT*IR (sf=0) on the high parts, MVMVA RT*IR (sf=1) on the
 *              low parts.
 * Callers:     game code.
 */
	.globl	ApplyMatrixLV
ApplyMatrixLV:
	/* RT = m.m (five words) */
	lw	t0, 0(a0)
	lw	t1, 4(a0)
	lw	t2, 8(a0)
	lw	t3, 0xc(a0)
	lw	t4, 0x10(a0)
	ctc2	t0, C2_R11R12
	ctc2	t1, C2_R13R21
	ctc2	t2, C2_R22R23
	ctc2	t3, C2_R31R32
	ctc2	t4, C2_R33
	/* split v0[0..2] into high parts t3..t5 (x >> 15) and low parts t0..t2 (x & 0x7fff),
	   both with the sign of x; the instruction after each "b" is never executed */
	lw	t0, 0(a1)
	lw	t1, 4(a1)
	lw	t2, 8(a1)
	bgez	t0, $x_positive
	sra	t3, t0, 0xf		/* delay slot (real work) */
	negu	t0, t0
	sra	t3, t0, 0xf
	andi	t0, t0, 0x7fff
	negu	t3, t3
	beq	zero, zero, $x_split
	negu	t0, t0			/* delay slot (real work) */
	sra	t3, t0, 0xf
$x_positive:
	andi	t0, t0, 0x7fff
$x_split:
	bgez	t1, $y_positive
	sra	t4, t1, 0xf		/* delay slot (real work) */
	negu	t1, t1
	sra	t4, t1, 0xf
	andi	t1, t1, 0x7fff
	negu	t4, t4
	beq	zero, zero, $y_split
	negu	t1, t1			/* delay slot (real work) */
	sra	t4, t1, 0xf
$y_positive:
	andi	t1, t1, 0x7fff
$y_split:
	bgez	t2, $z_positive
	sra	t5, t2, 0xf		/* delay slot (real work) */
	negu	t2, t2
	sra	t5, t2, 0xf
	andi	t2, t2, 0x7fff
	negu	t5, t5
	beq	zero, zero, $z_split
	negu	t2, t2			/* delay slot (real work) */
	sra	t5, t2, 0xf
$z_positive:
	andi	t2, t2, 0x7fff
$z_split:
	/* MAC = RT * high parts (no shift), then RT * low parts >> 12 */
	mtc2	t3, C2_IR1
	mtc2	t4, C2_IR2
	mtc2	t5, C2_IR3
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_IR		/* MAC1..3 = RT * IR (no shift) */
	mfc2	t3, C2_MAC1		/* read a result of the previous command */
	mfc2	t4, C2_MAC2		/* read a result of the previous command */
	mfc2	t5, C2_MAC3		/* read a result of the previous command */
	mtc2	t0, C2_IR1
	mtc2	t1, C2_IR2
	mtc2	t2, C2_IR3
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_IR_SF	/* MAC1..3 = RT * IR >> 12 */
	/* high products << 3 (sign-magnitude, like the split), added to the low products */
	bgez	t3, $hx_positive
	nop				/* branch delay: nothing to do */
	negu	t3, t3
	sll	t3, t3, 3
	beq	zero, zero, $hx_done
	negu	t3, t3			/* delay slot (real work) */
$hx_positive:
	sll	t3, t3, 3
$hx_done:
	bgez	t4, $hy_positive
	nop				/* branch delay: nothing to do */
	negu	t4, t4
	sll	t4, t4, 3
	beq	zero, zero, $hy_done
	negu	t4, t4			/* delay slot (real work) */
$hy_positive:
	sll	t4, t4, 3
$hy_done:
	bgez	t5, $hz_positive
	nop				/* branch delay: nothing to do */
	negu	t5, t5
	sll	t5, t5, 3
	beq	zero, zero, $hz_done
	negu	t5, t5			/* delay slot (real work) */
$hz_positive:
	sll	t5, t5, 3
$hz_done:
	mfc2	t0, C2_MAC1		/* read a result of the previous command */
	mfc2	t1, C2_MAC2		/* read a result of the previous command */
	mfc2	t2, C2_MAC3		/* read a result of the previous command */
	addu	t0, t0, t3
	addu	t1, t1, t4
	addu	t2, t2, t5
	sw	t0, 0(a2)
	sw	t1, 4(a2)
	sw	t2, 8(a2)
	jr	ra
	move	v0, a2			/* delay slot (real work): return v1 */

/*
 * ApplyRotMatrix - multiply a short vector by the current rotation matrix
 *
 * Purpose:     VECTOR *ApplyRotMatrix(SVECTOR *v0, VECTOR *v1) (libgte.h): v1 = RT * v0 >> 12.
 * Inputs:      a0 = v0, a1 = v1.
 * Outputs:     v0 = a2 as the caller left it (the routine copies a2, not a1: Sony's code,
 *              unchanged from 3.3).
 * Clobbers:    t0, t1, GTE V0, IR1..3. Leaf routine.
 * GTE:         one MVMVA RT*V0 (sf=1); IR1..3 stored with swc2.
 * Callers:     game code.
 */
	.globl	ApplyRotMatrix
ApplyRotMatrix:
	lw	t0, 0(a0)
	lw	t1, 4(a0)
	mtc2	t0, C2_VXY0
	mtc2	t1, C2_VZ0
	nop				/* GTE: no command right after a register move */
	cop2	GTE_MVMVA_RT_V0		/* IR1..3 = RT * V0 >> 12 */
	swc2	C2_IR1, 0(a1)
	swc2	C2_IR2, 4(a1)
	swc2	C2_IR3, 8(a1)
	move	v0, a2			/* returns the caller's a2 (as in 3.3) */
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * ScaleMatrixL - scale the columns of a matrix
 *
 * Purpose:     MATRIX *ScaleMatrixL(MATRIX *m, VECTOR *v) (libgte.h): m.m[i][j] =
 *              m.m[i][j] * v[j] >> 12 (column j scaled by the 4.12 factor v[j]).
 * Inputs:      a0 = m, a1 = v.
 * Outputs:     v0 = m.
 * Clobbers:    t0-t5, hi/lo. Leaf routine, CPU only (no GTE).
 * Registers:   t3, t4, t5 = v[0], v[1], v[2]; each matrix word holds two 16-bit elements,
 *              read in t0, split into t1 (low, sign-extended) and t2 (high).
 * Callers:     game code.
 */
	.globl	ScaleMatrixL
ScaleMatrixL:
	/* row 0: m[0][0] * v[0], m[0][1] * v[1] */
	lw	t0, 0(a0)
	lw	t3, 0(a1)
	andi	t1, t0, 0xffff
	sll	t1, t1, 0x10
	sra	t1, t1, 0x10
	multu	t1, t3
	sra	t2, t0, 0x10
	lw	t4, 4(a1)
	lw	t5, 8(a1)
	lw	t0, 4(a0)
	move	v0, a0			/* return m (scheduled into the multiply latency) */
	mflo	t1
	sra	t1, t1, 0xc
	andi	t1, t1, 0xffff
	multu	t2, t3
	mflo	t2
	sra	t2, t2, 0xc
	sll	t2, t2, 0x10
	or	t1, t1, t2
	sw	t1, 0(a0)
	/* row 0 m[0][2] * v[2], row 1 m[1][0] * v[0] */
	andi	t1, t0, 0xffff
	sll	t1, t1, 0x10
	sra	t1, t1, 0x10
	multu	t1, t3
	sra	t2, t0, 0x10
	lw	t0, 8(a0)
	mflo	t1
	sra	t1, t1, 0xc
	andi	t1, t1, 0xffff
	multu	t2, t4
	mflo	t2
	sra	t2, t2, 0xc
	sll	t2, t2, 0x10
	or	t1, t1, t2
	sw	t1, 4(a0)
	/* row 1: m[1][1] * v[1], m[1][2] * v[2] */
	andi	t1, t0, 0xffff
	sll	t1, t1, 0x10
	sra	t1, t1, 0x10
	multu	t1, t4
	sra	t2, t0, 0x10
	lw	t0, 0xc(a0)
	mflo	t1
	sra	t1, t1, 0xc
	andi	t1, t1, 0xffff
	multu	t2, t4
	mflo	t2
	sra	t2, t2, 0xc
	sll	t2, t2, 0x10
	or	t1, t1, t2
	sw	t1, 8(a0)
	/* row 2: m[2][0] * v[0], m[2][1] * v[1] */
	andi	t1, t0, 0xffff
	sll	t1, t1, 0x10
	sra	t1, t1, 0x10
	multu	t1, t5
	sra	t2, t0, 0x10
	lw	t0, 0x10(a0)
	mflo	t1
	sra	t1, t1, 0xc
	andi	t1, t1, 0xffff
	multu	t2, t5
	mflo	t2
	sra	t2, t2, 0xc
	sll	t2, t2, 0x10
	or	t1, t1, t2
	sw	t1, 0xc(a0)
	/* row 2: m[2][2] * v[2] (the word's high half is left as zero padding) */
	andi	t1, t0, 0xffff
	sll	t1, t1, 0x10
	sra	t1, t1, 0x10
	multu	t1, t5
	mflo	t1
	sra	t1, t1, 0xc
	jr	ra
	sw	t1, 0x10(a0)		/* delay slot (real work) */

/*
 * PushMatrix - save the GTE rotation matrix and translation on the matrix stack
 *
 * Purpose:     void PushMatrix(void) (libgte.h): copies RT and TR (8 words) to
 *              g_80076704[g_80076700 / 32] and advances g_80076700 by 32. With 20 entries
 *              in use (g_80076700 >= 0x280) it prints g_80076984 and returns.
 * Inputs:      none.
 * Outputs:     none.
 * Clobbers:    t0-t2, t6, t7, at; a0 and the printf registers on overflow.
 * Memory:      g_80076700 (stack offset), g_80076704 (stack), g_800766f4 (ra, while printf
 *              runs: the routine has no frame).
 * Callers:     game code.
 * Callees:     printf (overflow only).
 */
	.globl	PushMatrix
PushMatrix:
	/* overflow check: 20 entries of 32 bytes (0x280) */
	lw	t6, g_80076700		/* t6 = g_80076700 (lui t6 + lw) */
	nop				/* load delay */
	slti	AT, t6, MTX_STACK_MAX		/* at = (offset < 0x280) */
	bnez	AT, $push_entry		/* room: push */
	.set	at
	sw	ra, g_800766f4		/* delay slot: the lui at of "sw ra, g_800766f4"; the sw runs only if not taken */
	.set	noat
	/* full: printf(g_80076984) and return; ra is parked in g_800766f4 (no frame) */
	lui	a0, HI16(g_80076984)	/* a0 = g_80076984: high half; the low half is in the jal delay slot */
	jal	printf			/* printf(g_80076984) */
	addiu	a0, a0, LO16(g_80076984)	/* delay slot (real work): a0 = g_80076984 (low half) */
	lw	ra, g_800766f4		/* ra = g_800766f4 (lui ra + lw, the macro) */
	nop				/* load delay */
	jr	ra			/* return to PushMatrix's caller */
	nop				/* branch delay: nothing to do */
$push_entry:
	/* t7 = &g_80076704[g_80076700 / 32]: lui and addiu are split around the addu */
	lui	t7, HI16(g_80076704)	/* high half of g_80076704 */
	addu	t7, t7, t6		/* t7 += offset */
	addiu	t7, t7, LO16(g_80076704)	/* low half of g_80076704: t7 = entry address */
	/* copy RT (five words) and TR (three words) to the entry */
	cfc2	t0, C2_R11R12
	cfc2	t1, C2_R13R21
	sw	t0, 0(t7)
	sw	t1, 4(t7)
	cfc2	t0, C2_R22R23
	cfc2	t1, C2_R31R32
	sw	t0, 8(t7)
	sw	t1, 0xc(t7)
	cfc2	t0, C2_R33
	nop				/* load delay */
	sw	t0, 0x10(t7)
	cfc2	t0, C2_TRX
	cfc2	t1, C2_TRY
	cfc2	t2, C2_TRZ
	sw	t0, 0x14(t7)
	sw	t1, 0x18(t7)
	sw	t2, 0x1c(t7)
	/* g_80076700 += 32 */
	addi	t6, t6, 0x20		/* addi (trapping), as written */
	.set	at
	sw	t6, g_80076700		/* g_80076700 = t6 (lui at + sw) */
	.set	noat
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * PopMatrix - restore the GTE rotation matrix and translation from the matrix stack
 *
 * Purpose:     void PopMatrix(void) (libgte.h): steps g_80076700 back by 32 and loads RT and TR
 *              from g_80076704[g_80076700 / 32]. With the stack empty it prints g_800769b5
 *              and returns.
 * Inputs:      none.
 * Outputs:     none (GTE RT, TR).
 * Clobbers:    t0-t2, t6, t7, at; a0 and the printf registers on underflow.
 * Memory:      g_80076700, g_80076704, g_800766f4 (ra while printf runs).
 * Callers:     game code.
 * Callees:     printf (underflow only).
 */
	.globl	PopMatrix
PopMatrix:
	/* underflow check: nothing pushed when g_80076700 <= 0 */
	lw	t6, g_80076700		/* t6 = g_80076700 (lui t6 + lw) */
	nop				/* load delay */
	bgtz	t6, $pop_entry		/* pushed entries: pop */
	.set	at
	sw	ra, g_800766f4		/* delay slot: the lui at of "sw ra, g_800766f4"; the sw runs only if not taken */
	.set	noat
	/* empty: printf(g_800769b5) and return; ra is parked in g_800766f4 */
	lui	a0, HI16(g_800769b5)	/* a0 = g_800769b5: high half; the low half is in the jal delay slot */
	jal	printf			/* printf(g_800769b5) */
	addiu	a0, a0, LO16(g_800769b5)	/* delay slot (real work): a0 = g_800769b5 (low half) */
	lw	ra, g_800766f4		/* ra = g_800766f4 */
	nop				/* load delay */
	jr	ra
	nop				/* branch delay: nothing to do */
$pop_entry:
	/* g_80076700 -= 32; t7 = &g_80076704[g_80076700 / 32] (split lui/addiu pair) */
	addi	t6, t6, -0x20		/* addi (trapping), as written */
	.set	at
	sw	t6, g_80076700		/* g_80076700 = t6 (lui at + sw) */
	.set	noat
	lui	t7, HI16(g_80076704)	/* high half of g_80076704 */
	addu	t7, t7, t6
	addiu	t7, t7, LO16(g_80076704)	/* low half: t7 = entry address */
	/* load RT (five words) and TR (three words) from the entry */
	lw	t0, 0(t7)
	lw	t1, 4(t7)
	ctc2	t0, C2_R11R12
	ctc2	t1, C2_R13R21
	lw	t0, 8(t7)
	lw	t1, 0xc(t7)
	ctc2	t0, C2_R22R23
	ctc2	t1, C2_R31R32
	lw	t0, 0x10(t7)
	nop				/* load delay */
	ctc2	t0, C2_R33
	nop				/* GTE: no command right after a register move */
	lw	t0, 0x14(t7)
	lw	t1, 0x18(t7)
	lw	t2, 0x1c(t7)
	ctc2	t0, C2_TRX
	ctc2	t1, C2_TRY
	ctc2	t2, C2_TRZ
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * ReadRotMatrix - read the GTE rotation matrix and translation
 *
 * Purpose:     MATRIX *ReadRotMatrix(MATRIX *m) (libgte.h): m.m = RT, m.t = TR.
 * Inputs:      a0 = m.
 * Outputs:     v0 as the caller left it (the routine sets none).
 * Clobbers:    t0-t4. Leaf routine.
 * Callers:     game code.
 */
	.globl	ReadRotMatrix
ReadRotMatrix:
	/* m.m = RT (five words), m.t = TR */
	cfc2	t0, C2_R11R12
	cfc2	t1, C2_R13R21
	cfc2	t2, C2_R22R23
	cfc2	t3, C2_R31R32
	cfc2	t4, C2_R33
	sw	t0, 0(a0)
	sw	t1, 4(a0)
	sw	t2, 8(a0)
	sw	t3, 0xc(a0)
	sw	t4, 0x10(a0)
	cfc2	t0, C2_TRX
	cfc2	t1, C2_TRY
	cfc2	t2, C2_TRZ
	sw	t0, 0x14(a0)
	sw	t1, 0x18(a0)
	sw	t2, 0x1c(a0)
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * ReadLightMatrix - read the GTE light matrix and background colour
 *
 * Purpose:     MATRIX *ReadLightMatrix(MATRIX *m) (libgte.h): m.m = LLM, m.t = BK (RBK, GBK,
 *              BBK).
 * Inputs:      a0 = m.
 * Outputs:     v0 as the caller left it (the routine sets none).
 * Clobbers:    t0-t4. Leaf routine.
 * Callers:     game code.
 */
	.globl	ReadLightMatrix
ReadLightMatrix:
	/* m.m = LLM (five words), m.t = BK */
	cfc2	t0, C2_L11L12
	cfc2	t1, C2_L13L21
	cfc2	t2, C2_L22L23
	cfc2	t3, C2_L31L32
	cfc2	t4, C2_L33
	sw	t0, 0(a0)
	sw	t1, 4(a0)
	sw	t2, 8(a0)
	sw	t3, 0xc(a0)
	sw	t4, 0x10(a0)
	cfc2	t0, C2_RBK
	cfc2	t1, C2_GBK
	cfc2	t2, C2_BBK
	sw	t0, 0x14(a0)
	sw	t1, 0x18(a0)
	sw	t2, 0x1c(a0)
	jr	ra
	nop				/* branch delay: nothing to do */

/*
 * ReadColorMatrix - read the GTE light colour matrix and far colour
 *
 * Purpose:     MATRIX *ReadColorMatrix(MATRIX *m) (libgte.h): m.m = LCM, m.t = FC (RFC, GFC,
 *              BFC).
 * Inputs:      a0 = m.
 * Outputs:     v0 as the caller left it (the routine sets none).
 * Clobbers:    t0-t4. Leaf routine.
 * Callers:     game code.
 * Note:        the three nops after the return delay slot are module padding (the 3.3 object
 *              has them too); they end the module's .text at 0x80066e90.
 */
	.globl	ReadColorMatrix
ReadColorMatrix:
	/* m.m = LCM (five words), m.t = FC */
	cfc2	t0, C2_LR1LR2
	cfc2	t1, C2_LR3LG1
	cfc2	t2, C2_LG2LG3
	cfc2	t3, C2_LB1LB2
	cfc2	t4, C2_LB3
	sw	t0, 0(a0)
	sw	t1, 4(a0)
	sw	t2, 8(a0)
	sw	t3, 0xc(a0)
	sw	t4, 0x10(a0)
	cfc2	t0, C2_RFC
	cfc2	t1, C2_GFC
	cfc2	t2, C2_BFC
	sw	t0, 0x14(a0)
	sw	t1, 0x18(a0)
	sw	t2, 0x1c(a0)
	jr	ra
	nop				/* branch delay: nothing to do */
	nop				/* module padding */
	nop				/* module padding */
	nop				/* module padding */
