/*
 * libcd_c_009.s - LIBCD module C_009: StGetNext, fetch the next complete stream frame from the ring.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library module that retail
 * links as a rebuild matching no archive in ../sdk. Assembled by tools/build_asm.py (CPPPSX, then ASPSX 2.34);
 * placed by config/libcd_c_009.json and the "libcd_c_009" entry of config/data.json; counted as "library
 * assembly", never as matched C and never as certified library.
 *
 * Library:   LIBCD.LIB (Sony Runtime Library, CD-ROM streaming).   Module: C_009 (retail 0x800613c0..0x80061473,
 *            180 bytes).
 * Exports:   StGetNext.
 * Imports:   the stream COMMON variables StRingAddr (0x800a0f9c), StRingSize (0x800a18d4) and StRingIdx3
 *            (0x8009eed4).
 * Own data:  .data 0x800748d0 g_748d0, 16 bytes: four CD register pointers (CDREG0..CDREG3) that every C_0xx
 *            module carries; StGetNext does not read them.
 *
 * Difference from the nearest archive: Runtime 3.3 / 3.4 / 3.5 / 3.6 C_009 (196 bytes). 44 of the 45 retail
 * words are identical in order after relocation masking; retail lacks 5 words (the archived routine has an extra
 * load/test pair at archive offsets 0x30..0x44 before it returns "none"), and the branch at 0x800613e8 is
 * re-targeted accordingly. The module's .data is identical (16 bytes).
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.data
/*
 * g_748d0 - the CD register pointers carried by module C_009 (CDREG0..CDREG3); unused by StGetNext. Static.
 */
g_748d0:
	.word	0x1f801800	/* CDREG0 0x1f801800 */
	.word	0x1f801801	/* CDREG1 0x1f801801 */
	.word	0x1f801802	/* CDREG2 0x1f801802 */
	.word	0x1f801803	/* CDREG3 0x1f801803 */

	.text

/*
 * StGetNext - hand the next complete frame of the stream ring to the caller
 * Purpose:   int StGetNext(u_long **addr, u_long **header). The ring entry for StRingIdx3 is looked at:
 *            status 1 (wrap marker) resets StRingIdx3 to 0, clears the marker and moves to entry 0;
 *            status 2 (frame complete) becomes 4 (taken) and the frame is returned.
 * Inputs:    a0 = where to store the frame address, a1 = where to store the 32-byte header (ring entry).
 * Outputs:   v0 = 0 and *addr = StRingAddr + StRingSize * 32 + StRingIdx3 * 2016, *header = the entry;
 *            v0 = 1 when no complete frame is available.
 * Registers: a3 = addr, t0 = header, a2 = ring entry. Leaf, no frame.
 * Callers:   game code (StGetNext is the public name); the entry is released by StFreeRing (C_007).
 */
	.globl	StGetNext
StGetNext:
	addu	a3, a0, zero	/* a3 = a0 */

	/* Ring entry for StRingIdx3: a2 = StRingAddr + StRingIdx3 * 32 */
	lw	v0, StRingIdx3	/* v0 = StRingIdx3 (word) */
	lw	v1, StRingAddr	/* v1 = StRingAddr (word) */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	addu	a2, v0, v1	/* a2 = v0 + v1 */

	/* Status 1 = wrap marker: StRingIdx3 = 0, clear the marker and recompute the entry (entry 0) */
	lhu	v0, 0(a2)	/* v0 = *(u16 *)(a2 + 0) */
	ori	v1, zero, 0x1	/* v1 = 1 */
	andi	v0, v0, 0xffff	/* v0 = v0 & 0xffff */
	bne	v0, v1, $L61414	/* if (v0 != v1) goto $L61414 */
	addu	t0, a1, zero	/* [delay slot, real work] t0 = a1 */
	sw	zero, StRingIdx3	/* StRingIdx3 = zero */
	sh	zero, 0(a2)	/* *(u16 *)(a2 + 0) = zero */
	lw	v0, StRingIdx3	/* v0 = StRingIdx3 (word) */
	lw	v1, StRingAddr	/* v1 = StRingAddr (word) */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	addu	a2, v0, v1	/* a2 = v0 + v1 */

	/* Status 2 = frame complete? */
$L61414:
	lhu	v0, 0(a2)	/* v0 = *(u16 *)(a2 + 0) */
	ori	v1, zero, 0x2	/* v1 = 2 */
	andi	v0, v0, 0xffff	/* v0 = v0 & 0xffff */
	bne	v0, v1, $L6146c	/* if (v0 != v1) goto $L6146c */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */

	/* Mark the entry taken (status 4), return 0 and the frame address / header pointer */
	ori	v0, zero, 0x4	/* v0 = 4 */
	sh	v0, 0(a2)	/* *(u16 *)(a2 + 0) = v0 */
	addu	v0, zero, zero	/* v0 = zero */

	/* Frame address = StRingAddr + StRingSize * 32 + StRingIdx3 * 0x7e0 (63 * 32 = 2016 bytes per frame slot) */
	lw	a0, StRingSize	/* a0 = StRingSize (word) */
	lw	v1, StRingAddr	/* v1 = StRingAddr (word) */
	lw	a1, StRingIdx3	/* a1 = StRingIdx3 (word) */
	sll	a0, a0, 5	/* a0 = a0 << 5 */
	addu	a0, a0, v1	/* a0 = a0 + v1 */
	sll	v1, a1, 6	/* v1 = a1 << 6 */
	subu	v1, v1, a1	/* v1 = v1 - a1 */
	sll	v1, v1, 5	/* v1 = v1 << 5 */
	addu	a0, a0, v1	/* a0 = a0 + v1 */
	sw	a0, 0(a3)	/* *(word *)(a3 + 0) = a0 */
	sw	a2, 0(t0)	/* *(word *)(t0 + 0) = a2 */

	/* No complete frame: return 1 */
$L6146c:
	jr	ra	/* return */
	nop	/* delay slot: nothing */
