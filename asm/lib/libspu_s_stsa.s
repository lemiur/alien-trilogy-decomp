/*
 * libspu_s_stsa.s - LIBSPU module S_STSA: SpuSetTransferStartAddr.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libspu_s_stsa.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBSPU.LIB (Sony Runtime Library, PlayStation sound processing unit).
 * Module:      S_STSA.
 * Exports:     SpuSetTransferStartAddr (.text+0x0, 100 bytes).
 * Imports:     _spu_fd (S_INI .data, 0x80073cf4), _spu_ioctl (0x8005ab0c).
 * Own data:    none.
 *
 * Nearest archived version: Runtime Library 2.6 / ISA-board LIBSPU.LIB S_STSA (108 bytes; the
 * 3.0 member is a shorter 80-byte rewrite and 3.3 calls _spu_FsetRXXa/_spu_tsa instead).
 * Relocation-masked, 18 of the archive's 27 words are identical to retail's 25
 * (builds/lane_tools/lcs.py alignment). The differences are real, not scheduling: the range
 * check is an upper bound "addr > 0x7fff8" (the end of SPU RAM, 512 KB minus one 8-byte
 * unit) where the archive tests "addr < 0x1000" against a lower bound (ori 0xf000 / sltiu
 * 0x1000), and the round-up mask is -8 (8-byte units) where the archive used -5. So retail
 * is a later pre-3.3 rebuild: "2.6/ISA S_STSA with an upper-bound check and 8-byte rounding".
 *
 * The routine is compiled C (GCC register use, delay slots filled by the compiler); it is
 * transcribed here instruction for instruction.
 */
#include <asm.h>

	.set	noreorder	/* every delay slot is written out by hand */

	.text

/*
 * SpuSetTransferStartAddr - set the SPU RAM address the next SpuWrite/SpuRead starts at
 *
 * Purpose:     unsigned long SpuSetTransferStartAddr(unsigned long addr). addr is a byte
 *              address in sound buffer memory. Addresses above 0x7fff8 are refused (return
 *              0). Otherwise the address is rounded up to the next multiple of 8 when it is
 *              not already one (addr = (addr + 8) & -8, i.e. a non-multiple gains up to 8),
 *              and passed to the driver with ioctl command 0x2b, which may adjust it; the
 *              value left in the argument word is returned.
 * Inputs:      a0 = addr.
 * Outputs:     v0 = the accepted start address, or 0 when addr > 0x7fff8.
 * Clobbers:    v0, v1, a0..a2 (caller-saved) and, through _spu_ioctl, whatever it clobbers.
 * Stack:       0x18-byte frame: ra at 0x10; the (rounded) address word at 0x18, i.e. the
 *              caller-allocated argument slot of a0, whose address is passed to the driver.
 * Reads:       _spu_fd.
 * Callees:     _spu_ioctl(_spu_fd, 0x2b, &addr).
 * Callers:     sample_bank_load (config/audio.json, 0x80043ecc), at 0x80043f24.
 */
	.globl	SpuSetTransferStartAddr
SpuSetTransferStartAddr:
	addiu	sp, sp, -24	/* allocate the 0x18-byte frame */
	/* range check: addr > 0x7fff8 -> return 0 */
	lui	v0, 0x7	/* v0 = 0x00070000 ... */
	ori	v0, v0, 0xfff8	/* ... | 0xfff8 = 0x0007fff8, the highest accepted start address */
	sltu	v0, v0, a0	/* v0 = (0x7fff8 < addr): out of range? */
	sw	ra, 16(sp)	/* save ra */
	beq	v0, zero, $round	/* in range: continue */
	sw	a0, 24(sp)	/* delay slot (real work): spill addr to the argument slot, the driver argument */
	j	$return	/* out of range: return 0 */
	addu	v0, zero, zero	/* delay slot (real work): v0 = 0 */
$round:
	/* round a non-multiple of 8 up: (addr + 8) & -8 */
	andi	v0, a0, 0x7	/* v0 = addr & 7: already a multiple of 8? */
	beq	v0, zero, $ioctl	/* yes: no rounding */
	ori	a1, zero, 0x2b	/* delay slot (real work): a1 = ioctl command 0x2b (set transfer start address) */
	addiu	v0, a0, 8	/* v0 = addr + 8 */
	addiu	v1, zero, -8	/* v1 = -8, the 8-byte unit mask */
	and	v0, v0, v1	/* v0 = (addr + 8) & -8 */
	sw	v0, 24(sp)	/* store the rounded address back */
$ioctl:
	/* hand the address to the driver and return what it left there */
	lw	a0, _spu_fd	/* a0 = _spu_fd (lui+lw macro) */
	jal	_spu_ioctl	/* ioctl(_spu_fd, 0x2b, &addr) */
	addiu	a2, sp, 24	/* delay slot (real work): a2 = &addr */
	lw	v0, 24(sp)	/* v0 = the address as the driver left it (return value) */
$return:
	lw	ra, 16(sp)	/* restore ra */
	addiu	sp, sp, 24	/* release the frame */
	jr	ra
	nop	/* return delay: nothing to do */
