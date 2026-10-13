/*
 * libspu_s_itc.s - LIBSPU module S_ITC: SpuIsTransferCompleted.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_itc.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_ITC.
 * Exports:     SpuIsTransferCompleted (.text+0x0, 124 bytes).
 * Imports:     _spu_trans_mode (S_INI .data, 0x80073cf8), _spu_inTransfer (0x80073cbc, the
 *              SPU module's "idle" flag, initially 1), _spu_EVdma (S_INI .data, 0x80073d50,
 *              the DMA-busy word that SpuStart clears).
 * Own data:    none.
 *
 * Nearest archived version: none. Every archived S_ITC (2.6, 3.0, ISA board: 236 bytes with
 * _spu_getInTransfer/_spu_setInTransfer; 3.3 - 4.7: 168 bytes) decides completion by calling
 * TestEvent(_spu_EVdma); builds/lane_tools/lcs.py aligns only 22 of retail's 31 words with
 * any of them. This rebuild has no TestEvent import and no extra exports: it spins on the
 * word _spu_EVdma itself and sets _spu_inTransfer inline. It is a distinct revision absent
 * from every archive in ../sdk: "SpuIsTransferCompleted, polled-flag revision".
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * SpuIsTransferCompleted - has the last SpuWrite/SpuRead DMA finished?
 *
 * Purpose:     long SpuIsTransferCompleted(long flag). flag is SPU_TRANSFER_PEEK (0, look
 *              and return at once) or SPU_TRANSFER_WAIT (1, wait for completion).
 *              Returns 1 at once when transfers are by programmed I/O (nothing can be in
 *              flight) or when _spu_inTransfer already says idle. Otherwise the DMA is done
 *              when _spu_EVdma is 0. PEEK returns 0 if it is not. WAIT busy-waits until it
 *              clears. Once completion is seen, _spu_inTransfer is set to 1 (idle) and 1 is
 *              returned.
 * Inputs:      a0 = flag.
 * Outputs:     v0 = 1 completed, 0 still in flight (PEEK only).
 * Clobbers:    v0, v1. Leaf routine: no stack frame, no calls.
 * Reads/writes: _spu_trans_mode, _spu_EVdma (read); _spu_inTransfer (read, written).
 * Callees:     none.
 * Callers:     sample_bank_load (config/audio.json, 0x80043ecc), at 0x800440e8.
 */
	.globl	SpuIsTransferCompleted
SpuIsTransferCompleted:
	lw	v0, _spu_trans_mode	/* v0 = _spu_trans_mode (lui+lw macro) */
	ori	v1, zero, 0x1	/* v1 = 1, the constant for all the comparisons below */
	beq	v0, v1, $return	/* programmed-I/O mode (1): nothing can be in flight, return 1 */
	ori	v0, zero, 0x1	/* delay slot (real work): v0 = 1, the return value */
	lw	v0, _spu_inTransfer	/* v0 = _spu_inTransfer (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	bne	v0, v1, $in_flight	/* in transfer (!= 1): look at the DMA flag */
	nop	/* branch delay: nothing to do */
	j	$return	/* idle already: return 1 */
	ori	v0, zero, 0x1	/* delay slot (real work): v0 = 1 */
$in_flight:
	/* a transfer may be in flight: examine the DMA-busy word */
	lw	v0, _spu_EVdma	/* v0 = _spu_EVdma (lui+lw macro) */
	bne	a0, v1, $peek	/* flag != SPU_TRANSFER_WAIT: peek only */
	sltiu	v0, v0, 1	/* delay slot (real work): v0 = (_spu_EVdma == 0), "DMA done" */
	bne	v0, zero, $done	/* WAIT and the DMA is already done: mark idle */
	ori	v0, zero, 0x1	/* delay slot (real work): v0 = 1 */
$wait:
	/* SPU_TRANSFER_WAIT: spin until _spu_EVdma reads 0 */
	lw	v0, _spu_EVdma	/* v0 = _spu_EVdma (lui+lw macro), re-read each pass */
	nop	/* lw load delay slot: nothing to do */
	bne	v0, zero, $wait	/* still busy: spin */
	ori	v0, zero, 0x1	/* delay slot (real work): v0 = 1 (value used once the loop falls out) */
	j	$done	/* DMA finished: mark idle */
	nop	/* jump delay: nothing to do */
$peek:
	/* SPU_TRANSFER_PEEK: completed only if the DMA-done value (v0) is 1; else return 0 */
	bne	v0, v1, $return	/* PEEK: v0 == 1 only when the DMA is done */
	nop	/* branch delay: nothing to do */
$done:
	/* completed: _spu_inTransfer = 1 (idle), return 1 */
	sw	v0, _spu_inTransfer	/* macro "sw v0, _spu_inTransfer": lui AT then the store through AT; v0 is 1 */
$return:
	jr	ra	/* return v0 */
	nop	/* return delay: nothing to do */
