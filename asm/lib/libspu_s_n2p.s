/*
 * libspu_s_n2p.s - LIBSPU module S_N2P: _spu_note2pitch, _spu_pitch2note.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Placed by
 * config/libspu_s_n2p.json and the "libspu_s_n2p" entry of config/data.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Assembler: ASPSX 2.08 (A208, config/asm_build.json), not the project's ASPSX 2.34.
 * _spu_pitch2note's two checked signed divisions are written as the div macro; ASPSX 2.08
 * expands each to retail's exact sequence, ending in the overflow trap word 0x00001770
 * ("tge zero, zero, 0x5d"), where ASPSX 2.21 and 2.34 write "break 6". That expansion is the
 * evidence that Sony assembled this variant module with the Runtime 3.0-era assembler (the
 * variant runtime is the one the 3.6 kanji samples KANJIFNT MAIN.EXE and SJISCODE CODEVIEW.EXE
 * link: LIBGPU sys.c 1.104, intr.c 1.71). The table load and the reciprocal are GCC's lhu and
 * li macros too. ASPSX 2.08 knows no negu: the negation is written subu.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_N2P.
 * Exports:     _spu_note2pitch (.text+0x000, 244 bytes, 0x8005913c),
 *              _spu_pitch2note (.text+0x0f4, 344 bytes, 0x80059230).
 * Imports:     none.
 * Own data:    .data 388 bytes at 0x80073b00: g_73b00, the 193-entry pitch table (386 bytes)
 *              and 2 bytes of padding.
 *
 * Nearest archived version: Runtime Library 3.3 LIBSPU.LIB S_N2P (588 bytes .text, 386 bytes
 * .data, the same exports). Two words differ (analysis/data_survey/moddiff.py): the
 * integer-overflow traps of _spu_pitch2note's divisions at 0x800592b0 and 0x80059314 are
 * "break 6" (0x0006000d) in 3.3 and 0x00001770 ("tge zero, zero, 0x5d", trap if 0 >= 0: always)
 * in retail. The .data is byte-identical. So the module is "Runtime 3.3 S_N2P with tge overflow
 * traps instead of break 6": the same compiled code, assembled by ASPSX 2.08.
 *
 * The routines are compiled C (GCC register use, div macros, division by 12 through the
 * reciprocal 0x2aaaaaab); they are transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */

#define RECIP_6		0x2aaaaaab	/* 2^32 / 6, rounded up: high word of x * RECIP_6 = x / 6 */

/*
 * g_73b00 - pitch table, one octave in 1/16 semitone steps
 *
 * Purpose:     g_73b00[i] = 0x1000 * 2^(i / 192): the SPU pitch (0x1000 = the sample's own
 *              rate) of i sixteenths of a semitone above the base note, i = 0..192.
 * Layout:      193 u16 (0x1000 .. 0x2000), then 2 bytes of padding to the word boundary.
 * Users:       _spu_note2pitch.
 */
	.data
g_73b00:
	.half	0x1000, 0x100e, 0x101d, 0x102c, 0x103b, 0x104a, 0x1059, 0x1068	/* [0..7] */
	.half	0x1078, 0x1087, 0x1096, 0x10a5, 0x10b5, 0x10c4, 0x10d4, 0x10e3	/* [8..15] */
	.half	0x10f3, 0x1103, 0x1113, 0x1122, 0x1132, 0x1142, 0x1152, 0x1162	/* [16..23] */
	.half	0x1172, 0x1182, 0x1193, 0x11a3, 0x11b3, 0x11c4, 0x11d4, 0x11e5	/* [24..31] */
	.half	0x11f5, 0x1206, 0x1216, 0x1227, 0x1238, 0x1249, 0x125a, 0x126b	/* [32..39] */
	.half	0x127c, 0x128d, 0x129e, 0x12af, 0x12c1, 0x12d2, 0x12e3, 0x12f5	/* [40..47] */
	.half	0x1306, 0x1318, 0x132a, 0x133c, 0x134d, 0x135f, 0x1371, 0x1383	/* [48..55] */
	.half	0x1395, 0x13a7, 0x13ba, 0x13cc, 0x13de, 0x13f1, 0x1403, 0x1416	/* [56..63] */
	.half	0x1428, 0x143b, 0x144e, 0x1460, 0x1473, 0x1486, 0x1499, 0x14ac	/* [64..71] */
	.half	0x14bf, 0x14d3, 0x14e6, 0x14f9, 0x150d, 0x1520, 0x1534, 0x1547	/* [72..79] */
	.half	0x155b, 0x156f, 0x1583, 0x1597, 0x15ab, 0x15bf, 0x15d3, 0x15e7	/* [80..87] */
	.half	0x15fb, 0x1610, 0x1624, 0x1638, 0x164d, 0x1662, 0x1676, 0x168b	/* [88..95] */
	.half	0x16a0, 0x16b5, 0x16ca, 0x16df, 0x16f4, 0x170a, 0x171f, 0x1734	/* [96..103] */
	.half	0x174a, 0x175f, 0x1775, 0x178b, 0x17a1, 0x17b6, 0x17cc, 0x17e2	/* [104..111] */
	.half	0x17f9, 0x180f, 0x1825, 0x183b, 0x1852, 0x1868, 0x187f, 0x1896	/* [112..119] */
	.half	0x18ac, 0x18c3, 0x18da, 0x18f1, 0x1908, 0x191f, 0x1937, 0x194e	/* [120..127] */
	.half	0x1965, 0x197d, 0x1995, 0x19ac, 0x19c4, 0x19dc, 0x19f4, 0x1a0c	/* [128..135] */
	.half	0x1a24, 0x1a3c, 0x1a55, 0x1a6d, 0x1a85, 0x1a9e, 0x1ab7, 0x1acf	/* [136..143] */
	.half	0x1ae8, 0x1b01, 0x1b1a, 0x1b33, 0x1b4c, 0x1b66, 0x1b7f, 0x1b98	/* [144..151] */
	.half	0x1bb2, 0x1bcc, 0x1be5, 0x1bff, 0x1c19, 0x1c33, 0x1c4d, 0x1c67	/* [152..159] */
	.half	0x1c82, 0x1c9c, 0x1cb7, 0x1cd1, 0x1cec, 0x1d07, 0x1d22, 0x1d3d	/* [160..167] */
	.half	0x1d58, 0x1d73, 0x1d8e, 0x1da9, 0x1dc5, 0x1de0, 0x1dfc, 0x1e18	/* [168..175] */
	.half	0x1e34, 0x1e50, 0x1e6c, 0x1e88, 0x1ea4, 0x1ec1, 0x1edd, 0x1efa	/* [176..183] */
	.half	0x1f16, 0x1f33, 0x1f50, 0x1f6d, 0x1f8a, 0x1fa7, 0x1fc5, 0x1fe2	/* [184..191] */
	.half	0x2000	/* [192..192] */
	.half	0		/* padding to the word boundary */

	.text

/*
 * _spu_note2pitch - SPU pitch of a note relative to a sample's centre note
 *
 * Purpose:     u_short _spu_note2pitch(short cen_note, short cen_fine, u_short note,
 *              u_short fine). The fine values are in 1/128 semitone. With f = (cen_fine + fine)
 *              / 8 (sixteenths, rounded toward zero; whole semitones carried out of it while
 *              f >= 16) and n = note + 60 - cen_note + carry, the result is
 *              g_73b00[(n % 12) * 16 + f] shifted by n / 12 - 5 octaves.
 * Inputs:      a0 = cen_note, a1 = cen_fine, a2 = note, a3 = fine.
 * Outputs:     v0 = pitch (u16).
 * Clobbers:    v0, v1, a0-a3, t0, at, hi/lo. Leaf routine: no stack frame, no calls.
 * Memory:      reads g_73b00.
 * Callers:     LIBSPU voice code: the jal at 0x80058acc (library gap 0x80058980..).
 * Callees:     none.
 */
	.globl	_spu_note2pitch
_spu_note2pitch:
	/* f = (fine + cen_fine) / 8, as C's signed division (add 7 to a negative sum). */
	andi	a3, a3, 0xffff		/* a3 = fine (u16) */
	andi	a1, a1, 0xffff		/* a1 = cen_fine (u16) */
	addu	v0, a3, a1		/* v0 = fine + cen_fine */
	bgez	v0, $n2p_div8
	move	t0, a0			/* delay slot (real work): t0 = cen_note */
	addiu	v0, v0, 7		/* negative: round toward zero */
$n2p_div8:
	sra	v0, v0, 3		/* v0 = f */
	move	a1, v0			/* a1 = f */
	slti	v0, v0, 0x10		/* f < 16? */
	bnez	v0, $n2p_fine_ok
	move	v1, zero		/* delay slot (real work): v1 = carried semitones = 0 */
$n2p_carry:
	/* while ((short)f >= 16) { f -= 16; carry++; } */
	addiu	v0, a1, -0x10		/* f -= 16 */
	move	a1, v0
	sll	v0, v0, 0x10		/* (short)f ... */
	sra	v0, v0, 0x10
	slti	v0, v0, 0x10		/* ... < 16? */
	beqz	v0, $n2p_carry
	addiu	v1, v1, 1		/* delay slot (real work): carry++ */
$n2p_fine_ok:
	/* (short)f < 0 clamps to 0. */
	sll	v0, a1, 0x10		/* sign of (short)f */
	bgez	v0, $n2p_semitone
	lui	a0, RECIP_6 >> 16	/* delay slot (real work): a0 = RECIP_6 (high half) */
	move	a1, zero		/* f = 0 */
$n2p_semitone:
	/* n = (short)(note + 60 - cen_note + carry); octave = n / 12 (signed, through the
	   reciprocal of 6 and a further halving); semitone = n - 12 * octave. */
	ori	a0, a0, RECIP_6 & 0xffff	/* a0 = RECIP_6 */
	addiu	v0, a2, 0x3c		/* v0 = note + 60 */
	subu	v0, v0, t0		/* - cen_note */
	addu	v0, v1, v0		/* + carry */
	sll	v0, v0, 0x10
	sra	v1, v0, 0x10		/* v1 = n (short) */
	mult	v1, a0			/* hi = n * RECIP_6 >> 32 = n / 6 */
	sra	v0, v0, 0x1f		/* v0 = n < 0 ? -1 : 0 */
	mfhi	a0
	sra	a0, a0, 1		/* a0 = floor(n / 12) */
	subu	a0, a0, v0		/* + 1 for negative n: round toward zero, a0 = octave */
	sll	v0, a0, 1		/* v0 = octave * 12 ... */
	addu	v0, v0, a0
	sll	v0, v0, 2
	subu	v1, v1, v0		/* v1 = semitone = n - 12 * octave */
	sll	v1, v1, 0x10
	sra	v1, v1, 0xc		/* v1 = (short)semitone * 16 */
	sll	v0, a1, 0x10
	sra	v0, v0, 0x10		/* v0 = (short)f */
	addu	v1, v1, v0		/* v1 = table index */
	sll	v1, v1, 1		/* byte offset of the u16 entry */
	lhu	v1, g_73b00(v1)		/* v1 = g_73b00[index]; macro: lui at + addiu at (HI16/LO16
					   of g_73b00), addu at, at, v1, lhu v1, 0(at) */
	/* shift = octave - 5: left for positive, right for negative. */
	addiu	a0, a0, -5
	sll	a0, a0, 0x10
	sra	v0, a0, 0x10		/* v0 = (short)(octave - 5) */
	blez	v0, $n2p_not_up
	nop				/* branch delay: nothing to do */
	j	$n2p_return
	sllv	v1, v1, v0		/* delay slot (real work): pitch <<= shift */
$n2p_not_up:
	bgez	v0, $n2p_return		/* shift == 0: keep the table value */
	nop				/* branch delay: nothing to do */
	andi	v1, v1, 0xffff
	subu	v0, zero, v0
	srav	v1, v1, v0		/* pitch >>= -shift */
$n2p_return:
	jr	ra
	andi	v0, v1, 0xffff		/* delay slot (real work): return (u16)pitch */

/*
 * _spu_pitch2note - note of an SPU pitch relative to a sample's centre note
 *
 * Purpose:     u_short _spu_pitch2note(short cen_note, short cen_fine, u_short pitch). The
 *              inverse of _spu_note2pitch with a linear interpolation inside the octave: the
 *              octave is the highest set bit b of the pitch, the semitone is
 *              (pitch - 2^b) * 12 / 2^b and the fine part the remainder scaled to 1/128
 *              semitone. Returns note << 8 | fine for a note 0..127, else 0xffff (-1).
 * Inputs:      a0 = cen_note, a1 = cen_fine, a2 = pitch.
 * Outputs:     v0 = note << 8 | fine, or -1.
 * Clobbers:    v0, v1, a0-a3, t0, t1, at, hi/lo. Leaf routine: no stack frame, no calls.
 * Callers:     none in retail (linked as part of its module).
 * Callees:     none.
 */
	.globl	_spu_pitch2note
_spu_pitch2note:
	move	t0, a0			/* t0 = cen_note */
	move	t1, a1			/* t1 = cen_fine */
	move	a3, zero		/* a3 = b = highest set bit (0 if none) */
	move	v1, zero		/* v1 = bit index */
	andi	a1, a2, 0xffff		/* a1 = pitch (u16) */
$p2n_scan:
	srav	v0, a1, v1
	andi	v0, v0, 1		/* bit v1 of the pitch */
	beqz	v0, $p2n_next
	nop				/* branch delay: nothing to do */
	move	a3, v1			/* set: b = v1 */
$p2n_next:
	addiu	v1, v1, 1
	slti	v0, v1, 0x10
	bnez	v0, $p2n_scan		/* bits 0..15 */
	ori	v0, zero, 1		/* delay slot (real work): v0 = 1 (also after the loop) */
	addiu	a1, a3, 1
	sllv	a1, v0, a1		/* a1 = 2^(b+1) */
	sllv	v0, v0, a3		/* v0 = 2^b */
	subu	a1, a1, v0		/* a1 = 2^b: the width of the octave */
	andi	a0, a2, 0xffff
	subu	a0, a0, v0		/* a0 = pitch - 2^b */
	sll	a2, a0, 1		/* a2 = a0 * 12 */
	addu	a2, a2, a0
	sll	a2, a2, 2
	/* a2 = a2 / a1: ASPSX 2.08's checked signed division macro, 11 words, using $at:
	     div zero, a2, a1; bnez a1, 1f; nop; break 7 (divide by zero);
	  1: addiu at, zero, -1; bne a1, at, 2f; lui at, 0x8000 (delay slot);
	     bne a2, at, 2f; nop; tge zero, zero, 0x5d (0x00001770, INT_MIN / -1 overflow);
	  2: mflo a2. The div runs while the checks branch around the traps. */
	div	a2, a2, a1		/* a2 = semitone within the octave */
	nop				/* lo hazard: no mult/div in the next instruction */
	mult	a1, a2
	mflo	v1			/* v1 = semitone * 2^b */
	li	v0, RECIP_6		/* v0 = RECIP_6 (macro: lui + ori) */
	mult	v1, v0			/* hi = v1 / 6 */
	sra	v1, v1, 0x1f		/* v1 = sign of the product */
	mfhi	v0
	sra	v0, v0, 1		/* v0 = floor(semitone * 2^b / 12) */
	subu	v0, v0, v1		/* rounded toward zero */
	subu	a0, a0, v0		/* a0 = remainder below the semitone */
	sll	v1, a0, 1		/* v1 = a0 * 3 << 9 = a0 * 12 * 128 */
	addu	v1, v1, a0
	sll	v1, v1, 9
	/* v1 = v1 / a1, the same 11-word div macro (traps at break 7 and 0x00001770). */
	div	v1, v1, a1		/* v1 = fine part in 1/128 semitone */
	/* note = cen_note + (b - 12) * 12 + semitone; fine += cen_fine, carrying 128 into the
	   note. */
	andi	a1, t0, 0xffff		/* a1 = cen_note */
	addiu	a0, a3, -0xc
	sll	v0, a0, 1		/* v0 = (b - 12) * 12 */
	addu	v0, v0, a0
	sll	v0, v0, 2
	addu	a1, a1, v0
	addu	a1, a1, a2		/* a1 = note */
	andi	v0, t1, 0xffff
	addu	v1, v0, v1		/* v1 = cen_fine + fine */
	bgez	v1, $p2n_fine_div
	move	v0, v1			/* delay slot (real work): v0 = sum */
	addiu	v0, v1, 0x7f		/* negative: round toward zero */
$p2n_fine_div:
	sra	a0, v0, 7
	sll	v0, a0, 7
	subu	a0, v1, v0		/* a0 = sum % 128 */
	slti	v0, v1, 0x80
	xori	v0, v0, 1		/* v0 = (sum >= 128) */
	addu	a1, a1, v0		/* carry into the note */
	bltz	a1, $p2n_out_of_range	/* note < 0 */
	slti	v0, a1, 0x80		/* delay slot (real work): note < 128? */
	beqz	v0, $p2n_out_of_range	/* note >= 128 */
	sll	v0, a1, 8		/* delay slot (real work): v0 = note << 8 */
	j	$p2n_return
	or	v0, v0, a0		/* delay slot (real work): v0 = note << 8 | fine */
$p2n_out_of_range:
	addiu	v0, zero, -1		/* not a MIDI note: return -1 */
$p2n_return:
	jr	ra
	nop				/* return delay: nothing to do */
