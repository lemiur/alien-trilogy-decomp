/*
 * libspu_spu.s - LIBSPU module SPU: reset, init, FIFO / DMA transfer, the ioctl dispatcher.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Placed by
 * config/libspu_spu.json and the "libspu_spu" entry of config/data.json; counted as "library
 * assembly", never as matched C and never as certified library.
 *
 * Assembler: ASPSX 2.08 (A208, config/asm_build.json), not the project's ASPSX 2.34.
 * _spu_ioctl's three checked signed remainders are written as the rem macro; ASPSX 2.08 expands
 * each to retail's exact sequence, ending in the overflow trap word 0x00001770
 * ("tge zero, zero, 0x5d"), where ASPSX 2.21 and 2.34 write "break 6". That expansion is the
 * evidence that Sony assembled this variant module with the Runtime 3.0-era assembler (the
 * variant runtime is the one the 3.6 kanji samples KANJIFNT MAIN.EXE and SJISCODE CODEVIEW.EXE
 * link: LIBGPU sys.c 1.104, intr.c 1.71). The unsigned remainder in f_5b3f8 is its remu macro.
 * The two module-local routines carry .ent/.end, which gives them local symbol records.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      SPU (0x80059984..0x8005b577).
 * Exports:     _spu_reset (module .text+0x0000, 144 bytes), _spu_init (+0x0090, 1444), _spu_close
 *              (+0x0634, 52), _spu_open (+0x0668, 44), _spu_FiDMA (+0x09d8, 416), _spu_r_ (+0x0b78,
 *              560), _spu_t (+0x0da8, 748), _spu_write (+0x1094, 140), _spu_read (+0x1120, 104),
 *              _spu_ioctl (+0x1188, 2284).
 *              Module-local routines (statics: "static": true in the unit map): f_5a018 (+0x0694,
 *              836; the FIFO writer) and f_5b3f8 (+0x1a74, 384; the voice attribute writer).
 * Imports:     printf, _SpuDataCallback (S_DCB), _SpuCallback, _spu_setReverbAttr, f_5ba48,
 *              f_5bde0, g_73d50 (S_INI's data word 0x5c, the DMA-in-progress flag).
 * Own data:    .data 92 bytes at 0x80073c88..0x80073ce3 (library state, DMA register pointers, dummy
 *              ADPCM block, DMA bookkeeping words) and .rdata 89 bytes at 0x80012bdc..0x80012c34
 *              (the timeout format string and four wait descriptions). _spu_RXX at 0x80073c90 is
 *              the module's own definition of the SPU register base; the other modules of this
 *              build read it (S_SRA, SPU_IO), except S_GKS and S_SCA, which carry private copies.
 *
 * Nearest archived version: Runtime Library 3.3 LIBSPU.LIB SPU (also 3.5, 3.6), but retail is an
 * older rebuild. Against 3.3 SPU (4976 text bytes), 848 words are identical and 168 are relocated
 * words, which together cover _spu_reset, most of _spu_init, f_5a018, _spu_FiDMA, _spu_t,
 * _spu_write and _spu_read (word alignment with difflib, relocated words masked; 24 differing
 * regions over the retail module). The differences: (1) _spu_init lacks 3.3's inline voice
 * writer (it calls f_5b3f8, which sits after _spu_ioctl and equals 3.3 SPU+0x648..0x7c8 with
 * relocations masked), (2) _spu_close and _spu_open exist (they are in the 3.0 archive and not
 * in 3.3), (3) the DMA setup in _spu_r_ and _spu_t goes through six pointer words
 * (g_73c94..g_73ca8) and sets SPU_DELAY, DICR and DPCR itself, (4) the module owns the older
 * in-module _spu_ioctl (2.6 / 3.0 shape, every request inline; the 3.3 dispatcher is a 752-byte
 * routine in SPU_IO), whose remainder traps are tge instead of break 6. The .data differs from
 * 3.3's 96 bytes: no _spu_mem_mode_unit / _spu_mem_mode_unitM words, _spu_transMode starts at
 * 1, a DICR pointer word is added. Summary: "Runtime 3.3 SPU with older in-module ioctl,
 * register-pointer DMA setup, _spu_close / _spu_open from 3.0 and the older data layout".
 *
 * The routines are compiled C (GCC register use, scheduled delay slots, volatile-local delay
 * loops); they are transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */

/* ---- .rdata: the module's string literals (.rdata+0x00..0x5b), referenced with la ---- */
	.rdata

/*
 * s_spu_timeout_fmt - format of the poll timeout report
 *
 * Purpose:     printf format "SPU:T/O [%s]\n"; the %s names the wait that timed out.
 * Layout:      NUL-terminated string, 14 bytes, followed by 2 bytes of alignment padding.
 * Users:       _spu_init, f_5a018, _spu_ioctl.
 */
s_spu_timeout_fmt:
	.ascii	"SPU:T/O [%s]\n\000"
	.align	2

/*
 * s_wait_reset - wait description: SPU did not go idle after reset
 *
 * Layout:      NUL-terminated string, 13 bytes, 3 bytes of padding.
 * Users:       _spu_init (SPUSTAT idle poll).
 */
s_wait_reset:
	.ascii	"wait (reset)\000"
	.align	2

/*
 * s_wait_wrdy - wait description: the data transfer busy flag did not fall
 *
 * Layout:      NUL-terminated string, 19 bytes, 1 byte of padding.
 * Users:       f_5a018 (SPUSTAT bit 10 poll).
 */
s_wait_wrdy:
	.ascii	"wait (wrdy H -> L)\000"
	.align	2

/*
 * s_wait_dmaf - wait description: the transfer mode did not return to the saved state
 *
 * Layout:      NUL-terminated string, 20 bytes, no padding.
 * Users:       f_5a018 (final SPUSTAT poll).
 */
s_wait_dmaf:
	.ascii	"wait (dmaf clear/W)\000"
	.align	2

/* ---- .data: library state, 92 bytes at 0x80073c88 ---- */
	.data

/*
 * g_73c88 - poll counter
 *
 * Purpose:     counts SPUSTAT / SPUCNT polls in the bounded waits (limit 0x1388 = 5000).
 * Layout:      one 32-bit word, reset to 0 before every wait.
 * Users:       _spu_init, f_5a018, _spu_ioctl.
 */
g_73c88:
	.word	0

/*
 * _spu_tsa - sound RAM transfer start address
 *
 * Purpose:     the address (in 8-byte units) that the next transfer starts at. Only the low halfword
 *              is used (lhu / sh); it is copied into the TSA register before each transfer.
 * Layout:      one 32-bit word.
 * Users:       _spu_init, f_5a018, _spu_t, _spu_write, _spu_read, _spu_ioctl.
 */
	.globl	_spu_tsa
_spu_tsa:
	.word	0

/*
 * _spu_RXX - SPU register file base pointer
 *
 * Purpose:     the address of the SPU register file (0x1f801c00), read through a pointer by all LIBSPU
 *              code. Defined here; read by every routine of this module and by S_SRA and SPU_IO.
 * Layout:      one 32-bit pointer.
 */
	.globl	_spu_RXX
_spu_RXX:
	.word	0x1f801c00

/*
 * g_73c94 .. g_73ca8 - pointers to the DMA and timing registers used by the transfer code
 *
 * Purpose:     six words, each the address of one hardware register, read through a pointer (a
 *              lui/lw pair, then an indexed access) like _spu_RXX:
 *              g_73c94 = 0x1f8010c0 (D4_MADR, SPU DMA base address)
 *              g_73c98 = 0x1f8010c4 (D4_BCR, block control: block size | block count << 16)
 *              g_73c9c = 0x1f8010c8 (D4_CHCR, channel control: direction, sync, start)
 *              g_73ca0 = 0x1f8010f0 (DPCR, DMA priority / channel enable)
 *              g_73ca4 = 0x1f8010f4 (DICR, DMA interrupt control)
 *              g_73ca8 = 0x1f801014 (SPU_DELAY, SPU bus timing)
 * Users:       g_73c94 / g_73c98 / g_73c9c: _spu_r_, _spu_t. g_73ca0: _spu_init, _spu_r_, _spu_t.
 *              g_73ca4: _spu_r_, _spu_t. g_73ca8: _spu_r_, _spu_t.
 */
g_73c94:
	.word	0x1f8010c0
g_73c98:
	.word	0x1f8010c4
g_73c9c:
	.word	0x1f8010c8
g_73ca0:
	.word	0x1f8010f0
g_73ca4:
	.word	0x1f8010f4
g_73ca8:
	.word	0x1f801014

/*
 * _spu_transMode - transfer mode
 *
 * Purpose:     1 = sound RAM transfers go through DMA channel 4 (default), 0 = manual FIFO
 *              writes (f_5a018). Set by ioctl requests 0x21 / 0x2e, read by request 0x2f and by
 *              _spu_write.
 * Layout:      one 32-bit word, initial value 1 (the 3.3 archive starts at 0).
 */
	.globl	_spu_transMode
_spu_transMode:
	.word	1

/*
 * _spu_addrMode - voice start address mode
 *
 * Purpose:     0 = start addresses are already in 8-byte units; non-zero = byte addresses to be
 *              rounded and shifted (f_5b3f8). _spu_init clears it.
 * Layout:      one 32-bit word, initial value 0.
 */
	.globl	_spu_addrMode
_spu_addrMode:
	.word	0

/*
 * _spu_mem_mode - sound RAM address rounding unit
 *
 * Purpose:     byte addresses that are not a multiple of this value are rounded up by adding it
 *              (ioctl 0x13, 0x2b, 0x68; f_5b3f8). The 3.3 archive splits this into _spu_mem_mode
 *              and _spu_mem_mode_unit (8); this build has the single word.
 * Layout:      one 32-bit word, initial value 2.
 */
	.globl	_spu_mem_mode
_spu_mem_mode:
	.word	2

/*
 * _spu_mem_mode_plus - sound RAM address shift
 *
 * Purpose:     byte address >> this = SPU address unit (3: units of 8 bytes). Read by the API
 *              modules too (S_M_INIT, S_M_UTIL, SpuSetVoiceAttr).
 * Layout:      one 32-bit word, initial value 3.
 */
	.globl	_spu_mem_mode_plus
_spu_mem_mode_plus:
	.word	3

/*
 * _spu_inTransfer - transfer-in-progress flag
 *
 * Purpose:     1 when no transfer is running (initial value 1, set by _spu_init); the SpuWrite /
 *              SpuIsTransferCompleted modules toggle it.
 * Layout:      one 32-bit word.
 */
	.globl	_spu_inTransfer
_spu_inTransfer:
	.word	1

/*
 * _spu_transferCallback - user callback for transfer completion
 *
 * Purpose:     function pointer called by _spu_FiDMA when a DMA transfer finishes; 0 = none.
 * Layout:      one 32-bit pointer.
 */
	.globl	_spu_transferCallback
_spu_transferCallback:
	.word	0

/*
 * _spu_IRQCallback - user callback for SPU IRQ
 *
 * Purpose:     function pointer for the SPU IRQ; cleared by _spu_init and _spu_close, set through the
 *              S_CB module.
 * Layout:      one 32-bit pointer.
 */
	.globl	_spu_IRQCallback
_spu_IRQCallback:
	.word	0

/*
 * g_73cc8 - silent loop block
 *
 * Purpose:     one 16-byte ADPCM block of 0x07 bytes (shift / filter 7, flags 7: loop start,
 *              loop end, repeat), uploaded by a cold _spu_init to sound RAM 0x1000 so that every
 *              voice has a valid silent sample.
 * Layout:      16 bytes.
 * Users:       _spu_init.
 */
g_73cc8:
	.word	0x07070707
	.word	0x07070707
	.word	0x07070707
	.word	0x07070707

/*
 * g_73cd8 - DMA direction flag
 *
 * Purpose:     1 = the current or last transfer reads the SPU (SPU to RAM), 0 = it writes.
 * Layout:      one 32-bit word.
 * Users:       _spu_r_, _spu_t, _spu_FiDMA.
 */
g_73cd8:
	.word	0

/*
 * g_73cdc - DMA RAM address
 *
 * Purpose:     the RAM address of the transfer started by _spu_t op 3.
 * Layout:      one 32-bit word.
 */
g_73cdc:
	.word	0

/*
 * g_73ce0 - DMA block count
 *
 * Purpose:     the number of 64-byte blocks of the transfer started by _spu_t op 3.
 * Layout:      one 32-bit word.
 */
g_73ce0:
	.word	0

	.text

/*
 * _spu_reset - switch the SPU off briefly and restore the control register
 *
 * Purpose:     int _spu_reset(void). Reads SPUCNT, writes it back with the SPU enable bit (15) and the
 *              transfer mode bits (5-4) cleared, waits a settle delay, then writes the original value
 *              back with only the transfer mode bits cleared. Net effect: any running transfer mode
 *              is dropped and the SPU is power-cycled through its enable bit.
 * Inputs:      none.
 * Outputs:     v0 = 0.
 * Clobbers:    v0, v1, a0. Frame of 8 bytes for the delay loop's two words.
 * Hardware:    SPUCNT (0x1f801daa) read once and written twice, through _spu_RXX.
 * Reads:       _spu_RXX.
 * Callers:     none in the retail image (the 3.3 archive exports it; the linker keeps the whole module).
 * Callees:     none.
 */
	.globl	_spu_reset
_spu_reset:

	/*
	 * v1 = _spu_RXX; a0 = SPUCNT; SPUCNT = a0 & 0x7fcf (SPU enable and transfer mode bits cleared).
	 */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1aa(v1)	/* a0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	addiu	sp, sp, -8	/* sp = sp - 8 */
	andi	v0, a0, 0x7fcf	/* v0 = a0 & 0x7fcf */
	sh	v0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = v0 [SPU SPUCNT (control)] */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	sw	zero, 0(sp)	/* *(u32 *)(sp) = zero */
	j	$L599dc	/* goto $L599dc */
	nop	/* delay slot: nop */
$L599b4:
	lw	v1, 4(sp)	/* v1 = *(u32 *)(sp + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(sp)	/* *(u32 *)(sp) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
$L599dc:
	nop	/* load delay slot */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L599b4	/* if (v0 != 0) goto $L599b4 */
	nop	/* delay slot: nop */

	/*
	 * Restore: v0 = _spu_RXX; SPUCNT = original & 0xffcf (the enable bit comes back, the transfer mode stays cleared); return 0.
	 */
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	andi	a0, a0, 0xffcf	/* a0 = a0 & 0xffcf */
	sh	a0, 0x1aa(v0)	/* *(u16 *)(v0 + 0x1aa) = a0 [SPU SPUCNT (control)] */
	move	v0, zero	/* v0 = zero */
	addiu	sp, sp, 8	/* sp = sp + 8 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * _spu_init - initialise the SPU hardware and the library's transfer state
 *
 * Purpose:     int _spu_init(int mode). mode == 0 is the cold initialisation done by SpuInit (full
 *              voice and sound RAM setup); mode != 0 is the hot variant (SpuInitHot), which only
 *              resets the global registers. Sequence: reset the library state (transfer mode = DMA,
 *              address mode = 0, transfer address = 0), enable DMA channel 4 in DPCR, clear SPUCNT,
 *              wait for SPUSTAT to go idle, silence the global volumes, key off every voice and clear
 *              the reverb voice enables. A cold start additionally clears pitch modulation, noise
 *              and the CD / external volumes, writes a 16-byte silent loop block to sound RAM
 *              address 0x1000, programs all 24 voices to play it (volume 0, pitch 0x3fff), keys every
 *              voice on and off again (with settle delays), and finally enables and unmutes the SPU.
 * Inputs:      a0 = mode (kept in s1).
 * Outputs:     v0 = 0.
 * Clobbers:    v0, v1, a0, a1, a2 (s0, s1, ra saved). Frame of 0x48 bytes: the voice attribute record
 *              for f_5b3f8 at sp+0x10..0x2f and the delay-loop words at sp+0x30, sp+0x34.
 * Hardware:    SPUCNT, SPUSTAT, SPUDTC, MVOLL/R, RVOLL/R, KON, KOFF, PMON, NON, EON, CDVOL, EXTVOL
 *              (all through _spu_RXX), the DMA control register DPCR (0x1f8010f0, through g_73ca0).
 * Writes:      g_73c88 (timeout counter), _spu_transMode, _spu_addrMode, _spu_tsa, _spu_mem_mode,
 *              _spu_mem_mode_plus, _spu_inTransfer, _spu_transferCallback, _spu_IRQCallback.
 * Callers:     SpuInit / SpuInitHot module code (jal at 0x8005c508).
 * Callees:     f_5a018 (sound RAM upload), f_5b3f8 (voice attribute writer), printf (poll timeout).
 * Registers:   s0 = 0xffff (key mask constant), s1 = mode.
 */
	.globl	_spu_init
_spu_init:

	/*
	 * Prologue: frame 0x48, save s0/s1/ra; s1 = mode.
	 */
	addiu	sp, sp, -0x48	/* sp = sp - 72 */
	sw	s1, 0x3c(sp)	/* *(u32 *)(sp + 0x3c) = s1 */
	move	s1, a0	/* s1 = a0 */

	/*
	 * Library state: a0 = DPCR pointer (g_73ca0); _spu_transMode = 1 (DMA); _spu_addrMode = 0; _spu_tsa = 0
	 * (halfword store; the AT-based stores are lui AT + sw macros).
	 */
	lw	a0, g_73ca0	/* a0 = g_73ca0 (lui+lw macro) */
	ori	v0, zero, 1	/* v0 = 1 */
	sw	ra, 0x40(sp)	/* *(u32 *)(sp + 0x40) = ra */
	sw	s0, 0x38(sp)	/* *(u32 *)(sp + 0x38) = s0 */
	sw	v0, _spu_transMode	/* *(u32 *)&_spu_transMode = v0 (lui AT + sw macro, through AT) */
	sw	zero, _spu_addrMode	/* *(u32 *)&_spu_addrMode = zero (lui AT + sw macro, through AT) */
	sh	zero, _spu_tsa	/* *(u16 *)&_spu_tsa = zero (lui AT + sh macro, through AT) */

	/*
	 * DPCR |= 0x000b0000: DMA channel 4 (the SPU) enabled, priority 3 (nibble 0xb at bits 16-19).
	 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	lui	v1, 0xb	/* v1 = 0xb << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */

	/*
	 * SPUCNT = 0: SPU disabled and muted, transfer mode off, reverb and CD / external audio off.
	 */
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	sh	zero, 0x1aa(v0)	/* *(u16 *)(v0 + 0x1aa) = zero [SPU SPUCNT (control)] */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59acc	/* if (v0 == 0) goto $L59acc */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59a8c:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59a8c	/* if (v0 != 0) goto $L59a8c */
	nop	/* delay slot: nop */

	/*
	 * Wait for the SPU to go idle: g_73c88 = 0 (poll counter); poll SPUSTAT & 0x7ff (mode, IRQ and DMA request
	 * flags) until it reads zero. v0 is left as 2 on every exit (delay slots), used as _spu_mem_mode below.
	 */
$L59acc:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1ae(v0)	/* v0 = *(u16 *)(v0 + 0x1ae) [SPU SPUSTAT (status)] */
	sw	zero, g_73c88	/* *(u32 *)&g_73c88 = zero (lui AT + sw macro, through AT) */
	andi	v0, v0, 0x7ff	/* v0 = v0 & 0x7ff */
	beq	v0, zero, $L59b54	/* if (v0 == 0) goto $L59b54 */
	ori	v0, zero, 2	/* delay slot (real work): v0 = 2 */

	/*
	 * Not idle: count the poll (g_73c88++); give up after 0x1388 (5000) polls.
	 */
$L59af0:
	lw	v0, g_73c88	/* v0 = g_73c88 (lui+lw macro) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, g_73c88	/* *(u32 *)&g_73c88 = v0 (lui AT + sw macro, through AT) */
	slti	v0, v0, 0x1389	/* v0 = (v0 < 5001) */
	bne	v0, zero, $L59b34	/* if (v0 != 0) goto $L59b34 */
	nop	/* delay slot: nop */

	/*
	 * Timeout: printf("SPU:T/O [%s]\n", "wait (reset)") and leave the wait loop.
	 */
	la	a0, s_spu_timeout_fmt	/* a0 = &s_spu_timeout_fmt (lui+addiu macro) */
	la	a1, s_wait_reset	/* a1 = &s_wait_reset (lui+addiu macro) */
	jal	printf	/* call printf */
	nop	/* delay slot: nop */
	j	$L59b54	/* goto $L59b54 */
	ori	v0, zero, 2	/* delay slot (real work): v0 = 2 */

	/*
	 * Poll again: reload SPUSTAT & 0x7ff; if still non-zero go back to the counting step.
	 */
$L59b34:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1ae(v0)	/* v0 = *(u16 *)(v0 + 0x1ae) [SPU SPUSTAT (status)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x7ff	/* v0 = v0 & 0x7ff */
	bne	v0, zero, $L59af0	/* if (v0 != 0) goto $L59af0 */
	ori	v0, zero, 2	/* delay slot (real work): v0 = 2 */

	/*
	 * _spu_mem_mode = 2; _spu_mem_mode_plus = 3 (v0 = 3).
	 */
$L59b54:
	sw	v0, _spu_mem_mode	/* *(u32 *)&_spu_mem_mode = v0 (lui AT + sw macro, through AT) */
	ori	v0, zero, 3	/* v0 = 3 */
	sw	v0, _spu_mem_mode_plus	/* *(u32 *)&_spu_mem_mode_plus = v0 (lui AT + sw macro, through AT) */
	ori	v0, zero, 4	/* v0 = 4 */

	/*
	 * SPUDTC = 4 (transfer type 2, normal); main and reverb output volumes (MVOLL, MVOLR, RVOLL, RVOLR) = 0;
	 * KOFF low/high = 0xffff (every voice keyed off); EON low/high = 0 (no voice sends to reverb). s0 = 0xffff.
	 */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	ori	s0, zero, 0xffff	/* s0 = 0xffff */
	sh	v0, 0x1ac(v1)	/* *(u16 *)(v1 + 0x1ac) = v0 [SPU SPUDTC (data transfer control)] */
	sh	zero, 0x180(v1)	/* *(u16 *)(v1 + 0x180) = zero [SPU MVOLL (main volume left)] */
	sh	zero, 0x182(v1)	/* *(u16 *)(v1 + 0x182) = zero [SPU MVOLR (main volume right)] */
	sh	zero, 0x184(v1)	/* *(u16 *)(v1 + 0x184) = zero [SPU RVOLL (reverb output volume left)] */
	sh	zero, 0x186(v1)	/* *(u16 *)(v1 + 0x186) = zero [SPU RVOLR (reverb output volume right)] */
	sh	s0, 0x18c(v1)	/* *(u16 *)(v1 + 0x18c) = s0 [SPU KOFF low (voice key off, voices 0-15)] */
	sh	s0, 0x18e(v1)	/* *(u16 *)(v1 + 0x18e) = s0 [SPU KOFF high (voices 16-23)] */
	sh	zero, 0x198(v1)	/* *(u16 *)(v1 + 0x198) = zero [SPU EON low (reverb enable, voices 0-15)] */
	sh	zero, 0x19a(v1)	/* *(u16 *)(v1 + 0x19a) = zero [SPU EON high (voices 16-23)] */

	/*
	 * mode != 0 (hot start): skip to the final enable at 0x80059f74; the delay slot sets the return value 0.
	 */
	bne	s1, zero, $L59f74	/* if (s1 != 0) goto $L59f74 */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Cold start (mode == 0): a0 = &g_73cc8 (16 silent-loop bytes), a1 = 0x10, v0 = 0x200 (the dummy block's address in
	 * 8-byte units = byte 0x1000). Clear PMON, NON (low and high) and the CD / external audio volumes.
	 */
	la	a0, g_73cc8	/* a0 = &g_73cc8 (lui+addiu macro) */
	ori	a1, zero, 0x10	/* a1 = 0x10 */
	ori	v0, zero, 0x200	/* v0 = 0x200 */
	sh	zero, 0x190(v1)	/* *(u16 *)(v1 + 0x190) = zero [SPU PMON low (pitch modulation, voices 0-15)] */
	sh	zero, 0x192(v1)	/* *(u16 *)(v1 + 0x192) = zero [SPU PMON high (voices 16-23)] */
	sh	zero, 0x194(v1)	/* *(u16 *)(v1 + 0x194) = zero [SPU NON low (noise mode, voices 0-15)] */
	sh	zero, 0x196(v1)	/* *(u16 *)(v1 + 0x196) = zero [SPU NON high (voices 16-23)] */
	sh	zero, 0x1b0(v1)	/* *(u16 *)(v1 + 0x1b0) = zero [SPU CDVOLL (CD audio input volume left)] */
	sh	zero, 0x1b2(v1)	/* *(u16 *)(v1 + 0x1b2) = zero [SPU CDVOLR (CD audio input volume right)] */
	sh	zero, 0x1b4(v1)	/* *(u16 *)(v1 + 0x1b4) = zero [SPU EXTVOLL (external audio volume left)] */
	sh	zero, 0x1b6(v1)	/* *(u16 *)(v1 + 0x1b6) = zero [SPU EXTVOLR (external audio volume right)] */

	/*
	 * _spu_tsa = 0x200; f_5a018(g_73cc8, 16) writes the dummy ADPCM block (bytes 07 07 ...: shift/filter 7, flags 7 =
	 * loop start + end + repeat) to sound RAM 0x1000, so every voice has a valid silent sample to point at.
	 */
	sh	v0, _spu_tsa	/* *(u16 *)&_spu_tsa = v0 (lui AT + sh macro, through AT) */
	jal	f_5a018	/* call f_5a018 */
	nop	/* delay slot: nop */

	/*
	 * Build a voice attribute record on the stack for f_5b3f8, all 24 voices: sp+0x10 voice mask = -1; sp+0x14 attribute
	 * mask = 0x1f (volumes, pitch, start address, ADSR); sp+0x18/0x1a volumes = 0; sp+0x20 pitch = 0x3fff;
	 * sp+0x24 start address = 0x200; sp+0x2c ADSR (both halfwords) = 0. _spu_addrMode = 0 first, so the address is
	 * stored as given. a0 = &record, call f_5b3f8.
	 */
	addiu	v0, zero, -1	/* v0 = -1 */
	sw	v0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = v0 */
	ori	v0, zero, 0x1f	/* v0 = 0x1f */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	ori	v0, zero, 0x3fff	/* v0 = 0x3fff */
	sh	v0, 0x20(sp)	/* *(u16 *)(sp + 0x20) = v0 */
	ori	v0, zero, 0x200	/* v0 = 0x200 */
	sh	zero, 0x18(sp)	/* *(u16 *)(sp + 0x18) = zero */
	sh	zero, 0x1a(sp)	/* *(u16 *)(sp + 0x1a) = zero */
	sw	v0, 0x24(sp)	/* *(u32 *)(sp + 0x24) = v0 */
	sw	zero, 0x2c(sp)	/* *(u32 *)(sp + 0x2c) = zero */
	sw	zero, _spu_addrMode	/* *(u32 *)&_spu_addrMode = zero (lui AT + sw macro, through AT) */
	jal	f_5b3f8	/* call f_5b3f8 */
	addiu	a0, sp, 0x10	/* delay slot (real work): a0 = sp + 16 */

	/*
	 * Key on every voice (they all play the silent loop): v1 = _spu_RXX; KON low is read (value unused, a compiler
	 * read-modify-write) and set to 0xffff; KON high |= 0xff (voices 16-23).
	 */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x188(v1)	/* v0 = *(u16 *)(v1 + 0x188) [SPU KON low (voice key on, voices 0-15)] */
	sh	s0, 0x188(v1)	/* *(u16 *)(v1 + 0x188) = s0 [SPU KON low (voice key on, voices 0-15)] */
	lhu	v0, 0x18a(v1)	/* v0 = *(u16 *)(v1 + 0x18a) [SPU KON high (voices 16-23)] */
	nop	/* load delay slot */
	ori	v0, v0, 0xff	/* v0 = v0 | 0xff */
	sh	v0, 0x18a(v1)	/* *(u16 *)(v1 + 0x18a) = v0 [SPU KON high (voices 16-23)] */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59ca4	/* if (v0 == 0) goto $L59ca4 */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59c64:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59c64	/* if (v0 != 0) goto $L59c64 */
	nop	/* delay slot: nop */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
$L59ca4:
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59d04	/* if (v0 == 0) goto $L59d04 */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59cc4:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59cc4	/* if (v0 != 0) goto $L59cc4 */
	nop	/* delay slot: nop */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
$L59d04:
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59d64	/* if (v0 == 0) goto $L59d64 */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59d24:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59d24	/* if (v0 != 0) goto $L59d24 */
	nop	/* delay slot: nop */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
$L59d64:
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59dc4	/* if (v0 == 0) goto $L59dc4 */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59d84:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59d84	/* if (v0 != 0) goto $L59d84 */
	nop	/* delay slot: nop */

	/*
	 * Four settle delays above; now key every voice off again: KOFF low = 0xffff, KOFF high |= 0xff (read-modify-write
	 * of the old value, as the compiler emitted it). Four more settle delays follow.
	 */
$L59dc4:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x18c(v1)	/* v0 = *(u16 *)(v1 + 0x18c) [SPU KOFF low (voice key off, voices 0-15)] */
	nop	/* load delay slot */
	ori	v0, zero, 0xffff	/* v0 = 0xffff */
	sh	v0, 0x18c(v1)	/* *(u16 *)(v1 + 0x18c) = v0 [SPU KOFF low (voice key off, voices 0-15)] */
	lhu	v0, 0x18e(v1)	/* v0 = *(u16 *)(v1 + 0x18e) [SPU KOFF high (voices 16-23)] */
	nop	/* load delay slot */
	ori	v0, v0, 0xff	/* v0 = v0 | 0xff */
	sh	v0, 0x18e(v1)	/* *(u16 *)(v1 + 0x18e) = v0 [SPU KOFF high (voices 16-23)] */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59e50	/* if (v0 == 0) goto $L59e50 */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59e10:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59e10	/* if (v0 != 0) goto $L59e10 */
	nop	/* delay slot: nop */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
$L59e50:
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59eb0	/* if (v0 == 0) goto $L59eb0 */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59e70:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59e70	/* if (v0 != 0) goto $L59e70 */
	nop	/* delay slot: nop */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
$L59eb0:
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59f10	/* if (v0 == 0) goto $L59f10 */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59ed0:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59ed0	/* if (v0 != 0) goto $L59ed0 */
	nop	/* delay slot: nop */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
$L59f10:
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x34(sp)	/* *(u32 *)(sp + 0x34) = v0 */
	sw	zero, 0x30(sp)	/* *(u32 *)(sp + 0x30) = zero */
	lw	v0, 0x30(sp)	/* v0 = *(u32 *)(sp + 0x30) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L59f70	/* if (v0 == 0) goto $L59f70 */
	addiu	a0, sp, 0x30	/* delay slot (real work): a0 = sp + 48 */
$L59f30:
	lw	v1, 4(a0)	/* v1 = *(u32 *)(a0 + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(a0)	/* *(u32 *)(a0 + 4) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L59f30	/* if (v0 != 0) goto $L59f30 */
	nop	/* delay slot: nop */

	/*
	 * Cold path done: v0 = 0 (the hot path arrives at 0x80059f74 with v0 already 0).
	 */
$L59f70:
	move	v0, zero	/* v0 = zero */

	/*
	 * Enable the SPU: a0 = _spu_RXX; _spu_inTransfer = 1; SPUCNT = 0xc000 (bit 15 enable + bit 14 unmute, everything
	 * else off); _spu_transferCallback = 0; _spu_IRQCallback = 0; restore s0/s1/ra and return v0 (= 0).
	 */
$L59f74:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	ori	v1, zero, 1	/* v1 = 1 */
	sw	v1, _spu_inTransfer	/* *(u32 *)&_spu_inTransfer = v1 (lui AT + sw macro, through AT) */
	ori	v1, zero, 0xc000	/* v1 = 0xc000 */
	sh	v1, 0x1aa(a0)	/* *(u16 *)(a0 + 0x1aa) = v1 [SPU SPUCNT (control)] */
	sw	zero, _spu_transferCallback	/* *(u32 *)&_spu_transferCallback = zero (lui AT + sw macro, through AT) */
	sw	zero, _spu_IRQCallback	/* *(u32 *)&_spu_IRQCallback = zero (lui AT + sw macro, through AT) */
	lw	ra, 0x40(sp)	/* ra = *(u32 *)(sp + 0x40) */
	lw	s1, 0x3c(sp)	/* s1 = *(u32 *)(sp + 0x3c) */
	lw	s0, 0x38(sp)	/* s0 = *(u32 *)(sp + 0x38) */
	addiu	sp, sp, 0x48	/* sp = sp + 72 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * _spu_close - detach the library's DMA callbacks
 *
 * Purpose:     int _spu_close(void). Clears the transfer-finished and IRQ callbacks and removes the DMA
 *              interrupt hook (_SpuDataCallback(0)). Present in the Runtime Library 3.0 archive
 *              under this name (SPU+0x2c0, identical code).
 * Inputs:      none.
 * Outputs:     v0 = 1.
 * Clobbers:    v0, a0 (ra saved).
 * Writes:      _spu_transferCallback = 0, _spu_IRQCallback = 0.
 * Callers:     none in the retail image.
 * Callees:     _SpuDataCallback.
 */
	.globl	_spu_close
_spu_close:

	/*
	 * Prologue (frame 0x18, ra); _spu_transferCallback = 0; _spu_IRQCallback = 0; _SpuDataCallback(0) clears the DMA
	 * interrupt hook; return 1.
	 */
	addiu	sp, sp, -0x18	/* sp = sp - 24 */
	sw	ra, 0x10(sp)	/* *(u32 *)(sp + 0x10) = ra */
	sw	zero, _spu_transferCallback	/* *(u32 *)&_spu_transferCallback = zero (lui AT + sw macro, through AT) */
	sw	zero, _spu_IRQCallback	/* *(u32 *)&_spu_IRQCallback = zero (lui AT + sw macro, through AT) */
	jal	_SpuDataCallback	/* call _SpuDataCallback */
	move	a0, zero	/* delay slot (real work): a0 = zero */
	ori	v0, zero, 1	/* v0 = 1 */
	lw	ra, 0x10(sp)	/* ra = *(u32 *)(sp + 0x10) */
	addiu	sp, sp, 0x18	/* sp = sp + 24 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * _spu_open - hook the SPU DMA completion handler
 *
 * Purpose:     int _spu_open(void). Installs _spu_FiDMA as the SPU DMA completion callback
 *              (_SpuDataCallback(_spu_FiDMA)). Present in the Runtime Library 3.0 archive under this
 *              name (SPU+0x2e8, identical code).
 * Inputs:      none.
 * Outputs:     v0 = 3.
 * Clobbers:    v0, a0 (ra saved).
 * Callers:     SpuStart module code (jal at 0x8005c604).
 * Callees:     _SpuDataCallback.
 */
	.globl	_spu_open
_spu_open:

	/*
	 * Prologue; a0 = &_spu_FiDMA (lui+addiu pair; hi is 0x8006 because the low half is negative); _SpuDataCallback(a0);
	 * return 3.
	 */
	addiu	sp, sp, -0x18	/* sp = sp - 24 */
	sw	ra, 0x10(sp)	/* *(u32 *)(sp + 0x10) = ra */
	la	a0, _spu_FiDMA	/* a0 = &_spu_FiDMA (lui+addiu macro) */
	jal	_SpuDataCallback	/* call _SpuDataCallback */
	nop	/* delay slot: nop */
	ori	v0, zero, 3	/* v0 = 3 */
	lw	ra, 0x10(sp)	/* ra = *(u32 *)(sp + 0x10) */
	addiu	sp, sp, 0x18	/* sp = sp + 24 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * f_5a018 - write a block to sound RAM through the SPU FIFO (manual, non-DMA transfer)
 *
 * Purpose:     void f_5a018(const u16 *src, unsigned len). Sets the SPU transfer address register from
 *              _spu_tsa and then pushes len bytes through the 32-halfword data FIFO in chunks of up
 *              to 0x40 bytes: copy the chunk into the FIFO, switch SPUCNT to transfer mode 1
 *              (manual write), wait for the SPU to finish, and repeat. When all is written, SPUCNT's
 *              transfer mode is cleared and the routine waits until SPUSTAT shows the same mode bits it
 *              had on entry. Every wait is bounded by 5000 polls and reports through printf on timeout.
 *              The first four instructions (5a018..5a024) load _spu_RXX and _spu_tsa ahead of the stack
 *              frame setup, so the label is 16 bytes before the 0x38-byte frame allocation at 0x5a028.
 * Inputs:      a0 = src (RAM halfword pointer), a1 = len in bytes.
 * Outputs:     none.
 * Clobbers:    v0, v1, a0, a1 (s0-s4, ra saved). Frame of 0x38 bytes; delay-loop words at sp+0x10/0x14.
 * Hardware:    SPUSTAT, TSA, FIFO (0x1a8), SPUCNT through _spu_RXX.
 * Reads:       _spu_RXX, _spu_tsa.
 * Writes:      g_73c88 (poll counter).
 * Callers:     _spu_init (jal 0x8005a018 at 0x80059bdc), _spu_write (0x8005aa80).
 * Callees:     printf (timeouts).
 * Registers:   s1 = remaining length, s2 = source pointer, s0 = chunk size, s3 = 0xd (delay seed),
 *              s4 = SPUSTAT & 0x7ff at entry.
 */
	.ent	f_5a018	/* a static: .ent gives it a local symbol record */
f_5a018:

	/*
	 * v1 = _spu_RXX; v0 = _spu_tsa (halfword): hoisted above the frame setup. a0 = src, a1 = len on entry.
	 */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, _spu_tsa	/* v0 = (u16)_spu_tsa (lui+lhu macro) */

	/*
	 * Prologue: frame 0x38, save s0-s4 and ra; s1 = len, s2 = src.
	 */
	addiu	sp, sp, -0x38	/* sp = sp - 56 */
	sw	s1, 0x24(sp)	/* *(u32 *)(sp + 0x24) = s1 */
	move	s1, a1	/* s1 = a1 */
	sw	ra, 0x34(sp)	/* *(u32 *)(sp + 0x34) = ra */
	sw	s4, 0x30(sp)	/* *(u32 *)(sp + 0x30) = s4 */
	sw	s3, 0x2c(sp)	/* *(u32 *)(sp + 0x2c) = s3 */
	sw	s2, 0x28(sp)	/* *(u32 *)(sp + 0x28) = s2 */
	sw	s0, 0x20(sp)	/* *(u32 *)(sp + 0x20) = s0 */

	/*
	 * a1 = SPUSTAT (its low 11 bits are kept in s4 for the final wait); TSA (0x1a6) = _spu_tsa: the SPU latches the
	 * transfer start address. Then one settle delay.
	 */
	lhu	a1, 0x1ae(v1)	/* a1 = *(u16 *)(v1 + 0x1ae) [SPU SPUSTAT (status)] */
	move	s2, a0	/* s2 = a0 */
	sh	v0, 0x1a6(v1)	/* *(u16 *)(v1 + 0x1a6) = v0 [SPU TSA (sound RAM transfer address)] */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	sw	zero, 0x10(sp)	/* *(u32 *)(sp + 0x10) = zero */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L5a0b4	/* if (v0 == 0) goto $L5a0b4 */
	andi	s4, a1, 0x7ff	/* delay slot (real work): s4 = a1 & 0x7ff */
$L5a074:
	lw	v1, 0x14(sp)	/* v1 = *(u32 *)(sp + 0x14) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a074	/* if (v0 != 0) goto $L5a074 */
	nop	/* delay slot: nop */

	/*
	 * len == 0: nothing to write, go to the final wait.
	 */
$L5a0b4:
	beq	s1, zero, $L5a29c	/* if (s1 == 0) goto $L5a29c */
	sltiu	v0, s1, 0x41	/* delay slot (real work): v0 = ((u32)s1 < 65) */

	/*
	 * Next chunk: s3 = 0xd (delay seed); s0 = len, clamped to 0x40 bytes (the FIFO holds 32 halfwords).
	 */
	ori	s3, zero, 0xd	/* s3 = 0xd */
$L5a0c0:
	bne	v0, zero, $L5a0cc	/* if (v0 != 0) goto $L5a0cc */
	move	s0, s1	/* delay slot (real work): s0 = s1 */
	ori	s0, zero, 0x40	/* s0 = 0x40 */

	/*
	 * Chunk copy loop: for (v1 = 0; v1 < s0; v1 += 2) FIFO (0x1a8) = *src++ (halfword); a0 is reloaded from _spu_RXX
	 * every pass (the compiler did not hoist the volatile pointer).
	 */
$L5a0cc:
	blez	s0, $L5a0f8	/* if (s0 <= 0) goto $L5a0f8 */
	move	v1, zero	/* delay slot (real work): v1 = zero */
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
$L5a0dc:
	lhu	v0, 0(s2)	/* v0 = *(u16 *)(s2) */
	addiu	s2, s2, 2	/* s2 = s2 + 2 */
	addiu	v1, v1, 2	/* v1 = v1 + 2 */
	sh	v0, 0x1a8(a0)	/* *(u16 *)(a0 + 0x1a8) = v0 [SPU FIFO (manual-write data port)] */
	slt	v0, v1, s0	/* v0 = v1 < s0 */
	bne	v0, zero, $L5a0dc	/* if (v0 != 0) goto $L5a0dc */
	nop	/* delay slot: nop */

	/*
	 * SPUCNT = (SPUCNT & ~0x30) | 0x10: transfer mode 1 (manual write) starts moving the FIFO into sound RAM.
	 */
$L5a0f8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1aa(v1)	/* a0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, a0, 0xffcf	/* v0 = a0 & 0xffcf */
	ori	a0, v0, 0x10	/* a0 = v0 | 0x10 */
	sh	a0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = a0 [SPU SPUCNT (control)] */

	/*
	 * Delay with the seed in s3 (the delay-slot-free jump to the loop test is the compiler's loop inversion).
	 */
	sw	s3, 0x14(sp)	/* *(u32 *)(sp + 0x14) = s3 */
	sw	zero, 0x10(sp)	/* *(u32 *)(sp + 0x10) = zero */
	j	$L5a150	/* goto $L5a150 */
	nop	/* delay slot: nop */
$L5a128:
	lw	v1, 0x14(sp)	/* v1 = *(u32 *)(sp + 0x14) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
$L5a150:
	nop	/* load delay slot */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a128	/* if (v0 != 0) goto $L5a128 */
	nop	/* delay slot: nop */

	/*
	 * g_73c88 = 0; wait while SPUSTAT bit 10 (0x400, data transfer busy) is set. Each pass counts g_73c88; after
	 * 0x1388 polls printf("SPU:T/O [%s]\n", "wait (wrdy H -> L)") and stop waiting.
	 */
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1ae(v0)	/* v0 = *(u16 *)(v0 + 0x1ae) [SPU SPUSTAT (status)] */
	sw	zero, g_73c88	/* *(u32 *)&g_73c88 = zero (lui AT + sw macro, through AT) */
	andi	v0, v0, 0x400	/* v0 = v0 & 0x400 */
	beq	v0, zero, $L5a1f0	/* if (v0 == 0) goto $L5a1f0 */
	nop	/* delay slot: nop */
$L5a18c:
	lw	v0, g_73c88	/* v0 = g_73c88 (lui+lw macro) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, g_73c88	/* *(u32 *)&g_73c88 = v0 (lui AT + sw macro, through AT) */
	slti	v0, v0, 0x1389	/* v0 = (v0 < 5001) */
	bne	v0, zero, $L5a1d0	/* if (v0 != 0) goto $L5a1d0 */
	nop	/* delay slot: nop */
	la	a0, s_spu_timeout_fmt	/* a0 = &s_spu_timeout_fmt (lui+addiu macro) */
	la	a1, s_wait_wrdy	/* a1 = &s_wait_wrdy (lui+addiu macro) */
	jal	printf	/* call printf */
	nop	/* delay slot: nop */
	j	$L5a1f0	/* goto $L5a1f0 */
	nop	/* delay slot: nop */
$L5a1d0:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1ae(v0)	/* v0 = *(u16 *)(v0 + 0x1ae) [SPU SPUSTAT (status)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x400	/* v0 = v0 & 0x400 */
	bne	v0, zero, $L5a18c	/* if (v0 != 0) goto $L5a18c */
	nop	/* delay slot: nop */

	/*
	 * Two more settle delays after the chunk.
	 */
$L5a1f0:
	sw	s3, 0x14(sp)	/* *(u32 *)(sp + 0x14) = s3 */
	sw	zero, 0x10(sp)	/* *(u32 *)(sp + 0x10) = zero */
	j	$L5a228	/* goto $L5a228 */
	nop	/* delay slot: nop */
$L5a200:
	lw	v1, 0x14(sp)	/* v1 = *(u32 *)(sp + 0x14) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
$L5a228:
	nop	/* load delay slot */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a200	/* if (v0 != 0) goto $L5a200 */
	nop	/* delay slot: nop */

	/*
	 * Settle delay as above, but the seed w = 13 comes from s3 instead of a constant: 240 passes of
	 * w = w * 3 on the stack words at sp+0x10 (i) and sp+0x14 (w).
	 */
	sw	s3, 0x14(sp)	/* *(u32 *)(sp + 0x14) = s3 */
	sw	zero, 0x10(sp)	/* *(u32 *)(sp + 0x10) = zero */
	j	$L5a278	/* goto $L5a278 */
	nop	/* delay slot: nop */
$L5a250:
	lw	v1, 0x14(sp)	/* v1 = *(u32 *)(sp + 0x14) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
$L5a278:
	nop	/* load delay slot */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a250	/* if (v0 != 0) goto $L5a250 */
	nop	/* delay slot: nop */

	/*
	 * len -= chunk (s0); if bytes remain, start the next chunk (the sltiu in the delay slot pre-computes len < 0x41).
	 */
	subu	s1, s1, s0	/* s1 = s1 - s0 */
	bne	s1, zero, $L5a0c0	/* if (s1 != 0) goto $L5a0c0 */
	sltiu	v0, s1, 0x41	/* delay slot (real work): v0 = ((u32)s1 < 65) */

	/*
	 * All chunks written: SPUCNT &= ~0x30 (transfer mode off); a1 = s4 & 0xffff (SPUSTAT bits saved at entry);
	 * g_73c88 = 0; wait until SPUSTAT & 0x7ff equals a1 again, with the same 5000-poll bound and printf
	 * ("SPU:T/O [%s]\n", "wait (dmaf clear/W)") on timeout.
	 */
$L5a29c:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1aa(v0)	/* a0 = *(u16 *)(v0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	a0, a0, 0xffcf	/* a0 = a0 & 0xffcf */
	sh	a0, 0x1aa(v0)	/* *(u16 *)(v0 + 0x1aa) = a0 [SPU SPUCNT (control)] */
	lhu	v0, 0x1ae(v0)	/* v0 = *(u16 *)(v0 + 0x1ae) [SPU SPUSTAT (status)] */
	andi	a1, s4, 0xffff	/* a1 = s4 & 0xffff */
	sw	zero, g_73c88	/* *(u32 *)&g_73c88 = zero (lui AT + sw macro, through AT) */
	andi	v0, v0, 0x7ff	/* v0 = v0 & 0x7ff */
	beq	v0, a1, $L5a338	/* if (v0 == a1) goto $L5a338 */
	nop	/* delay slot: nop */
$L5a2d4:
	lw	v0, g_73c88	/* v0 = g_73c88 (lui+lw macro) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, g_73c88	/* *(u32 *)&g_73c88 = v0 (lui AT + sw macro, through AT) */
	slti	v0, v0, 0x1389	/* v0 = (v0 < 5001) */
	bne	v0, zero, $L5a318	/* if (v0 != 0) goto $L5a318 */
	nop	/* delay slot: nop */
	la	a0, s_spu_timeout_fmt	/* a0 = &s_spu_timeout_fmt (lui+addiu macro) */
	la	a1, s_wait_dmaf	/* a1 = &s_wait_dmaf (lui+addiu macro) */
	jal	printf	/* call printf */
	nop	/* delay slot: nop */
	j	$L5a338	/* goto $L5a338 */
	nop	/* delay slot: nop */
$L5a318:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1ae(v0)	/* v0 = *(u16 *)(v0 + 0x1ae) [SPU SPUSTAT (status)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x7ff	/* v0 = v0 & 0x7ff */
	bne	v0, a1, $L5a2d4	/* if (v0 != a1) goto $L5a2d4 */
	nop	/* delay slot: nop */

	/*
	 * Epilogue: restore ra, s0-s4 and the frame.
	 */
$L5a338:
	lw	ra, 0x34(sp)	/* ra = *(u32 *)(sp + 0x34) */
	lw	s4, 0x30(sp)	/* s4 = *(u32 *)(sp + 0x30) */
	lw	s3, 0x2c(sp)	/* s3 = *(u32 *)(sp + 0x2c) */
	lw	s2, 0x28(sp)	/* s2 = *(u32 *)(sp + 0x28) */
	lw	s1, 0x24(sp)	/* s1 = *(u32 *)(sp + 0x24) */
	lw	s0, 0x20(sp)	/* s0 = *(u32 *)(sp + 0x20) */
	addiu	sp, sp, 0x38	/* sp = sp + 56 */
	jr	ra	/* return */
	nop	/* delay slot: nop */
	.end	f_5a018

/*
 * _spu_FiDMA - SPU DMA finished handler (installed by _spu_open)
 *
 * Purpose:     void _spu_FiDMA(void). Called from the DMA interrupt when channel 4 completes. If the
 *              transfer was a write to the SPU (g_73cd8 == 0) it first waits three settle delays for
 *              the FIFO to drain. It then clears SPUCNT's transfer mode, waits (bounded) until the mode
 *              bits read back as zero, clears the "DMA in progress" flag g_73d50 and calls the user's
 *              transfer-finished callback _spu_transferCallback if one is set.
 * Inputs:      none.
 * Outputs:     none.
 * Clobbers:    v0, v1, a0 (ra saved). Frame of 0x20 bytes; delay-loop words at sp+0x10/0x14.
 * Hardware:    SPUCNT through _spu_RXX.
 * Reads:       g_73cd8 (direction flag: 1 = SPU to RAM), _spu_transferCallback.
 * Writes:      g_73d50 = 0.
 * Callers:     the DMA completion interrupt, through _SpuDataCallback; address passed by _spu_open.
 * Callees:     the registered _spu_transferCallback (indirect).
 */
	.globl	_spu_FiDMA
_spu_FiDMA:

	/*
	 * v0 = g_73cd8 (1 = the finished transfer was SPU to RAM). Prologue (frame 0x20, ra). If it was a write (flag 0)
	 * run three settle delays; a read (flag 1) skips them.
	 */
	lw	v0, g_73cd8	/* v0 = g_73cd8 (lui+lw macro) */
	addiu	sp, sp, -0x20	/* sp = sp - 32 */
	bne	v0, zero, $L5a464	/* if (v0 != 0) goto $L5a464 */
	sw	ra, 0x18(sp)	/* delay slot (real work): *(u32 *)(sp + 0x18) = ra */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	sw	zero, 0x10(sp)	/* *(u32 *)(sp + 0x10) = zero */
	j	$L5a3ac	/* goto $L5a3ac */
	nop	/* delay slot: nop */
$L5a384:
	lw	v1, 0x14(sp)	/* v1 = *(u32 *)(sp + 0x14) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
$L5a3ac:
	nop	/* load delay slot */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a384	/* if (v0 != 0) goto $L5a384 */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* delay slot (real work): v0 = 0xd */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	sw	zero, 0x10(sp)	/* *(u32 *)(sp + 0x10) = zero */
	j	$L5a3fc	/* goto $L5a3fc */
	nop	/* delay slot: nop */
$L5a3d4:
	lw	v1, 0x14(sp)	/* v1 = *(u32 *)(sp + 0x14) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
$L5a3fc:
	nop	/* load delay slot */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a3d4	/* if (v0 != 0) goto $L5a3d4 */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* delay slot (real work): v0 = 0xd */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	sw	zero, 0x10(sp)	/* *(u32 *)(sp + 0x10) = zero */
	j	$L5a44c	/* goto $L5a44c */
	nop	/* delay slot: nop */
$L5a424:
	lw	v1, 0x14(sp)	/* v1 = *(u32 *)(sp + 0x14) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 0x14(sp)	/* *(u32 *)(sp + 0x14) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = v0 */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
$L5a44c:
	nop	/* load delay slot */
	lw	v0, 0x10(sp)	/* v0 = *(u32 *)(sp + 0x10) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a424	/* if (v0 != 0) goto $L5a424 */
	nop	/* delay slot: nop */

	/*
	 * a0 = _spu_RXX; SPUCNT &= ~0x30 (transfer mode off). Then wait up to 0xf01 reads of SPUCNT & 0x30 for the mode bits to
	 * clear (v1 counts the polls; the trailing addiu -1 is the compiler's count adjustment on loop exit).
	 */
$L5a464:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(a0)	/* v0 = *(u16 *)(a0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0xffcf	/* v0 = v0 & 0xffcf */
	sh	v0, 0x1aa(a0)	/* *(u16 *)(a0 + 0x1aa) = v0 [SPU SPUCNT (control)] */
	lhu	v0, 0x1aa(a0)	/* v0 = *(u16 *)(a0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x30	/* v0 = v0 & 0x30 */
	beq	v0, zero, $L5a4bc	/* if (v0 == 0) goto $L5a4bc */
	move	v1, zero	/* delay slot (real work): v1 = zero */
	addiu	v1, v1, 1	/* v1 = v1 + 1 */
$L5a498:
	slti	v0, v1, 0xf01	/* v0 = (v1 < 3841) */
	beq	v0, zero, $L5a4bc	/* if (v0 == 0) goto $L5a4bc */
	nop	/* delay slot: nop */
	lhu	v0, 0x1aa(a0)	/* v0 = *(u16 *)(a0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x30	/* v0 = v0 & 0x30 */
	bne	v0, zero, $L5a498	/* if (v0 != 0) goto $L5a498 */
	addiu	v1, v1, 1	/* delay slot (real work): v1 = v1 + 1 */
	addiu	v1, v1, -1	/* v1 = v1 - 1 */

	/*
	 * g_73d50 = 0 (DMA no longer in progress); if _spu_transferCallback is set, call it (jalr; the pointer is
	 * reloaded for the call). Epilogue.
	 */
$L5a4bc:
	sw	zero, g_73d50	/* *(u32 *)&g_73d50 = zero (lui AT + sw macro, through AT) */
	lw	v0, _spu_transferCallback	/* v0 = _spu_transferCallback (lui+lw macro) */
	nop	/* load delay slot */
	beq	v0, zero, $L5a4ec	/* if (v0 == 0) goto $L5a4ec */
	nop	/* delay slot: nop */
	lw	v0, _spu_transferCallback	/* v0 = _spu_transferCallback (lui+lw macro) */
	nop	/* load delay slot */
	jalr	v0	/* call *v0 */
	nop	/* delay slot: nop */
$L5a4ec:
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(sp + 0x18) */
	addiu	sp, sp, 0x20	/* sp = sp + 32 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * _spu_r_ - start a DMA read from sound RAM into main RAM
 *
 * Purpose:     void _spu_r_(void *dest, int spu_addr, int blocks). Sets the SPU transfer address, selects
 *              transfer mode 3 (DMA read) in SPUCNT, programs the SPU timing register, enables DMA
 *              channel 4 and its interrupt, and starts the channel: MADR = dest, BCR = (blocks << 16) |
 *              0x10 (blocks of 16 words), CHCR = 0x01000200 (device to RAM, block sync, start). Returns
 *              immediately; completion arrives through _spu_FiDMA. Used by the ioctl codes that read
 *              the decoded-data capture area (0x210, 0x212, 0x213).
 * Inputs:      a0 = dest (RAM), a1 = sound RAM address in 8-byte units (TSA value), a2 = block count.
 * Outputs:     none.
 * Clobbers:    v0, v1, a0, a1, a3. Frame of 8 bytes for the delay-loop words (sp+0, sp+4).
 * Hardware:    TSA, SPUCNT through _spu_RXX; D4_MADR / D4_BCR / D4_CHCR, DPCR, DICR, SPU_DELAY through
 *              the pointer words g_73c94..g_73ca8.
 * Writes:      g_73cd8 = 1 (direction: SPU to RAM), g_73d50 = 1 (DMA in progress).
 * Callers:     _spu_ioctl (0x8005b3a4).
 * Callees:     none.
 * Registers:   a3 = dest (copied from a0 first), a1 reused for the CHCR command word.
 */
	.globl	_spu_r_
_spu_r_:

	/*
	 * v0 = _spu_RXX; TSA (0x1a6) = a1 (start address, 8-byte units); a3 = a0 = dest. Two settle delays follow.
	 */
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	addiu	sp, sp, -8	/* sp = sp - 8 */
	sh	a1, 0x1a6(v0)	/* *(u16 *)(v0 + 0x1a6) = a1 [SPU TSA (sound RAM transfer address)] */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	sw	zero, 0(sp)	/* *(u32 *)(sp) = zero */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	beq	v0, zero, $L5a56c	/* if (v0 == 0) goto $L5a56c */
	move	a3, a0	/* delay slot (real work): a3 = a0 */
$L5a52c:
	lw	v1, 4(sp)	/* v1 = *(u32 *)(sp + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(sp)	/* *(u32 *)(sp) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a52c	/* if (v0 != 0) goto $L5a52c */
	nop	/* delay slot: nop */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
$L5a56c:
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	sw	zero, 0(sp)	/* *(u32 *)(sp) = zero */
	j	$L5a5a8	/* goto $L5a5a8 */
	nop	/* delay slot: nop */
$L5a580:
	lw	v1, 4(sp)	/* v1 = *(u32 *)(sp + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(sp)	/* *(u32 *)(sp) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
$L5a5a8:
	nop	/* load delay slot */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a580	/* if (v0 != 0) goto $L5a580 */
	nop	/* delay slot: nop */

	/*
	 * SPUCNT |= 0x30: transfer mode 3, DMA read. Two more settle delays.
	 */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	ori	v0, v0, 0x30	/* v0 = v0 | 0x30 */
	sh	v0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = v0 [SPU SPUCNT (control)] */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* v0 = 0xd */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	sw	zero, 0(sp)	/* *(u32 *)(sp) = zero */
	j	$L5a618	/* goto $L5a618 */
	nop	/* delay slot: nop */
$L5a5f0:
	lw	v1, 4(sp)	/* v1 = *(u32 *)(sp + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(sp)	/* *(u32 *)(sp) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
$L5a618:
	nop	/* load delay slot */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a5f0	/* if (v0 != 0) goto $L5a5f0 */

	/*
	 * Settle delay (the library's software wait, a volatile busy loop): two stack words, i = 0 and w = 13, and 240 passes of
	 * w = w * 3 (sll + addu) with i++; i and w are reloaded from the stack on every pass, as for volatile locals.
	 */
	ori	v0, zero, 0xd	/* delay slot (real work): v0 = 0xd */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	sw	zero, 0(sp)	/* *(u32 *)(sp) = zero */
	j	$L5a668	/* goto $L5a668 */
	nop	/* delay slot: nop */
$L5a640:
	lw	v1, 4(sp)	/* v1 = *(u32 *)(sp + 4) */
	nop	/* load delay slot */
	sll	v0, v1, 1	/* v0 = v1 << 1 */
	addu	v0, v0, v1	/* v0 = v0 + v1 */
	sw	v0, 4(sp)	/* *(u32 *)(sp + 4) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, 0(sp)	/* *(u32 *)(sp) = v0 */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
$L5a668:
	nop	/* load delay slot */
	lw	v0, 0(sp)	/* v0 = *(u32 *)(sp) */
	nop	/* load delay slot */
	slti	v0, v0, 0xf0	/* v0 = (v0 < 240) */
	bne	v0, zero, $L5a640	/* if (v0 != 0) goto $L5a640 */

	/*
	 * SPU_DELAY (0x1f801014, via g_73ca8) = (SPU_DELAY & 0xf0ffffff) | 0x22000000: bits 27-24 cleared, bits 29 and 25 set
	 * (the DMA-read access timing). v1 = 0xf0ff.. mask built with lui/ori.
	 */
	lui	v1, 0xf0ff	/* delay slot (real work): v1 = 0xf0ff << 16 */
	lw	a0, g_73ca8	/* a0 = g_73ca8 (lui+lw macro) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	ori	v1, v1, 0xffff	/* v1 = v1 | 0xffff */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	lui	v1, 0x2200	/* v1 = 0x2200 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */

	/*
	 * DICR (via g_73ca4) |= 0x00900000: channel 4 interrupt enable (bit 20) and the master IRQ enable (bit 23).
	 */
	lw	a0, g_73ca4	/* a0 = g_73ca4 (lui+lw macro) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	lui	v1, 0x90	/* v1 = 0x90 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */

	/*
	 * DPCR (via g_73ca0) |= 0x00080000: channel 4 enabled. a1 = 0x01000000 for the start command.
	 */
	lw	a0, g_73ca0	/* a0 = g_73ca0 (lui+lw macro) */
	lui	a1, 0x100	/* a1 = 0x100 << 16 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	lui	v1, 8	/* v1 = 8 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */

	/*
	 * D4_MADR = a3 (destination); a1 = 0x01000200 (start, block sync, device to RAM); D4_BCR = (blocks << 16) | 0x10;
	 * g_73cd8 = 1 (read) and g_73d50 = 1 (in progress); D4_CHCR = a1 starts the transfer. Epilogue.
	 */
	lw	v0, g_73c94	/* v0 = g_73c94 (lui+lw macro) */
	ori	a1, a1, 0x200	/* a1 = a1 | 0x200 */
	sw	a3, 0(v0)	/* *(u32 *)(v0) = a3 */
	sll	v0, a2, 16	/* v0 = a2 << 16 */
	lw	v1, g_73c98	/* v1 = g_73c98 (lui+lw macro) */
	ori	v0, v0, 0x10	/* v0 = v0 | 0x10 */
	sw	v0, 0(v1)	/* *(u32 *)(v1) = v0 */
	lw	v1, g_73c9c	/* v1 = g_73c9c (lui+lw macro) */
	ori	v0, zero, 1	/* v0 = 1 */
	sw	v0, g_73cd8	/* *(u32 *)&g_73cd8 = v0 (lui AT + sw macro, through AT) */
	sw	v0, g_73d50	/* *(u32 *)&g_73d50 = v0 (lui AT + sw macro, through AT) */
	sw	a1, 0(v1)	/* *(u32 *)(v1) = a1 */
	addiu	sp, sp, 8	/* sp = sp + 8 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * _spu_t - SPU DMA transfer control (variable arguments)
 *
 * Purpose:     int _spu_t(int op, ...). A small command interpreter for the SPU DMA transfer; op is
 *              the first argument, the remaining arguments follow it in the caller's argument save
 *              area (the routine spills a0..a3 to the stack and takes a2 = &arg1, a varargs idiom).
 *              op 0   begin a read transfer: g_73cd8 = 1, wait for TSA to equal _spu_tsa, SPUCNT
 *                     transfer mode = 3 (DMA read).
 *              op 1   begin a write transfer: g_73cd8 = 0, wait for TSA to equal _spu_tsa, SPUCNT
 *                     transfer mode = 2 (DMA write).
 *              op 2   set the transfer address: arg1 = byte address; _spu_tsa = TSA = arg1 >>
 *                     _spu_mem_mode_plus.
 *              op 3   start the DMA: arg1 = RAM address, arg2 = byte count. Waits for SPUCNT's mode
 *                     bits to match the direction (0x30 for a read, 0x20 for a write), programs the
 *                     SPU_DELAY / DPCR / DICR registers, computes the block count (size / 64, rounded
 *                     up), loads D4_MADR / D4_BCR and sets D4_CHCR to start (0x01000200 for a read,
 *                     0x01000201 for a write). g_73cdc and g_73ce0 keep the address and block count.
 * Inputs:      a0 = op, a1 = arg1, a2 = arg2 (also stored for the varargs access).
 * Outputs:     v0 = 0, or -2 when a wait timed out (the poll counter v1 passed 0xf00).
 * Clobbers:    v0, v1, a0, a1, a2, a3. No stack frame beyond the argument save area at sp+0..0xf of the
 *              caller's frame.
 * Hardware:    TSA, SPUCNT through _spu_RXX; D4_MADR / D4_BCR / D4_CHCR, DPCR, DICR, SPU_DELAY through
 *              g_73c94..g_73ca8.
 * Reads:       _spu_tsa, _spu_mem_mode_plus, g_73cd8.
 * Writes:      g_73cd8, g_73cdc, g_73ce0, g_73d50 = 1 (DMA in progress), _spu_tsa.
 * Callers:     _spu_write (ops 2, 1, 3), _spu_read (ops 2, 0, 3), and library modules at 0x8005c450,
 *              0x8005c458, 0x8005c46c.
 * Callees:     none.
 */
	.globl	_spu_t
_spu_t:

	/*
	 * Spill a0..a3 into the caller's argument area (sp+0..0xc) and take a2 = &arg1 (sp+4): the varargs idiom. a3 = 1.
	 * Dispatch on op: 1 -> write setup, 0 -> read setup, 2 -> set address, 3 -> start DMA, anything else -> return 0.
	 */
	sw	a2, 8(sp)	/* *(u32 *)(sp + 8) = a2 */
	addiu	a2, sp, 4	/* a2 = sp + 4 */
	sw	a3, 0xc(sp)	/* *(u32 *)(sp + 0xc) = a3 */
	ori	a3, zero, 1	/* a3 = 1 */
	sw	a0, 0(sp)	/* *(u32 *)(sp) = a0 */
	sw	a1, 4(sp)	/* *(u32 *)(sp + 4) = a1 */
	beq	a0, a3, $L5a7b0	/* if (a0 == a3) goto $L5a7b0 */
	sw	a0, 0(sp)	/* delay slot (real work): *(u32 *)(sp) = a0 */
	slti	v0, a0, 2	/* v0 = (a0 < 2) */
	beq	v0, zero, $L5a768	/* if (v0 == 0) goto $L5a768 */
	nop	/* delay slot: nop */
	beq	a0, zero, $L5a820	/* if (a0 == 0) goto $L5a820 */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$L5aa10	/* goto $L5aa10 */
	nop	/* delay slot: nop */
$L5a768:
	ori	v0, zero, 2	/* v0 = 2 */
	beq	a0, v0, $L5a784	/* if (a0 == v0) goto $L5a784 */
	ori	v0, zero, 3	/* delay slot (real work): v0 = 3 */
	beq	a0, v0, $L5a88c	/* if (a0 == v0) goto $L5a88c */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$L5aa10	/* goto $L5aa10 */
	nop	/* delay slot: nop */

	/*
	 * op 2: set transfer address. a0 = arg1 (byte address); v0 = a0 >> _spu_mem_mode_plus; _spu_tsa = v0 and TSA
	 * (0x1a6) = v0. Return 0.
	 */
$L5a784:
	lw	a0, 4(sp)	/* a0 = *(u32 *)(sp + 4) */
	lw	v0, _spu_mem_mode_plus	/* v0 = _spu_mem_mode_plus (lui+lw macro) */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	srlv	v0, a0, v0	/* v0 = a0 >> (logical) v0 */
	sh	v0, _spu_tsa	/* *(u16 *)&_spu_tsa = v0 (lui AT + sh macro, through AT) */
	sh	v0, 0x1a6(v1)	/* *(u16 *)(v1 + 0x1a6) = v0 [SPU TSA (sound RAM transfer address)] */
	j	$L5aa10	/* goto $L5aa10 */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * op 1: begin a write transfer. a1 = _spu_RXX, a0 = _spu_tsa; g_73cd8 = 0 (direction = RAM to SPU). Wait (up to
	 * 0xf01 polls, v1 counting) until TSA reads back equal to _spu_tsa; a timeout returns -2. Then SPUCNT =
	 * (SPUCNT & ~0x30) | 0x20: transfer mode 2, DMA write. Return 0.
	 */
$L5a7b0:
	lw	a1, _spu_RXX	/* a1 = _spu_RXX (lui+lw macro) */
	lhu	a0, _spu_tsa	/* a0 = (u16)_spu_tsa (lui+lhu macro) */
	lhu	v0, 0x1a6(a1)	/* v0 = *(u16 *)(a1 + 0x1a6) [SPU TSA (sound RAM transfer address)] */
	sw	zero, g_73cd8	/* *(u32 *)&g_73cd8 = zero (lui AT + sw macro, through AT) */
	andi	v0, v0, 0xffff	/* v0 = v0 & 0xffff */
	beq	v0, a0, $L5a7f8	/* if (v0 == a0) goto $L5a7f8 */
	move	v1, zero	/* delay slot (real work): v1 = zero */
	addiu	v1, v1, 1	/* v1 = v1 + 1 */
$L5a7dc:
	slti	v0, v1, 0xf01	/* v0 = (v1 < 3841) */
	beq	v0, zero, $L5aa10	/* if (v0 == 0) goto $L5aa10 */
	addiu	v0, zero, -2	/* delay slot (real work): v0 = -2 */
	lhu	v0, 0x1a6(a1)	/* v0 = *(u16 *)(a1 + 0x1a6) [SPU TSA (sound RAM transfer address)] */
	nop	/* load delay slot */
	bne	v0, a0, $L5a7dc	/* if (v0 != a0) goto $L5a7dc */
	addiu	v1, v1, 1	/* delay slot (real work): v1 = v1 + 1 */
$L5a7f8:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1aa(v1)	/* a0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, a0, 0xffcf	/* v0 = a0 & 0xffcf */
	ori	a0, v0, 0x20	/* a0 = v0 | 0x20 */
	sh	a0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = a0 [SPU SPUCNT (control)] */
	j	$L5aa10	/* goto $L5aa10 */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * op 0: begin a read transfer. Same as op 1 but g_73cd8 = 1 (a3 holds the constant 1) and SPUCNT |= 0x30 (mode 3,
	 * DMA read).
	 */
$L5a820:
	lw	a1, _spu_RXX	/* a1 = _spu_RXX (lui+lw macro) */
	lhu	a0, _spu_tsa	/* a0 = (u16)_spu_tsa (lui+lhu macro) */
	lhu	v0, 0x1a6(a1)	/* v0 = *(u16 *)(a1 + 0x1a6) [SPU TSA (sound RAM transfer address)] */
	sw	a3, g_73cd8	/* *(u32 *)&g_73cd8 = a3 (lui AT + sw macro, through AT) */
	andi	v0, v0, 0xffff	/* v0 = v0 & 0xffff */
	beq	v0, a0, $L5a868	/* if (v0 == a0) goto $L5a868 */
	move	v1, zero	/* delay slot (real work): v1 = zero */
	addiu	v1, v1, 1	/* v1 = v1 + 1 */
$L5a84c:
	slti	v0, v1, 0xf01	/* v0 = (v1 < 3841) */
	beq	v0, zero, $L5aa10	/* if (v0 == 0) goto $L5aa10 */
	addiu	v0, zero, -2	/* delay slot (real work): v0 = -2 */
	lhu	v0, 0x1a6(a1)	/* v0 = *(u16 *)(a1 + 0x1a6) [SPU TSA (sound RAM transfer address)] */
	nop	/* load delay slot */
	bne	v0, a0, $L5a84c	/* if (v0 != a0) goto $L5a84c */
	addiu	v1, v1, 1	/* delay slot (real work): v1 = v1 + 1 */
$L5a868:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1aa(v0)	/* a0 = *(u16 *)(v0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	ori	a0, a0, 0x30	/* a0 = a0 | 0x30 */
	sh	a0, 0x1aa(v0)	/* *(u16 *)(v0 + 0x1aa) = a0 [SPU SPUCNT (control)] */
	j	$L5aa10	/* goto $L5aa10 */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * op 3: start the DMA. a0 = 0x30 if g_73cd8 == 1 (read) else 0x20: the SPUCNT mode bits the previous op must have
	 * established. Wait (bounded, -2 on timeout) until SPUCNT & 0x30 equals it.
	 */
$L5a88c:
	lw	v0, g_73cd8	/* v0 = g_73cd8 (lui+lw macro) */
	nop	/* load delay slot */
	bne	v0, a3, $L5a8a4	/* if (v0 != a3) goto $L5a8a4 */
	ori	a0, zero, 0x20	/* delay slot (real work): a0 = 0x20 */
	ori	a0, zero, 0x30	/* a0 = 0x30 */
$L5a8a4:
	lw	a1, _spu_RXX	/* a1 = _spu_RXX (lui+lw macro) */
	move	v1, zero	/* v1 = zero */
	lhu	v0, 0x1aa(a1)	/* v0 = *(u16 *)(a1 + 0x1aa) [SPU SPUCNT (control)] */
	andi	a0, a0, 0xffff	/* a0 = a0 & 0xffff */
	andi	v0, v0, 0x30	/* v0 = v0 & 0x30 */
	beq	v0, a0, $L5a8e4	/* if (v0 == a0) goto $L5a8e4 */
	addiu	v1, v1, 1	/* delay slot (real work): v1 = v1 + 1 */
$L5a8c4:
	slti	v0, v1, 0xf01	/* v0 = (v1 < 3841) */
	beq	v0, zero, $L5aa10	/* if (v0 == 0) goto $L5aa10 */
	addiu	v0, zero, -2	/* delay slot (real work): v0 = -2 */
	lhu	v0, 0x1aa(a1)	/* v0 = *(u16 *)(a1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x30	/* v0 = v0 & 0x30 */
	bne	v0, a0, $L5a8c4	/* if (v0 != a0) goto $L5a8c4 */
	addiu	v1, v1, 1	/* delay slot (real work): v1 = v1 + 1 */

	/*
	 * SPU_DELAY (via g_73ca8) = (SPU_DELAY & 0xf0ffffff) | 0x22000000 for a read, | 0x20000000 for a write
	 * (access timing per direction).
	 */
$L5a8e4:
	lw	v1, g_73cd8	/* v1 = g_73cd8 (lui+lw macro) */
	ori	v0, zero, 1	/* v0 = 1 */
	bne	v1, v0, $L5a918	/* if (v1 != v0) goto $L5a918 */
	lui	v1, 0xf0ff	/* delay slot (real work): v1 = 0xf0ff << 16 */
	lw	a0, g_73ca8	/* a0 = g_73ca8 (lui+lw macro) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	ori	v1, v1, 0xffff	/* v1 = v1 | 0xffff */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	j	$L5a934	/* goto $L5a934 */
	lui	v1, 0x2200	/* delay slot (real work): v1 = 0x2200 << 16 */
$L5a918:
	lw	a0, g_73ca8	/* a0 = g_73ca8 (lui+lw macro) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	ori	v1, v1, 0xffff	/* v1 = v1 | 0xffff */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	lui	v1, 0x2000	/* v1 = 0x2000 << 16 */
$L5a934:
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */

	/*
	 * a2 += 4: a2 now points at arg2. g_73cdc = arg1 (RAM address); blocks = (size >> 6) + ((size & 0x3f) != 0), i.e. the
	 * size in 64-byte units rounded up; g_73ce0 = blocks.
	 */
	addiu	a2, a2, 4	/* a2 = a2 + 4 */
	lw	a0, -4(a2)	/* a0 = *(u32 *)(a2 - 4) */
	nop	/* load delay slot */
	sw	a0, g_73cdc	/* *(u32 *)&g_73cdc = a0 (lui AT + sw macro, through AT) */
	lw	a0, 0(a2)	/* a0 = *(u32 *)(a2) */
	nop	/* load delay slot */
	srl	v1, a0, 6	/* v1 = a0 >> (logical) 6 */
	andi	v0, a0, 0x3f	/* v0 = a0 & 0x3f */
	sltu	v0, zero, v0	/* v0 = (v0 != 0) */
	lw	a0, g_73ca4	/* a0 = g_73ca4 (lui+lw macro) */
	addu	v1, v1, v0	/* v1 = v1 + v0 */
	sw	v1, g_73ce0	/* *(u32 *)&g_73ce0 = v1 (lui AT + sw macro, through AT) */

	/*
	 * DICR |= 0x00900000 (channel 4 IRQ enable + master enable); DPCR |= 0x00080000 (channel 4 enable).
	 */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	lui	v1, 0x90	/* v1 = 0x90 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */
	lw	a0, g_73ca0	/* a0 = g_73ca0 (lui+lw macro) */
	nop	/* load delay slot */
	lw	v0, 0(a0)	/* v0 = *(u32 *)(a0) */
	lui	v1, 8	/* v1 = 8 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sw	v0, 0(a0)	/* *(u32 *)(a0) = v0 */

	/*
	 * a0 = 0x01000201 (start, block sync, RAM to device). D4_MADR = g_73cdc; D4_BCR = (g_73ce0 << 16) | 0x10; if g_73cd8 == 1
	 * (read) a0 = 0x01000200 instead. g_73d50 = 1; D4_CHCR = a0 starts the transfer. Return 0.
	 */
	lui	a0, 0x100	/* a0 = 0x100 << 16 */
	lw	v1, g_73c94	/* v1 = g_73c94 (lui+lw macro) */
	lw	v0, g_73cdc	/* v0 = g_73cdc (lui+lw macro) */
	ori	a0, a0, 0x201	/* a0 = a0 | 0x201 */
	sw	v0, 0(v1)	/* *(u32 *)(v1) = v0 */
	lw	v0, g_73ce0	/* v0 = g_73ce0 (lui+lw macro) */
	lw	v1, g_73c98	/* v1 = g_73c98 (lui+lw macro) */
	sll	v0, v0, 16	/* v0 = v0 << 16 */
	ori	v0, v0, 0x10	/* v0 = v0 | 0x10 */
	sw	v0, 0(v1)	/* *(u32 *)(v1) = v0 */
	lw	v0, g_73cd8	/* v0 = g_73cd8 (lui+lw macro) */
	ori	v1, zero, 1	/* v1 = 1 */
	bne	v0, v1, $L5a9f8	/* if (v0 != v1) goto $L5a9f8 */
	nop	/* delay slot: nop */
	lui	a0, 0x100	/* a0 = 0x100 << 16 */
	ori	a0, a0, 0x200	/* a0 = a0 | 0x200 */
$L5a9f8:
	lw	v0, g_73c9c	/* v0 = g_73c9c (lui+lw macro) */
	sw	v1, g_73d50	/* *(u32 *)&g_73d50 = v1 (lui AT + sw macro, through AT) */
	sw	a0, 0(v0)	/* *(u32 *)(v0) = a0 */
	move	v0, zero	/* v0 = zero */

	/*
	 * Common return (v0 = 0 or -2).
	 */
$L5aa10:
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * _spu_write - write a block of main RAM to sound RAM
 *
 * Purpose:     int _spu_write(void *src, unsigned size). When _spu_transMode is 1 (DMA) the block goes
 *              through the DMA channel: _spu_t(2, _spu_tsa << _spu_mem_mode_plus) re-asserts the
 *              transfer address, _spu_t(1) selects the write direction and _spu_t(3, src, size) starts
 *              the transfer (it completes asynchronously through _spu_FiDMA). Otherwise (transfer mode 0,
 *              manual) f_5a018 pushes the data through the FIFO, synchronously.
 * Inputs:      a0 = src, a1 = size in bytes.
 * Outputs:     v0 = size (the size argument, in both paths).
 * Clobbers:    v0, v1, a0, a1, a2 (s0, s1, ra saved). Frame of 0x20 bytes.
 * Reads:       _spu_transMode, _spu_tsa, _spu_mem_mode_plus.
 * Callers:     SpuWrite module code (0x8005c9b8; the binding is shared with _spu_read, see the report).
 * Callees:     _spu_t, f_5a018.
 * Registers:   s1 = src, s0 = size.
 */
	.globl	_spu_write
_spu_write:

	/*
	 * Prologue (frame 0x20); s1 = src, s0 = size; if _spu_transMode == 1 (DMA) take the DMA path, else the FIFO path.
	 */
	addiu	sp, sp, -0x20	/* sp = sp - 32 */
	sw	s1, 0x14(sp)	/* *(u32 *)(sp + 0x14) = s1 */
	move	s1, a0	/* s1 = a0 */
	sw	s0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = s0 */
	move	s0, a1	/* s0 = a1 */
	lw	v1, _spu_transMode	/* v1 = _spu_transMode (lui+lw macro) */
	ori	v0, zero, 1	/* v0 = 1 */
	bne	v1, v0, $L5aa7c	/* if (v1 != v0) goto $L5aa7c */
	sw	ra, 0x18(sp)	/* delay slot (real work): *(u32 *)(sp + 0x18) = ra */

	/*
	 * DMA path: _spu_t(2, _spu_tsa << _spu_mem_mode_plus); _spu_t(1); _spu_t(3, src, size). Return size.
	 */
	lhu	v0, _spu_tsa	/* v0 = (u16)_spu_tsa (lui+lhu macro) */
	lw	a1, _spu_mem_mode_plus	/* a1 = _spu_mem_mode_plus (lui+lw macro) */
	ori	a0, zero, 2	/* a0 = 2 */
	jal	_spu_t	/* call _spu_t */
	sllv	a1, v0, a1	/* delay slot (real work): a1 = v0 << a1 */
	jal	_spu_t	/* call _spu_t */
	ori	a0, zero, 1	/* delay slot (real work): a0 = 1 */
	ori	a0, zero, 3	/* a0 = 3 */
	move	a1, s1	/* a1 = s1 */
	jal	_spu_t	/* call _spu_t */
	move	a2, s0	/* delay slot (real work): a2 = s0 */
	j	$L5aa8c	/* goto $L5aa8c */
	move	v0, s0	/* delay slot (real work): v0 = s0 */

	/*
	 * FIFO path: f_5a018(src, size) (synchronous); return size.
	 */
$L5aa7c:
	move	a0, s1	/* a0 = s1 */
	jal	f_5a018	/* call f_5a018 */
	move	a1, s0	/* delay slot (real work): a1 = s0 */
	move	v0, s0	/* v0 = s0 */
$L5aa8c:
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(sp + 0x18) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(sp + 0x14) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(sp + 0x10) */
	addiu	sp, sp, 0x20	/* sp = sp + 32 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * _spu_read - read a block of sound RAM into main RAM
 *
 * Purpose:     int _spu_read(void *dest, unsigned size). Always a DMA transfer: _spu_t(2, _spu_tsa <<
 *              _spu_mem_mode_plus) sets the address, _spu_t(0) selects the read direction and
 *              _spu_t(3, dest, size) starts it (completion through _spu_FiDMA).
 * Inputs:      a0 = dest, a1 = size in bytes.
 * Outputs:     v0 = size.
 * Clobbers:    v0, a0, a1, a2 (s0, s1, ra saved). Frame of 0x20 bytes.
 * Reads:       _spu_tsa, _spu_mem_mode_plus.
 * Callers:     SpuRead module code.
 * Callees:     _spu_t.
 * Registers:   s1 = dest, s0 = size.
 */
	.globl	_spu_read
_spu_read:

	/*
	 * Prologue; s1 = dest, s0 = size. _spu_t(2, _spu_tsa << _spu_mem_mode_plus); _spu_t(0) (read direction);
	 * _spu_t(3, dest, size). Return size.
	 */
	addiu	sp, sp, -0x20	/* sp = sp - 32 */
	sw	s1, 0x14(sp)	/* *(u32 *)(sp + 0x14) = s1 */
	move	s1, a0	/* s1 = a0 */
	sw	s0, 0x10(sp)	/* *(u32 *)(sp + 0x10) = s0 */
	move	s0, a1	/* s0 = a1 */
	lhu	v0, _spu_tsa	/* v0 = (u16)_spu_tsa (lui+lhu macro) */
	lw	a1, _spu_mem_mode_plus	/* a1 = _spu_mem_mode_plus (lui+lw macro) */
	ori	a0, zero, 2	/* a0 = 2 */
	sw	ra, 0x18(sp)	/* *(u32 *)(sp + 0x18) = ra */
	jal	_spu_t	/* call _spu_t */
	sllv	a1, v0, a1	/* delay slot (real work): a1 = v0 << a1 */
	jal	_spu_t	/* call _spu_t */
	move	a0, zero	/* delay slot (real work): a0 = zero */
	ori	a0, zero, 3	/* a0 = 3 */
	move	a1, s1	/* a1 = s1 */
	jal	_spu_t	/* call _spu_t */
	move	a2, s0	/* delay slot (real work): a2 = s0 */
	move	v0, s0	/* v0 = s0 */
	lw	ra, 0x18(sp)	/* ra = *(u32 *)(sp + 0x18) */
	lw	s1, 0x14(sp)	/* s1 = *(u32 *)(sp + 0x14) */
	lw	s0, 0x10(sp)	/* s0 = *(u32 *)(sp + 0x10) */
	addiu	sp, sp, 0x20	/* sp = sp + 32 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/* ---- Part of the module after _spu_read: _spu_ioctl and f_5b3f8 ---- */

	.rdata

/*
 * s_wait_ioctl_irq - wait description: the IRQ enable bit did not follow a request
 *
 * Layout:      NUL-terminated string, 17 bytes, 3 bytes of padding.
 * Users:       _spu_ioctl (requests 0x10).
 */
s_wait_ioctl_irq:
	.ascii	"wait (ioctl/IRQ)\000"
	.align	2

	.text

/*
 * _spu_ioctl - SPU control request dispatcher
 *
 * Purpose:     int _spu_ioctl(int unused, int request, void *arg). The single entry point through which
 *              the SpuXxx API module code reads and writes the SPU's global state. a1 selects the request;
 *              a2 points at (or carries) its argument. The request is found by a binary search over the
 *              request code (a comparison tree of beq/sltiu pairs, 0x8005ab0c..0x8005ad23), and each case
 *              then reads or writes SPU registers through _spu_RXX. Request codes handled:
 *                0x001 set mute (arg 0 = unmute, else mute)     0x002 get mute
 *                0x010 set IRQ enable                           0x016 get IRQ enable
 *                0x013 set IRQ address (byte address)           0x014 get IRQ address
 *                0x015 set IRQ callback (_SpuCallback)
 *                0x021 / 0x02e set transfer mode                0x02f get transfer mode
 *                0x02b set transfer start address (bytes)       0x02d get transfer start address
 *                0x040 set pitch-modulation voices (PMON, OR)   0x041 get      0x042 clear
 *                0x050 set noise clock                          0x051 get noise clock
 *                0x052 set noise voices (NON, OR)               0x053 get      0x054 clear
 *                0x060 set reverb master enable                 0x06a get reverb master enable
 *                0x061 set reverb work area start (raw)         0x068 set (byte address)
 *                0x069 get reverb work area start (bytes)
 *                0x063 load reverb attributes (_spu_setReverbAttr)
 *                0x065 set reverb voices (EON, OR)              0x066 get      0x067 clear
 *                0x100 set common attributes (f_5ba48)          0x101 get common attributes (f_5bde0)
 *                0x203 key off voices (KOFF write)              0x204 key on voices (KON write)
 *                0x210 / 0x212 / 0x213 read decoded-data (capture) area into arg
 *              Any other request returns 0 and does nothing. Voice-mask arguments are three bytes: a u16
 *              (voices 0-15) followed by a u8 (voices 16-23).
 * Inputs:      a1 = request code, a2 = arg.
 * Outputs:     v0 = 0, except 0x210 / 0x212 / 0x213 which return 1 when the capture half bit (SPUSTAT
 *              bit 11) is clear, and the cases that fall into a timeout, which printf and return 0.
 * Clobbers:    v0, v1, a0, a1, a2, a3 (ra saved). Frame of 0x18 bytes.
 * Hardware:    SPUCNT, SPUSTAT, MVOL, KON, KOFF, PMON, NON, EON, mBASE, IRQ_ADDR, TSA (all through
 *              _spu_RXX).
 * Reads:       _spu_mem_mode (the address rounding unit), _spu_mem_mode_plus (address shift),
 *              _spu_transMode, _spu_tsa.
 * Writes:      g_73c88 (poll counter), _spu_transMode, _spu_tsa.
 * Callers:     the SpuXxx API modules (jal at 0x8005971c, 0x80059740 ... 0x8005ca70, 17 sites).
 * Callees:     _spu_setReverbAttr, f_5ba48, f_5bde0, _spu_r_, _SpuCallback, printf (timeouts).
 * Byte-address cases (0x013, 0x02b, 0x068): the argument is a byte address. If it is not a multiple of
 * _spu_mem_mode it is rounded up by adding _spu_mem_mode, then shifted right by _spu_mem_mode_plus
 * to SPU address units (the same rounding the 3.3 SPU_IO code applies with _spu_mem_mode_unit).
 */
	.globl	_spu_ioctl
_spu_ioctl:

	/*
	 * Prologue (frame 0x18; ra is saved in the delay slot of the first beq). The request code a1 is looked up with a
	 * binary search: each node tests one code with beq and splits the remaining range with sltiu, so the tree
	 * below only branches; the leaves jump to the request handlers further down. A code with no handler leaves
	 * v0 = 0 and jumps to $ioctl_return.
	 */
	addiu	sp, sp, -0x18	/* sp = sp - 24 */
	ori	v0, zero, 0x52	/* v0 = 0x52 */
	beq	a1, v0, $req_52_set_non	/* if (a1 == v0) goto $req_52_set_non */
	sw	ra, 0x10(sp)	/* delay slot (real work): *(u32 *)(sp + 0x10) = ra */
	sltiu	v0, a1, 0x53	/* v0 = ((u32)a1 < 83) */
	beq	v0, zero, $L5ac1c	/* if (v0 == 0) goto $L5ac1c */
	ori	v0, zero, 0x2b	/* delay slot (real work): v0 = 0x2b */
	beq	a1, v0, $req_2b_set_tsa	/* if (a1 == v0) goto $req_2b_set_tsa */
	sltiu	v0, a1, 0x2c	/* delay slot (real work): v0 = ((u32)a1 < 44) */
	beq	v0, zero, $L5aba8	/* if (v0 == 0) goto $L5aba8 */
	ori	v0, zero, 0x13	/* delay slot (real work): v0 = 0x13 */
	beq	a1, v0, $req_13_set_irq_addr	/* if (a1 == v0) goto $req_13_set_irq_addr */
	sltiu	v0, a1, 0x14	/* delay slot (real work): v0 = ((u32)a1 < 20) */
	beq	v0, zero, $L5ab7c	/* if (v0 == 0) goto $L5ab7c */
	ori	v0, zero, 2	/* delay slot (real work): v0 = 2 */
	beq	a1, v0, $req_02_get_mute	/* if (a1 == v0) goto $req_02_get_mute */
	sltiu	v0, a1, 3	/* delay slot (real work): v0 = ((u32)a1 < 3) */
	beq	v0, zero, $L5ab68	/* if (v0 == 0) goto $L5ab68 */
	ori	v0, zero, 1	/* delay slot (real work): v0 = 1 */
	beq	a1, v0, $req_01_set_mute	/* if (a1 == v0) goto $req_01_set_mute */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5ab68:
	ori	v0, zero, 0x10	/* v0 = 0x10 */
	beq	a1, v0, $req_10_set_irq	/* if (a1 == v0) goto $req_10_set_irq */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5ab7c:
	ori	v0, zero, 0x15	/* v0 = 0x15 */
	beq	a1, v0, $req_15_irq_callback	/* if (a1 == v0) goto $req_15_irq_callback */
	sltiu	v0, a1, 0x15	/* delay slot (real work): v0 = ((u32)a1 < 21) */
	bne	v0, zero, $req_14_get_irq_addr	/* if (v0 != 0) goto $req_14_get_irq_addr */
	ori	v0, zero, 0x16	/* delay slot (real work): v0 = 0x16 */
	beq	a1, v0, $req_16_get_irq	/* if (a1 == v0) goto $req_16_get_irq */
	ori	v0, zero, 0x21	/* delay slot (real work): v0 = 0x21 */
	beq	a1, v0, $req_21_2e_set_trans_mode	/* if (a1 == v0) goto $req_21_2e_set_trans_mode */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5aba8:
	ori	v0, zero, 0x40	/* v0 = 0x40 */
	beq	a1, v0, $req_40_set_pmon	/* if (a1 == v0) goto $req_40_set_pmon */
	sltiu	v0, a1, 0x41	/* delay slot (real work): v0 = ((u32)a1 < 65) */
	beq	v0, zero, $L5abf0	/* if (v0 == 0) goto $L5abf0 */
	ori	v0, zero, 0x2e	/* delay slot (real work): v0 = 0x2e */
	beq	a1, v0, $req_21_2e_set_trans_mode	/* if (a1 == v0) goto $req_21_2e_set_trans_mode */
	sltiu	v0, a1, 0x2f	/* delay slot (real work): v0 = ((u32)a1 < 47) */
	beq	v0, zero, $L5abdc	/* if (v0 == 0) goto $L5abdc */
	ori	v0, zero, 0x2d	/* delay slot (real work): v0 = 0x2d */
	beq	a1, v0, $req_2d_get_tsa	/* if (a1 == v0) goto $req_2d_get_tsa */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5abdc:
	ori	v0, zero, 0x2f	/* v0 = 0x2f */
	beq	a1, v0, $req_2f_get_trans_mode	/* if (a1 == v0) goto $req_2f_get_trans_mode */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5abf0:
	ori	v0, zero, 0x42	/* v0 = 0x42 */
	beq	a1, v0, $req_42_clear_pmon	/* if (a1 == v0) goto $req_42_clear_pmon */
	sltiu	v0, a1, 0x42	/* delay slot (real work): v0 = ((u32)a1 < 66) */
	bne	v0, zero, $req_41_get_pmon	/* if (v0 != 0) goto $req_41_get_pmon */
	ori	v0, zero, 0x50	/* delay slot (real work): v0 = 0x50 */
	beq	a1, v0, $req_50_set_noise_clock	/* if (a1 == v0) goto $req_50_set_noise_clock */
	ori	v0, zero, 0x51	/* delay slot (real work): v0 = 0x51 */
	beq	a1, v0, $req_51_get_noise_clock	/* if (a1 == v0) goto $req_51_get_noise_clock */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5ac1c:
	ori	v0, zero, 0x68	/* v0 = 0x68 */
	beq	a1, v0, $req_68_set_mbase	/* if (a1 == v0) goto $req_68_set_mbase */
	sltiu	v0, a1, 0x69	/* delay slot (real work): v0 = ((u32)a1 < 105) */
	beq	v0, zero, $L5aca0	/* if (v0 == 0) goto $L5aca0 */
	ori	v0, zero, 0x61	/* delay slot (real work): v0 = 0x61 */
	beq	a1, v0, $req_61_set_mbase_raw	/* if (a1 == v0) goto $req_61_set_mbase_raw */
	sltiu	v0, a1, 0x62	/* delay slot (real work): v0 = ((u32)a1 < 98) */
	beq	v0, zero, $L5ac60	/* if (v0 == 0) goto $L5ac60 */
	ori	v0, zero, 0x54	/* delay slot (real work): v0 = 0x54 */
	beq	a1, v0, $req_54_clear_non	/* if (a1 == v0) goto $req_54_clear_non */
	sltiu	v0, a1, 0x54	/* delay slot (real work): v0 = ((u32)a1 < 84) */
	bne	v0, zero, $req_53_get_non	/* if (v0 != 0) goto $req_53_get_non */
	ori	v0, zero, 0x60	/* delay slot (real work): v0 = 0x60 */
	beq	a1, v0, $req_60_set_reverb	/* if (a1 == v0) goto $req_60_set_reverb */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5ac60:
	ori	v0, zero, 0x65	/* v0 = 0x65 */
	beq	a1, v0, $req_65_set_eon	/* if (a1 == v0) goto $req_65_set_eon */
	sltiu	v0, a1, 0x66	/* delay slot (real work): v0 = ((u32)a1 < 102) */
	beq	v0, zero, $L5ac84	/* if (v0 == 0) goto $L5ac84 */
	ori	v0, zero, 0x63	/* delay slot (real work): v0 = 0x63 */
	beq	a1, v0, $req_63_reverb_attr	/* if (a1 == v0) goto $req_63_reverb_attr */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5ac84:
	ori	v0, zero, 0x66	/* v0 = 0x66 */
	beq	a1, v0, $req_66_get_eon	/* if (a1 == v0) goto $req_66_get_eon */
	ori	v0, zero, 0x67	/* delay slot (real work): v0 = 0x67 */
	beq	a1, v0, $req_67_clear_eon	/* if (a1 == v0) goto $req_67_clear_eon */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5aca0:
	ori	v0, zero, 0x203	/* v0 = 0x203 */
	beq	a1, v0, $req_203_key_off	/* if (a1 == v0) goto $req_203_key_off */
	sltiu	v0, a1, 0x204	/* delay slot (real work): v0 = ((u32)a1 < 516) */
	beq	v0, zero, $L5acdc	/* if (v0 == 0) goto $L5acdc */
	ori	v0, zero, 0x6a	/* delay slot (real work): v0 = 0x6a */
	beq	a1, v0, $req_6a_get_reverb	/* if (a1 == v0) goto $req_6a_get_reverb */
	sltiu	v0, a1, 0x6a	/* delay slot (real work): v0 = ((u32)a1 < 106) */
	bne	v0, zero, $req_69_get_mbase	/* if (v0 != 0) goto $req_69_get_mbase */
	ori	v0, zero, 0x100	/* delay slot (real work): v0 = 0x100 */
	beq	a1, v0, $req_100_set_common	/* if (a1 == v0) goto $req_100_set_common */
	ori	v0, zero, 0x101	/* delay slot (real work): v0 = 0x101 */
	beq	a1, v0, $req_101_get_common	/* if (a1 == v0) goto $req_101_get_common */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5acdc:
	ori	v0, zero, 0x210	/* v0 = 0x210 */
	beq	a1, v0, $req_210_read_capture	/* if (a1 == v0) goto $req_210_read_capture */
	move	a0, a2	/* delay slot (real work): a0 = a2 */
	sltiu	v0, a1, 0x211	/* v0 = ((u32)a1 < 529) */
	beq	v0, zero, $L5ad04	/* if (v0 == 0) goto $L5ad04 */
	ori	v0, zero, 0x204	/* delay slot (real work): v0 = 0x204 */
	beq	a1, v0, $req_204_key_on	/* if (a1 == v0) goto $req_204_key_on */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */
$L5ad04:
	ori	v0, zero, 0x212	/* v0 = 0x212 */
	beq	a1, v0, $req_212_read_capture_lo	/* if (a1 == v0) goto $req_212_read_capture_lo */
	move	a0, a2	/* delay slot (real work): a0 = a2 */
	ori	v0, zero, 0x213	/* v0 = 0x213 */
	beq	a1, v0, $req_213_read_capture_hi	/* if (a1 == v0) goto $req_213_read_capture_hi */
	addiu	a0, a2, 0x800	/* delay slot (real work): a0 = a2 + 2048 */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x01, set mute: arg == 0 sets SPUCNT bit 14 (0x4000, SPU un-muted); any other arg clears it (muted).
	 * Return 0.
	 */
$req_01_set_mute:
	bne	a2, zero, $L5ad50	/* if (a2 != 0) goto $L5ad50 */
	nop	/* delay slot: nop */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	ori	v0, v0, 0x4000	/* v0 = v0 | 0x4000 */
	sh	v0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = v0 [SPU SPUCNT (control)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * arg != 0: SPUCNT &= ~0x4000 (mute).
	 */
$L5ad50:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0xbfff	/* v0 = v0 & 0xbfff */
	sh	v0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = v0 [SPU SPUCNT (control)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x02, get mute: *arg = 1 when SPUCNT bit 14 is clear (muted), else 0. Return 0.
	 */
$req_02_get_mute:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v0)	/* v0 = *(u16 *)(v0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x4000	/* v0 = v0 & 0x4000 */
	beq	v0, zero, $set_arg_one	/* if (v0 == 0) goto $set_arg_one */
	ori	v0, zero, 1	/* delay slot (real work): v0 = 1 */
$set_arg_zero:
	j	$ioctl_ok	/* goto $ioctl_ok */
	sw	zero, 0(a2)	/* delay slot (real work): *(u32 *)(a2) = zero */

	/*
	 * Request 0x10, set IRQ enable: arg == 0 clears SPUCNT bit 6 (0x40, IRQ9 enable), then polls (g_73c88 counts, bounded by
	 * 5000) until the bit reads back clear; arg != 0 sets it and polls until it reads back set. A timeout
	 * printf("SPU:T/O [%s]\n", "wait (ioctl/IRQ)") and return 0.
	 */
$req_10_set_irq:
	bne	a2, zero, $L5ae18	/* if (a2 != 0) goto $L5ae18 */
	nop	/* delay slot: nop */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1aa(v1)	/* a0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	a0, a0, 0xffbf	/* a0 = a0 & 0xffbf */
	sh	a0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = a0 [SPU SPUCNT (control)] */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	sw	zero, g_73c88	/* *(u32 *)&g_73c88 = zero (lui AT + sw macro, through AT) */
	andi	v0, v0, 0x40	/* v0 = v0 & 0x40 */
	beq	v0, zero, $ioctl_return	/* if (v0 == 0) goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Wait for the bit to clear: count a poll; past 0x1388 polls go to the timeout report, else re-read SPUCNT & 0x40.
	 */
$L5add8:
	lw	v0, g_73c88	/* v0 = g_73c88 (lui+lw macro) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, g_73c88	/* *(u32 *)&g_73c88 = v0 (lui AT + sw macro, through AT) */
	slti	v0, v0, 0x1389	/* v0 = (v0 < 5001) */
	beq	v0, zero, $ioctl_timeout	/* if (v0 == 0) goto $ioctl_timeout */
	nop	/* delay slot: nop */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x40	/* v0 = v0 & 0x40 */
	bne	v0, zero, $L5add8	/* if (v0 != 0) goto $L5add8 */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */

	/*
	 * arg != 0: SPUCNT |= 0x40 (IRQ9 enable), g_73c88 = 0; return at once if the bit already reads set.
	 */
$L5ae18:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1aa(v1)	/* a0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	ori	a0, a0, 0x40	/* a0 = a0 | 0x40 */
	sh	a0, 0x1aa(v1)	/* *(u16 *)(v1 + 0x1aa) = a0 [SPU SPUCNT (control)] */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	sw	zero, g_73c88	/* *(u32 *)&g_73c88 = zero (lui AT + sw macro, through AT) */
	andi	v0, v0, 0x40	/* v0 = v0 & 0x40 */
	bne	v0, zero, $ioctl_return	/* if (v0 != 0) goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Wait for the bit to set: count a poll; past 0x1388 polls go to the timeout report, else re-read SPUCNT & 0x40.
	 */
$L5ae4c:
	lw	v0, g_73c88	/* v0 = g_73c88 (lui+lw macro) */
	nop	/* load delay slot */
	addiu	v0, v0, 1	/* v0 = v0 + 1 */
	sw	v0, g_73c88	/* *(u32 *)&g_73c88 = v0 (lui AT + sw macro, through AT) */
	slti	v0, v0, 0x1389	/* v0 = (v0 < 5001) */
	beq	v0, zero, $ioctl_timeout	/* if (v0 == 0) goto $ioctl_timeout */
	nop	/* delay slot: nop */
	lhu	v0, 0x1aa(v1)	/* v0 = *(u16 *)(v1 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x40	/* v0 = v0 & 0x40 */
	beq	v0, zero, $L5ae4c	/* if (v0 == 0) goto $L5ae4c */
	move	v0, zero	/* delay slot (real work): v0 = zero */
	j	$ioctl_return	/* goto $ioctl_return */
	nop	/* delay slot: nop */

	/*
	 * Request 0x16, get IRQ enable: *arg = (SPUCNT & 0x40) != 0.
	 */
$req_16_get_irq:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v0)	/* v0 = *(u16 *)(v0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x40	/* v0 = v0 & 0x40 */
	bne	v0, zero, $set_arg_one	/* if (v0 != 0) goto $set_arg_one */
	ori	v0, zero, 1	/* delay slot (real work): v0 = 1 */
	j	$ioctl_ok	/* goto $ioctl_ok */
	sw	zero, 0(a2)	/* delay slot (real work): *(u32 *)(a2) = zero */

	/*
	 * Request 0x13, set IRQ address: a0 = *arg (byte address), a1 = _spu_mem_mode. If a1 != 0 and a0 is not a multiple of a1,
	 * a0 += a1 (the checked signed remainder below: divide-by-zero and INT_MIN / -1 traps).
	 * IRQ_ADDR (0x1a4) = a0 >> _spu_mem_mode_plus. Return 0.
	 */
$req_13_set_irq_addr:
	lw	a1, _spu_mem_mode	/* a1 = _spu_mem_mode (lui+lw macro) */
	lw	a0, 0(a2)	/* a0 = *(u32 *)(a2) */
	beq	a1, zero, $L5af04	/* if (a1 == 0) goto $L5af04 */
	nop	/* delay slot: nop */
	/* v0 = a0 % a1: ASPSX 2.08's checked signed remainder macro, 11 words, using $at:
	     div zero, a0, a1; bnez a1, 1f; nop; break 7 (divide by zero);
	  1: addiu at, zero, -1; bne a1, at, 2f; lui at, 0x8000 (delay slot);
	     bne a0, at, 2f; nop; tge zero, zero, 0x5d (0x00001770, INT_MIN % -1 overflow);
	  2: mfhi v0. The div runs while the checks branch around the traps. */
	rem	v0, a0, a1	/* v0 = remainder */
	nop		/* hi read delay, as in retail */
	beq	v0, zero, $L5af04	/* if (v0 == 0) goto $L5af04 */
	nop	/* delay slot: nop */
	addu	a0, a0, a1	/* a0 = a0 + a1 */
$L5af04:
	lw	v0, _spu_mem_mode_plus	/* v0 = _spu_mem_mode_plus (lui+lw macro) */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	srav	a0, a0, v0	/* a0 = a0 >> (arith) v0 */
	sh	a0, 0x1a4(v1)	/* *(u16 *)(v1 + 0x1a4) = a0 [SPU IRQ_ADDR (IRQ9 address)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x14, get IRQ address: a0 = IRQ_ADDR (0x1a4); shared tail at $req_69 converts it to a byte address into *arg.
	 */
$req_14_get_irq_addr:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1a4(v0)	/* a0 = *(u16 *)(v0 + 0x1a4) [SPU IRQ_ADDR (IRQ9 address)] */
	j	$L5b254	/* goto $L5b254 */
	nop	/* delay slot: nop */

	/*
	 * Request 0x15, set IRQ callback: _SpuCallback(arg) stores the user's IRQ function pointer. Return 0.
	 */
$req_15_irq_callback:
	jal	_SpuCallback	/* call _SpuCallback */
	move	a0, a2	/* delay slot (real work): a0 = a2 */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x2b, set transfer start address: byte address *arg rounded up to _spu_mem_mode as for 0x13, shifted right by
	 * _spu_mem_mode_plus and stored in _spu_tsa (halfword). Return 0.
	 */
$req_2b_set_tsa:
	lw	a1, _spu_mem_mode	/* a1 = _spu_mem_mode (lui+lw macro) */
	lw	a0, 0(a2)	/* a0 = *(u32 *)(a2) */
	beq	a1, zero, $L5af9c	/* if (a1 == 0) goto $L5af9c */
	nop	/* delay slot: nop */
	/* v0 = a0 % a1: ASPSX 2.08's checked signed remainder macro, 11 words, using $at:
	     div zero, a0, a1; bnez a1, 1f; nop; break 7 (divide by zero);
	  1: addiu at, zero, -1; bne a1, at, 2f; lui at, 0x8000 (delay slot);
	     bne a0, at, 2f; nop; tge zero, zero, 0x5d (0x00001770, INT_MIN % -1 overflow);
	  2: mfhi v0. The div runs while the checks branch around the traps. */
	rem	v0, a0, a1	/* v0 = remainder */
	nop		/* hi read delay, as in retail */
	beq	v0, zero, $L5af9c	/* if (v0 == 0) goto $L5af9c */
	nop	/* delay slot: nop */
	addu	a0, a0, a1	/* a0 = a0 + a1 */
$L5af9c:
	lw	v0, _spu_mem_mode_plus	/* v0 = _spu_mem_mode_plus (lui+lw macro) */
	nop	/* load delay slot */
	srav	a0, a0, v0	/* a0 = a0 >> (arith) v0 */
	sh	a0, _spu_tsa	/* *(u16 *)&_spu_tsa = a0 (lui AT + sh macro, through AT) */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x2d, get transfer start address: *arg = _spu_tsa << _spu_mem_mode_plus (a byte address).
	 */
$req_2d_get_tsa:
	lhu	v0, _spu_tsa	/* v0 = (u16)_spu_tsa (lui+lhu macro) */
	lw	v1, _spu_mem_mode_plus	/* v1 = _spu_mem_mode_plus (lui+lw macro) */
	nop	/* load delay slot */
	sllv	v0, v0, v1	/* v0 = v0 << v1 */
	j	$ioctl_ok	/* goto $ioctl_ok */
	sw	v0, 0(a2)	/* delay slot (real work): *(u32 *)(a2) = v0 */

	/*
	 * Requests 0x21 and 0x2e, set transfer mode: _spu_transMode = *arg (1 = DMA, 0 = manual FIFO). Return 0.
	 */
$req_21_2e_set_trans_mode:
	lw	v0, 0(a2)	/* v0 = *(u32 *)(a2) */
	nop	/* load delay slot */
	sw	v0, _spu_transMode	/* *(u32 *)&_spu_transMode = v0 (lui AT + sw macro, through AT) */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x2f, get transfer mode: *arg = _spu_transMode.
	 */
$req_2f_get_trans_mode:
	lw	v0, _spu_transMode	/* v0 = _spu_transMode (lui+lw macro) */
	j	$ioctl_ok	/* goto $ioctl_ok */
	sw	v0, 0(a2)	/* delay slot (real work): *(u32 *)(a2) = v0 */

	/*
	 * Request 0x40, set pitch-modulation voices: PMON low |= *(u16 *)arg; PMON high |= *(u8 *)(arg + 2) (voices 16-23).
	 * Return 0.
	 */
$req_40_set_pmon:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	lhu	a0, 0x190(v0)	/* a0 = *(u16 *)(v0 + 0x190) [SPU PMON low (pitch modulation, voices 0-15)] */
	nop	/* load delay slot */
	or	v1, a0, v1	/* v1 = a0 | v1 */
	move	a0, v1	/* a0 = v1 */
	sh	a0, 0x190(v0)	/* *(u16 *)(v0 + 0x190) = a0 [SPU PMON low (pitch modulation, voices 0-15)] */
	lhu	a0, 0x192(v0)	/* a0 = *(u16 *)(v0 + 0x192) [SPU PMON high (voices 16-23)] */
	lbu	v1, 2(a2)	/* v1 = *(u8 *)(a2 + 2) */
	nop	/* load delay slot */
	or	a0, a0, v1	/* a0 = a0 | v1 */
	sh	a0, 0x192(v0)	/* *(u16 *)(v0 + 0x192) = a0 [SPU PMON high (voices 16-23)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x41, get pitch-modulation voices: v1 = PMON high & 0xff, v0 = PMON low; joined at the shared tail $L5b2d0.
	 */
$req_41_get_pmon:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v1, 0x192(v0)	/* v1 = *(u16 *)(v0 + 0x192) [SPU PMON high (voices 16-23)] */
	lhu	v0, 0x190(v0)	/* v0 = *(u16 *)(v0 + 0x190) [SPU PMON low (pitch modulation, voices 0-15)] */
	j	$L5b2d0	/* goto $L5b2d0 */
	andi	v1, v1, 0xff	/* delay slot (real work): v1 = v1 & 0xff */

	/*
	 * Request 0x42, clear pitch-modulation voices: PMON low &= ~*(u16 *)arg; PMON high &= ~*(u8 *)(arg + 2) (nor with zero). Return 0.
	 */
$req_42_clear_pmon:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	lhu	a0, 0x190(v0)	/* a0 = *(u16 *)(v0 + 0x190) [SPU PMON low (pitch modulation, voices 0-15)] */
	nor	v1, zero, v1	/* v1 = ~v1 */
	and	a0, a0, v1	/* a0 = a0 & v1 */
	sh	a0, 0x190(v0)	/* *(u16 *)(v0 + 0x190) = a0 [SPU PMON low (pitch modulation, voices 0-15)] */
	lbu	v1, 2(a2)	/* v1 = *(u8 *)(a2 + 2) */
	lhu	a0, 0x192(v0)	/* a0 = *(u16 *)(v0 + 0x192) [SPU PMON high (voices 16-23)] */
	nor	v1, zero, v1	/* v1 = ~v1 */
	and	a0, a0, v1	/* a0 = a0 & v1 */
	sh	a0, 0x192(v0)	/* *(u16 *)(v0 + 0x192) = a0 [SPU PMON high (voices 16-23)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x50, set noise clock: v1 = (*(u16 *)arg & 0x3f) << 8; SPUCNT = (SPUCNT & 0xc0ff) | v1 (bits 13-8). Return 0.
	 */
$req_50_set_noise_clock:
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	andi	v1, v1, 0x3f	/* v1 = v1 & 0x3f */
	lhu	a0, 0x1aa(v0)	/* a0 = *(u16 *)(v0 + 0x1aa) [SPU SPUCNT (control)] */
	sll	v1, v1, 8	/* v1 = v1 << 8 */
	j	$L5b180	/* goto $L5b180 */
	andi	a0, a0, 0xc0ff	/* delay slot (real work): a0 = a0 & 0xc0ff */

	/*
	 * Request 0x51, get noise clock: *(u16 *)arg = (SPUCNT & 0x3f00) >> 8.
	 */
$req_51_get_noise_clock:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1aa(v0)	/* v0 = *(u16 *)(v0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x3f00	/* v0 = v0 & 0x3f00 */
	srl	v0, v0, 8	/* v0 = v0 >> (logical) 8 */
	j	$ioctl_ok	/* goto $ioctl_ok */
	sh	v0, 0(a2)	/* delay slot (real work): *(u16 *)(a2) = v0 */

	/*
	 * Request 0x52, set noise voices: NON low |= *(u16 *)arg; NON high |= *(u8 *)(arg + 2). Return 0.
	 */
$req_52_set_non:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	lhu	v0, 0x194(a0)	/* v0 = *(u16 *)(a0 + 0x194) [SPU NON low (noise mode, voices 0-15)] */
	nop	/* load delay slot */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sh	v0, 0x194(a0)	/* *(u16 *)(a0 + 0x194) = v0 [SPU NON low (noise mode, voices 0-15)] */
	lbu	v1, 2(a2)	/* v1 = *(u8 *)(a2 + 2) */
	lhu	v0, 0x196(a0)	/* v0 = *(u16 *)(a0 + 0x196) [SPU NON high (voices 16-23)] */
	nop	/* load delay slot */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	sh	v0, 0x196(a0)	/* *(u16 *)(a0 + 0x196) = v0 [SPU NON high (voices 16-23)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x53, get noise voices: v1 = NON high & 0xff, v0 = NON low; joined at the shared tail $L5b2d0.
	 */
$req_53_get_non:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v1, 0x196(v0)	/* v1 = *(u16 *)(v0 + 0x196) [SPU NON high (voices 16-23)] */
	lhu	v0, 0x194(v0)	/* v0 = *(u16 *)(v0 + 0x194) [SPU NON low (noise mode, voices 0-15)] */
	j	$L5b2d0	/* goto $L5b2d0 */
	andi	v1, v1, 0xff	/* delay slot (real work): v1 = v1 & 0xff */

	/*
	 * Request 0x54, clear noise voices: NON low &= ~*(u16 *)arg; NON high &= ~*(u8 *)(arg + 2). Return 0.
	 */
$req_54_clear_non:
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0(a2)	/* v0 = *(u16 *)(a2) */
	lhu	v1, 0x194(a0)	/* v1 = *(u16 *)(a0 + 0x194) [SPU NON low (noise mode, voices 0-15)] */
	nor	v0, zero, v0	/* v0 = ~v0 */
	and	v1, v1, v0	/* v1 = v1 & v0 */
	sh	v1, 0x194(a0)	/* *(u16 *)(a0 + 0x194) = v1 [SPU NON low (noise mode, voices 0-15)] */
	lbu	v0, 2(a2)	/* v0 = *(u8 *)(a2 + 2) */
	lhu	v1, 0x196(a0)	/* v1 = *(u16 *)(a0 + 0x196) [SPU NON high (voices 16-23)] */
	nor	v0, zero, v0	/* v0 = ~v0 */
	and	v1, v1, v0	/* v1 = v1 & v0 */
	sh	v1, 0x196(a0)	/* *(u16 *)(a0 + 0x196) = v1 [SPU NON high (voices 16-23)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x60, set reverb master enable: v1 = (*(u16 *)arg & 1) << 7; SPUCNT = (SPUCNT & 0xff7f) | v1 (bit 7).
	 * The tail at $L5b180 is shared with request 0x50.
	 */
$req_60_set_reverb:
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	andi	v1, v1, 1	/* v1 = v1 & 1 */
	lhu	a0, 0x1aa(v0)	/* a0 = *(u16 *)(v0 + 0x1aa) [SPU SPUCNT (control)] */
	sll	v1, v1, 7	/* v1 = v1 << 7 */
	andi	a0, a0, 0xff7f	/* a0 = a0 & 0xff7f */

	/*
	 * Merge and store: SPUCNT = a0 | v1. Return 0.
	 */
$L5b180:
	or	a0, a0, v1	/* a0 = a0 | v1 */
	sh	a0, 0x1aa(v0)	/* *(u16 *)(v0 + 0x1aa) = a0 [SPU SPUCNT (control)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x6a, get reverb master enable: *arg = (SPUCNT & 0x80) != 0, through the shared $set_arg_zero / $set_arg_one exits.
	 */
$req_6a_get_reverb:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1aa(v0)	/* a0 = *(u16 *)(v0 + 0x1aa) [SPU SPUCNT (control)] */
	nop	/* load delay slot */
	andi	v0, a0, 0x80	/* v0 = a0 & 0x80 */
	beq	v0, zero, $set_arg_zero	/* if (v0 == 0) goto $set_arg_zero */
	ori	v0, zero, 1	/* delay slot (real work): v0 = 1 */
$set_arg_one:
	j	$ioctl_ok	/* goto $ioctl_ok */
	sw	v0, 0(a2)	/* delay slot (real work): *(u32 *)(a2) = v0 */

	/*
	 * Request 0x61, set reverb work area start (raw): mBASE (0x1a2) = *(u16 *)arg, already in 8-byte units. Return 0.
	 */
$req_61_set_mbase_raw:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	v0, 0(a2)	/* v0 = *(u16 *)(a2) */
	nop	/* load delay slot */
	sh	v0, 0x1a2(v1)	/* *(u16 *)(v1 + 0x1a2) = v0 [SPU mBASE (reverb work area start)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x68, set reverb work area start (byte address): *arg rounded up to _spu_mem_mode as for 0x13, shifted right
	 * by _spu_mem_mode_plus, stored to mBASE (0x1a2). Return 0.
	 */
$req_68_set_mbase:
	lw	a1, _spu_mem_mode	/* a1 = _spu_mem_mode (lui+lw macro) */
	lw	a0, 0(a2)	/* a0 = *(u32 *)(a2) */
	beq	a1, zero, $L5b224	/* if (a1 == 0) goto $L5b224 */
	nop	/* delay slot: nop */
	/* v0 = a0 % a1: ASPSX 2.08's checked signed remainder macro, 11 words, using $at:
	     div zero, a0, a1; bnez a1, 1f; nop; break 7 (divide by zero);
	  1: addiu at, zero, -1; bne a1, at, 2f; lui at, 0x8000 (delay slot);
	     bne a0, at, 2f; nop; tge zero, zero, 0x5d (0x00001770, INT_MIN % -1 overflow);
	  2: mfhi v0. The div runs while the checks branch around the traps. */
	rem	v0, a0, a1	/* v0 = remainder */
	nop		/* hi read delay, as in retail */
	beq	v0, zero, $L5b224	/* if (v0 == 0) goto $L5b224 */
	nop	/* delay slot: nop */
	addu	a0, a0, a1	/* a0 = a0 + a1 */
$L5b224:
	lw	v0, _spu_mem_mode_plus	/* v0 = _spu_mem_mode_plus (lui+lw macro) */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	srav	a0, a0, v0	/* a0 = a0 >> (arith) v0 */
	sh	a0, 0x1a2(v1)	/* *(u16 *)(v1 + 0x1a2) = a0 [SPU mBASE (reverb work area start)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x69, get reverb work area start: a0 = mBASE (0x1a2); then (also entered from 0x14 with IRQ_ADDR) *arg = a0 << _spu_mem_mode_plus.
	 */
$req_69_get_mbase:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	a0, 0x1a2(v0)	/* a0 = *(u16 *)(v0 + 0x1a2) [SPU mBASE (reverb work area start)] */
$L5b254:
	lw	v0, _spu_mem_mode_plus	/* v0 = _spu_mem_mode_plus (lui+lw macro) */
	nop	/* load delay slot */
	sllv	a0, a0, v0	/* a0 = a0 << v0 */
	j	$ioctl_ok	/* goto $ioctl_ok */
	sw	a0, 0(a2)	/* delay slot (real work): *(u32 *)(a2) = a0 */

	/*
	 * Request 0x63, load reverb attributes: _spu_setReverbAttr(arg). Return 0.
	 */
$req_63_reverb_attr:
	jal	_spu_setReverbAttr	/* call _spu_setReverbAttr */
	move	a0, a2	/* delay slot (real work): a0 = a2 */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x65, set reverb voices: EON low |= *(u16 *)arg; EON high |= *(u8 *)(arg + 2). Return 0.
	 */
$req_65_set_eon:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	lhu	a0, 0x198(v0)	/* a0 = *(u16 *)(v0 + 0x198) [SPU EON low (reverb enable, voices 0-15)] */
	nop	/* load delay slot */
	or	v1, a0, v1	/* v1 = a0 | v1 */
	move	a0, v1	/* a0 = v1 */
	sh	a0, 0x198(v0)	/* *(u16 *)(v0 + 0x198) = a0 [SPU EON low (reverb enable, voices 0-15)] */
	lhu	a0, 0x19a(v0)	/* a0 = *(u16 *)(v0 + 0x19a) [SPU EON high (voices 16-23)] */
	lbu	v1, 2(a2)	/* v1 = *(u8 *)(a2 + 2) */
	nop	/* load delay slot */
	or	a0, a0, v1	/* a0 = a0 | v1 */
	sh	a0, 0x19a(v0)	/* *(u16 *)(v0 + 0x19a) = a0 [SPU EON high (voices 16-23)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x66, get reverb voices: v1 = EON high & 0xff, v0 = EON low; the tail at $L5b2d0 (shared with 0x41 and 0x53)
	 * forms (high << 16) | low and stores it to *arg.
	 */
$req_66_get_eon:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v1, 0x19a(v0)	/* v1 = *(u16 *)(v0 + 0x19a) [SPU EON high (voices 16-23)] */
	lhu	v0, 0x198(v0)	/* v0 = *(u16 *)(v0 + 0x198) [SPU EON low (reverb enable, voices 0-15)] */
	andi	v1, v1, 0xff	/* v1 = v1 & 0xff */

	/*
	 * Shared tail: v1 <<= 16; *(u32 *)arg = v0 | v1. Return 0.
	 */
$L5b2d0:
	sll	v1, v1, 16	/* v1 = v1 << 16 */
	or	v0, v0, v1	/* v0 = v0 | v1 */
	j	$ioctl_ok	/* goto $ioctl_ok */
	sw	v0, 0(a2)	/* delay slot (real work): *(u32 *)(a2) = v0 */

	/*
	 * Request 0x67, clear reverb voices: EON low &= ~*(u16 *)arg; EON high &= ~*(u8 *)(arg + 2). Return 0.
	 */
$req_67_clear_eon:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	lhu	a0, 0x198(v0)	/* a0 = *(u16 *)(v0 + 0x198) [SPU EON low (reverb enable, voices 0-15)] */
	nor	v1, zero, v1	/* v1 = ~v1 */
	and	a0, a0, v1	/* a0 = a0 & v1 */
	sh	a0, 0x198(v0)	/* *(u16 *)(v0 + 0x198) = a0 [SPU EON low (reverb enable, voices 0-15)] */
	lbu	v1, 2(a2)	/* v1 = *(u8 *)(a2 + 2) */
	lhu	a0, 0x19a(v0)	/* a0 = *(u16 *)(v0 + 0x19a) [SPU EON high (voices 16-23)] */
	nor	v1, zero, v1	/* v1 = ~v1 */
	and	a0, a0, v1	/* a0 = a0 & v1 */
	sh	a0, 0x19a(v0)	/* *(u16 *)(v0 + 0x19a) = a0 [SPU EON high (voices 16-23)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x100, set common attributes: f_5ba48(arg). Return 0.
	 */
$req_100_set_common:
	jal	f_5ba48	/* call f_5ba48 */
	move	a0, a2	/* delay slot (real work): a0 = a2 */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x101, get common attributes: f_5bde0(arg). Return 0.
	 */
$req_101_get_common:
	jal	f_5bde0	/* call f_5bde0 */
	move	a0, a2	/* delay slot (real work): a0 = a2 */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x204, key on: KON low = *(u16 *)arg; KON high = *(u8 *)(arg + 2) (direct writes, not OR). Return 0.
	 */
$req_204_key_on:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	nop	/* load delay slot */
	sh	v1, 0x188(v0)	/* *(u16 *)(v0 + 0x188) = v1 [SPU KON low (voice key on, voices 0-15)] */
	lbu	v1, 2(a2)	/* v1 = *(u8 *)(a2 + 2) */
	nop	/* load delay slot */
	sh	v1, 0x18a(v0)	/* *(u16 *)(v0 + 0x18a) = v1 [SPU KON high (voices 16-23)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Request 0x203, key off: KOFF low = *(u16 *)arg; KOFF high = *(u8 *)(arg + 2). Return 0.
	 */
$req_203_key_off:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 0(a2)	/* v1 = *(u16 *)(a2) */
	nop	/* load delay slot */
	sh	v1, 0x18c(v0)	/* *(u16 *)(v0 + 0x18c) = v1 [SPU KOFF low (voice key off, voices 0-15)] */
	lbu	v1, 2(a2)	/* v1 = *(u8 *)(a2 + 2) */
	nop	/* load delay slot */
	sh	v1, 0x18e(v0)	/* *(u16 *)(v0 + 0x18e) = v1 [SPU KOFF high (voices 16-23)] */
	j	$ioctl_return	/* goto $ioctl_return */
	move	v0, zero	/* delay slot (real work): v0 = zero */

	/*
	 * Requests 0x210 / 0x212 / 0x213, read the decoded-data capture area. a0 = dest, a1 = SPU address (8-byte units), a2 =
	 * block count of 64 bytes: 0x210 reads 0x40 blocks (0x1000 bytes) from SPU 0 into arg; 0x212 reads 0x20 blocks
	 * (0x800 bytes) from SPU 0 into arg; 0x213 reads 0x20 blocks from SPU address 0x100 (byte 0x800) into arg + 0x800.
	 * _spu_r_ starts the DMA; the result v0 is 1 when SPUSTAT bit 11 (capture buffer half) is clear.
	 */
$req_210_read_capture:
	move	a1, zero	/* a1 = zero */
	j	$L5b3a4	/* goto $L5b3a4 */
	ori	a2, zero, 0x40	/* delay slot (real work): a2 = 0x40 */
$req_212_read_capture_lo:
	j	$L5b3a0	/* goto $L5b3a0 */
	move	a1, zero	/* delay slot (real work): a1 = zero */
$req_213_read_capture_hi:
	ori	a1, zero, 0x100	/* a1 = 0x100 */
$L5b3a0:
	ori	a2, zero, 0x20	/* a2 = 0x20 */
$L5b3a4:
	jal	_spu_r_	/* call _spu_r_ */
	nop	/* delay slot: nop */
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot */
	lhu	v0, 0x1ae(v0)	/* v0 = *(u16 *)(v0 + 0x1ae) [SPU SPUSTAT (status)] */
	nop	/* load delay slot */
	andi	v0, v0, 0x800	/* v0 = v0 & 0x800 */
	j	$ioctl_return	/* goto $ioctl_return */
	sltiu	v0, v0, 1	/* delay slot (real work): v0 = ((u32)v0 < 1) */

	/*
	 * Timeout exit shared by the IRQ-enable polls: printf("SPU:T/O [%s]\n", "wait (ioctl/IRQ)"), then fall into $ioctl_ok.
	 */
$ioctl_timeout:
	la	a0, s_spu_timeout_fmt	/* a0 = &s_spu_timeout_fmt (lui+addiu macro) */
	la	a1, s_wait_ioctl_irq	/* a1 = &s_wait_ioctl_irq (lui+addiu macro) */
	jal	printf	/* call printf */
	nop	/* delay slot: nop */

	/*
	 * Success: v0 = 0. $ioctl_return: restore ra and the frame, return v0.
	 */
$ioctl_ok:
	move	v0, zero	/* v0 = zero */
$ioctl_return:
	lw	ra, 0x10(sp)	/* ra = *(u32 *)(sp + 0x10) */
	addiu	sp, sp, 0x18	/* sp = sp + 24 */
	jr	ra	/* return */
	nop	/* delay slot: nop */

/*
 * f_5b3f8 - write a set of voice attributes to the selected voices
 *
 * Purpose:     void f_5b3f8(SPU_VOICE_ATTR *attr). For every voice whose bit is set in attr->voice_mask,
 *              stores the attributes selected by attr->mask (0 = all) into that voice's 16-byte register
 *              block at _spu_RXX + 16 * voice.
 *              Record layout used here:
 *                +0x00 u32 voice mask, bit n = voice n (24 voices)
 *                +0x04 u32 attribute mask: 0 = everything; bit 0 volume L, 1 volume R, 2 pitch,
 *                      3 start address, 4 ADSR (both words), 8 repeat address
 *                +0x08 u16 volume left            -> voice +0x00 VOLL
 *                +0x0a u16 volume right           -> voice +0x02 VOLR
 *                +0x10 u16 pitch                  -> voice +0x04 PITCH
 *                +0x14 u32 start address          -> voice +0x06 SSA (u16 of 8-byte units); see below
 *                +0x18 u16 repeat address         -> voice +0x0e LSA
 *                +0x1c u16 ADSR upper word        -> voice +0x0a ADSR hi
 *                +0x1e u16 ADSR lower word        -> voice +0x08 ADSR lo
 *              Start address: with _spu_addrMode == 0 the low 16 bits are stored as they are (already
 *              in 8-byte units). Otherwise the value is a byte address: if it is not a multiple of
 *              _spu_mem_mode it is rounded up by adding _spu_mem_mode, then shifted right by
 *              _spu_mem_mode_plus.
 * Inputs:      a0 = attr.
 * Outputs:     none.
 * Clobbers:    v0, v1, a1, a2, a3, t0-t5. Leaf routine: no stack frame, no calls.
 * Hardware:    the 24 voice register blocks (0x1f801c00 + 16 * n), through _spu_RXX.
 * Reads:       _spu_RXX, _spu_mem_mode, _spu_addrMode, _spu_mem_mode_plus.
 * Callers:     _spu_init (jal 0x8005b3f8 at 0x80059c18).
 * Callees:     none.
 * Registers:   t2 = voice number, t4 = voice register block pointer (advanced by 0x10 per voice),
 *              a3 = register base, a2 = voice * 8, t1 = attribute mask, t0 = (attribute mask == 0),
 *              t5 = attribute mask & 0x10 (ADSR), t3 = _spu_mem_mode, a0 = attr.
 */
	.ent	f_5b3f8	/* a static: .ent gives it a local symbol record */
f_5b3f8:

	/*
	 * Setup: t2 = voice = 0; t1 = attr->mask; t3 = _spu_mem_mode; a3 = _spu_RXX; t0 = (mask == 0) "everything";
	 * t5 = mask & 0x10 (ADSR bit); t4 = a3 (voice 0's register block).
	 */
	move	t2, zero	/* t2 = zero */
	lw	t1, 4(a0)	/* t1 = *(u32 *)(a0 + 4) */
	lw	t3, _spu_mem_mode	/* t3 = _spu_mem_mode (lui+lw macro) */
	lw	a3, _spu_RXX	/* a3 = _spu_RXX (lui+lw macro) */
	sltiu	t0, t1, 1	/* t0 = ((u32)t1 < 1) */
	andi	t5, t1, 0x10	/* t5 = t1 & 0x10 */
	move	t4, a3	/* t4 = a3 */

	/*
	 * Per-voice loop head: v1 = attr->voice_mask; skip this voice when bit t2 of the mask is clear.
	 */
$L5b41c:
	ori	v0, zero, 1	/* v0 = 1 */
	lw	v1, 0(a0)	/* v1 = *(u32 *)(a0) */
	sllv	v0, v0, t2	/* v0 = v0 << t2 */
	and	v0, v0, v1	/* v0 = v0 & v1 */
	beq	v0, zero, $L5b560	/* if (v0 == 0) goto $L5b560 */
	nop	/* delay slot: nop */

	/*
	 * Volume left (mask bit 0 or all): voice VOLL = attr->vol_l (+0x08). a2 = voice * 8 for the other offsets.
	 */
	bne	t0, zero, $L5b448	/* if (t0 != 0) goto $L5b448 */
	sll	a2, t2, 3	/* delay slot (real work): a2 = t2 << 3 */
	andi	v0, t1, 1	/* v0 = t1 & 1 */
	beq	v0, zero, $L5b454	/* if (v0 == 0) goto $L5b454 */
	nop	/* delay slot: nop */
$L5b448:
	lhu	v0, 8(a0)	/* v0 = *(u16 *)(a0 + 8) */
	nop	/* load delay slot */
	sh	v0, 0(t4)	/* *(u16 *)(t4) = v0 */

	/*
	 * Volume right (bit 1 or all): voice + 2 = attr->vol_r (+0x0a); the address is a3 + (a2 << 1).
	 */
$L5b454:
	bne	t0, zero, $L5b468	/* if (t0 != 0) goto $L5b468 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
	andi	v0, t1, 2	/* v0 = t1 & 2 */
	beq	v0, zero, $L5b474	/* if (v0 == 0) goto $L5b474 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
$L5b468:
	lhu	v1, 0xa(a0)	/* v1 = *(u16 *)(a0 + 0xa) */
	addu	v0, v0, a3	/* v0 = v0 + a3 */
	sh	v1, 2(v0)	/* *(u16 *)(v0 + 2) = v1 */

	/*
	 * Pitch (bit 2 or all): voice + 4 = attr->pitch (+0x10).
	 */
$L5b474:
	bne	t0, zero, $L5b488	/* if (t0 != 0) goto $L5b488 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
	andi	v0, t1, 4	/* v0 = t1 & 4 */
	beq	v0, zero, $L5b494	/* if (v0 == 0) goto $L5b494 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
$L5b488:
	lhu	v1, 0x10(a0)	/* v1 = *(u16 *)(a0 + 0x10) */
	addu	v0, v0, a3	/* v0 = v0 + a3 */
	sh	v1, 4(v0)	/* *(u16 *)(v0 + 4) = v1 */

	/*
	 * Start address (bit 3 or all). _spu_addrMode == 0: store the low half of attr->addr (+0x14) as is.
	 */
$L5b494:
	bne	t0, zero, $L5b4a4	/* if (t0 != 0) goto $L5b4a4 */
	andi	v0, t1, 8	/* delay slot (real work): v0 = t1 & 8 */
	beq	v0, zero, $L5b508	/* if (v0 == 0) goto $L5b508 */
	nop	/* delay slot: nop */
$L5b4a4:
	lw	v0, _spu_addrMode	/* v0 = _spu_addrMode (lui+lw macro) */
	nop	/* load delay slot */
	bne	v0, zero, $L5b4c4	/* if (v0 != 0) goto $L5b4c4 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
	lhu	v1, 0x14(a0)	/* v1 = *(u16 *)(a0 + 0x14) */
	j	$L5b504	/* goto $L5b504 */
	addu	v0, v0, a3	/* delay slot (real work): v0 = v0 + a3 */

	/*
	 * _spu_addrMode != 0: a1 = attr->addr (byte address); with t3 = _spu_mem_mode != 0 and a1 % t3 != 0, a1 += t3
	 * (checked unsigned remainder: divide-by-zero trap, mfhi); v1 = a1 >> _spu_mem_mode_plus.
	 */
$L5b4c4:
	lw	a1, 0x14(a0)	/* a1 = *(u32 *)(a0 + 0x14) */
	beq	t3, zero, $L5b4f4	/* if (t3 == 0) goto $L5b4f4 */
	nop	/* delay slot: nop */
	/* v0 = a1 % t3: ASPSX 2.08's checked unsigned remainder macro, 5 words:
	   divu zero, a1, t3; bnez t3, 1f; nop; break 7 (divide by zero); 1: mfhi v0. */
	remu	v0, a1, t3	/* v0 = remainder */
	nop		/* hi read delay, as in retail */
	beq	v0, zero, $L5b4f4	/* if (v0 == 0) goto $L5b4f4 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
	addu	a1, a1, t3	/* a1 = a1 + t3 */
$L5b4f4:
	lw	v1, _spu_mem_mode_plus	/* v1 = _spu_mem_mode_plus (lui+lw macro) */
	addu	v0, v0, a3	/* v0 = v0 + a3 */
	srlv	v1, a1, v1	/* v1 = a1 >> (logical) v1 */

	/*
	 * Store the start address: voice + 6 (SSA).
	 */
$L5b504:
	sh	v1, 6(v0)	/* *(u16 *)(v0 + 6) = v1 */

	/*
	 * ADSR (bit 4 or all): voice + 8 (ADSR lo) = attr->adsr_lo (+0x1e).
	 */
$L5b508:
	bne	t0, zero, $L5b518	/* if (t0 != 0) goto $L5b518 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
	beq	t5, zero, $L5b524	/* if (t5 == 0) goto $L5b524 */
	nop	/* delay slot: nop */
$L5b518:
	lhu	v1, 0x1e(a0)	/* v1 = *(u16 *)(a0 + 0x1e) */
	addu	v0, v0, a3	/* v0 = v0 + a3 */
	sh	v1, 8(v0)	/* *(u16 *)(v0 + 8) = v1 */

	/*
	 * voice + 0xa (ADSR hi) = attr->adsr_hi (+0x1c).
	 */
$L5b524:
	bne	t0, zero, $L5b534	/* if (t0 != 0) goto $L5b534 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
	beq	t5, zero, $L5b540	/* if (t5 == 0) goto $L5b540 */
	nop	/* delay slot: nop */
$L5b534:
	lhu	v1, 0x1c(a0)	/* v1 = *(u16 *)(a0 + 0x1c) */
	addu	v0, v0, a3	/* v0 = v0 + a3 */
	sh	v1, 0xa(v0)	/* *(u16 *)(v0 + 0xa) = v1 */

	/*
	 * Repeat address (bit 8 or all): voice + 0xe (LSA) = attr->repeat (+0x18).
	 */
$L5b540:
	bne	t0, zero, $L5b554	/* if (t0 != 0) goto $L5b554 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
	andi	v0, t1, 0x100	/* v0 = t1 & 0x100 */
	beq	v0, zero, $L5b560	/* if (v0 == 0) goto $L5b560 */
	sll	v0, a2, 1	/* delay slot (real work): v0 = a2 << 1 */
$L5b554:
	lhu	v1, 0x18(a0)	/* v1 = *(u16 *)(a0 + 0x18) */
	addu	v0, v0, a3	/* v0 = v0 + a3 */
	sh	v1, 0xe(v0)	/* *(u16 *)(v0 + 0xe) = v1 */

	/*
	 * Next voice: t2++, t4 += 0x10; loop while t2 < 24. Return.
	 */
$L5b560:
	addiu	t2, t2, 1	/* t2 = t2 + 1 */
	slti	v0, t2, 0x18	/* v0 = (t2 < 24) */
	bne	v0, zero, $L5b41c	/* if (v0 != 0) goto $L5b41c */
	addiu	t4, t4, 0x10	/* delay slot (real work): t4 = t4 + 16 */
	jr	ra	/* return */
	nop	/* delay slot: nop */
	.end	f_5b3f8
