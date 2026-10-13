/*
 * libspu_s_sca.s - LIBSPU module S_SCA: SpuSetCommonAttr.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_sca.json and the
 * "libspu_s_sca" entry of config/data.json; counted as "library assembly", never as matched C
 * and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_SCA.
 * Exports:     SpuSetCommonAttr (.text+0x0, 916 bytes).
 * Imports:     none (the module-local _spu_RXX below).
 * Own data:    _spu_RXX, a module-local copy of the SPU register base pointer (.data+0x0 =
 *              0x1f801c00), at 0x80073cf0; and two 32-byte jump tables in .rdata, 0x80012c38
 *              (volume mode of the left main volume) and 0x80012c58 (right), whose entries are
 *              addresses inside SpuSetCommonAttr.
 *
 * Nearest archived version: Runtime Library 3.3 LIBSPU.LIB S_SCA (the 3.5, 3.6 and 4.0
 * members are the same 916 bytes). The 916 text bytes and the 64 .rdata bytes are identical
 * to retail's with relocations masked: all 229 words align and the 52 relocated words
 * (builds/lane_tools/lcs.py) differ only in their referent. The archive member has no .data
 * and imports a single global _spu_RXX from the SPU module (SPU .data+8); retail
 * SpuSetCommonAttr reads its own word at 0x80073cf0 instead: one of five words holding
 * 0x1f801c00 in retail .data, each read only by one module (0x80073af8 S_SVA, 0x80073afc
 * S_GKS, 0x80073c84 S_SRMP, 0x80073c90 the SPU main module and 0x80073cf0 this one;
 * analysis/runtime_variant_diagnostics.py). A global _spu_RXX defined in several modules
 * would not link, so this rebuild gives each module a static copy. That is the whole
 * difference from 3.3: "Runtime 3.3 S_SCA with a module-local _spu_RXX instead of the SPU
 * module's global".
 *
 * The routine is compiled C (GCC register use, a switch compiled to a jump table); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

/*
 * _spu_RXX - module-local SPU register base
 *
 * Purpose:     the address of the SPU register file (0x1f801c00), read through a pointer as
 *              all LIBSPU code does. Static: the retail module has its own word.
 * Layout:      one 32-bit pointer.
 * Users:       SpuSetCommonAttr only (the lui/lw pairs below).
 */
	.data
_spu_RXX:
	.word	0x1f801c00	/* SPU register file base (volatile u_short *) */

/*
 * Jump tables of the two volume-mode switches of SpuSetCommonAttr (the compiler's "switch
 * (attr->mvolmode.left)" and ".right"). Each has eight code-address entries, indexed by the
 * mode 0..7 (libspu.h SPU_VOICE_DIRECT .. SPU_VOICE_EXPDec); modes >= 8 skip the switch and
 * behave like mode 0. Entry 0 is the direct-volume path; entries 1..7 each load the sweep-
 * mode bits of the SPU volume register (0x8000, 0x9000, ... 0xe000) and join the common
 * code. Layout: 8 x .word (text address), 32 bytes each. Users: SpuSetCommonAttr.
 */
	.rdata
sca_mvoll_mode_tab:
	.word	$mvoll_m0, $mvoll_m1, $mvoll_m2, $mvoll_m3
	.word	$mvoll_m4, $mvoll_m5, $mvoll_m6, $mvoll_m7
sca_mvolr_mode_tab:
	.word	$mvolr_m0, $mvolr_m1, $mvolr_m2, $mvolr_m3
	.word	$mvolr_m4, $mvolr_m5, $mvolr_m6, $mvolr_m7

	.text

/*
 * SpuSetCommonAttr - set the SPU's common attributes (master volume, CD and external audio)
 *
 * Purpose:     void SpuSetCommonAttr(SpuCommonAttr *attr). attr->mask selects the fields to
 *              apply; a mask of 0 means all of them. SpuCommonAttr (libspu.h):
 *                +0x00 mask   +0x04 mvol.left/right (short)   +0x08 mvolmode.left/right
 *                +0x0c mvolx (get only)   +0x10 cd.volume.left/right (short)
 *                +0x14 cd.reverb   +0x18 cd.mix   +0x1c ext.volume.left/right
 *                +0x20 ext.reverb   +0x24 ext.mix
 *              Mask bits and the SPU registers they write (0x1f801c00 + offset):
 *                0x0001 MVOLL / 0x0004 MVOLMODEL  -> 0x180 main volume left  (D80h)
 *                0x0002 MVOLR / 0x0008 MVOLMODER  -> 0x182 main volume right (D82h)
 *                0x0040 CDVOLL -> 0x1b0 CD audio volume left;  0x0080 CDVOLR -> 0x1b2 right
 *                0x0400 EXTVOLL -> 0x1b4 external volume left; 0x0800 EXTVOLR -> 0x1b6 right
 *                0x0100 CDREV  -> SPUCNT (0x1aa) bit 2  (CD audio reverb enable)
 *                0x0200 CDMIX  -> SPUCNT bit 0          (CD audio enable)
 *                0x1000 EXTREV -> SPUCNT bit 3          (external audio reverb enable)
 *                0x2000 EXTMIX -> SPUCNT bit 1          (external audio enable)
 *              A main volume register is written as  mode | volume: in direct mode (mode
 *              index 0, or index >= 8) the volume is the field & 0x7fff (the mode bits
 *              come from the 15-bit field itself); in sweep modes 1..7 the mode bits are
 *              0x8000 + 0x1000 * index and the volume is the field clamped to 0..0x7f.
 * Inputs:      a0 = attr.
 * Outputs:     none.
 * Clobbers:    v0, v1, a1, a2, a3, t0, t1, t2 (caller-saved). Allocates a 0x10-byte stack
 *              frame it never uses.
 * Registers:   t0 = attr->mask; t1 = (mask == 0), "apply everything"; a1 = volume mode
 *              bits; a3 / t2 = the left / right volume being built.
 * Reads:       *attr, _spu_RXX, SPUCNT (0x1aa) read-modify-write. Writes: SPU registers 0x180,
 *              0x182, 0x1b0, 0x1b2, 0x1b4, 0x1b6, 0x1aa.
 * Callers:     spu_set_master_volume (0x800438e0) at 0x80043914, spu_set_cd_volume
 *              (0x800439f4) at 0x80043a28 and spu_init (0x80044138) at 0x80044190, all in
 *              config/audio.json.
 */
	.globl	SpuSetCommonAttr
SpuSetCommonAttr:
	/* t0 = mask, t1 = (mask == 0) selects every field. Main volume left: written when
	   mask == 0 or MVOLL is set. */
	lw	t0, 0(a0)	/* t0 = attr->mask */
	nop	/* lw load delay slot: nothing to do */
	sltiu	t1, t0, 1	/* t1 = (mask == 0): apply every field */
	bne	t1, zero, $mvoll_mode	/* mask == 0: do the left main volume */
	addiu	sp, sp, -16	/* delay slot (real work): allocate the (unused) 0x10-byte frame */
	andi	v0, t0, 0x1	/* v0 = mask & SPU_COMMON_MVOLL */
	beq	v0, zero, $mvolr	/* not selected: skip to the right volume */
	andi	v0, t0, 0x4	/* delay slot (real work): v0 = mask & SPU_COMMON_MVOLMODEL */
	beq	v0, zero, $mvoll_m0	/* mode not selected: direct volume */
	nop	/* branch delay: nothing to do */
$mvoll_mode:
	/* volume mode: bits 15..12 of the register come from a jump table indexed by
	   mvolmode.left (0..7); the table entries are the "j $join" stubs below */
	lh	v1, 8(a0)	/* v1 = attr->mvolmode.left (signed halfword) */
	nop	/* lh load delay slot: nothing to do */
	sltiu	v0, v1, 8	/* v0 = (mode < 8) */
	beq	v0, zero, $mvoll_m0	/* mode out of range: direct volume */
	sll	v0, v1, 2	/* delay slot (real work): v0 = mode * 4 */
	.set	noat	/* the sequence below names $at (register 1) explicitly */
	la	$1, sca_mvoll_mode_tab	/* AT = &sca_mvoll_mode_tab (la: lui + addiu) ... */
	addu	$1, $1, v0	/* ... AT = &sca_mvoll_mode_tab[mode] */
	lw	v0, 0($1)	/* v0 = sca_mvoll_mode_tab[mode], a code address inside this routine */
	.set	at
	nop	/* lw load delay slot: nothing to do */
	jr	v0	/* switch (mvolmode.left): jump to the entry */
	nop	/* jump delay: nothing to do */
$mvoll_m1:
	j	$mvoll_join	/* mode 1: join, delay slot (real work) loads the mode bits 0x8000 */
	ori	a1, zero, 0x8000	/* delay slot (real work): a1 = 0x8000, linear increase */
$mvoll_m2:
	j	$mvoll_join	/* mode 2: join */
	ori	a1, zero, 0x9000	/* delay slot (real work): a1 = 0x9000, linear increase, inverted phase */
$mvoll_m3:
	j	$mvoll_join	/* mode 3: join */
	ori	a1, zero, 0xa000	/* delay slot (real work): a1 = 0xa000, linear decrease */
$mvoll_m4:
	j	$mvoll_join	/* mode 4: join */
	ori	a1, zero, 0xb000	/* delay slot (real work): a1 = 0xb000, linear decrease, inverted phase */
$mvoll_m5:
	j	$mvoll_join	/* mode 5: join */
	ori	a1, zero, 0xc000	/* delay slot (real work): a1 = 0xc000, exponential increase */
$mvoll_m6:
	j	$mvoll_join	/* mode 6: join */
	ori	a1, zero, 0xd000	/* delay slot (real work): a1 = 0xd000, exponential increase, inverted phase */
$mvoll_m7:
	j	$mvoll_join	/* mode 7: join */
	ori	a1, zero, 0xe000	/* delay slot (real work): a1 = 0xe000, exponential decrease */
$mvoll_m0:
	/* direct mode (also reached for an out-of-range or unselected mode): no mode bits */
	lhu	a3, 4(a0)	/* direct mode: a3 = attr->mvol.left (unsigned halfword) */
	addu	a1, zero, zero	/* a1 = 0, no mode bits */
$mvoll_join:
	/* sweep mode: volume = attr->mvol.left clamped to 0..0x7f */
	beq	a1, zero, $mvoll_write	/* no mode bits: write the field as it is */
	andi	v0, a3, 0x7fff	/* delay slot (real work): v0 = field & 0x7fff, the direct-mode volume */
	lh	a2, 4(a0)	/* a2 = attr->mvol.left (signed) */
	nop	/* lh load delay slot: nothing to do */
	slti	v0, a2, 128	/* v0 = (volume < 0x80) */
	bne	v0, zero, $mvoll_nonneg	/* in range or negative: go and test the sign */
	addu	v1, a2, zero	/* delay slot (real work): v1 = volume */
	j	$mvoll_vol	/* volume >= 0x80: clamp to 0x7f */
	ori	a3, zero, 0x7f	/* delay slot (real work): a3 = 0x7f */
$mvoll_nonneg:
	bgez	a2, $mvoll_vol	/* volume >= 0: use it */
	addu	a3, v1, zero	/* delay slot (real work): a3 = volume */
	addu	a3, zero, zero	/* negative: a3 = 0 */
$mvoll_vol:
	andi	v0, a3, 0x7fff	/* v0 = a3 & 0x7fff */
$mvoll_write:
	/* write main volume left = volume | mode bits */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	or	v0, v0, a1	/* v0 = volume | mode bits */
	sh	v0, 384(v1)	/* write the register at RXX + 0x180 (main volume left) */
$mvolr:
	/* main volume right: same code with mvol.right / mvolmode.right, register +0x182 */
	bne	t1, zero, $mvolr_mode	/* mask == 0: do the right main volume */
	andi	v0, t0, 0x2	/* delay slot (real work): v0 = mask & SPU_COMMON_MVOLR */
	beq	v0, zero, $cdvoll	/* not selected: skip to the CD volume */
	andi	v0, t0, 0x8	/* delay slot (real work): v0 = mask & SPU_COMMON_MVOLMODER */
	beq	v0, zero, $mvolr_m0	/* mode not selected: direct volume */
	nop	/* branch delay: nothing to do */
$mvolr_mode:
	lh	v1, 10(a0)	/* v1 = attr->mvolmode.right (signed halfword) */
	nop
	sltiu	v0, v1, 8	/* v0 = (mode < 8) */
	beq	v0, zero, $mvolr_m0	/* mode out of range: direct volume */
	sll	v0, v1, 2	/* delay slot (real work): v0 = mode * 4 */
	.set	noat	/* the sequence below names $at (register 1) explicitly */
	la	$1, sca_mvolr_mode_tab	/* AT = &sca_mvolr_mode_tab (la: lui + addiu) ... */
	addu	$1, $1, v0	/* ... AT = &sca_mvolr_mode_tab[mode] */
	lw	v0, 0($1)	/* v0 = sca_mvolr_mode_tab[mode] */
	.set	at
	nop
	jr	v0	/* switch (mvolmode.right): jump to the entry */
	nop
$mvolr_m1:
	j	$mvolr_join
	ori	a1, zero, 0x8000
$mvolr_m2:
	j	$mvolr_join
	ori	a1, zero, 0x9000
$mvolr_m3:
	j	$mvolr_join
	ori	a1, zero, 0xa000
$mvolr_m4:
	j	$mvolr_join
	ori	a1, zero, 0xb000
$mvolr_m5:
	j	$mvolr_join
	ori	a1, zero, 0xc000
$mvolr_m6:
	j	$mvolr_join
	ori	a1, zero, 0xd000
$mvolr_m7:
	j	$mvolr_join
	ori	a1, zero, 0xe000
$mvolr_m0:
	lhu	t2, 6(a0)	/* direct mode: t2 = attr->mvol.right (unsigned halfword) */
	addu	a1, zero, zero	/* a1 = 0, no mode bits */
$mvolr_join:
	beq	a1, zero, $mvolr_write	/* no mode bits: write the field as it is */
	andi	v0, t2, 0x7fff	/* delay slot (real work): v0 = field & 0x7fff */
	lh	a2, 6(a0)	/* a2 = attr->mvol.right (signed) */
	nop
	slti	v0, a2, 128	/* v0 = (volume < 0x80) */
	bne	v0, zero, $mvolr_nonneg	/* in range or negative: go and test the sign */
	addu	v1, a2, zero	/* delay slot (real work): v1 = volume */
	j	$mvolr_vol	/* volume >= 0x80: clamp to 0x7f */
	ori	t2, zero, 0x7f	/* delay slot (real work): t2 = 0x7f */
$mvolr_nonneg:
	bgez	a2, $mvolr_vol	/* volume >= 0: use it */
	addu	t2, v1, zero	/* delay slot (real work): t2 = volume */
	addu	t2, zero, zero	/* negative: t2 = 0 */
$mvolr_vol:
	andi	v0, t2, 0x7fff	/* v0 = t2 & 0x7fff */
$mvolr_write:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	or	v0, v0, a1	/* v0 = volume | mode bits */
	sh	v0, 386(v1)	/* write the register at RXX + 0x182 (main volume right) */
$cdvoll:
	/* CD audio volume left/right and external audio volume left/right: plain halfword
	   copies into the volume registers */
	bne	t1, zero, $cdvoll_write	/* mask == 0: do the CD volumes */
	andi	v0, t0, 0x40	/* delay slot (real work): v0 = mask & SPU_COMMON_CDVOLL */
	beq	v0, zero, $cdvolr	/* not selected: skip */
	nop	/* branch delay: nothing to do */
$cdvoll_write:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 16(a0)	/* v0 = attr->cd.volume.left (unsigned) */
	nop
	sh	v0, 432(v1)	/* RXX + 0x1b0 = CD audio volume left */
$cdvolr:
	bne	t1, zero, $cdvolr_write	/* mask == 0: do CD volume right */
	andi	v0, t0, 0x80	/* delay slot (real work): v0 = mask & SPU_COMMON_CDVOLR */
	beq	v0, zero, $cdrev	/* not selected: skip */
	nop	/* branch delay: nothing to do */
$cdvolr_write:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 18(a0)	/* v0 = attr->cd.volume.right (unsigned) */
	nop
	sh	v0, 434(v1)	/* RXX + 0x1b2 = CD audio volume right */
$cdrev:
	/* SPUCNT bit fields: CD reverb (bit 2), CD mix (bit 0), ext reverb (bit 3), ext mix
	   (bit 1): each set or cleared from a non-zero / zero attribute word */
	bne	t1, zero, $cdrev_val	/* mask == 0: do CD reverb */
	andi	v0, t0, 0x100	/* delay slot (real work): v0 = mask & SPU_COMMON_CDREV */
	beq	v0, zero, $cdmix	/* not selected: skip */
	nop	/* branch delay: nothing to do */
$cdrev_val:
	lw	v0, 20(a0)	/* v0 = attr->cd.reverb (SPU_OFF / SPU_ON) */
	nop
	bne	v0, zero, $cdrev_on	/* on: set the bit */
	nop	/* branch delay: nothing to do */
	lw	v0, _spu_RXX	/* off: v0 = _spu_RXX (lui+lw macro) */
	nop
	lhu	v1, 426(v0)	/* v1 = SPUCNT */
	j	$cdrev_write	/* join the store */
	andi	v1, v1, 0xfffb	/* delay slot (real work): clear bit 2 (CD audio reverb enable) */
$cdrev_on:
	lw	v0, _spu_RXX	/* on: v0 = _spu_RXX (lui+lw macro) */
	nop
	lhu	v1, 426(v0)	/* v1 = SPUCNT */
	nop	/* lhu load delay slot: nothing to do */
	ori	v1, v1, 0x4	/* set bit 2 (CD audio reverb enable) */
$cdrev_write:
	sh	v1, 426(v0)	/* write SPUCNT (RXX + 0x1aa) */
$cdmix:
	bne	t1, zero, $cdmix_val	/* mask == 0: do CD mix */
	andi	v0, t0, 0x200	/* delay slot (real work): v0 = mask & SPU_COMMON_CDMIX */
	beq	v0, zero, $extvoll	/* not selected: skip */
	nop	/* branch delay: nothing to do */
$cdmix_val:
	lw	v0, 24(a0)	/* v0 = attr->cd.mix (SPU_OFF / SPU_ON) */
	nop
	bne	v0, zero, $cdmix_on	/* on: set the bit */
	nop	/* branch delay: nothing to do */
	lw	v0, _spu_RXX	/* off: v0 = _spu_RXX (lui+lw macro) */
	nop
	lhu	v1, 426(v0)	/* v1 = SPUCNT */
	j	$cdmix_write	/* join the store */
	andi	v1, v1, 0xfffe	/* delay slot (real work): clear bit 0 (CD audio enable) */
$cdmix_on:
	lw	v0, _spu_RXX	/* on: v0 = _spu_RXX (lui+lw macro) */
	nop
	lhu	v1, 426(v0)	/* v1 = SPUCNT */
	nop
	ori	v1, v1, 0x1	/* set bit 0 (CD audio enable) */
$cdmix_write:
	sh	v1, 426(v0)	/* write SPUCNT */
$extvoll:
	bne	t1, zero, $extvoll_write	/* mask == 0: do external volume left */
	andi	v0, t0, 0x400	/* delay slot (real work): v0 = mask & SPU_COMMON_EXTVOLL */
	beq	v0, zero, $extvolr	/* not selected: skip */
	nop	/* branch delay: nothing to do */
$extvoll_write:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 28(a0)	/* v0 = attr->ext.volume.left (unsigned) */
	nop
	sh	v0, 436(v1)	/* RXX + 0x1b4 = external audio volume left */
$extvolr:
	bne	t1, zero, $extvolr_write	/* mask == 0: do external volume right */
	andi	v0, t0, 0x800	/* delay slot (real work): v0 = mask & SPU_COMMON_EXTVOLR */
	beq	v0, zero, $extrev	/* not selected: skip */
	nop	/* branch delay: nothing to do */
$extvolr_write:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 30(a0)	/* v0 = attr->ext.volume.right (unsigned) */
	nop
	sh	v0, 438(v1)	/* RXX + 0x1b6 = external audio volume right */
$extrev:
	bne	t1, zero, $extrev_val	/* mask == 0: do external reverb */
	andi	v0, t0, 0x1000	/* delay slot (real work): v0 = mask & SPU_COMMON_EXTREV */
	beq	v0, zero, $extmix	/* not selected: skip */
	nop	/* branch delay: nothing to do */
$extrev_val:
	lw	v0, 32(a0)	/* v0 = attr->ext.reverb (SPU_OFF / SPU_ON) */
	nop
	bne	v0, zero, $extrev_on	/* on: set the bit */
	nop	/* branch delay: nothing to do */
	lw	v0, _spu_RXX	/* off: v0 = _spu_RXX (lui+lw macro) */
	nop
	lhu	v1, 426(v0)	/* v1 = SPUCNT */
	j	$extrev_write	/* join the store */
	andi	v1, v1, 0xfff7	/* delay slot (real work): clear bit 3 (external audio reverb enable) */
$extrev_on:
	lw	v0, _spu_RXX	/* on: v0 = _spu_RXX (lui+lw macro) */
	nop
	lhu	v1, 426(v0)	/* v1 = SPUCNT */
	nop
	ori	v1, v1, 0x8	/* set bit 3 (external audio reverb enable) */
$extrev_write:
	sh	v1, 426(v0)	/* write SPUCNT */
$extmix:
	bne	t1, zero, $extmix_val	/* mask == 0: do external mix */
	andi	v0, t0, 0x2000	/* delay slot (real work): v0 = mask & SPU_COMMON_EXTMIX */
	beq	v0, zero, $done	/* not selected: skip */
	nop	/* branch delay: nothing to do */
$extmix_val:
	lw	v0, 36(a0)	/* v0 = attr->ext.mix (SPU_OFF / SPU_ON) */
	nop
	bne	v0, zero, $extmix_on	/* on: set the bit */
	nop	/* branch delay: nothing to do */
	lw	v0, _spu_RXX	/* off: v0 = _spu_RXX (lui+lw macro) */
	nop
	lhu	v1, 426(v0)	/* v1 = SPUCNT */
	j	$extmix_write	/* join the store */
	andi	v1, v1, 0xfffd	/* delay slot (real work): clear bit 1 (external audio enable) */
$extmix_on:
	lw	v0, _spu_RXX	/* on: v0 = _spu_RXX (lui+lw macro) */
	nop
	lhu	v1, 426(v0)	/* v1 = SPUCNT */
	nop
	ori	v1, v1, 0x2	/* set bit 1 (external audio enable) */
$extmix_write:
	sh	v1, 426(v0)	/* write SPUCNT */
$done:
	/* epilogue */
	addiu	sp, sp, 16	/* release the frame */
	jr	ra	/* return */
	nop	/* return delay: nothing to do */
