/*
 * libspu_s_sr.s - LIBSPU module S_SR: SpuSetReverb.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_sr.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_SR.
 * Exports:     SpuSetReverb (.text+0x0, 208 bytes).
 * Imports:     _spu_fd, _spu_rev_flag, _spu_rev_reserve_wa, _spu_rev_offsetaddr (S_INI .data:
 *              0x80073cf4, 0x80073cfc, 0x80073d00, 0x80073d18), _spu_ioctl (0x8005ab0c),
 *              _SpuIsInAllocateArea_ (0x8005990c).
 * Own data:    none.
 *
 * Nearest archived version: Runtime Library 3.3 - 3.6 LIBSPU.LIB S_SR (also 208 bytes, the
 * same control flow and the same imports _spu_rev_flag, _spu_rev_reserve_wa,
 * _spu_rev_offsetaddr, _SpuIsInAllocateArea_). Relocation-masked, 40 of retail's 52 words are
 * identical to it (builds/lane_tools/lcs.py alignment). The 12 that differ are the effect:
 * 3.3 changes the reverb-enable bit (0x80) of the SPU control register itself (a "lhu / andi
 * 0xff7f or ori 0x80 / sh" sequence through _spu_RXX + 0x1aa), whereas retail asks the driver
 * to do it: "la a2, _spu_rev_flag / ori a1, zero, 0x60 / jal _spu_ioctl" with the device
 * descriptor _spu_fd. The 2.6 / 3.0 / ISA S_SR members (172 bytes) differ again, so no
 * archive matches: "Runtime 3.3 S_SR with the reverb switch issued as ioctl 0x60".
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * SpuSetReverb - switch the reverb effect on or off
 *
 * Purpose:     long SpuSetReverb(long on_off). on_off is SPU_OFF (0) or SPU_ON (1);
 *              any other value only returns the current flag.
 *              SPU_OFF: _spu_rev_flag = 0.
 *              SPU_ON : refused (flag forced to 0) when the reverb work area is not the
 *                       reserved one (_spu_rev_reserve_wa != 1) and the current reverb
 *                       offset address lies in memory handed out by SpuMalloc
 *                       (_SpuIsInAllocateArea_(_spu_rev_offsetaddr)); otherwise
 *                       _spu_rev_flag = on_off (1).
 *              Either way the new flag is passed to the driver: ioctl 0x60 on _spu_fd with
 *              the address of _spu_rev_flag, which turns the reverb unit on or off.
 * Inputs:      a0 = on_off (kept in s0).
 * Outputs:     v0 = _spu_rev_flag afterwards (0 off, 1 on).
 * Clobbers:    v0, v1, a0..a2 (caller-saved) and, through the calls, whatever they clobber.
 *              s0 is saved and restored.
 * Stack:       0x18-byte frame: s0 at 0x10, ra at 0x14.
 * Reads/writes: _spu_fd, _spu_rev_reserve_wa, _spu_rev_offsetaddr (read); _spu_rev_flag
 *              (read, written).
 * Callees:     _SpuIsInAllocateArea_, _spu_ioctl(_spu_fd, 0x60, &_spu_rev_flag).
 * Callers:     spu_init (0x80044138, config/audio.json), at 0x800441b4.
 */
	.globl	SpuSetReverb
SpuSetReverb:
	addiu	sp, sp, -24	/* allocate the 0x18-byte frame */
	sw	s0, 16(sp)	/* save s0 */
	addu	s0, a0, zero	/* s0 = on_off */
	beq	s0, zero, $off	/* on_off == SPU_OFF */
	sw	ra, 20(sp)	/* delay slot (real work): save ra */
	ori	v0, zero, 0x1	/* v0 = 1, SPU_ON */
	beq	s0, v0, $on	/* on_off == SPU_ON */
	nop	/* branch delay: nothing to do */
	j	$return	/* any other value: just return the flag */
	nop	/* jump delay: nothing to do */
$off:
	/* SPU_OFF: clear the flag and tell the driver */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	la	a2, _spu_rev_flag	/* a2 = &_spu_rev_flag (la macro: lui + addiu) */
	sw	zero, _spu_rev_flag	/* macro "sw zero, _spu_rev_flag": lui AT then the store through AT */
	j	$ioctl	/* issue the ioctl */
	ori	a1, zero, 0x60	/* delay slot (real work): a1 = ioctl command 0x60 (reverb on/off) */
$on:
	/* SPU_ON: allowed unless the reverb offset address lies in a SpuMalloc block and the
	   work area is not the reserved one */
	lw	v0, _spu_rev_reserve_wa	/* v0 = _spu_rev_reserve_wa (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	beq	v0, s0, $enable	/* work area is the reserved one (== 1): enable */
	ori	a1, zero, 0x60	/* delay slot (real work): a1 = 0x60 for the enable call */
	lw	a0, _spu_rev_offsetaddr	/* a0 = _spu_rev_offsetaddr (lui+lw macro) */
	jal	_SpuIsInAllocateArea_	/* _SpuIsInAllocateArea_(_spu_rev_offsetaddr) */
	nop	/* call delay: nothing to do */
	beq	v0, zero, $enable	/* not inside a SpuMalloc block: enable */
	ori	a1, zero, 0x60	/* delay slot (real work): a1 = 0x60 */
	lw	a0, _spu_fd	/* refuse: a0 = _spu_fd (lui+lw macro) */
	la	a2, _spu_rev_flag	/* a2 = &_spu_rev_flag (la macro) */
	sw	zero, _spu_rev_flag	/* macro "sw zero, _spu_rev_flag": the flag is forced to 0 */
	j	$ioctl	/* issue the ioctl with the flag 0 */
	nop	/* jump delay: nothing to do */
$enable:
	/* enable: store on_off in the flag and tell the driver */
	lw	a0, _spu_fd	/* enable: a0 = _spu_fd (lui+lw macro) */
	la	a2, _spu_rev_flag	/* a2 = &_spu_rev_flag (la macro) */
	sw	s0, _spu_rev_flag	/* macro "sw s0, _spu_rev_flag": the flag becomes on_off (1) */
$ioctl:
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x60, &_spu_rev_flag) */
	nop	/* call delay: nothing to do */
$return:
	/* return the flag */
	lw	v0, _spu_rev_flag	/* v0 = _spu_rev_flag (lui+lw macro), the return value */
	lw	ra, 20(sp)	/* restore ra */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 24	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
