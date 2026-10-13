/*
 * libspu_s_m_m.s - LIBSPU module S_M_M: SpuMalloc.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_m_m.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_M_M.
 * Exports:     SpuMalloc (.text+0x0, 716 bytes).
 * Imports:     _spu_mem_mode_plus (0x80073cb8, log2 of the allocation unit, initially 3),
 *              _spu_rev_reserve_wa (S_INI .data, 0x80073d00), _spu_rev_offsetaddr (S_INI .data,
 *              0x80073d18), _spu_memList (0x80073cec, pointer to the block table),
 *              _spu_AllocBlockNum (0x80073ce4, table capacity), _spu_AllocLastNum (0x80073ce8,
 *              index of the last/tail table entry), _spu_gcSPU (0x8005cdc0, merges free blocks).
 * Own data:    none.
 *
 * Nearest archived version: Runtime Library 3.3 - 4.x LIBSPU.LIB S_M_M (712 bytes, the same
 * block-table algorithm and the same imports apart from _spu_mem_mode_unitM). Relocation-
 * masked, 165 of retail's 179 words are identical to the 3.3 member (builds/lane_tools/
 * lcs.py alignment). Retail is four bytes longer because it does not read a precomputed
 * allocation-unit mask: 3.3 loads _spu_mem_mode_unitM, whereas retail builds the unit
 * (1 << _spu_mem_mode_plus) and the mask from _spu_mem_mode_plus in the prologue. So no
 * archive matches: "Runtime 3.3 S_M_M with the unit mask computed from _spu_mem_mode_plus".
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * SpuMalloc - allocate a block of sound buffer memory (SPU RAM)
 *
 * Purpose:     long SpuMalloc(long size). Returns the SPU RAM byte address of a block of
 *              at least `size` bytes, or -1 when there is no room. The blocks are a table
 *              _spu_memList[] of 8-byte records {addr, size}, with flags in the top bits of
 *              addr: 0x80000000 = free hole, 0x40000000 = the untouched tail (everything
 *              from addr to the end of usable RAM), neither = allocated block. The table
 *              has _spu_AllocBlockNum slots and _spu_AllocLastNum is the index of the tail.
 *              Steps (C-like):
 *                unit = 1 << _spu_mem_mode_plus;  size rounded up to a multiple of unit (a size
 *                below one unit becomes 0);
 *                reserve = _spu_rev_reserve_wa ? (0x10000 - _spu_rev_offsetaddr) << plus
 *                                              : 0;   (RAM the reverb needs at the top)
 *                if the first record is the tail: i = 0
 *                else { _spu_gcSPU(); find the first record that is the tail, or a free
 *                       hole with size >= the rounded size; i = its index, else -1 }
 *                if i == -1: return -1
 *                tail record: need i < _spu_AllocBlockNum and (record.size - reserve) >=
 *                       size, else -1; a new tail record i+1 takes the rest;
 *                free hole: if it is larger than size, the remainder becomes a new hole
 *                       record inserted at the tail slot (the tail moves one up);
 *                the record becomes allocated (flags cleared, size = size), _spu_gcSPU()
 *                is called, and the address of the record is returned.
 * Inputs:      a0 = size (kept in s1, then rounded).
 * Outputs:     v0 = SPU RAM address of the block, or -1.
 * Clobbers:    v0, v1, a0..a3, t0 (caller-saved) and, through _spu_gcSPU, whatever it
 *              clobbers. s0..s3 are saved and restored.
 * Stack:       0x28-byte frame: s0 0x10, s1 0x14, s2 0x18, s3 0x1c, ra 0x20.
 * Registers:   s0 = loop index then record offset (i * 8); s1 = rounded size; s2 = found
 *              index; s3 = reverb reserve; a0 = unit - 1; a1 = _spu_mem_mode_plus.
 * Reads/writes: _spu_mem_mode_plus, _spu_rev_reserve_wa, _spu_rev_offsetaddr, _spu_memList,
 *              _spu_AllocBlockNum (read); _spu_AllocLastNum (read, written); the records.
 * Callees:     _spu_gcSPU (three call sites).
 * Callers:     sample_bank_load (config/audio.json, 0x80043ecc), at 0x80043f0c.
 */
	.globl	SpuMalloc
SpuMalloc:
	/* prologue; s0 = i = 0, s2 = found = -1 */
	addiu	sp, sp, -40	/* allocate the 0x28-byte frame */
	sw	s1, 20(sp)	/* save s1 */
	addu	s1, a0, zero	/* s1 = size */
	sw	s0, 16(sp)	/* save s0 */
	addu	s0, zero, zero	/* s0 = 0, the loop index i */
	sw	s2, 24(sp)	/* save s2 */
	addiu	s2, zero, -1	/* s2 = -1, "no record found yet" */
	/* a1 = plus = _spu_mem_mode_plus; a0 = unit = 1 << plus; s3 = reverb reserve in bytes */
	lw	a1, _spu_mem_mode_plus	/* a1 = _spu_mem_mode_plus (lui+lw macro) */
	lw	v0, _spu_rev_reserve_wa	/* v0 = _spu_rev_reserve_wa (lui+lw macro) */
	ori	v1, zero, 0x1	/* v1 = 1 */
	sw	ra, 32(sp)	/* save ra */
	sw	s3, 28(sp)	/* save s3 */
	bne	v0, zero, $reserved	/* reverb work area reserved (non-zero): compute its size */
	sllv	a0, v1, a1	/* delay slot (real work): a0 = 1 << plus, the allocation unit */
	j	$round	/* not reserved: s3 = 0 and continue */
	addu	s3, zero, zero	/* delay slot (real work): s3 = 0, no reverb reserve */
$reserved:
	lw	v1, _spu_rev_offsetaddr	/* v1 = _spu_rev_offsetaddr (lui+lw macro), in 8-byte units */
	lui	v0, 0x1	/* v0 = 0x10000, the top of RAM in 8-byte units */
	subu	v0, v0, v1	/* v0 = 0x10000 - _spu_rev_offsetaddr */
	sllv	s3, v0, a1	/* s3 = that << plus: bytes the reverb needs */
$round:
	/* round the size up to the allocation unit: s1 = ((size + unit - 1) >> plus) << plus. The
	   unit - 1 is added only when size >= unit; a size below one unit stays unrounded and
	   the shifts reduce it to 0 (a quirk of the library, kept as is) */
	addiu	a0, a0, -1	/* a0 = unit - 1, the rounding mask */
	nor	v0, zero, a0	/* v0 = ~(unit - 1) */
	and	v0, s1, v0	/* v0 = size & ~(unit - 1): zero only when size < unit */
	beq	v0, zero, $rounded	/* size < unit: keep v1 = size (it then rounds down to 0 below) */
	addu	v1, s1, zero	/* delay slot (real work): v1 = size */
	addu	v1, s1, a0	/* size >= unit: v1 = size + unit - 1, so the shifts round up */
$rounded:
	lw	v0, _spu_mem_mode_plus	/* v0 = _spu_mem_mode_plus (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	srav	s1, v1, v0	/* s1 = v1 >> plus ... */
	lw	v1, _spu_memList	/* v1 = _spu_memList (lui+lw macro) */
	sllv	s1, s1, v0	/* ... s1 <<= plus: size rounded up to the unit */
	/* locate a record: the first record, if it is the tail, is used directly (found = 0) */
	lw	v0, 0(v1)	/* v0 = memList[0].addr */
	lui	v1, 0x4000	/* v1 = 0x40000000, the tail flag */
	and	v0, v0, v1	/* v0 = memList[0].addr & tail flag */
	beq	v0, zero, $search	/* first record is not the tail: search */
	nop	/* branch delay: nothing to do */
	j	$found	/* first record is the tail: use it */
	addu	s2, zero, zero	/* delay slot (real work): s2 = 0 */
$search:
	/* otherwise merge free blocks, then scan records 0 .. AllocBlockNum - 1 for the first
	   tail record, or free hole (0x80000000) that is large enough */
	jal	_spu_gcSPU	/* _spu_gcSPU(): merge adjacent free blocks first */
	nop	/* call delay: nothing to do */
	lw	v1, _spu_AllocBlockNum	/* v1 = _spu_AllocBlockNum (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	slt	v0, s0, v1	/* v0 = (i < AllocBlockNum) */
	beq	v0, zero, $found	/* no records to scan: not found */
	sll	v0, s0, 3	/* delay slot (real work): v0 = i * 8 */
	lui	a3, 0x4000	/* a3 = 0x40000000, the tail flag */
	lui	a2, 0x8000	/* a2 = 0x80000000, the free-hole flag */
	addu	a1, v1, zero	/* a1 = AllocBlockNum, the loop limit */
	lw	v1, _spu_memList	/* v1 = _spu_memList (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	addu	a0, v0, v1	/* a0 = &memList[i] */
$scan:
	lw	v1, 0(a0)	/* v1 = record.addr (with flags) */
	nop	/* lw load delay slot: nothing to do */
	and	v0, v1, a3	/* v0 = addr & tail flag */
	bne	v0, zero, $take	/* tail record: take it */
	and	v0, v1, a2	/* delay slot (real work): v0 = addr & free-hole flag */
	beq	v0, zero, $next	/* allocated block: skip */
	nop	/* branch delay: nothing to do */
	lw	v0, 4(a0)	/* v0 = record.size */
	nop	/* lw load delay slot: nothing to do */
	sltu	v0, v0, s1	/* v0 = (record.size < size): too small? */
	bne	v0, zero, $next	/* too small: skip */
	nop	/* branch delay: nothing to do */
$take:
	j	$found	/* found: leave the loop */
	addu	s2, s0, zero	/* delay slot (real work): s2 = i */
$next:
	addiu	s0, s0, 1	/* i++ */
	slt	v0, s0, a1	/* v0 = (i < AllocBlockNum) */
	bne	v0, zero, $scan	/* more records: loop */
	addiu	a0, a0, 8	/* delay slot (real work): next record, a0 += 8 */
$found:
	/* found == -1: out of memory */
	addiu	v0, zero, -1	/* v0 = -1 */
	beq	s2, v0, $return	/* nothing found: return -1 (v0) */
	sll	s0, s2, 3	/* delay slot (real work): s0 = found * 8, the record offset */
	/* record = &memList[found]; a tail record is split, a free hole is carved */
	lw	a3, _spu_memList	/* a3 = _spu_memList (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	addu	a2, s0, a3	/* a2 = &memList[found] */
	lw	v1, 0(a2)	/* v1 = record.addr */
	lui	t0, 0x4000	/* t0 = 0x40000000, the tail flag */
	and	v0, v1, t0	/* v0 = addr & tail flag */
	beq	v0, zero, $hole	/* not the tail (a free hole): split it */
	nop	/* branch delay: nothing to do */
	/* tail record: fails if the table has no slot left or the space below the reverb
	   reserve is too small; else a new tail record starts after the block */
	lw	v0, _spu_AllocBlockNum	/* v0 = _spu_AllocBlockNum (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	slt	v0, s2, v0	/* v0 = (found < AllocBlockNum) */
	beq	v0, zero, $return	/* table full: return -1 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
	lw	v0, 4(a2)	/* v0 = record.size (the whole tail) */
	nop	/* lw load delay slot: nothing to do */
	subu	v0, v0, s3	/* v0 = size - reverb reserve */
	sltu	v0, v0, s1	/* v0 = (usable < requested) */
	bne	v0, zero, $return	/* not enough room: return -1 */
	addiu	v0, zero, -1	/* delay slot (real work): v0 = -1 */
	lui	a1, 0xfff	/* a1 = 0x0fffffff ... */
	ori	a1, a1, 0xffff	/* ... the address mask (flags cleared) */
	addiu	a0, s2, 1	/* a0 = found + 1, the new tail index */
	sll	v1, a0, 3	/* v1 = (found + 1) * 8 */
	lw	v0, 0(a2)	/* v0 = record.addr */
	addu	v1, v1, a3	/* v1 = &memList[found + 1] */
	and	v0, v0, a1	/* v0 = addr without flags */
	addu	v0, v0, s1	/* v0 = addr + size: where the tail now starts */
	or	v0, v0, t0	/* v0 |= tail flag */
	sw	v0, 0(v1)	/* new tail record .addr */
	lw	v0, 4(a2)	/* v0 = record.size */
	nop	/* lw load delay slot: nothing to do */
	subu	v0, v0, s1	/* v0 = old size - requested size */
	sw	v0, 4(v1)	/* new tail record .size = what is left */
	lw	v0, 0(a2)	/* v0 = record.addr */
	sw	a0, _spu_AllocLastNum	/* macro "sw a0, _spu_AllocLastNum": lui AT then the store through AT */
	sw	s1, 4(a2)	/* record.size = requested size */
	and	v0, v0, a1	/* v0 = addr without flags: allocated */
	jal	_spu_gcSPU	/* _spu_gcSPU() */
	sw	v0, 0(a2)	/* delay slot (real work): record.addr = v0 */
	lw	v0, _spu_memList	/* v0 = _spu_memList (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	addu	v0, s0, v0	/* v0 = &memList[found] (s0 is the byte offset) */
	lw	v0, 0(v0)	/* v0 = memList[found].addr, the result */
	j	$return	/* return */
	nop	/* jump delay: nothing to do */
$hole:
	/* free hole: carve the block off its front; the remainder becomes a new hole record
	   inserted at the tail slot (tail moves up) if the table has room */
	lw	t0, 4(a2)	/* t0 = hole size (record.size) */
	nop
	sltu	v0, s1, t0	/* v0 = (requested < hole size): room left over? */
	beq	v0, zero, $mark	/* exact fit: nothing to split */
	sll	s0, s2, 3	/* delay slot (real work): s0 = found * 8 */
	lw	a2, _spu_AllocLastNum	/* a2 = _spu_AllocLastNum (lui+lw macro) */
	lw	v0, _spu_AllocBlockNum	/* v0 = _spu_AllocBlockNum (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	slt	v0, a2, v0	/* v0 = (AllocLastNum < AllocBlockNum): room for one more record? */
	beq	v0, zero, $mark	/* table full: no split, hand out the whole hole */
	addu	v1, s1, v1	/* delay slot (real work): v1 = size + hole address (with flags) */
	sll	v0, a2, 3	/* v0 = Last * 8 */
	addu	v0, v0, a3	/* v0 = &memList[Last], the current tail record */
	lui	a0, 0x8000	/* a0 = 0x80000000, the free-hole flag */
	or	v1, v1, a0	/* v1 = address of the remainder | free-hole flag */
	lw	a1, 0(v0)	/* a1 = tail.addr (saved to move it up) */
	lw	a0, 4(v0)	/* a0 = tail.size */
	sw	v1, 0(v0)	/* memList[Last].addr = remainder address */
	subu	v1, t0, s1	/* v1 = hole size - requested size */
	sw	v1, 4(v0)	/* memList[Last].size = remainder size */
	addiu	v1, a2, 1	/* v1 = Last + 1 */
	sw	v1, _spu_AllocLastNum	/* macro "sw v1, _spu_AllocLastNum": the tail moves up one slot */
	sw	a1, 8(v0)	/* memList[Last + 1].addr = old tail addr */
	sw	a0, 12(v0)	/* memList[Last + 1].size = old tail size */
$mark:
	/* mark the record allocated (flags cleared, size = requested size), merge, return addr */
	lui	a0, 0xfff	/* a0 = 0x0fff0000 ... */
	lw	v1, _spu_memList	/* v1 = _spu_memList (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	addu	v1, s0, v1	/* v1 = &memList[found] */
	lw	v0, 0(v1)	/* v0 = record.addr */
	ori	a0, a0, 0xffff	/* ... a0 = 0x0fffffff, the address mask */
	sw	s1, 4(v1)	/* record.size = requested size */
	and	v0, v0, a0	/* v0 = addr without flags: allocated */
	jal	_spu_gcSPU	/* _spu_gcSPU() */
	sw	v0, 0(v1)	/* delay slot (real work): record.addr = v0 */
	lw	v0, _spu_memList	/* v0 = _spu_memList (lui+lw macro) */
	nop	/* lw load delay slot: nothing to do */
	addu	s0, s0, v0	/* s0 = &memList[found] */
	lw	v0, 0(s0)	/* v0 = memList[found].addr, the result */
$return:
	/* epilogue; v0 = result */
	lw	ra, 32(sp)	/* restore ra */
	lw	s3, 28(sp)	/* restore s3 */
	lw	s2, 24(sp)	/* restore s2 */
	lw	s1, 20(sp)	/* restore s1 */
	lw	s0, 16(sp)	/* restore s0 */
	addiu	sp, sp, 40	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
