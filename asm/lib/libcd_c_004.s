/*
 * libcd_c_004.s - LIBCD module C_004: data_ready_callback, the DMA-complete callback of the stream layer.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library module that retail
 * links as a rebuild matching no archive in ../sdk. Assembled by tools/build_asm.py (CPPPSX, then ASPSX 2.34);
 * placed by config/libcd_c_004.json and the "libcd_c_004" entry of config/data.json; counted as "library
 * assembly", never as matched C and never as certified library.
 *
 * Library:   LIBCD.LIB (Sony Runtime Library, CD-ROM streaming).   Module: C_004 (retail 0x80061474..0x800614fb,
 *            136 bytes).
 * Exports:   data_ready_callback.
 * Imports:   StRingAddr, StRingIdx1, StRingIdx2, StFunc1, StFinalSector (COMMON variables of the stream layer).
 * Own data:  .data 0x800748e0 g_748e0, 16 bytes: the four CD register pointers (CDREG0..CDREG3) that every C_0xx
 *            module carries.
 *
 * Difference from the nearest archive: Runtime 3.3 / 3.4 C_004 (104 bytes, data_ready_callback only). Retail
 * has 34 words where the archive has 26: 24 of them equal the archive's in order; retail adds the
 * final two register clears (CDREG0 = 0, CDREG3 = 0 through g_748e0) at 0x800614cc..0x800614e8. 3.5 and later
 * C_004 also export StGetBackloc, which retail does not contain.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.data
/*
 * g_748e0 - the CD register pointers carried by module C_004. data_ready_callback uses the first (CDREG0) and the
 * last (CDREG3). Static.
 */
g_748e0:
	.word	0x1f801800	/* CDREG0 0x1f801800 */
	.word	0x1f801801	/* CDREG1 0x1f801801 (unused) */
	.word	0x1f801802	/* CDREG2 0x1f801802 (unused) */
	.word	0x1f801803	/* CDREG3 0x1f801803 */

	.text

/*
 * data_ready_callback - a frame sector DMA has finished
 * Purpose:   void data_ready_callback(void). Installed with CdDataCallback (DMA3 interrupt). Marks the ring entry
 *            StRingIdx2 as complete (status 2), advances StRingIdx2 to StRingIdx1, clears StFinalSector, calls the
 *            user's frame callback StFunc1 when set, then clears the CD index and request registers.
 * Inputs:    none.   Outputs: none.   Clobbers: v0, v1. Frame 24 bytes (ra only).
 *            The two leading loads (StRingIdx2, StRingAddr) are scheduled before the prologue, so the routine
 *            starts at 0x80061474.
 * Callers:   the DMA interrupt (DMACallback 3); StCdInterrupt calls it directly in emulation mode.
 * Callees:   StFunc1 (user callback, through jalr).
 */
	.globl	data_ready_callback
data_ready_callback:

	/* Entry (loads scheduled ahead of the prologue): v0 = StRingIdx2, v1 = StRingAddr */
	lw	v0, StRingIdx2	/* v0 = StRingIdx2 (word) */
	lw	v1, StRingAddr	/* v1 = StRingAddr (word) */
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */

	/* ring[StRingIdx2].status = 2 (frame complete) */
	ori	v1, zero, 0x2	/* v1 = 2 */
	sh	v1, 0(v0)	/* *(u16 *)(v0 + 0) = v1 */

	/* StRingIdx2 = StRingIdx1; StFinalSector = 0 (no last-sector DMA pending) */
	lw	v0, StRingIdx1	/* v0 = StRingIdx1 (word) */
	lw	v1, StFunc1	/* v1 = StFunc1 (word) */
	sw	zero, StFinalSector	/* StFinalSector = zero */
	sw	v0, StRingIdx2	/* StRingIdx2 = v0 */

	/* Frame callback StFunc1, if installed */
	beq	v1, zero, $L614cc	/* if (v1 == 0) goto $L614cc */
	nop	/* delay slot: nothing */
	jalr	ra, v1
	nop	/* delay slot: nothing */

	/* CDREG0 = 0 (bank 0) and CDREG3 = 0 (clear the data request) */
$L614cc:
	lw	v0, g_748e0	/* v0 = g_748e0 (word) */
	nop	/* load delay slot: nothing */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	lw	v0, g_748e0+12	/* v0 = g_748e0+12 (word) */
	nop	/* load delay slot: nothing */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
