/*

 * libspu_spu_io.s - LIBSPU module SPU_IO: the common-attribute routines (f_5ba48, f_5bde0) and
 * the single-register writer f_5bf0c.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_spu_io.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      SPU_IO (module boundary inferred, see below).
 * Exports:     f_5ba48 (.text+0x000, 920 bytes), f_5bde0 (.text+0x398, 300 bytes),
 *              f_5bf0c (.text+0x4c4, 72 bytes). No archive names them; the address names
 *              stand in until evidence gives real ones.
 * Imports:     _spu_RXX (the SPU module's register base pointer, 0x80073c90),
 *              _spu_mem_mode_plus (the SPU module's address shift, 0x80073cb8).
 * Own data:    none.
 *
 * Nearest archived version: Runtime Library 3.3 LIBSPU.LIB SPU_IO (also 3.5, 3.6). There the
 * module is _spu_ioctl followed by two static routines, and the static routines are these two:
 * the first 920 bytes after the dispatcher (from SPU_IO+0x2f0) equal f_5ba48 and the next 300
 * equal f_5bde0, relocations masked (analysis/data_survey/moddiff.py 3.3 SPU_IO; the module
 * is 752 + 920 + 300 = 1972 bytes). In the retail build the dispatcher lives in the SPU module,
 * so the routines are exported and called by that module's ioctl (jal, cross-module). The
 * 72-byte f_5bf0c has no archive twin; it follows in the same text block and nothing calls it.
 * The module split is inferred: the three routines make one contiguous block whose size
 * matches the tail of the archived SPU_IO, and no archive module of 1292 bytes exists.
 *
 * The routines are compiled C (GCC register use, delay slots filled by the scheduler); they
 * are transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */

	.text

/*
 * f_5ba48 - apply a record of common (global) SPU attributes
 *
 * Purpose:     void f_5ba48(SPU_COMMON_ATTR *attr)  (library-internal; the SPU module's ioctl
 *              dispatcher calls it for code 0x100, the "set common attributes" request behind
 *              SpuSetCommonAttr). The record starts with a 32-bit selection mask followed by the
 *              values. A mask of 0 applies every field; otherwise a field is applied only when its
 *              mask bit is set. Volumes are stored straight to the SPU volume registers; the
 *              on/off switches and small fields are merged into SPUCNT / SPUDTC with a
 *              read-modify-write.
 *              Record layout used here (byte offset: type, field -> destination, mask bit):
 *                +0x00 u32 mask
 *                +0x04 u16 main volume left   -> MVOLL    0x1f801d80   bit 0
 *                +0x06 u16 main volume right  -> MVOLR    0x1f801d82   bit 1
 *                +0x08 u16 reverb volume left -> RVOLL    0x1f801d84   bit 2
 *                +0x0a u16 reverb volume right-> RVOLR    0x1f801d86   bit 3
 *                +0x0c u16 CD volume left     -> CDVOLL   0x1f801db0   bit 4
 *                +0x0e u16 CD volume right    -> CDVOLR   0x1f801db2   bit 5
 *                +0x10 u32 CD reverb on/off   -> SPUCNT bit 2          bit 8
 *                +0x14 u32 CD audio on/off    -> SPUCNT bit 0          bit 10
 *                +0x18 u16 external volume left -> EXTVOLL 0x1f801db4  bit 6
 *                +0x1a u16 external volume right-> EXTVOLR 0x1f801db6  bit 7
 *                +0x1c u32 external reverb on/off -> SPUCNT bit 3      bit 9
 *                +0x20 u32 external audio on/off  -> SPUCNT bit 1      bit 11
 *                +0x24 u16 noise clock (6 bits)   -> SPUCNT bits 13-8  bit 12
 *                +0x26 u16 reverb master enable   -> SPUCNT bit 7      bit 13
 *                +0x28 u16 transfer type (3 bits) -> SPUDTC bits 3-1   bit 14
 *                +0x2a u16 transfer flag          -> SPUDTC bit 0      bit 15
 * Inputs:      a0 = attr (copied to a2 at entry).
 * Outputs:     v0 = 0.
 * Clobbers:    v0, v1, a0, a1, a2, a3. Leaf routine: no stack frame, no calls.
 * Hardware:    SPU registers listed above, through _spu_RXX (0x1f801c00 base).
 * Reads:       _spu_RXX (0x80073c90).
 * Callers:     _spu_ioctl (SPU module), case 0x100.
 * Callees:     none.
 * Register use: a1 = mask, a3 = (mask == 0) "apply everything" flag, a2 = attr, v1 = register
 *              base (a0 in the SPUCNT / SPUDTC read-modify-write blocks).
 */
	.globl	f_5ba48
f_5ba48:

	/*
	 * Prologue: a2 = attr; a1 = mask = attr->mask; a3 = (mask == 0), true when every field is to be applied.
	 */
	addu	a2, a0, zero	/* a2 = a0 */
	lw	a1, 0(a2)	/* a1 = *(u32 *)(a2) */
	nop	/* load delay slot */
	sltiu	a3, a1, 1	/* a3 = ((u32)a1 < 1) */

	/*
	 * Field 0, mask bit 0 (0x1): main volume left: store attr->mvol_l (+0x04) to MVOLL (0x1f801d80).
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
	bne	a3, zero, $L5ba68	/* if (a3 != 0) goto $L5ba68 */
	andi	v0, a1, 1	/* delay slot (real work): v0 = a1 & 1 */
	beq	v0, zero, $L5ba7c	/* if (v0 == 0) goto $L5ba7c */
	nop	/* delay slot: nop */
$L5ba68:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 4(a2)	/* v0 = *(u16 *)(a2 + 4) */
	nop	/* load delay slot */
	sh	v0, 0x180(v1)	/* *(u16 *)(v1 + 0x180) = v0 [SPU MVOLL (main volume left)] */

	/*
	 * Field 1, mask bit 1 (0x2): main volume right: store attr->mvol_r (+0x06) to MVOLR (0x1f801d82).
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5ba7c:
	bne	a3, zero, $L5ba8c	/* if (a3 != 0) goto $L5ba8c */
	andi	v0, a1, 2	/* delay slot (real work): v0 = a1 & 2 */
	beq	v0, zero, $L5baa0	/* if (v0 == 0) goto $L5baa0 */
	nop	/* delay slot: nop */
$L5ba8c:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 6(a2)	/* v0 = *(u16 *)(a2 + 6) */
	nop	/* load delay slot */
	sh	v0, 0x182(v1)	/* *(u16 *)(v1 + 0x182) = v0 [SPU MVOLR (main volume right)] */

	/*
	 * Field 2, mask bit 2 (0x4): reverb output volume left: store attr->rvol_l (+0x08) to RVOLL (0x1f801d84).
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5baa0:
	bne	a3, zero, $L5bab0	/* if (a3 != 0) goto $L5bab0 */
	andi	v0, a1, 4	/* delay slot (real work): v0 = a1 & 4 */
	beq	v0, zero, $L5bac4	/* if (v0 == 0) goto $L5bac4 */
	nop	/* delay slot: nop */
$L5bab0:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 8(a2)	/* v0 = *(u16 *)(a2 + 8) */
	nop	/* load delay slot */
	sh	v0, 0x184(v1)	/* *(u16 *)(v1 + 0x184) = v0 [SPU RVOLL (reverb output volume left)] */

	/*
	 * Field 3, mask bit 3 (0x8): reverb output volume right: store attr->rvol_r (+0x0a) to RVOLR (0x1f801d86).
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bac4:
	bne	a3, zero, $L5bad4	/* if (a3 != 0) goto $L5bad4 */
	andi	v0, a1, 8	/* delay slot (real work): v0 = a1 & 8 */
	beq	v0, zero, $L5bae8	/* if (v0 == 0) goto $L5bae8 */
	nop	/* delay slot: nop */
$L5bad4:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0xa(a2)	/* v0 = *(u16 *)(a2 + 0xa) */
	nop	/* load delay slot */
	sh	v0, 0x186(v1)	/* *(u16 *)(v1 + 0x186) = v0 [SPU RVOLR (reverb output volume right)] */

	/*
	 * Field 4, mask bit 4 (0x10): CD audio volume left: store attr->cdvol_l (+0x0c) to CDVOLL (0x1f801db0).
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bae8:
	bne	a3, zero, $L5baf8	/* if (a3 != 0) goto $L5baf8 */
	andi	v0, a1, 0x10	/* delay slot (real work): v0 = a1 & 0x10 */
	beq	v0, zero, $L5bb0c	/* if (v0 == 0) goto $L5bb0c */
	nop	/* delay slot: nop */
$L5baf8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0xc(a2)	/* v0 = *(u16 *)(a2 + 0xc) */
	nop	/* load delay slot */
	sh	v0, 0x1b0(v1)	/* *(u16 *)(v1 + 0x1b0) = v0 [SPU CDVOLL (CD audio input volume left)] */

	/*
	 * Field 5, mask bit 5 (0x20): CD audio volume right: store attr->cdvol_r (+0x0e) to CDVOLR (0x1f801db2).
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bb0c:
	bne	a3, zero, $L5bb1c	/* if (a3 != 0) goto $L5bb1c */
	andi	v0, a1, 0x20	/* delay slot (real work): v0 = a1 & 0x20 */
	beq	v0, zero, $L5bb30	/* if (v0 == 0) goto $L5bb30 */
	nop	/* delay slot: nop */
$L5bb1c:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0xe(a2)	/* v0 = *(u16 *)(a2 + 0xe) */
	nop	/* load delay slot */
	sh	v0, 0x1b2(v1)	/* *(u16 *)(v1 + 0x1b2) = v0 [SPU CDVOLR (CD audio input volume right)] */

	/*
	 * Field 6, mask bit 8 (0x100): CD audio reverb: SPUCNT bit 2 (0x4) cleared when attr->cd_reverb (+0x10) is 0, set otherwise.
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bb30:
	bne	a3, zero, $L5bb40	/* if (a3 != 0) goto $L5bb40 */
	andi	v0, a1, 0x100	/* delay slot (real work): v0 = a1 & 0x100 */
	beq	v0, zero, $L5bb84	/* if (v0 == 0) goto $L5bb84 */
	nop	/* delay slot: nop */
$L5bb40:
	lw	v0, 0x10(a2)	/* v0 = *(u32 *)(a2 + 0x10) */
	nop	/* load delay slot */
	bne	v0, zero, $L5bb68	/* if (v0 != 0) goto $L5bb68 */
	nop	/* delay slot: nop */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	j	$L5bb80	/* goto $L5bb80 */
	andi	v0, v0, 0xfffb	/* delay slot (real work): v0 = v0 & 0xfffb */
$L5bb68:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	ori	v0, v0, 4	/* v0 = v0 | 4 */
$L5bb80:
	sh	v0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = v0 [SPU SPUCNT (control)] */

	/*
	 * Field 7, mask bit 10 (0x400): CD audio enable: SPUCNT bit 0 (0x1) cleared when attr->cd_on (+0x14) is 0, set otherwise.
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bb84:
	bne	a3, zero, $L5bb94	/* if (a3 != 0) goto $L5bb94 */
	andi	v0, a1, 0x400	/* delay slot (real work): v0 = a1 & 0x400 */
	beq	v0, zero, $L5bbd8	/* if (v0 == 0) goto $L5bbd8 */
	nop	/* delay slot: nop */
$L5bb94:
	lw	v0, 0x14(a2)	/* v0 = *(u32 *)(a2 + 0x14) */
	nop	/* load delay slot */
	bne	v0, zero, $L5bbbc	/* if (v0 != 0) goto $L5bbbc */
	nop	/* delay slot: nop */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	j	$L5bbd4	/* goto $L5bbd4 */
	andi	v0, v0, 0xfffe	/* delay slot (real work): v0 = v0 & 0xfffe */
$L5bbbc:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	ori	v0, v0, 1	/* v0 = v0 | 1 */
$L5bbd4:
	sh	v0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = v0 [SPU SPUCNT (control)] */

	/*
	 * Field 8, mask bit 6 (0x40): external audio volume left: store attr->extvol_l (+0x18) to EXTVOLL (0x1f801db4).
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bbd8:
	bne	a3, zero, $L5bbe8	/* if (a3 != 0) goto $L5bbe8 */
	andi	v0, a1, 0x40	/* delay slot (real work): v0 = a1 & 0x40 */
	beq	v0, zero, $L5bbfc	/* if (v0 == 0) goto $L5bbfc */
	nop	/* delay slot: nop */
$L5bbe8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x18(a2)	/* v0 = *(u16 *)(a2 + 0x18) */
	nop	/* load delay slot */
	sh	v0, 0x1b4(v1)	/* *(u16 *)(v1 + 0x1b4) = v0 [SPU EXTVOLL (external audio volume left)] */

	/*
	 * Field 9, mask bit 7 (0x80): external audio volume right: store attr->extvol_r (+0x1a) to EXTVOLR (0x1f801db6).
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bbfc:
	bne	a3, zero, $L5bc0c	/* if (a3 != 0) goto $L5bc0c */
	andi	v0, a1, 0x80	/* delay slot (real work): v0 = a1 & 0x80 */
	beq	v0, zero, $L5bc20	/* if (v0 == 0) goto $L5bc20 */
	nop	/* delay slot: nop */
$L5bc0c:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0x1a(a2)	/* v0 = *(u16 *)(a2 + 0x1a) */
	nop	/* load delay slot */
	sh	v0, 0x1b6(v1)	/* *(u16 *)(v1 + 0x1b6) = v0 [SPU EXTVOLR (external audio volume right)] */

	/*
	 * Field 10, mask bit 9 (0x200): external audio reverb: SPUCNT bit 3 (0x8) cleared when attr->ext_reverb (+0x1c) is 0, set otherwise.
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bc20:
	bne	a3, zero, $L5bc30	/* if (a3 != 0) goto $L5bc30 */
	andi	v0, a1, 0x200	/* delay slot (real work): v0 = a1 & 0x200 */
	beq	v0, zero, $L5bc74	/* if (v0 == 0) goto $L5bc74 */
	nop	/* delay slot: nop */
$L5bc30:
	lw	v0, 0x1c(a2)	/* v0 = *(u32 *)(a2 + 0x1c) */
	nop	/* load delay slot */
	bne	v0, zero, $L5bc58	/* if (v0 != 0) goto $L5bc58 */
	nop	/* delay slot: nop */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	j	$L5bc70	/* goto $L5bc70 */
	andi	v0, v0, 0xfff7	/* delay slot (real work): v0 = v0 & 0xfff7 */
$L5bc58:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	ori	v0, v0, 8	/* v0 = v0 | 8 */
$L5bc70:
	sh	v0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = v0 [SPU SPUCNT (control)] */

	/*
	 * Field 11, mask bit 11 (0x800): external audio enable: SPUCNT bit 1 (0x2) cleared when attr->ext_on (+0x20) is 0, set otherwise.
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bc74:
	bne	a3, zero, $L5bc84	/* if (a3 != 0) goto $L5bc84 */
	andi	v0, a1, 0x800	/* delay slot (real work): v0 = a1 & 0x800 */
	beq	v0, zero, $L5bcc8	/* if (v0 == 0) goto $L5bcc8 */
	nop	/* delay slot: nop */
$L5bc84:
	lw	v0, 0x20(a2)	/* v0 = *(u32 *)(a2 + 0x20) */
	nop	/* load delay slot */
	bne	v0, zero, $L5bcac	/* if (v0 != 0) goto $L5bcac */
	nop	/* delay slot: nop */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	j	$L5bcc4	/* goto $L5bcc4 */
	andi	v0, v0, 0xfffd	/* delay slot (real work): v0 = v0 & 0xfffd */
$L5bcac:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	ori	v0, v0, 2	/* v0 = v0 | 2 */
$L5bcc4:
	sh	v0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = v0 [SPU SPUCNT (control)] */

	/*
	 * Field 12, mask bit 12 (0x1000): noise clock: SPUCNT bits 13-8 (0x3f00) replaced by attr->noise_clock (+0x24) & 0x3f.
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bcc8:
	bne	a3, zero, $L5bcd8	/* if (a3 != 0) goto $L5bcd8 */
	andi	v0, a1, 0x1000	/* delay slot (real work): v0 = a1 & 0x1000 */
	beq	v0, zero, $L5bd0c	/* if (v0 == 0) goto $L5bd0c */
	nop	/* delay slot: nop */
$L5bcd8:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(a0)	/* v0 = *(u16 *)(a0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0xc0ff	/* v0 = v0 & 0xc0ff */
	sh	v0, 0x1aa(a0)	/* *(u16 *)(a0 + 0x1aa) = v0 [SPU SPUCNT (control)] */
	lhu	v0, 0x24(a2)	/* v0 = *(u16 *)(a2 + 0x24) */
	lhu	v1, 0x1aa(a0)	/* v1 = *(u16 *)(a0 + 0x1aa) [SPU SPUCNT (control)] */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
	sll	v0, v0, 8	/* v0 = v0 << 8 */
	or	v1, v1, v0	/* v1 = v1 | v0 */
	sh	v1, 0x1aa(a0)	/* *(u16 *)(a0 + 0x1aa) = v1 [SPU SPUCNT (control)] */

	/*
	 * Field 13, mask bit 13 (0x2000): reverb master enable: SPUCNT bit 7 (0x80) replaced by attr->reverb_on (+0x26) & 1.
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bd0c:
	bne	a3, zero, $L5bd1c	/* if (a3 != 0) goto $L5bd1c */
	andi	v0, a1, 0x2000	/* delay slot (real work): v0 = a1 & 0x2000 */
	beq	v0, zero, $L5bd50	/* if (v0 == 0) goto $L5bd50 */
	nop	/* delay slot: nop */
$L5bd1c:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(a0)	/* v0 = *(u16 *)(a0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0xff7f	/* v0 = v0 & 0xff7f */
	sh	v0, 0x1aa(a0)	/* *(u16 *)(a0 + 0x1aa) = v0 [SPU SPUCNT (control)] */
	lhu	v0, 0x26(a2)	/* v0 = *(u16 *)(a2 + 0x26) */
	lhu	v1, 0x1aa(a0)	/* v1 = *(u16 *)(a0 + 0x1aa) [SPU SPUCNT (control)] */
	andi	v0, v0, 1	/* v0 = v0 & 1 */
	sll	v0, v0, 7	/* v0 = v0 << 7 */
	or	v1, v1, v0	/* v1 = v1 | v0 */
	sh	v1, 0x1aa(a0)	/* *(u16 *)(a0 + 0x1aa) = v1 [SPU SPUCNT (control)] */

	/*
	 * Field 14, mask bit 14 (0x4000): transfer type: SPUDTC (0x1f801dac) bits 3-1 (0xe) replaced by attr->xfer_type (+0x28) & 7.
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bd50:
	bne	a3, zero, $L5bd60	/* if (a3 != 0) goto $L5bd60 */
	andi	v0, a1, 0x4000	/* delay slot (real work): v0 = a1 & 0x4000 */
	beq	v0, zero, $L5bd94	/* if (v0 == 0) goto $L5bd94 */
	nop	/* delay slot: nop */
$L5bd60:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1ac(a0)	/* v0 = *(u16 *)(a0 + 0x1ac) [SPU SPUDTC (data transfer control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0xfff1	/* v0 = v0 & 0xfff1 */
	sh	v0, 0x1ac(a0)	/* *(u16 *)(a0 + 0x1ac) = v0 [SPU SPUDTC (data transfer control)] */
	lhu	v0, 0x28(a2)	/* v0 = *(u16 *)(a2 + 0x28) */
	lhu	v1, 0x1ac(a0)	/* v1 = *(u16 *)(a0 + 0x1ac) [SPU SPUDTC (data transfer control)] */
	andi	v0, v0, 7	/* v0 = v0 & 7 */
	sll	v0, v0, 1	/* v0 = v0 << 1 */
	or	v1, v1, v0	/* v1 = v1 | v0 */
	sh	v1, 0x1ac(a0)	/* *(u16 *)(a0 + 0x1ac) = v1 [SPU SPUDTC (data transfer control)] */

	/*
	 * Field 15, mask bit 15 (0x8000): transfer flag: SPUDTC (0x1f801dac) bit 0 replaced by attr->xfer_flag (+0x2a) & 1.
	 * Applied when the mask is 0 (a3 set) or the bit is set; otherwise skipped.
	 */
$L5bd94:
	bne	a3, zero, $L5bda8	/* if (a3 != 0) goto $L5bda8 */
	nop	/* delay slot: nop */
	andi	v0, a1, 0x8000	/* v0 = a1 & 0x8000 */
	beq	v0, zero, $L5bdd8	/* if (v0 == 0) goto $L5bdd8 */
	nop	/* delay slot: nop */
$L5bda8:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1ac(a0)	/* v0 = *(u16 *)(a0 + 0x1ac) [SPU SPUDTC (data transfer control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0xfffe	/* v0 = v0 & 0xfffe */
	sh	v0, 0x1ac(a0)	/* *(u16 *)(a0 + 0x1ac) = v0 [SPU SPUDTC (data transfer control)] */
	lhu	v0, 0x2a(a2)	/* v0 = *(u16 *)(a2 + 0x2a) */
	lhu	v1, 0x1ac(a0)	/* v1 = *(u16 *)(a0 + 0x1ac) [SPU SPUDTC (data transfer control)] */
	andi	v0, v0, 1	/* v0 = v0 & 1 */
	or	v1, v1, v0	/* v1 = v1 | v0 */
	sh	v1, 0x1ac(a0)	/* *(u16 *)(a0 + 0x1ac) = v1 [SPU SPUDTC (data transfer control)] */

	/*
	 * Epilogue: return 0 (the move executes in the jr delay slot).
	 */
$L5bdd8:
	jr	ra	/* return */
	addu	v0, zero, zero	/* delay slot (real work): v0 = zero */

/*
 * f_5bde0 - read back the common (global) SPU attributes
 *
 * Purpose:     void f_5bde0(SPU_COMMON_ATTR *attr)  (library-internal; the SPU module's ioctl
 *              dispatcher calls it for code 0x101, the "get common attributes" request behind
 *              SpuGetCommonAttr). Fills the record that f_5ba48 consumes (same layout, see there)
 *              from the live SPU registers; the mask word at +0 is not touched. On/off switches
 *              come out as 0 or 1.
 *              Extra fields (not settable through f_5ba48):
 *                +0x2c u32 ENDX, bit n set when voice n has reached the end of its sample
 *                          (0x1f801d9c low half, 0x1f801d9e high half)
 *                +0x30 u16 SPUSTAT (0x1f801dae)
 * Inputs:      a0 = attr, the record to fill.
 * Outputs:     v0 = 0.
 * Clobbers:    v0, v1, a1. Leaf routine: no stack frame, no calls.
 * Hardware:    SPU registers read through _spu_RXX (a1 holds the base throughout).
 * Reads:       _spu_RXX (0x80073c90).
 * Callers:     _spu_ioctl (SPU module), case 0x101.
 * Callees:     none.
 */
	.globl	f_5bde0
f_5bde0:

	/*
	 * Load the register base: a1 = _spu_RXX. Each field below is one load from an SPU register
	 * and one store into the record at a0.
	 */
	lw	a1, _spu_RXX	/* a1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */

	/*
	 * Volumes: copy MVOLL, MVOLR, RVOLL, RVOLR, CDVOLL, CDVOLR (SPU 0x180, 0x182, 0x184, 0x186, 0x1b0, 0x1b2) to attr +0x04..+0x0e.
	 */
	lhu	v0, 0x180(a1)	/* v0 = *(u16 *)(a1 + 0x180) [SPU MVOLL (main volume left)] */
	nop	/* load delay slot */
	sh	v0, 4(a0)	/* *(u16 *)(a0 + 4) = v0 */
	lhu	v0, 0x182(a1)	/* v0 = *(u16 *)(a1 + 0x182) [SPU MVOLR (main volume right)] */
	nop	/* load delay slot */
	sh	v0, 6(a0)	/* *(u16 *)(a0 + 6) = v0 */
	lhu	v0, 0x184(a1)	/* v0 = *(u16 *)(a1 + 0x184) [SPU RVOLL (reverb output volume left)] */
	nop	/* load delay slot */
	sh	v0, 8(a0)	/* *(u16 *)(a0 + 8) = v0 */
	lhu	v0, 0x186(a1)	/* v0 = *(u16 *)(a1 + 0x186) [SPU RVOLR (reverb output volume right)] */
	nop	/* load delay slot */
	sh	v0, 0xa(a0)	/* *(u16 *)(a0 + 0xa) = v0 */
	lhu	v0, 0x1b0(a1)	/* v0 = *(u16 *)(a1 + 0x1b0) [SPU CDVOLL (CD audio input volume left)] */
	nop	/* load delay slot */
	sh	v0, 0xc(a0)	/* *(u16 *)(a0 + 0xc) = v0 */
	lhu	v0, 0x1b2(a1)	/* v0 = *(u16 *)(a1 + 0x1b2) [SPU CDVOLR (CD audio input volume right)] */
	nop	/* load delay slot */
	sh	v0, 0xe(a0)	/* *(u16 *)(a0 + 0xe) = v0 */

	/*
	 * SPUCNT bit 2 (CD audio reverb) -> attr->cd_reverb (+0x10).
	 */
	lhu	v0, 0x1aa(a1)	/* v0 = *(u16 *)(a1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	srl	v0, v0, 2	/* v0 = v0 >> (logical) 2 */
	andi	v0, v0, 1	/* v0 = v0 & 1 */
	sw	v0, 0x10(a0)	/* *(u32 *)(a0 + 0x10) = v0 */

	/*
	 * SPUCNT bit 0 (CD audio enable) -> attr->cd_on (+0x14).
	 */
	lhu	v0, 0x1aa(a1)	/* v0 = *(u16 *)(a1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 1	/* v0 = v0 & 1 */
	sw	v0, 0x14(a0)	/* *(u32 *)(a0 + 0x14) = v0 */

	/*
	 * External audio volumes: EXTVOLL (0x1b4), EXTVOLR (0x1b6) -> attr +0x18, +0x1a.
	 */
	lhu	v0, 0x1b4(a1)	/* v0 = *(u16 *)(a1 + 0x1b4) [SPU EXTVOLL (external audio volume left)] */
	nop	/* load delay slot */
	sh	v0, 0x18(a0)	/* *(u16 *)(a0 + 0x18) = v0 */
	lhu	v0, 0x1b6(a1)	/* v0 = *(u16 *)(a1 + 0x1b6) [SPU EXTVOLR (external audio volume right)] */
	nop	/* load delay slot */
	sh	v0, 0x1a(a0)	/* *(u16 *)(a0 + 0x1a) = v0 */

	/*
	 * SPUCNT bit 3 (external audio reverb) -> attr->ext_reverb (+0x1c).
	 */
	lhu	v0, 0x1aa(a1)	/* v0 = *(u16 *)(a1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	srl	v0, v0, 3	/* v0 = v0 >> (logical) 3 */
	andi	v0, v0, 1	/* v0 = v0 & 1 */
	sw	v0, 0x1c(a0)	/* *(u32 *)(a0 + 0x1c) = v0 */

	/*
	 * SPUCNT bit 1 (external audio enable) -> attr->ext_on (+0x20).
	 */
	lhu	v0, 0x1aa(a1)	/* v0 = *(u16 *)(a1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	srl	v0, v0, 1	/* v0 = v0 >> (logical) 1 */
	andi	v0, v0, 1	/* v0 = v0 & 1 */
	sw	v0, 0x20(a0)	/* *(u32 *)(a0 + 0x20) = v0 */

	/*
	 * SPUCNT bits 13-8 (noise clock) -> attr->noise_clock (+0x24).
	 */
	lhu	v0, 0x1aa(a1)	/* v0 = *(u16 *)(a1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x3f00	/* v0 = v0 & 0x3f00 */
	srl	v0, v0, 8	/* v0 = v0 >> (logical) 8 */
	sh	v0, 0x24(a0)	/* *(u16 *)(a0 + 0x24) = v0 */

	/*
	 * SPUCNT bit 7 (reverb master enable) -> attr->reverb_on (+0x26).
	 */
	lhu	v0, 0x1aa(a1)	/* v0 = *(u16 *)(a1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	srl	v0, v0, 7	/* v0 = v0 >> (logical) 7 */
	andi	v0, v0, 1	/* v0 = v0 & 1 */
	sh	v0, 0x26(a0)	/* *(u16 *)(a0 + 0x26) = v0 */

	/*
	 * SPUDTC (0x1ac) bits 3-1 (transfer type) -> attr->xfer_type (+0x28).
	 */
	lhu	v0, 0x1ac(a1)	/* v0 = *(u16 *)(a1 + 0x1ac) [SPU SPUDTC (data transfer control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0xe	/* v0 = v0 & 0xe */
	srl	v0, v0, 1	/* v0 = v0 >> (logical) 1 */
	sh	v0, 0x28(a0)	/* *(u16 *)(a0 + 0x28) = v0 */

	/*
	 * SPUDTC (0x1ac) bit 0 (transfer flag) -> attr->xfer_flag (+0x2a).
	 */
	lhu	v0, 0x1ac(a1)	/* v0 = *(u16 *)(a1 + 0x1ac) [SPU SPUDTC (data transfer control)] */
	nop	/* load delay slot */
	andi	v0, v0, 1	/* v0 = v0 & 1 */
	sh	v0, 0x2a(a0)	/* *(u16 *)(a0 + 0x2a) = v0 */

	/*
	 * ENDX: (high half 0x19e << 16) | low half 0x19c -> attr->endx (+0x2c); the low-half load is scheduled after the high-half one.
	 */
	lhu	v0, 0x19e(a1)	/* v0 = *(u16 *)(a1 + 0x19e) [SPU ENDX high (voices 16-23)] */
	lhu	v1, 0x19c(a1)	/* v1 = *(u16 *)(a1 + 0x19c) [SPU ENDX low (voice reached loop end, voices 0-15)] */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	or	v1, v1, v0	/* v1 = v1 | v0 */
	sw	v1, 0x2c(a0)	/* *(u32 *)(a0 + 0x2c) = v1 */

	/*
	 * SPUSTAT (0x1ae) -> attr->status (+0x30); then return 0 (move in the jr delay slot).
	 */
	lhu	v0, 0x1ae(a1)	/* v0 = *(u16 *)(a1 + 0x1ae) [SPU SPUSTAT (status)] */
	nop	/* load delay slot */
	sh	v0, 0x30(a0)	/* *(u16 *)(a0 + 0x30) = v0 */
	jr	ra	/* return */
	addu	v0, zero, zero	/* delay slot (real work): v0 = zero */

/*
 * f_5bf0c - write one 16-bit SPU register, optionally converting a byte address
 *
 * Purpose:     void f_5bf0c(int index, int value, int convert)  Stores value to the SPU
 *              halfword register at _spu_RXX + 2 * index. With convert == 0 the value is stored
 *              as is. With convert != 0 the value is a sound RAM byte address and is shifted right
 *              by _spu_mem_mode_plus (3, the 8-byte SPU address unit) first, which is the form the
 *              SPU address registers (TSA, IRQ_ADDR, mBASE, voice start / repeat addresses)
 *              take.
 * Inputs:      a0 = index (halfword index into the register file), a1 = value, a2 = convert.
 * Outputs:     none (v0 is whatever the last instruction left; callers do not read it).
 * Clobbers:    v0, v1, a0. Leaf routine: no stack frame, no calls.
 * Hardware:    one SPU register write through _spu_RXX.
 * Reads:       _spu_RXX (0x80073c90), _spu_mem_mode_plus (0x80073cb8, when convert != 0).
 * Callers:     none in the retail image. The routine is a library export that no code links
 *              against; it is present because the linker takes whole modules.
 * Callees:     none.
 */
	.globl	f_5bf0c
f_5bf0c:

	/*
	 * index in a0, value in a1, convert flag in a2; v0 = index * 2 is computed in the bnez delay slot.
	 */
	bne	a2, zero, $L5bf30	/* if (a2 != 0) goto $L5bf30 */
	sll	v0, a0, 1	/* delay slot (real work): v0 = a0 << 1 */

	/*
	 * convert == 0: v1 = _spu_RXX; store the value unchanged at _spu_RXX + 2 * index.
	 */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sh	a1, 0(v0)	/* *(u16 *)(v0) = a1 */
	j	$L5bf4c	/* goto $L5bf4c */
	nop	/* delay slot: nop */

	/*
	 * convert != 0: shift the byte address right by _spu_mem_mode_plus (srav) and store the result at _spu_RXX + 2 * index.
	 */
$L5bf30:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	lw	v1, _spu_mem_mode_plus	/* v1 = _spu_mem_mode_plus (lui+lw macro) */
	addu	v0, v0, a0	/* v0 = v0 + a0 */
	srav	v1, a1, v1	/* v1 = a1 >> (arith) v1 */
	sh	v1, 0(v0)	/* *(u16 *)(v0) = v1 */
$L5bf4c:
	jr	ra	/* return */
	nop	/* delay slot: nop */
