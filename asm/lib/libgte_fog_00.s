/*
 * libgte_fog_00.s - LIBGTE module FOG_00: SetFogFar.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Placed by
 * config/libgte_fog_00.json; counted as "library assembly", never as matched C and never as
 * certified library.
 *
 * Assembler: ASPSX 2.08 (A208, config/asm_build.json), not the project's ASPSX 2.34. The
 * routine's checked division is written as the div macro; ASPSX 2.08 expands it to retail's
 * exact sequence, ending in the overflow trap word 0x00001770 ("tge zero, zero, 0x5d"), where
 * ASPSX 2.21 and 2.34 write "break 6". That expansion is the evidence that Sony assembled this
 * variant module with the Runtime 3.0-era assembler (the variant runtime is the one the 3.6 kanji
 * samples KANJIFNT MAIN.EXE and SJISCODE CODEVIEW.EXE link: LIBGPU sys.c 1.104, intr.c 1.71).
 * ASPSX 2.08 knows no negu: the negation is written subu.
 *
 * Library:     LIBGTE.LIB (Sony Runtime Library, geometry), module FOG_00.
 * Exports:     SetFogFar (.text+0x00, 92 bytes, 0x800684a0)
 * Imports:     SetDQA (0x8006731c) and SetDQB (0x80067328), both LIBGTE REG03.
 * Data:        none.
 *
 * Nearest archived version: Runtime Library 3.3 LIBGTE.LIB FOG_00 (92 bytes, the same export).
 * One word differs (analysis/data_survey/moddiff.py): the integer-overflow trap of the
 * compiled division at +0x2c is "break 6" (0x0006000d) in 3.3 and 0x00001770 in retail, which
 * is "tge zero, zero, 0x5d" (trap if 0 >= 0: always). Both trap unconditionally on the
 * INT_MIN / -1 overflow path, so the module is "Runtime 3.3 FOG_00 with a tge overflow trap
 * instead of break 6": the same compiled code, assembled by ASPSX 2.08. The routine is compiled C
 * (GCC's div macro and frame).
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */

#define DQB_FOG		0x0140	/* SetDQB argument >> 16: DQB = 0x01400000 (1.25 in 8.24) */

	.text

/*
 * SetFogFar - set the depth-cue (fog) parameters from the fog distance
 *
 * Purpose:     void SetFogFar(long a, long h) (libgte.h): a = distance at which the fog is
 *              complete, h = projection plane distance. Sets DQA = (-a * 64) / h through
 *              SetDQA, and DQB = 0x01400000 through SetDQB.
 * Inputs:      a0 = a, a1 = h.
 * Outputs:     none (GTE control registers DQA (27) and DQB (28) via the callees).
 * Clobbers:    a0, at, hi/lo and what SetDQA/SetDQB clobber; ra saved.
 * Memory:      stack frame 0x18 (ra at 0x10).
 * Callers:     game code (fog setup).
 * Callees:     SetDQA, SetDQB.
 */
	.globl	SetFogFar
SetFogFar:
	subu	a0, zero, a0		/* a0 = -a (GCC's negu) */
	sll	a0, a0, 6		/* a0 = -a * 64 */
	/* a0 = a0 / h: ASPSX 2.08's checked signed division macro, 11 words, using $at:
	     div zero, a0, a1; bnez a1, 1f; nop; break 7 (divide by zero);
	  1: addiu at, zero, -1; bne a1, at, 2f; lui at, 0x8000 (delay slot);
	     bne a0, at, 2f; nop; tge zero, zero, 0x5d (0x00001770, INT_MIN / -1 overflow);
	  2: mflo a0. The div runs while the checks branch around the traps. */
	div	a0, a0, a1		/* a0 = DQA = (-a * 64) / h */
	addiu	sp, sp, -0x18		/* frame: ra at 0x10 */
	sw	ra, 0x10(sp)
	jal	SetDQA			/* SetDQA(DQA) */
	nop				/* branch delay: nothing to do */
	jal	SetDQB			/* SetDQB(0x01400000) */
	lui	a0, DQB_FOG		/* delay slot (real work): a0 = 0x01400000 */
	lw	ra, 0x10(sp)		/* restore */
	addiu	sp, sp, 0x18		/* release the frame */
	jr	ra
	nop				/* return delay: nothing to do */
