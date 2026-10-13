/*
 * libgpu_sys.s - LIBGPU module SYS: the GPU driver core (ResetGraph, DrawSync, LoadImage, PutDrawEnv ...).
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Assembled by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libgpu_sys.json and the
 * "libgpu_sys" entry of config/data.json; counted as "library assembly", never as matched C
 * and never as certified library.
 *
 * Library:  LIBGPU.LIB (Sony Runtime Library, PlayStation GPU).  Module: SYS (source sys.c).
 * Extent:   .text 0x800623b8..0x80064fe7 (11312 bytes), .rdata 0x800131b4 (536 bytes),
 *           .data 0x800759fc (360 bytes).  The module's .bss (320 bytes at 0x80091af4 and the
 *           6144-byte queue _que at 0x8009d6cc) is outside the library_asm ranges, so it is
 *           referenced as imported symbols at its retail addresses (g_91af4, ctlbuf, _que).
 *
 * Exports (text): ResetGraph SetGraphReverse SetGraphDebug SetGraphQueue GetGraphType GetGraphDebug
 *           DrawSyncCallback SetDispMask DrawSync ClearImage LoadImage StoreImage MoveImage
 *           ClearOTag ClearOTagR DrawPrim DrawOTag PutDrawEnv GetDrawEnv PutDispEnv GetDispEnv
 *           GetODE SetTexWindow SetDrawArea SetDrawOffset SetPriority SetDrawMode SetDrawEnv.
 *           (data) GPU_printf, _qlog, _qin, _qout.
 * Static routines (26): the ten-odd driver entries held in the jump table jtb (_addque ... _sync,
 *           named as in the SDK sample object BS.LIB:GPUSYS, a sibling build of the same sys.c),
 *           the GPU command builders get_mode/get_cs/get_ce/get_ofs/get_tw/get_dx, the alarm
 *           helpers set_alarm/get_alarm, memset, and two unnamed ones (f_628c4, f_64eec).
 * Imports:  printf ResetCallback GPU_cw DMACallback memcpy GetVideoMode SetIntrMask VSync.
 *
 * Nearest archived versions (relocation-masked word comparison of .text, tools in
 * builds/lane_tools are not kept; method: analysis/data_survey/moddiff.py plus an
 * insertion/deletion-tolerant alignment):
 *   - Runtime 3.4 LIB34 LIBGPU SYS (11300 bytes): closest. 161 edit words. .data identical
 *     (0 differing words, 18 relocated), .rdata differs only in the $Id string.
 *   - Runtime 3.3 LIBGPU SYS (11316 bytes): 230 edit words, .data identical, .rdata differs
 *     only in the $Id string.
 *   - $Id of retail: "sys.c,v 1.104 1995/09/01"; 3.3 is 1.107 (1995/10/18), 3.4 is 1.116
 *     (1995/12/01).  Retail is the OLDEST revision, yet nearer to 3.4 in its clamp idiom.
 * Word-exact to both 3.3 and 3.4 (relocation-masked): every routine except the ones below.
 * Differences, per routine:
 *   SetDrawEnv (f_63540): frame 0x30 (3.3/3.4: 0x28), the RECT clamp scheduled differently
 *       (retail "bltz a0 / lui / lhu / slt a0" shape); 3.3 also clamps to w-1 where retail clamps to w.
 *   get_cs, get_ce: same clamp shape difference (3.3: 26+2 words, 3.4: 25+2 words differ).
 *   _clr (f_63c78): frame 0x30 (3.3/3.4: 0x20), clamp shape as above.
 *   _dws, _drs: identical to 3.4, but differ from 3.3 (frame size and clamp shape: 41/39 words).
 *   _addque2 (f_644b8): vs 3.4: GPUSTAT ready bit 0x10000000 where 3.4 tests 0x04000000, the
 *       3.4 call to a helper before the DMA test is absent, and the tail is reordered (6+6+4 words).
 *   _exeque (f_647b4): vs 3.4: the same 0x10000000 versus 0x04000000 constant (2 words).
 *   f_64eec (GPU type probe): both versions end with an extra "lui 0x2000 / ori 0x504 / sw" sequence
 *       that retail lacks (4+4 words).
 * Everything else is relocation-targets only (retail addresses differ, as expected).
 */
#include <asm.h>


/* GPU state block g_75a44 (128 bytes, cleared by ResetGraph): field offsets */
#define GS_TYPE		0	/* u8  GPU/graph type 0..4 (GetGraphType) */
#define GS_QUEUE	1	/* u8  queue mode: 0 = direct, 1 = DMA queue (SetGraphQueue) */
#define GS_DEBUG	2	/* u8  debug level (SetGraphDebug) */
#define GS_REVERSE	3	/* u8  display reverse flag (SetGraphReverse) */
#define GS_WIDTH	4	/* s16 VRAM width limit */
#define GS_HEIGHT	6	/* s16 VRAM height limit */
#define GS_BUSY		8	/* word: a queued draw callback is pending */
#define GS_CALLBACK	12	/* word: DrawSync callback (DrawSyncCallback) */
#define GS_DRAWENV	16	/* DRAWENV copy, 0x5c bytes (PutDrawEnv/GetDrawEnv) */
#define GS_DISPENV	108	/* DISPENV copy, 0x14 bytes (PutDispEnv/GetDispEnv) */

/* jtb slot offsets (jump table of driver entries, called through jtbp) */
#define J_ADDQUE	0x04
#define J_ADDQUE2	0x08	/* (fn, a, nbytes, b): queue or run fn */
#define J_CLR		0x0c
#define J_CTL		0x10	/* GP1 control word write (also shadows it in ctlbuf) */
#define J_CWB		0x14
#define J_CWC		0x18
#define J_DRS		0x1c
#define J_DWS		0x20
#define J_EXEQUE	0x24
#define J_GETCTL	0x28
#define J_OTC		0x2c
#define J_PARAM		0x30
#define J_RESET		0x34
#define J_STATUS	0x38
#define J_SYNC		0x3c

	.set	noreorder	/* every delay slot is written out by hand */

/*
 * .rdata of SYS (0x800131b4..0x800133cb, 536 bytes): the module's string literals. Compiler
 * labels $LCn (GCC style, no invented names); each is one item placed by position in
 * config/data.json. $LC0 is the RCS id, pointed to by jtb[0]; the rest are the debug
 * messages printed through GPU_printf and the function-name arguments of f_628c4.
 * Differs from Runtime 3.3/3.4 only in $LC0 (revision 1.104 of 1995/09/01).
 */
	.rdata
$LC0:
	.ascii	"$Id: sys.c,v 1.104 1995/09/01 02:20:49 suzu Exp $\000"
	.align	2
$LC1:
	.ascii	"ResetGraph:jtb=%08x,env=%08x\n\000"
	.align	2
$LC2:
	.ascii	"ResetGraph(%d)...\n\000"
	.align	2
$LC3:
	.ascii	"SetGraphReverse(%d)...\n\000"
	.align	2
$LC4:
	.ascii	"SetGraphDebug:level:%d,type:%d reverse:%d\n\000"
	.align	2
$LC5:
	.ascii	"SetGrapQue(%d)...\n\000"
	.align	2
$LC6:
	.ascii	"DrawSyncCallback(%08x)...\n\000"
	.align	2
$LC7:
	.ascii	"SetDispMask(%d)...\n\000"
	.align	2
$LC8:
	.ascii	"DrawSync(%d)...\n\000"
	.align	2
$LC9:
	.ascii	"%s:bad RECT\000"
	.align	2
$LC10:
	.ascii	"(%d,%d)-(%d,%d)\n\000"
	.align	2
$LC11:
	.ascii	"%s:\000"
	.align	2
$LC12:
	.ascii	"ClearImage\000"
	.align	2
$LC13:
	.ascii	"LoadImage\000"
	.align	2
$LC14:
	.ascii	"StoreImage\000"
	.align	2
$LC15:
	.ascii	"MoveImage\000"
	.align	2
$LC16:
	.ascii	"ClearOTag(%08x,%d)...\n\000"
	.align	2
$LC17:
	.ascii	"ClearOTagR(%08x,%d)...\n\000"
	.align	2
$LC18:
	.ascii	"DrawOTag(%08x)...\n\000"
	.align	2
$LC19:
	.ascii	"PutDrawEnv(%08x)...\n\000"
	.align	2
$LC20:
	.ascii	"PutDispEnv(%08x)...\n\000"
	.align	2
$LC21:
	.ascii	"GPU timeout:que=%d,stat=%08x,chcr=%08x,madr=%08x,\000"
	.align	2
$LC22:
	.ascii	"func=(%08x)(%08x,%08x)\n\000"
	.align	2

/*
 * .data of SYS (0x800759fc..0x80075b63, 360 bytes). Identical to Runtime 3.3 and 3.4 SYS .data
 * (0 differing words, 18 relocated words). Names: GPU_printf, _qlog, _qin and _qout are the
 * module's exports; jtb, jtbp, gp0, gp1, d2_*, d6_*, d_pcr are the names of the same variables
 * in the SDK sample object BS.LIB:GPUSYS; the rest keep address names. All items are
 * statics unless .globl.
 */
	.data
/* jtb - the driver jump table: 16 words, slot 0 the RCS id string, slots 1..15 the static
 * driver routines in alphabetical order. Every public entry calls through it (jtbp). Read by
 * all exported routines via jtbp, and by ResetGraph (prints its address). */
jtb:
	.word	$LC0	/* [0] RCS id */
	.word	_addque	/* [1] _addque */
	.word	_addque2	/* [2] _addque2 */
	.word	_clr	/* [3] _clr */
	.word	_ctl	/* [4] _ctl */
	.word	_cwb	/* [5] _cwb */
	.word	_cwc	/* [6] _cwc */
	.word	_drs	/* [7] _drs */
	.word	_dws	/* [8] _dws */
	.word	_exeque	/* [9] _exeque */
	.word	_getctl	/* [10] _getctl */
	.word	_otc	/* [11] _otc */
	.word	_param	/* [12] _param */
	.word	_reset	/* [13] _reset */
	.word	_status	/* [14] _status */
	.word	_sync	/* [15] _sync */
/* jtbp - pointer to jtb (the "current driver"); every exported routine loads it, then a slot. */
jtbp:
	.word	jtb
/* GPU_printf - exported debug printer pointer (= printf); also read by LIBGPU FONT and PRIM. */
	.globl	GPU_printf
GPU_printf:
	.word	printf
/* g_75a44 - the 128-byte GPU state block, cleared by ResetGraph; field layout: see GS_* above.
 * Contains the last DRAWENV (GS_DRAWENV, 0x5c) and DISPENV (GS_DISPENV, 0x14) written. */
g_75a44:
	.space	128
/* g_75ac4 - VRAM width limit per graph type 0..4 (words), copied to GS_WIDTH by ResetGraph. */
g_75ac4:
	.word	0x400, 0x400, 0x400, 0x400, 0x400
/* g_75ad8 - VRAM height limit per graph type 0..4 (words), copied to GS_HEIGHT by ResetGraph. */
g_75ad8:
	.word	0x200, 0x400, 0x400, 0x200, 0x400
/* g_75aec - static DMA packet for MoveImage: tag 0x04ffffff (4 words, end), command 0x80000000
 * (GP0 80h copy rectangle), source xy, destination xy, size (filled by MoveImage). */
g_75aec:
	.word	0x4ffffff, 0x80000000, 0x0, 0x0, 0x0
/* g_75b00 - static end-of-list DMA packet (tag 0x04ffffff); ClearOTag/ClearOTagR link the last entry to it. */
g_75b00:
	.word	0x4ffffff, 0x0, 0x0, 0x0, 0x0
/* gp0 - pointer to GP0/GPUREAD, 0x1f801810. */
gp0:
	.word	0x1f801810
/* gp1 - pointer to GP1/GPUSTAT, 0x1f801814. */
gp1:
	.word	0x1f801814
/* d2_madr - DMA channel 2 (GPU) MADR, 0x1f8010a0. */
d2_madr:
	.word	0x1f8010a0
/* d2_bcr - DMA channel 2 BCR, 0x1f8010a4. */
d2_bcr:
	.word	0x1f8010a4
/* d2_chcr - DMA channel 2 CHCR, 0x1f8010a8. */
d2_chcr:
	.word	0x1f8010a8
/* d6_madr - DMA channel 6 (OTC) MADR, 0x1f8010e0. */
d6_madr:
	.word	0x1f8010e0
/* d6_bcr - DMA channel 6 BCR, 0x1f8010e4. */
d6_bcr:
	.word	0x1f8010e4
/* d6_chcr - DMA channel 6 CHCR, 0x1f8010e8. */
d6_chcr:
	.word	0x1f8010e8
/* d_pcr - DMA control register DPCR, 0x1f8010f0. */
d_pcr:
	.word	0x1f8010f0
/* _qlog - exported: the last queue entry run by _exeque: [0] function, [1] arg0, [2] arg1, [3] unused;
 * printed by the GPU-timeout diagnostic. */
	.globl	_qlog
_qlog:
	.space	16
/* _qin - exported: queue write index (0..63); queue empty when _qin == _qout. */
	.globl	_qin
_qin:
	.word	0x0
/* _qout - exported: queue read index (0..63). */
	.globl	_qout
_qout:
	.word	0x0
/* g_75b50 - interrupt mask saved by _addque2 around its critical section. */
g_75b50:
	.word	0x0
/* g_75b54 - interrupt mask saved by _exeque. */
g_75b54:
	.word	0x0
/* g_75b58 - interrupt mask saved by _reset / the timeout path. */
g_75b58:
	.word	0x0
/* g_75b5c - alarm deadline: VSync(-1) + 240 (set_alarm). */
g_75b5c:
	.word	0x0
/* g_75b60 - alarm poll counter, timeout when it exceeds 0xf0000 (get_alarm). */
g_75b60:
	.word	0x0

	.text

/*
 * ResetGraph - long ResetGraph(long mode)
 *
 * Purpose:   Mode 0: prints jtb/state addresses, clears the 0x80-byte state block, ResetCallback(), _reset(mode) sets the type and VRAM limits from the tables, wipes the DRAWENV/DISPENV copies to -1. Other modes: optionally log then call _reset-slot 1 (J_RESET).
 * Inputs:    a0 = mode (0 full reset, 1 cancel drawing, 3 reset registers only; bit 3 passed on)
 * Outputs:   v0 = previous/GPU type or mode result
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	ResetGraph
	.ent	ResetGraph
ResetGraph:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a0	/* s1 = a0 */
	andi	v0, s1, 7	/* v0 = s1 & 7 */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	bnez	v0, $L624ac	/* if (v0 != 0) goto $L624ac */
	sw	s0, 0x10(sp)	/* delay slot (real work): *(u32 *)(0x10 + sp) = s0 */
	/* -- mode 0: log jtb and state addresses, wipe state block, install callbacks, reset the GPU -- */
	la	a0, $LC1	/* a0 = &$LC1 [lui+op macro] */
	la	a1, jtb	/* a1 = &jtb [lui+op macro] */
	la	s0, g_75a44	/* s0 = &g_75a44 [lui+op macro] */
	jal	printf	/* call printf */
	move	a2, s0	/* delay slot (real work): a2 = s0 */
	move	a0, s0	/* a0 = s0 */
	move	a1, zero	/* a1 = zero */
	jal	memset	/* call memset */
	ori	a2, zero, 0x80	/* delay slot (real work): a2 = 0x80 */
	jal	ResetCallback	/* call ResetCallback */
	nop		/* delay slot: nothing to donothing */
	lui	v0, 0xff	/* v0 = 0xff << 16 */
	/* -- GPU_cw(jtb[0] & 0xffffff); _reset(mode) -> type; width/height limits from the per-type tables -- */
	lw	a0, jtbp	/* a0 = *(u32 *)(jtbp) [lui+op macro] */
	ori	v0, v0, 0xffff	/* v0 = v0 | 0xffff */
	jal	GPU_cw	/* call GPU_cw */
	and	a0, a0, v0	/* delay slot (real work): a0 = a0 & v0 */
	jal	_reset	/* call _reset */
	move	a0, s1	/* delay slot (real work): a0 = s1 */
	addiu	a0, s0, 0x10	/* a0 = s0 + 0x10 */
	sb	v0, 0(s0)	/* *(u8 *)(0 + s0) = v0 */
	lbu	v0, 0(s0)	/* v0 = *(u8 *)(0 + s0) */
	ori	v1, zero, 1	/* v1 = 1 */
	sb	v1, g_75a44+1	/* *(u8 *)(g_75a44+1) = v1 [lui+op macro, via $1] */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	.set	noat
	la	$1, g_75ac4	/* $1 = &g_75ac4 [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lhu	v1, 0($1)	/* v1 = *(u16 *)(0 + $1) */
	.set	at
	lbu	v0, 0(s0)	/* v0 = *(u8 *)(0 + s0) */
	addiu	a1, zero, -1	/* a1 = -1 */
	sll	v0, v0, 2	/* v0 = v0 << 2 */
	sh	v1, g_75a44+4	/* *(u16 *)(g_75a44+4) = v1 [lui+op macro, via $1] */
	.set	noat
	la	$1, g_75ad8	/* $1 = &g_75ad8 [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lhu	v0, 0($1)	/* v0 = *(u16 *)(0 + $1) */
	.set	at
	nop		/* nothing */
	sh	v0, g_75a44+6	/* *(u16 *)(g_75a44+6) = v0 [lui+op macro, via $1] */
	jal	memset	/* call memset */
	ori	a2, zero, 0x5c	/* delay slot (real work): a2 = 0x5c */
	addiu	a0, s0, 0x6c	/* a0 = s0 + 0x6c */
	addiu	a1, zero, -1	/* a1 = -1 */
	jal	memset	/* call memset */
	ori	a2, zero, 0x14	/* delay slot (real work): a2 = 0x14 */
	lbu	v0, 0(s0)	/* v0 = *(u8 *)(0 + s0) */
	j	$L624fc	/* goto $L624fc */
	nop		/* delay slot: nothing to donothing */
$L624ac:
	/* -- modes != 0: log (debug >= 2) and call jtb[J_RESET](1) -- */
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	nop		/* nothing */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L624e0	/* if (v0 != 0) goto $L624e0 */
	nop		/* delay slot: nothing to donothing */
	la	a0, $LC2	/* a0 = &$LC2 [lui+op macro] */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s1	/* delay slot (real work): a1 = s1 */
$L624e0:
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0x34(v0)	/* v0 = *(u32 *)(0x34 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	ori	a0, zero, 1	/* delay slot (real work): a0 = 1 */
$L624fc:
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	ResetGraph
/*
 * SetGraphReverse - long SetGraphReverse(long mode)
 *
 * Purpose:   Stores the reverse flag, rewrites the display-mode word through J_CTL (GP1 08h) and, on type 2 hardware, the extra GP1 20000500h/501h word.
 * Inputs:    a0 = mode (0 normal, 1 reverse)
 * Outputs:   v0 = previous mode
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetGraphReverse
	.ent	SetGraphReverse
SetGraphReverse:
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	la	s0, g_75a44+3	/* s0 = &g_75a44+3 [lui+op macro] */
	sw	ra, 0x1c(sp)	/* *(u32 *)(0x1c + sp) = ra */
	sw	s2, 0x18(sp)	/* *(u32 *)(0x18 + sp) = s2 */
	lbu	s2, 0(s0)	/* s2 = *(u8 *)(0 + s0) */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62564	/* if (v0 != 0) goto $L62564 */
	move	s1, a0	/* delay slot (real work): s1 = a0 */
	la	a0, $LC3	/* a0 = &$LC3 [lui+op macro] */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s1	/* delay slot (real work): a1 = s1 */
$L62564:
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	sb	s1, 0(s0)	/* *(u8 *)(0 + s0) = s1 */
	lw	v0, 0x28(v0)	/* v0 = *(u32 *)(0x28 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	ori	a0, zero, 8	/* delay slot (real work): a0 = 8 */
	lbu	v1, 0(s0)	/* v1 = *(u8 *)(0 + s0) */
	nop		/* nothing */
	beqz	v1, $L6259c	/* if (v1 == 0) goto $L6259c */
	move	a0, v0	/* delay slot (real work): a0 = v0 */
	lui	v0, 0x800	/* v0 = 0x800 << 16 */
	j	$L625a0	/* goto $L625a0 */
	ori	v0, v0, 0x80	/* delay slot (real work): v0 = v0 | 0x80 */
$L6259c:
	lui	v0, 0x800	/* v0 = 0x800 << 16 */
$L625a0:
	or	a0, a0, v0	/* a0 = a0 | v0 */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0x10(v0)	/* v0 = *(u32 *)(0x10 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	nop		/* delay slot: nothing to donothing */
	lbu	v1, g_75a44	/* v1 = *(u8 *)(g_75a44) [lui+op macro] */
	ori	v0, zero, 2	/* v0 = 2 */
	bne	v1, v0, $L6260c	/* if (v1 != v0) goto $L6260c */
	move	v0, s2	/* delay slot (real work): v0 = s2 */
	lui	a0, 0x2000	/* a0 = 0x2000 << 16 */
	lbu	v0, g_75a44+3	/* v0 = *(u8 *)(g_75a44+3) [lui+op macro] */
	lw	v1, jtbp	/* v1 = *(u32 *)(jtbp) [lui+op macro] */
	beqz	v0, $L625f8	/* if (v0 == 0) goto $L625f8 */
	ori	a0, a0, 0x504	/* delay slot (real work): a0 = a0 | 0x504 */
	lui	a0, 0x2000	/* a0 = 0x2000 << 16 */
	ori	a0, a0, 0x501	/* a0 = a0 | 0x501 */
$L625f8:
	lw	v0, 0x10(v1)	/* v0 = *(u32 *)(0x10 + v1) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	nop		/* delay slot: nothing to donothing */
	move	v0, s2	/* v0 = s2 */
$L6260c:
	lw	ra, 0x1c(sp)	/* ra = *(u32 *)(0x1c + sp) */
	lw	s2, 0x18(sp)	/* s2 = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetGraphReverse
/*
 * SetGraphDebug - long SetGraphDebug(long level)
 *
 * Purpose:   Stores the debug level; when non-zero prints level, type and reverse through GPU_printf.
 * Inputs:    a0 = level
 * Outputs:   v0 = previous level
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetGraphDebug
	.ent	SetGraphDebug
SetGraphDebug:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	la	v1, g_75a44+2	/* v1 = &g_75a44+2 [lui+op macro] */
	sw	ra, 0x14(sp)	/* *(u32 *)(0x14 + sp) = ra */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	lbu	s0, 0(v1)	/* s0 = *(u8 *)(0 + v1) */
	sb	a0, 0(v1)	/* *(u8 *)(0 + v1) = a0 */
	andi	a0, a0, 0xff	/* a0 = a0 & 0xff */
	beqz	a0, $L62680	/* if (a0 == 0) goto $L62680 */
	move	v0, s0	/* delay slot (real work): v0 = s0 */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	lbu	a1, 0(v1)	/* a1 = *(u8 *)(0 + v1) */
	lbu	a2, g_75a44	/* a2 = *(u8 *)(g_75a44) [lui+op macro] */
	lbu	a3, g_75a44+3	/* a3 = *(u8 *)(g_75a44+3) [lui+op macro] */
	la	a0, $LC4	/* a0 = &$LC4 [lui+op macro] */
	jalr	ra, v0	/* call *v0 */
	nop		/* delay slot: nothing to donothing */
	move	v0, s0	/* v0 = s0 */
$L62680:
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetGraphDebug
/*
 * SetGraphQueue - long SetGraphQueue(long mode)
 *
 * Purpose:   On a change, drains with J_RESET(1) and unhooks the DMA callback (DMACallback(2,0)).
 * Inputs:    a0 = mode (0 direct, 1 queued)
 * Outputs:   v0 = previous mode
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetGraphQueue
	.ent	SetGraphQueue
SetGraphQueue:
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	la	s1, g_75a44+1	/* s1 = &g_75a44+1 [lui+op macro] */
	sw	ra, 0x1c(sp)	/* *(u32 *)(0x1c + sp) = ra */
	sw	s2, 0x18(sp)	/* *(u32 *)(0x18 + sp) = s2 */
	lbu	s2, 0(s1)	/* s2 = *(u8 *)(0 + s1) */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L626e4	/* if (v0 != 0) goto $L626e4 */
	move	s0, a0	/* delay slot (real work): s0 = a0 */
	la	a0, $LC5	/* a0 = &$LC5 [lui+op macro] */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
$L626e4:
	lbu	v0, 0(s1)	/* v0 = *(u8 *)(0 + s1) */
	nop		/* nothing */
	beq	s0, v0, $L62724	/* if (s0 == v0) goto $L62724 */
	move	v0, s2	/* delay slot (real work): v0 = s2 */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0x34(v0)	/* v0 = *(u32 *)(0x34 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	ori	a0, zero, 1	/* delay slot (real work): a0 = 1 */
	ori	a0, zero, 2	/* a0 = 2 */
	move	a1, zero	/* a1 = zero */
	jal	DMACallback	/* call DMACallback */
	sb	s0, 0(s1)	/* delay slot (real work): *(u8 *)(0 + s1) = s0 */
	move	v0, s2	/* v0 = s2 */
$L62724:
	lw	ra, 0x1c(sp)	/* ra = *(u32 *)(0x1c + sp) */
	lw	s2, 0x18(sp)	/* s2 = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetGraphQueue
/*
 * GetGraphType - long GetGraphType(void)
 *
 * Purpose:   Returns the type byte of the state block.
 * Inputs:    none
 * Outputs:   v0 = GPU type 0..4
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	GetGraphType
	.ent	GetGraphType
GetGraphType:
	lbu	v0, g_75a44	/* v0 = *(u8 *)(g_75a44) [lui+op macro] */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	GetGraphType
/*
 * GetGraphDebug - long GetGraphDebug(void)
 *
 * Purpose:   Returns the debug byte of the state block.
 * Inputs:    none
 * Outputs:   v0 = debug level
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	GetGraphDebug
	.ent	GetGraphDebug
GetGraphDebug:
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	GetGraphDebug
/*
 * DrawSyncCallback - void *DrawSyncCallback(void (*cb)(void))
 *
 * Purpose:   Installs the callback run by _exeque when the queue drains.
 * Inputs:    a0 = callback or 0
 * Outputs:   v0 = previous callback
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	DrawSyncCallback
	.ent	DrawSyncCallback
DrawSyncCallback:
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62798	/* if (v0 != 0) goto $L62798 */
	sw	ra, 0x14(sp)	/* delay slot (real work): *(u32 *)(0x14 + sp) = ra */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	la	a0, $LC6	/* a0 = &$LC6 [lui+op macro] */
	jalr	ra, v0	/* call *v0 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
$L62798:
	lw	v0, g_75a44+12	/* v0 = *(u32 *)(g_75a44+12) [lui+op macro] */
	sw	s0, g_75a44+12	/* *(u32 *)(g_75a44+12) = s0 [lui+op macro, via $1] */
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	DrawSyncCallback
/*
 * SetDispMask - void SetDispMask(long mask)
 *
 * Purpose:   Wipes the DISPENV copy to -1 (forces the next PutDispEnv to rewrite) and sends GP1 03h display enable/disable.
 * Inputs:    a0 = 0 blank, nonzero display on
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetDispMask
	.ent	SetDispMask
SetDispMask:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	la	s1, g_75a44+2	/* s1 = &g_75a44+2 [lui+op macro] */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	lbu	v0, 0(s1)	/* v0 = *(u8 *)(0 + s1) */
	nop		/* nothing */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62804	/* if (v0 != 0) goto $L62804 */
	move	s0, a0	/* delay slot (real work): s0 = a0 */
	la	a0, $LC7	/* a0 = &$LC7 [lui+op macro] */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
$L62804:
	bnez	s0, $L62818	/* if (s0 != 0) goto $L62818 */
	addiu	a0, s1, 0x6a	/* delay slot (real work): a0 = s1 + 0x6a */
	addiu	a1, zero, -1	/* a1 = -1 */
	jal	memset	/* call memset */
	ori	a2, zero, 0x14	/* delay slot (real work): a2 = 0x14 */
$L62818:
	lui	a0, 0x300	/* a0 = 0x300 << 16 */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	beqz	s0, $L62830	/* if (s0 == 0) goto $L62830 */
	ori	a0, a0, 1	/* delay slot (real work): a0 = a0 | 1 */
	lui	a0, 0x300	/* a0 = 0x300 << 16 */
$L62830:
	lw	v0, 0x10(v0)	/* v0 = *(u32 *)(0x10 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	nop		/* delay slot: nothing to donothing */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetDispMask
/*
 * DrawSync - long DrawSync(long mode)
 *
 * Purpose:   Logs, then returns J_SYNC(mode).
 * Inputs:    a0 = mode (0 wait, 1 poll)
 * Outputs:   v0 = queue length remaining (0 = done)
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	DrawSync
	.ent	DrawSync
DrawSync:
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62894	/* if (v0 != 0) goto $L62894 */
	sw	ra, 0x14(sp)	/* delay slot (real work): *(u32 *)(0x14 + sp) = ra */
	la	a0, $LC8	/* a0 = &$LC8 [lui+op macro] */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
$L62894:
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0x3c(v0)	/* v0 = *(u32 *)(0x3c + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a0, s0	/* delay slot (real work): a0 = s0 */
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	DrawSync
/*
 * f_628c4 - static void f_628c4(char *name, RECT *r)
 *
 * Purpose:   Checks a RECT against the VRAM limits when debug level is 1 (width/height positive, non-negative origin, inside the limits) and prints "<name>:bad RECT" plus the rectangle; at level 2 prints name and rectangle always. Name not evidenced (the sample build has no such symbol).
 * Inputs:    a0 = caller name string, a1 = RECT
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	f_628c4
f_628c4:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	move	t0, a0	/* t0 = a0 */
	sw	s0, 0x18(sp)	/* *(u32 *)(0x18 + sp) = s0 */
	move	s0, a1	/* s0 = a1 */
	lbu	v1, g_75a44+2	/* v1 = *(u8 *)(g_75a44+2) [lui+op macro] */
	ori	v0, zero, 1	/* v0 = 1 */
	beq	v1, v0, $L628fc	/* if (v1 == v0) goto $L628fc */
	sw	ra, 0x1c(sp)	/* delay slot (real work): *(u32 *)(0x1c + sp) = ra */
	ori	v0, zero, 2	/* v0 = 2 */
	beq	v1, v0, $L62994	/* if (v1 == v0) goto $L62994 */
	nop		/* delay slot: nothing to donothing */
	j	$L629d8	/* goto $L629d8 */
	nop		/* delay slot: nothing to donothing */
$L628fc:
	lh	a1, 4(s0)	/* a1 = *(s16 *)(4 + s0) */
	lh	v1, g_75a44+4	/* v1 = *(s16 *)(g_75a44+4) [lui+op macro] */
	nop		/* nothing */
	slt	v0, v1, a1	/* v0 = (v1 < a1) */
	bnez	v0, $L62984	/* if (v0 != 0) goto $L62984 */
	nop		/* delay slot: nothing to donothing */
	lh	a3, 0(s0)	/* a3 = *(s16 *)(0 + s0) */
	nop		/* nothing */
	addu	v0, a1, a3	/* v0 = a1 + a3 */
	slt	v0, v1, v0	/* v0 = (v1 < v0) */
	bnez	v0, $L62984	/* if (v0 != 0) goto $L62984 */
	nop		/* delay slot: nothing to donothing */
	lh	v1, 2(s0)	/* v1 = *(s16 *)(2 + s0) */
	lh	a0, g_75a44+6	/* a0 = *(s16 *)(g_75a44+6) [lui+op macro] */
	nop		/* nothing */
	slt	v0, a0, v1	/* v0 = (a0 < v1) */
	bnez	v0, $L62984	/* if (v0 != 0) goto $L62984 */
	nop		/* delay slot: nothing to donothing */
	lh	a2, 6(s0)	/* a2 = *(s16 *)(6 + s0) */
	nop		/* nothing */
	addu	v0, v1, a2	/* v0 = v1 + a2 */
	slt	v0, a0, v0	/* v0 = (a0 < v0) */
	bnez	v0, $L62984	/* if (v0 != 0) goto $L62984 */
	nop		/* delay slot: nothing to donothing */
	blez	a1, $L62984	/* if (a1 <= 0) goto $L62984 */
	nop		/* delay slot: nothing to donothing */
	bltz	a3, $L62984	/* if (a3 < 0) goto $L62984 */
	nop		/* delay slot: nothing to donothing */
	bltz	v1, $L62984	/* if (v1 < 0) goto $L62984 */
	nop		/* delay slot: nothing to donothing */
	bgtz	a2, $L629d8	/* if (a2 > 0) goto $L629d8 */
	nop		/* delay slot: nothing to donothing */
$L62984:
	la	a0, $LC9	/* a0 = &$LC9 [lui+op macro] */
	j	$L6299c	/* goto $L6299c */
	nop		/* delay slot: nothing to donothing */
$L62994:
	la	a0, $LC11	/* a0 = &$LC11 [lui+op macro] */
$L6299c:
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, t0	/* delay slot (real work): a1 = t0 */
	lh	a1, 0(s0)	/* a1 = *(s16 *)(0 + s0) */
	lh	a2, 2(s0)	/* a2 = *(s16 *)(2 + s0) */
	lh	a3, 4(s0)	/* a3 = *(s16 *)(4 + s0) */
	lh	v0, 6(s0)	/* v0 = *(s16 *)(6 + s0) */
	lw	v1, GPU_printf	/* v1 = *(u32 *)(GPU_printf) [lui+op macro] */
	la	a0, $LC10	/* a0 = &$LC10 [lui+op macro] */
	jalr	ra, v1	/* call *v1 */
	sw	v0, 0x10(sp)	/* delay slot (real work): *(u32 *)(0x10 + sp) = v0 */
$L629d8:
	lw	ra, 0x1c(sp)	/* ra = *(u32 *)(0x1c + sp) */
	lw	s0, 0x18(sp)	/* s0 = *(u32 *)(0x18 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	f_628c4
/*
 * ClearImage - int ClearImage(RECT *r, u_char r, u_char g, u_char b)
 *
 * Purpose:   Validates the rect (f_628c4), packs the colour 0bbggrrh and queues _clr via _addque2(J_CLR, rect, 8, colour).
 * Inputs:    a0 = rect, a1 = red, a2 = green, a3 = blue
 * Outputs:   v0 = queue result
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	ClearImage
	.ent	ClearImage
ClearImage:
	addiu	sp, sp, -40	/* sp = sp + -40 */
	sw	s3, 0x1c(sp)	/* *(u32 *)(0x1c + sp) = s3 */
	move	s3, a0	/* s3 = a0 */
	la	a0, $LC12	/* a0 = &$LC12 [lui+op macro] */
	sw	s2, 0x18(sp)	/* *(u32 *)(0x18 + sp) = s2 */
	move	s2, a1	/* s2 = a1 */
	move	a1, s3	/* a1 = s3 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a2	/* s1 = a2 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	sw	ra, 0x20(sp)	/* *(u32 *)(0x20 + sp) = ra */
	jal	f_628c4	/* call f_628c4 */
	move	s0, a3	/* delay slot (real work): s0 = a3 */
	move	a1, s3	/* a1 = s3 */
	andi	s0, s0, 0xff	/* s0 = s0 & 0xff */
	sll	s0, s0, 16	/* s0 = s0 << 16 */
	andi	s1, s1, 0xff	/* s1 = s1 & 0xff */
	sll	s1, s1, 8	/* s1 = s1 << 8 */
	or	s0, s0, s1	/* s0 = s0 | s1 */
	andi	s2, s2, 0xff	/* s2 = s2 & 0xff */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	ori	a2, zero, 8	/* a2 = 8 */
	lw	a0, 0xc(v0)	/* a0 = *(u32 *)(0xc + v0) */
	lw	v0, 8(v0)	/* v0 = *(u32 *)(8 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	or	a3, s0, s2	/* delay slot (real work): a3 = s0 | s2 */
	lw	ra, 0x20(sp)	/* ra = *(u32 *)(0x20 + sp) */
	lw	s3, 0x1c(sp)	/* s3 = *(u32 *)(0x1c + sp) */
	lw	s2, 0x18(sp)	/* s2 = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x28	/* sp = sp + 0x28 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	ClearImage
/*
 * LoadImage - int LoadImage(RECT *r, u_long *p)
 *
 * Purpose:   Validates the rect and queues _dws (CPU to VRAM) via _addque2.
 * Inputs:    a0 = rect, a1 = pixel data
 * Outputs:   v0 = queue result
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	LoadImage
	.ent	LoadImage
LoadImage:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a1	/* s1 = a1 */
	la	a0, $LC13	/* a0 = &$LC13 [lui+op macro] */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	jal	f_628c4	/* call f_628c4 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
	move	a1, s0	/* a1 = s0 */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	ori	a2, zero, 8	/* a2 = 8 */
	lw	a0, 0x20(v0)	/* a0 = *(u32 *)(0x20 + v0) */
	lw	v0, 8(v0)	/* v0 = *(u32 *)(8 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a3, s1	/* delay slot (real work): a3 = s1 */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	LoadImage
/*
 * StoreImage - int StoreImage(RECT *r, u_long *p)
 *
 * Purpose:   Validates the rect and queues _drs (VRAM to CPU) via _addque2.
 * Inputs:    a0 = rect, a1 = destination
 * Outputs:   v0 = queue result
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	StoreImage
	.ent	StoreImage
StoreImage:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a1	/* s1 = a1 */
	la	a0, $LC14	/* a0 = &$LC14 [lui+op macro] */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	jal	f_628c4	/* call f_628c4 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
	move	a1, s0	/* a1 = s0 */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	ori	a2, zero, 8	/* a2 = 8 */
	lw	a0, 0x1c(v0)	/* a0 = *(u32 *)(0x1c + v0) */
	lw	v0, 8(v0)	/* v0 = *(u32 *)(8 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a3, s1	/* delay slot (real work): a3 = s1 */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	StoreImage
/*
 * MoveImage - int MoveImage(RECT *r, long x, long y)
 *
 * Purpose:   Fills the static packet g_75aec (GP0 80h) and queues J_CWC on it.
 * Inputs:    a0 = source rect, a1 = dest x, a2 = dest y
 * Outputs:   v0 = queue result, -1 for an empty rect
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	MoveImage
	.ent	MoveImage
MoveImage:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sw	s2, 0x18(sp)	/* *(u32 *)(0x18 + sp) = s2 */
	move	s2, a1	/* s2 = a1 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a2	/* s1 = a2 */
	la	a0, $LC15	/* a0 = &$LC15 [lui+op macro] */
	sw	ra, 0x1c(sp)	/* *(u32 *)(0x1c + sp) = ra */
	jal	f_628c4	/* call f_628c4 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
	lh	v0, 4(s0)	/* v0 = *(s16 *)(4 + s0) */
	nop		/* nothing */
	beqz	v0, $L62bf0	/* if (v0 == 0) goto $L62bf0 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
	lh	v0, 6(s0)	/* v0 = *(s16 *)(6 + s0) */
	nop		/* nothing */
	bnez	v0, $L62ba0	/* if (v0 != 0) goto $L62ba0 */
	sll	v0, s1, 16	/* delay slot (real work): v0 = s1 << 16 */
	j	$L62bf0	/* goto $L62bf0 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
$L62ba0:
	andi	v1, s2, 0xffff	/* v1 = s2 & 0xffff */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	la	a1, g_75aec+8	/* a1 = &g_75aec+8 [lui+op macro] */
	lw	a0, 0(s0)	/* a0 = *(u32 *)(0 + s0) */
	lw	v1, jtbp	/* v1 = *(u32 *)(jtbp) [lui+op macro] */
	ori	a2, zero, 0x14	/* a2 = 0x14 */
	sw	v0, g_75aec+12	/* *(u32 *)(g_75aec+12) = v0 [lui+op macro, via $1] */
	sw	a0, 0(a1)	/* *(u32 *)(0 + a1) = a0 */
	lw	v0, 4(s0)	/* v0 = *(u32 *)(4 + s0) */
	move	a3, zero	/* a3 = zero */
	sw	v0, g_75aec+16	/* *(u32 *)(g_75aec+16) = v0 [lui+op macro, via $1] */
	lw	a0, 0x18(v1)	/* a0 = *(u32 *)(0x18 + v1) */
	lw	v0, 8(v1)	/* v0 = *(u32 *)(8 + v1) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	addiu	a1, a1, -8	/* delay slot (real work): a1 = a1 + -8 */
$L62bf0:
	lw	ra, 0x1c(sp)	/* ra = *(u32 *)(0x1c + sp) */
	lw	s2, 0x18(sp)	/* s2 = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	MoveImage
/*
 * ClearOTag - OT *ClearOTag(u_long *ot, long n)
 *
 * Purpose:   Links entries 0..n-2 to the next ascending, the last to the end packet g_75b00.
 * Inputs:    a0 = table, a1 = length
 * Outputs:   v0 = last entry
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	ClearOTag
	.ent	ClearOTag
ClearOTag:
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a1	/* s1 = a1 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62c50	/* if (v0 != 0) goto $L62c50 */
	sw	ra, 0x18(sp)	/* delay slot (real work): *(u32 *)(0x18 + sp) = ra */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	la	a0, $LC16	/* a0 = &$LC16 [lui+op macro] */
	move	a1, s0	/* a1 = s0 */
	jalr	ra, v0	/* call *v0 */
	move	a2, s1	/* delay slot (real work): a2 = s1 */
$L62c50:
	/* -- ClearOTag: link entry i to i+1 (24-bit address) for n-1 entries -- */
	addiu	s1, s1, -1	/* s1 = s1 + -1 */
	beqz	s1, $L62c94	/* if (s1 == 0) goto $L62c94 */
	lui	v1, 0xff	/* delay slot (real work): v1 = 0xff << 16 */
	lui	a1, 0xff	/* a1 = 0xff << 16 */
	ori	a1, a1, 0xffff	/* a1 = a1 | 0xffff */
	lui	a2, 0xff00	/* a2 = 0xff00 << 16 */
$L62c68:
	addiu	s1, s1, -1	/* s1 = s1 + -1 */
	addiu	a0, s0, 4	/* a0 = s0 + 4 */
	sb	zero, 3(s0)	/* *(u8 *)(3 + s0) = zero */
	lw	v0, 0(s0)	/* v0 = *(u32 *)(0 + s0) */
	and	v1, a0, a1	/* v1 = a0 & a1 */
	and	v0, v0, a2	/* v0 = v0 & a2 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(s0)	/* *(u32 *)(0 + s0) = v0 */
	bnez	s1, $L62c68	/* if (s1 != 0) goto $L62c68 */
	move	s0, a0	/* delay slot (real work): s0 = a0 */
	lui	v1, 0xff	/* v1 = 0xff << 16 */
$L62c94:
	/* -- last entry points at the end packet g_75b00 -- */
	ori	v1, v1, 0xffff	/* v1 = v1 | 0xffff */
	la	v0, g_75b00	/* v0 = &g_75b00 [lui+op macro] */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	sw	v0, 0(s0)	/* *(u32 *)(0 + s0) = v0 */
	move	v0, s0	/* v0 = s0 */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	ClearOTag
/*
 * ClearOTagR - OT *ClearOTagR(u_long *ot, long n)
 *
 * Purpose:   Builds the reverse list with the DMA6 clear (J_OTC), then terminates entry 0 with g_75b00.
 * Inputs:    a0 = table, a1 = length
 * Outputs:   v0 = table
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	ClearOTagR
	.ent	ClearOTagR
ClearOTagR:
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a1	/* s1 = a1 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62d0c	/* if (v0 != 0) goto $L62d0c */
	sw	ra, 0x18(sp)	/* delay slot (real work): *(u32 *)(0x18 + sp) = ra */
	la	a0, $LC17	/* a0 = &$LC17 [lui+op macro] */
	move	a1, s0	/* a1 = s0 */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a2, s1	/* delay slot (real work): a2 = s1 */
$L62d0c:
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	move	a0, s0	/* a0 = s0 */
	lw	v0, 0x2c(v0)	/* v0 = *(u32 *)(0x2c + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s1	/* delay slot (real work): a1 = s1 */
	lui	a0, 0xff	/* a0 = 0xff << 16 */
	ori	a0, a0, 0xffff	/* a0 = a0 | 0xffff */
	move	v0, s0	/* v0 = s0 */
	la	v1, g_75b00	/* v1 = &g_75b00 [lui+op macro] */
	and	v1, v1, a0	/* v1 = v1 & a0 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	ClearOTagR
/*
 * DrawPrim - void DrawPrim(void *p)
 *
 * Purpose:   J_SYNC(0) then writes the primitive words through J_CWB, size from the tag byte.
 * Inputs:    a0 = primitive
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	DrawPrim
	.ent	DrawPrim
DrawPrim:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	lw	v0, 0x3c(v0)	/* v0 = *(u32 *)(0x3c + v0) */
	lbu	s1, 3(s0)	/* s1 = *(u8 *)(3 + s0) */
	jalr	ra, v0	/* call *v0 */
	move	a0, zero	/* delay slot (real work): a0 = zero */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	addiu	a0, s0, 4	/* a0 = s0 + 4 */
	lw	v0, 0x14(v0)	/* v0 = *(u32 *)(0x14 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s1	/* delay slot (real work): a1 = s1 */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	DrawPrim
/*
 * DrawOTag - void DrawOTag(u_long *ot)
 *
 * Purpose:   Queues the table (J_CWC) through _addque2.
 * Inputs:    a0 = table
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	DrawOTag
	.ent	DrawOTag
DrawOTag:
	lbu	v0, g_75a44+2	/* v0 = *(u8 *)(g_75a44+2) [lui+op macro] */
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62df8	/* if (v0 != 0) goto $L62df8 */
	sw	ra, 0x14(sp)	/* delay slot (real work): *(u32 *)(0x14 + sp) = ra */
	la	a0, $LC18	/* a0 = &$LC18 [lui+op macro] */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
$L62df8:
	move	a1, s0	/* a1 = s0 */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	move	a2, zero	/* a2 = zero */
	lw	a0, 0x18(v0)	/* a0 = *(u32 *)(0x18 + v0) */
	lw	v0, 8(v0)	/* v0 = *(u32 *)(8 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a3, zero	/* delay slot (real work): a3 = zero */
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	DrawOTag
/*
 * PutDrawEnv - DRAWENV *PutDrawEnv(DRAWENV *env)
 *
 * Purpose:   Builds the primitive in env->dr_env with SetDrawEnv, queues it (J_CWC via _addque2), copies env to the state block.
 * Inputs:    a0 = environment
 * Outputs:   v0 = env
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	PutDrawEnv
	.ent	PutDrawEnv
PutDrawEnv:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s2, 0x18(sp)	/* *(u32 *)(0x18 + sp) = s2 */
	la	s2, g_75a44+2	/* s2 = &g_75a44+2 [lui+op macro] */
	sw	ra, 0x1c(sp)	/* *(u32 *)(0x1c + sp) = ra */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	lbu	v0, 0(s2)	/* v0 = *(u8 *)(0 + s2) */
	nop		/* nothing */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62e7c	/* if (v0 != 0) goto $L62e7c */
	move	s1, a0	/* delay slot (real work): s1 = a0 */
	la	a0, $LC19	/* a0 = &$LC19 [lui+op macro] */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s1	/* delay slot (real work): a1 = s1 */
$L62e7c:
	addiu	s0, s1, 0x1c	/* s0 = s1 + 0x1c */
	move	a0, s0	/* a0 = s0 */
	jal	SetDrawEnv	/* call SetDrawEnv */
	move	a1, s1	/* delay slot (real work): a1 = s1 */
	lui	a0, 0xff	/* a0 = 0xff << 16 */
	ori	a0, a0, 0xffff	/* a0 = a0 | 0xffff */
	move	a1, s0	/* a1 = s0 */
	ori	a2, zero, 0x40	/* a2 = 0x40 */
	lw	v0, 0x1c(s1)	/* v0 = *(u32 *)(0x1c + s1) */
	lw	v1, jtbp	/* v1 = *(u32 *)(jtbp) [lui+op macro] */
	or	v0, v0, a0	/* v0 = v0 | a0 */
	sw	v0, 0x1c(s1)	/* *(u32 *)(0x1c + s1) = v0 */
	lw	a0, 0x18(v1)	/* a0 = *(u32 *)(0x18 + v1) */
	lw	v0, 8(v1)	/* v0 = *(u32 *)(8 + v1) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a3, zero	/* delay slot (real work): a3 = zero */
	addiu	a0, s2, 0xe	/* a0 = s2 + 0xe */
	move	a1, s1	/* a1 = s1 */
	jal	memcpy	/* call memcpy */
	ori	a2, zero, 0x5c	/* delay slot (real work): a2 = 0x5c */
	move	v0, s1	/* v0 = s1 */
	lw	ra, 0x1c(sp)	/* ra = *(u32 *)(0x1c + sp) */
	lw	s2, 0x18(sp)	/* s2 = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	PutDrawEnv
/*
 * GetDrawEnv - DRAWENV *GetDrawEnv(DRAWENV *env)
 *
 * Purpose:   memcpy of the DRAWENV copy (0x5c).
 * Inputs:    a0 = destination
 * Outputs:   v0 = env
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	GetDrawEnv
	.ent	GetDrawEnv
GetDrawEnv:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	la	a1, g_75a44+16	/* a1 = &g_75a44+16 [lui+op macro] */
	sw	ra, 0x14(sp)	/* *(u32 *)(0x14 + sp) = ra */
	jal	memcpy	/* call memcpy */
	ori	a2, zero, 0x5c	/* delay slot (real work): a2 = 0x5c */
	move	v0, s0	/* v0 = s0 */
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	GetDrawEnv
/*
 * PutDispEnv - DISPENV *PutDispEnv(DISPENV *env)
 *
 * Purpose:   Emits GP1 05h (display start), 06h/07h (ranges) and 08h (mode) as the env differs from the last copy, then copies env.
 * Inputs:    a0 = environment
 * Outputs:   v0 = env
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	PutDispEnv
	.ent	PutDispEnv
PutDispEnv:
	addiu	sp, sp, -40	/* sp = sp + -40 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	la	v0, g_75a44+2	/* v0 = &g_75a44+2 [lui+op macro] */
	sw	ra, 0x20(sp)	/* *(u32 *)(0x20 + sp) = ra */
	sw	s3, 0x1c(sp)	/* *(u32 *)(0x1c + sp) = s3 */
	sw	s2, 0x18(sp)	/* *(u32 *)(0x18 + sp) = s2 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(0 + v0) */
	nop		/* nothing */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L62f80	/* if (v0 != 0) goto $L62f80 */
	lui	s2, 0x800	/* delay slot (real work): s2 = 0x800 << 16 */
	la	a0, $LC20	/* a0 = &$LC20 [lui+op macro] */
	lw	v0, GPU_printf	/* v0 = *(u32 *)(GPU_printf) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
$L62f80:
	/* -- PutDispEnv: display start word (GP1 05h): 12-bit fields on type 1/2, 10-bit otherwise -- */
	lbu	v0, g_75a44	/* v0 = *(u8 *)(g_75a44) [lui+op macro] */
	nop		/* nothing */
	addiu	v0, v0, -1	/* v0 = v0 + -1 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	beqz	v0, $L62fc0	/* if (v0 == 0) goto $L62fc0 */
	nop		/* delay slot: nothing to donothing */
	jal	get_dx	/* call get_dx */
	move	a0, s0	/* delay slot (real work): a0 = s0 */
	lhu	v1, 2(s0)	/* v1 = *(u16 *)(2 + s0) */
	andi	v0, v0, 0xfff	/* v0 = v0 & 0xfff */
	andi	v1, v1, 0xfff	/* v1 = v1 & 0xfff */
	sll	v1, v1, 12	/* v1 = v1 << 12 */
	or	v1, v1, v0	/* v1 = v1 | v0 */
	j	$L62fdc	/* goto $L62fdc */
	lui	v0, 0x500	/* delay slot (real work): v0 = 0x500 << 16 */
$L62fc0:
	lhu	v0, 2(s0)	/* v0 = *(u16 *)(2 + s0) */
	lhu	v1, 0(s0)	/* v1 = *(u16 *)(0 + s0) */
	andi	v0, v0, 0x3ff	/* v0 = v0 & 0x3ff */
	sll	v0, v0, 10	/* v0 = v0 << 10 */
	andi	v1, v1, 0x3ff	/* v1 = v1 & 0x3ff */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	lui	v1, 0x500	/* v1 = 0x500 << 16 */
$L62fdc:
	or	a0, v0, v1	/* a0 = v0 | v1 */
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0x10(v0)	/* v0 = *(u32 *)(0x10 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	nop		/* delay slot: nothing to donothing */
	/* -- ranges changed? -> recompute horizontal (06h) and vertical (07h) ranges with clamping -- */
	lw	v1, g_75a44+116	/* v1 = *(u32 *)(g_75a44+116) [lui+op macro] */
	lw	v0, 8(s0)	/* v0 = *(u32 *)(8 + s0) */
	nop		/* nothing */
	bne	v1, v0, $L6302c	/* if (v1 != v0) goto $L6302c */
	nop		/* delay slot: nothing to donothing */
	lw	v1, g_75a44+120	/* v1 = *(u32 *)(g_75a44+120) [lui+op macro] */
	lw	v0, 0xc(s0)	/* v0 = *(u32 *)(0xc + s0) */
	nop		/* nothing */
	beq	v1, v0, $L631e8	/* if (v1 == v0) goto $L631e8 */
	nop		/* delay slot: nothing to donothing */
$L6302c:
	jal	GetVideoMode	/* call GetVideoMode */
	nop		/* delay slot: nothing to donothing */
	lh	v1, 8(s0)	/* v1 = *(s16 *)(8 + s0) */
	sb	v0, 0x12(s0)	/* *(u8 *)(0x12 + s0) = v0 */
	sll	v0, v1, 2	/* v0 = v1 << 2 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 1	/* v0 = v0 << 1 */
	addiu	a1, v0, 0x260	/* a1 = v0 + 0x260 */
	lbu	v0, 0x12(s0)	/* v0 = *(u8 *)(0x12 + s0) */
	lh	a0, 0xa(s0)	/* a0 = *(s16 *)(0xa + s0) */
	bnez	v0, $L63060	/* if (v0 != 0) goto $L63060 */
	addiu	s1, a0, 0x13	/* delay slot (real work): s1 = a0 + 0x13 */
	addiu	s1, a0, 0x10	/* s1 = a0 + 0x10 */
$L63060:
	lh	v1, 0xc(s0)	/* v1 = *(s16 *)(0xc + s0) */
	nop		/* nothing */
	beqz	v1, $L63080	/* if (v1 == 0) goto $L63080 */
	sll	v0, v1, 2	/* delay slot (real work): v0 = v1 << 2 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 1	/* v0 = v0 << 1 */
	j	$L63084	/* goto $L63084 */
	addu	a2, a1, v0	/* delay slot (real work): a2 = a1 + v0 */
$L63080:
	addiu	a2, a1, 0xa00	/* a2 = a1 + 0xa00 */
$L63084:
	lh	v0, 0xe(s0)	/* v0 = *(s16 *)(0xe + s0) */
	nop		/* nothing */
	bnez	v0, $L63098	/* if (v0 != 0) goto $L63098 */
	addu	s3, s1, v0	/* delay slot (real work): s3 = s1 + v0 */
	addiu	s3, s1, 0xf0	/* s3 = s1 + 0xf0 */
$L63098:
	slti	v0, a1, 0x1f4	/* v0 = (a1 < 0x1f4) */
	bnez	v0, $L630b8	/* if (v0 != 0) goto $L630b8 */
	ori	a0, zero, 0x1f4	/* delay slot (real work): a0 = 0x1f4 */
	move	a0, a1	/* a0 = a1 */
	slti	v0, a0, 0xcdb	/* v0 = (a0 < 0xcdb) */
	bnez	v0, $L630bc	/* if (v0 != 0) goto $L630bc */
	move	a1, a0	/* delay slot (real work): a1 = a0 */
	ori	a0, zero, 0xcda	/* a0 = 0xcda */
$L630b8:
	move	a1, a0	/* a1 = a0 */
$L630bc:
	addiu	v1, a1, 0x50	/* v1 = a1 + 0x50 */
	slt	v0, a2, v1	/* v0 = (a2 < v1) */
	bnez	v0, $L630d8	/* if (v0 != 0) goto $L630d8 */
	slti	v0, a2, 0xcdb	/* delay slot (real work): v0 = (a2 < 0xcdb) */
	bnez	v0, $L630d8	/* if (v0 != 0) goto $L630d8 */
	move	v1, a2	/* delay slot (real work): v1 = a2 */
	ori	v1, zero, 0xcda	/* v1 = 0xcda */
$L630d8:
	bltz	s1, $L6312c	/* if (s1 < 0) goto $L6312c */
	move	a2, v1	/* delay slot (real work): a2 = v1 */
	lbu	v0, 0x12(s0)	/* v0 = *(u8 *)(0x12 + s0) */
	nop		/* nothing */
	beqz	v0, $L63100	/* if (v0 == 0) goto $L63100 */
	slti	v0, s1, 0x137	/* delay slot (real work): v0 = (s1 < 0x137) */
	beqz	v0, $L6310c	/* if (v0 == 0) goto $L6310c */
	nop		/* delay slot: nothing to donothing */
	j	$L63130	/* goto $L63130 */
	move	a0, s1	/* delay slot (real work): a0 = s1 */
$L63100:
	slti	v0, s1, 0xff	/* v0 = (s1 < 0xff) */
	bnez	v0, $L63124	/* if (v0 != 0) goto $L63124 */
	nop		/* delay slot: nothing to donothing */
$L6310c:
	lbu	v0, 0x12(s0)	/* v0 = *(u8 *)(0x12 + s0) */
	nop		/* nothing */
	beqz	v0, $L63130	/* if (v0 == 0) goto $L63130 */
	ori	a0, zero, 0xfe	/* delay slot (real work): a0 = 0xfe */
	j	$L63130	/* goto $L63130 */
	ori	a0, zero, 0x136	/* delay slot (real work): a0 = 0x136 */
$L63124:
	j	$L63130	/* goto $L63130 */
	move	a0, s1	/* delay slot (real work): a0 = s1 */
$L6312c:
	move	a0, zero	/* a0 = zero */
$L63130:
	move	s1, a0	/* s1 = a0 */
	addiu	v1, s1, 1	/* v1 = s1 + 1 */
	slt	v0, s3, v1	/* v0 = (s3 < v1) */
	bnez	v0, $L6318c	/* if (v0 != 0) goto $L6318c */
	nop		/* delay slot: nothing to donothing */
	lbu	v0, 0x12(s0)	/* v0 = *(u8 *)(0x12 + s0) */
	nop		/* nothing */
	beqz	v0, $L63164	/* if (v0 == 0) goto $L63164 */
	slti	v0, s3, 0x139	/* delay slot (real work): v0 = (s3 < 0x139) */
	beqz	v0, $L63170	/* if (v0 == 0) goto $L63170 */
	nop		/* delay slot: nothing to donothing */
	j	$L6318c	/* goto $L6318c */
	move	v1, s3	/* delay slot (real work): v1 = s3 */
$L63164:
	slti	v0, s3, 0x101	/* v0 = (s3 < 0x101) */
	bnez	v0, $L63188	/* if (v0 != 0) goto $L63188 */
	nop		/* delay slot: nothing to donothing */
$L63170:
	lbu	v0, 0x12(s0)	/* v0 = *(u8 *)(0x12 + s0) */
	nop		/* nothing */
	beqz	v0, $L6318c	/* if (v0 == 0) goto $L6318c */
	ori	v1, zero, 0x100	/* delay slot (real work): v1 = 0x100 */
	j	$L6318c	/* goto $L6318c */
	ori	v1, zero, 0x138	/* delay slot (real work): v1 = 0x138 */
$L63188:
	move	v1, s3	/* v1 = s3 */
$L6318c:
	move	s3, v1	/* s3 = v1 */
	andi	v0, a2, 0xfff	/* v0 = a2 & 0xfff */
	sll	v0, v0, 12	/* v0 = v0 << 12 */
	andi	a0, a1, 0xfff	/* a0 = a1 & 0xfff */
	lui	v1, 0x600	/* v1 = 0x600 << 16 */
	lw	a1, jtbp	/* a1 = *(u32 *)(jtbp) [lui+op macro] */
	or	a0, a0, v1	/* a0 = a0 | v1 */
	lw	v1, 0x10(a1)	/* v1 = *(u32 *)(0x10 + a1) */
	nop		/* nothing */
	jalr	ra, v1	/* call *v1 */
	or	a0, v0, a0	/* delay slot (real work): a0 = v0 | a0 */
	andi	v0, s3, 0x3ff	/* v0 = s3 & 0x3ff */
	sll	v0, v0, 10	/* v0 = v0 << 10 */
	andi	a0, s1, 0x3ff	/* a0 = s1 & 0x3ff */
	lui	v1, 0x700	/* v1 = 0x700 << 16 */
	lw	a1, jtbp	/* a1 = *(u32 *)(jtbp) [lui+op macro] */
	or	a0, a0, v1	/* a0 = a0 | v1 */
	lw	v1, 0x10(a1)	/* v1 = *(u32 *)(0x10 + a1) */
	nop		/* nothing */
	jalr	ra, v1	/* call *v1 */
	or	a0, v0, a0	/* delay slot (real work): a0 = v0 | a0 */
$L631e8:
	/* -- display mode (GP1 08h) rebuilt when size, interlace or mode changed -- */
	lw	v1, g_75a44+124	/* v1 = *(u32 *)(g_75a44+124) [lui+op macro] */
	lw	v0, 0x10(s0)	/* v0 = *(u32 *)(0x10 + s0) */
	nop		/* nothing */
	bne	v1, v0, $L63230	/* if (v1 != v0) goto $L63230 */
	nop		/* delay slot: nothing to donothing */
	lw	v1, g_75a44+108	/* v1 = *(u32 *)(g_75a44+108) [lui+op macro] */
	lw	v0, 0(s0)	/* v0 = *(u32 *)(0 + s0) */
	nop		/* nothing */
	bne	v1, v0, $L63230	/* if (v1 != v0) goto $L63230 */
	nop		/* delay slot: nothing to donothing */
	lw	v1, g_75a44+112	/* v1 = *(u32 *)(g_75a44+112) [lui+op macro] */
	lw	v0, 4(s0)	/* v0 = *(u32 *)(4 + s0) */
	nop		/* nothing */
	beq	v1, v0, $L63318	/* if (v1 == v0) goto $L63318 */
	nop		/* delay slot: nothing to donothing */
$L63230:
	jal	GetVideoMode	/* call GetVideoMode */
	nop		/* delay slot: nothing to donothing */
	sb	v0, 0x12(s0)	/* *(u8 *)(0x12 + s0) = v0 */
	lbu	v1, 0x12(s0)	/* v1 = *(u8 *)(0x12 + s0) */
	ori	v0, zero, 1	/* v0 = 1 */
	bne	v1, v0, $L63250	/* if (v1 != v0) goto $L63250 */
	nop		/* delay slot: nothing to donothing */
	ori	s2, s2, 8	/* s2 = s2 | 8 */
$L63250:
	lbu	v0, 0x11(s0)	/* v0 = *(u8 *)(0x11 + s0) */
	nop		/* nothing */
	beqz	v0, $L63264	/* if (v0 == 0) goto $L63264 */
	nop		/* delay slot: nothing to donothing */
	ori	s2, s2, 0x10	/* s2 = s2 | 0x10 */
$L63264:
	lbu	v0, 0x10(s0)	/* v0 = *(u8 *)(0x10 + s0) */
	nop		/* nothing */
	beqz	v0, $L63278	/* if (v0 == 0) goto $L63278 */
	nop		/* delay slot: nothing to donothing */
	ori	s2, s2, 0x20	/* s2 = s2 | 0x20 */
$L63278:
	la	v0, g_75a44+3	/* v0 = &g_75a44+3 [lui+op macro] */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(0 + v0) */
	nop		/* nothing */
	beqz	v0, $L63294	/* if (v0 == 0) goto $L63294 */
	nop		/* delay slot: nothing to donothing */
	ori	s2, s2, 0x80	/* s2 = s2 | 0x80 */
$L63294:
	lh	v1, 4(s0)	/* v1 = *(s16 *)(4 + s0) */
	nop		/* nothing */
	slti	v0, v1, 0x119	/* v0 = (v1 < 0x119) */
	bnez	v0, $L632dc	/* if (v0 != 0) goto $L632dc */
	slti	v0, v1, 0x161	/* delay slot (real work): v0 = (v1 < 0x161) */
	beqz	v0, $L632b8	/* if (v0 == 0) goto $L632b8 */
	slti	v0, v1, 0x191	/* delay slot (real work): v0 = (v1 < 0x191) */
	j	$L632dc	/* goto $L632dc */
	ori	s2, s2, 1	/* delay slot (real work): s2 = s2 | 1 */
$L632b8:
	beqz	v0, $L632c8	/* if (v0 == 0) goto $L632c8 */
	slti	v0, v1, 0x231	/* delay slot (real work): v0 = (v1 < 0x231) */
	j	$L632dc	/* goto $L632dc */
	ori	s2, s2, 0x40	/* delay slot (real work): s2 = s2 | 0x40 */
$L632c8:
	beqz	v0, $L632d8	/* if (v0 == 0) goto $L632d8 */
	nop		/* delay slot: nothing to donothing */
	j	$L632dc	/* goto $L632dc */
	ori	s2, s2, 2	/* delay slot (real work): s2 = s2 | 2 */
$L632d8:
	ori	s2, s2, 3	/* s2 = s2 | 3 */
$L632dc:
	lbu	v0, 0x12(s0)	/* v0 = *(u8 *)(0x12 + s0) */
	lh	v1, 6(s0)	/* v1 = *(s16 *)(6 + s0) */
	bnez	v0, $L632f0	/* if (v0 != 0) goto $L632f0 */
	slti	v0, v1, 0x121	/* delay slot (real work): v0 = (v1 < 0x121) */
	slti	v0, v1, 0x101	/* v0 = (v1 < 0x101) */
$L632f0:
	bnez	v0, $L632fc	/* if (v0 != 0) goto $L632fc */
	nop		/* delay slot: nothing to donothing */
	ori	s2, s2, 0x24	/* s2 = s2 | 0x24 */
$L632fc:
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0x10(v0)	/* v0 = *(u32 *)(0x10 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	move	a0, s2	/* delay slot (real work): a0 = s2 */
$L63318:
	/* -- remember the DISPENV -- */
	la	a0, g_75a44+108	/* a0 = &g_75a44+108 [lui+op macro] */
	move	a1, s0	/* a1 = s0 */
	jal	memcpy	/* call memcpy */
	ori	a2, zero, 0x14	/* delay slot (real work): a2 = 0x14 */
	move	v0, s0	/* v0 = s0 */
	lw	ra, 0x20(sp)	/* ra = *(u32 *)(0x20 + sp) */
	lw	s3, 0x1c(sp)	/* s3 = *(u32 *)(0x1c + sp) */
	lw	s2, 0x18(sp)	/* s2 = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x28	/* sp = sp + 0x28 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	PutDispEnv
/*
 * GetDispEnv - DISPENV *GetDispEnv(DISPENV *env)
 *
 * Purpose:   memcpy of the DISPENV copy (0x14).
 * Inputs:    a0 = destination
 * Outputs:   v0 = env
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	GetDispEnv
	.ent	GetDispEnv
GetDispEnv:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	la	a1, g_75a44+108	/* a1 = &g_75a44+108 [lui+op macro] */
	sw	ra, 0x14(sp)	/* *(u32 *)(0x14 + sp) = ra */
	jal	memcpy	/* call memcpy */
	ori	a2, zero, 0x14	/* delay slot (real work): a2 = 0x14 */
	move	v0, s0	/* v0 = s0 */
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	GetDispEnv
/*
 * GetODE - long GetODE(void)
 *
 * Purpose:   Reads GPUSTAT through J_STATUS and returns bit 31.
 * Inputs:    none
 * Outputs:   v0 = 0/1 odd-even display line bit
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	GetODE
	.ent	GetODE
GetODE:
	lw	v0, jtbp	/* v0 = *(u32 *)(jtbp) [lui+op macro] */
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	ra, 0x10(sp)	/* *(u32 *)(0x10 + sp) = ra */
	lw	v0, 0x38(v0)	/* v0 = *(u32 *)(0x38 + v0) */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	nop		/* delay slot: nothing to donothing */
	srl	v0, v0, 31	/* v0 = v0 >> 31 (logical) */
	lw	ra, 0x10(sp)	/* ra = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	GetODE
/*
 * SetTexWindow - void SetTexWindow(DR_TWIN *p, RECT *tw)
 *
 * Purpose:   Tag 2, word 1 = get_tw(tw), word 2 = 0.
 * Inputs:    a0 = packet, a1 = window
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetTexWindow
	.ent	SetTexWindow
SetTexWindow:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	ori	v0, zero, 2	/* v0 = 2 */
	move	a0, a1	/* a0 = a1 */
	sw	ra, 0x14(sp)	/* *(u32 *)(0x14 + sp) = ra */
	jal	get_tw	/* call get_tw */
	sb	v0, 3(s0)	/* delay slot (real work): *(u8 *)(3 + s0) = v0 */
	sw	v0, 4(s0)	/* *(u32 *)(4 + s0) = v0 */
	sw	zero, 8(s0)	/* *(u32 *)(8 + s0) = zero */
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetTexWindow
/*
 * SetDrawArea - void SetDrawArea(DR_AREA *p, RECT *r)
 *
 * Purpose:   Tag 2, word 1 = get_cs(x,y), word 2 = get_ce(x+w-1,y+h-1).
 * Inputs:    a0 = packet, a1 = rect
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetDrawArea
	.ent	SetDrawArea
SetDrawArea:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a0	/* s1 = a0 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a1	/* s0 = a1 */
	ori	v0, zero, 2	/* v0 = 2 */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	sb	v0, 3(s1)	/* *(u8 *)(3 + s1) = v0 */
	lh	a0, 0(s0)	/* a0 = *(s16 *)(0 + s0) */
	lh	a1, 2(s0)	/* a1 = *(s16 *)(2 + s0) */
	jal	get_cs	/* call get_cs */
	nop		/* delay slot: nothing to donothing */
	sw	v0, 4(s1)	/* *(u32 *)(4 + s1) = v0 */
	lhu	a0, 0(s0)	/* a0 = *(u16 *)(0 + s0) */
	lhu	v0, 4(s0)	/* v0 = *(u16 *)(4 + s0) */
	lhu	a1, 2(s0)	/* a1 = *(u16 *)(2 + s0) */
	addu	a0, a0, v0	/* a0 = a0 + v0 */
	addiu	a0, a0, -1	/* a0 = a0 + -1 */
	sll	a0, a0, 16	/* a0 = a0 << 16 */
	lhu	v0, 6(s0)	/* v0 = *(u16 *)(6 + s0) */
	sra	a0, a0, 16	/* a0 = a0 >> 16 (arithmetic) */
	addu	a1, a1, v0	/* a1 = a1 + v0 */
	addiu	a1, a1, -1	/* a1 = a1 + -1 */
	sll	a1, a1, 16	/* a1 = a1 << 16 */
	jal	get_ce	/* call get_ce */
	sra	a1, a1, 16	/* delay slot (real work): a1 = a1 >> 16 (arithmetic) */
	sw	v0, 8(s1)	/* *(u32 *)(8 + s1) = v0 */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetDrawArea
/*
 * SetDrawOffset - void SetDrawOffset(DR_OFFSET *p, u_short ofs[2])
 *
 * Purpose:   Tag 2, word 1 = get_ofs(ofs[0], ofs[1]).
 * Inputs:    a0 = packet, a1 = offsets
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetDrawOffset
	.ent	SetDrawOffset
SetDrawOffset:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	ori	v0, zero, 2	/* v0 = 2 */
	sw	ra, 0x14(sp)	/* *(u32 *)(0x14 + sp) = ra */
	sb	v0, 3(s0)	/* *(u8 *)(3 + s0) = v0 */
	lh	a0, 0(a1)	/* a0 = *(s16 *)(0 + a1) */
	lh	a1, 2(a1)	/* a1 = *(s16 *)(2 + a1) */
	jal	get_ofs	/* call get_ofs */
	nop		/* delay slot: nothing to donothing */
	sw	v0, 4(s0)	/* *(u32 *)(4 + s0) = v0 */
	sw	zero, 8(s0)	/* *(u32 *)(8 + s0) = zero */
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetDrawOffset
/*
 * SetPriority - void SetPriority(DR_PRIO *p, long pri, long dither)
 *
 * Purpose:   Tag 2, word 1 = 0xe6000000 | (pri?2:0) | (dither!=0) (mask bits E6h), word 2 = 0.
 * Inputs:    a0 = packet, a1 = pri, a2 = dither
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetPriority
	.ent	SetPriority
SetPriority:
	ori	v0, zero, 2	/* v0 = 2 */
	sb	v0, 3(a0)	/* *(u8 *)(3 + a0) = v0 */
	beqz	a1, $L634d4	/* if (a1 == 0) goto $L634d4 */
	lui	v1, 0xe600	/* delay slot (real work): v1 = 0xe600 << 16 */
	ori	v1, v1, 2	/* v1 = v1 | 2 */
$L634d4:
	sltu	v0, zero, a2	/* v0 = (zero < a2) unsigned */
	or	v0, v1, v0	/* v0 = v1 | v0 */
	sw	v0, 4(a0)	/* *(u32 *)(4 + a0) = v0 */
	jr	ra	/* return */
	sw	zero, 8(a0)	/* delay slot (real work): *(u32 *)(8 + a0) = zero */
	.end	SetPriority
/*
 * SetDrawMode - void SetDrawMode(DR_MODE *p, int dfe, int dtd, int tpage, RECT *tw)
 *
 * Purpose:   Tag 2, word 1 = get_mode(dfe,dtd,tpage), word 2 = get_tw(tw).
 * Inputs:    a0 = packet, a1 = dfe, a2 = dtd, a3 = tpage, 0x10(sp) = tw
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetDrawMode
	.ent	SetDrawMode
SetDrawMode:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	move	a0, a1	/* a0 = a1 */
	ori	v0, zero, 2	/* v0 = 2 */
	move	a1, a2	/* a1 = a2 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	lw	s1, 0x30(sp)	/* s1 = *(u32 *)(0x30 + sp) */
	andi	a2, a3, 0xffff	/* a2 = a3 & 0xffff */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	jal	get_mode	/* call get_mode */
	sb	v0, 3(s0)	/* delay slot (real work): *(u8 *)(3 + s0) = v0 */
	sw	v0, 4(s0)	/* *(u32 *)(4 + s0) = v0 */
	jal	get_tw	/* call get_tw */
	move	a0, s1	/* delay slot (real work): a0 = s1 */
	sw	v0, 8(s0)	/* *(u32 *)(8 + s0) = v0 */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetDrawMode
/*
 * SetDrawEnv - void SetDrawEnv(DR_ENV *p, DRAWENV *env)
 *
 * Purpose:   Emits E3h/E4h area, E5h offset, E1h mode, E2h texture window, E6h mask, optionally a rectangle fill (02h or 60h with clip handling) of env->clip.
 * Inputs:    a0 = packet, a1 = env
 * Outputs:   none (tag size = words emitted - 1)
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.globl	SetDrawEnv
	.ent	SetDrawEnv
SetDrawEnv:
	addiu	sp, sp, -48	/* sp = sp + -48 */
	sw	s0, 0x20(sp)	/* *(u32 *)(0x20 + sp) = s0 */
	move	s0, a1	/* s0 = a1 */
	sw	s1, 0x24(sp)	/* *(u32 *)(0x24 + sp) = s1 */
	move	s1, a0	/* s1 = a0 */
	sw	ra, 0x28(sp)	/* *(u32 *)(0x28 + sp) = ra */
	lh	a0, 0(s0)	/* a0 = *(s16 *)(0 + s0) */
	lh	a1, 2(s0)	/* a1 = *(s16 *)(2 + s0) */
	jal	get_cs	/* call get_cs */
	nop		/* delay slot: nothing to donothing */
	sw	v0, 4(s1)	/* *(u32 *)(4 + s1) = v0 */
	lhu	a0, 4(s0)	/* a0 = *(u16 *)(4 + s0) */
	lhu	v0, 0(s0)	/* v0 = *(u16 *)(0 + s0) */
	lhu	a1, 2(s0)	/* a1 = *(u16 *)(2 + s0) */
	addu	a0, a0, v0	/* a0 = a0 + v0 */
	addiu	a0, a0, -1	/* a0 = a0 + -1 */
	sll	a0, a0, 16	/* a0 = a0 << 16 */
	lhu	v0, 6(s0)	/* v0 = *(u16 *)(6 + s0) */
	sra	a0, a0, 16	/* a0 = a0 >> 16 (arithmetic) */
	addu	a1, a1, v0	/* a1 = a1 + v0 */
	addiu	a1, a1, -1	/* a1 = a1 + -1 */
	/* -- SetDrawEnv: area start E3h, area end E4h, offset E5h, mode E1h, window E2h, mask E6h -- */
	sll	a1, a1, 16	/* a1 = a1 << 16 */
	jal	get_ce	/* call get_ce */
	sra	a1, a1, 16	/* delay slot (real work): a1 = a1 >> 16 (arithmetic) */
	sw	v0, 8(s1)	/* *(u32 *)(8 + s1) = v0 */
	lh	a0, 8(s0)	/* a0 = *(s16 *)(8 + s0) */
	lh	a1, 0xa(s0)	/* a1 = *(s16 *)(0xa + s0) */
	jal	get_ofs	/* call get_ofs */
	nop		/* delay slot: nothing to donothing */
	sw	v0, 0xc(s1)	/* *(u32 *)(0xc + s1) = v0 */
	lbu	a0, 0x17(s0)	/* a0 = *(u8 *)(0x17 + s0) */
	lbu	a1, 0x16(s0)	/* a1 = *(u8 *)(0x16 + s0) */
	lhu	a2, 0x14(s0)	/* a2 = *(u16 *)(0x14 + s0) */
	jal	get_mode	/* call get_mode */
	nop		/* delay slot: nothing to donothing */
	addiu	a0, s0, 0xc	/* a0 = s0 + 0xc */
	jal	get_tw	/* call get_tw */
	sw	v0, 0x10(s1)	/* delay slot (real work): *(u32 *)(0x10 + s1) = v0 */
	sw	v0, 0x14(s1)	/* *(u32 *)(0x14 + s1) = v0 */
	lui	v0, 0xe600	/* v0 = 0xe600 << 16 */
	sw	v0, 0x18(s1)	/* *(u32 *)(0x18 + s1) = v0 */
	lbu	v0, 0x18(s0)	/* v0 = *(u8 *)(0x18 + s0) */
	nop		/* nothing */
	/* -- optional rectangle clear of env->clip: clamp x,y,w,h to the VRAM limits -- */
	beqz	v0, $L637cc	/* if (v0 == 0) goto $L637cc */
	ori	t0, zero, 7	/* delay slot (real work): t0 = 7 */
	lhu	v0, 0(s0)	/* v0 = *(u16 *)(0 + s0) */
	nop		/* nothing */
	sh	v0, 0x10(sp)	/* *(u16 *)(0x10 + sp) = v0 */
	lhu	v0, 2(s0)	/* v0 = *(u16 *)(2 + s0) */
	nop		/* nothing */
	sh	v0, 0x12(sp)	/* *(u16 *)(0x12 + sp) = v0 */
	lhu	v1, 4(s0)	/* v1 = *(u16 *)(4 + s0) */
	nop		/* nothing */
	sh	v1, 0x14(sp)	/* *(u16 *)(0x14 + sp) = v1 */
	lhu	v0, 6(s0)	/* v0 = *(u16 *)(6 + s0) */
	nop		/* nothing */
	sh	v0, 0x16(sp)	/* *(u16 *)(0x16 + sp) = v0 */
	sll	v0, v1, 16	/* v0 = v1 << 16 */
	sra	a0, v0, 16	/* a0 = v0 >> 16 (arithmetic) */
	bltz	a0, $L63664	/* if (a0 < 0) goto $L63664 */
	nop		/* delay slot: nothing to donothing */
	la	a1, g_75a44+4	/* a1 = &g_75a44+4 [lui+op macro] */
	lhu	v0, 0(a1)	/* v0 = *(u16 *)(0 + a1) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, a0	/* v0 = (v0 < a0) */
	beqz	v0, $L63668	/* if (v0 == 0) goto $L63668 */
	nop		/* delay slot: nothing to donothing */
	lhu	v1, 0(a1)	/* v1 = *(u16 *)(0 + a1) */
	j	$L63668	/* goto $L63668 */
	nop		/* delay slot: nothing to donothing */
$L63664:
	move	v1, zero	/* v1 = zero */
$L63668:
	lh	a0, 0x16(sp)	/* a0 = *(s16 *)(0x16 + sp) */
	sh	v1, 0x14(sp)	/* *(u16 *)(0x14 + sp) = v1 */
	bltz	a0, $L636a8	/* if (a0 < 0) goto $L636a8 */
	move	v1, a0	/* delay slot (real work): v1 = a0 */
	la	a1, g_75a44+6	/* a1 = &g_75a44+6 [lui+op macro] */
	lhu	v0, 0(a1)	/* v0 = *(u16 *)(0 + a1) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, a0	/* v0 = (v0 < a0) */
	beqz	v0, $L636ac	/* if (v0 == 0) goto $L636ac */
	nop		/* delay slot: nothing to donothing */
	lhu	v1, 0(a1)	/* v1 = *(u16 *)(0 + a1) */
	j	$L636ac	/* goto $L636ac */
	nop		/* delay slot: nothing to donothing */
$L636a8:
	move	v1, zero	/* v1 = zero */
$L636ac:
	lhu	a0, 0x10(sp)	/* a0 = *(u16 *)(0x10 + sp) */
	nop		/* nothing */
	andi	v0, a0, 0x3f	/* v0 = a0 & 0x3f */
	bnez	v0, $L636d4	/* if (v0 != 0) goto $L636d4 */
	sh	v1, 0x16(sp)	/* delay slot (real work): *(u16 *)(0x16 + sp) = v1 */
	lhu	v0, 0x14(sp)	/* v0 = *(u16 *)(0x14 + sp) */
	nop		/* nothing */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
	beqz	v0, $L63774	/* if (v0 == 0) goto $L63774 */
	sll	a1, t0, 2	/* delay slot (real work): a1 = t0 << 2 */
$L636d4:
	/* -- clip not 64-aligned: clear in two fills (60h) with the offset applied -- */
	sll	a2, t0, 2	/* a2 = t0 << 2 */
	addiu	t0, t0, 1	/* t0 = t0 + 1 */
	sll	a1, t0, 2	/* a1 = t0 << 2 */
	addiu	t0, t0, 1	/* t0 = t0 + 1 */
	lhu	v0, 8(s0)	/* v0 = *(u16 *)(8 + s0) */
	addu	a2, a2, s1	/* a2 = a2 + s1 */
	subu	v0, a0, v0	/* v0 = a0 - v0 */
	sh	v0, 0x10(sp)	/* *(u16 *)(0x10 + sp) = v0 */
	lhu	v0, 0x12(sp)	/* v0 = *(u16 *)(0x12 + sp) */
	lhu	v1, 0xa(s0)	/* v1 = *(u16 *)(0xa + s0) */
	lui	a0, 0x6000	/* a0 = 0x6000 << 16 */
	subu	v0, v0, v1	/* v0 = v0 - v1 */
	sh	v0, 0x12(sp)	/* *(u16 *)(0x12 + sp) = v0 */
	lbu	v0, 0x1b(s0)	/* v0 = *(u8 *)(0x1b + s0) */
	lbu	v1, 0x1a(s0)	/* v1 = *(u8 *)(0x1a + s0) */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sll	v1, v1, 8	/* v1 = v1 << 8 */
	or	v1, v1, a0	/* v1 = v1 | a0 */
	lbu	a0, 0x19(s0)	/* a0 = *(u8 *)(0x19 + s0) */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	or	v0, v0, a0	/* v0 = v0 | a0 */
	sw	v0, 0(a2)	/* *(u32 *)(0 + a2) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(0x10 + sp) */
	addu	a1, a1, s1	/* a1 = a1 + s1 */
	sw	v0, 0(a1)	/* *(u32 *)(0 + a1) = v0 */
	sll	v0, t0, 2	/* v0 = t0 << 2 */
	lw	v1, 0x14(sp)	/* v1 = *(u32 *)(0x14 + sp) */
	addu	v0, v0, s1	/* v0 = v0 + s1 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	lhu	v0, 0x10(sp)	/* v0 = *(u16 *)(0x10 + sp) */
	lhu	v1, 8(s0)	/* v1 = *(u16 *)(8 + s0) */
	nop		/* nothing */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sh	v0, 0x10(sp)	/* *(u16 *)(0x10 + sp) = v0 */
	lhu	v0, 0x12(sp)	/* v0 = *(u16 *)(0x12 + sp) */
	lhu	v1, 0xa(s0)	/* v1 = *(u16 *)(0xa + s0) */
	addiu	t0, t0, 1	/* t0 = t0 + 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	j	$L637cc	/* goto $L637cc */
	sh	v0, 0x12(sp)	/* delay slot (real work): *(u16 *)(0x12 + sp) = v0 */
$L63774:
	/* -- aligned: single fill (02h) -- */
	addiu	t0, t0, 1	/* t0 = t0 + 1 */
	sll	a2, t0, 2	/* a2 = t0 << 2 */
	addiu	t0, t0, 1	/* t0 = t0 + 1 */
	sll	a3, t0, 2	/* a3 = t0 << 2 */
	addiu	t0, t0, 1	/* t0 = t0 + 1 */
	addu	a1, a1, s1	/* a1 = a1 + s1 */
	lui	a0, 0x200	/* a0 = 0x200 << 16 */
	lbu	v0, 0x1b(s0)	/* v0 = *(u8 *)(0x1b + s0) */
	lbu	v1, 0x1a(s0)	/* v1 = *(u8 *)(0x1a + s0) */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sll	v1, v1, 8	/* v1 = v1 << 8 */
	or	v1, v1, a0	/* v1 = v1 | a0 */
	lbu	a0, 0x19(s0)	/* a0 = *(u8 *)(0x19 + s0) */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	or	v0, v0, a0	/* v0 = v0 | a0 */
	sw	v0, 0(a1)	/* *(u32 *)(0 + a1) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(0x10 + sp) */
	addu	a2, a2, s1	/* a2 = a2 + s1 */
	sw	v0, 0(a2)	/* *(u32 *)(0 + a2) = v0 */
	lw	v0, 0x14(sp)	/* v0 = *(u32 *)(0x14 + sp) */
	addu	a3, a3, s1	/* a3 = a3 + s1 */
	sw	v0, 0(a3)	/* *(u32 *)(0 + a3) = v0 */
$L637cc:
	addiu	v0, t0, -1	/* v0 = t0 + -1 */
	sb	v0, 3(s1)	/* *(u8 *)(3 + s1) = v0 */
	lw	ra, 0x28(sp)	/* ra = *(u32 *)(0x28 + sp) */
	lw	s1, 0x24(sp)	/* s1 = *(u32 *)(0x24 + sp) */
	lw	s0, 0x20(sp)	/* s0 = *(u32 *)(0x20 + sp) */
	addiu	sp, sp, 0x30	/* sp = sp + 0x30 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	SetDrawEnv
/*
 * get_mode - static u_long get_mode(int dfe, int dtd, int tpage)
 *
 * Purpose:   Type 1/2 hardware use the 0x27ff/0x1000 layout, others 0x9ff/0x400.
 * Inputs:    a0 = dfe, a1 = dtd, a2 = tpage
 * Outputs:   v0 = GP0 E1h draw-mode word
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	get_mode
get_mode:
	la	v0, g_75a44	/* v0 = &g_75a44 [lui+op macro] */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(0 + v0) */
	nop		/* nothing */
	addiu	v0, v0, -1	/* v0 = v0 + -1 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	beqz	v0, $L63828	/* if (v0 == 0) goto $L63828 */
	nop		/* delay slot: nothing to donothing */
	beqz	a1, $L63818	/* if (a1 == 0) goto $L63818 */
	lui	v1, 0xe100	/* delay slot (real work): v1 = 0xe100 << 16 */
	ori	v1, v1, 0x800	/* v1 = v1 | 0x800 */
$L63818:
	beqz	a0, $L63840	/* if (a0 == 0) goto $L63840 */
	andi	v0, a2, 0x27ff	/* delay slot (real work): v0 = a2 & 0x27ff */
	j	$L63840	/* goto $L63840 */
	ori	v0, v0, 0x1000	/* delay slot (real work): v0 = v0 | 0x1000 */
$L63828:
	beqz	a1, $L63834	/* if (a1 == 0) goto $L63834 */
	lui	v1, 0xe100	/* delay slot (real work): v1 = 0xe100 << 16 */
	ori	v1, v1, 0x200	/* v1 = v1 | 0x200 */
$L63834:
	beqz	a0, $L63840	/* if (a0 == 0) goto $L63840 */
	andi	v0, a2, 0x9ff	/* delay slot (real work): v0 = a2 & 0x9ff */
	ori	v0, v0, 0x400	/* v0 = v0 | 0x400 */
$L63840:
	jr	ra	/* return */
	or	v0, v1, v0	/* delay slot (real work): v0 = v1 | v0 */
	.end	get_mode
/*
 * get_cs - static u_long get_cs(int x, int y)
 *
 * Purpose:   Clamps x,y to 0..limit and packs by type (10/12-bit fields).
 * Inputs:    a0 = x, a1 = y
 * Outputs:   v0 = GP0 E3h drawing-area top-left
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	get_cs
get_cs:
	move	a2, a0	/* a2 = a0 */
	sll	a0, a0, 16	/* a0 = a0 << 16 */
	sra	v1, a0, 16	/* v1 = a0 >> 16 (arithmetic) */
	bltz	v1, $L6388c	/* if (v1 < 0) goto $L6388c */
	nop		/* delay slot: nothing to donothing */
	la	a3, g_75a44+4	/* a3 = &g_75a44+4 [lui+op macro] */
	lhu	v0, 0(a3)	/* v0 = *(u16 *)(0 + a3) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, v1	/* v0 = (v0 < v1) */
	beqz	v0, $L63890	/* if (v0 == 0) goto $L63890 */
	move	a0, a2	/* delay slot (real work): a0 = a2 */
	lhu	a0, 0(a3)	/* a0 = *(u16 *)(0 + a3) */
	j	$L63894	/* goto $L63894 */
	sll	v0, a1, 16	/* delay slot (real work): v0 = a1 << 16 */
$L6388c:
	move	a0, zero	/* a0 = zero */
$L63890:
	sll	v0, a1, 16	/* v0 = a1 << 16 */
$L63894:
	sra	v1, v0, 16	/* v1 = v0 >> 16 (arithmetic) */
	bltz	v1, $L638d0	/* if (v1 < 0) goto $L638d0 */
	move	a2, a0	/* delay slot (real work): a2 = a0 */
	la	a0, g_75a44+6	/* a0 = &g_75a44+6 [lui+op macro] */
	lhu	v0, 0(a0)	/* v0 = *(u16 *)(0 + a0) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, v1	/* v0 = (v0 < v1) */
	beqz	v0, $L638d4	/* if (v0 == 0) goto $L638d4 */
	nop		/* delay slot: nothing to donothing */
	lhu	a1, 0(a0)	/* a1 = *(u16 *)(0 + a0) */
	j	$L638d4	/* goto $L638d4 */
	nop		/* delay slot: nothing to donothing */
$L638d0:
	move	a1, zero	/* a1 = zero */
$L638d4:
	la	v0, g_75a44	/* v0 = &g_75a44 [lui+op macro] */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(0 + v0) */
	nop		/* nothing */
	addiu	v0, v0, -1	/* v0 = v0 + -1 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L63904	/* if (v0 != 0) goto $L63904 */
	andi	v1, a1, 0xfff	/* delay slot (real work): v1 = a1 & 0xfff */
	andi	v1, a1, 0x3ff	/* v1 = a1 & 0x3ff */
	sll	v1, v1, 10	/* v1 = v1 << 10 */
	j	$L6390c	/* goto $L6390c */
	andi	v0, a2, 0x3ff	/* delay slot (real work): v0 = a2 & 0x3ff */
$L63904:
	sll	v1, v1, 12	/* v1 = v1 << 12 */
	andi	v0, a2, 0xfff	/* v0 = a2 & 0xfff */
$L6390c:
	lui	a0, 0xe300	/* a0 = 0xe300 << 16 */
	or	v0, v0, a0	/* v0 = v0 | a0 */
	jr	ra	/* return */
	or	v0, v1, v0	/* delay slot (real work): v0 = v1 | v0 */
	.end	get_cs
/*
 * get_ce - static u_long get_ce(int x, int y)
 *
 * Purpose:   Same as get_cs with opcode E4h.
 * Inputs:    a0 = x, a1 = y
 * Outputs:   v0 = GP0 E4h drawing-area bottom-right
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	get_ce
get_ce:
	move	a2, a0	/* a2 = a0 */
	sll	a0, a0, 16	/* a0 = a0 << 16 */
	sra	v1, a0, 16	/* v1 = a0 >> 16 (arithmetic) */
	bltz	v1, $L63960	/* if (v1 < 0) goto $L63960 */
	nop		/* delay slot: nothing to donothing */
	la	a3, g_75a44+4	/* a3 = &g_75a44+4 [lui+op macro] */
	lhu	v0, 0(a3)	/* v0 = *(u16 *)(0 + a3) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, v1	/* v0 = (v0 < v1) */
	beqz	v0, $L63964	/* if (v0 == 0) goto $L63964 */
	move	a0, a2	/* delay slot (real work): a0 = a2 */
	lhu	a0, 0(a3)	/* a0 = *(u16 *)(0 + a3) */
	j	$L63968	/* goto $L63968 */
	sll	v0, a1, 16	/* delay slot (real work): v0 = a1 << 16 */
$L63960:
	move	a0, zero	/* a0 = zero */
$L63964:
	sll	v0, a1, 16	/* v0 = a1 << 16 */
$L63968:
	sra	v1, v0, 16	/* v1 = v0 >> 16 (arithmetic) */
	bltz	v1, $L639a4	/* if (v1 < 0) goto $L639a4 */
	move	a2, a0	/* delay slot (real work): a2 = a0 */
	la	a0, g_75a44+6	/* a0 = &g_75a44+6 [lui+op macro] */
	lhu	v0, 0(a0)	/* v0 = *(u16 *)(0 + a0) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, v1	/* v0 = (v0 < v1) */
	beqz	v0, $L639a8	/* if (v0 == 0) goto $L639a8 */
	nop		/* delay slot: nothing to donothing */
	lhu	a1, 0(a0)	/* a1 = *(u16 *)(0 + a0) */
	j	$L639a8	/* goto $L639a8 */
	nop		/* delay slot: nothing to donothing */
$L639a4:
	move	a1, zero	/* a1 = zero */
$L639a8:
	la	v0, g_75a44	/* v0 = &g_75a44 [lui+op macro] */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(0 + v0) */
	nop		/* nothing */
	addiu	v0, v0, -1	/* v0 = v0 + -1 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L639d8	/* if (v0 != 0) goto $L639d8 */
	andi	v1, a1, 0xfff	/* delay slot (real work): v1 = a1 & 0xfff */
	andi	v1, a1, 0x3ff	/* v1 = a1 & 0x3ff */
	sll	v1, v1, 10	/* v1 = v1 << 10 */
	j	$L639e0	/* goto $L639e0 */
	andi	v0, a2, 0x3ff	/* delay slot (real work): v0 = a2 & 0x3ff */
$L639d8:
	sll	v1, v1, 12	/* v1 = v1 << 12 */
	andi	v0, a2, 0xfff	/* v0 = a2 & 0xfff */
$L639e0:
	lui	a0, 0xe400	/* a0 = 0xe400 << 16 */
	or	v0, v0, a0	/* v0 = v0 | a0 */
	jr	ra	/* return */
	or	v0, v1, v0	/* delay slot (real work): v0 = v1 | v0 */
	.end	get_ce
/*
 * get_ofs - static u_long get_ofs(int x, int y)
 *
 * Purpose:   11-bit or 12-bit fields by type.
 * Inputs:    a0 = x, a1 = y
 * Outputs:   v0 = GP0 E5h drawing offset
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	get_ofs
get_ofs:
	la	v0, g_75a44	/* v0 = &g_75a44 [lui+op macro] */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(0 + v0) */
	nop		/* nothing */
	addiu	v0, v0, -1	/* v0 = v0 + -1 */
	sltiu	v0, v0, 2	/* v0 = (v0 < 2) unsigned */
	bnez	v0, $L63a20	/* if (v0 != 0) goto $L63a20 */
	andi	v1, a1, 0xfff	/* delay slot (real work): v1 = a1 & 0xfff */
	andi	v1, a1, 0x7ff	/* v1 = a1 & 0x7ff */
	sll	v1, v1, 11	/* v1 = v1 << 11 */
	j	$L63a28	/* goto $L63a28 */
	andi	v0, a0, 0x7ff	/* delay slot (real work): v0 = a0 & 0x7ff */
$L63a20:
	sll	v1, v1, 12	/* v1 = v1 << 12 */
	andi	v0, a0, 0xfff	/* v0 = a0 & 0xfff */
$L63a28:
	lui	a0, 0xe500	/* a0 = 0xe500 << 16 */
	or	v0, v0, a0	/* v0 = v0 | a0 */
	jr	ra	/* return */
	or	v0, v1, v0	/* delay slot (real work): v0 = v1 | v0 */
	.end	get_ofs
/*
 * get_tw - static u_long get_tw(RECT *tw)
 *
 * Purpose:   Converts the window to 8-pixel units; negates the offsets mod 256.
 * Inputs:    a0 = window or 0
 * Outputs:   v0 = GP0 E2h texture window (0 for NULL)
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	get_tw
get_tw:
	bnez	a0, $L63a48	/* if (a0 != 0) goto $L63a48 */
	addiu	sp, sp, -16	/* delay slot (real work): sp = sp + -16 */
	j	$L63ab0	/* goto $L63ab0 */
	move	v0, zero	/* delay slot (real work): v0 = zero */
$L63a48:
	lbu	a1, 0(a0)	/* a1 = *(u8 *)(0 + a0) */
	nop		/* nothing */
	srl	a1, a1, 3	/* a1 = a1 >> 3 (logical) */
	sw	a1, 0(sp)	/* *(u32 *)(0 + sp) = a1 */
	lh	a2, 4(a0)	/* a2 = *(s16 *)(4 + a0) */
	nop		/* nothing */
	subu	a2, zero, a2	/* a2 = zero - a2 */
	andi	a2, a2, 0xff	/* a2 = a2 & 0xff */
	sra	a2, a2, 3	/* a2 = a2 >> 3 (arithmetic) */
	sw	a2, 8(sp)	/* *(u32 *)(8 + sp) = a2 */
	lbu	v0, 2(a0)	/* v0 = *(u8 *)(2 + a0) */
	sll	a1, a1, 10	/* a1 = a1 << 10 */
	srl	v0, v0, 3	/* v0 = v0 >> 3 (logical) */
	sw	v0, 4(sp)	/* *(u32 *)(4 + sp) = v0 */
	sll	v0, v0, 15	/* v0 = v0 << 15 */
	lh	v1, 6(a0)	/* v1 = *(s16 *)(6 + a0) */
	lui	a0, 0xe200	/* a0 = 0xe200 << 16 */
	or	a1, a1, a0	/* a1 = a1 | a0 */
	or	v0, v0, a1	/* v0 = v0 | a1 */
	subu	v1, zero, v1	/* v1 = zero - v1 */
	andi	v1, v1, 0xff	/* v1 = v1 & 0xff */
	sra	v1, v1, 3	/* v1 = v1 >> 3 (arithmetic) */
	sll	a0, v1, 5	/* a0 = v1 << 5 */
	or	v0, v0, a0	/* v0 = v0 | a0 */
	or	v0, v0, a2	/* v0 = v0 | a2 */
	sw	v1, 0xc(sp)	/* *(u32 *)(0xc + sp) = v1 */
$L63ab0:
	addiu	sp, sp, 0x10	/* sp = sp + 0x10 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	get_tw
/*
 * get_dx - static int get_dx(RECT *disp)
 *
 * Purpose:   Reverse flag aware: type 1 mirrors against 0x400, type 2 halves.
 * Inputs:    a0 = pointer to the display RECT
 * Outputs:   v0 = horizontal display start
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	get_dx
get_dx:
	la	v0, g_75a44	/* v0 = &g_75a44 [lui+op macro] */
	lbu	v0, 0(v0)	/* v0 = *(u8 *)(0 + v0) */
	nop		/* nothing */
	andi	v1, v0, 0xff	/* v1 = v0 & 0xff */
	ori	v0, zero, 1	/* v0 = 1 */
	beq	v1, v0, $L63aec	/* if (v1 == v0) goto $L63aec */
	ori	v0, zero, 2	/* delay slot (real work): v0 = 2 */
	beq	v1, v0, $L63b14	/* if (v1 == v0) goto $L63b14 */
	nop		/* delay slot: nothing to donothing */
	j	$L63b6c	/* goto $L63b6c */
	nop		/* delay slot: nothing to donothing */
$L63aec:
	lbu	v0, g_75a44+3	/* v0 = *(u8 *)(g_75a44+3) [lui+op macro] */
	nop		/* nothing */
	beqz	v0, $L63b6c	/* if (v0 == 0) goto $L63b6c */
	ori	v0, zero, 0x400	/* delay slot (real work): v0 = 0x400 */
	lh	v1, 4(a0)	/* v1 = *(s16 *)(4 + a0) */
	lh	a0, 0(a0)	/* a0 = *(s16 *)(0 + a0) */
$L63b08:
	subu	v0, v0, v1	/* v0 = v0 - v1 */
	j	$L63b70	/* goto $L63b70 */
	subu	v0, v0, a0	/* delay slot (real work): v0 = v0 - a0 */
$L63b14:
	lbu	v0, g_75a44+3	/* v0 = *(u8 *)(g_75a44+3) [lui+op macro] */
	nop		/* nothing */
	beqz	v0, $L63b4c	/* if (v0 == 0) goto $L63b4c */
	nop		/* delay slot: nothing to donothing */
	lhu	v0, 4(a0)	/* v0 = *(u16 *)(4 + a0) */
	lh	a0, 0(a0)	/* a0 = *(s16 *)(0 + a0) */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v1, v0, 16	/* v1 = v0 >> 16 (arithmetic) */
	srl	v0, v0, 31	/* v0 = v0 >> 31 (logical) */
	addu	v1, v1, v0	/* v1 = v1 + v0 */
	sra	v1, v1, 1	/* v1 = v1 >> 1 (arithmetic) */
	j	$L63b08	/* goto $L63b08 */
	ori	v0, zero, 0x400	/* delay slot (real work): v0 = 0x400 */
$L63b4c:
	lhu	v0, 0(a0)	/* v0 = *(u16 *)(0 + a0) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v1, v0, 16	/* v1 = v0 >> 16 (arithmetic) */
	srl	v0, v0, 31	/* v0 = v0 >> 31 (logical) */
	addu	v1, v1, v0	/* v1 = v1 + v0 */
	j	$L63b70	/* goto $L63b70 */
	sra	v0, v1, 1	/* delay slot (real work): v0 = v1 >> 1 (arithmetic) */
$L63b6c:
	lh	v0, 0(a0)	/* v0 = *(s16 *)(0 + a0) */
$L63b70:
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	get_dx
/*
 * _status - static u_long _status(void)
 *
 * Purpose:   Reads *gp1.
 * Inputs:    none
 * Outputs:   v0 = GPUSTAT
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_status
_status:
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_status
/*
 * _otc - static int _otc(u_long *ot, long n)
 *
 * Purpose:   DMA6 (OTC) reverse clear: enables the channel in DPCR, points MADR at the last entry, BCR = n, CHCR = 0x11000002, waits for completion with the alarm.
 * Inputs:    a0 = table, a1 = length
 * Outputs:   v0 = n or -1 on timeout
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_otc
_otc:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a1	/* s0 = a1 */
	lw	a1, d_pcr	/* a1 = *(u32 *)(d_pcr) [lui+op macro] */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	lw	v0, 0(a1)	/* v0 = *(u32 *)(0 + a1) */
	lui	v1, 0x800	/* v1 = 0x800 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a1)	/* *(u32 *)(0 + a1) = v0 */
	lw	v0, d6_chcr	/* v0 = *(u32 *)(d6_chcr) [lui+op macro] */
	nop		/* nothing */
	sw	zero, 0(v0)	/* *(u32 *)(0 + v0) = zero */
	sll	v0, s0, 2	/* v0 = s0 << 2 */
	addiu	v0, v0, -4	/* v0 = v0 + -4 */
	lw	v1, d6_madr	/* v1 = *(u32 *)(d6_madr) [lui+op macro] */
	addu	a0, a0, v0	/* a0 = a0 + v0 */
	sw	a0, 0(v1)	/* *(u32 *)(0 + v1) = a0 */
	lw	v0, d6_bcr	/* v0 = *(u32 *)(d6_bcr) [lui+op macro] */
	lui	v1, 0x1100	/* v1 = 0x1100 << 16 */
	sw	s0, 0(v0)	/* *(u32 *)(0 + v0) = s0 */
	lw	v0, d6_chcr	/* v0 = *(u32 *)(d6_chcr) [lui+op macro] */
	ori	v1, v1, 2	/* v1 = v1 | 2 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	jal	set_alarm	/* call set_alarm */
	nop		/* delay slot: nothing to donothing */
	lw	v0, d6_chcr	/* v0 = *(u32 *)(d6_chcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x100	/* v1 = 0x100 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	beqz	v0, $L63c60	/* if (v0 == 0) goto $L63c60 */
	move	v0, s0	/* delay slot (real work): v0 = s0 */
	lui	s1, 0x100	/* s1 = 0x100 << 16 */
$L63c30:
	jal	get_alarm	/* call get_alarm */
	nop		/* delay slot: nothing to donothing */
	bnez	v0, $L63c60	/* if (v0 != 0) goto $L63c60 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
	lw	v0, d6_chcr	/* v0 = *(u32 *)(d6_chcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	nop		/* nothing */
	and	v0, v0, s1	/* v0 = v0 & s1 */
	bnez	v0, $L63c30	/* if (v0 != 0) goto $L63c30 */
	move	v0, s0	/* delay slot (real work): v0 = s0 */
$L63c60:
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_otc
/*
 * _clr - static int _clr(RECT *r, u_long cmd)
 *
 * Purpose:   Clamps the rect to the VRAM limits; for unaligned/odd x,w builds a 3 GP0 command list in the bss packet and runs _cwc, otherwise the plain GP0 02h fill.
 * Inputs:    a0 = rect, a1 = 0x02rrggbb fill word
 * Outputs:   v0 = 0
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_clr
_clr:
	addiu	sp, sp, -48	/* sp = sp + -48 */
	move	t0, a0	/* t0 = a0 */
	sw	ra, 0x28(sp)	/* *(u32 *)(0x28 + sp) = ra */
	sw	s1, 0x24(sp)	/* *(u32 *)(0x24 + sp) = s1 */
	sw	s0, 0x20(sp)	/* *(u32 *)(0x20 + sp) = s0 */
	lh	a0, 4(t0)	/* a0 = *(s16 *)(4 + t0) */
	move	t1, a1	/* t1 = a1 */
	bltz	a0, $L63ccc	/* if (a0 < 0) goto $L63ccc */
	move	v1, a0	/* delay slot (real work): v1 = a0 */
	la	a1, g_75a44+4	/* a1 = &g_75a44+4 [lui+op macro] */
	lhu	v0, 0(a1)	/* v0 = *(u16 *)(0 + a1) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, a0	/* v0 = (v0 < a0) */
	beqz	v0, $L63cd0	/* if (v0 == 0) goto $L63cd0 */
	nop		/* delay slot: nothing to donothing */
	lhu	v1, 0(a1)	/* v1 = *(u16 *)(0 + a1) */
	j	$L63cd0	/* goto $L63cd0 */
	nop		/* delay slot: nothing to donothing */
$L63ccc:
	move	v1, zero	/* v1 = zero */
$L63cd0:
	lh	a0, 6(t0)	/* a0 = *(s16 *)(6 + t0) */
	sh	v1, 4(t0)	/* *(u16 *)(4 + t0) = v1 */
	bltz	a0, $L63d10	/* if (a0 < 0) goto $L63d10 */
	move	v1, a0	/* delay slot (real work): v1 = a0 */
	la	a1, g_75a44+6	/* a1 = &g_75a44+6 [lui+op macro] */
	lhu	v0, 0(a1)	/* v0 = *(u16 *)(0 + a1) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, a0	/* v0 = (v0 < a0) */
	beqz	v0, $L63d14	/* if (v0 == 0) goto $L63d14 */
	nop		/* delay slot: nothing to donothing */
	lhu	v1, 0(a1)	/* v1 = *(u16 *)(0 + a1) */
	j	$L63d14	/* goto $L63d14 */
	nop		/* delay slot: nothing to donothing */
$L63d10:
	move	v1, zero	/* v1 = zero */
$L63d14:
	lhu	v0, 0(t0)	/* v0 = *(u16 *)(0 + t0) */
	nop		/* nothing */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
	bnez	v0, $L63d3c	/* if (v0 != 0) goto $L63d3c */
	sh	v1, 6(t0)	/* delay slot (real work): *(u16 *)(6 + t0) = v1 */
	lhu	v0, 4(t0)	/* v0 = *(u16 *)(4 + t0) */
	nop		/* nothing */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
	beqz	v0, $L63e14	/* if (v0 == 0) goto $L63e14 */
	lui	v0, 0x4ff	/* delay slot (real work): v0 = 0x4ff << 16 */
$L63d3c:
	/* -- _clr: unaligned x/w -> build a 6-word GP0 list in the bss packet and send it with _cwc -- */
	lui	a0, 0xff	/* a0 = 0xff << 16 */
	ori	a0, a0, 0xffff	/* a0 = a0 | 0xffff */
	lui	a1, 0xe4ff	/* a1 = 0xe4ff << 16 */
	ori	a1, a1, 0xffff	/* a1 = a1 | 0xffff */
	lui	a3, 0x3ff	/* a3 = 0x3ff << 16 */
	la	a2, g_91af4+32	/* a2 = &g_91af4+32 [lui+op macro] */
	and	v0, a2, a0	/* v0 = a2 & a0 */
	lui	v1, 0x700	/* v1 = 0x700 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	lui	s0, 0xe300	/* s0 = 0xe300 << 16 */
	lui	s1, 0xe500	/* s1 = 0xe500 << 16 */
	sw	v0, g_91af4	/* *(u32 *)(g_91af4) = v0 [lui+op macro, via $1] */
	lui	v0, 0xe600	/* v0 = 0xe600 << 16 */
	and	a0, t1, a0	/* a0 = t1 & a0 */
	sw	v0, g_91af4+16	/* *(u32 *)(g_91af4+16) = v0 [lui+op macro, via $1] */
	lui	v0, 0x6000	/* v0 = 0x6000 << 16 */
	or	a0, a0, v0	/* a0 = a0 | v0 */
	sw	s0, g_91af4+4	/* *(u32 *)(g_91af4+4) = s0 [lui+op macro, via $1] */
	sw	a1, g_91af4+8	/* *(u32 *)(g_91af4+8) = a1 [lui+op macro, via $1] */
	sw	s1, g_91af4+12	/* *(u32 *)(g_91af4+12) = s1 [lui+op macro, via $1] */
	sw	a0, g_91af4+20	/* *(u32 *)(g_91af4+20) = a0 [lui+op macro, via $1] */
	lw	v0, 0(t0)	/* v0 = *(u32 *)(0 + t0) */
	ori	a3, a3, 0xffff	/* a3 = a3 | 0xffff */
	sw	v0, g_91af4+24	/* *(u32 *)(g_91af4+24) = v0 [lui+op macro, via $1] */
	lw	v0, 4(t0)	/* v0 = *(u32 *)(4 + t0) */
	sw	a3, 0(a2)	/* *(u32 *)(0 + a2) = a3 */
	sw	v0, g_91af4+28	/* *(u32 *)(g_91af4+28) = v0 [lui+op macro, via $1] */
	jal	_param	/* call _param */
	ori	a0, zero, 3	/* delay slot (real work): a0 = 3 */
	or	v0, v0, s0	/* v0 = v0 | s0 */
	sw	v0, g_91af4+36	/* *(u32 *)(g_91af4+36) = v0 [lui+op macro, via $1] */
	jal	_param	/* call _param */
	ori	a0, zero, 4	/* delay slot (real work): a0 = 4 */
	lui	v1, 0xe400	/* v1 = 0xe400 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, g_91af4+40	/* *(u32 *)(g_91af4+40) = v0 [lui+op macro, via $1] */
	jal	_param	/* call _param */
	ori	a0, zero, 5	/* delay slot (real work): a0 = 5 */
	or	v0, v0, s1	/* v0 = v0 | s1 */
	sw	v0, g_91af4+44	/* *(u32 *)(g_91af4+44) = v0 [lui+op macro, via $1] */
	j	$L63e68	/* goto $L63e68 */
	nop		/* delay slot: nothing to donothing */
$L63e14:
	/* -- _clr: aligned -> plain 02h fill list -- */
	ori	v0, v0, 0xffff	/* v0 = v0 | 0xffff */
	lui	v1, 0xff	/* v1 = 0xff << 16 */
	ori	v1, v1, 0xffff	/* v1 = v1 | 0xffff */
	sw	v0, g_91af4	/* *(u32 *)(g_91af4) = v0 [lui+op macro, via $1] */
	lui	v0, 0xe600	/* v0 = 0xe600 << 16 */
	and	v1, t1, v1	/* v1 = t1 & v1 */
	sw	v0, g_91af4+4	/* *(u32 *)(g_91af4+4) = v0 [lui+op macro, via $1] */
	lui	v0, 0x200	/* v0 = 0x200 << 16 */
	or	v1, v1, v0	/* v1 = v1 | v0 */
	sw	v1, g_91af4+8	/* *(u32 *)(g_91af4+8) = v1 [lui+op macro, via $1] */
	lw	v0, 0(t0)	/* v0 = *(u32 *)(0 + t0) */
	nop		/* nothing */
	sw	v0, g_91af4+12	/* *(u32 *)(g_91af4+12) = v0 [lui+op macro, via $1] */
	lw	v0, 4(t0)	/* v0 = *(u32 *)(4 + t0) */
	nop		/* nothing */
	sw	v0, g_91af4+16	/* *(u32 *)(g_91af4+16) = v0 [lui+op macro, via $1] */
$L63e68:
	la	a0, g_91af4	/* a0 = &g_91af4 [lui+op macro] */
	jal	_cwc	/* call _cwc */
	nop		/* delay slot: nothing to donothing */
	move	v0, zero	/* v0 = zero */
	lw	ra, 0x28(sp)	/* ra = *(u32 *)(0x28 + sp) */
	lw	s1, 0x24(sp)	/* s1 = *(u32 *)(0x24 + sp) */
	lw	s0, 0x20(sp)	/* s0 = *(u32 *)(0x20 + sp) */
	addiu	sp, sp, 0x30	/* sp = sp + 0x30 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_clr
/*
 * _dws - static int _dws(RECT *r, u_long *p)
 *
 * Purpose:   CPU to VRAM: GP0 A0h, rect, data words by PIO for the first part (n&15) and DMA2 blocks for the rest.
 * Inputs:    a0 = rect, a1 = data
 * Outputs:   v0 = 0 or -1
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_dws
_dws:
	addiu	sp, sp, -64	/* sp = sp + -64 */
	sw	s1, 0x24(sp)	/* *(u32 *)(0x24 + sp) = s1 */
	move	s1, a0	/* s1 = a0 */
	sw	s2, 0x28(sp)	/* *(u32 *)(0x28 + sp) = s2 */
	move	s2, a1	/* s2 = a1 */
	sw	ra, 0x38(sp)	/* *(u32 *)(0x38 + sp) = ra */
	sw	s5, 0x34(sp)	/* *(u32 *)(0x34 + sp) = s5 */
	sw	s4, 0x30(sp)	/* *(u32 *)(0x30 + sp) = s4 */
	sw	s3, 0x2c(sp)	/* *(u32 *)(0x2c + sp) = s3 */
	jal	set_alarm	/* call set_alarm */
	sw	s0, 0x20(sp)	/* delay slot (real work): *(u32 *)(0x20 + sp) = s0 */
	lh	a0, 4(s1)	/* a0 = *(s16 *)(4 + s1) */
	move	s5, zero	/* s5 = zero */
	bltz	a0, $L63f00	/* if (a0 < 0) goto $L63f00 */
	move	v1, a0	/* delay slot (real work): v1 = a0 */
	la	a1, g_75a44+4	/* a1 = &g_75a44+4 [lui+op macro] */
	lhu	v0, 0(a1)	/* v0 = *(u16 *)(0 + a1) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, a0	/* v0 = (v0 < a0) */
	beqz	v0, $L63f04	/* if (v0 == 0) goto $L63f04 */
	nop		/* delay slot: nothing to donothing */
	lhu	v1, 0(a1)	/* v1 = *(u16 *)(0 + a1) */
	j	$L63f04	/* goto $L63f04 */
	nop		/* delay slot: nothing to donothing */
$L63f00:
	move	v1, zero	/* v1 = zero */
$L63f04:
	lh	a1, 6(s1)	/* a1 = *(s16 *)(6 + s1) */
	sh	v1, 4(s1)	/* *(u16 *)(4 + s1) = v1 */
	bltz	a1, $L63f44	/* if (a1 < 0) goto $L63f44 */
	move	v1, a1	/* delay slot (real work): v1 = a1 */
	la	a2, g_75a44+6	/* a2 = &g_75a44+6 [lui+op macro] */
	lhu	v0, 0(a2)	/* v0 = *(u16 *)(0 + a2) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, a1	/* v0 = (v0 < a1) */
	beqz	v0, $L63f48	/* if (v0 == 0) goto $L63f48 */
	move	a0, v1	/* delay slot (real work): a0 = v1 */
	lhu	a0, 0(a2)	/* a0 = *(u16 *)(0 + a2) */
	j	$L63f4c	/* goto $L63f4c */
	sll	v0, a0, 16	/* delay slot (real work): v0 = a0 << 16 */
$L63f44:
	move	a0, zero	/* a0 = zero */
$L63f48:
	sll	v0, a0, 16	/* v0 = a0 << 16 */
$L63f4c:
	lh	v1, 4(s1)	/* v1 = *(s16 *)(4 + s1) */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	mult	v1, v0	/* LO = v1 * v0 */
	sh	a0, 6(s1)	/* *(u16 *)(6 + s1) = a0 */
	mflo	v0	/* v0 = LO */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	srl	v1, v0, 31	/* v1 = v0 >> 31 (logical) */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sra	a0, v0, 1	/* a0 = v0 >> 1 (arithmetic) */
	bgtz	a0, $L63f80	/* if (a0 > 0) goto $L63f80 */
	sra	s0, v0, 5	/* delay slot (real work): s0 = v0 >> 5 (arithmetic) */
	j	$L640c0	/* goto $L640c0 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
$L63f80:
	/* -- _dws: wait for GPU ready (GPUSTAT bit 26), then GP0 A0h, position, size, data -- */
	move	v1, s0	/* v1 = s0 */
	sll	v0, v1, 4	/* v0 = v1 << 4 */
	subu	s0, a0, v0	/* s0 = a0 - v0 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	move	s4, v1	/* s4 = v1 */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x400	/* v1 = 0x400 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L63fe0	/* if (v0 != 0) goto $L63fe0 */
	lui	a0, 0xa000	/* delay slot (real work): a0 = 0xa000 << 16 */
	lui	s3, 0x400	/* s3 = 0x400 << 16 */
$L63fb0:
	jal	get_alarm	/* call get_alarm */
	nop		/* delay slot: nothing to donothing */
	bnez	v0, $L640c0	/* if (v0 != 0) goto $L640c0 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	nop		/* nothing */
	and	v0, v0, s3	/* v0 = v0 & s3 */
	beqz	v0, $L63fb0	/* if (v0 == 0) goto $L63fb0 */
	lui	a0, 0xa000	/* delay slot (real work): a0 = 0xa000 << 16 */
$L63fe0:
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	lui	v0, 0x400	/* v0 = 0x400 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp0	/* v1 = *(u32 *)(gp0) [lui+op macro] */
	lui	v0, 0x100	/* v0 = 0x100 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v0, gp0	/* v0 = *(u32 *)(gp0) [lui+op macro] */
	beqz	s5, $L64014	/* if (s5 == 0) goto $L64014 */
	nop		/* delay slot: nothing to donothing */
	lui	a0, 0xb000	/* a0 = 0xb000 << 16 */
$L64014:
	sw	a0, 0(v0)	/* *(u32 *)(0 + v0) = a0 */
	lw	v1, gp0	/* v1 = *(u32 *)(gp0) [lui+op macro] */
	lw	v0, 0(s1)	/* v0 = *(u32 *)(0 + s1) */
	nop		/* nothing */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp0	/* v1 = *(u32 *)(gp0) [lui+op macro] */
	lw	v0, 4(s1)	/* v0 = *(u32 *)(4 + s1) */
	addiu	s0, s0, -1	/* s0 = s0 + -1 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	addiu	v0, zero, -1	/* v0 = -1 */
	beq	s0, v0, $L64070	/* if (s0 == v0) goto $L64070 */
	nop		/* delay slot: nothing to donothing */
	addiu	a0, zero, -1	/* a0 = -1 */
$L64050:
	/* -- _dws: PIO the n & 15 leftover words -- */
	lw	v1, 0(s2)	/* v1 = *(u32 *)(0 + s2) */
	addiu	s2, s2, 4	/* s2 = s2 + 4 */
	lw	v0, gp0	/* v0 = *(u32 *)(gp0) [lui+op macro] */
	addiu	s0, s0, -1	/* s0 = s0 + -1 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	bne	s0, a0, $L64050	/* if (s0 != a0) goto $L64050 */
	nop		/* delay slot: nothing to donothing */
$L64070:
	/* -- _dws: remaining 16-word blocks by DMA2 (CHCR 0x01000201) -- */
	beqz	s4, $L640bc	/* if (s4 == 0) goto $L640bc */
	lui	v1, 0x400	/* delay slot (real work): v1 = 0x400 << 16 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	ori	v1, v1, 2	/* v1 = v1 | 2 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	lw	v0, d2_madr	/* v0 = *(u32 *)(d2_madr) [lui+op macro] */
	lui	a0, 0x100	/* a0 = 0x100 << 16 */
	sw	s2, 0(v0)	/* *(u32 *)(0 + v0) = s2 */
	sll	v0, s4, 16	/* v0 = s4 << 16 */
	lw	v1, d2_bcr	/* v1 = *(u32 *)(d2_bcr) [lui+op macro] */
	ori	v0, v0, 0x10	/* v0 = v0 | 0x10 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	ori	a0, a0, 0x201	/* a0 = a0 | 0x201 */
	sw	a0, 0(v0)	/* *(u32 *)(0 + v0) = a0 */
$L640bc:
	move	v0, zero	/* v0 = zero */
$L640c0:
	lw	ra, 0x38(sp)	/* ra = *(u32 *)(0x38 + sp) */
	lw	s5, 0x34(sp)	/* s5 = *(u32 *)(0x34 + sp) */
	lw	s4, 0x30(sp)	/* s4 = *(u32 *)(0x30 + sp) */
	lw	s3, 0x2c(sp)	/* s3 = *(u32 *)(0x2c + sp) */
	lw	s2, 0x28(sp)	/* s2 = *(u32 *)(0x28 + sp) */
	lw	s1, 0x24(sp)	/* s1 = *(u32 *)(0x24 + sp) */
	lw	s0, 0x20(sp)	/* s0 = *(u32 *)(0x20 + sp) */
	addiu	sp, sp, 0x40	/* sp = sp + 0x40 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_dws
/*
 * _drs - static int _drs(RECT *r, u_long *p)
 *
 * Purpose:   VRAM to CPU: GP0 C0h, rect, reads GPUREAD by PIO then DMA2 blocks.
 * Inputs:    a0 = rect, a1 = destination
 * Outputs:   v0 = 0 or -1
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_drs
_drs:
	addiu	sp, sp, -56	/* sp = sp + -56 */
	sw	s1, 0x24(sp)	/* *(u32 *)(0x24 + sp) = s1 */
	move	s1, a0	/* s1 = a0 */
	sw	s2, 0x28(sp)	/* *(u32 *)(0x28 + sp) = s2 */
	move	s2, a1	/* s2 = a1 */
	sw	ra, 0x34(sp)	/* *(u32 *)(0x34 + sp) = ra */
	sw	s4, 0x30(sp)	/* *(u32 *)(0x30 + sp) = s4 */
	sw	s3, 0x2c(sp)	/* *(u32 *)(0x2c + sp) = s3 */
	jal	set_alarm	/* call set_alarm */
	sw	s0, 0x20(sp)	/* delay slot (real work): *(u32 *)(0x20 + sp) = s0 */
	lh	a0, 4(s1)	/* a0 = *(s16 *)(4 + s1) */
	nop		/* nothing */
	bltz	a0, $L64150	/* if (a0 < 0) goto $L64150 */
	move	v1, a0	/* delay slot (real work): v1 = a0 */
	la	a1, g_75a44+4	/* a1 = &g_75a44+4 [lui+op macro] */
	lhu	v0, 0(a1)	/* v0 = *(u16 *)(0 + a1) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, a0	/* v0 = (v0 < a0) */
	beqz	v0, $L64154	/* if (v0 == 0) goto $L64154 */
	nop		/* delay slot: nothing to donothing */
	lhu	v1, 0(a1)	/* v1 = *(u16 *)(0 + a1) */
	j	$L64154	/* goto $L64154 */
	nop		/* delay slot: nothing to donothing */
$L64150:
	move	v1, zero	/* v1 = zero */
$L64154:
	lh	a1, 6(s1)	/* a1 = *(s16 *)(6 + s1) */
	sh	v1, 4(s1)	/* *(u16 *)(4 + s1) = v1 */
	bltz	a1, $L64194	/* if (a1 < 0) goto $L64194 */
	move	v1, a1	/* delay slot (real work): v1 = a1 */
	la	a2, g_75a44+6	/* a2 = &g_75a44+6 [lui+op macro] */
	lhu	v0, 0(a2)	/* v0 = *(u16 *)(0 + a2) */
	nop		/* nothing */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	slt	v0, v0, a1	/* v0 = (v0 < a1) */
	beqz	v0, $L64198	/* if (v0 == 0) goto $L64198 */
	move	a0, v1	/* delay slot (real work): a0 = v1 */
	lhu	a0, 0(a2)	/* a0 = *(u16 *)(0 + a2) */
	j	$L6419c	/* goto $L6419c */
	sll	v0, a0, 16	/* delay slot (real work): v0 = a0 << 16 */
$L64194:
	move	a0, zero	/* a0 = zero */
$L64198:
	sll	v0, a0, 16	/* v0 = a0 << 16 */
$L6419c:
	lh	v1, 4(s1)	/* v1 = *(s16 *)(4 + s1) */
	sra	v0, v0, 16	/* v0 = v0 >> 16 (arithmetic) */
	mult	v1, v0	/* LO = v1 * v0 */
	sh	a0, 6(s1)	/* *(u16 *)(6 + s1) = a0 */
	mflo	v0	/* v0 = LO */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	srl	v1, v0, 31	/* v1 = v0 >> 31 (logical) */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sra	a0, v0, 1	/* a0 = v0 >> 1 (arithmetic) */
	bgtz	a0, $L641d0	/* if (a0 > 0) goto $L641d0 */
	sra	s0, v0, 5	/* delay slot (real work): s0 = v0 >> 5 (arithmetic) */
	j	$L64360	/* goto $L64360 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
$L641d0:
	/* -- _drs: GP0 C0h read setup, wait for data ready, PIO read, DMA2 for blocks -- */
	move	v1, s0	/* v1 = s0 */
	sll	v0, v1, 4	/* v0 = v1 << 4 */
	subu	s0, a0, v0	/* s0 = a0 - v0 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	move	s4, v1	/* s4 = v1 */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x400	/* v1 = 0x400 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L64230	/* if (v0 != 0) goto $L64230 */
	nop		/* delay slot: nothing to donothing */
	lui	s3, 0x400	/* s3 = 0x400 << 16 */
$L64200:
	jal	get_alarm	/* call get_alarm */
	nop		/* delay slot: nothing to donothing */
	bnez	v0, $L64360	/* if (v0 != 0) goto $L64360 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	nop		/* nothing */
	and	v0, v0, s3	/* v0 = v0 & s3 */
	beqz	v0, $L64200	/* if (v0 == 0) goto $L64200 */
	nop		/* delay slot: nothing to donothing */
$L64230:
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	lui	v0, 0x400	/* v0 = 0x400 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp0	/* v1 = *(u32 *)(gp0) [lui+op macro] */
	lui	v0, 0x100	/* v0 = 0x100 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp0	/* v1 = *(u32 *)(gp0) [lui+op macro] */
	lui	v0, 0xc000	/* v0 = 0xc000 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp0	/* v1 = *(u32 *)(gp0) [lui+op macro] */
	lw	v0, 0(s1)	/* v0 = *(u32 *)(0 + s1) */
	nop		/* nothing */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp0	/* v1 = *(u32 *)(gp0) [lui+op macro] */
	lw	v0, 4(s1)	/* v0 = *(u32 *)(4 + s1) */
	nop		/* nothing */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x800	/* v1 = 0x800 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L642dc	/* if (v0 != 0) goto $L642dc */
	nop		/* delay slot: nothing to donothing */
	lui	s1, 0x800	/* s1 = 0x800 << 16 */
$L642ac:
	jal	get_alarm	/* call get_alarm */
	nop		/* delay slot: nothing to donothing */
	bnez	v0, $L64360	/* if (v0 != 0) goto $L64360 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	nop		/* nothing */
	and	v0, v0, s1	/* v0 = v0 & s1 */
	beqz	v0, $L642ac	/* if (v0 == 0) goto $L642ac */
	nop		/* delay slot: nothing to donothing */
$L642dc:
	addiu	s0, s0, -1	/* s0 = s0 + -1 */
	addiu	v0, zero, -1	/* v0 = -1 */
	beq	s0, v0, $L64310	/* if (s0 == v0) goto $L64310 */
	nop		/* delay slot: nothing to donothing */
	addiu	v1, zero, -1	/* v1 = -1 */
$L642f0:
	lw	v0, gp0	/* v0 = *(u32 *)(gp0) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	addiu	s0, s0, -1	/* s0 = s0 + -1 */
	sw	v0, 0(s2)	/* *(u32 *)(0 + s2) = v0 */
	bne	s0, v1, $L642f0	/* if (s0 != v1) goto $L642f0 */
	addiu	s2, s2, 4	/* delay slot (real work): s2 = s2 + 4 */
$L64310:
	beqz	s4, $L6435c	/* if (s4 == 0) goto $L6435c */
	lui	v1, 0x400	/* delay slot (real work): v1 = 0x400 << 16 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	ori	v1, v1, 3	/* v1 = v1 | 3 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	lw	v0, d2_madr	/* v0 = *(u32 *)(d2_madr) [lui+op macro] */
	lui	a0, 0x100	/* a0 = 0x100 << 16 */
	sw	s2, 0(v0)	/* *(u32 *)(0 + v0) = s2 */
	sll	v0, s4, 16	/* v0 = s4 << 16 */
	lw	v1, d2_bcr	/* v1 = *(u32 *)(d2_bcr) [lui+op macro] */
	ori	v0, v0, 0x10	/* v0 = v0 | 0x10 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	ori	a0, a0, 0x200	/* a0 = a0 | 0x200 */
	sw	a0, 0(v0)	/* *(u32 *)(0 + v0) = a0 */
$L6435c:
	move	v0, zero	/* v0 = zero */
$L64360:
	lw	ra, 0x34(sp)	/* ra = *(u32 *)(0x34 + sp) */
	lw	s4, 0x30(sp)	/* s4 = *(u32 *)(0x30 + sp) */
	lw	s3, 0x2c(sp)	/* s3 = *(u32 *)(0x2c + sp) */
	lw	s2, 0x28(sp)	/* s2 = *(u32 *)(0x28 + sp) */
	lw	s1, 0x24(sp)	/* s1 = *(u32 *)(0x24 + sp) */
	lw	s0, 0x20(sp)	/* s0 = *(u32 *)(0x20 + sp) */
	addiu	sp, sp, 0x38	/* sp = sp + 0x38 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_drs
/*
 * _ctl - static void _ctl(u_long w)
 *
 * Purpose:   Writes *gp1 and shadows the argument in ctlbuf[w >> 24].
 * Inputs:    a0 = GP1 control word
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_ctl
_ctl:
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	sw	a0, 0(v0)	/* *(u32 *)(0 + v0) = a0 */
	srl	v0, a0, 24	/* v0 = a0 >> 24 (logical) */
	.set	noat
	la	$1, ctlbuf	/* $1 = &ctlbuf [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	sb	a0, 0($1)	/* *(u8 *)(0 + $1) = a0 */
	.set	at
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_ctl
/*
 * _getctl - static u_char _getctl(int n)
 *
 * Purpose:   Reads ctlbuf[n].
 * Inputs:    a0 = command number
 * Outputs:   v0 = shadowed byte
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_getctl
_getctl:
	.set	noat
	la	$1, ctlbuf	/* $1 = &ctlbuf [lui+op macro] */
	addu	$1, $1, a0	/* $1 = $1 + a0 */
	lbu	v0, 0($1)	/* v0 = *(u8 *)(0 + $1) */
	.set	at
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_getctl
/*
 * _cwb - static int _cwb(u_long *p, long n)
 *
 * Purpose:   Sets GP1 04h (DMA off) and writes n words to GP0.
 * Inputs:    a0 = words, a1 = count
 * Outputs:   v0 = 0
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_cwb
_cwb:
	addiu	sp, sp, -8	/* sp = sp + -8 */
	addiu	a2, a1, -1	/* a2 = a1 + -1 */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	lui	v0, 0x400	/* v0 = 0x400 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	beqz	a1, $L6440c	/* if (a1 == 0) goto $L6440c */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	addiu	a1, zero, -1	/* a1 = -1 */
$L643ec:
	lw	v1, 0(a0)	/* v1 = *(u32 *)(0 + a0) */
	addiu	a0, a0, 4	/* a0 = a0 + 4 */
	lw	v0, gp0	/* v0 = *(u32 *)(gp0) [lui+op macro] */
	addiu	a2, a2, -1	/* a2 = a2 + -1 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	bne	a2, a1, $L643ec	/* if (a2 != a1) goto $L643ec */
	move	v0, zero	/* delay slot (real work): v0 = zero */
$L6440c:
	addiu	sp, sp, 8	/* sp = sp + 8 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_cwb
/*
 * _cwc - static void _cwc(u_long *list)
 *
 * Purpose:   Starts DMA2 in linked-list mode (CHCR 0x01000401) on list.
 * Inputs:    a0 = linked list
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_cwc
_cwc:
	lui	v1, 0x400	/* v1 = 0x400 << 16 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	ori	v1, v1, 2	/* v1 = v1 | 2 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	lw	v0, d2_madr	/* v0 = *(u32 *)(d2_madr) [lui+op macro] */
	nop		/* nothing */
	sw	a0, 0(v0)	/* *(u32 *)(0 + v0) = a0 */
	lw	v0, d2_bcr	/* v0 = *(u32 *)(d2_bcr) [lui+op macro] */
	lui	v1, 0x100	/* v1 = 0x100 << 16 */
	sw	zero, 0(v0)	/* *(u32 *)(0 + v0) = zero */
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	ori	v1, v1, 0x401	/* v1 = v1 | 0x401 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_cwc
/*
 * _param - static u_long _param(u_long n)
 *
 * Purpose:   GP1 10h (get GPU info) then reads the result.
 * Inputs:    a0 = GP1 10h parameter
 * Outputs:   v0 = GPUREAD & 0xffffff
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_param
_param:
	lui	v0, 0x1000	/* v0 = 0x1000 << 16 */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	or	a0, a0, v0	/* a0 = a0 | v0 */
	sw	a0, 0(v1)	/* *(u32 *)(0 + v1) = a0 */
	lw	v0, gp0	/* v0 = *(u32 *)(gp0) [lui+op macro] */
	lui	v1, 0xff	/* v1 = 0xff << 16 */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	ori	v1, v1, 0xffff	/* v1 = v1 | 0xffff */
	jr	ra	/* return */
	and	v0, v0, v1	/* delay slot (real work): v0 = v0 & v1 */
	.end	_param
/*
 * _addque - static int _addque(fn, a0, a1)
 *
 * Purpose:   Calls _addque2(fn, arg0, 0, arg1).
 * Inputs:    a0 = fn, a1 = arg0, a2 = arg1
 * Outputs:   v0 = queue result
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_addque
_addque:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	ra, 0x10(sp)	/* *(u32 *)(0x10 + sp) = ra */
	move	a3, a2	/* a3 = a2 */
	jal	_addque2	/* call _addque2 */
	move	a2, zero	/* delay slot (real work): a2 = zero */
	lw	ra, 0x10(sp)	/* ra = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_addque
/*
 * _addque2 - static int _addque2(fn, a0, nbytes, a1)
 *
 * Purpose:   Queues (fn,a0,a1) in _que (96-byte entries, 64 slots) or, when idle and not in queued mode, runs fn directly; with a data block the words are copied into the entry.
 * Inputs:    a0 = fn, a1 = arg0, a2 = nbytes (0 or copy size), a3 = arg1
 * Outputs:   v0 = free queue slots used, -1 on timeout
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_addque2
_addque2:
	addiu	sp, sp, -40	/* sp = sp + -40 */
	sw	s3, 0x1c(sp)	/* *(u32 *)(0x1c + sp) = s3 */
	move	s3, a0	/* s3 = a0 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a1	/* s0 = a1 */
	sw	s1, 0x14(sp)	/* *(u32 *)(0x14 + sp) = s1 */
	move	s1, a2	/* s1 = a2 */
	sw	s2, 0x18(sp)	/* *(u32 *)(0x18 + sp) = s2 */
	sw	ra, 0x20(sp)	/* *(u32 *)(0x20 + sp) = ra */
	jal	set_alarm	/* call set_alarm */
	move	s2, a3	/* delay slot (real work): s2 = a3 */
	j	$L64504	/* goto $L64504 */
	nop		/* delay slot: nothing to donothing */
$L644ec:
	jal	get_alarm	/* call get_alarm */
	nop		/* delay slot: nothing to donothing */
	bnez	v0, $L64794	/* if (v0 != 0) goto $L64794 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
	jal	_exeque	/* call _exeque */
	nop		/* delay slot: nothing to donothing */
$L64504:
	/* -- _addque2: queue full? wait via get_alarm and run _exeque; else mask interrupts -- */
	lw	v0, _qin	/* v0 = *(u32 *)(_qin) [lui+op macro] */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
	beq	v0, v1, $L644ec	/* if (v0 == v1) goto $L644ec */
	nop		/* delay slot: nothing to donothing */
	jal	SetIntrMask	/* call SetIntrMask */
	move	a0, zero	/* delay slot (real work): a0 = zero */
	la	v1, g_75a44+8	/* v1 = &g_75a44+8 [lui+op macro] */
	ori	a0, zero, 1	/* a0 = 1 */
	sw	a0, 0(v1)	/* *(u32 *)(0 + v1) = a0 */
	lbu	v1, g_75a44+1	/* v1 = *(u8 *)(g_75a44+1) [lui+op macro] */
	sw	v0, g_75b50	/* *(u32 *)(g_75b50) = v0 [lui+op macro, via $1] */
	beqz	v1, $L645a4	/* if (v1 == 0) goto $L645a4 */
	lui	a0, 0x1000	/* delay slot (real work): a0 = 0x1000 << 16 */
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	lw	v0, _qout	/* v0 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	bne	v1, v0, $L645f4	/* if (v1 != v0) goto $L645f4 */
	nop		/* delay slot: nothing to donothing */
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x100	/* v1 = 0x100 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L645f4	/* if (v0 != 0) goto $L645f4 */
	nop		/* delay slot: nothing to donothing */
	lw	v0, g_75a44+12	/* v0 = *(u32 *)(g_75a44+12) [lui+op macro] */
	nop		/* nothing */
	bnez	v0, $L645f4	/* if (v0 != 0) goto $L645f4 */
	nop		/* delay slot: nothing to donothing */
$L645a4:
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
$L645ac:
	nop		/* nothing */
	lw	v0, 0(v1)	/* v0 = *(u32 *)(0 + v1) */
	nop		/* nothing */
	and	v0, v0, a0	/* v0 = v0 & a0 */
	beqz	v0, $L645ac	/* if (v0 == 0) goto $L645ac */
	nop		/* delay slot: nothing to donothing */
	move	a0, s0	/* a0 = s0 */
	jalr	ra, s3	/* call *s3 */
	move	a1, s2	/* delay slot (real work): a1 = s2 */
	la	v0, _qlog	/* v0 = &_qlog [lui+op macro] */
	sw	s3, 0(v0)	/* *(u32 *)(0 + v0) = s3 */
	sw	s0, _qlog+4	/* *(u32 *)(_qlog+4) = s0 [lui+op macro, via $1] */
	sw	s2, _qlog+8	/* *(u32 *)(_qlog+8) = s2 [lui+op macro, via $1] */
	j	$L64780	/* goto $L64780 */
	nop		/* delay slot: nothing to donothing */
$L645f4:
	/* -- _addque2: hook the DMA-end callback (_exeque) and copy the block into the queue entry -- */
	la	a1, _exeque	/* a1 = &_exeque [lui+op macro] */
	jal	DMACallback	/* call DMACallback */
	ori	a0, zero, 2	/* delay slot (real work): a0 = 2 */
	beqz	s1, $L646b8	/* if (s1 == 0) goto $L646b8 */
	move	a2, zero	/* delay slot (real work): a2 = zero */
	la	t0, _que+12	/* t0 = &_que+12 [lui+op macro] */
	move	a3, s0	/* a3 = s0 */
	move	v0, s1	/* v0 = s1 */
$L6461c:
	bgez	v0, $L64628	/* if (v0 >= 0) goto $L64628 */
	nop		/* delay slot: nothing to donothing */
	addiu	v0, v0, 3	/* v0 = v0 + 3 */
$L64628:
	sra	v0, v0, 2	/* v0 = v0 >> 2 (arithmetic) */
	slt	v0, a2, v0	/* v0 = (a2 < v0) */
	beqz	v0, $L6466c	/* if (v0 == 0) goto $L6466c */
	sll	a0, a2, 2	/* delay slot (real work): a0 = a2 << 2 */
	lw	a1, 0(a3)	/* a1 = *(u32 *)(0 + a3) */
	addiu	a3, a3, 4	/* a3 = a3 + 4 */
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	addiu	a2, a2, 1	/* a2 = a2 + 1 */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	addu	v0, v0, t0	/* v0 = v0 + t0 */
	addu	a0, a0, v0	/* a0 = a0 + v0 */
	sw	a1, 0(a0)	/* *(u32 *)(0 + a0) = a1 */
	j	$L6461c	/* goto $L6461c */
	move	v0, s1	/* delay slot (real work): v0 = s1 */
$L6466c:
	lw	v0, _qin	/* v0 = *(u32 *)(_qin) [lui+op macro] */
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	sll	a0, v0, 1	/* a0 = v0 << 1 */
	addu	a0, a0, v0	/* a0 = a0 + v0 */
	sll	a0, a0, 5	/* a0 = a0 << 5 */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	la	v1, _que+12	/* v1 = &_que+12 [lui+op macro] */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	.set	noat
	la	$1, _que+4	/* $1 = &_que+4 [lui+op macro] */
	addu	$1, $1, a0	/* $1 = $1 + a0 */
	sw	v0, 0($1)	/* *(u32 *)(0 + $1) = v0 */
	.set	at
	j	$L646e0	/* goto $L646e0 */
	nop		/* delay slot: nothing to donothing */
$L646b8:
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	nop		/* nothing */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	.set	noat
	la	$1, _que+4	/* $1 = &_que+4 [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	sw	s0, 0($1)	/* *(u32 *)(0 + $1) = s0 */
$L646e0:
	/* -- _addque2: store fn,a0,a1 and advance _qin -- */
	.set	at
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	nop		/* nothing */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	.set	noat
	la	$1, _que+8	/* $1 = &_que+8 [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	sw	s2, 0($1)	/* *(u32 *)(0 + $1) = s2 */
	.set	at
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	nop		/* nothing */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	.set	noat
	la	$1, _que	/* $1 = &_que [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	sw	s3, 0($1)	/* *(u32 *)(0 + $1) = s3 */
	.set	at
	lw	v0, _qin	/* v0 = *(u32 *)(_qin) [lui+op macro] */
	lw	a0, g_75b50	/* a0 = *(u32 *)(g_75b50) [lui+op macro] */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
	sw	v0, _qin	/* *(u32 *)(_qin) = v0 [lui+op macro, via $1] */
	jal	SetIntrMask	/* call SetIntrMask */
	nop		/* delay slot: nothing to donothing */
	jal	_exeque	/* call _exeque */
	nop		/* delay slot: nothing to donothing */
	lw	v0, _qin	/* v0 = *(u32 *)(_qin) [lui+op macro] */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	subu	v0, v0, v1	/* v0 = v0 - v1 */
	j	$L64794	/* goto $L64794 */
	andi	v0, v0, 0x3f	/* delay slot (real work): v0 = v0 & 0x3f */
$L64780:
	/* -- _addque2: direct-run path restores the interrupt mask -- */
	lui	a0, 0x8007	/* a0 = 0x8007 << 16 */
	lw	a0, 0x5b50(a0)	/* a0 = *(u32 *)(0x5b50 + a0) */
	jal	SetIntrMask	/* call SetIntrMask */
	nop		/* delay slot: nothing to donothing */
	move	v0, zero	/* v0 = zero */
$L64794:
	lw	ra, 0x20(sp)	/* ra = *(u32 *)(0x20 + sp) */
	lw	s3, 0x1c(sp)	/* s3 = *(u32 *)(0x1c + sp) */
	lw	s2, 0x18(sp)	/* s2 = *(u32 *)(0x18 + sp) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x28	/* sp = sp + 0x28 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_addque2
/*
 * _exeque - static int _exeque(void)
 *
 * Purpose:   Runs queued entries while the GPU is idle (DMA2 CHCR busy bit 0x01000000 clear, GPUSTAT bit 0x10000000), logs them in _qlog, fires the DrawSync callback when the queue empties.
 * Inputs:    none
 * Outputs:   v0 = queue length
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_exeque
_exeque:
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	ra, 0x14(sp)	/* *(u32 *)(0x14 + sp) = ra */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	s0, 0x100	/* s0 = 0x100 << 16 */
	and	v0, v0, s0	/* v0 = v0 & s0 */
	bnez	v0, $L64ab4	/* if (v0 != 0) goto $L64ab4 */
	ori	v0, zero, 1	/* delay slot (real work): v0 = 1 */
	jal	SetIntrMask	/* call SetIntrMask */
	move	a0, zero	/* delay slot (real work): a0 = zero */
	lw	a0, _qin	/* a0 = *(u32 *)(_qin) [lui+op macro] */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	sw	v0, g_75b54	/* *(u32 *)(g_75b54) = v0 [lui+op macro, via $1] */
	beq	a0, v1, $L64a08	/* if (a0 == v1) goto $L64a08 */
	nop		/* delay slot: nothing to donothing */
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	nop		/* nothing */
	and	v0, v0, s0	/* v0 = v0 & s0 */
	bnez	v0, $L64a08	/* if (v0 != 0) goto $L64a08 */
	nop		/* delay slot: nothing to donothing */
	la	s0, _qlog	/* s0 = &_qlog [lui+op macro] */
$L6482c:
	/* -- _exeque: loop: pop an entry, call it, log it in _qlog, advance _qout -- */
	lw	v0, _qout	/* v0 = *(u32 *)(_qout) [lui+op macro] */
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
	bne	v0, v1, $L6486c	/* if (v0 != v1) goto $L6486c */
	nop		/* delay slot: nothing to donothing */
	la	v0, g_75a44+12	/* v0 = &g_75a44+12 [lui+op macro] */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	nop		/* nothing */
	bnez	v0, $L6486c	/* if (v0 != 0) goto $L6486c */
	ori	a0, zero, 2	/* delay slot (real work): a0 = 2 */
	jal	DMACallback	/* call DMACallback */
	move	a1, zero	/* delay slot (real work): a1 = zero */
$L6486c:
	lw	a0, gp1	/* a0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(0 + a0) */
	lui	v1, 0x1000	/* v1 = 0x1000 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L648a4	/* if (v0 != 0) goto $L648a4 */
	move	v1, a0	/* delay slot (real work): v1 = a0 */
	lui	a0, 0x1000	/* a0 = 0x1000 << 16 */
$L64890:
	lw	v0, 0(v1)	/* v0 = *(u32 *)(0 + v1) */
	nop		/* nothing */
	and	v0, v0, a0	/* v0 = v0 & a0 */
	beqz	v0, $L64890	/* if (v0 == 0) goto $L64890 */
	nop		/* delay slot: nothing to donothing */
$L648a4:
	lw	a1, _qout	/* a1 = *(u32 *)(_qout) [lui+op macro] */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	sll	v1, a1, 1	/* v1 = a1 << 1 */
	addu	v1, v1, a1	/* v1 = v1 + a1 */
	.set	noat
	la	$1, _que+4	/* $1 = &_que+4 [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	a0, 0($1)	/* a0 = *(u32 *)(0 + $1) */
	.set	at
	lw	a1, _qout	/* a1 = *(u32 *)(_qout) [lui+op macro] */
	sll	v1, v1, 5	/* v1 = v1 << 5 */
	sll	v0, a1, 1	/* v0 = a1 << 1 */
	addu	v0, v0, a1	/* v0 = v0 + a1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	.set	noat
	la	$1, _que+8	/* $1 = &_que+8 [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	a1, 0($1)	/* a1 = *(u32 *)(0 + $1) */
	la	$1, _que	/* $1 = &_que [lui+op macro] */
	addu	$1, $1, v1	/* $1 = $1 + v1 */
	lw	v0, 0($1)	/* v0 = *(u32 *)(0 + $1) */
	.set	at
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	nop		/* delay slot: nothing to donothing */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	.set	noat
	la	$1, _que	/* $1 = &_que [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	v0, 0($1)	/* v0 = *(u32 *)(0 + $1) */
	.set	at
	nop		/* nothing */
	sw	v0, 0(s0)	/* *(u32 *)(0 + s0) = v0 */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	.set	noat
	la	$1, _que+4	/* $1 = &_que+4 [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	v0, 0($1)	/* v0 = *(u32 *)(0 + $1) */
	.set	at
	nop		/* nothing */
	sw	v0, 4(s0)	/* *(u32 *)(4 + s0) = v0 */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sll	v0, v0, 5	/* v0 = v0 << 5 */
	.set	noat
	la	$1, _que+8	/* $1 = &_que+8 [lui+op macro] */
	addu	$1, $1, v0	/* $1 = $1 + v0 */
	lw	v0, 0($1)	/* v0 = *(u32 *)(0 + $1) */
	.set	at
	nop		/* nothing */
	sw	v0, 8(s0)	/* *(u32 *)(8 + s0) = v0 */
	lw	v0, _qout	/* v0 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
	sw	v0, _qout	/* *(u32 *)(_qout) = v0 [lui+op macro, via $1] */
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	lw	v0, _qout	/* v0 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	beq	v1, v0, $L64a08	/* if (v1 == v0) goto $L64a08 */
	nop		/* delay slot: nothing to donothing */
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x100	/* v1 = 0x100 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	beqz	v0, $L6482c	/* if (v0 == 0) goto $L6482c */
	nop		/* delay slot: nothing to donothing */
$L64a08:
	/* -- _exeque: restore the mask; callback when drained -- */
	lw	a0, g_75b54	/* a0 = *(u32 *)(g_75b54) [lui+op macro] */
	jal	SetIntrMask	/* call SetIntrMask */
	nop		/* delay slot: nothing to donothing */
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	lw	v0, _qout	/* v0 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	bne	v1, v0, $L64a98	/* if (v1 != v0) goto $L64a98 */
	nop		/* delay slot: nothing to donothing */
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x100	/* v1 = 0x100 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L64a98	/* if (v0 != 0) goto $L64a98 */
	nop		/* delay slot: nothing to donothing */
	la	v1, g_75a44+8	/* v1 = &g_75a44+8 [lui+op macro] */
	lw	v0, 0(v1)	/* v0 = *(u32 *)(0 + v1) */
	nop		/* nothing */
	beqz	v0, $L64a98	/* if (v0 == 0) goto $L64a98 */
	nop		/* delay slot: nothing to donothing */
	lw	v0, g_75a44+12	/* v0 = *(u32 *)(g_75a44+12) [lui+op macro] */
	nop		/* nothing */
	beqz	v0, $L64a98	/* if (v0 == 0) goto $L64a98 */
	nop		/* delay slot: nothing to donothing */
	sw	zero, 0(v1)	/* *(u32 *)(0 + v1) = zero */
	lw	v0, g_75a44+12	/* v0 = *(u32 *)(g_75a44+12) [lui+op macro] */
	nop		/* nothing */
	jalr	ra, v0	/* call *v0 */
	nop		/* delay slot: nothing to donothing */
$L64a98:
	lw	v0, _qin	/* v0 = *(u32 *)(_qin) [lui+op macro] */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	subu	v0, v0, v1	/* v0 = v0 - v1 */
	andi	v0, v0, 0x3f	/* v0 = v0 & 0x3f */
$L64ab4:
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_exeque
/*
 * _reset - static int _reset(int mode)
 *
 * Purpose:   Masks interrupts, empties the queue, mode 0: GP1 00h reset, clears ctlbuf and _que; mode 1: soft stop; then restores the mask and, for mode 0, detects the type with f_64eec.
 * Inputs:    a0 = mode
 * Outputs:   v0 = 0 or GPU type
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_reset
_reset:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	s0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = s0 */
	move	s0, a0	/* s0 = a0 */
	sw	ra, 0x14(sp)	/* *(u32 *)(0x14 + sp) = ra */
	jal	SetIntrMask	/* call SetIntrMask */
	move	a0, zero	/* delay slot (real work): a0 = zero */
	sw	zero, _qout	/* *(u32 *)(_qout) = zero [lui+op macro, via $1] */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	sw	v0, g_75b58	/* *(u32 *)(g_75b58) = v0 [lui+op macro, via $1] */
	sw	v1, _qin	/* *(u32 *)(_qin) = v1 [lui+op macro, via $1] */
	andi	v1, s0, 7	/* v1 = s0 & 7 */
	beqz	v1, $L64b1c	/* if (v1 == 0) goto $L64b1c */
	ori	v0, zero, 1	/* delay slot (real work): v0 = 1 */
	beq	v1, v0, $L64b80	/* if (v1 == v0) goto $L64b80 */
	ori	v0, zero, 0x401	/* delay slot (real work): v0 = 0x401 */
	j	$L64bcc	/* goto $L64bcc */
	nop		/* delay slot: nothing to donothing */
$L64b1c:
	/* -- _reset mode 0: DMA2 off, DPCR enable, GP1 00h, clear ctlbuf and _que -- */
	lw	v1, d2_chcr	/* v1 = *(u32 *)(d2_chcr) [lui+op macro] */
	ori	v0, zero, 0x401	/* v0 = 0x401 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, d_pcr	/* v1 = *(u32 *)(d_pcr) [lui+op macro] */
	la	a0, ctlbuf	/* a0 = &ctlbuf [lui+op macro] */
	lw	v0, 0(v1)	/* v0 = *(u32 *)(0 + v1) */
	move	a1, zero	/* a1 = zero */
	ori	v0, v0, 0x800	/* v0 = v0 | 0x800 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	ori	a2, zero, 0x100	/* a2 = 0x100 */
	sw	zero, 0(v0)	/* *(u32 *)(0 + v0) = zero */
	jal	memset	/* call memset */
	nop		/* delay slot: nothing to donothing */
	la	a0, _que	/* a0 = &_que [lui+op macro] */
	move	a1, zero	/* a1 = zero */
	jal	memset	/* call memset */
	ori	a2, zero, 0x1800	/* delay slot (real work): a2 = 0x1800 */
	j	$L64bcc	/* goto $L64bcc */
	nop		/* delay slot: nothing to donothing */
$L64b80:
	/* -- _reset mode 1: flush FIFO (GP1 01h/02h) -- */
	lw	v1, d2_chcr	/* v1 = *(u32 *)(d2_chcr) [lui+op macro] */
	nop		/* nothing */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, d_pcr	/* v1 = *(u32 *)(d_pcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v1)	/* v0 = *(u32 *)(0 + v1) */
	nop		/* nothing */
	ori	v0, v0, 0x800	/* v0 = v0 | 0x800 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	lui	v0, 0x200	/* v0 = 0x200 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	lui	v0, 0x100	/* v0 = 0x100 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
$L64bcc:
	/* -- _reset: restore the mask; mode 0 detects the GPU type -- */
	lw	a0, g_75b58	/* a0 = *(u32 *)(g_75b58) [lui+op macro] */
	jal	SetIntrMask	/* call SetIntrMask */
	nop		/* delay slot: nothing to donothing */
	andi	v0, s0, 7	/* v0 = s0 & 7 */
	bnez	v0, $L64bf0	/* if (v0 != 0) goto $L64bf0 */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	jal	f_64eec	/* call f_64eec */
	move	a0, s0	/* delay slot (real work): a0 = s0 */
$L64bf0:
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_reset
/*
 * _sync - static int _sync(int mode)
 *
 * Purpose:   Mode 0 loops _exeque until the queue is empty and the GPU idle, with the timeout alarm.
 * Inputs:    a0 = mode (0 wait, else poll)
 * Outputs:   v0 = 0 done, or queue length
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	_sync
_sync:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	ra, 0x14(sp)	/* *(u32 *)(0x14 + sp) = ra */
	bnez	a0, $L64cb8	/* if (a0 != 0) goto $L64cb8 */
	sw	s0, 0x10(sp)	/* delay slot (real work): *(u32 *)(0x10 + sp) = s0 */
	jal	set_alarm	/* call set_alarm */
	nop		/* delay slot: nothing to donothing */
	j	$L64c3c	/* goto $L64c3c */
	nop		/* delay slot: nothing to donothing */
$L64c24:
	/* -- _sync mode 0: run the queue to empty, then wait for DMA and GPU ready -- */
	jal	_exeque	/* call _exeque */
	nop		/* delay slot: nothing to donothing */
	jal	get_alarm	/* call get_alarm */
	nop		/* delay slot: nothing to donothing */
	bnez	v0, $L64d38	/* if (v0 != 0) goto $L64d38 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
$L64c3c:
	lw	v1, _qin	/* v1 = *(u32 *)(_qin) [lui+op macro] */
	lw	v0, _qout	/* v0 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	beq	v1, v0, $L64c70	/* if (v1 == v0) goto $L64c70 */
	nop		/* delay slot: nothing to donothing */
	j	$L64c24	/* goto $L64c24 */
	nop		/* delay slot: nothing to donothing */
$L64c60:
	jal	get_alarm	/* call get_alarm */
	nop		/* delay slot: nothing to donothing */
	bnez	v0, $L64d38	/* if (v0 != 0) goto $L64d38 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
$L64c70:
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x100	/* v1 = 0x100 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L64c60	/* if (v0 != 0) goto $L64c60 */
	nop		/* delay slot: nothing to donothing */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x400	/* v1 = 0x400 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	beqz	v0, $L64c60	/* if (v0 == 0) goto $L64c60 */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$L64d38	/* goto $L64d38 */
	nop		/* delay slot: nothing to donothing */
$L64cb8:
	/* -- _sync mode != 0: run one step and report the queue length / idle state -- */
	lw	v0, _qin	/* v0 = *(u32 *)(_qin) [lui+op macro] */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	nop		/* nothing */
	subu	v0, v0, v1	/* v0 = v0 - v1 */
	andi	s0, v0, 0x3f	/* s0 = v0 & 0x3f */
	beqz	s0, $L64ce4	/* if (s0 == 0) goto $L64ce4 */
	nop		/* delay slot: nothing to donothing */
	jal	_exeque	/* call _exeque */
	nop		/* delay slot: nothing to donothing */
$L64ce4:
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x100	/* v1 = 0x100 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L64d24	/* if (v0 != 0) goto $L64d24 */
	nop		/* delay slot: nothing to donothing */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	lui	v1, 0x400	/* v1 = 0x400 << 16 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	bnez	v0, $L64d34	/* if (v0 != 0) goto $L64d34 */
	nop		/* delay slot: nothing to donothing */
$L64d24:
	bnez	s0, $L64d38	/* if (s0 != 0) goto $L64d38 */
	move	v0, s0	/* delay slot (real work): v0 = s0 */
	j	$L64d38	/* goto $L64d38 */
	ori	v0, zero, 1	/* delay slot (real work): v0 = 1 */
$L64d34:
	move	v0, s0	/* v0 = s0 */
$L64d38:
	lw	ra, 0x14(sp)	/* ra = *(u32 *)(0x14 + sp) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	_sync
/*
 * set_alarm - static void set_alarm(void)
 *
 * Purpose:   Deadline = VSync(-1) + 240, poll counter 0.
 * Inputs:    none
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	set_alarm
set_alarm:
	addiu	sp, sp, -24	/* sp = sp + -24 */
	sw	ra, 0x10(sp)	/* *(u32 *)(0x10 + sp) = ra */
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* delay slot (real work): a0 = -1 */
	addiu	v0, v0, 0xf0	/* v0 = v0 + 0xf0 */
	sw	v0, g_75b5c	/* *(u32 *)(g_75b5c) = v0 [lui+op macro, via $1] */
	sw	zero, g_75b60	/* *(u32 *)(g_75b60) = zero [lui+op macro, via $1] */
	lw	ra, 0x10(sp)	/* ra = *(u32 *)(0x10 + sp) */
	addiu	sp, sp, 0x18	/* sp = sp + 0x18 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	set_alarm
/*
 * get_alarm - static int get_alarm(void)
 *
 * Purpose:   Times out after 240 vblanks or 0xf0000 polls: prints the diagnostic (queue length, GPUSTAT, CHCR, MADR, last function), resets the GPU and returns -1.
 * Inputs:    none
 * Outputs:   v0 = 0 ok, -1 timed out
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	get_alarm
get_alarm:
	addiu	sp, sp, -32	/* sp = sp + -32 */
	sw	ra, 0x18(sp)	/* *(u32 *)(0x18 + sp) = ra */
	jal	VSync	/* call VSync */
	addiu	a0, zero, -1	/* delay slot (real work): a0 = -1 */
	lw	v1, g_75b5c	/* v1 = *(u32 *)(g_75b5c) [lui+op macro] */
	nop		/* nothing */
	slt	v1, v1, v0	/* v1 = (v1 < v0) */
	bnez	v1, $L64dd0	/* if (v1 != 0) goto $L64dd0 */
	nop		/* delay slot: nothing to donothing */
	lw	v1, g_75b60	/* v1 = *(u32 *)(g_75b60) [lui+op macro] */
	nop		/* nothing */
	addiu	v0, v1, 1	/* v0 = v1 + 1 */
	sw	v0, g_75b60	/* *(u32 *)(g_75b60) = v0 [lui+op macro, via $1] */
	lui	v0, 0xf	/* v0 = 0xf << 16 */
	slt	v0, v0, v1	/* v0 = (v0 < v1) */
	beqz	v0, $L64ed8	/* if (v0 == 0) goto $L64ed8 */
	nop		/* delay slot: nothing to donothing */
$L64dd0:
	/* -- get_alarm: timeout diagnostic and GPU reset -- */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	la	a0, $LC21	/* a0 = &$LC21 [lui+op macro] */
	lw	v0, 0(v1)	/* v0 = *(u32 *)(0 + v1) */
	lw	a1, _qin	/* a1 = *(u32 *)(_qin) [lui+op macro] */
	lw	v0, d2_madr	/* v0 = *(u32 *)(d2_madr) [lui+op macro] */
	lw	t0, _qout	/* t0 = *(u32 *)(_qout) [lui+op macro] */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	subu	a1, a1, t0	/* a1 = a1 - t0 */
	sw	v0, 0x10(sp)	/* *(u32 *)(0x10 + sp) = v0 */
	lw	v0, d2_chcr	/* v0 = *(u32 *)(d2_chcr) [lui+op macro] */
	lw	a2, 0(v1)	/* a2 = *(u32 *)(0 + v1) */
	lw	a3, 0(v0)	/* a3 = *(u32 *)(0 + v0) */
	jal	printf	/* call printf */
	andi	a1, a1, 0x3f	/* delay slot (real work): a1 = a1 & 0x3f */
	la	v0, _qlog	/* v0 = &_qlog [lui+op macro] */
	lw	a1, 0(v0)	/* a1 = *(u32 *)(0 + v0) */
	lw	a2, _qlog+4	/* a2 = *(u32 *)(_qlog+4) [lui+op macro] */
	lw	a3, _qlog+8	/* a3 = *(u32 *)(_qlog+8) [lui+op macro] */
	la	a0, $LC22	/* a0 = &$LC22 [lui+op macro] */
	jal	printf	/* call printf */
	nop		/* delay slot: nothing to donothing */
	jal	SetIntrMask	/* call SetIntrMask */
	move	a0, zero	/* delay slot (real work): a0 = zero */
	sw	zero, _qout	/* *(u32 *)(_qout) = zero [lui+op macro, via $1] */
	lw	v1, _qout	/* v1 = *(u32 *)(_qout) [lui+op macro] */
	sw	v0, g_75b58	/* *(u32 *)(g_75b58) = v0 [lui+op macro, via $1] */
	sw	v1, _qin	/* *(u32 *)(_qin) = v1 [lui+op macro, via $1] */
	lw	v1, d2_chcr	/* v1 = *(u32 *)(d2_chcr) [lui+op macro] */
	ori	v0, zero, 0x401	/* v0 = 0x401 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, d_pcr	/* v1 = *(u32 *)(d_pcr) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v1)	/* v0 = *(u32 *)(0 + v1) */
	nop		/* nothing */
	ori	v0, v0, 0x800	/* v0 = v0 | 0x800 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	lui	v0, 0x200	/* v0 = 0x200 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	lui	v0, 0x100	/* v0 = 0x100 << 16 */
	sw	v0, 0(v1)	/* *(u32 *)(0 + v1) = v0 */
	lw	a0, g_75b58	/* a0 = *(u32 *)(g_75b58) [lui+op macro] */
	jal	SetIntrMask	/* call SetIntrMask */
	nop		/* delay slot: nothing to donothing */
	j	$L64edc	/* goto $L64edc */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
$L64ed8:
	move	v0, zero	/* v0 = zero */
$L64edc:
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(0x18 + sp) */
	addiu	sp, sp, 0x20	/* sp = sp + 0x20 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	get_alarm
/*
 * f_64eec - static int f_64eec(int mode)
 *
 * Purpose:   GPU type probe: GP1 10h/07h version, then a draw-mode write E1h to test texture-disable bit 12 in GPUSTAT.
 * Inputs:    a0 = mode (bit 3 selects the 0x09000001 display mode)
 * Outputs:   v0 = GPU type 1..4 (0 unsupported)
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	f_64eec
f_64eec:
	lui	v1, 0x1000	/* v1 = 0x1000 << 16 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	ori	v1, v1, 7	/* v1 = v1 | 7 */
	sw	v1, 0(v0)	/* *(u32 *)(0 + v0) = v1 */
	lw	a1, gp0	/* a1 = *(u32 *)(gp0) [lui+op macro] */
	lui	v1, 0xff	/* v1 = 0xff << 16 */
	lw	v0, 0(a1)	/* v0 = *(u32 *)(0 + a1) */
	ori	v1, v1, 0xffff	/* v1 = v1 | 0xffff */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	ori	v1, zero, 2	/* v1 = 2 */
	beq	v0, v1, $L64f88	/* if (v0 == v1) goto $L64f88 */
	lui	v1, 0xe100	/* delay slot (real work): v1 = 0xe100 << 16 */
	lw	v0, gp1	/* v0 = *(u32 *)(gp1) [lui+op macro] */
	nop		/* nothing */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	ori	v1, v1, 0x1000	/* v1 = v1 | 0x1000 */
	andi	v0, v0, 0x3fff	/* v0 = v0 & 0x3fff */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a1)	/* *(u32 *)(0 + a1) = v0 */
	lw	v0, gp0	/* v0 = *(u32 *)(gp0) [lui+op macro] */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	lw	v0, 0(v0)	/* v0 = *(u32 *)(0 + v0) */
	nop		/* nothing */
	lw	v0, 0(v1)	/* v0 = *(u32 *)(0 + v1) */
	nop		/* nothing */
	andi	v0, v0, 0x1000	/* v0 = v0 & 0x1000 */
	bnez	v0, $L64f78	/* if (v0 != 0) goto $L64f78 */
	andi	v1, a0, 8	/* delay slot (real work): v1 = a0 & 8 */
	j	$L64fb4	/* goto $L64fb4 */
	move	v0, zero	/* delay slot (real work): v0 = zero */
$L64f78:
	bnez	v1, $L64fb4	/* if (v1 != 0) goto $L64fb4 */
	ori	v0, zero, 2	/* delay slot (real work): v0 = 2 */
	j	$L64fb4	/* goto $L64fb4 */
	ori	v0, zero, 1	/* delay slot (real work): v0 = 1 */
$L64f88:
	andi	v0, a0, 8	/* v0 = a0 & 8 */
	beqz	v0, $L64fb0	/* if (v0 == 0) goto $L64fb0 */
	lui	a0, 0x900	/* delay slot (real work): a0 = 0x900 << 16 */
	ori	a0, a0, 1	/* a0 = a0 | 1 */
	lw	v1, gp1	/* v1 = *(u32 *)(gp1) [lui+op macro] */
	ori	v0, zero, 4	/* v0 = 4 */
	sw	a0, 0(v1)	/* *(u32 *)(0 + v1) = a0 */
	j	$L64fb4	/* goto $L64fb4 */
	nop		/* delay slot: nothing to donothing */
$L64fb0:
	ori	v0, zero, 3	/* v0 = 3 */
$L64fb4:
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	f_64eec
/*
 * memset - static void memset(char *p, int c, int n)
 *
 * Purpose:   Byte fill loop (same bytes as the BS.LIB memset).
 * Inputs:    a0 = p, a1 = c, a2 = n
 * Outputs:   none
 * Clobbers:  caller-saved registers (v0-v1, a0-a3, t0-t9 as used); s-registers saved in the frame.
 * State:     the .data state block g_75a44 and the driver table jtb (via jtbp); hardware through
 *            the pointers gp0/gp1 (GP0 0x1f801810, GP1/GPUSTAT 0x1f801814) and the DMA2/DMA6
 *            registers (0x1f8010a0.., 0x1f8010e0.., DPCR 0x1f8010f0).
 * Callers:   game code and other LIBGPU modules (exports); the jtb table or siblings (statics).
 */
	.ent	memset
memset:
	addiu	sp, sp, -8	/* sp = sp + -8 */
	beqz	a2, $L64fdc	/* if (a2 == 0) goto $L64fdc */
	addiu	v0, a2, -1	/* delay slot (real work): v0 = a2 + -1 */
	addiu	v1, zero, -1	/* v1 = -1 */
$L64fcc:
	sb	a1, 0(a0)	/* *(u8 *)(0 + a0) = a1 */
	addiu	v0, v0, -1	/* v0 = v0 + -1 */
	bne	v0, v1, $L64fcc	/* if (v0 != v1) goto $L64fcc */
	addiu	a0, a0, 1	/* delay slot (real work): a0 = a0 + 1 */
$L64fdc:
	addiu	sp, sp, 8	/* sp = sp + 8 */
	jr	ra	/* return */
	nop		/* delay slot: nothing to donothing */
	.end	memset
