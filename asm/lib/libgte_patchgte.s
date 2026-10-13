/*
 * libgte_patchgte.s - LIBGTE module PATCHGTE: _patch_gte.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Placed by
 * config/libgte_patchgte.json; counted as "library assembly", never as matched C and never as
 * certified library.
 *
 * Assembler: GNU as (config/asm_build.json), not ASPSX. The routine addresses its own 16-byte
 * .sbss block absolutely (lui + sw, lui + lw). ASPSX puts a 16-byte .lcomm in .bss under its
 * default -G (the wrong section), and under -G16, or as a label in ".section .sbss", it
 * addresses the block $gp-relative; a load/store with an explicit LO16 offset is a macro to it
 * (it needs $at). GNU as with -G0 writes the absolute %hi/%lo pairs; the unit is preprocessed
 * by CPPPSX, assembled by the GNU assembler pinned in config/toolchain.lock.json (gnu_as) and
 * converted to a PsyQ LNK object by tools/elf_lnk.py.
 *
 * Library:     LIBGTE.LIB (Sony Runtime Library, geometry), module PATCHGTE.
 * Exports:     _patch_gte (.text+0x00, 160 bytes, 0x80068030): 104 bytes of routine followed
 *              by the 56-byte handler prologue that it copies ($stub, .text+0x68).
 * Imports:     EnterCriticalSection (0x8005d0c0) and ExitCriticalSection (0x8005d2e0), both
 *              LIBAPI, and FlushCache (0x800680d0, LIBAPI C68).
 * Data:        .sbss 16 bytes at 0x8008d304 (g_8008d304): the routine keeps its return
 *              address in the first word while it calls out.
 *
 * Nearest archived version: Runtime Library 3.3 LIBGTE.LIB PATCHGTE (160 bytes .text, 16 bytes
 * .sbss, the same export). Three words differ (analysis/data_survey/moddiff.py), all in the
 * copy loop: retail walks the source with k0/k1 where 3.3 uses t2/t1 (0x80068060 lw,
 * 0x80068064 addiu, 0x8006806c bne), and the two lui/addiu pairs that set the loop bounds
 * (relocated, so masked by moddiff) load k0/k1 as well. So the module is "Runtime 3.3 PATCHGTE
 * with the copy loop in k0/k1 instead of t2/t1". The 3.3 object was assembled by ASMPSX
 * (sections numbered from 0xf000); its loop bounds are split HI16/LO16 pairs (lui k0; lui k1;
 * lw; addiu k0; addiu k1), written here with %hi/%lo.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */
	.set	noat		/* at is used explicitly; no macro here needs it */
	.set	nomacro		/* every instruction is written as it is encoded */

/* Kernel calls (psx-spx, "BIOS Function Summary") */
#define B0_TABLE	0xb0	/* B(n) functions: call 0x000000b0 with t1 = n */
#define B_GETC0TABLE	0x56	/* B(56h) GetC0Table: v0 = address of the C0 function table */
#define C0_EXCEPTION	0x18	/* C0 table entry 6 (6 * 4): the kernel's exception handler */

/* Kernel system tables (psx-spx, "BIOS Memory Map") */
#define SYS_TABLES	0x100	/* table of kernel structure pointers at 0x00000100 */
#define SYS_PCB		0x08	/* 0x108: pointer to the process control block */
#define TCB_REGS	0x08	/* thread control block: saved r0 at +8, register n at +8 + 4n */

/*
 * g_8008d304 - PATCHGTE's .sbss block
 *
 * Purpose:     _patch_gte's return address while it calls EnterCriticalSection, B(56h),
 *              FlushCache and ExitCriticalSection (the routine has no stack frame).
 * Layout:      16 bytes (the 3.3 module's whole .sbss); only the first word is used.
 * Users:       _patch_gte.
 */
	.section	.sbss,"aw",@nobits
g_8008d304:
	.space	16

	.text

/*
 * _patch_gte - install the kernel exception handler prologue below
 *
 * Purpose:     void _patch_gte(void). With interrupts disabled, finds the kernel's
 *              exception handler (C0 table entry 6, from B(56h) GetC0Table) and overwrites
 *              its first 14 words with $stub..$stub_end, then flushes the instruction cache
 *              and enables interrupts again.
 * Inputs:      none.
 * Outputs:     none.
 * Clobbers:    v0, v1, t1, t2, k0, k1 and what the callees clobber. ra is kept in
 *              g_8008d304 (no stack frame). k0/k1 belong to the kernel's exception code;
 *              they are free here because interrupts are off.
 * Memory:      writes 56 bytes of kernel code at the exception handler; g_8008d304.
 * Callers:     LIBGTE InitGeom.
 * Callees:     EnterCriticalSection, B(56h) GetC0Table, FlushCache, ExitCriticalSection.
 */
	.globl	_patch_gte
_patch_gte:
	/* Save ra in the .sbss block: an absolute %hi/%lo pair, back to back. */
	lui	AT, %hi(g_8008d304)	/* at = high half of &g_8008d304 */
	sw	ra, %lo(g_8008d304)(AT)	/* g_8008d304 = ra */
	jal	EnterCriticalSection	/* interrupts off while the kernel code is rewritten */
	nop				/* branch delay: nothing to do */
	addiu	t2, zero, B0_TABLE	/* t2 = 0xb0: the B(n) kernel entry */
	jalr	t2			/* v0 = GetC0Table() */
	addiu	t1, zero, B_GETC0TABLE	/* delay slot (real work): t1 = function number 56h */
	/* Copy bounds: k0 = $stub, k1 = $stub_end. Both luis come first and the low halves
	   after the load, so each HI16/LO16 pair is split. */
	lui	k0, %hi($stub)
	lui	k1, %hi($stub_end)
	lw	v0, C0_EXCEPTION(v0)	/* v0 = the exception handler (C0 table entry 6) */
	addiu	k0, k0, %lo($stub)	/* k0 = $stub (load delay of v0 filled) */
	addiu	k1, k1, %lo($stub_end)	/* k1 = $stub_end, one past the last word */
$copy:
	/* Copy one prologue word over the handler. v0 is advanced before the store, which
	   therefore writes to -4(v0). */
	lw	v1, 0(k0)		/* v1 = next prologue word */
	addiu	k0, k0, 4		/* advance the source (load delay: v1 is not used here) */
	addiu	v0, v0, 4		/* advance the destination */
	bne	k0, k1, $copy		/* loop until k0 reaches $stub_end */
	sw	v1, -4(v0)		/* delay slot (real work): store the word */
	jal	FlushCache		/* the handler's old instructions may still be cached */
	nop				/* branch delay: nothing to do */
	jal	ExitCriticalSection	/* interrupts back on */
	nop				/* branch delay: nothing to do */
	lui	ra, %hi(g_8008d304)	/* reload the saved return address (%hi/%lo pair) */
	lw	ra, %lo(g_8008d304)(ra)
	nop				/* load delay: ra is used by the jr */
	jr	ra
	nop				/* return delay: nothing to do */

/*
 * $stub .. $stub_end - the exception handler prologue that _patch_gte installs
 *
 * Purpose:     14 instructions copied word for word over the start of the kernel exception
 *              handler; never executed in place. They save at, v0, v1 and ra into the
 *              current thread's register area and read the Cause register.
 * Inputs:      (as installed) the state at the exception; k0 is the kernel's register.
 * Outputs:     (as installed) at, v0, v1 and ra saved in the current TCB; v0 = Cause.
 * Registers:   k0 = 0x100 -> [0x108] (process control block) -> [PCB] (current TCB) + 8.
 * Memory:      kernel system table at 0x100; the current TCB's register save area.
 * Callers:     none here; once installed it is entered from the CPU exception vector.
 */
$stub:
	nop				/* two leading nops, copied as they are */
	nop
	addiu	k0, zero, SYS_TABLES	/* k0 = 0x100: the kernel's system table */
	lw	k0, SYS_PCB(k0)		/* k0 = [0x108]: the process control block */
	nop				/* load delay */
	lw	k0, 0(k0)		/* k0 = [PCB]: the current thread's TCB */
	nop				/* load delay */
	addi	k0, k0, TCB_REGS	/* k0 = &tcb->reg[0] (the trapping addi, as written) */
	sw	AT, 4(k0)		/* reg[1]  = at */
	sw	v0, 8(k0)		/* reg[2]  = v0 */
	sw	v1, 0xc(k0)		/* reg[3]  = v1 */
	sw	ra, 0x7c(k0)		/* reg[31] = ra */
	mfc0	v0, $13			/* v0 = COP0 register 13, Cause */
	nop				/* load delay: Cause reaches v0 one instruction later */
$stub_end:
