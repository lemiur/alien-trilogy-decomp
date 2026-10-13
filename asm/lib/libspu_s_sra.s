/*

 * libspu_s_sra.s - LIBSPU module S_SRA: _spu_setReverbAttr.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_sra.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_SRA.
 * Exports:     _spu_setReverbAttr (.text+0x0, 1232 bytes).
 * Imports:     _spu_RXX (the SPU module's SPU register base pointer, 0x80073c90).
 * Own data:    none.
 *
 * Nearest archived version: Runtime Library 4.0 LIBSPU.LIB S_SRA (also 4.1, 4.3 .. 4.6: the
 * module is identical in all of them). Its 1232 text bytes equal these with relocations masked,
 * except one word (analysis/data_survey/moddiff.py: 1 differing word of 308): the delay slot of
 * the final "jr ra" is "move v0, zero" here and "nop" in the archive, so the retail routine
 * returns 0 where the 4.x one returns nothing. That is the whole difference: "Runtime 4.0
 * S_SRA with a zero return value". The module does not exist in the 3.3 archive, which keeps
 * the same code inline in SPU_IO's ioctl; the retail SPU module is an older rebuild whose
 * _spu_ioctl calls this routine.
 *
 * The routine is compiled C (GCC register use): 32 copies of one "if (!mask || (mask & bit))
 * store" step, one per reverb register. Delay slots that hold the next block's mask test are
 * written out as they appear.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */

	.text

/*
 * _spu_setReverbAttr - load a record of reverb register values into the SPU
 *
 * Purpose:     void _spu_setReverbAttr(_SPU_REV_ATTR *attr)  (internal; called by the SPU ioctl
 *              dispatcher, code 0x63, after SpuSetReverbModeParam has filled the record).
 *              The record starts with a 32-bit selection mask followed by one 16-bit value per SPU
 *              reverb register, in register order. A mask of 0 selects every register; otherwise
 *              bit n of the mask selects register n. Each selected value is stored to
 *              _spu_RXX + 0x1c0 + 2 * n, the 32 halfword reverb registers (dAPF1 .. vRIN,
 *              0x1f801dc0..0x1f801dfe).
 * Inputs:      a0 = attr: +0 u32 mask, +4 + 2 * n u16 value of register n (n = 0..31, so the
 *              record is 0x44 bytes).
 * Outputs:     v0 = 0 (always).
 * Clobbers:    v0, v1, a1, a2. Leaf routine: no stack frame, no calls.
 * Hardware:    SPU reverb registers 0x1f801dc0..0x1f801dfe (written through _spu_RXX).
 * Reads:       _spu_RXX (the SPU module's register base pointer, 0x80073c90).
 * Callers:     _spu_ioctl (SPU module), case 0x63.
 * Callees:     none.
 * Register use: a1 = mask, a2 = (mask == 0) "write everything" flag, a0 = attr, v1 = register
 *              base, v0 = value / bit test.
 */
	.globl	_spu_setReverbAttr
_spu_setReverbAttr:

	/*
	 * Prologue: a1 = mask = attr->mask; a2 = (mask == 0), true when every register is to be written.
	 */
	lw	a1, 0(a0)	/* a1 = *(u32 *)(a0) */
	nop	/* load delay slot */
	sltiu	a2, a1, 1	/* a2 = ((u32)a1 < 1) */

	/*
	 * Register 0, dAPF1 (0x1f801dc0): written when the mask is 0 or bit 0 (0x1) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
	bne	a2, zero, $L5b594	/* if (a2 != 0) goto $L5b594 */
	andi	v0, a1, 1	/* delay slot (real work): v0 = a1 & 1 */
	beq	v0, zero, $L5b5a8	/* if (v0 == 0) goto $L5b5a8 */
	nop	/* delay slot: nop */
$L5b594:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 4(a0)	/* v0 = *(u16 *)(a0 + 4) */
	nop	/* load delay slot */
	sh	v0, 0x1c0(v1)	/* *(u16 *)(v1 + 0x1c0) = v0 [SPU reverb dAPF1] */

	/*
	 * Register 1, dAPF2 (0x1f801dc2): written when the mask is 0 or bit 1 (0x2) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b5a8:
	bne	a2, zero, $L5b5b8	/* if (a2 != 0) goto $L5b5b8 */
	andi	v0, a1, 2	/* delay slot (real work): v0 = a1 & 2 */
	beq	v0, zero, $L5b5cc	/* if (v0 == 0) goto $L5b5cc */
	nop	/* delay slot: nop */
$L5b5b8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 6(a0)	/* v0 = *(u16 *)(a0 + 6) */
	nop	/* load delay slot */
	sh	v0, 0x1c2(v1)	/* *(u16 *)(v1 + 0x1c2) = v0 [SPU reverb dAPF2] */

	/*
	 * Register 2, vIIR (0x1f801dc4): written when the mask is 0 or bit 2 (0x4) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b5cc:
	bne	a2, zero, $L5b5dc	/* if (a2 != 0) goto $L5b5dc */
	andi	v0, a1, 4	/* delay slot (real work): v0 = a1 & 4 */
	beq	v0, zero, $L5b5f0	/* if (v0 == 0) goto $L5b5f0 */
	nop	/* delay slot: nop */
$L5b5dc:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 8(a0)	/* v0 = *(u16 *)(a0 + 8) */
	nop	/* load delay slot */
	sh	v0, 0x1c4(v1)	/* *(u16 *)(v1 + 0x1c4) = v0 [SPU reverb vIIR] */

	/*
	 * Register 3, vCOMB1 (0x1f801dc6): written when the mask is 0 or bit 3 (0x8) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b5f0:
	bne	a2, zero, $L5b600	/* if (a2 != 0) goto $L5b600 */
	andi	v0, a1, 8	/* delay slot (real work): v0 = a1 & 8 */
	beq	v0, zero, $L5b614	/* if (v0 == 0) goto $L5b614 */
	nop	/* delay slot: nop */
$L5b600:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0xa(a0)	/* v0 = *(u16 *)(a0 + 0xa) */
	nop	/* load delay slot */
	sh	v0, 0x1c6(v1)	/* *(u16 *)(v1 + 0x1c6) = v0 [SPU reverb vCOMB1] */

	/*
	 * Register 4, vCOMB2 (0x1f801dc8): written when the mask is 0 or bit 4 (0x10) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b614:
	bne	a2, zero, $L5b624	/* if (a2 != 0) goto $L5b624 */
	andi	v0, a1, 0x10	/* delay slot (real work): v0 = a1 & 0x10 */
	beq	v0, zero, $L5b638	/* if (v0 == 0) goto $L5b638 */
	nop	/* delay slot: nop */
$L5b624:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0xc(a0)	/* v0 = *(u16 *)(a0 + 0xc) */
	nop	/* load delay slot */
	sh	v0, 0x1c8(v1)	/* *(u16 *)(v1 + 0x1c8) = v0 [SPU reverb vCOMB2] */

	/*
	 * Register 5, vCOMB3 (0x1f801dca): written when the mask is 0 or bit 5 (0x20) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b638:
	bne	a2, zero, $L5b648	/* if (a2 != 0) goto $L5b648 */
	andi	v0, a1, 0x20	/* delay slot (real work): v0 = a1 & 0x20 */
	beq	v0, zero, $L5b65c	/* if (v0 == 0) goto $L5b65c */
	nop	/* delay slot: nop */
$L5b648:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0xe(a0)	/* v0 = *(u16 *)(a0 + 0xe) */
	nop	/* load delay slot */
	sh	v0, 0x1ca(v1)	/* *(u16 *)(v1 + 0x1ca) = v0 [SPU reverb vCOMB3] */

	/*
	 * Register 6, vCOMB4 (0x1f801dcc): written when the mask is 0 or bit 6 (0x40) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b65c:
	bne	a2, zero, $L5b66c	/* if (a2 != 0) goto $L5b66c */
	andi	v0, a1, 0x40	/* delay slot (real work): v0 = a1 & 0x40 */
	beq	v0, zero, $L5b680	/* if (v0 == 0) goto $L5b680 */
	nop	/* delay slot: nop */
$L5b66c:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x10(a0)	/* v0 = *(u16 *)(a0 + 0x10) */
	nop	/* load delay slot */
	sh	v0, 0x1cc(v1)	/* *(u16 *)(v1 + 0x1cc) = v0 [SPU reverb vCOMB4] */

	/*
	 * Register 7, vWALL (0x1f801dce): written when the mask is 0 or bit 7 (0x80) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b680:
	bne	a2, zero, $L5b690	/* if (a2 != 0) goto $L5b690 */
	andi	v0, a1, 0x80	/* delay slot (real work): v0 = a1 & 0x80 */
	beq	v0, zero, $L5b6a4	/* if (v0 == 0) goto $L5b6a4 */
	nop	/* delay slot: nop */
$L5b690:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x12(a0)	/* v0 = *(u16 *)(a0 + 0x12) */
	nop	/* load delay slot */
	sh	v0, 0x1ce(v1)	/* *(u16 *)(v1 + 0x1ce) = v0 [SPU reverb vWALL] */

	/*
	 * Register 8, vAPF1 (0x1f801dd0): written when the mask is 0 or bit 8 (0x100) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b6a4:
	bne	a2, zero, $L5b6b4	/* if (a2 != 0) goto $L5b6b4 */
	andi	v0, a1, 0x100	/* delay slot (real work): v0 = a1 & 0x100 */
	beq	v0, zero, $L5b6c8	/* if (v0 == 0) goto $L5b6c8 */
	nop	/* delay slot: nop */
$L5b6b4:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x14(a0)	/* v0 = *(u16 *)(a0 + 0x14) */
	nop	/* load delay slot */
	sh	v0, 0x1d0(v1)	/* *(u16 *)(v1 + 0x1d0) = v0 [SPU reverb vAPF1] */

	/*
	 * Register 9, vAPF2 (0x1f801dd2): written when the mask is 0 or bit 9 (0x200) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b6c8:
	bne	a2, zero, $L5b6d8	/* if (a2 != 0) goto $L5b6d8 */
	andi	v0, a1, 0x200	/* delay slot (real work): v0 = a1 & 0x200 */
	beq	v0, zero, $L5b6ec	/* if (v0 == 0) goto $L5b6ec */
	nop	/* delay slot: nop */
$L5b6d8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x16(a0)	/* v0 = *(u16 *)(a0 + 0x16) */
	nop	/* load delay slot */
	sh	v0, 0x1d2(v1)	/* *(u16 *)(v1 + 0x1d2) = v0 [SPU reverb vAPF2] */

	/*
	 * Register 10, mLSAME (0x1f801dd4): written when the mask is 0 or bit 10 (0x400) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b6ec:
	bne	a2, zero, $L5b6fc	/* if (a2 != 0) goto $L5b6fc */
	andi	v0, a1, 0x400	/* delay slot (real work): v0 = a1 & 0x400 */
	beq	v0, zero, $L5b710	/* if (v0 == 0) goto $L5b710 */
	nop	/* delay slot: nop */
$L5b6fc:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x18(a0)	/* v0 = *(u16 *)(a0 + 0x18) */
	nop	/* load delay slot */
	sh	v0, 0x1d4(v1)	/* *(u16 *)(v1 + 0x1d4) = v0 [SPU reverb mLSAME] */

	/*
	 * Register 11, mRSAME (0x1f801dd6): written when the mask is 0 or bit 11 (0x800) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b710:
	bne	a2, zero, $L5b720	/* if (a2 != 0) goto $L5b720 */
	andi	v0, a1, 0x800	/* delay slot (real work): v0 = a1 & 0x800 */
	beq	v0, zero, $L5b734	/* if (v0 == 0) goto $L5b734 */
	nop	/* delay slot: nop */
$L5b720:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x1a(a0)	/* v0 = *(u16 *)(a0 + 0x1a) */
	nop	/* load delay slot */
	sh	v0, 0x1d6(v1)	/* *(u16 *)(v1 + 0x1d6) = v0 [SPU reverb mRSAME] */

	/*
	 * Register 12, mLCOMB1 (0x1f801dd8): written when the mask is 0 or bit 12 (0x1000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b734:
	bne	a2, zero, $L5b744	/* if (a2 != 0) goto $L5b744 */
	andi	v0, a1, 0x1000	/* delay slot (real work): v0 = a1 & 0x1000 */
	beq	v0, zero, $L5b758	/* if (v0 == 0) goto $L5b758 */
	nop	/* delay slot: nop */
$L5b744:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x1c(a0)	/* v0 = *(u16 *)(a0 + 0x1c) */
	nop	/* load delay slot */
	sh	v0, 0x1d8(v1)	/* *(u16 *)(v1 + 0x1d8) = v0 [SPU reverb mLCOMB1] */

	/*
	 * Register 13, mRCOMB1 (0x1f801dda): written when the mask is 0 or bit 13 (0x2000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b758:
	bne	a2, zero, $L5b768	/* if (a2 != 0) goto $L5b768 */
	andi	v0, a1, 0x2000	/* delay slot (real work): v0 = a1 & 0x2000 */
	beq	v0, zero, $L5b77c	/* if (v0 == 0) goto $L5b77c */
	nop	/* delay slot: nop */
$L5b768:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x1e(a0)	/* v0 = *(u16 *)(a0 + 0x1e) */
	nop	/* load delay slot */
	sh	v0, 0x1da(v1)	/* *(u16 *)(v1 + 0x1da) = v0 [SPU reverb mRCOMB1] */

	/*
	 * Register 14, mLCOMB2 (0x1f801ddc): written when the mask is 0 or bit 14 (0x4000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b77c:
	bne	a2, zero, $L5b78c	/* if (a2 != 0) goto $L5b78c */
	andi	v0, a1, 0x4000	/* delay slot (real work): v0 = a1 & 0x4000 */
	beq	v0, zero, $L5b7a0	/* if (v0 == 0) goto $L5b7a0 */
	nop	/* delay slot: nop */
$L5b78c:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x20(a0)	/* v0 = *(u16 *)(a0 + 0x20) */
	nop	/* load delay slot */
	sh	v0, 0x1dc(v1)	/* *(u16 *)(v1 + 0x1dc) = v0 [SPU reverb mLCOMB2] */

	/*
	 * Register 15, mRCOMB2 (0x1f801dde): written when the mask is 0 or bit 15 (0x8000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b7a0:
	bne	a2, zero, $L5b7b0	/* if (a2 != 0) goto $L5b7b0 */
	andi	v0, a1, 0x8000	/* delay slot (real work): v0 = a1 & 0x8000 */
	beq	v0, zero, $L5b7c4	/* if (v0 == 0) goto $L5b7c4 */
	nop	/* delay slot: nop */
$L5b7b0:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x22(a0)	/* v0 = *(u16 *)(a0 + 0x22) */
	nop	/* load delay slot */
	sh	v0, 0x1de(v1)	/* *(u16 *)(v1 + 0x1de) = v0 [SPU reverb mRCOMB2] */

	/*
	 * Register 16, dLSAME (0x1f801de0): written when the mask is 0 or bit 16 (0x10000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b7c4:
	bne	a2, zero, $L5b7d8	/* if (a2 != 0) goto $L5b7d8 */
	lui	v0, 1	/* delay slot (real work): v0 = 1 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b7ec	/* if (v0 == 0) goto $L5b7ec */
	nop	/* delay slot: nop */
$L5b7d8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x24(a0)	/* v0 = *(u16 *)(a0 + 0x24) */
	nop	/* load delay slot */
	sh	v0, 0x1e0(v1)	/* *(u16 *)(v1 + 0x1e0) = v0 [SPU reverb dLSAME] */

	/*
	 * Register 17, dRSAME (0x1f801de2): written when the mask is 0 or bit 17 (0x20000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b7ec:
	bne	a2, zero, $L5b800	/* if (a2 != 0) goto $L5b800 */
	lui	v0, 2	/* delay slot (real work): v0 = 2 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b814	/* if (v0 == 0) goto $L5b814 */
	nop	/* delay slot: nop */
$L5b800:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x26(a0)	/* v0 = *(u16 *)(a0 + 0x26) */
	nop	/* load delay slot */
	sh	v0, 0x1e2(v1)	/* *(u16 *)(v1 + 0x1e2) = v0 [SPU reverb dRSAME] */

	/*
	 * Register 18, mLDIFF (0x1f801de4): written when the mask is 0 or bit 18 (0x40000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b814:
	bne	a2, zero, $L5b828	/* if (a2 != 0) goto $L5b828 */
	lui	v0, 4	/* delay slot (real work): v0 = 4 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b83c	/* if (v0 == 0) goto $L5b83c */
	nop	/* delay slot: nop */
$L5b828:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x28(a0)	/* v0 = *(u16 *)(a0 + 0x28) */
	nop	/* load delay slot */
	sh	v0, 0x1e4(v1)	/* *(u16 *)(v1 + 0x1e4) = v0 [SPU reverb mLDIFF] */

	/*
	 * Register 19, mRDIFF (0x1f801de6): written when the mask is 0 or bit 19 (0x80000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b83c:
	bne	a2, zero, $L5b850	/* if (a2 != 0) goto $L5b850 */
	lui	v0, 8	/* delay slot (real work): v0 = 8 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b864	/* if (v0 == 0) goto $L5b864 */
	nop	/* delay slot: nop */
$L5b850:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x2a(a0)	/* v0 = *(u16 *)(a0 + 0x2a) */
	nop	/* load delay slot */
	sh	v0, 0x1e6(v1)	/* *(u16 *)(v1 + 0x1e6) = v0 [SPU reverb mRDIFF] */

	/*
	 * Register 20, mLCOMB3 (0x1f801de8): written when the mask is 0 or bit 20 (0x100000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b864:
	bne	a2, zero, $L5b878	/* if (a2 != 0) goto $L5b878 */
	lui	v0, 0x10	/* delay slot (real work): v0 = 0x10 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b88c	/* if (v0 == 0) goto $L5b88c */
	nop	/* delay slot: nop */
$L5b878:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x2c(a0)	/* v0 = *(u16 *)(a0 + 0x2c) */
	nop	/* load delay slot */
	sh	v0, 0x1e8(v1)	/* *(u16 *)(v1 + 0x1e8) = v0 [SPU reverb mLCOMB3] */

	/*
	 * Register 21, mRCOMB3 (0x1f801dea): written when the mask is 0 or bit 21 (0x200000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b88c:
	bne	a2, zero, $L5b8a0	/* if (a2 != 0) goto $L5b8a0 */
	lui	v0, 0x20	/* delay slot (real work): v0 = 0x20 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b8b4	/* if (v0 == 0) goto $L5b8b4 */
	nop	/* delay slot: nop */
$L5b8a0:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x2e(a0)	/* v0 = *(u16 *)(a0 + 0x2e) */
	nop	/* load delay slot */
	sh	v0, 0x1ea(v1)	/* *(u16 *)(v1 + 0x1ea) = v0 [SPU reverb mRCOMB3] */

	/*
	 * Register 22, mLCOMB4 (0x1f801dec): written when the mask is 0 or bit 22 (0x400000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b8b4:
	bne	a2, zero, $L5b8c8	/* if (a2 != 0) goto $L5b8c8 */
	lui	v0, 0x40	/* delay slot (real work): v0 = 0x40 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b8dc	/* if (v0 == 0) goto $L5b8dc */
	nop	/* delay slot: nop */
$L5b8c8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x30(a0)	/* v0 = *(u16 *)(a0 + 0x30) */
	nop	/* load delay slot */
	sh	v0, 0x1ec(v1)	/* *(u16 *)(v1 + 0x1ec) = v0 [SPU reverb mLCOMB4] */

	/*
	 * Register 23, mRCOMB4 (0x1f801dee): written when the mask is 0 or bit 23 (0x800000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b8dc:
	bne	a2, zero, $L5b8f0	/* if (a2 != 0) goto $L5b8f0 */
	lui	v0, 0x80	/* delay slot (real work): v0 = 0x80 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b904	/* if (v0 == 0) goto $L5b904 */
	nop	/* delay slot: nop */
$L5b8f0:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x32(a0)	/* v0 = *(u16 *)(a0 + 0x32) */
	nop	/* load delay slot */
	sh	v0, 0x1ee(v1)	/* *(u16 *)(v1 + 0x1ee) = v0 [SPU reverb mRCOMB4] */

	/*
	 * Register 24, dLDIFF (0x1f801df0): written when the mask is 0 or bit 24 (0x1000000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b904:
	bne	a2, zero, $L5b918	/* if (a2 != 0) goto $L5b918 */
	lui	v0, 0x100	/* delay slot (real work): v0 = 0x100 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b92c	/* if (v0 == 0) goto $L5b92c */
	nop	/* delay slot: nop */
$L5b918:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x34(a0)	/* v0 = *(u16 *)(a0 + 0x34) */
	nop	/* load delay slot */
	sh	v0, 0x1f0(v1)	/* *(u16 *)(v1 + 0x1f0) = v0 [SPU reverb dLDIFF] */

	/*
	 * Register 25, dRDIFF (0x1f801df2): written when the mask is 0 or bit 25 (0x2000000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b92c:
	bne	a2, zero, $L5b940	/* if (a2 != 0) goto $L5b940 */
	lui	v0, 0x200	/* delay slot (real work): v0 = 0x200 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b954	/* if (v0 == 0) goto $L5b954 */
	nop	/* delay slot: nop */
$L5b940:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x36(a0)	/* v0 = *(u16 *)(a0 + 0x36) */
	nop	/* load delay slot */
	sh	v0, 0x1f2(v1)	/* *(u16 *)(v1 + 0x1f2) = v0 [SPU reverb dRDIFF] */

	/*
	 * Register 26, mLAPF1 (0x1f801df4): written when the mask is 0 or bit 26 (0x4000000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b954:
	bne	a2, zero, $L5b968	/* if (a2 != 0) goto $L5b968 */
	lui	v0, 0x400	/* delay slot (real work): v0 = 0x400 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b97c	/* if (v0 == 0) goto $L5b97c */
	nop	/* delay slot: nop */
$L5b968:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x38(a0)	/* v0 = *(u16 *)(a0 + 0x38) */
	nop	/* load delay slot */
	sh	v0, 0x1f4(v1)	/* *(u16 *)(v1 + 0x1f4) = v0 [SPU reverb mLAPF1] */

	/*
	 * Register 27, mRAPF1 (0x1f801df6): written when the mask is 0 or bit 27 (0x8000000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b97c:
	bne	a2, zero, $L5b990	/* if (a2 != 0) goto $L5b990 */
	lui	v0, 0x800	/* delay slot (real work): v0 = 0x800 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b9a4	/* if (v0 == 0) goto $L5b9a4 */
	nop	/* delay slot: nop */
$L5b990:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x3a(a0)	/* v0 = *(u16 *)(a0 + 0x3a) */
	nop	/* load delay slot */
	sh	v0, 0x1f6(v1)	/* *(u16 *)(v1 + 0x1f6) = v0 [SPU reverb mRAPF1] */

	/*
	 * Register 28, mLAPF2 (0x1f801df8): written when the mask is 0 or bit 28 (0x10000000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b9a4:
	bne	a2, zero, $L5b9b8	/* if (a2 != 0) goto $L5b9b8 */
	lui	v0, 0x1000	/* delay slot (real work): v0 = 0x1000 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b9cc	/* if (v0 == 0) goto $L5b9cc */
	nop	/* delay slot: nop */
$L5b9b8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x3c(a0)	/* v0 = *(u16 *)(a0 + 0x3c) */
	nop	/* load delay slot */
	sh	v0, 0x1f8(v1)	/* *(u16 *)(v1 + 0x1f8) = v0 [SPU reverb mLAPF2] */

	/*
	 * Register 29, mRAPF2 (0x1f801dfa): written when the mask is 0 or bit 29 (0x20000000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b9cc:
	bne	a2, zero, $L5b9e0	/* if (a2 != 0) goto $L5b9e0 */
	lui	v0, 0x2000	/* delay slot (real work): v0 = 0x2000 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5b9f4	/* if (v0 == 0) goto $L5b9f4 */
	nop	/* delay slot: nop */
$L5b9e0:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x3e(a0)	/* v0 = *(u16 *)(a0 + 0x3e) */
	nop	/* load delay slot */
	sh	v0, 0x1fa(v1)	/* *(u16 *)(v1 + 0x1fa) = v0 [SPU reverb mRAPF2] */

	/*
	 * Register 30, vLIN (0x1f801dfc): written when the mask is 0 or bit 30 (0x40000000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5b9f4:
	bne	a2, zero, $L5ba08	/* if (a2 != 0) goto $L5ba08 */
	lui	v0, 0x4000	/* delay slot (real work): v0 = 0x4000 << 16 */
	and	v0, a1, v0	/* v0 = a1 & v0 */
	beq	v0, zero, $L5ba1c	/* if (v0 == 0) goto $L5ba1c */
	nop	/* delay slot: nop */
$L5ba08:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x40(a0)	/* v0 = *(u16 *)(a0 + 0x40) */
	nop	/* load delay slot */
	sh	v0, 0x1fc(v1)	/* *(u16 *)(v1 + 0x1fc) = v0 [SPU reverb vLIN] */

	/*
	 * Register 31, vRIN (0x1f801dfe): written when the mask is 0 or bit 31 (0x80000000) is set.
	 * Skipped (branch to the next block) when the bit is clear; the all-registers flag a2
	 * skips the bit test.
	 */
$L5ba1c:
	bne	a2, zero, $L5ba2c	/* if (a2 != 0) goto $L5ba2c */
	nop	/* delay slot: nop */
	bgez	a1, $L5ba40	/* if (a1 >= 0) goto $L5ba40 */
	nop	/* delay slot: nop */
$L5ba2c:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x42(a0)	/* v0 = *(u16 *)(a0 + 0x42) */
	nop	/* load delay slot */
	sh	v0, 0x1fe(v1)	/* *(u16 *)(v1 + 0x1fe) = v0 [SPU reverb vRIN] */

	/*
	 * Epilogue: return 0 (the move executes in the jr delay slot).
	 */
$L5ba40:
	jr	ra	/* return */
	addu	v0, zero, zero	/* delay slot (real work): v0 = zero */
