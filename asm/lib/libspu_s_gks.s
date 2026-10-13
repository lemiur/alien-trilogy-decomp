/*
 * libspu_s_gks.s - LIBSPU module S_GKS: SpuGetKeyStatus.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_gks.json and the
 * "libspu_s_gks" entry of config/data.json; counted as "library assembly", never as matched C
 * and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_GKS.
 * Exports:     SpuGetKeyStatus (.text+0x0, 148 bytes).
 * Imports:     _spu_keystat (LIBSPU's software copy of the key-on mask, written by
 *              SpuSetKey; 0x80073d4c, bound by the certified LIBCD C_010 relocations too).
 * Own data:    _spu_RXX, a module-local copy of the SPU register base pointer
 *              (.data+0x0 = 0x1f801c00), at 0x80073afc.
 *
 * Nearest archived version: Runtime Library 3.3 LIBSPU.LIB S_GKS. Its 148 text bytes are
 * identical to these with relocations masked (analysis/data_survey/moddiff.py, 0 differing
 * words), but the 3.3 object has no .data and imports a single global _spu_RXX from the SPU
 * module (SPU .data+8). Retail S_GKS reads its own word at 0x80073afc instead: one of four
 * words holding 0x1f801c00 in retail .data, each read only by one module, in text order
 * (0x80073af8 SpuSetVoiceAttr variant, 0x80073afc S_GKS, 0x80073c90 the SPU main module,
 * 0x80073cf0 S_SCA; analysis/runtime_variant_diagnostics.py). A global _spu_RXX defined in
 * several modules would not link, so this rebuild gives each module a static copy. That
 * is the whole difference from 3.3: "Runtime 3.3 S_GKS with a module-local _spu_RXX
 * instead of the SPU module's global".
 *
 * The routine is compiled C (GCC register use, a "break" written as a jump back into
 * the loop exit); it is transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */

/* SPU voice registers: voice n occupies 16 bytes at _spu_RXX + 16 * n */
#define SPU_VOICE_SHIFT	4	/* log2 of the 16-byte voice register block */
#define SPU_VOICE_ENVX	0x0c	/* current ADSR envelope volume of the voice */
#define SPU_NVOICES	24	/* voices 0..23 */

/* Results (libspu.h): key-on/off state combined with "envelope still sounding" */
#define SPU_OFF		0	/* keyed off, envelope silent */
#define SPU_ON		1	/* keyed on, envelope sounding */
#define SPU_OFF_ENV_ON	2	/* keyed off, release still sounding */
#define SPU_ON_ENV_OFF	3	/* keyed on, envelope already silent */
#define SPU_ERROR	-1	/* no voice bit set in the argument */

/*
 * _spu_RXX - module-local SPU register base
 *
 * Purpose:     the address of the SPU register file (0x1f801c00), read through a pointer
 *              as all LIBSPU code does. Static: the retail module has its own word.
 * Layout:      one 32-bit pointer.
 * Users:       SpuGetKeyStatus only (lui/lw pair below).
 */
	.data
_spu_RXX:
	.word	0x1f801c00	/* SPU register file base (volatile u_short *) */

	.text

/*
 * SpuGetKeyStatus - key-on and envelope state of one voice
 *
 * Purpose:     long SpuGetKeyStatus(unsigned long voice_bit). Finds the lowest voice whose
 *              bit (SPU_0CH << n) is set in voice_bit, then combines that voice's bit in
 *              _spu_keystat (keyed on by SpuSetKey) with its ENVX register (envelope still
 *              non-zero) into SPU_ON / SPU_ON_ENV_OFF / SPU_OFF_ENV_ON / SPU_OFF.
 * Inputs:      a0 = voice_bit, a mask of SPU_0CH..SPU_23CH.
 * Outputs:     v0 = SPU_ON (1), SPU_ON_ENV_OFF (3), SPU_OFF_ENV_ON (2), SPU_OFF (0), or
 *              SPU_ERROR (-1) when no bit of voices 0..23 is set.
 * Clobbers:    v0, v1, a0, a1, a2. Leaf routine: no stack frame, no calls.
 * Reads:       _spu_keystat (memory), _spu_RXX (the module's own pointer) and the
 *              voice's ENVX hardware register through it.
 * Scratchpad:  none.
 * Callers:     voice_play (src/audio.c), which polls each voice for a free one.
 * Callees:     none.
 */
	.globl	SpuGetKeyStatus
SpuGetKeyStatus:
	/* Scan bits 0..23 of voice_bit for the first set one: a1 = voice, v1 = i, a2 = 1 */
	addiu	a1, zero, SPU_ERROR	/* voice = -1 (none found) */
	addu	v1, zero, zero		/* i = 0 */
	ori	a2, zero, 1		/* a2 = 1, the bit to shift */
	sllv	v0, a2, v1		/* v0 = 1 << i (first iteration) */
$scan:
	and	v0, a0, v0		/* voice_bit & (1 << i) */
	bnez	v0, $found		/* bit set: this is the voice */
	nop				/* branch delay: nothing to do */
	addiu	v1, v1, 1		/* i++ */
	slti	v0, v1, SPU_NVOICES	/* i < 24 ? */
	bnez	v0, $scan
	sllv	v0, a2, v1		/* delay slot (real work): next 1 << i */

	/* Loop exit (also reached from $found with a1 = voice): no voice -> SPU_ERROR */
$scanned:
	addiu	v0, zero, SPU_ERROR	/* v0 = -1: the return value if voice == -1 */
	bne	a1, v0, $have_voice
	sll	v1, a1, SPU_VOICE_SHIFT	/* delay slot: v1 = voice * 16, the register block offset */
	j	$return			/* no voice: return SPU_ERROR */
	nop				/* jump delay: nothing to do */

	/* "break" out of the scan, compiled as a jump back to the loop exit */
$found:
	j	$scanned
	addu	a1, v1, zero		/* delay slot (real work): voice = i */

$have_voice:
	/* v0 = _spu_RXX (own copy), a0 = _spu_keystat; each a lui/lw macro pair, no AT */
	lw	v0, _spu_RXX		/* v0 = SPU register base */
	lw	a0, _spu_keystat	/* a0 = keyed-on voice mask */
	addu	v1, v1, v0		/* v1 = this voice's register block */
	ori	v0, zero, 1
	sllv	v0, v0, a1		/* v0 = 1 << voice */
	and	v0, v0, a0		/* keyed on? */
	lhu	v1, SPU_VOICE_ENVX(v1)	/* v1 = ENVX, current envelope volume (hardware read) */
	beqz	v0, $keyed_off
	nop				/* branch delay; also the lhu load delay */

	/* Keyed on: SPU_ON while the envelope sounds, SPU_ON_ENV_OFF once it is silent */
	beqz	v1, $return
	ori	v0, zero, SPU_ON_ENV_OFF	/* delay slot (real work): result if envelope silent */
	j	$return
	ori	v0, zero, SPU_ON	/* delay slot (real work): envelope sounding */

	/* Keyed off: SPU_OFF_ENV_ON (2) while the release sounds, else SPU_OFF (0) */
$keyed_off:
	sltu	v0, zero, v1		/* v0 = (envelope != 0) */
	sll	v0, v0, 1		/* 0 -> SPU_OFF, 1 -> SPU_OFF_ENV_ON */
$return:
	jr	ra
	nop				/* return delay: nothing to do */
