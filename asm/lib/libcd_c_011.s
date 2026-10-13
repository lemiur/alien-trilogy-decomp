/*
 * libcd_c_011.s - LIBCD module C_011: the streaming (Str*) CD-ROM interrupt handler.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library module that retail
 * links as a rebuild matching no archive in ../sdk. Assembled by tools/build_asm.py (CPPPSX, then ASPSX 2.34);
 * placed by config/libcd_c_011.json and the "libcd_c_011" entry of config/data.json; counted as "library
 * assembly", never as matched C and never as certified library.
 *
 * Library:   LIBCD.LIB (Sony Runtime Library, CD-ROM streaming).   Module: C_011 (retail 0x800607b0..0x800612cf,
 *            2848 bytes).
 * Exports:   StCdInterrupt (text), debug_cause (data, 8 bytes). Two unexported helpers keep address names:
 *            f_610ec (copy words) and f_61120 (start a DMA channel).
 * Imports:   CdReady, data_ready_callback (C_004), init_ring_status (C_008), printf, and the COMMON variables
 *            of the stream layer (names from the 3.3 objects, addresses from the retail operands):
 *            StRingAddr 0x800a0f9c, StRingBase 0x800a0f98, StRingSize 0x800a18d4, StRingIdx1/2 0x8009eecc/ed0,
 *            StEmu_Addr 0x8009eee0, StEmu_Idx 0x8009d26c, StRgb24 0x80096be0, StCdIntrFlag 0x8009a120,
 *            StFinalSector 0x8009cae4, StSTART_FLAG 0x800a0f94, StStartFrame 0x8009a308, StEndFrame 0x8009eedc,
 *            Stframe_no 0x80096b98, Stsector_offset 0x80096b9c, StFunc2 0x8009a11c, CChannel 0x8009c6dc,
 *            StCHANNEL 0x8009a304; and the module's own 4-byte .bss g_91aec (0x80091aec, the ring entry being
 *            filled), which lies outside the library-assembly data ranges and stays an import.
 * Own data:  .rdata 0x80013104 "DMA STATUS ERROR %x" (20 bytes; the 2 bytes before it are alignment after
 *            TOC's .rdata); .data 0x80074840 g_74840 (72 bytes of hardware-register pointers) and
 *            debug_cause (0x80074888, 8 bytes).
 *
 * Difference from the nearest archive: Runtime 3.3/3.4/3.5 C_011. Relocation-masked word comparison:
 *   StCdInterrupt   502 of the 591 retail words occur in order in 3.5 (3.5 has 104 words that retail lacks:
 *                   its per-byte CD register read loops; 3.3 and 3.4 tie on the same count); retail's frame is
 *                   56 bytes where 3.5 uses 64. The retail routine reads the 32-byte sector header with
 *                   f_61120 or f_610ec only.
 *   f_610ec         identical to 3.3 .. 4.0
 *   f_61120         62 of 108 retail words match 3.3/3.4 (the DMA start sequence is shorter in retail)
 *   debug_cause     3.3 exports it as .data+72 too (an 8-byte word pair)
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.rdata
/*
 * Format of the DMA time-out report of f_61120 (compiler label).
 */
$LC0:
	.ascii	"DMA STATUS ERROR %x\n\000"
	.align	2

	.data
/*
 * g_74840 - pointers to the hardware registers used by the stream handler (18 words); each word is
 * commented with the register it points at. Static. Users: StCdInterrupt, f_61120.
 */
g_74840:
	.word	0x1f801800	/* CDREG0 0x1f801800 */
	.word	0x1f801801	/* CDREG1 0x1f801801 */
	.word	0x1f801802	/* CDREG2 0x1f801802 (unused) */
	.word	0x1f801803	/* CDREG3 0x1f801803 */
	.word	0x1f801018	/* CD-ROM delay/size 0x1f801018 */
	.word	0x1f801020	/* COM_DELAY 0x1f801020 */
	.word	0x1f8010f0	/* DPCR 0x1f8010f0 */
	.word	0x1f8010f4	/* DICR 0x1f8010f4 */
	.word	0x1f801098	/* DMA1 (MDEC out) CHCR 0x1f801098 (busy test in StCdInterrupt) */
	.word	0x1f801090	/* DMA1 MADR 0x1f801090 (unused) */
	.word	0x1f8010a8	/* DMA2 CHCR 0x1f8010a8 (unused) */
	.word	0x1f8010a0	/* DMA2 MADR 0x1f8010a0 (unused) */
	.word	0x1f8010b8	/* DMA3 (CD-ROM) CHCR 0x1f8010b8 */
	.word	0x1f8010b0	/* DMA3 MADR 0x1f8010b0 (unused) */
	.word	0x1f8010d8	/* DMA5 CHCR 0x1f8010d8 (unused) */
	.word	0x1f8010d0	/* DMA5 MADR 0x1f8010d0 (unused) */
	.word	0x1f801070	/* I_STAT 0x1f801070 (unused) */
	.word	0x1f801074	/* I_MASK 0x1f801074 (unused) */

/*
 * debug_cause - why StCdInterrupt last returned (word 0, see its header); word 1 is unused. Exported.
 */
	.globl	debug_cause
debug_cause:
	.word	0, 0

	.text

/*
 * StCdInterrupt - ready (sector) callback of the streaming layer
 * Purpose:   void StCdInterrupt(void). Installed with CdReadyCallback by CdRead2. For each sector that the
 *            controller announces it reads the 32-byte sector header into the next ring entry, validates
 *            it (id 0x0160, channel, sector and frame sequence), then starts the DMA of the 504 payload words
 *            into the ring frame buffer. The reason for leaving is recorded in debug_cause:
 *            1 MDEC DMA still busy, 3 seek-error status, 4 ring slot busy, 5 wrong id/channel, 6 sequence error,
 *            7 end frame reached, 8 ring full (no end frame), 9 ring full (end frame set), 0x0a sector accepted
 *            (these codes are read from the stores to debug_cause; 2 is not produced by this version).
 * Ring:      StRingAddr points at StRingSize 32-byte entries (the header copy; first halfword = frame status:
 *            0 free, 1 wrap marker, 2 complete frame, 3 filling, 4 taken by StGetNext), followed by the frame data
 *            area of 2016-byte (0x7e0) slots; StRingIdx1 = slot being filled, StRingIdx2 = oldest unfinished.
 * Header:    ring entry +0 halfword 0x0160 (video sector id), +2 bits 10..14 channel, +4 sector number inside the
 *            frame, +6 sectors per frame, +8 frame number (as read by the code below).
 * Inputs:    none (all state in the COMMON variables).   Clobbers: v0, v1, a0-a3, t0, s-registers untouched.
 * Frame:     56 bytes (header copy at 0x20..0x29(sp), CdReady result at 0x28(sp)).
 * Callers:   the CD interrupt layer (f_5f708 via CD_cbready); CdRead2 installs it.
 * Callees:   CdReady, f_610ec, f_61120, init_ring_status, data_ready_callback (emulation mode), StFunc2.
 */
	.globl	StCdInterrupt
StCdInterrupt:

	/* Leave at once when the last sector of the previous frame is still being DMAed (StFinalSector == 1) */
	addiu	sp, sp, -56	/* allocate 56 byte frame */
	lw	v0, StFinalSector	/* v0 = StFinalSector (word) */
	ori	a0, zero, 0x1	/* a0 = 1 */
	beq	v0, a0, $L610dc	/* if (v0 == a0) goto $L610dc */
	sw	ra, 48(sp)	/* [delay slot, real work] save ra */

	/* Two scratch halfwords preset to 0x5555 (a0 stays 1 for the stores below) */
	ori	v0, zero, 0x5555	/* v0 = 21845 */
	sh	v0, 36(sp)	/* spill v0 to sp+36 */
	lhu	v0, 36(sp)	/* reload v0 from sp+36 */

	/* RGB24 mode (StRgb24 != 0): if the MDEC-out DMA (channel 1, CHCR at g_74840+32) is still busy the sector is not taken */
	lw	v1, StRgb24	/* v1 = StRgb24 (word) */
	sh	v0, 34(sp)	/* spill v0 to sp+34 */
	beq	v1, zero, $L60848	/* if (v1 == 0) goto $L60848 */
	nop	/* delay slot: nothing */
	lw	v0, g_74840+32	/* v0 = g_74840+32 (word) */
	nop	/* load delay slot: nothing */
	lw	v0, 0(v0)	/* v0 = *(word *)(v0 + 0) */
	lui	v1, 0x100	/* v1 = 0x1000000 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	beq	v0, zero, $L60848	/* if (v0 == 0) goto $L60848 */
	nop	/* delay slot: nothing */

	/* Note the overrun: StCdIntrFlag = 1, count a skipped sector in StEmu_Idx when emulating; debug_cause = 1 */
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	sw	a0, StCdIntrFlag	/* StCdIntrFlag = a0 */
	beq	v0, zero, $L60838	/* if (v0 == 0) goto $L60838 */
	nop	/* delay slot: nothing */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
$L60838:
	sw	a0, debug_cause	/* debug_cause = a0 */
	j	$L610dc	/* goto $L610dc */
	nop	/* delay slot: nothing */

	/* Fetch the response of the pending interrupt: CdReady(1, result) (poll once); a DiskError (5) ends the handler */
$L60848:
	ori	a0, zero, 0x1	/* a0 = 1 */
	jal	CdReady	/* call CdReady */
	addiu	a1, sp, 40	/* [delay slot, real work] a1 = sp + 40 */
	ori	v1, zero, 0x5	/* v1 = 5 */
	beq	v0, v1, $L610dc	/* if (v0 == v1) goto $L610dc */
	nop	/* delay slot: nothing */

	/* Keep the first two response bytes and the CDREG0 status byte in the stack frame */
	lbu	v0, 40(sp)	/* reload v0 from sp+40 */
	lbu	v1, 41(sp)	/* reload v1 from sp+41 */
	lw	a1, g_74840	/* a1 = g_74840 (word) */
	sh	v0, 34(sp)	/* spill v0 to sp+34 */
	sh	v1, 36(sp)	/* spill v1 to sp+36 */
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0xff	/* v0 = v0 & 0xff */
	sh	v0, 32(sp)	/* spill v0 to sp+32 */
	lhu	v0, 34(sp)	/* reload v0 from sp+34 */

	/* Response status bit 2 set (seek error): read one response byte and leave with debug_cause = 3 */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x4	/* v0 = v0 & 0x4 */
	beq	v0, zero, $L608c8	/* if (v0 == 0) goto $L608c8 */
	nop	/* delay slot: nothing */
	lw	v0, g_74840+4	/* v0 = g_74840+4 (word) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	ori	v1, zero, 0x3	/* v1 = 3 */
	sw	v1, debug_cause	/* debug_cause = v1 */
	andi	v0, v0, 0xff	/* v0 = v0 & 0xff */
	sh	v0, 36(sp)	/* spill v0 to sp+36 */
	j	$L610dc	/* goto $L610dc */
	nop	/* delay slot: nothing */

	/* Next ring entry = StRingAddr + StRingIdx1 * 32, remembered in g_91aec */
$L608c8:
	lw	v0, StRingIdx1	/* v0 = StRingIdx1 (word) */
	lw	v1, StRingAddr	/* v1 = StRingAddr (word) */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, g_91aec	/* g_91aec = v0 */

	/* Entry still in use (status != 0): leave with debug_cause = 4 (counting a skipped sector when emulating) */
	lhu	v0, 0(v0)	/* v0 = *(u16 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60938	/* if (v0 == 0) goto $L60938 */
	lui	a0, 0x2	/* [delay slot, real work] a0 = 0x20000 */
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60928	/* if (v0 == 0) goto $L60928 */
	ori	v0, zero, 0x4	/* [delay slot, real work] v0 = 4 */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
	ori	v0, zero, 0x4	/* v0 = 4 */
$L60928:
	sw	v0, debug_cause	/* debug_cause = v0 */
	j	$L610dc	/* goto $L610dc */
	nop	/* delay slot: nothing */

	/* Entry free: bank 0, request the data FIFO (CDREG3 = 0x80), CD-ROM delay 0x20943, COM_DELAY 0x1323 */
$L60938:
	sb	zero, 0(a1)	/* *(u8 *)(a1 + 0) = zero */
	lw	v1, g_74840+12	/* v1 = g_74840+12 (word) */
	ori	v0, zero, 0x80	/* v0 = 128 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v0, g_74840+16	/* v0 = g_74840+16 (word) */
	ori	a0, a0, 0x943	/* a0 = a0 | 0x943 */
	sw	a0, 0(v0)	/* *(word *)(v0 + 0) = a0 */
	lw	v1, g_74840+20	/* v1 = g_74840+20 (word) */
	ori	v0, zero, 0x1323	/* v0 = 4899 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */

	/* Read the 32-byte sector header (8 words) into the entry: from the emulation buffer with f_610ec, or by DMA3 through f_61120(3, entry, 0, 8, CHCR, 0, 0) */
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L609ac	/* if (v0 == 0) goto $L609ac */
	lui	t0, 0x1100	/* [delay slot, real work] t0 = 0x11000000 */
	ori	a2, zero, 0x8	/* a2 = 8 */
	addu	a3, zero, zero	/* a3 = zero */
	lw	a1, StEmu_Idx	/* a1 = StEmu_Idx (word) */
	lw	a0, g_91aec	/* a0 = g_91aec (word) */
	sll	a1, a1, 11	/* a1 = a1 << 11 */
	jal	f_610ec	/* call f_610ec */
	addu	a1, a1, v0	/* [delay slot, real work] a1 = a1 + v0 */
	j	$L609d0	/* goto $L609d0 */
	nop	/* delay slot: nothing */
$L609ac:
	ori	a0, zero, 0x3	/* a0 = 3 */
	addu	a2, zero, zero	/* a2 = zero */
	lw	a1, g_91aec	/* a1 = g_91aec (word) */
	ori	a3, zero, 0x8	/* a3 = 8 */
	sw	t0, 16(sp)	/* spill t0 to sp+16 */
	sw	zero, 20(sp)	/* spill zero to sp+20 */
	jal	f_61120	/* call f_61120 */
	sw	zero, 24(sp)	/* [delay slot, real work] spill zero to sp+24 */

	/* Busy-wait until DMA3 (CHCR bit 24) is idle, then restore the timing registers (0x20843, COM_DELAY 0x1325) */
$L609d0:
	lw	a0, g_74840+48	/* a0 = g_74840+48 (word) */
	nop	/* load delay slot: nothing */
	lw	v0, 0(a0)	/* v0 = *(word *)(a0 + 0) */
	lui	v1, 0x100	/* v1 = 0x1000000 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	beq	v0, zero, $L60a08	/* if (v0 == 0) goto $L60a08 */
	addu	v1, a0, zero	/* [delay slot, real work] v1 = a0 */
	lui	a0, 0x100	/* a0 = 0x1000000 */
$L609f4:
	lw	v0, 0(v1)	/* v0 = *(word *)(v1 + 0) */
	nop	/* load delay slot: nothing */
	and	v0, v0, a0	/* v0 = v0 & a0 */
	bne	v0, zero, $L609f4	/* if (v0 != 0) goto $L609f4 */
	nop	/* delay slot: nothing */
$L60a08:
	lui	v1, 0x2	/* v1 = 0x20000 */
	lw	v0, g_74840+16	/* v0 = g_74840+16 (word) */
	ori	v1, v1, 0x843	/* v1 = v1 | 0x843 */
	sw	v1, 0(v0)	/* *(word *)(v0 + 0) = v1 */
	lw	v1, g_74840+20	/* v1 = g_74840+20 (word) */
	ori	v0, zero, 0x1325	/* v0 = 4901 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */

	/* Waiting for the start frame (StSTART_FLAG == 1 and StStartFrame != 0): accept the sector when its frame number matches, otherwise drop it */
	lw	v1, StSTART_FLAG	/* v1 = StSTART_FLAG (word) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	bne	v1, v0, $L60ac0	/* if (v1 != v0) goto $L60ac0 */
	nop	/* delay slot: nothing */
	lw	a0, StStartFrame	/* a0 = StStartFrame (word) */
	nop	/* load delay slot: nothing */
	beq	a0, zero, $L60ac0	/* if (a0 == 0) goto $L60ac0 */
	nop	/* delay slot: nothing */
	lw	v1, g_91aec	/* v1 = g_91aec (word) */
	nop	/* load delay slot: nothing */
	lhu	v0, 8(v1)	/* v0 = *(u16 *)(v1 + 8) */
	nop	/* load delay slot: nothing */
	bne	a0, v0, $L60a80	/* if (a0 != v0) goto $L60a80 */
	nop	/* delay slot: nothing */
	sw	zero, StSTART_FLAG	/* StSTART_FLAG = zero */
	j	$L60ac0	/* goto $L60ac0 */
	nop	/* delay slot: nothing */

	/* Dropped: entry status = 0, count a skipped sector, clear CDREG0 and CDREG3 and leave */
$L60a80:
	sh	zero, 0(v1)	/* *(u16 *)(v1 + 0) = zero */
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60ab0	/* if (v0 == 0) goto $L60ab0 */
	nop	/* delay slot: nothing */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
$L60ab0:
	lw	v0, g_74840	/* v0 = g_74840 (word) */
	j	$L60dfc	/* goto $L60dfc */
	nop	/* delay slot: nothing */

	/* Validate the header: id must be 0x0160 and the channel (bits 10..14 of +2) must equal CChannel; otherwise drop it with debug_cause = 5 */
$L60ac0:
	lw	a0, g_91aec	/* a0 = g_91aec (word) */
	nop	/* load delay slot: nothing */
	lhu	v0, 0(a0)	/* v0 = *(u16 *)(a0 + 0) */
	ori	v1, zero, 0x160	/* v1 = 352 */
	andi	v0, v0, 0xffff	/* v0 = v0 & 0xffff */
	bne	v0, v1, $L60afc	/* if (v0 != v1) goto $L60afc */
	nop	/* delay slot: nothing */
	lhu	v0, 2(a0)	/* v0 = *(u16 *)(a0 + 2) */
	lw	v1, CChannel	/* v1 = CChannel (word) */
	srl	v0, v0, 10	/* v0 = v0 >> 10 */
	andi	v0, v0, 0x1f	/* v0 = v0 & 0x1f */
	beq	v0, v1, $L60b4c	/* if (v0 == v1) goto $L60b4c */
	nop	/* delay slot: nothing */
$L60afc:
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60b20	/* if (v0 == 0) goto $L60b20 */
	nop	/* delay slot: nothing */
	sw	zero, StEmu_Idx	/* StEmu_Idx = zero */
	j	$L60b24	/* goto $L60b24 */
	nop	/* delay slot: nothing */
$L60b20:
	lhu	v0, 0(a0)	/* v0 = *(u16 *)(a0 + 0) */
$L60b24:
	lw	v1, g_91aec	/* v1 = g_91aec (word) */
	ori	v0, zero, 0x5	/* v0 = 5 */
	sw	v0, debug_cause	/* debug_cause = v0 */
	sh	zero, 0(v1)	/* *(u16 *)(v1 + 0) = zero */
	lw	v0, g_74840	/* v0 = g_74840 (word) */
	j	$L60dfc	/* goto $L60dfc */
	nop	/* delay slot: nothing */

	/* Sequence check: +4 must equal Stsector_offset and (when Stframe_no != 0) +8 must equal Stframe_no */
$L60b4c:
	lh	v1, Stsector_offset	/* v1 = Stsector_offset (s16) */
	lhu	v0, 4(a0)	/* v0 = *(u16 *)(a0 + 4) */
	nop	/* load delay slot: nothing */
	bne	v1, v0, $L60b88	/* if (v1 != v0) goto $L60b88 */
	nop	/* delay slot: nothing */
	lw	v1, Stframe_no	/* v1 = Stframe_no (word) */
	nop	/* load delay slot: nothing */
	beq	v1, zero, $L60c08	/* if (v1 == 0) goto $L60c08 */
	nop	/* delay slot: nothing */
	lhu	v0, 8(a0)	/* v0 = *(u16 *)(a0 + 8) */
	nop	/* load delay slot: nothing */
	beq	v1, v0, $L60c08	/* if (v1 == v0) goto $L60c08 */
	nop	/* delay slot: nothing */

	/* Out of sequence: forget the frame (Stframe_no = Stsector_offset = 0), init_ring_status(StRingIdx2, StRingIdx1 - StRingIdx2), StRingIdx1 = StRingIdx2, free the entry, debug_cause = 6 */
$L60b88:
	lw	a0, StRingIdx2	/* a0 = StRingIdx2 (word) */
	lw	a1, StRingIdx1	/* a1 = StRingIdx1 (word) */
	sw	zero, Stframe_no	/* Stframe_no = zero */
	sh	zero, Stsector_offset	/* Stsector_offset <- zero */
	jal	init_ring_status	/* call init_ring_status */
	subu	a1, a1, a0	/* [delay slot, real work] a1 = a1 - a0 */
	lw	v0, StRingIdx2	/* v0 = StRingIdx2 (word) */
	lw	v1, g_91aec	/* v1 = g_91aec (word) */
	sw	v0, StRingIdx1	/* StRingIdx1 = v0 */
	sh	zero, 0(v1)	/* *(u16 *)(v1 + 0) = zero */
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60bf8	/* if (v0 == 0) goto $L60bf8 */
	ori	v1, zero, 0x6	/* [delay slot, real work] v1 = 6 */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
$L60bf8:
	lw	v0, g_74840	/* v0 = g_74840 (word) */
	j	$L60df4	/* goto $L60df4 */
	nop	/* delay slot: nothing */

	/* Sector accepted. Not the first sector of a frame (+4 != 0): go straight to the transfer set-up */
$L60c08:
	lw	v1, g_91aec	/* v1 = g_91aec (word) */
	nop	/* load delay slot: nothing */
	lhu	v0, 4(v1)	/* v0 = *(u16 *)(v1 + 4) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L60e80	/* if (v0 != 0) goto $L60e80 */
	ori	v0, zero, 0xa	/* [delay slot, real work] v0 = 10 */

	/* First sector: Stsector_offset = 0, Stframe_no = frame number; frame past StEndFrame ends the stream (debug_cause = 7) */
	lhu	v0, 8(v1)	/* v0 = *(u16 *)(v1 + 8) */
	lw	v1, StEndFrame	/* v1 = StEndFrame (word) */
	sh	zero, Stsector_offset	/* Stsector_offset <- zero */
	andi	v0, v0, 0xffff	/* v0 = v0 & 0xffff */
	sw	v0, Stframe_no	/* Stframe_no = v0 */
	beq	v1, zero, $L60cf8	/* if (v1 == 0) goto $L60cf8 */
	sltu	v0, v0, v1	/* [delay slot, real work] v0 = v0 < v1 */
	bne	v0, zero, $L60cf8	/* if (v0 != 0) goto $L60cf8 */
	nop	/* delay slot: nothing */
	lw	a0, StRingIdx2	/* a0 = StRingIdx2 (word) */
	lw	a1, StRingIdx1	/* a1 = StRingIdx1 (word) */
	sw	zero, Stframe_no	/* Stframe_no = zero */
	sh	zero, Stsector_offset	/* Stsector_offset <- zero */
	jal	init_ring_status	/* call init_ring_status */
	subu	a1, a1, a0	/* [delay slot, real work] a1 = a1 - a0 */
	lw	v0, StRingIdx2	/* v0 = StRingIdx2 (word) */
	lw	v1, g_91aec	/* v1 = g_91aec (word) */
	sw	v0, StRingIdx1	/* StRingIdx1 = v0 */
	sh	zero, 0(v1)	/* *(u16 *)(v1 + 0) = zero */
	lw	v1, StFunc2	/* v1 = StFunc2 (word) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sw	v0, StSTART_FLAG	/* StSTART_FLAG = v0 */
	beq	v1, zero, $L60cbc	/* if (v1 == 0) goto $L60cbc */
	nop	/* delay slot: nothing */
	jalr	ra, v1
	nop	/* delay slot: nothing */
$L60cbc:
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60ce8	/* if (v0 == 0) goto $L60ce8 */
	ori	v1, zero, 0x7	/* [delay slot, real work] v1 = 7 */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
$L60ce8:
	lw	v0, g_74840	/* v0 = g_74840 (word) */
	j	$L60df4	/* goto $L60df4 */
	nop	/* delay slot: nothing */

	/* Room for the whole frame? (StRingSize - StRingIdx1 - 1 >= sectors per frame); if not the ring is full (debug_cause = 8 / 9) */
$L60cf8:
	lw	v0, StRingSize	/* v0 = StRingSize (word) */
	lw	v1, StRingIdx1	/* v1 = StRingIdx1 (word) */
	lw	a0, g_91aec	/* a0 = g_91aec (word) */
	subu	v0, v0, v1	/* v0 = v0 - v1 */
	lhu	v1, 6(a0)	/* v1 = *(u16 *)(a0 + 6) */
	addiu	v0, v0, -1	/* v0 = v0 - 1 */
	sltu	v0, v0, v1	/* v0 = v0 < v1 */
	beq	v0, zero, $L60e68	/* if (v0 == 0) goto $L60e68 */
	nop	/* delay slot: nothing */
	lw	v0, StEndFrame	/* v0 = StEndFrame (word) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L60da0	/* if (v0 != 0) goto $L60da0 */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */
	sh	v0, 0(a0)	/* *(u16 *)(a0 + 0) = v0 */
	lw	v1, StFunc2	/* v1 = StFunc2 (word) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sw	v0, StSTART_FLAG	/* StSTART_FLAG = v0 */
	beq	v1, zero, $L60d64	/* if (v1 == 0) goto $L60d64 */
	nop	/* delay slot: nothing */
	jalr	ra, v1
	nop	/* delay slot: nothing */
$L60d64:
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60d90	/* if (v0 == 0) goto $L60d90 */
	ori	v1, zero, 0x8	/* [delay slot, real work] v1 = 8 */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
$L60d90:
	lw	v0, g_74840	/* v0 = g_74840 (word) */
	j	$L60df4	/* goto $L60df4 */
	nop	/* delay slot: nothing */
$L60da0:
	lw	v0, StRingAddr	/* v0 = StRingAddr (word) */
	nop	/* load delay slot: nothing */
	lh	v0, 0(v0)	/* v0 = *(s16 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60e18	/* if (v0 == 0) goto $L60e18 */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */
	sh	zero, 0(a0)	/* *(u16 *)(a0 + 0) = zero */
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60dec	/* if (v0 == 0) goto $L60dec */
	ori	v1, zero, 0x9	/* [delay slot, real work] v1 = 9 */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
$L60dec:
	lw	v0, g_74840	/* v0 = g_74840 (word) */
$L60df4:
	sw	v1, debug_cause	/* debug_cause = v1 */
$L60dfc:
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	lw	v0, g_74840+12	/* v0 = g_74840+12 (word) */
	nop	/* load delay slot: nothing */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	j	$L610dc	/* goto $L610dc */
	nop	/* delay slot: nothing */

	/* Ring wrapped: restart at slot 0 by copying the header to the first ring entry */
$L60e18:
	sh	v0, 0(a0)	/* *(u16 *)(a0 + 0) = v0 */
	lw	a1, StRingAddr	/* a1 = StRingAddr (word) */
	lw	a0, g_91aec	/* a0 = g_91aec (word) */
	addu	v1, zero, zero	/* v1 = zero */
	sw	zero, StRingIdx1	/* StRingIdx1 = zero */
$L60e38:
	lw	v0, 0(a0)	/* v0 = *(word *)(a0 + 0) */
	addiu	a0, a0, 4	/* a0 = a0 + 4 */
	addiu	v1, v1, 1	/* v1 = v1 + 1 */
	sw	v0, 0(a1)	/* *(word *)(a1 + 0) = v0 */
	sltiu	v0, v1, 8	/* v0 = v1 < 8 */
	bne	v0, zero, $L60e38	/* if (v0 != 0) goto $L60e38 */
	addiu	a1, a1, 4	/* [delay slot, real work] a1 = a1 + 4 */
	lw	v0, StRingAddr	/* v0 = StRingAddr (word) */
	nop	/* load delay slot: nothing */
	sw	v0, g_91aec	/* g_91aec = v0 */

	/* Transfer set-up: StRingIdx2 = StRingIdx1, debug_cause = 0x0a, count the sector, StRingBase = frame area + StRingIdx1 * 0x7e0 */
$L60e68:
	lw	v0, StRingIdx1	/* v0 = StRingIdx1 (word) */
	nop	/* load delay slot: nothing */
	sw	v0, StRingIdx2	/* StRingIdx2 = v0 */
	ori	v0, zero, 0xa	/* v0 = 10 */
$L60e80:
	sw	v0, debug_cause	/* debug_cause = v0 */
	lhu	v0, Stsector_offset	/* v0 = Stsector_offset (u16) */
	lw	v1, StRingSize	/* v1 = StRingSize (word) */
	lw	a0, StRingIdx1	/* a0 = StRingIdx1 (word) */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sh	v0, Stsector_offset	/* Stsector_offset <- v0 */
	lw	v0, StRingAddr	/* v0 = StRingAddr (word) */
	sll	v1, v1, 5	/* v1 = v1 << 5 */
	addu	v1, v1, v0	/* v1 = v1 + v0 */
	sll	v0, a0, 6	/* v0 = a0 << 6 */
	subu	v0, v0, a0	/* v0 = v0 - a0 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	lw	a0, StRgb24	/* a0 = StRgb24 (word) */
	addu	v1, v1, v0	/* v1 = v1 + v0 */
	sw	v1, StRingBase	/* StRingBase = v1 */
	beq	a0, zero, $L60f10	/* if (a0 == 0) goto $L60f10 */

	/* DMA3 and CD-ROM timing for the payload: RGB24 mode 0x20943/CHCR 0x11000000, otherwise 0x21020843/CHCR 0x11400100 */
	lui	t0, 0x1100	/* [delay slot, real work] t0 = 0x11000000 */
	lui	v1, 0x2	/* v1 = 0x20000 */
	lw	v0, g_74840+16	/* v0 = g_74840+16 (word) */
	ori	v1, v1, 0x943	/* v1 = v1 | 0x943 */
	sw	v1, 0(v0)	/* *(word *)(v0 + 0) = v1 */
	lw	v1, g_74840+20	/* v1 = g_74840+20 (word) */
	ori	v0, zero, 0x1323	/* v0 = 4899 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */
	j	$L60f2c	/* goto $L60f2c */
	nop	/* delay slot: nothing */
$L60f10:
	lui	v1, 0x2102	/* v1 = 0x21020000 */
	ori	v1, v1, 0x843	/* v1 = v1 | 0x843 */
	lui	t0, 0x1140	/* t0 = 0x11400000 */
	lw	v0, g_74840+16	/* v0 = g_74840+16 (word) */
	ori	t0, t0, 0x100	/* t0 = t0 | 0x100 */
	sw	v1, 0(v0)	/* *(word *)(v0 + 0) = v1 */

	/* Last sector of the frame (sectors per frame - 1 == sector number)? Then the payload transfer raises the DMA interrupt (f_61120 argument 1) and StFinalSector is set */
$L60f2c:
	lw	v0, g_91aec	/* v0 = g_91aec (word) */
	nop	/* load delay slot: nothing */
	lhu	v1, 6(v0)	/* v1 = *(u16 *)(v0 + 6) */
	lhu	v0, 4(v0)	/* v0 = *(u16 *)(v0 + 4) */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */
	bne	v1, v0, $L61000	/* if (v1 != v0) goto $L61000 */
	nop	/* delay slot: nothing */
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L60fa4	/* if (v0 == 0) goto $L60fa4 */
	ori	a2, zero, 0x1f8	/* [delay slot, real work] a2 = 504 */
	ori	a3, zero, 0x1	/* a3 = 1 */
	lw	a1, StEmu_Idx	/* a1 = StEmu_Idx (word) */
	lw	a0, StRingBase	/* a0 = StRingBase (word) */
	sll	a1, a1, 11	/* a1 = a1 << 11 */
	addu	a1, a1, v0	/* a1 = a1 + v0 */
	jal	f_610ec	/* call f_610ec */
	addiu	a1, a1, 32	/* [delay slot, real work] a1 = a1 + 32 */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
	j	$L60fcc	/* goto $L60fcc */
	nop	/* delay slot: nothing */
$L60fa4:
	ori	a0, zero, 0x3	/* a0 = 3 */
	addu	a2, zero, zero	/* a2 = zero */
	ori	a3, zero, 0x1f8	/* a3 = 504 */
	lw	a1, StRingBase	/* a1 = StRingBase (word) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sw	t0, 16(sp)	/* spill t0 to sp+16 */
	sw	v0, 20(sp)	/* spill v0 to sp+20 */
	jal	f_61120	/* call f_61120 */
	sw	zero, 24(sp)	/* [delay slot, real work] spill zero to sp+24 */
$L60fcc:
	lw	v1, StCHANNEL	/* v1 = StCHANNEL (word) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sh	zero, Stsector_offset	/* Stsector_offset <- zero */
	sw	zero, Stframe_no	/* Stframe_no = zero */
	sw	v0, StFinalSector	/* StFinalSector = v0 */
	sw	v1, CChannel	/* CChannel = v1 */
	j	$L6107c	/* goto $L6107c */
	nop	/* delay slot: nothing */

	/* Not the last sector: same transfer without the interrupt */
$L61000:
	lw	v0, StEmu_Addr	/* v0 = StEmu_Addr (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L61058	/* if (v0 == 0) goto $L61058 */
	ori	a2, zero, 0x1f8	/* [delay slot, real work] a2 = 504 */
	addu	a3, zero, zero	/* a3 = zero */
	lw	a1, StEmu_Idx	/* a1 = StEmu_Idx (word) */
	lw	a0, StRingBase	/* a0 = StRingBase (word) */
	sll	a1, a1, 11	/* a1 = a1 << 11 */
	addu	a1, a1, v0	/* a1 = a1 + v0 */
	jal	f_610ec	/* call f_610ec */
	addiu	a1, a1, 32	/* [delay slot, real work] a1 = a1 + 32 */
	lw	v0, StEmu_Idx	/* v0 = StEmu_Idx (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StEmu_Idx	/* StEmu_Idx = v0 */
	j	$L6107c	/* goto $L6107c */
	nop	/* delay slot: nothing */
$L61058:
	ori	a0, zero, 0x3	/* a0 = 3 */
	addu	a2, zero, zero	/* a2 = zero */
	lw	a1, StRingBase	/* a1 = StRingBase (word) */
	ori	a3, zero, 0x1f8	/* a3 = 504 */
	sw	t0, 16(sp)	/* spill t0 to sp+16 */
	sw	zero, 20(sp)	/* spill zero to sp+20 */
	jal	f_61120	/* call f_61120 */
	sw	zero, 24(sp)	/* [delay slot, real work] spill zero to sp+24 */

	/* Common tail: COM_DELAY 0x1325, entry status = 3 (filling), StRingIdx1++; in emulation mode with a DMA pending call data_ready_callback by hand */
$L6107c:
	lw	v1, g_74840+20	/* v1 = g_74840+20 (word) */
	ori	v0, zero, 0x1325	/* v0 = 4901 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */
	lw	v1, g_91aec	/* v1 = g_91aec (word) */
	ori	v0, zero, 0x3	/* v0 = 3 */
	sh	v0, 0(v1)	/* *(u16 *)(v1 + 0) = v0 */
	lw	v0, StRingIdx1	/* v0 = StRingIdx1 (word) */
	lw	v1, StEmu_Addr	/* v1 = StEmu_Addr (word) */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, StRingIdx1	/* StRingIdx1 = v0 */
	beq	v1, zero, $L610dc	/* if (v1 == 0) goto $L610dc */
	nop	/* delay slot: nothing */
	lw	v0, StFinalSector	/* v0 = StFinalSector (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L610dc	/* if (v0 == 0) goto $L610dc */
	nop	/* delay slot: nothing */
	jal	data_ready_callback	/* call data_ready_callback */
	nop	/* delay slot: nothing */

	/* Return */
$L610dc:
	lw	ra, 48(sp)	/* restore ra */
	addiu	sp, sp, 56	/* release 56 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * f_610ec - copy words
 * Purpose:   static void copy_words(u_long *dst, u_long *src, int n): n words from src to dst (loop at 0x800610f8).
 * Inputs:    a0 = dst, a1 = src, a2 = count.   Outputs: none.   Frame 8 bytes (unused).
 * Callers:   StCdInterrupt (emulation mode).
 */
	.globl	f_610ec
f_610ec:
	addiu	sp, sp, -8	/* allocate 8 byte frame */
	beq	a2, zero, $L61114	/* if (a2 == 0) goto $L61114 */
	addu	v1, zero, zero	/* [delay slot, real work] v1 = zero */
$L610f8:
	lw	v0, 0(a1)	/* v0 = *(word *)(a1 + 0) */
	addiu	a1, a1, 4	/* a1 = a1 + 4 */
	addiu	v1, v1, 1	/* v1 = v1 + 1 */
	sw	v0, 0(a0)	/* *(word *)(a0 + 0) = v0 */
	sltu	v0, v1, a2	/* v0 = v1 < a2 */
	bne	v0, zero, $L610f8	/* if (v0 != 0) goto $L610f8 */
	addiu	a0, a0, 4	/* [delay slot, real work] a0 = a0 + 4 */
$L61114:
	addiu	sp, sp, 8	/* release 8 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * f_61120 - start a DMA channel transfer
 * Purpose:   static void start_dma(int ch, u_long madr, int blocksize, int blocks, u_long chcr, int irq, int mask).
 *            Waits (up to 0x10000 polls) for the channel's CHCR busy bit; on time-out prints "DMA STATUS ERROR %x"
 *            with the CHCR value. With irq == 1 it sets the channel's interrupt-enable bit (16 + ch) in DICR
 *            (0x1f8010f4, pointer g_74840+28, also OR-ing mask << ch), otherwise clears that bit; the DICR flag
 *            bits (24 and up) are masked off in both cases. It sets the channel's master-enable bit in DPCR
 *            (0x1f8010f0, pointer g_74840+24); then writes MADR, BCR = (blocksize << 16) | blocks and CHCR.
 * Inputs:    a0 = channel, a1 = MADR, a2 = block size, a3 = block count, 0x10(sp) = CHCR value,
 *            0x14(sp) = irq flag, 0x18(sp) = DICR mask value (callers pass 0).
 * Registers: s2 channel, s3 madr, s4 block size, s5 count, s6 chcr, s0 irq, s1 mask. Frame 56 bytes.
 * Callers:   StCdInterrupt.  Callees: printf.
 */
	.globl	f_61120
f_61120:
	addiu	sp, sp, -56	/* allocate 56 byte frame */
	sw	s2, 32(sp)	/* save s2 */
	addu	s2, a0, zero	/* s2 = a0 */
	sw	s3, 36(sp)	/* save s3 */
	addu	s3, a1, zero	/* s3 = a1 */
	sw	s4, 40(sp)	/* save s4 */
	addu	s4, a2, zero	/* s4 = a2 */
	sw	s5, 44(sp)	/* save s5 */
	addu	s5, a3, zero	/* s5 = a3 */
	sw	s0, 24(sp)	/* save s0 */
	lw	s0, 76(sp)	/* restore s0 */
	sll	a1, s2, 4	/* a1 = s2 << 4 */
	sw	s1, 28(sp)	/* save s1 */
	lw	s1, 80(sp)	/* restore s1 */
	lui	v1, 0x100	/* v1 = 0x1000000 */
	sw	ra, 52(sp)	/* save ra */
	sw	s6, 48(sp)	/* save s6 */

	/* CHCR of the channel is at 0x1f801088 + channel * 16; wait while its bit 24 (busy) is set, at most 0x10000 polls */
	.set	noat
	lui	$1, 0x1f80	/* $1 = 0x1f800000 */
	addu	$1, a1, $1	/* $1 = a1 + $1 */
	lw	v0, 4232($1)	/* v0 = *(word *)($1 + 4232) */
	.set	at
	lw	s6, 72(sp)	/* restore s6 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	beq	v0, zero, $L611a8	/* if (v0 == 0) goto $L611a8 */
	addu	a0, zero, zero	/* [delay slot, real work] a0 = zero */
	lui	a2, 0x1	/* a2 = 0x10000 */
$L61184:
	beq	a0, a2, $L611e4	/* if (a0 == a2) goto $L611e4 */
	nop	/* delay slot: nothing */
	.set	noat
	lui	$1, 0x1f80	/* $1 = 0x1f800000 */
	addu	$1, a1, $1	/* $1 = a1 + $1 */
	lw	v0, 4232($1)	/* v0 = *(word *)($1 + 4232) */
	.set	at
	nop	/* load delay slot: nothing */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bne	v0, zero, $L61184	/* if (v0 != 0) goto $L61184 */
	addiu	a0, a0, 1	/* [delay slot, real work] a0 = a0 + 1 */
$L611a8:
	ori	v1, zero, 0x1	/* v1 = 1 */

	/* irq == 1: new DICR = (DICR & 0x00ffffff) | (1 << (16 + ch)) | (mask << ch) */
$L611ac:
	bne	s0, v1, $L61208	/* if (s0 != v1) goto $L61208 */
	addiu	v0, s2, 16	/* [delay slot, real work] v0 = s2 + 16 */
	lui	s0, 0x1	/* s0 = 0x10000 */
	sllv	s0, s0, s2
	sllv	s1, s1, s2
	or	s0, s0, s1	/* s0 = s0 | s1 */
	lw	v1, g_74840+28	/* v1 = g_74840+28 (word) */
	lui	v0, 0xff	/* v0 = 0xff0000 */
	lw	a2, 0(v1)	/* a2 = *(word *)(v1 + 0) */
	ori	v0, v0, 0xffff	/* v0 = v0 | 0xffff */
	and	a2, a2, v0	/* a2 = a2 & v0 */
	j	$L6122c	/* goto $L6122c */
	or	a2, a2, s0	/* [delay slot, real work] a2 = a2 | s0 */

	/* Time-out: print "DMA STATUS ERROR %x" with the CHCR value and carry on */
$L611e4:
	.set	noat
	lui	$1, 0x1f80	/* $1 = 0x1f800000 */
	addu	$1, a1, $1	/* $1 = a1 + $1 */
	lw	a1, 4232($1)	/* a1 = *(word *)($1 + 4232) */
	.set	at
	la	a0, $LC0	/* a0 = &$LC0 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	j	$L611ac	/* goto $L611ac */
	ori	v1, zero, 0x1	/* [delay slot, real work] v1 = 1 */

	/* irq != 1: new DICR = (DICR & 0x00ffffff) with the channel enable bit (16 + ch) cleared */
$L61208:
	sllv	s0, v1, v0
	lw	v1, g_74840+28	/* v1 = g_74840+28 (word) */
	lui	v0, 0xff	/* v0 = 0xff0000 */
	lw	a2, 0(v1)	/* a2 = *(word *)(v1 + 0) */
	ori	v0, v0, 0xffff	/* v0 = v0 | 0xffff */
	and	a2, a2, v0	/* a2 = a2 & v0 */
	nor	v0, zero, s0	/* v0 = zero nor s0 */
	and	a2, a2, v0	/* a2 = a2 & v0 */
$L6122c:
	sw	a2, 0(v1)	/* *(word *)(v1 + 0) = a2 */

	/* Read DICR back (flush), enable the channel in DPCR (bit 4 * ch + 3) */
	lw	v0, g_74840+28	/* v0 = g_74840+28 (word) */
	nop	/* load delay slot: nothing */
	lw	v0, 0(v0)	/* v0 = *(word *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	sw	v0, 16(sp)	/* spill v0 to sp+16 */
	sll	a2, s2, 2	/* a2 = s2 << 2 */
	addiu	a2, a2, 3	/* a2 = a2 + 3 */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sllv	v0, v0, a2
	lui	a1, 0x1f80	/* a1 = 0x1f800000 */
	ori	a1, a1, 0x1080	/* a1 = a1 | 0x1080 */
	lw	a0, g_74840+24	/* a0 = g_74840+24 (word) */
	sll	v1, s2, 4	/* v1 = s2 << 4 */
	lw	a2, 0(a0)	/* a2 = *(word *)(a0 + 0) */
	addu	v1, v1, a1	/* v1 = v1 + a1 */
	or	a2, a2, v0	/* a2 = a2 | v0 */
	sw	a2, 0(a0)	/* *(word *)(a0 + 0) = a2 */

	/* MADR, BCR, CHCR (writing CHCR starts the transfer); read CHCR back to complete the write */
	sw	s3, 0(v1)	/* *(word *)(v1 + 0) = s3 */
	addiu	v1, v1, 4	/* v1 = v1 + 4 */
	sll	v0, s4, 16	/* v0 = s4 << 16 */
	or	v0, v0, s5	/* v0 = v0 | s5 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */
	addiu	v1, v1, 4	/* v1 = v1 + 4 */
	sw	s6, 0(v1)	/* *(word *)(v1 + 0) = s6 */
	lw	v0, 0(v1)	/* v0 = *(word *)(v1 + 0) */
	nop	/* load delay slot: nothing */
	sw	v0, 16(sp)	/* spill v0 to sp+16 */
	lw	ra, 52(sp)	/* restore ra */
	lw	s6, 48(sp)	/* restore s6 */
	lw	s5, 44(sp)	/* restore s5 */
	lw	s4, 40(sp)	/* restore s4 */
	lw	s3, 36(sp)	/* restore s3 */
	lw	s2, 32(sp)	/* restore s2 */
	lw	s1, 28(sp)	/* restore s1 */
	lw	s0, 24(sp)	/* restore s0 */
	addiu	sp, sp, 56	/* release 56 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
