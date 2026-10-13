/*
 * libcd_cdread2.s - LIBCD module CDREAD2: CdRead2, start a streaming read.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library module that retail
 * links as a rebuild matching no archive in ../sdk. Assembled by tools/build_asm.py (CPPPSX, then ASPSX 2.34);
 * placed by config/libcd_cdread2.json and the "libcd_cdread2" entry of config/data.json; counted as "library
 * assembly", never as matched C and never as certified library.
 *
 * Library:   LIBCD.LIB (Sony Runtime Library, CD-ROM).   Module: CDREAD2 (retail 0x8005fa64..0x8005fad7, 116 bytes).
 * Exports:   CdRead2.
 * Imports:   CdControl, CdDataCallback, CdReadyCallback (SYS), data_ready_callback (C_004), StCdInterrupt (C_011).
 * Own data:  none (the archived module has 43 bytes of .rdata for a debug message; retail has none).
 *
 * Difference from the nearest archive: Runtime 3.4 / 3.5 / 3.6 CDREAD2 (148-180 bytes). 26 of the 29 retail words
 * match in order; the archived routine also tests mode bit 0x20 to set the StMode variable and prints a message,
 * which retail does not, and the callbacks are installed only for mode bit 0x100 (stream).
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * CdRead2 - start reading the disc with CdlReadS (for streaming)
 * Purpose:   int CdRead2(long mode): CdControl(CdlSetmode, &mode_byte, NULL); if (mode & 0x100) (CdlModeStream)
 *            the stream callbacks are installed: CdDataCallback(data_ready_callback) and
 *            CdReadyCallback(StCdInterrupt); then CdControl(CdlReadS, NULL, NULL). Returns CdControl's result.
 * Inputs:    a0 = mode (low byte sent to the drive, bit 8 selects streaming).
 * Outputs:   v0 = result of the final CdControl (1 started, 0 failed).
 * Registers: s0 = mode; the mode byte is stored at 16(sp). Frame 32 bytes.
 * Callers:   game code (the CD streaming start).
 * Callees:   CdControl, CdDataCallback, CdReadyCallback.
 */
	.globl	CdRead2
CdRead2:
	addiu	sp, sp, -32	/* allocate 32 byte frame */
	sw	s0, 24(sp)	/* save s0 */
	addu	s0, a0, zero	/* s0 = a0 */

	/* CdControl(CdlSetmode = 0x0e, &mode_byte, NULL): the byte at 16(sp) is stored in the delay slot of the jal */
	ori	a0, zero, 0xe	/* a0 = 14 */
	addiu	a1, sp, 16	/* a1 = sp + 16 */
	addu	a2, zero, zero	/* a2 = zero */
	sw	ra, 28(sp)	/* save ra */
	jal	CdControl	/* call CdControl */
	sb	s0, 16(sp)	/* [delay slot, real work] save s0 */

	/* Streaming (mode & 0x100)? Otherwise skip to the read command with a0 = 0x1b (CdlReadS, set in the delay slot) */
	andi	s0, s0, 0x100	/* s0 = s0 & 0x100 */
	beq	s0, zero, $L5fab8	/* if (s0 == 0) goto $L5fab8 */
	ori	a0, zero, 0x1b	/* [delay slot, real work] a0 = 27 */

	/* Install the DMA-complete callback (the address of data_ready_callback is built with lui/addiu) and the ready callback StCdInterrupt */
	la	a0, data_ready_callback		/* a0 = &data_ready_callback */
	jal	CdDataCallback	/* call CdDataCallback */
	nop	/* delay slot: nothing */
	la	a0, StCdInterrupt	/* a0 = &StCdInterrupt */
	jal	CdReadyCallback	/* call CdReadyCallback */
	nop	/* delay slot: nothing */

	/* CdControl(CdlReadS = 0x1b, NULL, NULL) and return its result */
	ori	a0, zero, 0x1b	/* a0 = 27 */
$L5fab8:
	addu	a1, zero, zero	/* a1 = zero */
	jal	CdControl	/* call CdControl */
	addu	a2, zero, zero	/* [delay slot, real work] a2 = zero */
	lw	ra, 28(sp)	/* restore ra */
	lw	s0, 24(sp)	/* restore s0 */
	addiu	sp, sp, 32	/* release 32 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
