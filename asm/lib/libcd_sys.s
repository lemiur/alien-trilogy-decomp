/*
 * libcd_sys.s - LIBCD module SYS: the public Cd* wrapper functions over the BIOS driver.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library module that retail
 * links as a rebuild matching no archive in ../sdk. Assembled by tools/build_asm.py (CPPPSX, then ASPSX 2.34);
 * placed by config/libcd_sys.json and the "libcd_sys" entry of config/data.json; counted as "library
 * assembly", never as matched C and never as certified library.
 *
 * Library:   LIBCD.LIB (Sony Runtime Library, CD-ROM).   Module: SYS (retail 0x8005d310..0x8005daa7, 1944 bytes).
 * Exports:   CdStatus, CdLastCom, CdReset, CdFlush, CdSetDebug, CdComstr, CdIntstr, CdSync, CdReady,
 *            CdSyncCallback, CdReadyCallback, CdReadCallback, CdControl, CdControlF, CdControlB, CdMix,
 *            CdGetSector, CdDataCallback, CdDataSync, CdReadSync, CdRead, CdIntToPos, CdPosToInt.
 * Imports:   the BIOS module's CD_* routines and variables (libcd_bios.s), DMACallback (LIBETC).
 * Own data:  .rdata 0x80012c78 "none" (5 bytes, shared by CdComstr and CdIntstr);
 *            .data 0x8007447c g_7447c, 128 bytes, the CdControl per-command table.
 *
 * Module identification. The survey's S_0xx alignment (4.6 module names) is not the retail layout: retail has
 * one shared "none" literal and one 128-byte .data table, which a split of SYS into S_003, S_010, S_011, S_016
 * ... would have duplicated, and the functions follow the 3.x order of the single SYS module (CdStatus,
 * CdLastCom, CdReset, ..., CdIntToPos, CdPosToInt). This unit is therefore the single module SYS. No archived
 * LIBCD has the same function set (the 4.x exports CdLastPos and CdMode are absent), so it is a variant.
 *
 * Difference from the nearest archive (relocation-masked word comparison per routine; "3.4" is LIB34.ZIP of the
 * 3.5 CD release, "3.7" is LIB37.ZIP of the 4.0 release):
 *   CdStatus, CdLastCom, CdSync, CdReady, CdDataSync, CdReadSync, CdFlush, CdSyncCallback, CdReadyCallback,
 *   CdReadCallback, CdSetDebug   one-load/one-call wrappers, word-identical in shape to the same wrappers of
 *                                every archived version (only the relocation targets differ by module layout)
 *   CdComstr, CdIntstr           identical to 2.6 .. 3.5
 *   CdMix, CdGetSector, CdDataCallback, CdIntToPos    identical to 3.3, 3.4, 3.5 and 3.6
 *   CdRead                       identical to 3.3 and 3.4
 *   CdPosToInt                   identical to 3.5 .. 4.7
 *   CdReset                      19 of its 20 words identical to 3.3-3.6 (0x8005d340, the delay-slot move,
 *                                differs); the archived routine is longer (27 words)
 *   CdControl                    6 retail words differ from 3.5/3.6 (0x8005d508, 0x8005d50c, 0x8005d510,
 *                                0x8005d520, 0x8005d580, 0x8005d584)
 *   CdControlF                   2 words differ from 3.3/3.4 (0x8005d690, 0x8005d694)
 *   CdControlB                   5 words differ from 3.5/3.6 (0x8005d724, 0x8005d728, 0x8005d72c, 0x8005d73c,
 *                                0x8005d79c)
 * Each routine is transcribed exactly as retail has it; every delay slot is written out.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.rdata
/*
 * "none" - the name CdComstr and CdIntstr return for an out-of-range code. Compiler label; users: CdComstr, CdIntstr.
 */
$LC0:
	.ascii	"none\000"
	.align	2

	.data
/*
 * g_7447c - CdControl's per-command flag table (32 words, indexed by command code): 1 for CdlPlay (3), CdlReadN (6),
 * CdlSeekL (0x15), CdlSeekP (0x16) and CdlReadS (0x1b), the commands that take a start position and so are
 * preceded by CdlSetloc when a parameter is passed. Static (no .globl). Users: CdControl, CdControlF, CdControlB.
 */
g_7447c:
	.word	0, 0, 0, 1
	.word	0, 0, 1, 0
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0
	.word	0, 1, 1, 0
	.word	0, 0, 0, 1
	.word	0, 0, 0, 0

	.text

/*
 * CdStatus - last drive status byte
 * Purpose:   int CdStatus(void): returns CD_status (u8), the status byte of the last command response.
 * Outputs:   v0.   Leaf, no frame.   Callers: game code.
 */
	.globl	CdStatus
CdStatus:
	lbu	v0, CD_status	/* v0 = CD_status (u8) */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdLastCom - last command code
 * Purpose:   int CdLastCom(void): returns CD_com (u8), the code of the last command sent by CD_cw.
 * Outputs:   v0.   Leaf, no frame.
 */
	.globl	CdLastCom
CdLastCom:
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdReset - initialise the CD-ROM system
 * Purpose:   int CdReset(int mode): CD_init(); with mode 1 also CD_initvol(). Returns 1 on success, 0 on failure.
 * Inputs:    a0 = mode.   Outputs: v0.   Clobbers: v0, a0, s0 (saved). Frame 24 bytes.
 * Callees:   CD_init, CD_initvol.
 */
	.globl	CdReset
CdReset:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	s0, 16(sp)	/* save s0 */
	sw	ra, 20(sp)	/* save ra */
	jal	CD_init	/* call CD_init */
	addu	s0, a0, zero	/* [delay slot, real work] s0 = a0 */
	bne	v0, zero, $L5d36c	/* if (v0 != 0) goto $L5d36c */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */
	ori	v0, zero, 0x1	/* v0 = 1 */
	bne	s0, v0, $L5d36c	/* if (s0 != v0) goto $L5d36c */
	nop	/* delay slot: nothing */
	jal	CD_initvol	/* call CD_initvol */
	nop	/* delay slot: nothing */
	bne	v0, zero, $L5d36c	/* if (v0 != 0) goto $L5d36c */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */
	ori	v0, zero, 0x1	/* v0 = 1 */
$L5d36c:
	lw	ra, 20(sp)	/* restore ra */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdFlush - discard pending controller interrupts
 * Purpose:   int CdFlush(void): calls CD_flush and returns its value. Frame 24 bytes.
 */
	.globl	CdFlush
CdFlush:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	jal	CD_flush	/* call CD_flush */
	nop	/* delay slot: nothing */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdSetDebug - set the trace level
 * Purpose:   int CdSetDebug(int level): CD_debug = level; returns the previous level.
 * Inputs:    a0 = level.   Outputs: v0 = old level.   Leaf, no frame.
 */
	.globl	CdSetDebug
CdSetDebug:
	lw	v0, CD_debug	/* v0 = CD_debug (word) */
	sw	a0, CD_debug	/* CD_debug = a0 */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdComstr - name of a command code
 * Purpose:   char *CdComstr(u_char code): CD_comstr[code] for code < 28, otherwise "none".
 * Inputs:    a0 = code (low byte used).   Outputs: v0 = string.   Leaf. The table entry is fetched with
 *            lui/addiu/addu/lw through $at (the compiler's indexed load of a static table).
 */
	.globl	CdComstr
CdComstr:
	andi	a0, a0, 0xff	/* a0 = a0 & 0xff */
	sltiu	v0, a0, 28	/* v0 = a0 < 28 */
	beq	v0, zero, $L5d3e0	/* if (v0 == 0) goto $L5d3e0 */
	sll	v0, a0, 2	/* [delay slot, real work] v0 = a0 << 2 */
	.set	noat
	la	$1, CD_comstr	/* $1 = &CD_comstr */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	v0, 0($1)	/* v0 = *(word *)($1 + 0) */
	.set	at
	j	$L5d3e8	/* goto $L5d3e8 */
	nop	/* delay slot: nothing */
$L5d3e0:
	la	v0, $LC0	/* v0 = &$LC0 */
$L5d3e8:
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdIntstr - name of an interrupt code
 * Purpose:   char *CdIntstr(u_char intr): CD_intstr[intr] for intr < 7, otherwise "none".
 * Inputs:    a0 = code.   Outputs: v0 = string.   Leaf.
 */
	.globl	CdIntstr
CdIntstr:
	andi	a0, a0, 0xff	/* a0 = a0 & 0xff */
	sltiu	v0, a0, 7	/* v0 = a0 < 7 */
	beq	v0, zero, $L5d418	/* if (v0 == 0) goto $L5d418 */
	sll	v0, a0, 2	/* [delay slot, real work] v0 = a0 << 2 */
	.set	noat
	la	$1, CD_intstr	/* $1 = &CD_intstr */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	v0, 0($1)	/* v0 = *(word *)($1 + 0) */
	.set	at
	j	$L5d420	/* goto $L5d420 */
	nop	/* delay slot: nothing */
$L5d418:
	la	v0, $LC0	/* v0 = &$LC0 */
$L5d420:
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdSync - wait for the current command to finish
 * Purpose:   int CdSync(int mode, u_char *result) = CD_sync(mode, result). Frame 24 bytes.
 */
	.globl	CdSync
CdSync:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	jal	CD_sync	/* call CD_sync */
	nop	/* delay slot: nothing */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdReady - wait for sector data
 * Purpose:   int CdReady(int mode, u_char *result) = CD_ready(mode, result). Frame 24 bytes.
 */
	.globl	CdReady
CdReady:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	jal	CD_ready	/* call CD_ready */
	nop	/* delay slot: nothing */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdSyncCallback - set the command-complete callback
 * Purpose:   CdlCB CdSyncCallback(CdlCB func): CD_cbsync = func; returns the old callback. Leaf.
 */
	.globl	CdSyncCallback
CdSyncCallback:
	lw	v0, CD_cbsync	/* v0 = CD_cbsync (word) */
	sw	a0, CD_cbsync	/* CD_cbsync = a0 */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdReadyCallback - set the data-ready callback
 * Purpose:   CdlCB CdReadyCallback(CdlCB func): CD_cbready = func; returns the old callback. Leaf.
 */
	.globl	CdReadyCallback
CdReadyCallback:
	lw	v0, CD_cbready	/* v0 = CD_cbready (word) */
	sw	a0, CD_cbready	/* CD_cbready = a0 */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdReadCallback - set the read-complete callback
 * Purpose:   CdlCB CdReadCallback(CdlCB func): CD_cbread = func; returns the old callback. Leaf.
 */
	.globl	CdReadCallback
CdReadCallback:
	lw	v0, CD_cbread	/* v0 = CD_cbread (word) */
	sw	a0, CD_cbread	/* CD_cbread = a0 */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdControl - issue a CD-ROM command and wait for its acknowledge
 * Purpose:   int CdControl(u_char com, u_char *param, u_char *result). Up to four attempts (s0 = 3 .. -1).
 *            Each attempt (except for CdlNop) clears CD_cbsync, waits out an open lid (CD_shell) and, when a
 *            position is supplied for a command flagged in g_7447c (Play, ReadN, SeekL, SeekP, ReadS), sends
 *            CdlSetloc (2) first; then sends the command with CD_cw(com, param, result, 0). The saved
 *            CD_cbsync (s5) is restored after the Setloc and when giving up.
 * Inputs:    a0 = command code, a1 = parameter bytes or NULL, a2 = result buffer or NULL.
 * Outputs:   v0 = 1 on success, 0 after four failures.
 * Registers: s0 attempts left, s1 param, s2 result, s3 command, s4 raw command, s5 saved CD_cbsync, s6 =
 *            &g_7447c[com], s7 = 0 / -1 (result - 1). Frame 56 bytes.
 * Callers:   game code, CdRead2.
 */
	.globl	CdControl
CdControl:
	addiu	sp, sp, -56	/* allocate 56 byte frame */
	sw	s1, 20(sp)	/* save s1 */
	addu	s1, a1, zero	/* s1 = a1 */
	sw	s2, 24(sp)	/* save s2 */
	addu	s2, a2, zero	/* s2 = a2 */
	sw	s4, 32(sp)	/* save s4 */
	addu	s4, a0, zero	/* s4 = a0 */
	sw	s0, 16(sp)	/* save s0 */
	ori	s0, zero, 0x3	/* s0 = 3 */
	sw	s3, 28(sp)	/* save s3 */
	andi	s3, s4, 0xff	/* s3 = s4 & 0xff */
	la	v1, g_7447c	/* v1 = &g_7447c */
	sw	s5, 36(sp)	/* save s5 */
	lw	s5, CD_cbsync	/* s5 = CD_cbsync (word) */
	sll	v0, s3, 2	/* v0 = s3 << 2 */
	sw	s6, 40(sp)	/* save s6 */
	addu	s6, v0, v1	/* s6 = v0 + v1 */
	sw	s7, 44(sp)	/* save s7 */
	addu	s7, zero, zero	/* s7 = zero */
	sw	ra, 48(sp)	/* save ra */
	ori	v0, zero, 0x1	/* v0 = 1 */
$L5d50c:
	beq	s3, v0, $L5d560	/* if (s3 == v0) goto $L5d560 */
	andi	a0, s4, 0xff	/* [delay slot, real work] a0 = s4 & 0xff */
	sw	zero, CD_cbsync	/* CD_cbsync = zero */
	jal	CD_shell	/* call CD_shell */
	nop	/* delay slot: nothing */
	beq	s1, zero, $L5d554	/* if (s1 == 0) goto $L5d554 */
	nop	/* delay slot: nothing */
	lw	v0, 0(s6)	/* v0 = *(word *)(s6 + 0) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5d554	/* if (v0 == 0) goto $L5d554 */
	ori	a0, zero, 0x2	/* [delay slot, real work] a0 = 2 */
	addu	a1, s1, zero	/* a1 = s1 */
	addu	a2, s2, zero	/* a2 = s2 */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5d578	/* if (v0 != 0) goto $L5d578 */
	nop	/* delay slot: nothing */
$L5d554:
	sw	s5, CD_cbsync	/* CD_cbsync = s5 */
	andi	a0, s4, 0xff	/* a0 = s4 & 0xff */
$L5d560:
	addu	a1, s1, zero	/* a1 = s1 */
	addu	a2, s2, zero	/* a2 = s2 */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	beq	v0, zero, $L5d598	/* if (v0 == 0) goto $L5d598 */
	addiu	v0, s7, 1	/* [delay slot, real work] v0 = s7 + 1 */
$L5d578:
	addiu	s0, s0, -1	/* s0 = s0 - 1 */
	addiu	v0, zero, -1	/* v0 = -1 */
	bne	s0, v0, $L5d50c	/* if (s0 != v0) goto $L5d50c */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */
	sw	s5, CD_cbsync	/* CD_cbsync = s5 */
	addiu	s7, zero, -1	/* s7 = -1 */
	addiu	v0, s7, 1	/* v0 = s7 + 1 */
$L5d598:
	lw	ra, 48(sp)	/* restore ra */
	lw	s7, 44(sp)	/* restore s7 */
	lw	s6, 40(sp)	/* restore s6 */
	lw	s5, 36(sp)	/* restore s5 */
	lw	s4, 32(sp)	/* restore s4 */
	lw	s3, 28(sp)	/* restore s3 */
	lw	s2, 24(sp)	/* restore s2 */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 56	/* release 56 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdControlF - issue a command without waiting for the acknowledge
 * Purpose:   int CdControlF(u_char com, u_char *param). Like CdControl but with no result buffer: the Setloc
 *            is sent with CD_cw(2, param, NULL, 0) and the command with CD_cw(com, param, NULL, 1), which
 *            returns as soon as the command byte is written.
 * Outputs:   v0 = 1 success, 0 failure.   Frame 48 bytes.
 */
	.globl	CdControlF
CdControlF:
	addiu	sp, sp, -48	/* allocate 48 byte frame */
	sw	s1, 20(sp)	/* save s1 */
	addu	s1, a1, zero	/* s1 = a1 */
	sw	s3, 28(sp)	/* save s3 */
	addu	s3, a0, zero	/* s3 = a0 */
	sw	s0, 16(sp)	/* save s0 */
	ori	s0, zero, 0x3	/* s0 = 3 */
	sw	s2, 24(sp)	/* save s2 */
	andi	s2, s3, 0xff	/* s2 = s3 & 0xff */
	la	v1, g_7447c	/* v1 = &g_7447c */
	sw	s4, 32(sp)	/* save s4 */
	lw	s4, CD_cbsync	/* s4 = CD_cbsync (word) */
	sll	v0, s2, 2	/* v0 = s2 << 2 */
	sw	s5, 36(sp)	/* save s5 */
	addu	s5, v0, v1	/* s5 = v0 + v1 */
	sw	s6, 40(sp)	/* save s6 */
	addu	s6, zero, zero	/* s6 = zero */
	sw	ra, 44(sp)	/* save ra */
	ori	v0, zero, 0x1	/* v0 = 1 */
$L5d61c:
	beq	s2, v0, $L5d670	/* if (s2 == v0) goto $L5d670 */
	andi	a0, s3, 0xff	/* [delay slot, real work] a0 = s3 & 0xff */
	sw	zero, CD_cbsync	/* CD_cbsync = zero */
	jal	CD_shell	/* call CD_shell */
	nop	/* delay slot: nothing */
	beq	s1, zero, $L5d664	/* if (s1 == 0) goto $L5d664 */
	nop	/* delay slot: nothing */
	lw	v0, 0(s5)	/* v0 = *(word *)(s5 + 0) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5d664	/* if (v0 == 0) goto $L5d664 */
	ori	a0, zero, 0x2	/* [delay slot, real work] a0 = 2 */
	addu	a1, s1, zero	/* a1 = s1 */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5d688	/* if (v0 != 0) goto $L5d688 */
	nop	/* delay slot: nothing */
$L5d664:
	sw	s4, CD_cbsync	/* CD_cbsync = s4 */
	andi	a0, s3, 0xff	/* a0 = s3 & 0xff */
$L5d670:
	addu	a1, s1, zero	/* a1 = s1 */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	ori	a3, zero, 0x1	/* [delay slot, real work] a3 = 1 */
	beq	v0, zero, $L5d6a8	/* if (v0 == 0) goto $L5d6a8 */
	addiu	v0, s6, 1	/* [delay slot, real work] v0 = s6 + 1 */
$L5d688:
	addiu	s0, s0, -1	/* s0 = s0 - 1 */
	addiu	v0, zero, -1	/* v0 = -1 */
	bne	s0, v0, $L5d61c	/* if (s0 != v0) goto $L5d61c */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */
	sw	s4, CD_cbsync	/* CD_cbsync = s4 */
	addiu	s6, zero, -1	/* s6 = -1 */
	addiu	v0, s6, 1	/* v0 = s6 + 1 */
$L5d6a8:
	lw	ra, 44(sp)	/* restore ra */
	lw	s6, 40(sp)	/* restore s6 */
	lw	s5, 36(sp)	/* restore s5 */
	lw	s4, 32(sp)	/* restore s4 */
	lw	s3, 28(sp)	/* restore s3 */
	lw	s2, 24(sp)	/* restore s2 */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 48	/* release 48 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdControlB - issue a command and wait for it to complete
 * Purpose:   int CdControlB(u_char com, u_char *param, u_char *result). Like CdControl, but after a successful
 *            send it waits with CD_sync(0, result) and returns 1 only when the sync code is 2 (Complete).
 * Outputs:   v0 = 1 / 0.   Frame 48 bytes.
 */
	.globl	CdControlB
CdControlB:
	addiu	sp, sp, -48	/* allocate 48 byte frame */
	sw	s1, 20(sp)	/* save s1 */
	addu	s1, a1, zero	/* s1 = a1 */
	sw	s2, 24(sp)	/* save s2 */
	addu	s2, a2, zero	/* s2 = a2 */
	sw	s4, 32(sp)	/* save s4 */
	addu	s4, a0, zero	/* s4 = a0 */
	sw	s0, 16(sp)	/* save s0 */
	ori	s0, zero, 0x3	/* s0 = 3 */
	sw	s3, 28(sp)	/* save s3 */
	andi	s3, s4, 0xff	/* s3 = s4 & 0xff */
	la	v1, g_7447c	/* v1 = &g_7447c */
	sw	s5, 36(sp)	/* save s5 */
	lw	s5, CD_cbsync	/* s5 = CD_cbsync (word) */
	sll	v0, s3, 2	/* v0 = s3 << 2 */
	sw	s6, 40(sp)	/* save s6 */
	addu	s6, v0, v1	/* s6 = v0 + v1 */
	sw	ra, 44(sp)	/* save ra */
$L5d724:
	ori	v0, zero, 0x1	/* v0 = 1 */
	beq	s3, v0, $L5d77c	/* if (s3 == v0) goto $L5d77c */
	andi	a0, s4, 0xff	/* [delay slot, real work] a0 = s4 & 0xff */
	sw	zero, CD_cbsync	/* CD_cbsync = zero */
	jal	CD_shell	/* call CD_shell */
	nop	/* delay slot: nothing */
	beq	s1, zero, $L5d770	/* if (s1 == 0) goto $L5d770 */
	nop	/* delay slot: nothing */
	lw	v0, 0(s6)	/* v0 = *(word *)(s6 + 0) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5d770	/* if (v0 == 0) goto $L5d770 */
	ori	a0, zero, 0x2	/* [delay slot, real work] a0 = 2 */
	addu	a1, s1, zero	/* a1 = s1 */
	addu	a2, s2, zero	/* a2 = s2 */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5d794	/* if (v0 != 0) goto $L5d794 */
	nop	/* delay slot: nothing */
$L5d770:
	sw	s5, CD_cbsync	/* CD_cbsync = s5 */
	andi	a0, s4, 0xff	/* a0 = s4 & 0xff */
$L5d77c:
	addu	a1, s1, zero	/* a1 = s1 */
	addu	a2, s2, zero	/* a2 = s2 */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	beq	v0, zero, $L5d7ac	/* if (v0 == 0) goto $L5d7ac */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */
$L5d794:
	addiu	s0, s0, -1	/* s0 = s0 - 1 */
	addiu	v0, zero, -1	/* v0 = -1 */
	bne	s0, v0, $L5d724	/* if (s0 != v0) goto $L5d724 */
	nop	/* delay slot: nothing */
	sw	s5, CD_cbsync	/* CD_cbsync = s5 */
$L5d7ac:
	bne	v0, zero, $L5d7c8	/* if (v0 != 0) goto $L5d7c8 */
	addu	a0, zero, zero	/* [delay slot, real work] a0 = zero */
	jal	CD_sync	/* call CD_sync */
	addu	a1, s2, zero	/* [delay slot, real work] a1 = s2 */
	xori	v0, v0, 0x2	/* v0 = v0 ^ 0x2 */
	j	$L5d7cc	/* goto $L5d7cc */
	sltiu	v0, v0, 1	/* [delay slot, real work] v0 = v0 < 1 */
$L5d7c8:
	addu	v0, zero, zero	/* v0 = zero */
$L5d7cc:
	lw	ra, 44(sp)	/* restore ra */
	lw	s6, 40(sp)	/* restore s6 */
	lw	s5, 36(sp)	/* restore s5 */
	lw	s4, 32(sp)	/* restore s4 */
	lw	s3, 28(sp)	/* restore s3 */
	lw	s2, 24(sp)	/* restore s2 */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 48	/* release 48 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdMix - set the CD audio mixer
 * Purpose:   int CdMix(CdlATV *vol): CD_vol(vol); returns 1. Frame 24 bytes.
 */
	.globl	CdMix
CdMix:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	jal	CD_vol	/* call CD_vol */
	nop	/* delay slot: nothing */
	ori	v0, zero, 0x1	/* v0 = 1 */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdGetSector - copy one sector from the controller by DMA
 * Purpose:   int CdGetSector(void *madr, int size): returns CD_getsector(madr, size) == 0 (1 on success). Frame 24 bytes.
 */
	.globl	CdGetSector
CdGetSector:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	jal	CD_getsector	/* call CD_getsector */
	nop	/* delay slot: nothing */
	sltiu	v0, v0, 1	/* v0 = v0 < 1 */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdDataCallback - set the CD DMA-complete callback
 * Purpose:   int CdDataCallback(void (*func)()): DMACallback(3, func) (DMA channel 3 = CD-ROM). Frame 24 bytes.
 */
	.globl	CdDataCallback
CdDataCallback:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	addu	a1, a0, zero	/* a1 = a0 */
	jal	DMACallback	/* call DMACallback */
	ori	a0, zero, 0x3	/* [delay slot, real work] a0 = 3 */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdDataSync - wait for the CD DMA
 * Purpose:   int CdDataSync(int mode) = CD_datasync(mode). Frame 24 bytes.
 */
	.globl	CdDataSync
CdDataSync:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	jal	CD_datasync	/* call CD_datasync */
	nop	/* delay slot: nothing */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdReadSync - wait for a CdRead to finish
 * Purpose:   int CdReadSync(int mode, u_char *result) = CD_readsync(mode, result). Frame 24 bytes.
 */
	.globl	CdReadSync
CdReadSync:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	ra, 16(sp)	/* save ra */
	jal	CD_readsync	/* call CD_readsync */
	nop	/* delay slot: nothing */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdRead - read sectors into memory
 * Purpose:   int CdRead(int sectors, u_long *buf, int mode): up to three tries (s0 = 3 .. -1) of
 *            CD_readm(buf, sectors, mode); returns 1 as soon as one try starts, 0 after three failures.
 * Inputs:    a0 = sector count, a1 = buffer, a2 = mode.   Frame 40 bytes.
 */
	.globl	CdRead
CdRead:
	addiu	sp, sp, -40	/* allocate 40 byte frame */
	sw	s1, 20(sp)	/* save s1 */
	addu	s1, a0, zero	/* s1 = a0 */
	sw	s2, 24(sp)	/* save s2 */
	addu	s2, a1, zero	/* s2 = a1 */
	sw	s3, 28(sp)	/* save s3 */
	addu	s3, a2, zero	/* s3 = a2 */
	sw	s0, 16(sp)	/* save s0 */
	ori	s0, zero, 0x3	/* s0 = 3 */
	sw	s4, 32(sp)	/* save s4 */
	addiu	s4, zero, -1	/* s4 = -1 */
	sw	ra, 36(sp)	/* save ra */
	addu	a0, s2, zero	/* a0 = s2 */
$L5d8d8:
	addu	a1, s1, zero	/* a1 = s1 */
	jal	CD_readm	/* call CD_readm */
	addu	a2, s3, zero	/* [delay slot, real work] a2 = s3 */
	bne	v0, zero, $L5d8f4	/* if (v0 != 0) goto $L5d8f4 */
	addiu	s0, s0, -1	/* [delay slot, real work] s0 = s0 - 1 */
	j	$L5d900	/* goto $L5d900 */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */
$L5d8f4:
	bne	s0, s4, $L5d8d8	/* if (s0 != s4) goto $L5d8d8 */
	addu	a0, s2, zero	/* [delay slot, real work] a0 = s2 */
	addu	v0, zero, zero	/* v0 = zero */
$L5d900:
	lw	ra, 36(sp)	/* restore ra */
	lw	s4, 32(sp)	/* restore s4 */
	lw	s3, 28(sp)	/* restore s3 */
	lw	s2, 24(sp)	/* restore s2 */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 40	/* release 40 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CdIntToPos - absolute sector number to minute/second/frame (BCD)
 * Purpose:   CdlLOC *CdIntToPos(int i, CdlLOC *p): i += 150 (the 2 s lead-in), then minute = i / 4500,
 *            second = (i / 75) % 60, frame = i % 75, each stored as two BCD digits at p[0..2]. The divisions by
 *            4500, 75 and 60 are compiled as multiplications by reciprocals (0x1b4e81b5, 0x66666667, 0x88888889).
 * Inputs:    a0 = sector, a1 = destination.   Outputs: v0 = p.   Leaf, no frame.
 */
	.globl	CdIntToPos
CdIntToPos:
	lui	v1, 0x1b4e	/* v1 = 0x1b4e0000 */
	ori	v1, v1, 0x81b5	/* v1 = v1 | 0x81b5 */
	addiu	a0, a0, 150	/* a0 = a0 + 150 */
	mult	a0, v1	/* (HI,LO) = a0 * v1 */
	addu	v0, a1, zero	/* v0 = a1 */
	lui	a3, 0x6666	/* a3 = 0x66660000 */
	ori	a3, a3, 0x6667	/* a3 = a3 | 0x6667 */
	sra	v1, a0, 31	/* v1 = a0 >> (arith) 31 */
	mfhi	a2	/* a2 = HI */
	sra	a2, a2, 3	/* a2 = a2 >> (arith) 3 */
	subu	a2, a2, v1	/* a2 = a2 - v1 */
	sll	a1, a2, 2	/* a1 = a2 << 2 */
	addu	a1, a1, a2	/* a1 = a1 + a2 */
	sll	v1, a1, 4	/* v1 = a1 << 4 */
	subu	v1, v1, a1	/* v1 = v1 - a1 */
	subu	a0, a0, v1	/* a0 = a0 - v1 */
	mult	a0, a3	/* (HI,LO) = a0 * a3 */
	mfhi	t0	/* t0 = HI */
	lui	v1, 0x8888	/* v1 = 0x88880000 */
	ori	v1, v1, 0x8889	/* v1 = v1 | 0x8889 */
	mult	a2, v1	/* (HI,LO) = a2 * v1 */
	sra	v1, a2, 31	/* v1 = a2 >> (arith) 31 */
	mfhi	t1	/* t1 = HI */
	addu	t1, t1, a2	/* t1 = t1 + a2 */
	sra	t1, t1, 5	/* t1 = t1 >> (arith) 5 */
	subu	t1, t1, v1	/* t1 = t1 - v1 */
	sll	v1, t1, 4	/* v1 = t1 << 4 */
	subu	v1, v1, t1	/* v1 = v1 - t1 */
	sll	v1, v1, 2	/* v1 = v1 << 2 */
	subu	a2, a2, v1	/* a2 = a2 - v1 */
	mult	a2, a3	/* (HI,LO) = a2 * a3 */
	sra	t0, t0, 2	/* t0 = t0 >> (arith) 2 */
	sra	v1, a0, 31	/* v1 = a0 >> (arith) 31 */
	subu	t0, t0, v1	/* t0 = t0 - v1 */
	sll	v1, t0, 2	/* v1 = t0 << 2 */
	addu	v1, v1, t0	/* v1 = v1 + t0 */
	sll	v1, v1, 1	/* v1 = v1 << 1 */
	mfhi	a1	/* a1 = HI */
	subu	a0, a0, v1	/* a0 = a0 - v1 */
	sra	v1, a2, 31	/* v1 = a2 >> (arith) 31 */
	mult	t1, a3	/* (HI,LO) = t1 * a3 */
	sll	a3, t0, 4	/* a3 = t0 << 4 */
	addu	a3, a3, a0	/* a3 = a3 + a0 */
	sra	a1, a1, 2	/* a1 = a1 >> (arith) 2 */
	subu	a1, a1, v1	/* a1 = a1 - v1 */
	sll	a0, a1, 4	/* a0 = a1 << 4 */
	sll	v1, a1, 2	/* v1 = a1 << 2 */
	addu	v1, v1, a1	/* v1 = v1 + a1 */
	sll	v1, v1, 1	/* v1 = v1 << 1 */
	subu	a2, a2, v1	/* a2 = a2 - v1 */
	addu	a0, a0, a2	/* a0 = a0 + a2 */
	sb	a0, 1(v0)	/* *(u8 *)(v0 + 1) = a0 */
	sra	a0, t1, 31	/* a0 = t1 >> (arith) 31 */
	sb	a3, 2(v0)	/* *(u8 *)(v0 + 2) = a3 */
	mfhi	v1	/* v1 = HI */
	sra	v1, v1, 2	/* v1 = v1 >> (arith) 2 */
	subu	v1, v1, a0	/* v1 = v1 - a0 */
	sll	a1, v1, 4	/* a1 = v1 << 4 */
	sll	a0, v1, 2	/* a0 = v1 << 2 */
	addu	a0, a0, v1	/* a0 = a0 + v1 */
	sll	a0, a0, 1	/* a0 = a0 << 1 */
	subu	t1, t1, a0	/* t1 = t1 - a0 */
	addu	a1, a1, t1	/* a1 = a1 + t1 */
	jr	ra	/* return */
	sb	a1, 0(v0)	/* [delay slot, real work] *(u8 *)(v0 + 0) = a1 */
/*
 * CdPosToInt - minute/second/frame (BCD) to absolute sector number
 * Purpose:   int CdPosToInt(CdlLOC *p): ((bcd(p[0]) * 60 + bcd(p[1])) * 75 + bcd(p[2])) - 150. Leaf.
 */
	.globl	CdPosToInt
CdPosToInt:
	lbu	v1, 0(a0)	/* v1 = *(u8 *)(a0 + 0) */
	lbu	a2, 1(a0)	/* a2 = *(u8 *)(a0 + 1) */
	srl	a1, v1, 4	/* a1 = v1 >> 4 */
	sll	v0, a1, 2	/* v0 = a1 << 2 */
	addu	v0, v0, a1	/* v0 = v0 + a1 */
	sll	v0, v0, 1	/* v0 = v0 << 1 */
	andi	v1, v1, 0xf	/* v1 = v1 & 0xf */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	a1, v0, 4	/* a1 = v0 << 4 */
	subu	a1, a1, v0	/* a1 = a1 - v0 */
	sll	a1, a1, 2	/* a1 = a1 << 2 */
	srl	v1, a2, 4	/* v1 = a2 >> 4 */
	sll	v0, v1, 2	/* v0 = v1 << 2 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 1	/* v0 = v0 << 1 */
	andi	a2, a2, 0xf	/* a2 = a2 & 0xf */
	addu	v0, v0, a2	/* v0 = v0 + a2 */
	addu	a1, a1, v0	/* a1 = a1 + v0 */
	sll	v1, a1, 2	/* v1 = a1 << 2 */
	addu	v1, v1, a1	/* v1 = v1 + a1 */
	sll	v0, v1, 4	/* v0 = v1 << 4 */
	lbu	a1, 2(a0)	/* a1 = *(u8 *)(a0 + 2) */
	subu	v0, v0, v1	/* v0 = v0 - v1 */
	srl	a0, a1, 4	/* a0 = a1 >> 4 */
	sll	v1, a0, 2	/* v1 = a0 << 2 */
	addu	v1, v1, a0	/* v1 = v1 + a0 */
	sll	v1, v1, 1	/* v1 = v1 << 1 */
	andi	a1, a1, 0xf	/* a1 = a1 & 0xf */
	addu	v1, v1, a1	/* v1 = v1 + a1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	jr	ra	/* return */
	addiu	v0, v0, -150	/* [delay slot, real work] v0 = v0 - 150 */
