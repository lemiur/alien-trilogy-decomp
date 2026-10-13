/*
 * libspu_s_stm.s - LIBSPU module S_STM: SpuSetTransferMode.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_stm.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_STM.
 * Exports:     SpuSetTransferMode (.text+0x0, 84 bytes).
 * Imports:     _spu_trans_mode, _spu_fd (both S_INI .data: 0x80073cf8, 0x80073cf4),
 *              _spu_ioctl (0x8005ab0c, the SPU device driver's ioctl entry).
 * Own data:    none.
 *
 * Nearest archived version: the Runtime Library 2.6 / 3.0 / ISA-board LIBSPU.LIB S_STM (84
 * bytes, the same three imports). Relocation-masked, 18 of its 21 words are identical to
 * these (builds/lane_tools/lcs.py alignment); the other three are scheduling: retail hoists
 * "ori a1, zero, 0x21" above the _spu_trans_mode store, and ends "lw ra / addiu sp / jr ra /
 * nop" where the archive has "lw ra / nop / jr ra / addiu sp". Runtime 3.3 replaced the
 * module with a 52-byte _spu_transMode wrapper, so no 3.x archive matches: "ISA/2.6/3.0
 * S_STM with the epilogue and one constant load scheduled differently".
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * SpuSetTransferMode - select how SpuWrite/SpuRead move data (DMA or programmed I/O)
 *
 * Purpose:     long SpuSetTransferMode(long mode). mode is SPU_TRANSFER_BY_DMA (0) or
 *              SPU_TRANSFER_BY_IO (1). The mode is kept in _spu_trans_mode, and the driver is
 *              told through ioctl command 0x21 with the argument  (mode == 1) ? 0 : 1  --
 *              i.e. 1 selects DMA in the driver, 0 selects I/O, and any value other than 1
 *              (including an invalid one) is treated as DMA.
 * Inputs:      a0 = mode.
 * Outputs:     none meaningful (v0 is whatever the call left; the library documents void).
 * Clobbers:    v0, a0..a2 (caller-saved) and, through _spu_ioctl, whatever it clobbers.
 * Stack:       0x20-byte frame: ra saved at 0x18, the ioctl argument word at 0x10.
 * Reads:       _spu_fd. Writes: _spu_trans_mode.
 * Callees:     _spu_ioctl(_spu_fd, 0x21, &arg).
 * Callers:     spu_init (config/audio.json, 0x80044138), at 0x80044164.
 */
	.globl	SpuSetTransferMode
SpuSetTransferMode:
	addiu	sp, sp, -32	/* allocate the 0x20-byte frame */
	beq	a0, zero, $dma	/* mode == 0 (DMA): argument is 1 */
	sw	ra, 24(sp)	/* delay slot (real work): save the return address */
	/* mode -> driver argument: 0 -> 1 (DMA), 1 -> 0 (I/O), anything else -> 1 */
	ori	v0, zero, 0x1	/* v0 = 1, the comparison value and the DMA argument */
	bne	a0, v0, $set_arg	/* mode != 1: argument is 1 (v0 already holds it) */
	nop	/* branch delay: nothing to do */
	j	$call	/* mode == 1 (I/O): argument is 0, store it and join */
	sw	zero, 16(sp)	/* delay slot (real work): arg = 0 */
$dma:
	/* mode == 0: DMA */
	ori	v0, zero, 0x1	/* v0 = 1: the DMA argument */
$set_arg:
	sw	v0, 16(sp)	/* arg = 1 (DMA) */
$call:
	/* common tail: remember the mode, tell the driver */
	ori	a1, zero, 0x21	/* a1 = ioctl command 0x21 (set transfer mode) */
	sw	a0, _spu_trans_mode	/* macro "sw a0, _spu_trans_mode": lui AT then the store through AT */
	lw	a0, _spu_fd	/* a0 = _spu_fd, the SPU device descriptor (lui+lw macro) */
	jal	_spu_ioctl	/* call the driver */
	addiu	a2, sp, 16	/* delay slot (real work): a2 = &arg, the pointer to the argument word */
	lw	ra, 24(sp)	/* restore ra */
	addiu	sp, sp, 32	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
