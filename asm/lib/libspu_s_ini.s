/*
 * libspu_s_ini.s - LIBSPU module S_INI: _SpuInit, SpuStart and the SPU library's state words.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_ini.json and the
 * "libspu_s_ini" entry of config/data.json; counted as "library assembly", never as matched C
 * and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_INI.
 * Exports:     _SpuInit (.text+0x0, 228 bytes), SpuStart (.text+0xe4, 92 bytes), and the data
 *              words _spu_fd, _spu_trans_mode, _spu_rev_flag, _spu_rev_reserve_wa,
 *              _spu_rev_attr, _spu_rev_offsetaddr, _spu_voice_centerNote, _spu_keystat,
 *              _spu_EVdma, _spu_keyevent_time, _spu_keyon_bit, _spu_keyoff_bit, _spu_zerobuf,
 *              _spu_isCalled (.data+0x0 .. +0x470, 1136 bytes, 0x80073cf4 .. 0x80074163).
 * Imports:     ResetCallback (0x800616d0), _spu_init (0x80059a14), _spu_open (0x80059fec),
 *              EnterCriticalSection (0x8005d0c0), ExitCriticalSection (0x8005d2e0),
 *              _spu_ioctl (0x8005ab0c), _spu_rev_startaddr (0x80074164, the S_SRMP/S_CRWA
 *              table of reverb start addresses).
 * Own data:    the .data above.
 *
 * Nearest archived version: Runtime Library 3.3 LIBSPU.LIB S_INI. Its .data (1136 bytes, the
 * same fourteen exports at the same offsets) is byte-identical to retail's
 * (analysis/data_survey/moddiff.py: 0 differing bytes). The text differs completely: the 3.3
 * member is 332 bytes (_SpuInit 200 + SpuStart 132) and drives the SPU registers directly
 * (SpuStart opens event and DMA callbacks: OpenEvent / EnableEvent / _spu_FiDMA), whereas
 * retail's is 320 bytes (_SpuInit 228 + SpuStart 92) and goes through the device layer:
 * _spu_open / _spu_ioctl (the interface the 2.6 / 3.0 / ISA S_INI members use). Neither the
 * 3.0 member (524 text bytes, 1024-byte .data) nor any other archived S_INI matches, so this
 * is "Runtime 3.3 S_INI data with an ioctl-based _SpuInit/SpuStart".
 *
 * The routines are compiled C (GCC register use, delay slots filled by the compiler); they
 * are transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

/*
 * The module's .data, 0x80073cf4 .. 0x80074163 (1136 bytes), identical to the Runtime 3.3
 * S_INI .data. Everything is zero except _spu_voice_centerNote; _SpuInit and SpuStart set the
 * words that need other values.
 */
	.data

/*
 * _spu_fd - the file descriptor of the SPU device, set by SpuStart from _spu_open(0, 3).
 * Users: S_STM, S_SRV, S_STSA, S_SK, S_SR, S_CRWA, S_INI.
 */
	.globl	_spu_fd
_spu_fd:
	.word	0

/*
 * _spu_trans_mode - the transfer mode given to SpuSetTransferMode; _SpuInit sets 0.
 * Users: S_STM, S_ITC, S_INI.
 */
	.globl	_spu_trans_mode
_spu_trans_mode:
	.word	0

/*
 * _spu_rev_flag - reverb on (1) / off (0), as passed to the driver by SpuSetReverb.
 * Users: S_SR, S_INI.
 */
	.globl	_spu_rev_flag
_spu_rev_flag:
	.word	0

/*
 * _spu_rev_reserve_wa - 1 when the reverb work area is reserved out of SpuMalloc's range.
 * Users: S_SR, S_M_M, S_INI.
 */
	.globl	_spu_rev_reserve_wa
_spu_rev_reserve_wa:
	.word	0

/*
 * _spu_rev_attr - the current reverb attributes (20 bytes), laid out like SpuReverbAttr:
 * mask, mode, depth.left (short), depth.right (short), delay, feedback. _SpuInit clears the
 * mode, depths, delay and feedback words.
 * Users: S_INI.
 */
	.globl	_spu_rev_attr
_spu_rev_attr:
	.word	0		/* +0  mask */
	.word	0		/* +4  mode */
	.half	0		/* +8  depth.left */
	.half	0		/* +10 depth.right */
	.word	0		/* +12 delay */
	.word	0		/* +16 feedback */

/*
 * _spu_rev_offsetaddr - SPU RAM address of the reverb work area, in 8-byte units; _SpuInit
 * sets _spu_rev_startaddr[mode 0].
 * Users: S_SR, S_M_M, S_INI.
 */
	.globl	_spu_rev_offsetaddr
_spu_rev_offsetaddr:
	.word	0

/*
 * _spu_voice_centerNote - one halfword per voice (24), every one 0xc000 at start.
 * Users: none among the modules of this lane.
 */
	.globl	_spu_voice_centerNote
_spu_voice_centerNote:
	.half	0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000
	.half	0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000
	.half	0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000, 0xc000

/*
 * _spu_keystat - the voices currently keyed on: written by SpuSetKey, read by
 * SpuGetKeyStatus; _SpuInit clears it.
 * Users: S_SK, S_GKS, S_INI.
 */
	.globl	_spu_keystat
_spu_keystat:
	.word	0

/*
 * _spu_EVdma - DMA-busy word: SpuStart clears it; SpuIsTransferCompleted and
 * SpuClearReverbWorkArea spin until it reads 0.
 * Users: S_ITC, S_CRWA, S_INI.
 */
	.globl	_spu_EVdma
_spu_EVdma:
	.word	0

/*
 * _spu_keyevent_time - zero at start; not referenced by the modules of this lane.
 */
	.globl	_spu_keyevent_time
_spu_keyevent_time:
	.word	0

/* _spu_keyon_bit - voices queued for key-on; _SpuInit clears it. Users: S_INI. */
	.globl	_spu_keyon_bit
_spu_keyon_bit:
	.word	0

/* _spu_keyoff_bit - voices queued for key-off; _SpuInit clears it. Users: S_INI. */
	.globl	_spu_keyoff_bit
_spu_keyoff_bit:
	.word	0

/*
 * _spu_zerobuf - 1024 bytes of zeros: the source of SpuClearReverbWorkArea's DMA writes
 * (one 0x400-byte chunk per pass).
 * Users: S_CRWA.
 */
	.globl	_spu_zerobuf
_spu_zerobuf:
	.space	1024

/* _spu_isCalled - SpuStart sets it to 1 so the device is opened once. Users: S_INI. */
	.globl	_spu_isCalled
_spu_isCalled:
	.word	0

	.text

/*
 * _SpuInit - initialise the SPU library (called by SpuInit)
 *
 * Purpose:     void _SpuInit(long mode). Resets the BIOS callback tables (ResetCallback),
 *              initialises the SPU hardware layer (_spu_init(mode)), opens the device
 *              (SpuStart), then puts the library state back to its defaults:
 *                _spu_rev_attr.mode/depth/delay/feedback = 0, _spu_rev_flag = 0,
 *                _spu_rev_reserve_wa = 0, _spu_rev_offsetaddr = _spu_rev_startaddr[0]
 *                (mode 0, reverb off), passed to the driver with ioctl 0x61;
 *                _spu_trans_mode = 0, passed to the driver with ioctl 0x21;
 *                _spu_keystat = _spu_keyon_bit = _spu_keyoff_bit = 0.
 * Inputs:      a0 = mode (kept in s0 for the call to _spu_init).
 * Outputs:     none.
 * Clobbers:    v0, v1, a0..a2, AT (caller-saved) and, through the calls, whatever they
 *              clobber. s0 is saved and restored.
 * Stack:       0x18-byte frame: s0 at 0x10, ra at 0x14.
 * Callees:     ResetCallback, _spu_init, SpuStart, _spu_ioctl (twice).
 * Callers:     SpuInit (module S_I, 0x8005c718), at 0x8005c720.
 */
	.globl	_SpuInit
_SpuInit:
	/* prologue; reset callbacks, initialise the hardware layer, open the device */
	addiu	sp, sp, -24	/* allocate the 0x18-byte frame */
	sw	s0, 16(sp)	/* save s0 */
	sw	ra, 20(sp)	/* save ra */
	jal	ResetCallback	/* ResetCallback(): reset the BIOS event/callback tables */
	addu	s0, a0, zero	/* delay slot (real work): s0 = mode */
	jal	_spu_init	/* _spu_init(mode): initialise the SPU hardware layer */
	addu	a0, s0, zero	/* delay slot (real work): a0 = mode */
	jal	SpuStart	/* SpuStart(): open the device */
	nop	/* call delay: nothing to do */
	/* default reverb state: mode 0, everything zero, work area = _spu_rev_startaddr[mode] */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro), the descriptor for the later ioctls */
	sw	zero, _spu_rev_attr+4	/* macro "sw zero, _spu_rev_attr+4": lui AT then the store through AT; mode = 0 */
	lw	v0, _spu_rev_attr+4	/* v0 = _spu_rev_attr.mode (lui+lw macro), the index into the start address table */
	sh	zero, _spu_rev_attr+8	/* macro "sh zero, _spu_rev_attr+8": depth.left = 0 */
	sh	zero, _spu_rev_attr+10	/* macro "sh zero, _spu_rev_attr+10": depth.right = 0 */
	sw	zero, _spu_rev_attr+12	/* macro "sw zero, _spu_rev_attr+12": delay = 0 */
	sw	zero, _spu_rev_attr+16	/* macro "sw zero, _spu_rev_attr+16": feedback = 0 */
	sll	v0, v0, 2	/* v0 = mode * 4 */
	.set	noat	/* the sequence below names $at (register 1) explicitly */
	la	$1, _spu_rev_startaddr	/* AT = &_spu_rev_startaddr[0] (la macro: lui + addiu) ... */
	addu	$1, $1, v0	/* ... AT = &_spu_rev_startaddr[mode] */
	lw	v0, 0($1)	/* v0 = _spu_rev_startaddr[mode] */
	.set	at
	la	a2, _spu_rev_offsetaddr	/* ... (la macro: lui + addiu) */
	sw	zero, _spu_rev_flag	/* macro "sw zero, _spu_rev_flag": reverb off */
	sw	zero, _spu_rev_reserve_wa	/* macro "sw zero, _spu_rev_reserve_wa": work area not reserved */
	sw	v0, _spu_rev_offsetaddr	/* macro "sw v0, _spu_rev_offsetaddr": the default work area address */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x61, &_spu_rev_offsetaddr): tell the driver */
	ori	a1, zero, 0x61	/* delay slot (real work): a1 = ioctl command 0x61 (set reverb work area address) */
	/* default transfer mode: 0, told to the driver */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	la	a2, _spu_trans_mode	/* a2 = &_spu_trans_mode (la macro: lui + addiu) */
	sw	zero, _spu_trans_mode	/* macro "sw zero, _spu_trans_mode": transfer mode 0 */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x21, &_spu_trans_mode): tell the driver */
	ori	a1, zero, 0x21	/* delay slot (real work): a1 = ioctl command 0x21 (set transfer mode) */
	/* no voices keyed */
	sw	zero, _spu_keystat	/* macro "sw zero, _spu_keystat": no voice keyed on */
	sw	zero, _spu_keyon_bit	/* macro "sw zero, _spu_keyon_bit" */
	sw	zero, _spu_keyoff_bit	/* macro "sw zero, _spu_keyoff_bit" */
	/* epilogue */
	lw	ra, 20(sp)	/* restore ra */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 24	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */

/*
 * SpuStart - open the SPU device the first time (called by _SpuInit)
 *
 * Purpose:     void SpuStart(void). On the first call (_spu_isCalled == 0): sets
 *              _spu_isCalled = 1; with interrupts off (EnterCriticalSection /
 *              ExitCriticalSection) opens the SPU device, _spu_fd = _spu_open(0, 3), and
 *              clears _spu_EVdma. Later calls do nothing.
 * Inputs:      none.
 * Outputs:     none.
 * Clobbers:    v0, a0, a1 (caller-saved) and, through the calls, whatever they clobber.
 * Stack:       0x18-byte frame: ra at 0x10.
 * Callees:     EnterCriticalSection, _spu_open, ExitCriticalSection.
 * Callers:     _SpuInit, at 0x8005c510.
 */
	.globl	SpuStart
SpuStart:
	/* only the first call does anything */
	lw	v0, _spu_isCalled	/* v0 = _spu_isCalled (lui+lw macro) */
	addiu	sp, sp, -24	/* allocate the 0x18-byte frame (fills the load delay slot) */
	bne	v0, zero, $return	/* already started: return */
	sw	ra, 16(sp)	/* delay slot (real work): save ra */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sw	v0, _spu_isCalled	/* macro "sw v0, _spu_isCalled": lui AT then the store through AT */
	/* open the device with interrupts off, and mark DMA idle */
	jal	EnterCriticalSection	/* EnterCriticalSection() */
	nop	/* call delay: nothing to do */
	addu	a0, zero, zero	/* a0 = 0: device number */
	jal	_spu_open	/* _spu_open(0, 3) */
	ori	a1, zero, 0x3	/* delay slot (real work): a1 = 3: open flags */
	sw	v0, _spu_fd	/* macro "sw v0, _spu_fd": the descriptor */
	sw	zero, _spu_EVdma	/* macro "sw zero, _spu_EVdma": no DMA in flight */
	jal	ExitCriticalSection	/* ExitCriticalSection() */
	nop	/* call delay: nothing to do */
$return:
	/* epilogue */
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
