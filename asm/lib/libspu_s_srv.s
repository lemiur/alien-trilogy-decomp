/*
 * libspu_s_srv.s - LIBSPU module S_SRV: SpuSetReverbVoice.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_srv.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_SRV.
 * Exports:     SpuSetReverbVoice (.text+0x0, 104 bytes).
 * Imports:     _spu_fd (S_INI .data, 0x80073cf4), _spu_ioctl (0x8005ab0c).
 * Own data:    none.
 *
 * Nearest archived version: the Runtime Library 2.6 / 3.0 / ISA-board LIBSPU.LIB S_SRV (104
 * bytes, the same two imports and the same ioctl commands 0x65/0x66/0x67). Relocation-masked,
 * 20 of its 26 words are identical to these (builds/lane_tools/lcs.py alignment); the other
 * six are scheduling: retail fills the delay slots of the two branches with "ori a1" command
 * loads and ends "lw v0 / lw ra / addiu sp / jr ra / nop" where the archive ends "lw ra /
 * nop / jr ra / addiu sp". Runtime 3.3 and later rebuilt the module as 36 bytes, so no 3.x
 * archive matches: "ISA/2.6/3.0 S_SRV with different delay-slot scheduling".
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * SpuSetReverbVoice - switch reverb on or off for a set of voices, return the current set
 *
 * Purpose:     unsigned long SpuSetReverbVoice(long on_off, unsigned long voice_bit).
 *              on_off is SPU_OFF (0) or SPU_ON (1); voice_bit is a mask of SPU_0CH..SPU_23CH.
 *              SPU_ON  : ioctl 0x65 (add voice_bit to the reverb voice mask);
 *              SPU_OFF : ioctl 0x67 (remove voice_bit from it);
 *              any other on_off only queries. Always finishes with ioctl 0x66 (read the
 *              mask) and returns the mask it gives back.
 * Inputs:      a0 = on_off, a1 = voice_bit.
 * Outputs:     v0 = the reverb voice mask after the change.
 * Clobbers:    v0, a0..a2 (caller-saved) and, through _spu_ioctl, whatever it clobbers.
 * Stack:       0x20-byte frame: ra at 0x18, the result word at 0x10, voice_bit (spilled
 *              argument, passed by pointer) at 0x24 (the caller-allocated argument area).
 * Reads:       _spu_fd. Writes: none directly (the driver keeps the mask).
 * Callees:     _spu_ioctl(_spu_fd, command, &word).
 * Callers:     voice_reverb_on (0x80043df0), voice_reverb_off (0x80043e44) in config/audio.json,
 *              reaching it at 0x80043894 (voice_play), 0x80043e28 and 0x80043e7c.
 */
	.globl	SpuSetReverbVoice
SpuSetReverbVoice:
	addiu	sp, sp, -32	/* allocate the 0x20-byte frame */
	sw	ra, 24(sp)	/* save ra */
	beq	a0, zero, $off	/* on_off == 0: take the clear path */
	sw	a1, 36(sp)	/* delay slot (real work): spill voice_bit so its address can be passed */
	ori	v0, zero, 0x1	/* v0 = 1, the SPU_ON value to compare with */
	bne	a0, v0, $query	/* on_off != 1: query only */
	ori	a1, zero, 0x66	/* delay slot (real work): a1 = 0x66 (read the reverb mask), used by the query path */
	lw	a0, _spu_fd	/* on_off == 1: a0 = _spu_fd (lui+lw macro) */
	j	$set	/* join the common ioctl call */
	ori	a1, zero, 0x65	/* delay slot (real work): a1 = 0x65, ioctl "reverb voices on" */
$off:
	/* SPU_OFF: command 0x67 */
	lw	a0, _spu_fd	/* on_off == 0: a0 = _spu_fd (lui+lw macro) */
	ori	a1, zero, 0x67	/* a1 = 0x67, ioctl "reverb voices off" */
$set:
	jal	_spu_ioctl	/* ioctl(_spu_fd, 0x65 or 0x67, &voice_bit) */
	addiu	a2, sp, 36	/* delay slot (real work): a2 = &voice_bit */
	ori	a1, zero, 0x66	/* a1 = 0x66, ioctl "read reverb mask" */
$query:
	/* read back the reverb voice mask (command 0x66) and return it */
	lw	a0, _spu_fd	/* query: a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* ioctl(_spu_fd, 0x66, &result) */
	addiu	a2, sp, 16	/* delay slot (real work): a2 = &result */
	lw	v0, 16(sp)	/* v0 = the mask the driver stored */
	lw	ra, 24(sp)	/* restore ra */
	addiu	sp, sp, 32	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
