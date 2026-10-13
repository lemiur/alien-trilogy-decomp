/*
 * libcd_bios.s - LIBCD module BIOS: the CD-ROM controller driver (command, interrupt and sector-transfer layer).
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library module that retail
 * links as a rebuild matching no archive in ../sdk. Assembled by tools/build_asm.py (CPPPSX, then ASPSX 2.34);
 * placed by config/libcd_bios.json and the "libcd_bios" entry of config/data.json; counted as "library
 * assembly", never as matched C and never as certified library.
 *
 * Library:   LIBCD.LIB (Sony Runtime Library, CD-ROM).   Module: BIOS (retail 0x8005daa8..0x8005f94b, 7844 bytes).
 * Exports (text): CD_sync, CD_ready, CD_cw, CD_vol, CD_shell, CD_flush, CD_init, CD_initvol, CD_readm, CD_readsync,
 *   CD_datasync, CD_getsector, CD_set_test_parmnum. Unexported routines keep address names: f_5daa8 (CD interrupt
 *   service), f_5ee60 (read start/restart), f_5f708 (CD interrupt callback), f_5f7e8 (read-ready callback).
 * Exports (data): CD_cbsync, CD_cbready, CD_cbread, CD_debug, CD_status, CD_nopen, CD_pos, CD_com, CD_comstr,
 *   CD_intstr (names from the 3.3 object's xdefs; the retail order is that of 3.3 without CD_status1).
 * Imports: ResetCallback, InterruptCallback, CheckCallback, VSync (LIBETC), printf, and the module's own .bss,
 *   which the survey places outside the library-assembly data ranges and which therefore stays an import:
 *   g_8f6c0..g_8f6e8 (retail 0x8008f6c0..0x8008f6eb, 44 bytes, described below).
 * Own data: .rdata 0x80012c80..0x80012ebf (576 bytes: command/interrupt names, messages, one switch table),
 *   .data 0x800744fc..0x80074827 (812 bytes: callbacks, status, name tables, per-command flag tables,
 *   hardware-register pointers, state words).
 *
 * Difference from the nearest archive. Retail BIOS is Runtime Library 3.3 BIOS code with small edits, linked
 * beside SYS/C_0xx modules of the 3.3-3.6 era; no archived LIBCD contains this exact module. Relocation-masked
 * word comparison of every routine against every archived LIBCD (analysis/data_survey/moddiff.py plus an
 * alignment of each routine):
 *   CD_sync, CD_ready, CD_datasync             identical to 3.3
 *   CD_vol, CD_initvol, f_5f708 (irq callback)  identical to 3.3, 3.4, 3.5 and 3.6
 *   CD_set_test_parmnum                        identical to every archived version from 2.6 on
 *   CD_flush                                   1 word differs from 3.3/3.5/3.6 (0x8005eb08, the COM_DELAY constant load)
 *   CD_init                                    1 word differs from 3.3 (0x8005ec64, the COM_DELAY constant load)
 *   CD_readsync                                2 words differ from 3.3 (0x8005f34c, 0x8005f488)
 *   CD_cw                                      3.3 has a 24-word block after the parameter loop (a jal and byte
 *                                              stores to the state bytes) that retail does not have
 *   CD_shell                                   3.3 counts the wait iterations and returns the count; retail returns 0
 *   CD_readm                                   3.3/3.4 additionally issue CdlPause (9) from a mode test; retail only
 *                                              stores the callbacks and starts f_5ee60
 *   CD_getsector                               retail programs the DMA registers through the pointer table at
 *                                              0x80074814; 25 of 54 words differ from 3.3/3.4
 *   f_5daa8, f_5ee60, f_5f7e8                  closest are 3.5 / 3.3 / 3.4, but 25-40 % of the words differ
 *   CD_initintr, CD_status1 (3.3 exports)      absent in retail
 * Each routine is transcribed exactly as retail has it.
 *
 * Conventions used by every routine (the compiled C kept its controller pointers in .data, so the code reloads
 * them with lui/lw pairs; the assembler macro "lw reg, symbol" produces those pairs):
 *   CDREG0 = *g_747bc[0] 0x1f801800  index/status register (bits 1:0 select the bank; bit5 = response FIFO not empty)
 *   CDREG1 = *g_747bc[1] 0x1f801801  command (write) / response FIFO (read)
 *   CDREG2 = *g_747bc[2] 0x1f801802  parameter FIFO (write) / interrupt enable (bank 1)
 *   CDREG3 = *g_747bc[3] 0x1f801803  request register (bank 0) / interrupt flag, write 7 to acknowledge (bank 1)
 * The controller interrupt (IRQ2) is serviced by f_5f708; the decoded state lives in these words:
 *   g_747d4  u8   interrupt number of the last INT read by f_5daa8
 *   g_747d5  u8   sync-channel code: 0 none, 3 Acknowledge, 2 Complete, 5 DiskError (CD_sync's result)
 *   g_747d6  u8   ready-channel code: 0 none, 1 DataReady, 4 DataEnd, 5 DiskError (CD_ready's result)
 *   g_747d7  u8   DataEnd latch (4) read by CD_ready
 *   g_8f6c0  8 B  raw response of the interrupt being decoded      g_8f6c8  8 B  sync-channel response
 *   g_8f6d0  8 B  ready-channel response                            g_8f6d8  8 B  DataEnd response
 *   g_8f6e0  word time-out deadline (VSync(-1) tick)  g_8f6e4 word spin counter  g_8f6e8 word routine-name string
 * Time-out guard (CD_sync, CD_ready, CD_cw, CD_readsync, CD_datasync): deadline = VSync(-1) + 480 ticks,
 * spin = 0, name = "CD_xxx"; every poll does "if (deadline < VSync(-1) || spin++ > 0x1e0000)" and then prints
 * "%s:(%s) timeout Sync=%s, Ready=%s" (name, CD_comstr[CD_com], CD_intstr[g_747d5], CD_intstr[g_747d6]),
 * calls CD_init() and returns -1.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.rdata
/*
 * BIOS read-only data. The strings are in descending command order because the compiler emitted the
 * name tables' strings in reverse; each string is a compiler label ($LCn) padded to a word.
 */
/*
 * command name "CdlReadS" (CD_comstr entry 0x1b)
 */
$LC0:
	.ascii	"CdlReadS\000"
	.align	2

/*
 * "CdlSeekP" (0x16)
 */
$LC1:
	.ascii	"CdlSeekP\000"
	.align	2

/*
 * "CdlSeekL" (0x15)
 */
$LC2:
	.ascii	"CdlSeekL\000"
	.align	2

/*
 * "CdlGetTD" (0x14)
 */
$LC3:
	.ascii	"CdlGetTD\000"
	.align	2

/*
 * "CdlGetTN" (0x13)
 */
$LC4:
	.ascii	"CdlGetTN\000"
	.align	2

/*
 * "CdlGetlocP" (0x11)
 */
$LC5:
	.ascii	"CdlGetlocP\000"
	.align	2

/*
 * "CdlGetlocL" (0x10)
 */
$LC6:
	.ascii	"CdlGetlocL\000"
	.align	2

/*
 * placeholder "?" used for every command/interrupt code that has no name
 */
$LC7:
	.ascii	"?\000"
	.align	2

/*
 * "CdlSetmode" (0x0e)
 */
$LC8:
	.ascii	"CdlSetmode\000"
	.align	2

/*
 * "CdlSetfilter" (0x0d)
 */
$LC9:
	.ascii	"CdlSetfilter\000"
	.align	2

/*
 * "CdlDemute" (0x0c)
 */
$LC10:
	.ascii	"CdlDemute\000"
	.align	2

/*
 * "CdlMute" (0x0b)
 */
$LC11:
	.ascii	"CdlMute\000"
	.align	2

/*
 * "CdlReset" (0x0a)
 */
$LC12:
	.ascii	"CdlReset\000"
	.align	2

/*
 * "CdlPause" (0x09)
 */
$LC13:
	.ascii	"CdlPause\000"
	.align	2

/*
 * "CdlStop" (0x08)
 */
$LC14:
	.ascii	"CdlStop\000"
	.align	2

/*
 * "CdlStandby" (0x07)
 */
$LC15:
	.ascii	"CdlStandby\000"
	.align	2

/*
 * "CdlReadN" (0x06)
 */
$LC16:
	.ascii	"CdlReadN\000"
	.align	2

/*
 * "CdlBackword" (0x05; the misspelling is in the retail string)
 */
$LC17:
	.ascii	"CdlBackword\000"
	.align	2

/*
 * "CdlForward" (0x04)
 */
$LC18:
	.ascii	"CdlForward\000"
	.align	2

/*
 * "CdlPlay" (0x03)
 */
$LC19:
	.ascii	"CdlPlay\000"
	.align	2

/*
 * "CdlSetloc" (0x02)
 */
$LC20:
	.ascii	"CdlSetloc\000"
	.align	2

/*
 * "CdlNop" (0x01)
 */
$LC21:
	.ascii	"CdlNop\000"
	.align	2

/*
 * "CdlSync" (0x00)
 */
$LC22:
	.ascii	"CdlSync\000"
	.align	2

/*
 * interrupt name "DiskError" (INT5)
 */
$LC23:
	.ascii	"DiskError\000"
	.align	2

/*
 * "DataEnd" (INT4)
 */
$LC24:
	.ascii	"DataEnd\000"
	.align	2

/*
 * "Acknowledge" (INT3)
 */
$LC25:
	.ascii	"Acknowledge\000"
	.align	2

/*
 * "Complete" (INT2)
 */
$LC26:
	.ascii	"Complete\000"
	.align	2

/*
 * "DataReady" (INT1)
 */
$LC27:
	.ascii	"DataReady\000"
	.align	2

/*
 * "NoIntr" (INT0)
 */
$LC28:
	.ascii	"NoIntr\000"
	.align	2

/*
 * printf format of the CD_sync/CD_ready/CD_readsync/CD_datasync time-out report (args: routine name, command, sync code, ready code)
 */
$LC29:
	.ascii	"%s:(%s) timeout Sync=%s, Ready=%s\n\000"
	.align	2

/*
 * printf format used by f_5daa8 when the drive reports an error (INT5)
 */
$LC30:
	.ascii	"%s: DiskError(%02x)\n\000"
	.align	2

/*
 * printf format used by f_5daa8 for an interrupt number outside 1..5
 */
$LC31:
	.ascii	"CDROM: unknown intr(%d)\n\000"
	.align	2

	.align	3
/*
 * Switch table of f_5daa8: one code address per interrupt number INT1..INT5 (index = INT - 1).
 * INT1 DataReady, INT2 Complete, INT3 Acknowledge, INT4 DataEnd, INT5 DiskError.
 * User: f_5daa8 (lui/addiu/addu/lw/jr at 0x8005dcb0).
 */
$LJT0:
	.word	$L5de3c, $L5ddec, $L5dccc, $L5de8c, $L5df24

/*
 * routine name "CD_sync" passed to the time-out report
 */
$LC32:
	.ascii	"CD_sync\000"
	.align	2

/*
 * routine name "CD_ready"
 */
$LC33:
	.ascii	"CD_ready\000"
	.align	2

/*
 * printf format of the CD_cw trace (CD_debug >= 2): command name
 */
$LC34:
	.ascii	"%s...\n\000"
	.align	2

/*
 * printf format: CD_cw refused a command that needs parameters
 */
$LC35:
	.ascii	"%s: no param\n\000"
	.align	2

/*
 * routine name "CD_cw"
 */
$LC36:
	.ascii	"CD_cw\000"
	.align	2

/*
 * printf format of the lid-open wait (CD_shell, CD_init, f_5ee60)
 */
$LC37:
	.ascii	"CD open(%02x)...\n\000"
	.align	2

/*
 * printf format printed by CD_init (address of the g_74800 state table)
 */
$LC38:
	.ascii	"CD_init:addr=%08x\n\000"
	.align	2

/*
 * printf format of the read-retry report in f_5ee60
 */
$LC39:
	.ascii	"CD read retry %2d(%02x:%02x:%02x)\n\000"
	.align	2

/*
 * routine name "CD_read" (CD_readsync time-out report)
 */
$LC40:
	.ascii	"CD_read\000"
	.align	2

/*
 * routine name "CD_datasync"
 */
$LC41:
	.ascii	"CD_datasync\000"
	.align	2

	.data
/*
 * CD_cbsync - user sync callback (CdSyncCallback); 0 = none. Users: CdSyncCallback, CD_sync, f_5f708.
 */
	.globl	CD_cbsync
CD_cbsync:
	.word	0

/*
 * CD_cbready - user ready callback (CdReadyCallback); 0 = none. Users: CdReadyCallback, CD_sync, f_5f708.
 */
	.globl	CD_cbready
CD_cbready:
	.word	0

/*
 * CD_cbread - user read-complete callback (CdReadCallback); 0 = none. Users: CdReadCallback, f_5f7e8.
 */
	.globl	CD_cbread
CD_cbread:
	.word	0

/*
 * CD_debug - trace level of CdSetDebug (0 quiet, 1 refusals, 2 every command). Users: CD_cw, CdSetDebug.
 */
	.globl	CD_debug
CD_debug:
	.word	0

/*
 * CD_status - last drive status byte (bit4 = lid open). Users: CdStatus, f_5daa8, CD_shell, CD_init.
 */
	.globl	CD_status
CD_status:
	.word	0

/*
 * CD_nopen - number of lid-open transitions seen by f_5daa8.
 */
	.globl	CD_nopen
CD_nopen:
	.word	0

/*
 * CD_pos - the last CdlSetloc position as four bytes {minute, second, frame, spare}; initial value 2. Users: CD_cw (writer), f_5ee60 (re-seek).
 */
	.globl	CD_pos
CD_pos:
	.word	2

/*
 * CD_com - code of the last command written by CD_cw. Users: CdLastCom, f_5daa8, CD_sync/CD_ready/CD_cw time-out reports.
 */
	.globl	CD_com
CD_com:
	.word	0

/*
 * CD_comstr - names of the commands, indexed by command code (0..0x1f); codes without a name point at "?". Users: CdComstr, f_5daa8, CD_cw and the time-out reports.
 */
	.globl	CD_comstr
CD_comstr:
	.word	$LC22, $LC21, $LC20, $LC19
	.word	$LC18, $LC17, $LC16, $LC15
	.word	$LC14, $LC13, $LC12, $LC11
	.word	$LC10, $LC9, $LC8, $LC7
	.word	$LC6, $LC5, $LC7, $LC4
	.word	$LC3, $LC2, $LC1, $LC7
	.word	$LC7, $LC7, $LC7, $LC0
	.word	$LC7, $LC7, $LC7, $LC7

/*
 * CD_intstr - names of the interrupt codes 0..7 (NoIntr, DataReady, Complete, Acknowledge, DataEnd, DiskError, ?, ?). Users: CdIntstr and the time-out reports.
 */
	.globl	CD_intstr
CD_intstr:
	.word	$LC28, $LC27, $LC26, $LC25
	.word	$LC24, $LC23, $LC7, $LC7

/*
 * g_745bc - per-command flag: 1 for commands that answer with a second (INT2 Complete) response after the INT3 acknowledge (Standby, Stop, Pause, Init, SeekL, SeekP, ...). Indexed by command code. User: f_5daa8 (INT3 case).
 */
g_745bc:
	.word	0, 0, 0, 0
	.word	0, 0, 0, 1
	.word	1, 1, 1, 0
	.word	0, 0, 0, 0
	.word	0, 0, 1, 0
	.word	0, 1, 1, 0
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0

/*
 * g_7463c - per-command flag: 1 if CD_cw must also clear the ready code g_747d6 before issuing the command. Indexed by command code. User: CD_cw.
 */
g_7463c:
	.word	0, 0, 0, 1
	.word	1, 1, 1, 0
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0
	.word	0, 1, 1, 0
	.word	0, 0, 0, 1
	.word	0, 0, 0, 0

/*
 * g_746bc - per-command flag: 1 if the first response byte is a drive status byte to latch into CD_status. Indexed by command code. User: f_5daa8.
 */
g_746bc:
	.word	1, 1, 1, 1
	.word	1, 1, 1, 1
	.word	1, 1, 1, 1
	.word	1, 1, 1, 1
	.word	0, 0, 1, 1
	.word	1, 1, 1, 1
	.word	1, 1, 1, 1
	.word	1, 1, 1, 1

/*
 * g_7473c - number of parameter bytes of each command (Setloc 3, Setfilter 2, Setmode 1, Test 0x19 set by CD_set_test_parmnum, ...). Users: CD_cw, CD_set_test_parmnum.
 */
g_7473c:
	.word	0, 0, 3, 0
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0
	.word	0, 2, 1, 0
	.word	0, 0, 1, 0
	.word	1, 0, 0, 0
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0

/*
 * g_747bc - pointers to the controller registers: CDREG0..CDREG3, COM_DELAY, SPU base. The comments name each word.
 */
g_747bc:
	.word	0x1f801800	/* CD-ROM index/status register (0x1f801800) */
	.word	0x1f801801	/* CD-ROM command/response register (0x1f801801) */
	.word	0x1f801802	/* CD-ROM parameter/data/interrupt-enable register (0x1f801802) */
	.word	0x1f801803	/* CD-ROM request/interrupt-flag register (0x1f801803) */
	.word	0x1f801020	/* COM_DELAY timing register (0x1f801020) */
	.word	0x1f801c00	/* SPU register base (0x1f801c00) */

/*
 * g_747d4 - four bytes: [0] last INT number, [1] sync code, [2] ready code, [3] DataEnd latch (see the module header).
 */
g_747d4:
	.word	0

/*
 * g_747d8 - read retries left (CD_readm sets 16).
 */
g_747d8:
	.word	0

/*
 * g_747dc - sector count of the running CD_readm.
 */
g_747dc:
	.word	0

/*
 * g_747e0 - destination buffer of the running CD_readm.
 */
g_747e0:
	.word	0

/*
 * g_747e4 - current write pointer (advanced by f_5f7e8, rewound by f_5ee60).
 */
g_747e4:
	.word	0

/*
 * g_747e8 - mode byte of the running read.
 */
g_747e8:
	.word	0

/*
 * g_747ec - words per sector for the running read (0x200, 0x249 or 0x246).
 */
g_747ec:
	.word	0

/*
 * g_747f0 - sectors still to read; -1 = failed/needs restart; 0 = done.
 */
g_747f0:
	.word	0

/*
 * g_747f4 - VSync tick after which a stalled read is restarted.
 */
g_747f4:
	.word	0

/*
 * g_747f8 - CD_cbsync saved by CD_readm.
 */
g_747f8:
	.word	0

/*
 * g_747fc - CD_cbready saved by CD_readm.
 */
g_747fc:
	.word	0

/*
 * g_74800 - table of pointers to the driver state (g_747d4, response buffer g_8f6c0, CD_com, CD_status, CD_pos); only its address is used (printed by CD_init).
 */
g_74800:
	.word	g_747d4	/* &g_747d4 */
	.word	g_8f6c0	/* &g_8f6c0 (.bss) */
	.word	CD_com	/* &CD_com */
	.word	CD_status	/* &CD_status */
	.word	CD_pos	/* &CD_pos */

/*
 * g_74814 - pointers to the registers CD_getsector programs.
 */
g_74814:
	.word	0x1f801018	/* CD-ROM delay/size register 0x1f801018 */
	.word	0x1f8010f0	/* DMA control register DPCR 0x1f8010f0 */
	.word	0x1f8010b0	/* DMA3 MADR 0x1f8010b0 */
	.word	0x1f8010b4	/* DMA3 BCR 0x1f8010b4 */
	.word	0x1f8010b8	/* DMA3 CHCR 0x1f8010b8 */

	.text

/*
 * f_5daa8 - CD interrupt service: fetch and classify one pending controller interrupt
 *
 * Purpose:   static int cd_intr(void) (unexported in 3.3 as well). Reads the pending interrupt number and
 *            the response bytes, updates CD_status/CD_nopen, files the response under the sync or ready
 *            channel and returns which channels fired.
 * Inputs:    none (reads CDREG0/CDREG3 and the response FIFO through the g_747bc pointers).
 * Outputs:   v0 = 0 no interrupt pending; 1 / 2 sync-channel event (Acknowledge / Complete); 2 = sync event;
 *            4 = ready event (DataReady, DataEnd); 6 = DiskError (both); -1 = unknown interrupt number.
 * Hardware:  CDREG0 index 1; CDREG3 low 3 bits = pending INT; response FIFO via CDREG1 (until CDREG0 bit5
 *            clears, at most 8 bytes); acknowledges with CDREG3 = 7 and re-enables with CDREG2 = 7.
 * Memory:    writes g_747d4..g_747d7, g_8f6c0..g_8f6d8, CD_status, CD_nopen; reads CD_com, CD_comstr,
 *            g_745bc, g_746bc, $LJT0.
 * Clobbers:  v0, v1, a0-a2, s0, s1 (saved). Frame 32 bytes.
 * Callers:   CD_sync, CD_ready, CD_cw, CD_readsync, f_5f708 (each loops on it).
 * Callees:   printf.
 */
	.globl	f_5daa8
f_5daa8:

	/* Entry: select register bank 1 and read the pending interrupt number */
	addiu	sp, sp, -32	/* allocate 32 byte frame */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sw	ra, 24(sp)	/* save ra */
	sw	s1, 20(sp)	/* save s1 */
	sw	s0, 16(sp)	/* save s0 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	la	v1, g_747d4	/* v1 = &g_747d4 */
	andi	v0, v0, 0x7	/* v0 = v0 & 0x7 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lbu	v0, 0(v1)	/* v0 = *(u8 *)(v1 + 0) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L5db00	/* if (v0 != 0) goto $L5db00 */
	addu	s1, zero, zero	/* [delay slot, real work] s1 = zero */
	j	$L5dfcc	/* goto $L5dfcc */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */

	/* Read the response FIFO into g_8f6c0[0..7] while CDREG0 bit5 says it is not empty */
$L5db00:
	addu	v1, zero, zero	/* v1 = zero */
$L5db04:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x20	/* v0 = v0 & 0x20 */
	beq	v0, zero, $L5db78	/* if (v0 == 0) goto $L5db78 */
	slti	v0, v1, 8	/* [delay slot, real work] v0 = v1 < 8 */
	lw	v0, g_747bc+4	/* v0 = CDREG1 pointer (g_747bc+4) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	.set	noat
	la	$1, g_8f6c0	/* $1 = &g_8f6c0 */
	addu	$1, $1, v1	/* $1 = $1 + v1 */
	sb	v0, 0($1)	/* *(u8 *)($1 + 0) = v0 */
	.set	at
	addiu	v1, v1, 1	/* v1 = v1 + 1 */
	slti	v0, v1, 8	/* v0 = v1 < 8 */
	beq	v0, zero, $L5db78	/* if (v0 == 0) goto $L5db78 */
	nop	/* delay slot: nothing */
	j	$L5db04	/* goto $L5db04 */
	nop	/* delay slot: nothing */

	/* Zero-fill the unread tail of g_8f6c0 */
$L5db60:
	.set	noat
	la	$1, g_8f6c0	/* $1 = &g_8f6c0 */
	addu	$1, $1, v1	/* $1 = $1 + v1 */
	sb	zero, 0($1)	/* *(u8 *)($1 + 0) = zero */
	.set	at
	addiu	v1, v1, 1	/* v1 = v1 + 1 */
	slti	v0, v1, 8	/* v0 = v1 < 8 */
$L5db78:
	bne	v0, zero, $L5db60	/* if (v0 != 0) goto $L5db60 */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */

	/* Acknowledge every pending INT (CDREG3 = 7) and re-enable all three INT sources (CDREG2 = 7) */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	ori	v1, zero, 0x7	/* v1 = 7 */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	lw	v0, g_747bc+8	/* v0 = CDREG2 pointer (g_747bc+8) */
	nop	/* load delay slot: nothing */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */

	/* If this command returns a status byte (g_746bc[CD_com]): count lid-open transitions into CD_nopen and latch CD_status; s1 = error bits (status & 0x1d) */
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	nop	/* load delay slot: nothing */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	.set	noat
	la	$1, g_746bc	/* $1 = &g_746bc */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	v0, 0($1)	/* v0 = *(word *)($1 + 0) */
	.set	at
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5dc48	/* if (v0 == 0) goto $L5dc48 */
	nop	/* delay slot: nothing */
	lw	v0, CD_status	/* v0 = CD_status (word) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x10	/* v0 = v0 & 0x10 */
	bne	v0, zero, $L5dc28	/* if (v0 != 0) goto $L5dc28 */
	nop	/* delay slot: nothing */
	la	v0, g_8f6c0	/* v0 = &g_8f6c0 */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x10	/* v0 = v0 & 0x10 */
	beq	v0, zero, $L5dc28	/* if (v0 == 0) goto $L5dc28 */
	nop	/* delay slot: nothing */
	lw	v0, CD_nopen	/* v0 = CD_nopen (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, CD_nopen	/* CD_nopen = v0 */
$L5dc28:
	la	v0, g_8f6c0	/* v0 = &g_8f6c0 */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0xff	/* v0 = v0 & 0xff */
	andi	s1, v0, 0x1d	/* s1 = v0 & 0x1d */
	sw	v0, CD_status	/* CD_status = v0 */

	/* INT5 (DiskError): report "%s: DiskError(%02x)" with the command name and CD_status */
$L5dc48:
	la	s0, g_747d4	/* s0 = &g_747d4 */
	lbu	v0, 0(s0)	/* v0 = *(u8 *)(s0 + 0) */
	ori	v1, zero, 0x5	/* v1 = 5 */
	andi	v0, v0, 0xff	/* v0 = v0 & 0xff */
	bne	v0, v1, $L5dc98	/* if (v0 != v1) goto $L5dc98 */
	nop	/* delay slot: nothing */
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	lw	a2, CD_status	/* a2 = CD_status (word) */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	.set	noat
	la	$1, CD_comstr	/* $1 = &CD_comstr */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	a1, 0($1)	/* a1 = *(word *)($1 + 0) */
	.set	at
	la	a0, $LC30	/* a0 = &$LC30 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */

	/* Dispatch on INT1..INT5 through the switch table $LJT0; anything else prints "CDROM: unknown intr(%d)" and returns -1 */
$L5dc98:
	lbu	v0, 0(s0)	/* v0 = *(u8 *)(s0 + 0) */
	nop	/* load delay slot: nothing */
	addiu	v1, v0, -1	/* v1 = v0 - 1 */
	sltiu	v0, v1, 5	/* v0 = v1 < 5 */
	beq	v0, zero, $L5dfb4	/* if (v0 == 0) goto $L5dfb4 */
	sll	v0, v1, 2	/* [delay slot, real work] v0 = v1 << 2 */
	.set	noat
	la	$1, $LJT0	/* $1 = &$LJT0 */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	v0, 0($1)	/* v0 = *(word *)($1 + 0) */
	.set	at
	nop	/* load delay slot: nothing */
	jr	v0	/* jump through v0 */
	nop	/* delay slot: nothing */

	/* INT3 Acknowledge: errors -> g_747d5 = 5; if the command also has a second response (g_745bc[CD_com]) and no sync code is pending -> g_747d5 = 3, else 2; copy response to g_8f6c8 */
$L5dccc:
	beq	s1, zero, $L5dd1c	/* if (s1 == 0) goto $L5dd1c */
	ori	v1, zero, 0x5	/* [delay slot, real work] v1 = 5 */
	la	v0, g_747d4+1	/* v0 = &g_747d4+1 */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	la	v0, g_8f6c8	/* v0 = &g_8f6c8 */
	addu	v1, v0, zero	/* v1 = v0 */
	beq	v1, zero, $L5de34	/* if (v1 == 0) goto $L5de34 */
	addiu	a1, v1, -8	/* [delay slot, real work] a1 = v1 - 8 */
	ori	a0, zero, 0x7	/* a0 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5dcfc:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	a0, a0, -1	/* a0 = a0 - 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	bne	a0, a2, $L5dcfc	/* if (a0 != a2) goto $L5dcfc */
	addiu	v1, v1, 1	/* [delay slot, real work] v1 = v1 + 1 */
	j	$L5dfcc	/* goto $L5dfcc */
	ori	v0, zero, 0x2	/* [delay slot, real work] v0 = 2 */
$L5dd1c:
	la	v1, g_747d4+1	/* v1 = &g_747d4+1 */
	lbu	v0, 0(v1)	/* v0 = *(u8 *)(v1 + 0) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L5dda0	/* if (v0 != 0) goto $L5dda0 */
	nop	/* delay slot: nothing */
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	nop	/* load delay slot: nothing */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	.set	noat
	la	$1, g_745bc	/* $1 = &g_745bc */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	v0, 0($1)	/* v0 = *(word *)($1 + 0) */
	.set	at
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5dda0	/* if (v0 == 0) goto $L5dda0 */
	ori	v0, zero, 0x3	/* [delay slot, real work] v0 = 3 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	la	v0, g_8f6c8	/* v0 = &g_8f6c8 */
	addu	v1, v0, zero	/* v1 = v0 */
	beq	v1, zero, $L5dd98	/* if (v1 == 0) goto $L5dd98 */
	addiu	a1, v1, -8	/* [delay slot, real work] a1 = v1 - 8 */
	ori	a0, zero, 0x7	/* a0 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5dd80:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	a0, a0, -1	/* a0 = a0 - 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	bne	a0, a2, $L5dd80	/* if (a0 != a2) goto $L5dd80 */
	addiu	v1, v1, 1	/* [delay slot, real work] v1 = v1 + 1 */
$L5dd98:
	j	$L5dfcc	/* goto $L5dfcc */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */
$L5dda0:
	la	v0, g_747d4+1	/* v0 = &g_747d4+1 */
	ori	v1, zero, 0x2	/* v1 = 2 */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	la	v0, g_8f6c8	/* v0 = &g_8f6c8 */
	addu	v1, v0, zero	/* v1 = v0 */
	beq	v1, zero, $L5de34	/* if (v1 == 0) goto $L5de34 */
	addiu	a1, v1, -8	/* [delay slot, real work] a1 = v1 - 8 */
	ori	a0, zero, 0x7	/* a0 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5ddcc:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	a0, a0, -1	/* a0 = a0 - 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	bne	a0, a2, $L5ddcc	/* if (a0 != a2) goto $L5ddcc */
	addiu	v1, v1, 1	/* [delay slot, real work] v1 = v1 + 1 */
	j	$L5dfcc	/* goto $L5dfcc */
	ori	v0, zero, 0x2	/* [delay slot, real work] v0 = 2 */

	/* INT2 Complete: g_747d5 = 5 on error else 2; copy response to g_8f6c8; return 2 */
$L5ddec:
	beq	s1, zero, $L5ddf8	/* if (s1 == 0) goto $L5ddf8 */
	ori	v0, zero, 0x2	/* [delay slot, real work] v0 = 2 */
	ori	v0, zero, 0x5	/* v0 = 5 */
$L5ddf8:
	sb	v0, g_747d4+1	/* g_747d4+1 <- v0 */
	la	v0, g_8f6c8	/* v0 = &g_8f6c8 */
	addu	v1, v0, zero	/* v1 = v0 */
	beq	v1, zero, $L5de34	/* if (v1 == 0) goto $L5de34 */
	addiu	a1, v1, -8	/* [delay slot, real work] a1 = v1 - 8 */
	ori	a0, zero, 0x7	/* a0 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5de1c:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	a0, a0, -1	/* a0 = a0 - 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	bne	a0, a2, $L5de1c	/* if (a0 != a2) goto $L5de1c */
	addiu	v1, v1, 1	/* [delay slot, real work] v1 = v1 + 1 */
$L5de34:
	j	$L5dfcc	/* goto $L5dfcc */
	ori	v0, zero, 0x2	/* [delay slot, real work] v0 = 2 */

	/* INT1 DataReady: g_747d6 = 5 on error else 1; copy response to g_8f6d0; return 4 */
$L5de3c:
	beq	s1, zero, $L5de48	/* if (s1 == 0) goto $L5de48 */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */
	ori	v0, zero, 0x5	/* v0 = 5 */
$L5de48:
	sb	v0, g_747d4+2	/* g_747d4+2 <- v0 */
	la	v0, g_8f6d0	/* v0 = &g_8f6d0 */
	addu	v1, v0, zero	/* v1 = v0 */
	beq	v1, zero, $L5df1c	/* if (v1 == 0) goto $L5df1c */
	addiu	a1, v1, -16	/* [delay slot, real work] a1 = v1 - 16 */
	ori	a0, zero, 0x7	/* a0 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5de6c:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	a0, a0, -1	/* a0 = a0 - 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	bne	a0, a2, $L5de6c	/* if (a0 != a2) goto $L5de6c */
	addiu	v1, v1, 1	/* [delay slot, real work] v1 = v1 + 1 */
	j	$L5dfcc	/* goto $L5dfcc */
	ori	v0, zero, 0x4	/* [delay slot, real work] v0 = 4 */

	/* INT4 DataEnd: g_747d7 = g_747d6 = 4; copy response to g_8f6d8 and g_8f6d0; return 4 */
$L5de8c:
	la	v0, g_8f6d8	/* v0 = &g_8f6d8 */
	addu	a1, v0, zero	/* a1 = v0 */
	addiu	a2, a1, -24	/* a2 = a1 - 24 */
	ori	a0, zero, 0x4	/* a0 = 4 */
	sb	a0, g_747d4+3	/* g_747d4+3 <- a0 */
	lbu	v1, g_747d4+3	/* v1 = g_747d4+3 (u8) */
	la	v0, g_747d4+2	/* v0 = &g_747d4+2 */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	sb	a0, g_747d4+3	/* g_747d4+3 <- a0 */
	beq	a1, zero, $L5dee8	/* if (a1 == 0) goto $L5dee8 */
	ori	v1, zero, 0x7	/* [delay slot, real work] v1 = 7 */
	addiu	a0, zero, -1	/* a0 = -1 */
$L5ded0:
	lbu	v0, 0(a2)	/* v0 = *(u8 *)(a2 + 0) */
	addiu	a2, a2, 1	/* a2 = a2 + 1 */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */
	sb	v0, 0(a1)	/* *(u8 *)(a1 + 0) = v0 */
	bne	v1, a0, $L5ded0	/* if (v1 != a0) goto $L5ded0 */
	addiu	a1, a1, 1	/* [delay slot, real work] a1 = a1 + 1 */
$L5dee8:
	la	v0, g_8f6d0	/* v0 = &g_8f6d0 */
	addu	v1, v0, zero	/* v1 = v0 */
	beq	v1, zero, $L5df1c	/* if (v1 == 0) goto $L5df1c */
	addiu	a1, v1, -16	/* [delay slot, real work] a1 = v1 - 16 */
	ori	a0, zero, 0x7	/* a0 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5df04:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	a0, a0, -1	/* a0 = a0 - 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	bne	a0, a2, $L5df04	/* if (a0 != a2) goto $L5df04 */
	addiu	v1, v1, 1	/* [delay slot, real work] v1 = v1 + 1 */
$L5df1c:
	j	$L5dfcc	/* goto $L5dfcc */
	ori	v0, zero, 0x4	/* [delay slot, real work] v0 = 4 */

	/* INT5 DiskError: g_747d6 = g_747d5 = 5; copy response to g_8f6c8 and g_8f6d0; return 6 */
$L5df24:
	la	v0, g_8f6c8	/* v0 = &g_8f6c8 */
	addu	a0, v0, zero	/* a0 = v0 */
	addiu	a1, a0, -8	/* a1 = a0 - 8 */
	ori	v0, zero, 0x5	/* v0 = 5 */
	sb	v0, g_747d4+2	/* g_747d4+2 <- v0 */
	lbu	v1, g_747d4+2	/* v1 = g_747d4+2 (u8) */
	la	v0, g_747d4+1	/* v0 = &g_747d4+1 */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	beq	a0, zero, $L5df78	/* if (a0 == 0) goto $L5df78 */
	ori	v1, zero, 0x7	/* [delay slot, real work] v1 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5df60:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */
	sb	v0, 0(a0)	/* *(u8 *)(a0 + 0) = v0 */
	bne	v1, a2, $L5df60	/* if (v1 != a2) goto $L5df60 */
	addiu	a0, a0, 1	/* [delay slot, real work] a0 = a0 + 1 */
$L5df78:
	la	v0, g_8f6d0	/* v0 = &g_8f6d0 */
	addu	v1, v0, zero	/* v1 = v0 */
	beq	v1, zero, $L5dfac	/* if (v1 == 0) goto $L5dfac */
	addiu	a1, v1, -16	/* [delay slot, real work] a1 = v1 - 16 */
	ori	a0, zero, 0x7	/* a0 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5df94:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	a0, a0, -1	/* a0 = a0 - 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	bne	a0, a2, $L5df94	/* if (a0 != a2) goto $L5df94 */
	addiu	v1, v1, 1	/* [delay slot, real work] v1 = v1 + 1 */
$L5dfac:
	j	$L5dfcc	/* goto $L5dfcc */
	ori	v0, zero, 0x6	/* [delay slot, real work] v0 = 6 */

	/* Unknown interrupt number */
$L5dfb4:
	lbu	a1, 0(s0)	/* a1 = *(u8 *)(s0 + 0) */
	la	a0, $LC31	/* a0 = &$LC31 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	addiu	v0, zero, -1	/* v0 = -1 */

	/* Common return */
$L5dfcc:
	lw	ra, 24(sp)	/* restore ra */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 32	/* release 32 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_sync - wait for (or poll) the end of the current CD command
 *
 * Purpose:   int CD_sync(int mode, u_char *result); CdSync() calls it. mode 0 blocks until the sync channel
 *            reports Complete (2) or DiskError (5); non-zero polls once.
 * Inputs:    a0 = mode, a1 = result buffer (8 bytes) or NULL.
 * Outputs:   v0 = 2 or 5 (the sync code, result copied from g_8f6c8, g_747d5 reset to 2); 0 = nothing yet
 *            (mode != 0); -1 = time-out (after CD_init).
 * Memory:    g_747d5, g_8f6c8, g_8f6e0..g_8f6e8, CD_cbsync, CD_cbready.
 * Clobbers:  v0, v1, a0-a3, s0-s6 (saved). Frame 56 bytes.
 * Callers:   CdSync, CD_cw, CD_init, CdControlB. Callees: VSync, printf, CD_init, CheckCallback, f_5daa8,
 *            CD_cbready/CD_cbsync (user callbacks).
 */
	.globl	CD_sync
CD_sync:
	addiu	sp, sp, -56	/* allocate 56 byte frame */
	sw	s5, 44(sp)	/* save s5 */
	addu	s5, a0, zero	/* s5 = a0 */
	sw	s6, 48(sp)	/* save s6 */
	addu	s6, a1, zero	/* s6 = a1 */

	/* Arm the time-out guard: deadline = VSync(-1) + 0x1e0, spin counter = 0, name = "CD_sync" */
	addiu	a0, zero, -1	/* a0 = -1 */
	sw	ra, 52(sp)	/* save ra */
	sw	s4, 40(sp)	/* save s4 */
	sw	s3, 36(sp)	/* save s3 */
	sw	s2, 32(sp)	/* save s2 */
	sw	s1, 28(sp)	/* save s1 */
	jal	VSync	/* call VSync */
	sw	s0, 24(sp)	/* [delay slot, real work] save s0 */
	la	s3, CD_intstr	/* s3 = &CD_intstr */
	la	s2, g_747d4+1	/* s2 = &g_747d4+1 */
	addiu	s4, s2, 1	/* s4 = s2 + 1 */
	addiu	v0, v0, 480	/* v0 = v0 + 480 */
	sw	v0, g_8f6e0	/* g_8f6e0 = v0 */
	la	v0, $LC32	/* v0 = &$LC32 */
	sw	zero, g_8f6e4	/* g_8f6e4 = zero */
	sw	v0, g_8f6e8	/* g_8f6e8 = v0 */

	/* Poll loop: check the time-out guard first */
$L5e050:
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* [delay slot, real work] a0 = -1 */
	lw	v1, g_8f6e0	/* v1 = g_8f6e0 (word) */
	nop	/* load delay slot: nothing */
	slt	v1, v1, v0	/* v1 = v1 < v0 */
	bne	v1, zero, $L5e098	/* if (v1 != 0) goto $L5e098 */
	nop	/* delay slot: nothing */
	lw	v1, g_8f6e4	/* v1 = g_8f6e4 (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v1, 1	/* v0 = v1 + 1 */
	sw	v0, g_8f6e4	/* g_8f6e4 = v0 */
	lui	v0, 0x1e	/* v0 = 0x1e0000 */
	slt	v0, v0, v1	/* v0 = v0 < v1 */
	beq	v0, zero, $L5e100	/* if (v0 == 0) goto $L5e100 */
	nop	/* delay slot: nothing */

	/* Time-out: print the report and re-initialise the controller (CD_init); return -1 */
$L5e098:
	lbu	a0, 0(s2)	/* a0 = *(u8 *)(s2 + 0) */
	lbu	v0, 1(s2)	/* v0 = *(u8 *)(s2 + 1) */
	lw	a1, g_8f6e8	/* a1 = g_8f6e8 (word) */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	addu	v0, v0, s3	/* v0 = v0 + s3 */
	sll	a0, a0, 2	/* a0 = a0 << 2 */
	lw	v1, 0(v0)	/* v1 = *(word *)(v0 + 0) */
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	addu	a0, a0, s3	/* a0 = a0 + s3 */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	sw	v1, 16(sp)	/* spill v1 to sp+16 */
	.set	noat
	la	$1, CD_comstr	/* $1 = &CD_comstr */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	a2, 0($1)	/* a2 = *(word *)($1 + 0) */
	.set	at
	lw	a3, 0(a0)	/* a3 = *(word *)(a0 + 0) */
	la	a0, $LC29	/* a0 = &$LC29 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	jal	CD_init	/* call CD_init */
	nop	/* delay slot: nothing */
	j	$L5e104	/* goto $L5e104 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
$L5e100:
	addu	v0, zero, zero	/* v0 = zero */

	/* CheckCallback() != 0 means the caller runs inside an interrupt callback, so IRQ2 cannot be delivered: service the controller by hand */
$L5e104:
	bne	v0, zero, $L5e22c	/* if (v0 != 0) goto $L5e22c */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
	jal	CheckCallback	/* call CheckCallback */
	nop	/* delay slot: nothing */
	beq	v0, zero, $L5e1c4	/* if (v0 == 0) goto $L5e1c4 */
	nop	/* delay slot: nothing */
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	s1, v0, 0x3	/* s1 = v0 & 0x3 */

	/* Drain loop: call f_5daa8 until it reports nothing; ready events (bit 4) go to CD_cbready(g_747d6, g_8f6d0), sync events (bit 2) to CD_cbsync(g_747d5, g_8f6c8) */
$L5e134:
	jal	f_5daa8	/* call f_5daa8 */
	nop	/* delay slot: nothing */
	addu	s0, v0, zero	/* s0 = v0 */
	beq	s0, zero, $L5e1b4	/* if (s0 == 0) goto $L5e1b4 */
	andi	v0, s0, 0x4	/* [delay slot, real work] v0 = s0 & 0x4 */
	beq	v0, zero, $L5e17c	/* if (v0 == 0) goto $L5e17c */
	andi	v0, s0, 0x2	/* [delay slot, real work] v0 = s0 & 0x2 */
	lw	v0, CD_cbready	/* v0 = CD_cbready (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5e178	/* if (v0 == 0) goto $L5e178 */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s4)	/* a0 = *(u8 *)(s4 + 0) */
	la	a1, g_8f6d0	/* a1 = &g_8f6d0 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
$L5e178:
	andi	v0, s0, 0x2	/* v0 = s0 & 0x2 */
$L5e17c:
	beq	v0, zero, $L5e134	/* if (v0 == 0) goto $L5e134 */
	nop	/* delay slot: nothing */
	lw	v0, CD_cbsync	/* v0 = CD_cbsync (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5e134	/* if (v0 == 0) goto $L5e134 */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s2)	/* a0 = *(u8 *)(s2 + 0) */
	la	a1, g_8f6c8	/* a1 = &g_8f6c8 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
	j	$L5e134	/* goto $L5e134 */
	nop	/* delay slot: nothing */

	/* Restore the CDREG0 bank that was selected on entry */
$L5e1b4:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	s1, 0(v0)	/* *(u8 *)(v0 + 0) = s1 */

	/* If the sync code is 2 (Complete) or 5 (DiskError): mark it consumed (2), copy g_8f6c8 into *result (8 bytes) and return the code */
$L5e1c4:
	lbu	v0, 0(s2)	/* v0 = *(u8 *)(s2 + 0) */
	nop	/* load delay slot: nothing */
	andi	a2, v0, 0xff	/* a2 = v0 & 0xff */
	ori	v0, zero, 0x2	/* v0 = 2 */
	beq	a2, v0, $L5e1e4	/* if (a2 == v0) goto $L5e1e4 */
	ori	v0, zero, 0x5	/* [delay slot, real work] v0 = 5 */
	bne	a2, v0, $L5e224	/* if (a2 != v0) goto $L5e224 */
	nop	/* delay slot: nothing */
$L5e1e4:
	ori	v0, zero, 0x2	/* v0 = 2 */
	sb	v0, 0(s2)	/* *(u8 *)(s2 + 0) = v0 */
	addu	a1, s6, zero	/* a1 = s6 */
	la	a0, g_8f6c8	/* a0 = &g_8f6c8 */
	beq	a1, zero, $L5e21c	/* if (a1 == 0) goto $L5e21c */
	ori	v1, zero, 0x7	/* [delay slot, real work] v1 = 7 */
	addiu	a3, zero, -1	/* a3 = -1 */
$L5e204:
	lbu	v0, 0(a0)	/* v0 = *(u8 *)(a0 + 0) */
	addiu	a0, a0, 1	/* a0 = a0 + 1 */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */
	sb	v0, 0(a1)	/* *(u8 *)(a1 + 0) = v0 */
	bne	v1, a3, $L5e204	/* if (v1 != a3) goto $L5e204 */
	addiu	a1, a1, 1	/* [delay slot, real work] a1 = a1 + 1 */
$L5e21c:
	j	$L5e22c	/* goto $L5e22c */
	addu	v0, a2, zero	/* [delay slot, real work] v0 = a2 */

	/* Otherwise loop again when blocking (mode == 0), else return 0 */
$L5e224:
	beq	s5, zero, $L5e050	/* if (s5 == 0) goto $L5e050 */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */
$L5e22c:
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
/*
 * CD_ready - wait for (or poll) the ready channel (sector data / data end)
 *
 * Purpose:   int CD_ready(int mode, u_char *result); CdReady() calls it. Same structure as CD_sync but it
 *            consumes g_747d7 (DataEnd) and g_747d6 (DataReady/DiskError) instead of the sync code.
 * Inputs:    a0 = mode (0 blocks), a1 = result buffer or NULL.
 * Outputs:   v0 = ready code (1 DataReady, 4 DataEnd, 5 DiskError), result copied from g_8f6d8 (DataEnd) or
 *            g_8f6d0; 0 = nothing (mode != 0); -1 = time-out.
 * Memory:    g_747d6, g_747d7, g_8f6d0, g_8f6d8, time-out words ("CD_ready").
 * Callers:   CdReady (and so StCdInterrupt). Callees as CD_sync.
 */
	.globl	CD_ready
CD_ready:
	addiu	sp, sp, -64	/* allocate 64 byte frame */
	sw	s7, 52(sp)	/* save s7 */
	addu	s7, a0, zero	/* s7 = a0 */
	sw	s4, 40(sp)	/* save s4 */
	addu	s4, a1, zero	/* s4 = a1 */
	addiu	a0, zero, -1	/* a0 = -1 */
	sw	ra, 56(sp)	/* save ra */
	sw	s6, 48(sp)	/* save s6 */
	sw	s5, 44(sp)	/* save s5 */
	sw	s3, 36(sp)	/* save s3 */
	sw	s2, 32(sp)	/* save s2 */
	sw	s1, 28(sp)	/* save s1 */
	jal	VSync	/* call VSync */
	sw	s0, 24(sp)	/* [delay slot, real work] save s0 */

	/* Arm the time-out guard with the name "CD_ready" */
	la	s5, CD_intstr	/* s5 = &CD_intstr */
	la	s2, g_747d4+1	/* s2 = &g_747d4+1 */
	addiu	s6, s2, 1	/* s6 = s2 + 1 */
	addiu	s3, s2, 2	/* s3 = s2 + 2 */
	addiu	v0, v0, 480	/* v0 = v0 + 480 */
	sw	v0, g_8f6e0	/* g_8f6e0 = v0 */
	la	v0, $LC33	/* v0 = &$LC33 */
	sw	zero, g_8f6e4	/* g_8f6e4 = zero */
	sw	v0, g_8f6e8	/* g_8f6e8 = v0 */

	/* Poll loop (time-out guard, interrupt drain) exactly as in CD_sync */
$L5e2cc:
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* [delay slot, real work] a0 = -1 */
	lw	v1, g_8f6e0	/* v1 = g_8f6e0 (word) */
	nop	/* load delay slot: nothing */
	slt	v1, v1, v0	/* v1 = v1 < v0 */
	bne	v1, zero, $L5e314	/* if (v1 != 0) goto $L5e314 */
	nop	/* delay slot: nothing */
	lw	v1, g_8f6e4	/* v1 = g_8f6e4 (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v1, 1	/* v0 = v1 + 1 */
	sw	v0, g_8f6e4	/* g_8f6e4 = v0 */
	lui	v0, 0x1e	/* v0 = 0x1e0000 */
	slt	v0, v0, v1	/* v0 = v0 < v1 */
	beq	v0, zero, $L5e37c	/* if (v0 == 0) goto $L5e37c */
	nop	/* delay slot: nothing */
$L5e314:
	lbu	a0, 0(s2)	/* a0 = *(u8 *)(s2 + 0) */
	lbu	v0, 1(s2)	/* v0 = *(u8 *)(s2 + 1) */
	lw	a1, g_8f6e8	/* a1 = g_8f6e8 (word) */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	addu	v0, v0, s5	/* v0 = v0 + s5 */
	sll	a0, a0, 2	/* a0 = a0 << 2 */
	lw	v1, 0(v0)	/* v1 = *(word *)(v0 + 0) */
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	addu	a0, a0, s5	/* a0 = a0 + s5 */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	sw	v1, 16(sp)	/* spill v1 to sp+16 */
	.set	noat
	la	$1, CD_comstr	/* $1 = &CD_comstr */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	a2, 0($1)	/* a2 = *(word *)($1 + 0) */
	.set	at
	lw	a3, 0(a0)	/* a3 = *(word *)(a0 + 0) */
	la	a0, $LC29	/* a0 = &$LC29 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	jal	CD_init	/* call CD_init */
	nop	/* delay slot: nothing */
	j	$L5e380	/* goto $L5e380 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
$L5e37c:
	addu	v0, zero, zero	/* v0 = zero */
$L5e380:
	bne	v0, zero, $L5e4e8	/* if (v0 != 0) goto $L5e4e8 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
	jal	CheckCallback	/* call CheckCallback */
	nop	/* delay slot: nothing */
	beq	v0, zero, $L5e440	/* if (v0 == 0) goto $L5e440 */
	nop	/* delay slot: nothing */
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	s1, v0, 0x3	/* s1 = v0 & 0x3 */
$L5e3b0:
	jal	f_5daa8	/* call f_5daa8 */
	nop	/* delay slot: nothing */
	addu	s0, v0, zero	/* s0 = v0 */
	beq	s0, zero, $L5e430	/* if (s0 == 0) goto $L5e430 */
	andi	v0, s0, 0x4	/* [delay slot, real work] v0 = s0 & 0x4 */
	beq	v0, zero, $L5e3f8	/* if (v0 == 0) goto $L5e3f8 */
	andi	v0, s0, 0x2	/* [delay slot, real work] v0 = s0 & 0x2 */
	lw	v0, CD_cbready	/* v0 = CD_cbready (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5e3f4	/* if (v0 == 0) goto $L5e3f4 */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s6)	/* a0 = *(u8 *)(s6 + 0) */
	la	a1, g_8f6d0	/* a1 = &g_8f6d0 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
$L5e3f4:
	andi	v0, s0, 0x2	/* v0 = s0 & 0x2 */
$L5e3f8:
	beq	v0, zero, $L5e3b0	/* if (v0 == 0) goto $L5e3b0 */
	nop	/* delay slot: nothing */
	lw	v0, CD_cbsync	/* v0 = CD_cbsync (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5e3b0	/* if (v0 == 0) goto $L5e3b0 */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s2)	/* a0 = *(u8 *)(s2 + 0) */
	la	a1, g_8f6c8	/* a1 = &g_8f6c8 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
	j	$L5e3b0	/* goto $L5e3b0 */
	nop	/* delay slot: nothing */
$L5e430:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	s1, 0(v0)	/* *(u8 *)(v0 + 0) = s1 */

	/* DataEnd latch g_747d7 set? clear it, copy g_8f6d8 to *result, return its code */
$L5e440:
	lbu	v0, 0(s3)	/* v0 = *(u8 *)(s3 + 0) */
	nop	/* load delay slot: nothing */
	andi	a2, v0, 0xff	/* a2 = v0 & 0xff */
	beq	a2, zero, $L5e490	/* if (a2 == 0) goto $L5e490 */
	nop	/* delay slot: nothing */
	sb	zero, 0(s3)	/* *(u8 *)(s3 + 0) = zero */
	la	a0, g_8f6d8	/* a0 = &g_8f6d8 */
	beq	s4, zero, $L5e4d8	/* if (s4 == 0) goto $L5e4d8 */
	addu	a1, s4, zero	/* [delay slot, real work] a1 = s4 */
	ori	v1, zero, 0x7	/* v1 = 7 */
	addiu	a3, zero, -1	/* a3 = -1 */
$L5e470:
	lbu	v0, 0(a0)	/* v0 = *(u8 *)(a0 + 0) */
	addiu	a0, a0, 1	/* a0 = a0 + 1 */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */
	sb	v0, 0(a1)	/* *(u8 *)(a1 + 0) = v0 */
	bne	v1, a3, $L5e470	/* if (v1 != a3) goto $L5e470 */
	addiu	a1, a1, 1	/* [delay slot, real work] a1 = a1 + 1 */
	j	$L5e4e8	/* goto $L5e4e8 */
	addu	v0, a2, zero	/* [delay slot, real work] v0 = a2 */

	/* Else ready code g_747d6 set? clear it, copy g_8f6d0 to *result, return its code */
$L5e490:
	lbu	v0, -1(s3)	/* v0 = *(u8 *)(s3 + -1) */
	nop	/* load delay slot: nothing */
	andi	a2, v0, 0xff	/* a2 = v0 & 0xff */
	beq	a2, zero, $L5e4e0	/* if (a2 == 0) goto $L5e4e0 */
	nop	/* delay slot: nothing */
	sb	zero, -1(s3)	/* *(u8 *)(s3 + -1) = zero */
	addu	a1, s4, zero	/* a1 = s4 */
	la	a0, g_8f6d0	/* a0 = &g_8f6d0 */
	beq	a1, zero, $L5e4d8	/* if (a1 == 0) goto $L5e4d8 */
	ori	v1, zero, 0x7	/* [delay slot, real work] v1 = 7 */
	addiu	a3, zero, -1	/* a3 = -1 */
$L5e4c0:
	lbu	v0, 0(a0)	/* v0 = *(u8 *)(a0 + 0) */
	addiu	a0, a0, 1	/* a0 = a0 + 1 */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */
	sb	v0, 0(a1)	/* *(u8 *)(a1 + 0) = v0 */
	bne	v1, a3, $L5e4c0	/* if (v1 != a3) goto $L5e4c0 */
	addiu	a1, a1, 1	/* [delay slot, real work] a1 = a1 + 1 */
$L5e4d8:
	j	$L5e4e8	/* goto $L5e4e8 */
	addu	v0, a2, zero	/* [delay slot, real work] v0 = a2 */

	/* Nothing yet: loop when blocking, else return 0 */
$L5e4e0:
	beq	s7, zero, $L5e2cc	/* if (s7 == 0) goto $L5e2cc */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */
$L5e4e8:
	lw	ra, 56(sp)	/* restore ra */
	lw	s7, 52(sp)	/* restore s7 */
	lw	s6, 48(sp)	/* restore s6 */
	lw	s5, 44(sp)	/* restore s5 */
	lw	s4, 40(sp)	/* restore s4 */
	lw	s3, 36(sp)	/* restore s3 */
	lw	s2, 32(sp)	/* restore s2 */
	lw	s1, 28(sp)	/* restore s1 */
	lw	s0, 24(sp)	/* restore s0 */
	addiu	sp, sp, 64	/* release 64 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_cw - send one command (with parameters) to the CD controller
 *
 * Purpose:   int CD_cw(u_char cmd, u_char *param, u_char *result, int nowait); the command layer under
 *            CdControl/CdControlF/CdControlB. Waits for the previous command (CD_sync), loads the parameter
 *            FIFO, writes the command, then (unless nowait) waits for the acknowledge.
 * Inputs:    a0 = command code (CdlNop 1 .. CdlReadS 0x1b), a1 = parameter bytes (NULL if none), a2 = result
 *            buffer or NULL, a3 = non-zero to return right after the command was written.
 * Outputs:   v0 = 0 ok; -1 = DiskError (g_747d5 == 5) or time-out; -2 = the command needs parameters but a1 is NULL.
 * Memory:    CD_debug (trace level), g_7473c (parameter count per command), g_7463c (clear the ready code),
 *            CD_pos (Setloc), CD_com, g_747d5/g_747d6, g_8f6c8.
 * Hardware:  CDREG0 = 0, CDREG2 <- parameters, CDREG1 <- command.
 * Clobbers:  v0, v1, a0-a3, s0-s5 (saved). Frame 72 bytes. The first two instructions (load of CD_debug)
 *            were scheduled ahead of the prologue, so callers jal to 0x8005e518.
 * Callers:   CdControl, CdControlF, CdControlB, CD_shell, CD_init, f_5ee60, f_5f7e8. Callees: printf, CD_sync, VSync.
 */
	.globl	CD_cw
CD_cw:
	lw	v0, CD_debug	/* v0 = CD_debug (word) */
	addiu	sp, sp, -72	/* allocate 72 byte frame */
	sw	s0, 40(sp)	/* save s0 */
	addu	s0, a1, zero	/* s0 = a1 */
	sw	s5, 60(sp)	/* save s5 */
	addu	s5, a2, zero	/* s5 = a2 */
	sw	s2, 48(sp)	/* save s2 */
	addu	s2, a3, zero	/* s2 = a3 */
	sw	s1, 44(sp)	/* save s1 */
	addu	s1, a0, zero	/* s1 = a0 */
	sw	ra, 64(sp)	/* save ra */
	sw	s4, 56(sp)	/* save s4 */

	/* Trace: with CD_debug >= 2 print "%s...\n" with the command name */
	slti	v0, v0, 2	/* v0 = v0 < 2 */
	bne	v0, zero, $L5e580	/* if (v0 != 0) goto $L5e580 */
	sw	s3, 52(sp)	/* [delay slot, real work] save s3 */
	andi	v0, s1, 0xff	/* v0 = s1 & 0xff */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	.set	noat
	la	$1, CD_comstr	/* $1 = &CD_comstr */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	a1, 0($1)	/* a1 = *(word *)($1 + 0) */
	.set	at
	la	a0, $LC34	/* a0 = &$LC34 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */

	/* Commands that take parameters (g_7473c[cmd] != 0) with a NULL buffer are refused (-2, message when CD_debug > 0) */
$L5e580:
	andi	v0, s1, 0xff	/* v0 = s1 & 0xff */
	sll	v1, v0, 2	/* v1 = v0 << 2 */
	.set	noat
	la	$1, g_7473c	/* $1 = &g_7473c */
	addu	$1, $1, v1	/* $1 = $1 + v1 */
	lw	v0, 0($1)	/* v0 = *(word *)($1 + 0) */
	.set	at
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5e5e8	/* if (v0 == 0) goto $L5e5e8 */
	nop	/* delay slot: nothing */
	bne	s0, zero, $L5e5e8	/* if (s0 != 0) goto $L5e5e8 */
	nop	/* delay slot: nothing */
	lw	v0, CD_debug	/* v0 = CD_debug (word) */
	nop	/* load delay slot: nothing */
	blez	v0, $L5e8fc	/* if (v0 <= 0) goto $L5e8fc */
	addiu	v0, zero, -2	/* [delay slot, real work] v0 = -2 */
	.set	noat
	la	$1, CD_comstr	/* $1 = &CD_comstr */
	addu	$1, $1, v1	/* $1 = $1 + v1 */
	lw	a1, 0($1)	/* a1 = *(word *)($1 + 0) */
	.set	at
	la	a0, $LC35	/* a0 = &$LC35 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	j	$L5e8fc	/* goto $L5e8fc */
	addiu	v0, zero, -2	/* [delay slot, real work] v0 = -2 */

	/* CdlSetloc (2): remember the four position bytes in CD_pos for later retries */
$L5e5e8:
	andi	v1, s1, 0xff	/* v1 = s1 & 0xff */
	ori	v0, zero, 0x2	/* v0 = 2 */
	bne	v1, v0, $L5e628	/* if (v1 != v0) goto $L5e628 */
	addu	a0, zero, zero	/* [delay slot, real work] a0 = zero */
	addu	v1, s0, zero	/* v1 = s0 */
$L5e5fc:
	lbu	v0, 0(v1)	/* v0 = *(u8 *)(v1 + 0) */
	nop	/* load delay slot: nothing */
	.set	noat
	la	$1, CD_pos	/* $1 = &CD_pos */
	addu	$1, $1, a0	/* $1 = $1 + a0 */
	sb	v0, 0($1)	/* *(u8 *)($1 + 0) = v0 */
	.set	at
	addiu	a0, a0, 1	/* a0 = a0 + 1 */
	slti	v0, a0, 4	/* v0 = a0 < 4 */
	bne	v0, zero, $L5e5fc	/* if (v0 != 0) goto $L5e5fc */
	addiu	v1, v1, 1	/* [delay slot, real work] v1 = v1 + 1 */
	addu	a0, zero, zero	/* a0 = zero */

	/* Wait for the previous command: CD_sync(0, 0) */
$L5e628:
	jal	CD_sync	/* call CD_sync */
	addu	a1, zero, zero	/* [delay slot, real work] a1 = zero */

	/* Reset the sync code; commands flagged in g_7463c also reset the ready code */
	la	v0, g_747d4+1	/* v0 = &g_747d4+1 */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	andi	v0, s1, 0xff	/* v0 = s1 & 0xff */
	sll	a0, v0, 2	/* a0 = v0 << 2 */
	.set	noat
	la	$1, g_7463c	/* $1 = &g_7463c */
	addu	$1, $1, a0	/* $1 = $1 + a0 */
	lw	v0, 0($1)	/* v0 = *(word *)($1 + 0) */
	.set	at
	la	v1, g_7463c	/* v1 = &g_7463c */
	beq	v0, zero, $L5e66c	/* if (v0 == 0) goto $L5e66c */
	nop	/* delay slot: nothing */
	sb	zero, g_747d4+2	/* g_747d4+2 <- zero */

	/* CDREG0 = 0 (bank 0); push g_7473c[cmd] parameter bytes into the parameter FIFO (CDREG2) */
$L5e66c:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	addiu	v0, v1, 256	/* v0 = v1 + 256 */
	addu	v1, a0, v0	/* v1 = a0 + v0 */
	lw	v0, 0(v1)	/* v0 = *(word *)(v1 + 0) */
	nop	/* load delay slot: nothing */
	blez	v0, $L5e6c0	/* if (v0 <= 0) goto $L5e6c0 */
	addu	a0, zero, zero	/* [delay slot, real work] a0 = zero */
	addu	a1, v1, zero	/* a1 = v1 */
$L5e698:
	lw	v1, g_747bc+8	/* v1 = CDREG2 pointer (g_747bc+8) */
	lbu	v0, 0(s0)	/* v0 = *(u8 *)(s0 + 0) */
	addiu	s0, s0, 1	/* s0 = s0 + 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v0, 0(a1)	/* v0 = *(word *)(a1 + 0) */
	addiu	a0, a0, 1	/* a0 = a0 + 1 */
	slt	v0, a0, v0	/* v0 = a0 < v0 */
	bne	v0, zero, $L5e698	/* if (v0 != 0) goto $L5e698 */
	nop	/* delay slot: nothing */

	/* Record the command in CD_com and write it to CDREG1 (this starts the command) */
$L5e6c0:
	lw	v0, g_747bc+4	/* v0 = CDREG1 pointer (g_747bc+4) */
	sb	s1, CD_com	/* CD_com <- s1 */
	sb	s1, 0(v0)	/* *(u8 *)(v0 + 0) = s1 */

	/* nowait: return 0 at once */
	bne	s2, zero, $L5e8fc	/* if (s2 != 0) goto $L5e8fc */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */

	/* Arm the time-out guard ("CD_cw") and wait for the acknowledge, same poll/drain structure as CD_sync */
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* [delay slot, real work] a0 = -1 */
	addiu	v0, v0, 480	/* v0 = v0 + 480 */
	la	a0, g_747d4+1	/* a0 = &g_747d4+1 */
	sw	v0, g_8f6e0	/* g_8f6e0 = v0 */
	sw	zero, g_8f6e4	/* g_8f6e4 = zero */
	lbu	v1, 0(a0)	/* v1 = *(u8 *)(a0 + 0) */
	la	v0, $LC36	/* v0 = &$LC36 */
	sw	v0, g_8f6e8	/* g_8f6e8 = v0 */
	bne	v1, zero, $L5e8b0	/* if (v1 != 0) goto $L5e8b0 */
	addu	a2, s5, zero	/* [delay slot, real work] a2 = s5 */
	la	s3, CD_intstr	/* s3 = &CD_intstr */
	addu	s2, a0, zero	/* s2 = a0 */
	addiu	s4, s2, 1	/* s4 = s2 + 1 */
$L5e72c:
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* [delay slot, real work] a0 = -1 */
	lw	v1, g_8f6e0	/* v1 = g_8f6e0 (word) */
	nop	/* load delay slot: nothing */
	slt	v1, v1, v0	/* v1 = v1 < v0 */
	bne	v1, zero, $L5e774	/* if (v1 != 0) goto $L5e774 */
	nop	/* delay slot: nothing */
	lw	v1, g_8f6e4	/* v1 = g_8f6e4 (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v1, 1	/* v0 = v1 + 1 */
	sw	v0, g_8f6e4	/* g_8f6e4 = v0 */
	lui	v0, 0x1e	/* v0 = 0x1e0000 */
	slt	v0, v0, v1	/* v0 = v0 < v1 */
	beq	v0, zero, $L5e7dc	/* if (v0 == 0) goto $L5e7dc */
	nop	/* delay slot: nothing */
$L5e774:
	lbu	a0, 0(s2)	/* a0 = *(u8 *)(s2 + 0) */
	lbu	v0, 1(s2)	/* v0 = *(u8 *)(s2 + 1) */
	lw	a1, g_8f6e8	/* a1 = g_8f6e8 (word) */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	addu	v0, v0, s3	/* v0 = v0 + s3 */
	sll	a0, a0, 2	/* a0 = a0 << 2 */
	lw	v1, 0(v0)	/* v1 = *(word *)(v0 + 0) */
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	addu	a0, a0, s3	/* a0 = a0 + s3 */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	sw	v1, 16(sp)	/* spill v1 to sp+16 */
	.set	noat
	la	$1, CD_comstr	/* $1 = &CD_comstr */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	a2, 0($1)	/* a2 = *(word *)($1 + 0) */
	.set	at
	lw	a3, 0(a0)	/* a3 = *(word *)(a0 + 0) */
	la	a0, $LC29	/* a0 = &$LC29 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	jal	CD_init	/* call CD_init */
	nop	/* delay slot: nothing */
	j	$L5e7e0	/* goto $L5e7e0 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
$L5e7dc:
	addu	v0, zero, zero	/* v0 = zero */
$L5e7e0:
	bne	v0, zero, $L5e8fc	/* if (v0 != 0) goto $L5e8fc */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
	jal	CheckCallback	/* call CheckCallback */
	nop	/* delay slot: nothing */
	beq	v0, zero, $L5e8a0	/* if (v0 == 0) goto $L5e8a0 */
	nop	/* delay slot: nothing */
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	s1, v0, 0x3	/* s1 = v0 & 0x3 */
$L5e810:
	jal	f_5daa8	/* call f_5daa8 */
	nop	/* delay slot: nothing */
	addu	s0, v0, zero	/* s0 = v0 */
	beq	s0, zero, $L5e890	/* if (s0 == 0) goto $L5e890 */
	andi	v0, s0, 0x4	/* [delay slot, real work] v0 = s0 & 0x4 */
	beq	v0, zero, $L5e858	/* if (v0 == 0) goto $L5e858 */
	andi	v0, s0, 0x2	/* [delay slot, real work] v0 = s0 & 0x2 */
	lw	v0, CD_cbready	/* v0 = CD_cbready (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5e854	/* if (v0 == 0) goto $L5e854 */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s4)	/* a0 = *(u8 *)(s4 + 0) */
	la	a1, g_8f6d0	/* a1 = &g_8f6d0 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
$L5e854:
	andi	v0, s0, 0x2	/* v0 = s0 & 0x2 */
$L5e858:
	beq	v0, zero, $L5e810	/* if (v0 == 0) goto $L5e810 */
	nop	/* delay slot: nothing */
	lw	v0, CD_cbsync	/* v0 = CD_cbsync (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5e810	/* if (v0 == 0) goto $L5e810 */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s2)	/* a0 = *(u8 *)(s2 + 0) */
	la	a1, g_8f6c8	/* a1 = &g_8f6c8 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
	j	$L5e810	/* goto $L5e810 */
	nop	/* delay slot: nothing */
$L5e890:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	s1, 0(v0)	/* *(u8 *)(v0 + 0) = s1 */
$L5e8a0:
	lbu	v0, 0(s2)	/* v0 = *(u8 *)(s2 + 0) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5e72c	/* if (v0 == 0) goto $L5e72c */
	addu	a2, s5, zero	/* [delay slot, real work] a2 = s5 */

	/* Copy the acknowledge response (g_8f6c8) to *result; return -1 if the sync code is 5 (DiskError), else 0 */
$L5e8b0:
	la	a0, g_8f6c8	/* a0 = &g_8f6c8 */
	beq	a2, zero, $L5e8e0	/* if (a2 == 0) goto $L5e8e0 */
	nop	/* delay slot: nothing */
	ori	v1, zero, 0x7	/* v1 = 7 */
	addiu	a1, zero, -1	/* a1 = -1 */
$L5e8c8:
	lbu	v0, 0(a0)	/* v0 = *(u8 *)(a0 + 0) */
	addiu	a0, a0, 1	/* a0 = a0 + 1 */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */
	sb	v0, 0(a2)	/* *(u8 *)(a2 + 0) = v0 */
	bne	v1, a1, $L5e8c8	/* if (v1 != a1) goto $L5e8c8 */
	addiu	a2, a2, 1	/* [delay slot, real work] a2 = a2 + 1 */
$L5e8e0:
	la	v0, g_747d4+1	/* v0 = &g_747d4+1 */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	xori	v0, v0, 0x5	/* v0 = v0 ^ 0x5 */
	sltiu	v0, v0, 1	/* v0 = v0 < 1 */
	subu	v0, zero, v0	/* v0 = zero - v0 */
$L5e8fc:
	lw	ra, 64(sp)	/* restore ra */
	lw	s5, 60(sp)	/* restore s5 */
	lw	s4, 56(sp)	/* restore s4 */
	lw	s3, 52(sp)	/* restore s3 */
	lw	s2, 48(sp)	/* restore s2 */
	lw	s1, 44(sp)	/* restore s1 */
	lw	s0, 40(sp)	/* restore s0 */
	addiu	sp, sp, 72	/* release 72 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_vol - program the CD-DA to SPU volume matrix
 *
 * Purpose:   int CD_vol(CdlATV *vol); CdMix() calls it. vol = {left->left, left->right, right->right,
 *            right->left} as four bytes.
 * Inputs:    a0 = pointer to the four volume bytes.   Outputs: v0 = 0.
 * Hardware:  CDREG0 = 2: CDREG2 <- vol[0], CDREG3 <- vol[1]; CDREG0 = 3: CDREG1 <- vol[2], CDREG2 <- vol[3];
 *            CDREG3 = 0x20 applies the new volume.
 * Clobbers:  v0, v1. Leaf, no frame.   Callers: CdMix.
 */
	.globl	CD_vol
CD_vol:

	/* Bank 2: left->left and left->right volume */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x2	/* v0 = 2 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+8	/* v1 = CDREG2 pointer (g_747bc+8) */
	lbu	v0, 0(a0)	/* v0 = *(u8 *)(a0 + 0) */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+12	/* v1 = CDREG3 pointer (g_747bc+12) */
	lbu	v0, 1(a0)	/* v0 = *(u8 *)(a0 + 1) */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */

	/* Bank 3: right->right and right->left volume */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x3	/* v0 = 3 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+4	/* v1 = CDREG1 pointer (g_747bc+4) */
	lbu	v0, 2(a0)	/* v0 = *(u8 *)(a0 + 2) */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+8	/* v1 = CDREG2 pointer (g_747bc+8) */
	lbu	v0, 3(a0)	/* v0 = *(u8 *)(a0 + 3) */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */

	/* CDREG3 = 0x20: latch the volume registers; return 0 */
	lw	v1, g_747bc+12	/* v1 = CDREG3 pointer (g_747bc+12) */
	ori	v0, zero, 0x20	/* v0 = 32 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	jr	ra	/* return */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */
/*
 * CD_shell - wait until the drive lid is closed
 *
 * Purpose:   int CD_shell(void). While CD_status bit 4 (lid open) is set: clear CD_cbsync, print
 *            "CD open(%02x)...", issue CdlNop (1), wait 60 VSyncs and re-read CD_status; restores CD_cbsync.
 * Outputs:   v0 = 0. The first two instructions (load of CD_status) precede the prologue, so the routine
 *            starts at 0x8005e9ac.   Clobbers: v0, a0-a3, s0 (saved). Frame 24 bytes.
 * Callers:   none inside the library (the same loop is inlined in CD_init and f_5ee60); exported name of 3.3.
 * Callees:   printf, CD_cw, VSync.
 */
	.globl	CD_shell
CD_shell:
	lw	a1, CD_status	/* a1 = CD_status (word) */
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	sw	s0, 16(sp)	/* save s0 */
	lw	s0, CD_cbsync	/* s0 = CD_cbsync (word) */
	andi	v0, a1, 0x10	/* v0 = a1 & 0x10 */
	beq	v0, zero, $L5ea20	/* if (v0 == 0) goto $L5ea20 */
	sw	ra, 20(sp)	/* [delay slot, real work] save ra */

	/* Lid open: silence CD_cbsync, announce, send CdlNop to refresh the status, sleep one second */
$L5e9d0:
	la	a0, $LC37	/* a0 = &$LC37 */
	sw	zero, CD_cbsync	/* CD_cbsync = zero */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	ori	a0, zero, 0x1	/* a0 = 1 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	jal	VSync	/* call VSync */
	ori	a0, zero, 0x3c	/* [delay slot, real work] a0 = 60 */
	lw	a1, CD_status	/* a1 = CD_status (word) */
	sw	s0, CD_cbsync	/* CD_cbsync = s0 */
	andi	v0, a1, 0x10	/* v0 = a1 & 0x10 */
	bne	v0, zero, $L5e9d0	/* if (v0 != 0) goto $L5e9d0 */
	nop	/* delay slot: nothing */

	/* Lid closed (or nothing to wait for): return 0 */
$L5ea20:
	addu	v0, zero, zero	/* v0 = zero */
	lw	ra, 20(sp)	/* restore ra */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_flush - discard every pending controller interrupt and reset the decoded state
 *
 * Purpose:   void CD_flush(void); CdFlush() calls it.
 * Hardware:  CDREG0 = 1; while CDREG3 & 7: acknowledge (CDREG3 = 7) and re-enable (CDREG2 = 7); then CDREG0 = 0,
 *            CDREG3 = 0 (clear the data request) and COM_DELAY (0x1f801020) = 0x132c.
 * Memory:    g_747d7 = g_747d6 = 0, g_747d5 = 2 (idle).
 * Returns:   whatever v0 holds (0x132c); callers ignore it.   Clobbers: v0, v1, a0. Leaf.
 */
	.globl	CD_flush
CD_flush:
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */

	/* Anything pending (CDREG3 & 7)? then acknowledge repeatedly until clear */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x7	/* v0 = v0 & 0x7 */
	beq	v0, zero, $L5eabc	/* if (v0 == 0) goto $L5eabc */
	ori	v1, zero, 0x7	/* [delay slot, real work] v1 = 7 */
	ori	a0, zero, 0x1	/* a0 = 1 */
$L5ea6c:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	a0, 0(v0)	/* *(u8 *)(v0 + 0) = a0 */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	lw	v0, g_747bc+8	/* v0 = CDREG2 pointer (g_747bc+8) */
	nop	/* load delay slot: nothing */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x7	/* v0 = v0 & 0x7 */
	bne	v0, zero, $L5ea6c	/* if (v0 != 0) goto $L5ea6c */
	nop	/* delay slot: nothing */

	/* Reset the decoded channel state: no ready code, sync channel idle (2) */
$L5eabc:
	sb	zero, g_747d4+3	/* g_747d4+3 <- zero */
	lbu	v1, g_747d4+3	/* v1 = g_747d4+3 (u8) */
	la	v0, g_747d4+2	/* v0 = &g_747d4+2 */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */

	/* Bank 0, request register cleared, CD-ROM timing back to 0x132c */
	ori	v0, zero, 0x2	/* v0 = 2 */
	sb	v0, g_747d4+1	/* g_747d4+1 <- v0 */
	sb	zero, 0(v1)	/* *(u8 *)(v1 + 0) = zero */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	lw	v1, g_747bc+16	/* v1 = COM_DELAY pointer (g_747bc+16) */
	ori	v0, zero, 0x132c	/* v0 = 4908 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_init - (re)initialise the CD-ROM driver and the drive
 *
 * Purpose:   int CD_init(void); CdReset(), the time-out paths and CdInit use it.
 * Steps:     print the state-table address, clear the callbacks/CD_status and the ten state words at g_747d8,
 *            ResetCallback(), InterruptCallback(2, f_5f708), flush the controller (as CD_flush), send CdlNop,
 *            wait out an open lid, then CdlInit (0x0a) and CdlDemute (0x0c) and CD_sync(0, 0).
 * Outputs:   v0 = 0 success, -1 on any failure.   Frame 24 bytes; s0 = saved CD_cbsync.
 * Callers:   CdReset, CD_sync/CD_ready/CD_cw/CD_readsync/CD_datasync (time-out recovery).
 * Callees:   printf, ResetCallback, InterruptCallback, CD_cw, VSync, CD_sync.
 */
	.globl	CD_init
CD_init:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	la	a0, $LC38	/* a0 = &$LC38 */
	la	a1, g_74800	/* a1 = &g_74800 */
	sw	ra, 20(sp)	/* save ra */
	jal	printf	/* call printf */
	sw	s0, 16(sp)	/* [delay slot, real work] save s0 */

	/* Forget the callbacks and CD_status, clear the ten state words g_747d8..g_747fc */
	la	v1, g_747d8	/* v1 = &g_747d8 */
	ori	v0, zero, 0x9	/* v0 = 9 */
	addiu	a0, zero, -1	/* a0 = -1 */
	sw	zero, CD_cbready	/* CD_cbready = zero */
	sw	zero, CD_cbsync	/* CD_cbsync = zero */
	sw	zero, CD_status	/* CD_status = zero */
$L5eb60:
	sw	zero, 0(v1)	/* *(word *)(v1 + 0) = zero */
	addiu	v0, v0, -1	/* v0 = v0 - 1 */
	bne	v0, a0, $L5eb60	/* if (v0 != a0) goto $L5eb60 */
	addiu	v1, v1, 4	/* [delay slot, real work] v1 = v1 + 4 */

	/* Re-install the interrupt layer and hook the CD interrupt (IRQ2) to f_5f708 */
	jal	ResetCallback	/* call ResetCallback */
	nop	/* delay slot: nothing */
	la	a1, f_5f708	/* a1 = &f_5f708 */
	jal	InterruptCallback	/* call InterruptCallback */
	ori	a0, zero, 0x2	/* [delay slot, real work] a0 = 2 */

	/* Same controller flush as CD_flush (inlined) */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x7	/* v0 = v0 & 0x7 */
	beq	v0, zero, $L5ec0c	/* if (v0 == 0) goto $L5ec0c */
	ori	a0, zero, 0x1	/* [delay slot, real work] a0 = 1 */
	ori	v1, zero, 0x7	/* v1 = 7 */
$L5ebbc:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	a0, 0(v0)	/* *(u8 *)(v0 + 0) = a0 */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	lw	v0, g_747bc+8	/* v0 = CDREG2 pointer (g_747bc+8) */
	nop	/* load delay slot: nothing */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x7	/* v0 = v0 & 0x7 */
	bne	v0, zero, $L5ebbc	/* if (v0 != 0) goto $L5ebbc */
	nop	/* delay slot: nothing */

	/* CdlNop (1) to fetch the drive status */
$L5ec0c:
	ori	a0, zero, 0x1	/* a0 = 1 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	sb	zero, g_747d4+3	/* g_747d4+3 <- zero */
	lbu	v1, g_747d4+3	/* v1 = g_747d4+3 (u8) */
	la	v0, g_747d4+2	/* v0 = &g_747d4+2 */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x2	/* v0 = 2 */
	sb	v0, g_747d4+1	/* g_747d4+1 <- v0 */
	sb	zero, 0(v1)	/* *(u8 *)(v1 + 0) = zero */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	addu	a3, zero, zero	/* a3 = zero */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	lw	v1, g_747bc+16	/* v1 = COM_DELAY pointer (g_747bc+16) */
	ori	v0, zero, 0x132c	/* v0 = 4908 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */
	jal	CD_cw	/* call CD_cw */
	nop	/* delay slot: nothing */
	lw	v0, CD_status	/* v0 = CD_status (word) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x10	/* v0 = v0 & 0x10 */
	beq	v0, zero, $L5ec9c	/* if (v0 == 0) goto $L5ec9c */
	ori	a0, zero, 0x1	/* [delay slot, real work] a0 = 1 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */

	/* Lid open: wait for it to close (the CD_shell loop, inlined) */
$L5ec9c:
	lw	a1, CD_status	/* a1 = CD_status (word) */
	lw	s0, CD_cbsync	/* s0 = CD_cbsync (word) */
	andi	v0, a1, 0x10	/* v0 = a1 & 0x10 */
	beq	v0, zero, $L5ed08	/* if (v0 == 0) goto $L5ed08 */
	nop	/* delay slot: nothing */
$L5ecb8:
	la	a0, $LC37	/* a0 = &$LC37 */
	sw	zero, CD_cbsync	/* CD_cbsync = zero */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	ori	a0, zero, 0x1	/* a0 = 1 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	jal	VSync	/* call VSync */
	ori	a0, zero, 0x3c	/* [delay slot, real work] a0 = 60 */
	lw	a1, CD_status	/* a1 = CD_status (word) */
	sw	s0, CD_cbsync	/* CD_cbsync = s0 */
	andi	v0, a1, 0x10	/* v0 = a1 & 0x10 */
	bne	v0, zero, $L5ecb8	/* if (v0 != 0) goto $L5ecb8 */
	nop	/* delay slot: nothing */

	/* CdlInit (0x0a), CdlDemute (0x0c), then CD_sync(0, 0): success only if it ends with Complete (2) */
$L5ed08:
	ori	a0, zero, 0xa	/* a0 = 10 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5ed58	/* if (v0 != 0) goto $L5ed58 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
	ori	a0, zero, 0xc	/* a0 = 12 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5ed58	/* if (v0 != 0) goto $L5ed58 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
	addu	a0, zero, zero	/* a0 = zero */
	jal	CD_sync	/* call CD_sync */
	addu	a1, zero, zero	/* [delay slot, real work] a1 = zero */
	xori	v0, v0, 0x2	/* v0 = v0 ^ 0x2 */
	sltu	v0, zero, v0	/* v0 = zero < v0 */
	subu	v0, zero, v0	/* v0 = zero - v0 */
$L5ed58:
	lw	ra, 20(sp)	/* restore ra */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_initvol - give the CD audio a sane SPU and CD mixer volume
 *
 * Purpose:   int CD_initvol(void); CdReset(1) calls it.
 * Hardware:  SPU (via g_747d0 = 0x1f801c00): if the current main volume registers (+0x1b8/+0x1ba) read zero,
 *            main volume L/R (+0x180/+0x182) = 0x3fff; CD input volume L/R (+0x1b0/+0x1b2) = 0x3fff;
 *            SPUCNT (+0x1aa) = 0xc001 (SPU enable, unmute, CD audio on). Then the CD mixer matrix
 *            {0x80, 0, 0x80, 0} (straight stereo) through CDREG banks 2 and 3, CDREG3 = 0x20.
 * Outputs:   v0 = 0.   Frame 8 bytes (the four volume bytes live at 0(sp)..3(sp)).
 */
	.globl	CD_initvol
CD_initvol:
	lw	v1, g_747bc+20	/* v1 = SPU base pointer (g_747bc+20) */
	nop	/* load delay slot: nothing */
	lhu	v0, 440(v1)	/* v0 = *(u16 *)(v1 + 440) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L5eda8	/* if (v0 != 0) goto $L5eda8 */
	addiu	sp, sp, -8	/* [delay slot, real work] allocate 8 byte frame */
	lhu	v0, 442(v1)	/* v0 = *(u16 *)(v1 + 442) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L5edac	/* if (v0 != 0) goto $L5edac */
	ori	v0, zero, 0x3fff	/* [delay slot, real work] v0 = 16383 */
	sh	v0, 384(v1)	/* *(u16 *)(v1 + 384) = v0 */
	sh	v0, 386(v1)	/* *(u16 *)(v1 + 386) = v0 */
	lw	v1, g_747bc+20	/* v1 = SPU base pointer (g_747bc+20) */
$L5eda8:
	ori	v0, zero, 0x3fff	/* v0 = 16383 */
$L5edac:
	sh	v0, 432(v1)	/* *(u16 *)(v1 + 432) = v0 */
	sh	v0, 434(v1)	/* *(u16 *)(v1 + 434) = v0 */
	ori	v0, zero, 0xc001	/* v0 = 49153 */
	sh	v0, 426(v1)	/* *(u16 *)(v1 + 426) = v0 */

	/* Build the volume matrix on the stack and send it to the controller */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x80	/* v0 = 128 */
	sb	v0, 2(sp)	/* spill v0 to sp+2 */
	sb	v0, 0(sp)	/* spill v0 to sp+0 */
	ori	v0, zero, 0x2	/* v0 = 2 */
	sb	zero, 3(sp)	/* spill zero to sp+3 */
	sb	zero, 1(sp)	/* spill zero to sp+1 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+8	/* v1 = CDREG2 pointer (g_747bc+8) */
	lbu	v0, 0(sp)	/* reload v0 from sp+0 */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+12	/* v1 = CDREG3 pointer (g_747bc+12) */
	lbu	v0, 1(sp)	/* reload v0 from sp+1 */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x3	/* v0 = 3 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+4	/* v1 = CDREG1 pointer (g_747bc+4) */
	lbu	v0, 2(sp)	/* reload v0 from sp+2 */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+8	/* v1 = CDREG2 pointer (g_747bc+8) */
	lbu	v0, 3(sp)	/* reload v0 from sp+3 */
	nop	/* load delay slot: nothing */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	lw	v1, g_747bc+12	/* v1 = CDREG3 pointer (g_747bc+12) */

	/* CDREG3 = 0x20 latches the matrix */
	ori	v0, zero, 0x20	/* v0 = 32 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */
	addu	v0, zero, zero	/* v0 = zero */
	addiu	sp, sp, 8	/* release 8 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * f_5ee60 - start (or restart after an error) the multi-sector read set up by CD_readm
 *
 * Purpose:   static int start_read(void). Retries up to g_747d8 (16) times: re-seek to CD_pos, set the mode,
 *            install f_5f7e8 as ready callback and issue CdlReadN (6).
 * State:     g_747d8 retries left, g_747dc sector count, g_747e0 buffer start, g_747e4 write pointer,
 *            g_747e8 mode, g_747f0 sectors still to read (-1 = failed / restart), g_747f4 time-out tick,
 *            g_747f8/g_747fc callbacks saved by CD_readm.
 * Outputs:   v0 = g_747f0 on exit (> 0 started).   Frame 56 bytes.
 * Callers:   CD_readm, CD_readsync (on stall), f_5f7e8 (after an error).
 */
	.globl	f_5ee60
f_5ee60:

	/* g_747f0 = -1; nothing to do when no read is armed (g_747d8 == 0) */
	addiu	sp, sp, -56	/* allocate 56 byte frame */
	la	v1, g_747f0	/* v1 = &g_747f0 */
	addiu	v0, zero, -1	/* v0 = -1 */
	sw	ra, 52(sp)	/* save ra */
	sw	s4, 48(sp)	/* save s4 */
	sw	s3, 44(sp)	/* save s3 */
	sw	s2, 40(sp)	/* save s2 */
	sw	s1, 36(sp)	/* save s1 */
	sw	s0, 32(sp)	/* save s0 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */
	lw	v0, g_747d8	/* v0 = g_747d8 (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5f128	/* if (v0 == 0) goto $L5f128 */
	nop	/* delay slot: nothing */
	ori	s4, zero, 0x1	/* s4 = 1 */
	ori	s3, zero, 0x7	/* s3 = 7 */
	addu	s2, v1, zero	/* s2 = v1 */
	addiu	s1, v1, -12	/* s1 = v1 - 12 */

	/* Attempt loop: consume one retry, silence the callbacks */
$L5eeb0:
	lw	v0, 0(s2)	/* v0 = *(word *)(s2 + 0) */
	nop	/* load delay slot: nothing */
	bgez	v0, $L5f128	/* if (v0 >= 0) goto $L5f128 */
	nop	/* delay slot: nothing */
	lw	v0, -24(s2)	/* v0 = *(word *)(s2 + -24) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, -1	/* v0 = v0 - 1 */
	sw	v0, -24(s2)	/* *(word *)(s2 + -24) = v0 */
	lw	v0, -24(s2)	/* v0 = *(word *)(s2 + -24) */
	nop	/* load delay slot: nothing */
	lw	v0, -24(s2)	/* v0 = *(word *)(s2 + -24) */
	sw	zero, CD_cbready	/* CD_cbready = zero */
	sw	zero, CD_cbsync	/* CD_cbsync = zero */
	slti	v0, v0, 15	/* v0 = v0 < 15 */
	beq	v0, zero, $L5efc4	/* if (v0 == 0) goto $L5efc4 */
	nop	/* delay slot: nothing */

	/* Every attempt after the first prints "CD read retry %2d(%02x:%02x:%02x)" with the retry count and CD_pos */
	lbu	a2, CD_pos	/* a2 = CD_pos (u8) */
	lw	a1, -24(s2)	/* a1 = *(word *)(s2 + -24) */
	lbu	a3, CD_pos+1	/* a3 = CD_pos+1 (u8) */
	lbu	v0, CD_pos+2	/* v0 = CD_pos+2 (u8) */
	la	a0, $LC39	/* a0 = &$LC39 */
	jal	printf	/* call printf */
	sw	v0, 16(sp)	/* [delay slot, real work] spill v0 to sp+16 */
	lw	a1, CD_status	/* a1 = CD_status (word) */

	/* Wait out an open lid (as CD_shell) */
	lw	s0, CD_cbsync	/* s0 = CD_cbsync (word) */
	andi	v0, a1, 0x10	/* v0 = a1 & 0x10 */
	beq	v0, zero, $L5ef90	/* if (v0 == 0) goto $L5ef90 */
	nop	/* delay slot: nothing */
$L5ef40:
	la	a0, $LC37	/* a0 = &$LC37 */
	sw	zero, CD_cbsync	/* CD_cbsync = zero */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	ori	a0, zero, 0x1	/* a0 = 1 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	jal	VSync	/* call VSync */
	ori	a0, zero, 0x3c	/* [delay slot, real work] a0 = 60 */
	lw	a1, CD_status	/* a1 = CD_status (word) */
	sw	s0, CD_cbsync	/* CD_cbsync = s0 */
	andi	v0, a1, 0x10	/* v0 = a1 & 0x10 */
	bne	v0, zero, $L5ef40	/* if (v0 != 0) goto $L5ef40 */
	nop	/* delay slot: nothing */

	/* Recovery: CdlStop (8) then CdlInit (0x0a); give up this attempt if either fails */
$L5ef90:
	ori	a0, zero, 0x8	/* a0 = 8 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5f110	/* if (v0 != 0) goto $L5f110 */
	ori	a0, zero, 0xa	/* [delay slot, real work] a0 = 10 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5f110	/* if (v0 != 0) goto $L5f110 */
	nop	/* delay slot: nothing */

	/* Flush pending interrupts and reset the decoded state (as CD_flush) */
$L5efc4:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	s4, 0(v0)	/* *(u8 *)(v0 + 0) = s4 */
	j	$L5f00c	/* goto $L5f00c */
	nop	/* delay slot: nothing */
$L5efdc:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	s4, 0(v0)	/* *(u8 *)(v0 + 0) = s4 */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	sb	s3, 0(v0)	/* *(u8 *)(v0 + 0) = s3 */
	lw	v0, g_747bc+8	/* v0 = CDREG2 pointer (g_747bc+8) */
	nop	/* load delay slot: nothing */
	sb	s3, 0(v0)	/* *(u8 *)(v0 + 0) = s3 */
$L5f00c:
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0x7	/* v0 = v0 & 0x7 */
	bne	v0, zero, $L5efdc	/* if (v0 != 0) goto $L5efdc */
	ori	a0, zero, 0xe	/* [delay slot, real work] a0 = 14 */
	addiu	a1, sp, 24	/* a1 = sp + 24 */
	sb	zero, g_747d4+3	/* g_747d4+3 <- zero */
	lbu	v1, g_747d4+3	/* v1 = g_747d4+3 (u8) */
	la	v0, g_747d4+2	/* v0 = &g_747d4+2 */
	sb	v1, 0(v0)	/* *(u8 *)(v0 + 0) = v1 */
	lw	v1, g_747bc	/* v1 = CDREG0 pointer (g_747bc) */
	ori	v0, zero, 0x2	/* v0 = 2 */
	sb	v0, g_747d4+1	/* g_747d4+1 <- v0 */
	sb	zero, 0(v1)	/* *(u8 *)(v1 + 0) = zero */
	lw	v0, g_747bc+12	/* v0 = CDREG3 pointer (g_747bc+12) */
	addu	a2, zero, zero	/* a2 = zero */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	lw	v1, g_747bc+16	/* v1 = COM_DELAY pointer (g_747bc+16) */
	ori	v0, zero, 0x132c	/* v0 = 4908 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */

	/* Rewind the write pointer, hook f_5f7e8 as ready callback, send CdlSetmode(mode), CdlSetloc(CD_pos), CdlReadN */
	lw	v0, -4(s1)	/* v0 = *(word *)(s1 + -4) */
	addu	a3, zero, zero	/* a3 = zero */
	sw	v0, 0(s1)	/* *(word *)(s1 + 0) = v0 */
	lw	v1, 4(s1)	/* v1 = *(word *)(s1 + 4) */
	la	v0, f_5f7e8	/* v0 = &f_5f7e8 */
	sw	v0, CD_cbready	/* CD_cbready = v0 */
	jal	CD_cw	/* call CD_cw */
	sb	v1, 24(sp)	/* [delay slot, real work] spill v1 to sp+24 */
	bne	v0, zero, $L5f110	/* if (v0 != 0) goto $L5f110 */
	ori	a0, zero, 0x2	/* [delay slot, real work] a0 = 2 */
	la	a1, CD_pos	/* a1 = &CD_pos */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5f110	/* if (v0 != 0) goto $L5f110 */
	ori	a0, zero, 0x6	/* [delay slot, real work] a0 = 6 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	bne	v0, zero, $L5f110	/* if (v0 != 0) goto $L5f110 */
	nop	/* delay slot: nothing */

	/* Armed: g_747f0 = sector count; time-out tick = VSync(-1) + count + 480 */
	lw	v0, -8(s1)	/* v0 = *(word *)(s1 + -8) */
	addiu	a0, zero, -1	/* a0 = -1 */
	sw	v0, 12(s1)	/* *(word *)(s1 + 12) = v0 */
	jal	VSync	/* call VSync */
	nop	/* delay slot: nothing */
	lw	v1, -8(s1)	/* v1 = *(word *)(s1 + -8) */
	nop	/* load delay slot: nothing */
	addiu	v1, v1, 480	/* v1 = v1 + 480 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 16(s1)	/* *(word *)(s1 + 16) = v0 */

	/* Loop while attempts remain and the read has not started */
$L5f110:
	la	v0, g_747d8	/* v0 = &g_747d8 */
	lw	v0, 0(v0)	/* v0 = *(word *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L5eeb0	/* if (v0 != 0) goto $L5eeb0 */
	nop	/* delay slot: nothing */
$L5f128:
	la	v0, g_747f0	/* v0 = &g_747f0 */
	lw	v0, 0(v0)	/* v0 = *(word *)(v0 + 0) */
	lw	ra, 52(sp)	/* restore ra */
	lw	s4, 48(sp)	/* restore s4 */
	lw	s3, 44(sp)	/* restore s3 */
	lw	s2, 40(sp)	/* restore s2 */
	lw	s1, 36(sp)	/* restore s1 */
	lw	s0, 32(sp)	/* restore s0 */
	addiu	sp, sp, 56	/* release 56 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_readm - begin reading n sectors into memory (non-blocking)
 *
 * Purpose:   int CD_readm(u_long *buf, int count, int mode); CdRead() calls it up to three times.
 *            Chooses the sector size from mode & 0x30 (0: 0x200 words = 2048 B; 0x20: 0x249 words = 2340 B;
 *            else 0x246 words = 2328 B), stores buf/count/mode, 16 retries, saves CD_cbsync/CD_cbready and
 *            calls f_5ee60.
 * Inputs:    a0 = buffer, a1 = sector count, a2 = mode.   Outputs: v0 = 0 started, -1 failed.
 * Memory:    g_747d8, g_747dc, g_747e0, g_747e8, g_747ec, g_747f8, g_747fc.   Frame 24 bytes.
 */
	.globl	CD_readm
CD_readm:
	addiu	sp, sp, -24	/* allocate 24 byte frame */
	la	v0, g_747e8	/* v0 = &g_747e8 */
	sw	ra, 16(sp)	/* save ra */
	sw	a2, 0(v0)	/* *(word *)(v0 + 0) = a2 */

	/* Sector size in words from the mode bits */
	lw	v0, 0(v0)	/* v0 = *(word *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v1, v0, 0x30	/* v1 = v0 & 0x30 */
	beq	v1, zero, $L5f190	/* if (v1 == 0) goto $L5f190 */
	ori	v0, zero, 0x20	/* [delay slot, real work] v0 = 32 */
	beq	v1, v0, $L5f1a4	/* if (v1 == v0) goto $L5f1a4 */
	ori	v0, zero, 0x249	/* [delay slot, real work] v0 = 585 */
	j	$L5f1b4	/* goto $L5f1b4 */
	ori	v0, zero, 0x246	/* [delay slot, real work] v0 = 582 */
$L5f190:
	ori	v0, zero, 0x200	/* v0 = 512 */
	sw	v0, g_747ec	/* g_747ec = v0 */
	j	$L5f1c0	/* goto $L5f1c0 */
	nop	/* delay slot: nothing */
$L5f1a4:
	sw	v0, g_747ec	/* g_747ec = v0 */
	j	$L5f1c0	/* goto $L5f1c0 */
	nop	/* delay slot: nothing */
$L5f1b4:
	la	v1, g_747ec	/* v1 = &g_747ec */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */

	/* Save the callbacks that the read will temporarily replace */
$L5f1c0:
	lw	v1, CD_cbsync	/* v1 = CD_cbsync (word) */
	la	v0, g_747e0	/* v0 = &g_747e0 */
	sw	a0, 0(v0)	/* *(word *)(v0 + 0) = a0 */
	lw	a0, CD_cbready	/* a0 = CD_cbready (word) */
	ori	v0, zero, 0x10	/* v0 = 16 */
	sw	a1, g_747dc	/* g_747dc = a1 */
	sw	v0, g_747d8	/* g_747d8 = v0 */
	sw	v1, g_747f8	/* g_747f8 = v1 */
	sw	a0, g_747fc	/* g_747fc = a0 */

	/* Start the read; map "started" to 0 and "failed" to -1 */
	jal	f_5ee60	/* call f_5ee60 */
	nop	/* delay slot: nothing */
	slti	v0, v0, 1	/* v0 = v0 < 1 */
	subu	v0, zero, v0	/* v0 = zero - v0 */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release 24 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_readsync - wait for (or poll) the end of a CD_readm read
 *
 * Purpose:   int CD_readsync(int mode, u_char *result); CdReadSync() calls it. Time-out guard name "CD_read".
 *            Polls the controller as CD_sync does; on a stall (VSync(-1) past g_747f4) restarts the read
 *            through f_5ee60; when no sectors are left it waits for the DMA with CD_datasync(0).
 * Outputs:   v0 = sectors still to read (0 = finished, < 0 = failed), or -1 after a time-out.
 * Memory:    g_747f0, g_747f4, g_8f6d0 (copied to *result).   Frame 64 bytes.
 */
	.globl	CD_readsync
CD_readsync:
	addiu	sp, sp, -64	/* allocate 64 byte frame */
	sw	s6, 48(sp)	/* save s6 */
	addu	s6, a0, zero	/* s6 = a0 */
	sw	s7, 52(sp)	/* save s7 */
	addu	s7, a1, zero	/* s7 = a1 */
	addiu	a0, zero, -1	/* a0 = -1 */
	sw	ra, 56(sp)	/* save ra */
	sw	s5, 44(sp)	/* save s5 */
	sw	s4, 40(sp)	/* save s4 */
	sw	s3, 36(sp)	/* save s3 */
	sw	s2, 32(sp)	/* save s2 */
	sw	s1, 28(sp)	/* save s1 */
	jal	VSync	/* call VSync */
	sw	s0, 24(sp)	/* [delay slot, real work] save s0 */
	la	s4, CD_intstr	/* s4 = &CD_intstr */
	la	s3, g_747d4+1	/* s3 = &g_747d4+1 */
	addiu	s5, s3, 1	/* s5 = s3 + 1 */
	la	s2, g_747f4	/* s2 = &g_747f4 */
	addiu	v0, v0, 480	/* v0 = v0 + 480 */
	sw	v0, g_8f6e0	/* g_8f6e0 = v0 */
	la	v0, $LC40	/* v0 = &$LC40 */
	sw	zero, g_8f6e4	/* g_8f6e4 = zero */
	sw	v0, g_8f6e8	/* g_8f6e8 = v0 */
$L5f298:
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* [delay slot, real work] a0 = -1 */
	lw	v1, g_8f6e0	/* v1 = g_8f6e0 (word) */
	nop	/* load delay slot: nothing */
	slt	v1, v1, v0	/* v1 = v1 < v0 */
	bne	v1, zero, $L5f2e0	/* if (v1 != 0) goto $L5f2e0 */
	nop	/* delay slot: nothing */
	lw	v1, g_8f6e4	/* v1 = g_8f6e4 (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v1, 1	/* v0 = v1 + 1 */
	sw	v0, g_8f6e4	/* g_8f6e4 = v0 */
	lui	v0, 0x1e	/* v0 = 0x1e0000 */
	slt	v0, v0, v1	/* v0 = v0 < v1 */
	beq	v0, zero, $L5f348	/* if (v0 == 0) goto $L5f348 */

	/* Time-out guard / interrupt drain as in CD_sync */
	nop	/* delay slot: nothing */
$L5f2e0:
	lbu	a0, 0(s3)	/* a0 = *(u8 *)(s3 + 0) */
	lbu	v0, 1(s3)	/* v0 = *(u8 *)(s3 + 1) */
	lw	a1, g_8f6e8	/* a1 = g_8f6e8 (word) */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	addu	v0, v0, s4	/* v0 = v0 + s4 */
	sll	a0, a0, 2	/* a0 = a0 << 2 */
	lw	v1, 0(v0)	/* v1 = *(word *)(v0 + 0) */
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	addu	a0, a0, s4	/* a0 = a0 + s4 */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	sw	v1, 16(sp)	/* spill v1 to sp+16 */
	.set	noat
	la	$1, CD_comstr	/* $1 = &CD_comstr */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	a2, 0($1)	/* a2 = *(word *)($1 + 0) */
	.set	at
	lw	a3, 0(a0)	/* a3 = *(word *)(a0 + 0) */
	la	a0, $LC29	/* a0 = &$LC29 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	jal	CD_init	/* call CD_init */
	nop	/* delay slot: nothing */
	j	$L5f34c	/* goto $L5f34c */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
$L5f348:
	addu	v0, zero, zero	/* v0 = zero */
$L5f34c:
	bne	v0, zero, $L5f494	/* if (v0 != 0) goto $L5f494 */
	nop	/* delay slot: nothing */
	jal	CheckCallback	/* call CheckCallback */
	nop	/* delay slot: nothing */
	beq	v0, zero, $L5f410	/* if (v0 == 0) goto $L5f410 */
	addu	a0, s7, zero	/* [delay slot, real work] a0 = s7 */
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	s1, v0, 0x3	/* s1 = v0 & 0x3 */
$L5f37c:
	jal	f_5daa8	/* call f_5daa8 */
	nop	/* delay slot: nothing */
	addu	s0, v0, zero	/* s0 = v0 */
	beq	s0, zero, $L5f3fc	/* if (s0 == 0) goto $L5f3fc */
	andi	v0, s0, 0x4	/* [delay slot, real work] v0 = s0 & 0x4 */
	beq	v0, zero, $L5f3c4	/* if (v0 == 0) goto $L5f3c4 */
	andi	v0, s0, 0x2	/* [delay slot, real work] v0 = s0 & 0x2 */
	lw	v0, CD_cbready	/* v0 = CD_cbready (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5f3c0	/* if (v0 == 0) goto $L5f3c0 */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s5)	/* a0 = *(u8 *)(s5 + 0) */
	la	a1, g_8f6d0	/* a1 = &g_8f6d0 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
$L5f3c0:
	andi	v0, s0, 0x2	/* v0 = s0 & 0x2 */
$L5f3c4:
	beq	v0, zero, $L5f37c	/* if (v0 == 0) goto $L5f37c */
	nop	/* delay slot: nothing */
	lw	v0, CD_cbsync	/* v0 = CD_cbsync (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5f37c	/* if (v0 == 0) goto $L5f37c */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s3)	/* a0 = *(u8 *)(s3 + 0) */
	la	a1, g_8f6c8	/* a1 = &g_8f6c8 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
	j	$L5f37c	/* goto $L5f37c */
	nop	/* delay slot: nothing */
$L5f3fc:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	s1, 0(v0)	/* *(u8 *)(v0 + 0) = s1 */
	addu	a0, s7, zero	/* a0 = s7 */

	/* Copy the last ready response to *result */
$L5f410:
	la	a1, g_8f6d0	/* a1 = &g_8f6d0 */
	beq	a0, zero, $L5f43c	/* if (a0 == 0) goto $L5f43c */
	ori	v1, zero, 0x7	/* [delay slot, real work] v1 = 7 */
	addiu	a2, zero, -1	/* a2 = -1 */
$L5f424:
	lbu	v0, 0(a1)	/* v0 = *(u8 *)(a1 + 0) */
	addiu	a1, a1, 1	/* a1 = a1 + 1 */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */
	sb	v0, 0(a0)	/* *(u8 *)(a0 + 0) = v0 */
	bne	v1, a2, $L5f424	/* if (v1 != a2) goto $L5f424 */
	addiu	a0, a0, 1	/* [delay slot, real work] a0 = a0 + 1 */

	/* Stalled for too long? restart the read */
$L5f43c:
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* [delay slot, real work] a0 = -1 */
	lw	v1, 0(s2)	/* v1 = *(word *)(s2 + 0) */
	nop	/* load delay slot: nothing */
	slt	v1, v1, v0	/* v1 = v1 < v0 */
	beq	v1, zero, $L5f460	/* if (v1 == 0) goto $L5f460 */
	nop	/* delay slot: nothing */
	jal	f_5ee60	/* call f_5ee60 */
	nop	/* delay slot: nothing */

	/* No sectors left: wait for the DMA to drain */
$L5f460:
	lw	v0, -4(s2)	/* v0 = *(word *)(s2 + -4) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L5f478	/* if (v0 != 0) goto $L5f478 */
	nop	/* delay slot: nothing */
	jal	CD_datasync	/* call CD_datasync */
	addu	a0, zero, zero	/* [delay slot, real work] a0 = zero */

	/* Blocking mode loops while sectors remain */
$L5f478:
	bne	s6, zero, $L5f490	/* if (s6 != 0) goto $L5f490 */
	nop	/* delay slot: nothing */
	lw	v0, -4(s2)	/* v0 = *(word *)(s2 + -4) */
	nop	/* load delay slot: nothing */
	bgtz	v0, $L5f298	/* if (v0 > 0) goto $L5f298 */
	nop	/* delay slot: nothing */
$L5f490:
	lw	v0, -4(s2)	/* v0 = *(word *)(s2 + -4) */
$L5f494:
	lw	ra, 56(sp)	/* restore ra */
	lw	s7, 52(sp)	/* restore s7 */
	lw	s6, 48(sp)	/* restore s6 */
	lw	s5, 44(sp)	/* restore s5 */
	lw	s4, 40(sp)	/* restore s4 */
	lw	s3, 36(sp)	/* restore s3 */
	lw	s2, 32(sp)	/* restore s2 */
	lw	s1, 28(sp)	/* restore s1 */
	lw	s0, 24(sp)	/* restore s0 */
	addiu	sp, sp, 64	/* release 64 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_datasync - wait for the CD DMA (channel 3) to finish
 *
 * Purpose:   int CD_datasync(int mode); CdDataSync() calls it. Polls DMA3 CHCR (g_74824 -> 0x1f8010b8) bit 24.
 * Outputs:   v0 = 0 idle, 1 busy (mode != 0), -1 time-out (after CD_init). Guard name "CD_datasync".
 * Frame 48 bytes.
 */
	.globl	CD_datasync
CD_datasync:
	addiu	sp, sp, -48	/* allocate 48 byte frame */
	sw	s2, 32(sp)	/* save s2 */
	addu	s2, a0, zero	/* s2 = a0 */
	addiu	a0, zero, -1	/* a0 = -1 */
	sw	ra, 40(sp)	/* save ra */
	sw	s3, 36(sp)	/* save s3 */
	sw	s1, 28(sp)	/* save s1 */
	jal	VSync	/* call VSync */
	sw	s0, 24(sp)	/* [delay slot, real work] save s0 */
	la	s3, CD_comstr	/* s3 = &CD_comstr */
	la	s1, g_747d4+1	/* s1 = &g_747d4+1 */
	la	s0, CD_intstr	/* s0 = &CD_intstr */
	addiu	v0, v0, 480	/* v0 = v0 + 480 */
	sw	v0, g_8f6e0	/* g_8f6e0 = v0 */
	la	v0, $LC41	/* v0 = &$LC41 */
	sw	zero, g_8f6e4	/* g_8f6e4 = zero */
	sw	v0, g_8f6e8	/* g_8f6e8 = v0 */
$L5f524:
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* [delay slot, real work] a0 = -1 */
	lw	v1, g_8f6e0	/* v1 = g_8f6e0 (word) */
	nop	/* load delay slot: nothing */
	slt	v1, v1, v0	/* v1 = v1 < v0 */
	bne	v1, zero, $L5f56c	/* if (v1 != 0) goto $L5f56c */
	nop	/* delay slot: nothing */
	lw	v1, g_8f6e4	/* v1 = g_8f6e4 (word) */
	nop	/* load delay slot: nothing */
	addiu	v0, v1, 1	/* v0 = v1 + 1 */
	sw	v0, g_8f6e4	/* g_8f6e4 = v0 */
	lui	v0, 0x1e	/* v0 = 0x1e0000 */
	slt	v0, v0, v1	/* v0 = v0 < v1 */
	beq	v0, zero, $L5f5cc	/* if (v0 == 0) goto $L5f5cc */
	nop	/* delay slot: nothing */
$L5f56c:
	lbu	a0, 0(s1)	/* a0 = *(u8 *)(s1 + 0) */
	lbu	v0, 1(s1)	/* v0 = *(u8 *)(s1 + 1) */
	lw	a1, g_8f6e8	/* a1 = g_8f6e8 (word) */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	addu	v0, v0, s0	/* v0 = v0 + s0 */
	sll	a0, a0, 2	/* a0 = a0 << 2 */
	lw	v1, 0(v0)	/* v1 = *(word *)(v0 + 0) */
	lbu	v0, CD_com	/* v0 = CD_com (u8) */
	addu	a0, a0, s0	/* a0 = a0 + s0 */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	addu	v0, v0, s3	/* v0 = v0 + s3 */
	sw	v1, 16(sp)	/* spill v1 to sp+16 */
	lw	a2, 0(v0)	/* a2 = *(word *)(v0 + 0) */
	lw	a3, 0(a0)	/* a3 = *(word *)(a0 + 0) */
	la	a0, $LC29	/* a0 = &$LC29 */
	jal	printf	/* call printf */
	nop	/* delay slot: nothing */
	jal	CD_init	/* call CD_init */
	nop	/* delay slot: nothing */
	j	$L5f5d0	/* goto $L5f5d0 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
$L5f5cc:
	addu	v0, zero, zero	/* v0 = zero */
$L5f5d0:
	bne	v0, zero, $L5f600	/* if (v0 != 0) goto $L5f600 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */

	/* DMA3 busy bit still set? (mode 0 keeps looping, otherwise report busy) */
	lw	v0, g_74814+16	/* v0 = DMA3 CHCR pointer (g_74814+16) */
	nop	/* load delay slot: nothing */
	lw	v0, 0(v0)	/* v0 = *(word *)(v0 + 0) */
	lui	v1, 0x100	/* v1 = 0x1000000 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	beq	v0, zero, $L5f600	/* if (v0 == 0) goto $L5f600 */
	addu	v0, zero, zero	/* [delay slot, real work] v0 = zero */
	beq	s2, zero, $L5f524	/* if (s2 == 0) goto $L5f524 */
	ori	v0, zero, 0x1	/* [delay slot, real work] v0 = 1 */
$L5f600:
	lw	ra, 40(sp)	/* restore ra */
	lw	s3, 36(sp)	/* restore s3 */
	lw	s2, 32(sp)	/* restore s2 */
	lw	s1, 28(sp)	/* restore s1 */
	lw	s0, 24(sp)	/* restore s0 */
	addiu	sp, sp, 48	/* release 48 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_getsector - start a DMA3 transfer of sector data from the controller
 *
 * Purpose:   int CD_getsector(void *buf, int words); CdGetSector() and f_5f7e8 call it. First waits for the
 *            previous DMA (CD_datasync(0)), asks the controller for data (CDREG0 = 0, CDREG3 = 0x80), sets the
 *            CD-ROM timing (0x1f801018 = 0x20943, COM_DELAY = 0x132c), enables DMA3 in DPCR (bit 15), then
 *            MADR = buf, BCR = words | 0x10000 (one block), CHCR = 0x11000000 (start).
 * Outputs:   v0 = 0.   Frame 32 bytes.
 */
	.globl	CD_getsector
CD_getsector:
	addiu	sp, sp, -32	/* allocate 32 byte frame */
	sw	s1, 20(sp)	/* save s1 */
	addu	s1, a0, zero	/* s1 = a0 */
	sw	s0, 16(sp)	/* save s0 */
	addu	s0, a1, zero	/* s0 = a1 */
	sw	ra, 24(sp)	/* save ra */

	/* Wait for any DMA still running */
	jal	CD_datasync	/* call CD_datasync */
	addu	a0, zero, zero	/* [delay slot, real work] a0 = zero */

	/* Request the data FIFO (BFRD) */
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	lui	a0, 0x2	/* a0 = 0x20000 */
	sb	zero, 0(v0)	/* *(u8 *)(v0 + 0) = zero */
	lw	v1, g_747bc+12	/* v1 = CDREG3 pointer (g_747bc+12) */
	ori	v0, zero, 0x80	/* v0 = 128 */
	sb	v0, 0(v1)	/* *(u8 *)(v1 + 0) = v0 */

	/* Timing registers, DMA3 master enable */
	lw	v0, g_74814	/* v0 = CD-ROM delay register pointer (g_74814) */
	ori	a0, a0, 0x943	/* a0 = a0 | 0x943 */
	sw	a0, 0(v0)	/* *(word *)(v0 + 0) = a0 */
	lw	v1, g_747bc+16	/* v1 = COM_DELAY pointer (g_747bc+16) */
	ori	v0, zero, 0x132c	/* v0 = 4908 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */
	lw	v1, g_74814+4	/* v1 = DPCR pointer (g_74814+4) */
	nop	/* load delay slot: nothing */
	lw	v0, 0(v1)	/* v0 = *(word *)(v1 + 0) */
	nop	/* load delay slot: nothing */
	ori	v0, v0, 0x8000	/* v0 = v0 | 0x8000 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */

	/* Program the transfer and start it */
	lw	v0, g_74814+16	/* v0 = DMA3 CHCR pointer (g_74814+16) */
	nop	/* load delay slot: nothing */
	sw	zero, 0(v0)	/* *(word *)(v0 + 0) = zero */
	lw	v1, g_74814+8	/* v1 = DMA3 MADR pointer (g_74814+8) */
	addu	v0, zero, zero	/* v0 = zero */
	sw	s1, 0(v1)	/* *(word *)(v1 + 0) = s1 */
	lui	v1, 0x1	/* v1 = 0x10000 */
	lw	a0, g_74814+12	/* a0 = DMA3 BCR pointer (g_74814+12) */
	or	s0, s0, v1	/* s0 = s0 | v1 */
	sw	s0, 0(a0)	/* *(word *)(a0 + 0) = s0 */
	lw	a0, g_74814+16	/* a0 = DMA3 CHCR pointer (g_74814+16) */
	lui	v1, 0x1100	/* v1 = 0x11000000 */
	sw	v1, 0(a0)	/* *(word *)(a0 + 0) = v1 */
	lw	ra, 24(sp)	/* restore ra */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 32	/* release 32 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * CD_set_test_parmnum - set how many parameter bytes the Test command (0x19) sends
 *
 * Purpose:   void CD_set_test_parmnum(int n): g_7473c[0x19] = n (the word at 0x800747a0). Leaf.
 */
	.globl	CD_set_test_parmnum
CD_set_test_parmnum:
	sw	a0, g_7473c+100	/* g_7473c+100 = a0 */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * f_5f708 - CD-ROM interrupt callback (installed by CD_init with InterruptCallback(2, ...))
 *
 * Purpose:   void cd_irq(void). Saves the CDREG0 bank, then drains f_5daa8: ready events call
 *            CD_cbready(g_747d6, g_8f6d0), sync events CD_cbsync(g_747d5, g_8f6c8); restores the bank.
 * The first two instructions (load of the CDREG0 pointer) precede the prologue.   Frame 32 bytes.
 */
	.globl	f_5f708
f_5f708:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	addiu	sp, sp, -32	/* allocate 32 byte frame */
	sw	ra, 28(sp)	/* save ra */
	sw	s2, 24(sp)	/* save s2 */
	sw	s1, 20(sp)	/* save s1 */
	sw	s0, 16(sp)	/* save s0 */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	la	s2, g_747d4+2	/* s2 = &g_747d4+2 */
	andi	s1, v0, 0x3	/* s1 = v0 & 0x3 */

	/* Drain loop (identical to the one inside CD_sync) */
$L5f734:
	jal	f_5daa8	/* call f_5daa8 */
	nop	/* delay slot: nothing */
	addu	s0, v0, zero	/* s0 = v0 */
	beq	s0, zero, $L5f7bc	/* if (s0 == 0) goto $L5f7bc */
	andi	v0, s0, 0x4	/* [delay slot, real work] v0 = s0 & 0x4 */
	beq	v0, zero, $L5f77c	/* if (v0 == 0) goto $L5f77c */
	andi	v0, s0, 0x2	/* [delay slot, real work] v0 = s0 & 0x2 */
	lw	v0, CD_cbready	/* v0 = CD_cbready (word) */
	nop	/* load delay slot: nothing */
	beq	v0, zero, $L5f778	/* if (v0 == 0) goto $L5f778 */
	nop	/* delay slot: nothing */
	lbu	a0, 0(s2)	/* a0 = *(u8 *)(s2 + 0) */
	la	a1, g_8f6d0	/* a1 = &g_8f6d0 */
	jalr	ra, v0
	nop	/* delay slot: nothing */
$L5f778:
	andi	v0, s0, 0x2	/* v0 = s0 & 0x2 */
$L5f77c:
	beq	v0, zero, $L5f734	/* if (v0 == 0) goto $L5f734 */
	nop	/* delay slot: nothing */
	lw	v1, CD_cbsync	/* v1 = CD_cbsync (word) */
	nop	/* load delay slot: nothing */
	beq	v1, zero, $L5f734	/* if (v1 == 0) goto $L5f734 */
	nop	/* delay slot: nothing */
	la	v0, g_747d4+1	/* v0 = &g_747d4+1 */
	lbu	a0, 0(v0)	/* a0 = *(u8 *)(v0 + 0) */
	la	a1, g_8f6c8	/* a1 = &g_8f6c8 */
	jalr	ra, v1
	nop	/* delay slot: nothing */
	j	$L5f734	/* goto $L5f734 */
	nop	/* delay slot: nothing */

	/* Nothing left: restore the CDREG0 bank selected on entry */
$L5f7bc:
	lw	v0, g_747bc	/* v0 = CDREG0 pointer (g_747bc) */
	nop	/* load delay slot: nothing */
	sb	s1, 0(v0)	/* *(u8 *)(v0 + 0) = s1 */
	lw	ra, 28(sp)	/* restore ra */
	lw	s2, 24(sp)	/* restore s2 */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 32	/* release 32 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
/*
 * f_5f7e8 - ready callback while a CD_readm read runs
 *
 * Purpose:   void read_ready(int code, u_char *result). DataReady (1): if sectors remain, move one sector
 *            with CD_getsector(g_747e4, g_747ec), advance the write pointer and count it down. Any other
 *            code marks the read failed (g_747f0 = -1). A failed read restarts through f_5ee60; a finished or
 *            failed read restores the saved callbacks, issues CdlPause (9) and reports to CD_cbread
 *            (code 2 Complete, or 5 DiskError when sectors were left).
 * Inputs:    a0 = ready code, a1 = response bytes.   Frame 32 bytes.
 */
	.globl	f_5f7e8
f_5f7e8:
	addiu	sp, sp, -32	/* allocate 32 byte frame */
	sw	s1, 20(sp)	/* save s1 */
	addu	s1, a1, zero	/* s1 = a1 */
	ori	v0, zero, 0x5	/* v0 = 5 */
	sw	ra, 24(sp)	/* save ra */
	bne	a0, v0, $L5f820	/* if (a0 != v0) goto $L5f820 */
	sw	s0, 16(sp)	/* [delay slot, real work] save s0 */
	la	v0, g_747d4+1	/* v0 = &g_747d4+1 */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(v0 + 0) */
	nop	/* load delay slot: nothing */
	andi	v0, v0, 0xff	/* v0 = v0 & 0xff */
	beq	v0, a0, $L5f934	/* if (v0 == a0) goto $L5f934 */
	nop	/* delay slot: nothing */

	/* DataReady: move one sector */
$L5f820:
	ori	v0, zero, 0x1	/* v0 = 1 */
	bne	a0, v0, $L5f898	/* if (a0 != v0) goto $L5f898 */
	addiu	v0, zero, -1	/* [delay slot, real work] v0 = -1 */
	la	s0, g_747f0	/* s0 = &g_747f0 */
	lw	v0, 0(s0)	/* v0 = *(word *)(s0 + 0) */
	nop	/* load delay slot: nothing */
	blez	v0, $L5f8ac	/* if (v0 <= 0) goto $L5f8ac */
	nop	/* delay slot: nothing */
	lw	a0, g_747e4	/* a0 = g_747e4 (word) */
	lw	a1, g_747ec	/* a1 = g_747ec (word) */
	jal	CD_getsector	/* call CD_getsector */
	nop	/* delay slot: nothing */
	lw	v0, g_747ec	/* v0 = g_747ec (word) */
	lw	v1, g_747e4	/* v1 = g_747e4 (word) */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, g_747e4	/* g_747e4 = v0 */
	lw	v0, 0(s0)	/* v0 = *(word *)(s0 + 0) */
	nop	/* load delay slot: nothing */
	addiu	v0, v0, -1	/* v0 = v0 - 1 */
	sw	v0, 0(s0)	/* *(word *)(s0 + 0) = v0 */
	lw	v0, 0(s0)	/* v0 = *(word *)(s0 + 0) */
	j	$L5f8a4	/* goto $L5f8a4 */
	nop	/* delay slot: nothing */
$L5f898:
	la	v1, g_747f0	/* v1 = &g_747f0 */
	sw	v0, 0(v1)	/* *(word *)(v1 + 0) = v0 */

	/* Read failed? restart */
$L5f8a4:
	la	s0, g_747f0	/* s0 = &g_747f0 */
$L5f8ac:
	lw	v0, 0(s0)	/* v0 = *(word *)(s0 + 0) */
	nop	/* load delay slot: nothing */
	bgez	v0, $L5f8c4	/* if (v0 >= 0) goto $L5f8c4 */
	nop	/* delay slot: nothing */
	jal	f_5ee60	/* call f_5ee60 */
	nop	/* delay slot: nothing */

	/* Still reading: return */
$L5f8c4:
	lw	v0, 0(s0)	/* v0 = *(word *)(s0 + 0) */
	nop	/* load delay slot: nothing */
	bgtz	v0, $L5f934	/* if (v0 > 0) goto $L5f934 */

	/* Finished/failed: restore CD_cbsync/CD_cbready, CdlPause, notify CD_cbread */
	ori	a0, zero, 0x9	/* [delay slot, real work] a0 = 9 */
	addu	a1, zero, zero	/* a1 = zero */
	addu	a2, zero, zero	/* a2 = zero */
	lw	v0, g_747f8	/* v0 = g_747f8 (word) */
	lw	v1, g_747fc	/* v1 = g_747fc (word) */
	sw	v0, CD_cbsync	/* CD_cbsync = v0 */
	sw	v1, CD_cbready	/* CD_cbready = v1 */
	jal	CD_cw	/* call CD_cw */
	addu	a3, zero, zero	/* [delay slot, real work] a3 = zero */
	lw	v1, CD_cbread	/* v1 = CD_cbread (word) */
	nop	/* load delay slot: nothing */
	beq	v1, zero, $L5f934	/* if (v1 == 0) goto $L5f934 */
	nop	/* delay slot: nothing */
	lw	v0, 0(s0)	/* v0 = *(word *)(s0 + 0) */
	nop	/* load delay slot: nothing */
	bne	v0, zero, $L5f92c	/* if (v0 != 0) goto $L5f92c */
	ori	a0, zero, 0x5	/* [delay slot, real work] a0 = 5 */
	ori	a0, zero, 0x2	/* a0 = 2 */
$L5f92c:
	jalr	ra, v1
	addu	a1, s1, zero	/* [delay slot, real work] a1 = s1 */
$L5f934:
	lw	ra, 24(sp)	/* restore ra */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 32	/* release 32 byte frame */
	jr	ra	/* return */
	nop	/* delay slot: nothing */
