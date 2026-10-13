/*
 * libspu_s_sva.s - LIBSPU module S_SVA: SpuSetVoiceAttr and _SpuSetVoiceAttr.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_sva.json and the
 * "libspu_s_sva" entry of config/data.json; counted as "library assembly", never as matched C
 * and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_SVA.
 * Exports:     SpuSetVoiceAttr (.text+0x0, 40 bytes), _SpuSetVoiceAttr (.text+0x28, 1792 bytes).
 * Imports:     _spu_note2pitch (0x8005913c, module S_N2P), _spu_voice_centerNote (S_INI .data,
 *              0x80073d1c), _spu_mem_mode (0x80073cb4, the SPU module's memory-mode word),
 *              _spu_mem_mode_plus (0x80073cb8, log2 of the allocation unit).
 * Own data:    _spu_RXX, a module-local copy of the SPU register base pointer (.data+0x0 =
 *              0x1f801c00), at 0x80073af8; two 32-byte jump tables in .rdata, 0x80012b9c (left
 *              volume mode) and 0x80012bbc (right), whose entries are addresses inside
 *              _SpuSetVoiceAttr.
 *
 * Nearest archived version: Runtime Library 3.3 / 3.4 LIBSPU.LIB S_SVA (1680 bytes, exports
 * _SpuSetVoiceAttr and SpuSetVoiceAttr, 64 bytes of .rdata jump tables). Relocation-masked,
 * 354 of retail's 458 words are identical to the 3.3 member (builds/lane_tools/best.py);
 * retail is 152 bytes longer. The same field-by-field code, in the same order, the same
 * jump-table volume-mode switches, but the SPU RAM address fields are converted inline in
 * retail (round up by _spu_mem_mode, shift right by _spu_mem_mode_plus) where 3.3 calls
 * _spu_FsetRXXa (its extra import), the module is entered through a thin SpuSetVoiceAttr
 * wrapper, and the end of the routine inserts a short busy-wait. The 3.0, 3.5, 3.6, ISA and
 * 4.x members differ more, so no archive matches: "Runtime 3.3 S_SVA with inline address
 * conversion and a module-local _spu_RXX". The module-local copies of _spu_RXX are explained in
 * libspu_s_gks.s.
 *
 * The routines are compiled C (GCC register use, switches compiled to jump tables); they are
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
 * Users:       _SpuSetVoiceAttr only (the lui/lw pairs below).
 */
	.data
_spu_RXX:
	.word	0x1f801c00	/* SPU register file base (volatile u_short *) */

/*
 * Jump tables of the two volume-mode switches of _SpuSetVoiceAttr (the compiler's "switch
 * (attr->volmode.left)" and ".right"). Each has eight code-address entries indexed by the mode
 * 0..7 (SPU_VOICE_DIRECT .. SPU_VOICE_EXPDec); modes >= 8 behave like mode 0. Entry 0 is the
 * direct-volume path; entries 1..7 load the sweep-mode bits of the volume register (0x8000,
 * 0x9000 ... 0xe000) and join the common code. Layout: 8 x .word (text address), 32 bytes
 * each. Users: _SpuSetVoiceAttr.
 */
	.rdata
sva_voll_mode_tab:
	.word	$vl_m0, $vl_m1, $vl_m2, $vl_m3
	.word	$vl_m4, $vl_m5, $vl_m6, $vl_m7
sva_volr_mode_tab:
	.word	$vr_m0, $vr_m1, $vr_m2, $vr_m3
	.word	$vr_m4, $vr_m5, $vr_m6, $vr_m7

	.text

/*
 * SpuSetVoiceAttr - set the attributes of the voices named in attr->voice
 *
 * Purpose:     void SpuSetVoiceAttr(SpuVoiceAttr *attr). Calls
 *              _SpuSetVoiceAttr(attr, 0, 23, 0): voices 0..23, with the trailing wait.
 * Inputs:      a0 = attr.
 * Outputs:     v0 = the result of _SpuSetVoiceAttr (0, or -3 on a bad range).
 * Clobbers:    everything _SpuSetVoiceAttr clobbers. Stack: 0x18-byte frame, ra at 0x10.
 * Callers:     voices_mute (0x8004392c), voices_restore (0x80043968), voice_set_volume
 *              (0x80043a40), voice_set_pitch (0x80043c88), voice_set_note (0x80043cec) and
 *              spu_init (0x80044138), all config/audio.json, and library code at 0x8005c9fc.
 */
	.globl	SpuSetVoiceAttr
SpuSetVoiceAttr:
	addiu	sp, sp, -24	/* allocate the 0x18-byte frame */
	sw	ra, 16(sp)	/* save ra */
	addu	a1, zero, zero	/* a1 = 0: first voice */
	ori	a2, zero, 0x17	/* a2 = 23: last voice */
	jal	_SpuSetVoiceAttr	/* _SpuSetVoiceAttr(attr, 0, 23, 0) */
	addu	a3, zero, zero	/* delay slot (real work): a3 = 0, wait afterwards */
	lw	ra, 16(sp)
	addiu	sp, sp, 24
	jr	ra
	nop	/* delay slot: nothing to do */

/*
 * _SpuSetVoiceAttr - write the selected attributes of a range of voices to the SPU
 *
 * Purpose:     long _SpuSetVoiceAttr(SpuVoiceAttr *attr, long start, long end, long nowait).
 *              For every voice n in [start, end] whose bit (1 << n) is set in attr->voice,
 *              writes the attributes selected by attr->mask (all of them when mask == 0) to
 *              the voice's 16-byte register block at _spu_RXX + 16 * n:
 *                +0 volume left   +2 volume right   +4 pitch   +6 start address
 *                +8 ADSR low (attack, decay, sustain level)  +0xa ADSR high (sustain, release)
 *                +0xe loop (repeat) address
 *              The remaining fields are kept in _spu_voice_centerNote[n] (sample_note).
 *              SpuVoiceAttr (libspu.h), with the mask bit that selects each field:
 *                +0x00 voice   +0x04 mask   +0x08 volume.left (0x1)  .right (0x2)
 *                +0x0c volmode.left (0x4)  .right (0x8)   +0x14 pitch (0x10)
 *                +0x16 note (0x20)   +0x18 sample_note (0x40)   +0x1c addr (0x80)
 *                +0x20 loop_addr (0x10000)   +0x24 a_mode (0x100)   +0x28 s_mode (0x200)
 *                +0x2c r_mode (0x400)   +0x30 ar (0x800)   +0x32 dr (0x1000)
 *                +0x34 sr (0x2000)   +0x36 rr (0x4000)   +0x38 sl (0x8000)
 *                +0x3a adsr1 (0x20000)   +0x3c adsr2 (0x40000)
 *              Volumes: volume modes 1..7 write the sweep-mode bits 0x8000 + 0x1000 * (mode
 *              - 1) ORed with the field clamped to 0..0x7f; mode 0 (or >= 8) writes
 *              the field & 0x7fff.
 *              ADSR parts are merged into the existing register halves (ar into the high
 *              byte of +8, dr bits 4..7, sl bits 0..3, sr bits 6..12 of +0xa, rr bits 0..5).
 *              Address fields: rounded up by _spu_mem_mode when not a multiple of it and
 *              shifted right by _spu_mem_mode_plus (to 8-byte units).
 *              Returns -3 when start >= 24, end < 0 or end < start (end is clamped to 23,
 *              start to 0); otherwise 0. When nowait == 0 it finishes with a short counted
 *              wait (two passes multiplying a stack word by 13) before returning.
 * Inputs:      a0 = attr, a1 = start voice, a2 = end voice, a3 = nowait.
 * Outputs:     v0 = 0 or -3.
 * Clobbers:    v0, v1, a0..a3. s0..s7 and fp are saved and restored.
 * Stack:       0x50-byte frame: wait counter 0x10, wait value 0x14, s0 0x28, s1 0x2c, s2
 *              0x30, s3 0x34, s4 0x38, s5 0x3c, s6 0x40, s7 0x44, fp 0x48, ra 0x4c.
 * Registers:   s0 = attr; s1 = attr->mask; s2 = (mask == 0); s3 = voice * 8 (so s3 * 2 is
 *              the register block offset); s4 = voice; s5 / s6 = left / right volume being
 *              built; s7 = end + 1; fp = nowait.
 * Callees:     _spu_note2pitch.
 */
	.globl	_SpuSetVoiceAttr
_SpuSetVoiceAttr:
	/* prologue; validate and clamp the voice range; return -3 on error */
	addiu	sp, sp, -80	/* allocate the 0x50-byte frame */
	sw	s0, 40(sp)
	addu	s0, a0, zero	/* s0 = attr */
	sw	s7, 68(sp)
	addu	s7, a2, zero	/* s7 = end voice */
	sw	ra, 76(sp)
	sw	fp, 72(sp)
	sw	s6, 64(sp)
	sw	s5, 60(sp)
	sw	s4, 56(sp)
	sw	s3, 52(sp)
	sw	s2, 48(sp)
	sw	s1, 44(sp)
	lw	s1, 4(s0)	/* s1 = attr->mask */
	addu	fp, a3, zero	/* fp = nowait */
	bgez	a1, $L0070	/* start >= 0: keep it */
	sltiu	s2, s1, 1	/* delay slot (real work): s2 = (mask == 0), "set everything" */
	addu	a1, zero, zero	/* negative start: clamp to 0 */
$L0070:
	slti	v0, a1, 24	/* v0 = (start < 24) */
	beq	v0, zero, $L0098	/* start >= 24: error -3 */
	slti	v0, s7, 24	/* delay slot (real work): v0 = (end < 24) */
	bne	v0, zero, $L0088	/* end < 24: keep it */
	nop	/* delay slot: nothing to do */
	ori	s7, zero, 0x17	/* end = 23 */
$L0088:
	bltz	s7, $L0098	/* end < 0: error -3 */
	slt	v0, s7, a1	/* delay slot (real work): v0 = (end < start) */
	beq	v0, zero, $L00a0	/* end >= start: go */
	addiu	s7, s7, 1	/* delay slot (real work): s7 = end + 1, the exclusive limit */
$L0098:
	j	$return	/* error exit */
	addiu	v0, zero, -3	/* delay slot (real work): v0 = -3 */
$L00a0:
	addu	s4, a1, zero	/* s4 = voice = start */
	slt	v0, s4, s7	/* v0 = (voice < end + 1) */
	beq	v0, zero, $tail	/* empty range: go to the tail */
	ori	v0, zero, 0x1	/* delay slot (real work): v0 = 1 */
$L00b0:
	/* per voice: skip voices not named in attr->voice */
	lw	v1, 0(s0)	/* v1 = attr->voice */
	sllv	v0, v0, s4	/* v0 = 1 << voice */
	and	v0, v0, v1	/* v0 = voice bit & attr->voice */
	beq	v0, zero, $next_voice	/* voice not selected: next voice */
	nop	/* delay slot: nothing to do */
	bne	s2, zero, $L00d8
	sll	s3, s4, 3	/* s3 = voice * 8 */
	andi	v0, s1, 0x10	/* mask & pitch bit (0x10) */
	beq	v0, zero, $L00f0
	nop	/* delay slot: nothing to do */
$L00d8:
	/* pitch (mask bit 0x10): voice block + 4 = attr->pitch */
	sll	v0, s4, 4	/* v0 = voice * 16, the register block offset */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	a0, 20(s0)	/* attr->pitch */
	addu	v0, v0, v1	/* v0 = voice register block */
	sh	a0, 4(v0)	/* voice block + 4 (pitch) = attr->pitch */
$L00f0:
	/* sample_note (0x40): _spu_voice_centerNote[voice] = attr->sample_note */
	bne	s2, zero, $L0100
	andi	v0, s1, 0x40	/* delay slot (real work): mask & sample_note bit (0x40) */
	beq	v0, zero, $L0118
	nop	/* delay slot: nothing to do */
$L0100:
	lhu	v1, 24(s0)	/* v1 = attr->sample_note */
	sll	v0, s4, 1	/* v0 = voice * 2 */
	.set	noat	/* the sequence below names $at (register 1) explicitly */
	la	$1, _spu_voice_centerNote	/* AT = &_spu_voice_centerNote (la: lui + addiu) ... */
	addu	$1, $1, v0	/* ... AT = &_spu_voice_centerNote[voice] */
	sh	v1, 0($1)	/* _spu_voice_centerNote[voice] = sample_note */
	.set	at
$L0118:
	/* note (0x20): pitch = _spu_note2pitch(centre note, note), written to voice block + 4 */
	bne	s2, zero, $L012c
	sll	v0, s4, 1	/* delay slot (real work) */
	andi	v0, s1, 0x20	/* mask & note bit (0x20) */
	beq	v0, zero, $L0168
	sll	v0, s4, 1	/* delay slot (real work) */
$L012c:
	.set	noat	/* the sequence below names $at (register 1) explicitly */
	la	$1, _spu_voice_centerNote	/* AT = &_spu_voice_centerNote[voice] (la: lui + addiu, addu) ... */
	addu	$1, $1, v0
	lhu	a1, 0($1)	/* a1 = _spu_voice_centerNote[voice] */
	.set	at
	lhu	a3, 22(s0)	/* a3 = attr->note */
	srl	a0, a1, 8	/* a0 = centre note, integer part (high byte) */
	andi	a1, a1, 0xff	/* a1 = centre note, fraction (low byte) */
	srl	a2, a3, 8	/* a2 = note, integer part */
	jal	_spu_note2pitch	/* v0 = _spu_note2pitch(centre_hi, centre_lo, note_hi, note_lo) */
	andi	a3, a3, 0xff	/* delay slot (real work): a3 = note, fraction */
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	sll	v1, s3, 1	/* v1 = voice * 16 */
	addu	v1, v1, a0
	sh	v0, 4(v1)	/* voice block + 4 (pitch) = converted pitch */
$L0168:
	/* addr (0x80): start address = (addr rounded up to _spu_mem_mode) >> plus -> block + 6 */
	bne	s2, zero, $L0178
	andi	v0, s1, 0x80	/* delay slot (real work): mask & addr bit (0x80) */
	beq	v0, zero, $L01d0
	nop	/* delay slot: nothing to do */
$L0178:
	lw	a1, _spu_mem_mode	/* a1 = _spu_mem_mode (lui+lw macro) */
	lw	a0, 28(s0)	/* a0 = attr->addr */
	beq	a1, zero, $L01b0	/* _spu_mem_mode == 0: no rounding */
	nop	/* delay slot: nothing to do */
	divu	a0, a1	/* a0 % _spu_mem_mode ... */
	bne	a1, zero, $L019c	/* divisor non-zero (always here) */
	nop	/* delay slot: nothing to do */
	break	7168	/* divide by zero trap */
$L019c:
	mfhi	v0	/* ... remainder */
	nop
	beq	v0, zero, $L01b0	/* already a multiple: keep */
	nop	/* delay slot: nothing to do */
	addu	a0, a0, a1	/* round up: addr += _spu_mem_mode */
$L01b0:
	lw	v0, _spu_mem_mode_plus	/* v0 = _spu_mem_mode_plus (lui+lw macro) */
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	srlv	a0, a0, v0	/* a0 >>= plus: SPU RAM address in 8-byte units */
	sll	v0, s3, 1	/* v0 = voice * 2 */
	addu	v0, v0, v1
	sh	a0, 6(v0)	/* voice block + 6 (start address) = a0 */
$L01d0:
	/* ar / a_mode (0x800 | 0x100): attack rate in the high byte of ADSR low (block + 8);
	   a_mode 1 -> mode bit 0, a_mode 5 -> 0x80 (exponential), others 0 */
	bne	s2, zero, $L01e8
	andi	v0, s1, 0x800	/* delay slot (real work): mask & ar bit (0x800) */
	beq	v0, zero, $L0250
	andi	v0, s1, 0x100	/* delay slot (real work): mask & a_mode bit (0x100) */
	beq	v0, zero, $L020c
	addu	a1, zero, zero	/* delay slot (real work) */
$L01e8:
	lw	v1, 36(s0)	/* v1 = attr->a_mode */
	ori	v0, zero, 0x1
	beq	v1, v0, $L0208	/* a_mode == 1 (linear) */
	ori	v0, zero, 0x5	/* delay slot (real work) */
	bne	v1, v0, $L020c	/* a_mode != 5 (exponential): a1 = 0 */
	addu	a1, zero, zero	/* delay slot (real work) */
	j	$L020c
	ori	a1, zero, 0x80	/* a_mode == 5: a1 = 0x80 (exponential attack bit) */
$L0208:
	addu	a1, zero, zero
$L020c:
	lhu	v0, 48(s0)	/* v0 = attr->ar */
	nop	/* load delay slot: nothing to do */
	sltiu	v0, v0, 128	/* v0 = (ar < 0x80) */
	beq	v0, zero, $L0224
	ori	a2, zero, 0x7f	/* delay slot (real work): a2 = 0x7f, the clamp */
	lhu	a2, 48(s0)	/* a2 = ar */
$L0224:
	lw	v0, _spu_RXX	/* v0 = _spu_RXX (lui+lw macro) */
	sll	v1, s3, 1
	addu	v1, v1, v0
	lhu	v0, 8(v1)	/* v0 = voice block + 8, ADSR low */
	nop	/* load delay slot: nothing to do */
	andi	a0, v0, 0xff	/* a0 = low byte, kept */
	or	v0, a2, a1	/* v0 = ar | attack mode bit */
	sll	v0, v0, 8	/* moved to the high byte */
	or	v0, a0, v0	/* merged */
	sh	v0, 8(v1)	/* voice block + 8 = new ADSR low */
$L0250:
	/* dr (0x1000): decay rate, bits 4..7 of ADSR low */
	bne	s2, zero, $L0260
	andi	v0, s1, 0x1000	/* delay slot (real work): mask & dr bit (0x1000) */
	beq	v0, zero, $L02a0
	nop	/* delay slot: nothing to do */
$L0260:
	lhu	v0, 50(s0)	/* v0 = attr->dr */
	nop	/* load delay slot: nothing to do */
	sltiu	v0, v0, 16	/* v0 = (dr < 16) */
	beq	v0, zero, $L0278
	ori	a2, zero, 0xf	/* delay slot (real work): a2 = 15, the clamp */
	lhu	a2, 50(s0)	/* a2 = dr */
$L0278:
	lw	v0, _spu_RXX
	sll	v1, s3, 1
	addu	v1, v1, v0
	lhu	v0, 8(v1)
	nop	/* load delay slot: nothing to do */
	andi	a0, v0, 0xff0f	/* a0 = ADSR low with the decay bits cleared */
	sll	v0, a2, 4	/* a2 << 4 = decay field */
	or	v0, a0, v0
	sh	v0, 8(v1)	/* voice block + 8 = new ADSR low */
$L02a0:
	/* sl (0x8000): sustain level, bits 0..3 of ADSR low */
	bne	s2, zero, $L02b0
	andi	v0, s1, 0x8000	/* delay slot (real work): mask & sl bit (0x8000) */
	beq	v0, zero, $L02ec
	nop	/* delay slot: nothing to do */
$L02b0:
	lhu	v0, 56(s0)	/* v0 = attr->sl */
	nop	/* load delay slot: nothing to do */
	sltiu	v0, v0, 16	/* v0 = (sl < 16) */
	beq	v0, zero, $L02c8
	ori	a2, zero, 0xf	/* delay slot (real work): a2 = 15, the clamp */
	lhu	a2, 56(s0)	/* a2 = sl */
$L02c8:
	lw	v0, _spu_RXX
	sll	v1, s3, 1
	addu	v1, v1, v0
	lhu	v0, 8(v1)
	nop	/* load delay slot: nothing to do */
	andi	v0, v0, 0xfff0	/* ADSR low with the sustain-level bits cleared */
	or	v0, a2, v0	/* merged with sl */
	sh	v0, 8(v1)	/* voice block + 8 = new ADSR low */
$L02ec:
	/* sr / s_mode (0x2000 | 0x200): sustain rate in ADSR high (block + 0xa), bits 6..; the
	   s_mode selects the direction/exponential bits 0x100 / 0x200 / 0x300 (modes 1,3,5,7) */
	bne	s2, zero, $L0304
	andi	v0, s1, 0x2000	/* delay slot (real work): mask & sr bit (0x2000) */
	beq	v0, zero, $L0398
	andi	v0, s1, 0x200	/* delay slot (real work): s_mode bit (0x200) */
	beq	v0, zero, $L0354
	addu	a1, zero, zero	/* delay slot (real work) */
$L0304:
	lw	v1, 40(s0)	/* v1 = attr->s_mode */
	ori	v0, zero, 0x3
	beq	v1, v0, $L0350	/* s_mode == 3 */
	slti	v0, v1, 4	/* delay slot (real work) */
	beq	v0, zero, $L032c
	ori	v0, zero, 0x1	/* delay slot (real work): v0 = 1 */
	beq	v1, v0, $L0354
	addu	a1, zero, zero	/* delay slot (real work) */
	j	$L0354
	ori	a1, zero, 0x100	/* delay slot (real work) */
$L032c:
	ori	v0, zero, 0x5
	beq	v1, v0, $L0348
	ori	v0, zero, 0x7	/* delay slot (real work) */
	beq	v1, v0, $L0354
	ori	a1, zero, 0x300	/* delay slot (real work) */
	j	$L0354
	ori	a1, zero, 0x100	/* delay slot (real work) */
$L0348:
	j	$L0354
	ori	a1, zero, 0x200	/* delay slot (real work) */
$L0350:
	ori	a1, zero, 0x100
$L0354:
	lhu	v0, 52(s0)	/* v0 = attr->sr */
	nop	/* load delay slot: nothing to do */
	sltiu	v0, v0, 128	/* v0 = (sr < 0x80) */
	beq	v0, zero, $L036c
	ori	a2, zero, 0x7f	/* delay slot (real work): a2 = 0x7f, the clamp */
	lhu	a2, 52(s0)	/* a2 = sr */
$L036c:
	lw	v0, _spu_RXX
	sll	v1, s3, 1
	addu	v1, v1, v0
	lhu	v0, 10(v1)	/* v0 = voice block + 0xa, ADSR high */
	nop	/* load delay slot: nothing to do */
	andi	a0, v0, 0x3f	/* a0 = low 6 bits (release), kept */
	or	v0, a2, a1	/* v0 = sr | sustain mode bits */
	sll	v0, v0, 6	/* shifted into bits 6.. */
	or	v0, a0, v0	/* merged */
	sh	v0, 10(v1)	/* voice block + 0xa = new ADSR high */
$L0398:
	/* rr / r_mode (0x4000 | 0x400): release rate, bits 0..5 of ADSR high;
	   r_mode 7 -> exponential bit 0x20 */
	bne	s2, zero, $L03b0
	andi	v0, s1, 0x4000	/* delay slot (real work): mask & rr bit (0x4000) */
	beq	v0, zero, $L0414
	andi	v0, s1, 0x400	/* delay slot (real work): mask & r_mode bit (0x400) */
	beq	v0, zero, $L03d4
	addu	a1, zero, zero	/* delay slot (real work) */
$L03b0:
	lw	v1, 44(s0)	/* v1 = attr->r_mode */
	ori	v0, zero, 0x3
	beq	v1, v0, $L03d0
	ori	v0, zero, 0x7	/* delay slot (real work) */
	bne	v1, v0, $L03d4
	addu	a1, zero, zero	/* delay slot (real work) */
	j	$L03d4
	ori	a1, zero, 0x20	/* delay slot (real work) */
$L03d0:
	addu	a1, zero, zero
$L03d4:
	lhu	v0, 54(s0)	/* v0 = attr->rr */
	nop	/* load delay slot: nothing to do */
	sltiu	v0, v0, 32	/* v0 = (rr < 32) */
	beq	v0, zero, $L03ec
	ori	a2, zero, 0x1f	/* delay slot (real work): a2 = 31, the clamp */
	lhu	a2, 54(s0)	/* a2 = rr */
$L03ec:
	lw	v0, _spu_RXX
	sll	v1, s3, 1
	addu	v1, v1, v0
	lhu	v0, 10(v1)
	nop	/* load delay slot: nothing to do */
	andi	a0, v0, 0xffc0	/* a0 = ADSR high with the release bits cleared */
	or	v0, a2, a1	/* v0 = rr | release mode bit */
	or	v0, a0, v0
	sh	v0, 10(v1)	/* voice block + 0xa = new ADSR high */
$L0414:
	/* adsr1 (0x20000) and adsr2 (0x40000): raw ADSR halves (block + 8 and + 0xa) */
	bne	s2, zero, $L042c
	sll	v0, s3, 1	/* delay slot (real work) */
	lui	v0, 0x2
	and	v0, s1, v0
	beq	v0, zero, $L0440
	sll	v0, s3, 1	/* delay slot (real work) */
$L042c:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	a0, 58(s0)	/* a0 = attr->adsr1 */
	addu	v0, v0, v1
	sh	a0, 8(v0)	/* voice block + 8 = adsr1 */
$L0440:
	bne	s2, zero, $L0458
	sll	v0, s3, 1	/* delay slot (real work) */
	lui	v0, 0x4
	and	v0, s1, v0
	beq	v0, zero, $L046c
	sll	v0, s3, 1	/* delay slot (real work) */
$L0458:
	lw	v1, _spu_RXX	/* v1 = _spu_RXX (lui+lw macro) */
	lhu	a0, 60(s0)	/* a0 = attr->adsr2 */
	addu	v0, v0, v1
	sh	a0, 10(v0)	/* voice block + 0xa = adsr2 */
$L046c:
	/* loop_addr (0x10000): repeat address, converted like addr -> block + 0xe */
	bne	s2, zero, $L0480
	lui	v0, 0x1	/* delay slot (real work) */
	and	v0, s1, v0
	beq	v0, zero, $L04d8
	nop	/* delay slot: nothing to do */
$L0480:
	lw	a1, _spu_mem_mode	/* a1 = _spu_mem_mode (lui+lw macro) */
	lw	a0, 32(s0)	/* a0 = attr->loop_addr */
	beq	a1, zero, $L04b8
	nop	/* delay slot: nothing to do */
	divu	a0, a1
	bne	a1, zero, $L04a4
	nop	/* delay slot: nothing to do */
	break	7168
$L04a4:
	mfhi	v0
	nop
	beq	v0, zero, $L04b8
	nop	/* delay slot: nothing to do */
	addu	a0, a0, a1
$L04b8:
	lw	v0, _spu_mem_mode_plus	/* v0 = _spu_mem_mode_plus (lui+lw macro) */
	lw	v1, _spu_RXX
	srlv	a0, a0, v0	/* a0 >>= plus: loop address in 8-byte units */
	sll	v0, s3, 1
	addu	v0, v0, v1
	sh	a0, 14(v0)	/* voice block + 0xe (repeat address) = a0 */
$L04d8:
	/* volume.left / volmode.left (0x1 / 0x4): jump table on the mode */
	bne	s2, zero, $L04f0
	andi	v0, s1, 0x1	/* delay slot (real work): mask & volume.left bit (0x1) */
	beq	v0, zero, $L05ac
	andi	v0, s1, 0x4	/* delay slot (real work): mask & volmode.left bit (0x4) */
	beq	v0, zero, $vl_m0
	nop	/* delay slot: nothing to do */
$L04f0:
	lh	v1, 12(s0)	/* v1 = attr->volmode.left (signed) */
	nop	/* load delay slot: nothing to do */
	sltiu	v0, v1, 8	/* v0 = (mode < 8) */
	beq	v0, zero, $vl_m0	/* out of range: direct volume */
	sll	v0, v1, 2	/* delay slot (real work): v0 = mode * 4 */
	.set	noat	/* the sequence below names $at (register 1) explicitly */
	la	$1, sva_voll_mode_tab	/* AT = &sva_voll_mode_tab (la: lui + addiu) ... */
	addu	$1, $1, v0	/* ... AT = &sva_voll_mode_tab[mode] */
	lw	v0, 0($1)	/* v0 = sva_voll_mode_tab[mode] */
	.set	at
	nop	/* load delay slot: nothing to do */
	jr	v0	/* switch (volmode.left): jump to the entry */
	nop	/* delay slot: nothing to do */
$vl_m1:
	j	$vl_join	/* mode 1: join with a1 = 0x8000 (linear increase) */
	ori	a1, zero, 0x8000	/* delay slot (real work): a1 = 0x8000 */
$vl_m2:
	j	$vl_join	/* mode 2: join */
	ori	a1, zero, 0x9000	/* delay slot (real work): a1 = 0x9000 */
$vl_m3:
	j	$vl_join	/* mode 3: join */
	ori	a1, zero, 0xa000	/* delay slot (real work): a1 = 0xa000 (linear decrease) */
$vl_m4:
	j	$vl_join	/* mode 4: join */
	ori	a1, zero, 0xb000	/* delay slot (real work): a1 = 0xb000 */
$vl_m5:
	j	$vl_join	/* mode 5: join */
	ori	a1, zero, 0xc000	/* delay slot (real work): a1 = 0xc000 (exponential increase) */
$vl_m6:
	j	$vl_join	/* mode 6: join */
	ori	a1, zero, 0xd000	/* delay slot (real work): a1 = 0xd000 */
$vl_m7:
	j	$vl_join	/* mode 7: join */
	ori	a1, zero, 0xe000	/* delay slot (real work): a1 = 0xe000 (exponential decrease) */
$vl_m0:
	lhu	s5, 8(s0)	/* direct mode: s5 = attr->volume.left (unsigned) */
	addu	a1, zero, zero	/* a1 = 0, no mode bits */
$vl_join:
	beq	a1, zero, $L0594	/* no mode bits: write the field as it is */
	andi	v0, s5, 0x7fff	/* delay slot (real work): v0 = field & 0x7fff */
	lh	a0, 8(s0)	/* a0 = attr->volume.left (signed) */
	nop	/* load delay slot: nothing to do */
	slti	v0, a0, 128	/* v0 = (volume < 0x80) */
	bne	v0, zero, $L0584	/* in range or negative: test the sign */
	addu	v1, a0, zero	/* delay slot (real work): v1 = volume */
	j	$L0590	/* volume >= 0x80: clamp to 0x7f */
	ori	s5, zero, 0x7f	/* delay slot (real work): s5 = 0x7f */
$L0584:
	bgez	a0, $L0590	/* volume >= 0: use it */
	addu	s5, v1, zero	/* delay slot (real work): s5 = volume */
	addu	s5, zero, zero	/* negative: s5 = 0 */
$L0590:
	andi	v0, s5, 0x7fff	/* v0 = s5 & 0x7fff */
$L0594:
	sll	v1, s3, 1	/* v1 = voice * 2 */
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	or	v0, v0, a1	/* v0 = volume | mode bits */
	addu	v1, v1, a0	/* v1 = voice block */
	sh	v0, 0(v1)	/* voice block + 0 (volume left) = v0 */
$L05ac:
	/* volume.right / volmode.right (0x2 / 0x8): jump table on the mode */
	bne	s2, zero, $L05c4
	andi	v0, s1, 0x2	/* delay slot (real work): mask & volume.right bit (0x2) */
	beq	v0, zero, $next_voice
	andi	v0, s1, 0x8	/* delay slot (real work): mask & volmode.right bit (0x8) */
	beq	v0, zero, $vr_m0
	nop	/* delay slot: nothing to do */
$L05c4:
	lh	v1, 14(s0)	/* v1 = attr->volmode.right (signed) */
	nop	/* load delay slot: nothing to do */
	sltiu	v0, v1, 8
	beq	v0, zero, $vr_m0
	sll	v0, v1, 2	/* delay slot (real work): v0 = mode * 4 */
	.set	noat	/* the sequence below names $at (register 1) explicitly */
	la	$1, sva_volr_mode_tab	/* AT = &sva_volr_mode_tab (la: lui + addiu) ... */
	addu	$1, $1, v0	/* ... AT = &sva_volr_mode_tab[mode] */
	lw	v0, 0($1)	/* v0 = sva_volr_mode_tab[mode] */
	.set	at
	nop	/* load delay slot: nothing to do */
	jr	v0	/* switch (volmode.right): jump to the entry */
	nop	/* delay slot: nothing to do */
$vr_m1:
	j	$vr_join
	ori	a1, zero, 0x8000	/* delay slot (real work) */
$vr_m2:
	j	$vr_join
	ori	a1, zero, 0x9000	/* delay slot (real work) */
$vr_m3:
	j	$vr_join
	ori	a1, zero, 0xa000	/* delay slot (real work) */
$vr_m4:
	j	$vr_join
	ori	a1, zero, 0xb000	/* delay slot (real work) */
$vr_m5:
	j	$vr_join
	ori	a1, zero, 0xc000	/* delay slot (real work) */
$vr_m6:
	j	$vr_join
	ori	a1, zero, 0xd000	/* delay slot (real work) */
$vr_m7:
	j	$vr_join
	ori	a1, zero, 0xe000	/* delay slot (real work) */
$vr_m0:
	lhu	s6, 10(s0)	/* direct mode: s6 = attr->volume.right (unsigned) */
	addu	a1, zero, zero	/* a1 = 0, no mode bits */
$vr_join:
	beq	a1, zero, $L0668	/* no mode bits: write the field as it is */
	andi	v0, s6, 0x7fff	/* delay slot (real work): v0 = field & 0x7fff */
	lh	a0, 10(s0)	/* a0 = attr->volume.right (signed) */
	nop	/* load delay slot: nothing to do */
	slti	v0, a0, 128	/* v0 = (volume < 0x80) */
	bne	v0, zero, $L0658
	addu	v1, a0, zero	/* delay slot (real work) */
	j	$L0664	/* volume >= 0x80: clamp to 0x7f */
	ori	s6, zero, 0x7f	/* delay slot (real work): s6 = 0x7f */
$L0658:
	bgez	a0, $L0664
	addu	s6, v1, zero	/* delay slot (real work): s6 = volume */
	addu	s6, zero, zero	/* negative: s6 = 0 */
$L0664:
	andi	v0, s6, 0x7fff	/* v0 = s6 & 0x7fff */
$L0668:
	sll	v1, s3, 1	/* v1 = voice * 2 */
	lw	a0, _spu_RXX	/* a0 = _spu_RXX (lui+lw macro) */
	or	v0, v0, a1	/* v0 = volume | mode bits */
	addu	v1, v1, a0	/* v1 = voice block */
	sh	v0, 2(v1)	/* voice block + 2 (volume right) = v0 */
$next_voice:
	/* next voice */
	addiu	s4, s4, 1	/* next voice: voice++ */
	slt	v0, s4, s7	/* v0 = (voice < end + 1) */
	bne	v0, zero, $L00b0	/* more voices: loop */
	ori	v0, zero, 0x1	/* delay slot (real work): v0 = 1 (for the next pass) */
$tail:
	/* tail: without nowait, a short counted busy-wait (volatile-style stack counters) */
	bne	fp, zero, $return	/* nowait set: skip the wait */
	addu	v0, zero, zero	/* delay slot (real work): v0 = 0, the return value */
	ori	v0, zero, 0x1	/* v0 = 1 */
	sw	v0, 20(sp)	/* wait value = 1 */
	sw	zero, 16(sp)	/* wait counter = 0 */
	j	$L06dc	/* go to the loop test */
	nop	/* delay slot: nothing to do */
$L06ac:
	lw	v1, 20(sp)	/* v1 = wait value */
	nop	/* load delay slot: nothing to do */
	sll	v0, v1, 1	/* v0 = value * 2 */
	addu	v0, v0, v1	/* v0 = value * 3 */
	sll	v0, v0, 2	/* v0 = value * 12 */
	addu	v0, v0, v1	/* v0 = value * 13 */
	sw	v0, 20(sp)	/* store the new wait value */
	lw	v0, 16(sp)	/* v0 = wait counter */
	nop	/* load delay slot: nothing to do */
	addiu	v0, v0, 1	/* counter + 1 */
	sw	v0, 16(sp)	/* store the counter */
	lw	v0, 16(sp)	/* reload (the counter is volatile-like in the compiler's eyes) */
$L06dc:
	nop	/* load delay slot: nothing to do */
	lw	v0, 16(sp)	/* v0 = wait counter */
	nop	/* load delay slot: nothing to do */
	slti	v0, v0, 2	/* v0 = (counter < 2) */
	bne	v0, zero, $L06ac	/* twice only: loop */
	addu	v0, zero, zero	/* delay slot (real work): v0 = 0, the return value */
$return:
	/* epilogue */
	lw	ra, 76(sp)	/* restore ra */
	lw	fp, 72(sp)	/* restore fp */
	lw	s7, 68(sp)	/* restore s7 */
	lw	s6, 64(sp)	/* restore s6 */
	lw	s5, 60(sp)	/* restore s5 */
	lw	s4, 56(sp)	/* restore s4 */
	lw	s3, 52(sp)	/* restore s3 */
	lw	s2, 48(sp)	/* restore s2 */
	lw	s1, 44(sp)	/* restore s1 */
	lw	s0, 40(sp)	/* restore s0 */
	addiu	sp, sp, 80	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
