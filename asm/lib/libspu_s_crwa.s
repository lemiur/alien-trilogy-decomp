/*
 * libspu_s_crwa.s - LIBSPU module S_CRWA: SpuClearReverbWorkArea.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_crwa.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_CRWA.
 * Exports:     SpuClearReverbWorkArea (.text+0x0, 452 bytes).
 * Imports:     _spu_rev_startaddr (0x80074164, table of ten start addresses, one per reverb
 *              mode, in 8-byte units), _spu_mem_mode_plus (0x80073cb8, log2 of the unit),
 *              _spu_fd (S_INI .data, 0x80073cf4), _spu_transferCallback (0x80073cc0),
 *              _spu_EVdma (S_INI .data, 0x80073d50, DMA-busy word), _spu_zerobuf (S_INI .data,
 *              0x80073d60, the 1 KB buffer of zeros), _spu_ioctl (0x8005ab0c), _spu_t
 *              (0x8005a72c, the low-level transfer routine), _SpuIsInAllocateArea_ (0x8005990c).
 * Own data:    none.
 *
 * Nearest archived version: Runtime Library 3.3 / 3.4 LIBSPU.LIB S_CRWA (416 bytes; 3.5 and later
 * 412). Relocation-masked, 80 of retail's 113 words are identical to the 3.3 member
 * (builds/lane_tools/lcs.py alignment). The 3.3 member keeps the transfer mode with
 * _spu_transMode and waits on the DMA event with WaitEvent(_spu_EVdma) (imports _spu_transMode,
 * WaitEvent); retail instead reads and sets the driver transfer mode through _spu_ioctl
 * (commands 0x2f and 0x2e), spins on the word _spu_EVdma, and uses a different register
 * allocation (five saved registers against six). The 2.6 / 3.0 / ISA
 * members (328 bytes) call SpuSetTransferStartAddr / SpuWrite / SpuIsTransferCompleted
 * instead. So no archive matches: "Runtime 3.3 S_CRWA with ioctl transfer-mode switching and
 * a polled DMA flag".
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * SpuClearReverbWorkArea - fill the reverb work area of a reverb mode with zeros
 *
 * Purpose:     long SpuClearReverbWorkArea(long mode). mode is the reverb mode 0..9
 *              (SPU_REV_MODE_OFF .. SPU_REV_MODE_PIPE). The area is the SPU RAM from the
 *              start address of that mode, _spu_rev_startaddr[mode] (8-byte units, shifted
 *              by _spu_mem_mode_plus into bytes), up to the top of RAM; mode 0 uses the
 *              fixed 0x80-byte area at 0x7ff80 instead. Fails with -1 when mode >= 10 or
 *              when the area lies inside memory handed out by SpuMalloc
 *              (_SpuIsInAllocateArea_(_spu_rev_startaddr[mode])). Otherwise it:
 *                - makes sure the driver transfer mode is the DMA one (reads it with ioctl
 *                  0x2f; if it was 0 it sets 1 with ioctl 0x2e and restores it at the end);
 *                - parks _spu_transferCallback (saves it, sets it to 0, restores it after);
 *                - writes _spu_zerobuf (1 KB of zeros) over the area in chunks of at most
 *                  0x400 bytes: _spu_t(2, address); _spu_t(1); _spu_t(3, _spu_zerobuf,
 *                  chunk); then spins until _spu_EVdma reads 0 (the DMA finished).
 *              and returns 0.
 * Inputs:      a0 = mode (kept in s0 until reused as the chunk size).
 * Outputs:     v0 = 0 on success, -1 on a bad mode or a work area inside a SpuMalloc block.
 * Clobbers:    v0, v1, a0..a3 (caller-saved) and, through the calls, whatever they clobber.
 *              s0..s4 are saved and restored.
 * Stack:       0x38-byte frame: 0x10 saved callback, 0x14 saved driver transfer mode, 0x18
 *              ioctl argument, s0 0x20, s1 0x24, s2 0x28, s3 0x2c, s4 0x30, ra 0x34.
 * Registers:   s0 = chunk size; s1 = bytes left (before the table pointer); s2 = current SPU
 *              RAM byte address; s3 = 1 while chunks remain; s4 = 1 if the transfer mode was
 *              changed and must be restored.
 * Reads/writes: _spu_rev_startaddr, _spu_mem_mode_plus, _spu_fd, _spu_EVdma (read);
 *              _spu_transferCallback (read, cleared, restored).
 * Callees:     _SpuIsInAllocateArea_, _spu_ioctl, _spu_t.
 * Callers:     SpuSetReverbModeParam (S_SRMP, libspu_s_srmp.s) at 0x8005981c.
 */
	.globl	SpuClearReverbWorkArea
SpuClearReverbWorkArea:
	/* prologue; s4 = 0; mode >= 10 -> -1 */
	addiu	sp, sp, -56	/* allocate the 0x38-byte frame */
	sw	s0, 32(sp)	/* save s0 */
	addu	s0, a0, zero	/* s0 = mode */
	sw	s4, 48(sp)	/* save s4 */
	sltiu	v0, s0, 10	/* v0 = (mode < 10) */
	sw	ra, 52(sp)	/* save ra */
	sw	s3, 44(sp)	/* save s3 */
	sw	s2, 40(sp)	/* save s2 */
	sw	s1, 36(sp)	/* save s1 */
	sw	zero, 16(sp)	/* saved callback = 0 (nothing saved yet) */
	beq	v0, zero, $fail	/* mode >= 10: fail */
	addu	s4, zero, zero	/* delay slot (real work): s4 = 0, the transfer mode is unchanged so far */
	/* the work area must not overlap memory given out by SpuMalloc */
	la	v1, _spu_rev_startaddr	/* ... = &_spu_rev_startaddr[0] (la macro: lui + addiu) */
	sll	v0, s0, 2	/* v0 = mode * 4 */
	addu	s1, v0, v1	/* s1 = &_spu_rev_startaddr[mode] */
	lw	a0, 0(s1)	/* a0 = _spu_rev_startaddr[mode] */
	jal	_SpuIsInAllocateArea_	/* _SpuIsInAllocateArea_(start) */
	nop	/* call delay: nothing to do */
	beq	v0, zero, $table	/* not inside a SpuMalloc block: go on */
	nop	/* branch delay: nothing to do */
$fail:
	j	$return	/* fail: return -1 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
$table:
	/* mode 0: the fixed 0x80-byte area at 0x7ff80; other modes: from _spu_rev_startaddr[mode]
	   up to the top of RAM. s1 = length in bytes, s2 = start byte address. */
	bne	s0, zero, $tabled	/* mode != 0: use the table entry */
	lui	v0, 0x1	/* delay slot (real work): v0 = 0x10000, the top of RAM in 8-byte units */
	lw	v0, _spu_mem_mode_plus	/* mode 0: v0 = _spu_mem_mode_plus (lui+lw macro) */
	ori	v1, zero, 0x10	/* v1 = 0x10 */
	sllv	s1, v1, v0	/* s1 = 0x10 << plus = 0x80 bytes (size of the mode-0 area) */
	ori	v1, zero, 0xfff0	/* v1 = 0xfff0 */
	j	$mode	/* join */
	sllv	s2, v1, v0	/* delay slot (real work): s2 = 0xfff0 << plus = 0x7ff80, its start address */
$tabled:
	lw	a0, 0(s1)	/* a0 = start (8-byte units) */
	lw	v1, _spu_mem_mode_plus	/* v1 = _spu_mem_mode_plus (lui+lw macro) */
	subu	v0, v0, a0	/* v0 = 0x10000 - start */
	sllv	s1, v0, v1	/* s1 = (0x10000 - start) << plus: bytes up to the top of RAM */
	sllv	s2, a0, v1	/* s2 = start << plus: the start byte address */
$mode:
	/* make sure the driver is in the DMA transfer mode (value 1) for the duration */
	ori	a1, zero, 0x2f	/* a1 = ioctl command 0x2f (read the driver transfer mode) */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x2f, &mode_word) */
	addiu	a2, sp, 20	/* delay slot (real work): a2 = &mode_word (0x14(sp)) */
	lw	v0, 20(sp)	/* v0 = the driver transfer mode */
	nop	/* lw load delay slot: nothing to do */
	bne	v0, zero, $callback	/* already non-zero (DMA): leave it */
	ori	a1, zero, 0x2e	/* delay slot (real work): a1 = ioctl command 0x2e (set the driver transfer mode) */
	addiu	a2, sp, 24	/* a2 = &arg (0x18(sp)) */
	ori	s4, zero, 0x1	/* s4 = 1: remember to restore the mode afterwards */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	ori	v0, zero, 0x1	/* v0 = 1 */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x2e, &arg) with arg = 1 */
	sw	v0, 24(sp)	/* delay slot (real work): arg = 1 */
$callback:
	/* park the transfer-complete callback: saved at 0x10(sp), then cleared */
	lw	v0, _spu_transferCallback	/* v0 = _spu_transferCallback (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	beq	v0, zero, $chunk	/* no callback installed: nothing to park */
	ori	s3, zero, 0x1	/* delay slot (real work): s3 = 1, "chunks remain" */
	lw	v0, _spu_transferCallback	/* v0 = _spu_transferCallback (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	sw	v0, 16(sp)	/* saved callback = v0 */
	sw	zero, _spu_transferCallback	/* macro "sw zero, _spu_transferCallback": lui AT then the store through AT */
$chunk:
	/* chunk loop: write at most 0x400 bytes of zeros per pass */
	sltiu	v0, s1, 1025	/* v0 = (bytes left < 0x401): last chunk? */
	bne	v0, zero, $last	/* yes: the chunk is what is left */
	addu	s0, s1, zero	/* delay slot (real work): s0 = bytes left */
	j	$write	/* no: a full 0x400-byte chunk, more follow */
	ori	s0, zero, 0x400	/* delay slot (real work): s0 = 0x400 */
$last:
	addu	s3, zero, zero	/* last chunk: s3 = 0, no more chunks */
$write:
	ori	a0, zero, 0x2	/* a0 = 2: _spu_t operation "set the SPU RAM address" */
	jal	_spu_t	/* _spu_t(2, address) */
	addu	a1, s2, zero	/* delay slot (real work): a1 = s2, the current SPU RAM byte address */
	jal	_spu_t	/* _spu_t(1) */
	ori	a0, zero, 0x1	/* delay slot (real work): a0 = 1 */
	ori	a0, zero, 0x3	/* a0 = 3: _spu_t operation "write from main RAM" */
	la	a1, _spu_zerobuf	/* ... (la macro: lui + addiu) */
	jal	_spu_t	/* _spu_t(3, _spu_zerobuf, chunk) */
	addu	a2, s0, zero	/* delay slot (real work): a2 = chunk size */
$spin:
	lw	v0, _spu_EVdma	/* v0 = _spu_EVdma (lui+lw macro), re-read on every pass */
	nop	/* lw load delay slot: nothing to do */
	bne	v0, zero, $spin	/* DMA still busy: spin */
	nop	/* branch delay: nothing to do */
	addiu	s1, s1, -1024	/* bytes left -= 0x400 */
	bne	s3, zero, $chunk	/* more chunks: loop */
	addiu	s2, s2, 1024	/* delay slot (real work): address += 0x400 */
	/* undo: restore the driver transfer mode and the callback, return 0 */
	beq	s4, zero, $restore	/* transfer mode was not changed: skip the restore */
	ori	a1, zero, 0x2e	/* delay slot (real work): a1 = ioctl command 0x2e */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* _spu_ioctl(_spu_fd, 0x2e, &saved_mode): restore the old transfer mode */
	addiu	a2, sp, 20	/* delay slot (real work): a2 = &saved_mode (0x14(sp)) */
$restore:
	lw	v0, 16(sp)	/* v0 = saved callback */
	nop	/* lw load delay slot: nothing to do */
	beq	v0, zero, $return	/* none was parked: just return 0 */
	addu	v0, zero, zero	/* delay slot (real work): v0 = 0, the return value */
	lw	v0, 16(sp)	/* v0 = saved callback */
	nop	/* lw load delay slot: nothing to do */
	sw	v0, _spu_transferCallback	/* macro "sw v0, _spu_transferCallback": lui AT then the store through AT */
	addu	v0, zero, zero	/* v0 = 0, the return value */
$return:
	/* epilogue; v0 = 0 or -1 */
	lw	ra, 52(sp)	/* restore ra */
	lw	s4, 48(sp)	/* restore s4 */
	lw	s3, 44(sp)	/* restore s3 */
	lw	s2, 40(sp)	/* restore s2 */
	lw	s1, 36(sp)	/* restore s1 */
	lw	s0, 32(sp)	/* restore s0 */
	addiu	sp, sp, 56	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
