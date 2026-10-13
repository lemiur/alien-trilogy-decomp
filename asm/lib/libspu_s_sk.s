/*
 * libspu_s_sk.s - LIBSPU module S_SK: SpuSetKey.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_sk.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_SK.
 * Exports:     SpuSetKey (.text+0x0, 128 bytes).
 * Imports:     _spu_fd, _spu_keystat (S_INI .data, 0x80073cf4 and 0x80073d4c),
 *              _spu_ioctl (0x8005ab0c).
 * Own data:    none.
 *
 * Nearest archived version: Runtime Library 3.0 / ISA-board LIBSPU.LIB S_SK (128 bytes, the
 * same three imports and ioctl commands 0x203/0x204; 2.6 has the same member). Relocation-
 * masked, 29 of its 32 words are identical to retail (builds/lane_tools/lcs.py alignment):
 * the only difference is that retail loads "ori a1, zero, 0x203" before the "lui a0 / lw a0"
 * of _spu_fd on the key-off path instead of after it, and fills the final "jr ra" slot with
 * nothing where the archive ends "lw ra / nop / jr ra / addiu sp" (retail: "lw ra / addiu
 * sp / jr ra / nop"). Runtime 3.3 rewrote the module as a 120-byte member of different
 * shape, so no 3.x archive matches: "ISA/2.6/3.0 S_SK with different instruction scheduling".
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * SpuSetKey - key voices on or off, and keep the software copy of the key-on mask
 *
 * Purpose:     long SpuSetKey(long on_off, unsigned long voice_bit). on_off is SPU_OFF (0) or
 *              SPU_ON (1); voice_bit is a mask of SPU_0CH..SPU_23CH. The driver does the key
 *              write: ioctl 0x204 keys the voices on, 0x203 keys them off. Then _spu_keystat,
 *              the library's copy of "which voices are keyed on" (read by SpuGetKeyStatus),
 *              is updated: OR-ed with voice_bit on SPU_ON, and-ed with ~voice_bit on SPU_OFF.
 *              Any other on_off does nothing.
 * Inputs:      a0 = on_off, a1 = voice_bit.
 * Outputs:     v0 = the new _spu_keystat on SPU_ON / SPU_OFF (undefined otherwise).
 * Clobbers:    v0, v1, a0..a2 (caller-saved) and, through _spu_ioctl, whatever it clobbers.
 * Stack:       0x18-byte frame: ra at 0x10; voice_bit is spilled at 0x1c (the caller-
 *              allocated slot of a1) so its address can be passed to the driver.
 * Reads/writes: _spu_fd (read); _spu_keystat (read, written).
 * Callees:     _spu_ioctl(_spu_fd, 0x203 | 0x204, &voice_bit).
 * Callers:     voice_play (0x8004334c) at 0x8004379c and voice_stop (0x80043d50) at
 *              0x80043d8c, both in config/audio.json.
 */
	.globl	SpuSetKey
SpuSetKey:
	addiu	sp, sp, -24	/* allocate the 0x18-byte frame */
	sw	ra, 16(sp)	/* save ra */
	beq	a0, zero, $key_off	/* on_off == 0: key off */
	sw	a1, 28(sp)	/* delay slot (real work): spill voice_bit so its address can be passed */
	/* SPU_ON: key the voices on, then set their bits in _spu_keystat */
	ori	v0, zero, 0x1	/* v0 = 1, SPU_ON */
	bne	a0, v0, $return	/* on_off is neither 0 nor 1: nothing to do */
	ori	a1, zero, 0x204	/* delay slot (real work): a1 = ioctl command 0x204, key on */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* ioctl(_spu_fd, 0x204, &voice_bit) */
	addiu	a2, sp, 28	/* delay slot (real work): a2 = &voice_bit */
	lw	v0, _spu_keystat	/* v0 = _spu_keystat (lui+lw macro) */
	lw	v1, 28(sp)	/* v1 = voice_bit (as the driver left it) */
	j	$store	/* join the store */
	or	v0, v0, v1	/* delay slot (real work): keystat | voice_bit */
$key_off:
	/* SPU_OFF: key the voices off, then clear their bits in _spu_keystat */
	ori	a1, zero, 0x203	/* a1 = ioctl command 0x203, key off */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* ioctl(_spu_fd, 0x203, &voice_bit) */
	addiu	a2, sp, 28	/* delay slot (real work): a2 = &voice_bit */
	lw	v0, 28(sp)	/* v0 = voice_bit */
	lw	v1, _spu_keystat	/* v1 = _spu_keystat (lui+lw macro) */
	nor	v0, zero, v0	/* v0 = ~voice_bit  (nor with zero) */
	and	v0, v0, v1	/* v0 = ~voice_bit & keystat */
$store:
	/* common tail: _spu_keystat = the new mask */
	sw	v0, _spu_keystat	/* macro "sw v0, _spu_keystat": lui AT then the store through AT */
$return:
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
