/*
 * libspu_s_srmp.s - LIBSPU module S_SRMP: SpuSetReverbModeParam.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Placed by
 * config/libspu_s_srmp.json and the "libspu_s_srmp" entry of config/data.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Assembler: ASPSX 2.08 (A208, config/asm_build.json), not the project's ASPSX 2.34.
 * ASPSX 2.34 inserts a hazard nop between "sll a1, a0, 12" and "mult a1, a2" at 0x800595a4
 * (a mult two instructions after an mfhi) even under .set noreorder, which retail does not
 * have (1304 bytes instead of 1300). ASPSX 2.08, the Runtime 3.0-era assembler that the
 * variant SPU modules' overflow traps point to (libspu_spu.s), inserts none, and its indexed
 * load macro gives retail's lui/addiu/addu/lw table read.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_SRMP.
 * Exports:     SpuSetReverbModeParam (.text+0x0, 1300 bytes).
 * Imports:     _spu_rev_attr, _spu_rev_offsetaddr (S_INI .data, 0x80073d04, 0x80073d18),
 *              _spu_fd (S_INI .data, 0x80073cf4), _spu_rev_startaddr (0x80074164, ten start
 *              addresses), g_741b4 (0x800741b4, ten 68-byte reverb parameter blocks, one per
 *              mode), _spu_ioctl (0x8005ab0c), _SpuIsInAllocateArea_ (0x8005990c),
 *              SpuClearReverbWorkArea (0x8005c330, module S_CRWA).
 * Own data:    _spu_RXX, a module-local copy of the SPU register base pointer (.data+0x0 =
 *              0x1f801c00), at 0x80073c84. (_spu_rev_startaddr, the parameter blocks and the
 *              work-area sizes that follow them at 0x80074164 .. 0x8007447b belong to this
 *              module's archive original but lie outside the library-assembly data ranges.)
 *
 * Nearest archived version: Runtime Library 4.0 (USA and Japan) LIBSPU.LIB S_SRMP (1272
 * bytes; the 3.0 / ISA members are 1312 bytes with a 760-byte .data, 3.3 - 3.6 are 2528).
 * Relocation-masked, 158 of retail's 325 words are identical to the 4.0 member
 * (builds/lane_tools/best.py), the rest being the ioctl-based register access of the older
 * design: retail reads the reverb parameter block of the chosen mode, patches the echo/delay
 * values, and passes it to the driver (ioctl 0x63), where later libraries write the SPU
 * registers directly through _spu_RXX. Like the 3.0 member it calls _spu_ioctl and
 * _SpuIsInAllocateArea_, and unlike it keeps the parameter table outside the module. So no
 * archive matches: "pre-3.3 ioctl-based S_SRMP with a module-local _spu_RXX". The module-
 * local _spu_RXX copies are explained in libspu_s_gks.s.
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

/*
 * _spu_RXX - module-local SPU register base
 *
 * Purpose:     the address of the SPU register file (0x1f801c00), read through a pointer as
 *              all LIBSPU code does. Static: the retail module has its own word.
 * Layout:      one 32-bit pointer.
 * Users:       SpuSetReverbModeParam only (reverb depth registers 0x184 / 0x186).
 */
	.data
_spu_RXX:
	.word	0x1f801c00	/* SPU register file base (volatile u_short *) */

	.text

/*
 * SpuSetReverbModeParam - set the reverb mode and its parameters
 *
 * Purpose:     long SpuSetReverbModeParam(SpuReverbAttr *attr). attr (libspu.h):
 *                +0x00 mask   +0x04 mode   +0x08 depth.left   +0x0a depth.right
 *                +0x0c delay   +0x10 feedback
 *              mask bits: 0x1 mode, 0x2 depth.left, 0x4 depth.right, 0x8 delay, 0x10
 *              feedback; 0 means all. mode bit 0x100 (SPU_REV_MODE_CLEAR_WA) additionally
 *              clears the reverb work area afterwards (SpuClearReverbWorkArea).
 *              Mode (0..9, SPU_REV_MODE_OFF .. SPU_REV_MODE_PIPE): rejected with -1 when
 *              >= 10 or when its work area, _spu_rev_startaddr[mode], lies inside a block
 *              handed out by SpuMalloc (_SpuIsInAllocateArea_). A valid mode becomes
 *              _spu_rev_attr.mode and _spu_rev_offsetaddr = _spu_rev_startaddr[mode]; the
 *              mode's 68-byte parameter block is copied to a local buffer; delay and
 *              feedback take their defaults (echo mode 7: both 0x7f; delay mode 8: delay
 *              0x7f, feedback 0; otherwise 0). For echo/delay modes (7, 8) the delay and
 *              feedback of attr are then scaled (x / 127 fixed-point) and added into
 *              16-bit entries of the local block, and the new values are kept in
 *              _spu_rev_attr. Depths are written to the SPU registers 0x184 / 0x186 and
 *              kept in _spu_rev_attr (zeroed while the mode changes). When anything of
 *              the block changed the driver gets it with ioctl 0x63; when the mode
 *              changed the reverb is switched off around the change (ioctl 0x6a reads the
 *              reverb state, 0x60 writes it) and the work area address is sent with
 *              ioctl 0x61.
 * Inputs:      a0 = attr.
 * Outputs:     v0 = 0 on success, -1 for a bad mode or a work area inside a SpuMalloc block.
 * Clobbers:    v0, v1, a0..a3 (caller-saved) and, through the calls, whatever they clobber.
 *              s0..s7 and fp are saved and restored.
 * Stack:       0x90-byte frame: the local parameter block at 0x10 .. 0x54 (68 bytes),
 *              reverb state word 0x58, a zero word 0x5c, saved registers s0 0x68 .. fp
 *              0x88, ra 0x8c.
 * Registers:   s2 = attr; s3 = attr->mask; s5 = (mask == 0); s0 = mode; s1 = &_spu_rev_
 *              startaddr; s4 = 1 when the mode was set this call; fp = 1 when the work
 *              area is to be cleared; s6 / s7 = 1 when the delay / feedback was handled.
 * Callees:     _SpuIsInAllocateArea_, _spu_ioctl, SpuClearReverbWorkArea.
 * Callers:     spu_init (0x80044138, config/audio.json), at 0x800441ac.
 */
	.globl	SpuSetReverbModeParam
SpuSetReverbModeParam:
	/* prologue; s3 = mask, s5 = (mask == 0) */
	addiu	sp, sp, -144	/* allocate the 0x90-byte frame */
	sw	s2, 112(sp)
	move	s2, a0	/* s2 = attr */
	sw	fp, 136(sp)
	move	fp, zero	/* fp = 0: do not clear the work area */
	sw	s4, 120(sp)
	move	s4, zero	/* s4 = 0: mode not set */
	sw	s6, 128(sp)
	move	s6, zero	/* s6 = 0: delay not handled */
	sw	ra, 140(sp)
	sw	s7, 132(sp)
	sw	s5, 124(sp)
	sw	s3, 116(sp)
	sw	s1, 108(sp)
	sw	s0, 104(sp)
	lw	s3, 0(s2)	/* s3 = attr->mask */
	move	s7, zero	/* s7 = 0: feedback not handled */
	sltiu	s5, s3, 1	/* s5 = (mask == 0), "set everything" */
	bne	s5, zero, $L005c	/* mask == 0: take the mode */
	sw	zero, 16(sp)	/* delay slot (real work): local parameter buffer word 0 = 0 */
	andi	v0, s3, 0x1	/* v0 = mask & mode bit */
	beq	v0, zero, $delay	/* mode not selected: skip to the depths/delay */
	nop	/* delay slot: nothing to do */
$L005c:
	/* mode (mask bit 0x1): take attr->mode, strip the clear-work-area flag, validate */
	lw	s0, 4(s2)	/* s0 = attr->mode */
	nop	/* load delay slot: nothing to do */
	andi	v0, s0, 0x100	/* v0 = mode & SPU_REV_MODE_CLEAR_WA */
	beq	v0, zero, $L0078	/* flag not set */
	addiu	v0, zero, -257	/* delay slot (real work): v0 = ~0x100 */
	and	s0, s0, v0	/* mode &= ~0x100 */
	ori	fp, zero, 0x1	/* fp = 1: clear the work area at the end */
$L0078:
	sltiu	v0, s0, 10	/* v0 = (mode < 10) */
	beq	v0, zero, $fail	/* bad mode: fail */
	sll	v0, s0, 2	/* delay slot (real work): v0 = mode * 4 */
	lw	a0, _spu_rev_startaddr(v0)	/* a0 = _spu_rev_startaddr[mode] (macro: lui at + addiu + addu + lw) */
	la	s1, _spu_rev_startaddr	/* s1 = &_spu_rev_startaddr[0] (la: lui + addiu) */
	jal	_SpuIsInAllocateArea_	/* _SpuIsInAllocateArea_(start address) */
	nop	/* call delay: nothing to do */
	beq	v0, zero, $L00b4	/* not in a SpuMalloc block: go on */
	ori	s4, zero, 0x1	/* delay slot (real work): s4 = 1, the mode is set this call */
$fail:
	j	$return	/* fail: return -1 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
$L00b4:
	/* commit the mode: _spu_rev_attr.mode, the work area address, a local copy of the
	   mode's 68-byte parameter block; delay/feedback defaults by mode (7 echo, 8 delay) */
	addiu	a2, sp, 16	/* a2 = &local parameter buffer */
	ori	a1, zero, 0x43	/* a1 = 0x43: copy 68 bytes (counts down to -1) */
	sw	s0, _spu_rev_attr+4	/* macro "sw s0, _spu_rev_attr+4": _spu_rev_attr.mode = mode */
	lw	v1, _spu_rev_attr+4	/* v1 = _spu_rev_attr.mode (lui+lw macro) */
	addiu	a3, zero, -1	/* a3 = -1, the loop end value */
	sll	a0, v1, 2	/* a0 = mode * 4 */
	addu	a0, a0, s1	/* a0 = &_spu_rev_startaddr[mode] */
	sll	v0, v1, 4	/* v0 = mode * 16 */
	addu	v0, v0, v1	/* v0 = mode * 17 */
	sll	v0, v0, 2	/* v0 = mode * 68, the parameter block offset */
	la	v1, g_741b4	/* v1 = &g_741b4 (la: lui + addiu) */
	lw	a0, 0(a0)	/* a0 = _spu_rev_startaddr[mode] */
	addu	v1, v0, v1	/* v1 = &parameter block of the mode */
	sw	a0, _spu_rev_offsetaddr	/* macro "sw a0, _spu_rev_offsetaddr": the work area address */
$copy:
	lbu	v0, 0(v1)	/* copy loop: *a2++ = *v1++ for 68 bytes */
	addiu	v1, v1, 1
	addiu	a1, a1, -1
	sb	v0, 0(a2)
	bne	a1, a3, $copy
	addiu	a2, a2, 1	/* delay slot (real work): a2++ */
	lw	v1, _spu_rev_attr+4	/* v1 = _spu_rev_attr.mode (lui+lw macro) */
	ori	v0, zero, 0x7	/* v0 = 7 (echo) */
	beq	v1, v0, $L0138	/* echo mode */
	ori	v0, zero, 0x8	/* delay slot (real work): v0 = 8 (delay) */
	beq	v1, v0, $L0154	/* delay mode */
	ori	v0, zero, 0x7f	/* delay slot (real work): v0 = 0x7f */
	j	$L016c	/* other modes: defaults 0 */
	nop	/* delay slot: nothing to do */
$L0138:
	ori	v0, zero, 0x7f	/* echo: v0 = 0x7f */
	sw	v0, _spu_rev_attr+16	/* macro "sw v0, _spu_rev_attr+16": feedback = 0x7f */
	sw	v0, _spu_rev_attr+12	/* macro "sw v0, _spu_rev_attr+12": delay = 0x7f */
	j	$delay	/* join */
	nop	/* delay slot: nothing to do */
$L0154:
	sw	zero, _spu_rev_attr+16	/* delay mode: macro "sw zero, _spu_rev_attr+16": feedback = 0 */
	sw	v0, _spu_rev_attr+12	/* macro "sw v0, _spu_rev_attr+12": delay = 0x7f */
	j	$delay	/* join */
	nop	/* delay slot: nothing to do */
$L016c:
	sw	zero, _spu_rev_attr+16	/* other modes: feedback = 0 */
	sw	zero, _spu_rev_attr+12	/* delay = 0 */
$delay:
	/* delay (mask bit 0x8): only for the echo and delay modes (7, 8): rescale delay by 1/127
	   into the local parameter block */
	bne	s5, zero, $L018c	/* mask == 0: handle the delay */
	andi	v0, s3, 0x8	/* delay slot (real work): v0 = mask & delay bit */
	beq	v0, zero, $feedback	/* not selected: skip to the feedback */
	nop	/* delay slot: nothing to do */
$L018c:
	lw	v1, _spu_rev_attr+4	/* v1 = _spu_rev_attr.mode (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	slti	v0, v1, 9	/* v0 = (mode < 9) */
	beq	v0, zero, $L029c	/* mode >= 9: delay not applicable */
	slti	v0, v1, 7	/* delay slot (real work): v0 = (mode < 7) */
	bne	v0, zero, $L029c	/* mode < 7: not applicable */
	nop	/* delay slot: nothing to do */
	bne	s4, zero, $L0204	/* mode set this call: the block is already local */
	ori	s6, zero, 0x1	/* delay slot (real work): s6 = 1, delay handled */
	addiu	a1, sp, 16	/* mode not set: a1 = &local buffer */
	ori	a0, zero, 0x43	/* a0 = 0x43: copy count */
	lw	v0, _spu_rev_attr+4	/* v0 = _spu_rev_attr.mode (lui+lw macro) */
	addiu	a2, zero, -1	/* a2 = -1 */
	sll	v1, v0, 4	/* v1 = mode * 16 */
	addu	v1, v1, v0	/* v1 = mode * 17 */
	sll	v1, v1, 2	/* v1 = mode * 68 */
	la	v0, g_741b4	/* v0 = &g_741b4 (la: lui + addiu) */
	addu	v1, v1, v0	/* v1 = parameter block */
$copy2:
	lbu	v0, 0(v1)	/* copy loop: *a1++ = *v1++ for 68 bytes */
	addiu	v1, v1, 1
	addiu	a0, a0, -1
	sb	v0, 0(a1)
	bne	a0, a2, $copy2	/* loop until a0 == -1 */
	addiu	a1, a1, 1	/* delay slot (real work): a1++ */
	lui	v0, 0xc01	/* v0 = 0x0c011c00 ... */
	ori	v0, v0, 0x1c00
	sw	v0, 16(sp)	/* ... local word 0 = 0x0c011c00 (block header for the delay patch) */
$L0204:
	lui	a2, 0x8102	/* a2 = 0x81020409: 2^38 / 127 reciprocal for x / 127 */
	lw	a0, 12(s2)	/* a0 = attr->delay */
	ori	a2, a2, 0x409	/* a2 low half */
	sll	v1, a0, 13	/* v1 = delay << 13 */
	mult	v1, a2	/* hi(v1 * reciprocal) ... */
	mfhi	v0	/* ... v0 = high word */
	sll	a1, a0, 12	/* a1 = delay << 12 */
	mult	a1, a2	/* hi/lo of a1 * reciprocal */
	sw	a0, _spu_rev_attr+12	/* macro "sw a0, _spu_rev_attr+12": _spu_rev_attr.delay = delay */
	lhu	a0, 20(sp)	/* a0 = local[4..5] (halfword 0x14(sp)) */
	addu	v0, v0, v1	/* + v1: signed correction term */
	sra	v0, v0, 6	/* arithmetic shift by 6: (delay << 13) / 127 */
	sra	v1, v1, 31	/* v1 = sign of (delay << 13) */
	subu	v0, v0, v1	/* v0 = quotient (rounded toward zero) */
	subu	v0, v0, a0	/* minus the original entry */
	sh	v0, 40(sp)	/* local halfword at 0x28(sp) = result */
	lhu	v0, 22(sp)	/* v0 = local halfword 0x16(sp) */
	lhu	a0, 54(sp)	/* a0 = local halfword 0x36(sp) */
	mfhi	v1	/* v1 = high word of the second multiply */
	addu	v1, v1, a1	/* + a1 */
	sra	v1, v1, 6	/* shift by 6: (delay << 12) / 127 */
	sra	a1, a1, 31	/* a1 = sign of (delay << 12) */
	subu	v1, v1, a1	/* v1 = quotient, rounded toward zero */
	subu	v0, v1, v0	/* v0 = quotient - local[0x16] */
	sh	v0, 42(sp)	/* local halfword at 0x2a(sp) = result */
	lhu	v0, 46(sp)	/* v0 = local halfword 0x2e(sp) */
	addu	a0, v1, a0	/* a0 = quotient + local[0x36] */
	sh	a0, 52(sp)	/* local halfword at 0x34(sp) = result */
	addu	v0, v1, v0	/* v0 = quotient + local[0x2e] */
	sh	v0, 44(sp)	/* local halfword at 0x2c(sp) = result */
	lhu	v0, 76(sp)	/* v0 = local halfword 0x4c(sp) */
	lhu	a0, 78(sp)	/* a0 = local halfword 0x4e(sp) */
	addu	v0, v1, v0	/* v0 = quotient + local[0x4c] */
	addu	v1, v1, a0	/* v1 = quotient + local[0x4e] */
	sh	v0, 72(sp)	/* local halfword at 0x48(sp) = result */
	j	$feedback	/* go to the feedback */
	sh	v1, 74(sp)	/* delay slot (real work): local halfword at 0x4a(sp) = result */
$L029c:
	sw	zero, _spu_rev_attr+12	/* mode not echo/delay: _spu_rev_attr.delay = 0 */
$feedback:
	/* feedback (mask bit 0x10): echo and delay modes only: feedback * 129 / 127 scaled */
	bne	s5, zero, $L02b4	/* mask == 0: handle the feedback */
	andi	v0, s3, 0x10	/* delay slot (real work): v0 = mask & feedback bit */
	beq	v0, zero, $depths	/* not selected: skip to the depths */
	nop	/* delay slot: nothing to do */
$L02b4:
	lw	v1, _spu_rev_attr+4	/* v1 = _spu_rev_attr.mode (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	slti	v0, v1, 9	/* v0 = (mode < 9) */
	beq	v0, zero, $L037c	/* mode >= 9: not applicable */
	slti	v0, v1, 7	/* delay slot (real work): v0 = (mode < 7) */
	bne	v0, zero, $L037c	/* mode < 7: not applicable */
	nop	/* delay slot: nothing to do */
	bne	s4, zero, $L033c	/* mode set this call: the block is already local */
	ori	s7, zero, 0x1	/* delay slot (real work): s7 = 1, feedback handled */
	bne	s6, zero, $L032c	/* delay already copied the block: reuse it */
	addiu	a1, sp, 16	/* delay slot (real work): a1 = &local buffer */
	ori	a0, zero, 0x43	/* a0 = 0x43: copy count */
	lw	v0, _spu_rev_attr+4	/* v0 = _spu_rev_attr.mode (lui+lw macro) */
	addiu	a2, zero, -1	/* a2 = -1 */
	sll	v1, v0, 4	/* v1 = mode * 16 */
	addu	v1, v1, v0	/* v1 = mode * 17 */
	sll	v1, v1, 2	/* v1 = mode * 68 */
	la	v0, g_741b4	/* v0 = &g_741b4 (la: lui + addiu) */
	addu	v1, v1, v0	/* v1 = parameter block */
$copy3:
	lbu	v0, 0(v1)	/* copy loop: *a1++ = *v1++ for 68 bytes */
	addiu	v1, v1, 1
	addiu	a0, a0, -1
	sb	v0, 0(a1)
	bne	a0, a2, $copy3	/* loop until a0 == -1 */
	addiu	a1, a1, 1	/* delay slot (real work): a1++ */
	j	$L0338	/* join */
	ori	v0, zero, 0x80	/* delay slot (real work): v0 = 0x80 */
$L032c:
	lw	v0, 16(sp)	/* v0 = local word 0 */
	nop	/* load delay slot: nothing to do */
	ori	v0, v0, 0x80	/* local word 0 |= 0x80 */
$L0338:
	sw	v0, 16(sp)	/* store local word 0 */
$L033c:
	lui	a0, 0x8102	/* a0 = 0x81020409 ... */
	lw	v0, 16(s2)	/* v0 = attr->feedback */
	ori	a0, a0, 0x409	/* ... reciprocal of 127 */
	sll	v1, v0, 7	/* v1 = feedback << 7 */
	addu	v1, v1, v0	/* v1 = feedback * 129 */
	sll	v1, v1, 8	/* v1 = feedback * 129 << 8 */
	mult	v1, a0	/* hi/lo of v1 * reciprocal */
	sw	v0, _spu_rev_attr+16	/* macro "sw v0, _spu_rev_attr+16": _spu_rev_attr.feedback = feedback */
	mfhi	v0	/* v0 = high word */
	addu	v0, v0, v1	/* + v1 */
	sra	v0, v0, 6	/* shift by 6: quotient by 127 */
	sra	v1, v1, 31	/* v1 = sign */
	subu	v0, v0, v1	/* v0 = quotient rounded toward zero */
	j	$depths	/* join */
	sh	v0, 34(sp)	/* delay slot (real work): local halfword at 0x22(sp) = feedback value */
$L037c:
	sw	zero, _spu_rev_attr+16	/* mode not echo/delay: _spu_rev_attr.feedback = 0 */
$depths:
	/* depths: when the mode changed, switch the reverb off and zero the depths; otherwise
	   store depth.left/right (mask bits 0x2 / 0x4) to RXX + 0x184 / 0x186 */
	beq	s4, zero, $L03c8	/* mode not set this call: do the depths */
	ori	a1, zero, 0x6a	/* delay slot (real work): a1 = ioctl command 0x6a (get reverb state) */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x6a, &state) */
	addiu	a2, sp, 88	/* delay slot (real work): a2 = &state (0x58(sp)) */
	lw	v0, 88(sp)	/* v0 = reverb state */
	nop	/* load delay slot: nothing to do */
	beq	v0, zero, $L0438	/* reverb was off: just zero the depth registers */
	ori	a1, zero, 0x60	/* delay slot (real work): a1 = ioctl command 0x60 (reverb on/off) */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	addiu	a2, sp, 92	/* a2 = &zero (0x5c(sp)) */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x60, &0): switch the reverb off during the change */
	sw	zero, 92(sp)	/* delay slot (real work): zero word = 0 */
	j	$L0438	/* go and zero the depths */
	nop	/* delay slot: nothing to do */
$L03c8:
	bne	s5, zero, $L03d8	/* mask == 0: depth.left */
	andi	v0, s3, 0x2	/* delay slot (real work): v0 = mask & depth.left bit */
	beq	v0, zero, $L03fc	/* not selected: skip */
	nop	/* delay slot: nothing to do */
$L03d8:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 8(s2)	/* v1 = attr->depth.left */
	nop	/* load delay slot: nothing to do */
	sh	v1, 388(v0)	/* RXX + 0x184 = reverb output volume left */
	lhu	v0, 8(s2)	/* v0 = attr->depth.left */
	nop	/* load delay slot: nothing to do */
	sh	v0, _spu_rev_attr+8	/* macro "sh v0, _spu_rev_attr+8": kept in _spu_rev_attr */
$L03fc:
	bne	s5, zero, $L040c	/* mask == 0: depth.right */
	andi	v0, s3, 0x4	/* delay slot (real work): v0 = mask & depth.right bit */
	beq	v0, zero, $send	/* not selected: skip */
	nop	/* delay slot: nothing to do */
$L040c:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	lhu	v1, 10(s2)	/* v1 = attr->depth.right */
	nop	/* load delay slot: nothing to do */
	sh	v1, 390(v0)	/* RXX + 0x186 = reverb output volume right */
	lhu	v0, 10(s2)	/* v0 = attr->depth.right */
	nop	/* load delay slot: nothing to do */
	sh	v0, _spu_rev_attr+10	/* macro "sh v0, _spu_rev_attr+10": kept in _spu_rev_attr */
	j	$send	/* go to the driver update */
	nop	/* delay slot: nothing to do */
$L0438:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	nop	/* load delay slot: nothing to do */
	sh	zero, 388(v0)	/* RXX + 0x184 = 0 */
	sh	zero, 390(v0)	/* RXX + 0x186 = 0 */
	sh	zero, _spu_rev_attr+8	/* macro "sh zero, _spu_rev_attr+8": depth.left = 0 */
	sh	zero, _spu_rev_attr+10	/* macro "sh zero, _spu_rev_attr+10": depth.right = 0 */
$send:
	/* hand the block to the driver, clear the work area if asked, update the work area
	   address and restore the reverb state */
	bne	s4, zero, $L0474	/* mode set: send the block */
	ori	a1, zero, 0x63	/* delay slot (real work): a1 = ioctl command 0x63 (load reverb block) */
	bne	s6, zero, $L0474	/* delay handled: send the block */
	nop	/* delay slot: nothing to do */
	beq	s7, zero, $L0484	/* feedback handled: send the block, else skip */
	nop	/* delay slot: nothing to do */
$L0474:
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x63, local block) */
	addiu	a2, sp, 16	/* delay slot (real work): a2 = &local block (0x10(sp)) */
$L0484:
	beq	fp, zero, $tail	/* no clear-work-area flag: skip */
	nop	/* delay slot: nothing to do */
	lw	a0, _spu_rev_attr+4	/* a0 = _spu_rev_attr.mode (lui+lw macro) */
	jal	SpuClearReverbWorkArea	/* SpuClearReverbWorkArea(mode) */
	nop	/* call delay: nothing to do */
$tail:
	beq	s4, zero, $return	/* mode not set this call: return 0 */
	move	v0, zero	/* delay slot (real work): v0 = 0 */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	la	a2, _spu_rev_offsetaddr	/* a2 = &_spu_rev_offsetaddr (la: lui + addiu) */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x61, &_spu_rev_offsetaddr): the new work area */
	ori	a1, zero, 0x61	/* delay slot (real work): a1 = ioctl command 0x61 */
	lw	v0, 88(sp)	/* v0 = reverb state saved earlier */
	nop	/* load delay slot: nothing to do */
	beq	v0, zero, $ok	/* reverb was off: done */
	ori	a1, zero, 0x60	/* delay slot (real work): a1 = ioctl command 0x60 */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x60, &state): restore the reverb state */
	addiu	a2, sp, 88	/* delay slot (real work): a2 = &state (0x58(sp)) */
$ok:
	move	v0, zero	/* v0 = 0, the return value */
$return:
	/* epilogue */
	lw	ra, 140(sp)	/* restore ra */
	lw	fp, 136(sp)	/* restore fp */
	lw	s7, 132(sp)	/* restore s7 */
	lw	s6, 128(sp)	/* restore s6 */
	lw	s5, 124(sp)	/* restore s5 */
	lw	s4, 120(sp)	/* restore s4 */
	lw	s3, 116(sp)	/* restore s3 */
	lw	s2, 112(sp)	/* restore s2 */
	lw	s1, 108(sp)	/* restore s1 */
	lw	s0, 104(sp)	/* restore s0 */
	addiu	sp, sp, 144	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
