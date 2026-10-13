/*
 * libetc_intr_dma.s - LIBETC module INTR_DMA: the DMA interrupt (IRQ3) service.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libetc_intr_dma.json and the
 * "libetc_intr_dma" entry of config/data.json; counted as "library assembly", never as matched
 * C and never as certified library.
 *
 * Library:     LIBETC.LIB (Sony Runtime Library, system support), module INTR_DMA.
 * Exports:     startIntrDMA (.text+0x0, 0x80061e34). Like the archived module the object has a
 *              single exported symbol and the whole module (628 bytes) is the extent of that
 *              symbol. It contains four routines, three of them local to the module:
 *                startIntrDMA   0x80061e34   80 bytes  install the IRQ3 handler (exported)
 *                f_61e84        0x80061e84  336 bytes  DMA interrupt handler (local)
 *                f_61fd4        0x80061fd4  168 bytes  set/clear one channel's callback (local;
 *                                                      its address is what startIntrDMA returns)
 *                f_6207c        0x8006207c   44 bytes  zero a word array (local)
 * Imports:     InterruptCallback (0x80061700, LIBETC INTR), printf (0x80069880).
 * Own data:    .data  0x800759a0, 36 bytes: _dma_icr (pointer to the DMA interrupt control
 *                    register) followed by _dma_callbacks (eight callback slots).
 *              .rdata 0x80013188, 28 bytes: the bus-error message.
 *
 * Nearest archived version: Runtime Library 3.3 LIBETC.LIB INTR_DMA (680 text bytes, 40 .data
 * bytes, 43 .rdata bytes; relocation-masked word coverage 0.81, analysis/data_survey/gaps.json).
 * Same structure and same four routines, but the 3.3 handler also dumps every channel's MADR
 * register ("MADR[%d]=%08x\n", a second message and a trailing 0x1f801080 pointer in .data)
 * after a bus error. The retail module has neither: 628 text bytes, one 26-byte message, and
 * no trailing pointer. Runtime 2.6 (644 text bytes), 3.0 (676), 4.0 (680), 4.1 (672) and the
 * 4.x sample NEW_BS.LIB (836) are different sizes again; none matches. The routines are
 * compiled C (GCC register use and scheduling, a frame-less zero-fill that still allocates an
 * 8-byte frame), transcribed instruction for instruction.
 *
 * Hardware: DMA interrupt control register DICR, 0x1f8010f4 (psx-spx "DMA Interrupt Register"):
 *   bits 0..5   unknown/ignored      bit 15  bus error flag (R/W)
 *   bits 16..22 IRQ enable, channels 0..6    bit 23  IRQ master enable
 *   bits 24..30 IRQ flags, channels 0..6 (write 1 to acknowledge)
 *   bit 31      IRQ master flag (read only)
 * The handler runs from the interrupt dispatcher (INTR InterruptCallback source 3 = DMA).
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */

#define DMA_IRQ_SOURCE		3		/* I_STAT bit 3: DMA (InterruptCallback source number) */
#define DMA_CHANNELS		7		/* DMA channels 0..6 (MDECin .. OTC) */
#define DMA_CB_SLOTS		8		/* callback table size in words (7 channels + 1 spare) */
#define DICR_ENABLE_SHIFT	16		/* channel n's IRQ enable is bit 16 + n */
#define DICR_FLAG_SHIFT		24		/* channel n's IRQ flag is bit 24 + n */
#define DICR_FLAG_MASK		0x7f		/* seven channel flags after the shift */
#define DICR_KEEP_LOW		0x00ffffff	/* bits 0..23: control/enable bits written back as read */
#define DICR_MASTER_ENABLE	0x00800000	/* bit 23: IRQ master enable */
#define DICR_FLAGS_AND_MASTER	0xff000000	/* bits 24..31: channel flags and IRQ master flag */
#define DICR_MASTER_FLAG	0x80000000	/* bit 31: IRQ master flag */
#define DICR_FLAGS_AND_MASK_HI	0xff00		/* bits 24..31 as a lui operand */
#define DICR_MASTER_FLAG_HI	0x8000		/* bit 31 as a lui operand */
#define DICR_MASTER_ENABLE_HI	0x0080		/* bit 23 as a lui operand */
#define DICR_BUS_ERROR		0x8000		/* bit 15: bus error flag */

/*
 * _dma_icr - pointer to the DMA interrupt control register
 *
 * Purpose:     the address of DICR (0x1f8010f4), read through a pointer as all LIBETC code does.
 * Layout:      one 32-bit pointer.
 * Users:       startIntrDMA, the handler f_61e84 and the callback setter f_61fd4.
 *
 * _dma_callbacks - per-channel DMA callback functions
 *
 * Purpose:     slot n holds the function the handler calls when channel n's IRQ flag is set
 *              (zero: no callback). Zero-filled at startIntrDMA, set by f_61fd4.
 * Layout:      eight 32-bit function pointers, indexed by DMA channel; slots 0..6 are used.
 * Users:       startIntrDMA (clears it), f_61e84 (calls through it), f_61fd4 (writes it).
 */
	.data
_dma_icr:
	.word	0x1f8010f4	/* DICR, the DMA interrupt control register */
_dma_callbacks:
	.word	0, 0, 0, 0	/* channels 0..3: no callback yet */
	.word	0, 0, 0, 0	/* channels 4..6 and the spare slot */

/*
 * _dma_bus_error_msg - format of the bus-error report printed by the handler
 *
 * Purpose:     printf format; its one argument is the DICR value.
 * Layout:      NUL-terminated string (26 bytes) padded to a word (2 zero bytes).
 * Users:       f_61e84.
 */
	.rdata
_dma_bus_error_msg:
	.ascii	"DMA bus error: code=%08x\n\000"
	.byte	0, 0		/* pad to a word boundary */

	.text

/*
 * startIntrDMA - install the DMA interrupt handler
 *
 * Purpose:     void *startIntrDMA(void). Clears the callback table, disables every DMA
 *              interrupt (DICR = 0), registers f_61e84 for interrupt source 3 (DMA) with
 *              InterruptCallback, and returns the address of the callback setter f_61fd4
 *              (INTR keeps it in its function table, DMACallback calls through it).
 * Inputs:      none.
 * Outputs:     v0 = f_61fd4, the callback-setter function.
 * Clobbers:    v0, a0, a1 and what f_6207c/InterruptCallback clobber. ra saved.
 * Hardware:    writes DICR (0x1f8010f4) = 0.
 * Memory:      stack frame 0x18 (ra at 0x10); writes _dma_callbacks.
 * Callers:     ResetCallback (INTR).
 * Callees:     f_6207c, InterruptCallback.
 */
	.globl	startIntrDMA
startIntrDMA:
	addiu	sp, sp, -0x18		/* frame: ra */
	la	a0, _dma_callbacks	/* a0 = callback table (lui + addiu) */
	sw	ra, 0x10(sp)		/* save the return address */
	jal	f_6207c			/* zero the table: f_6207c(table, 8 words) */
	ori	a1, zero, DMA_CB_SLOTS	/* delay slot (real work): a1 = 8 words */
	ori	a0, zero, DMA_IRQ_SOURCE	/* a0 = 3: the DMA interrupt */
	lw	v0, _dma_icr		/* v0 = &DICR (lui + lw, no $at) */
	la	a1, f_61e84		/* a1 = the handler */
	sw	zero, 0(v0)		/* DICR = 0: all channel IRQs and flags off */
	jal	InterruptCallback	/* InterruptCallback(3, f_61e84) */
	nop				/* branch delay: nothing to do */
	la	v0, f_61fd4		/* return value: the callback setter */
	lw	ra, 0x10(sp)		/* restore */
	addiu	sp, sp, 0x18		/* release the frame */
	jr	ra
	nop				/* return delay: nothing to do */

/*
 * f_61e84 - DMA interrupt handler (local)
 *
 * Purpose:     Called by the interrupt dispatcher for IRQ3. Reads the channel IRQ flags from
 *              DICR (bits 24..30), and for every flagged channel in ascending order
 *              acknowledges its flag (write 1 back, keeping bits 0..23 as read) and then
 *              calls that channel's callback if one is set. Re-reads DICR and repeats until no
 *              channel flag remains. Finally, if the master flag is set with no channel flag
 *              (spurious), or the bus error flag (bit 15) is set, prints
 *              "DMA bus error: code=%08x" with the DICR value.
 * Inputs:      none (interrupt context; the dispatcher has saved the registers it needs).
 * Outputs:     none.
 * Registers:   s0 = pending channel flags (shifted right as channels are consumed),
 *              s1 = channel index, s2 = &_dma_callbacks[channel], s3 = 0x00ffffff,
 *              s4 = 1, s5 = &_dma_callbacks[0]; all saved on the 0x30-byte frame with ra.
 * Hardware:    reads and writes DICR (0x1f8010f4).
 * Callers:     the INTR dispatcher via InterruptCallback(3, ...).
 * Callees:     channel callbacks (indirect), printf.
 */
f_61e84:
	/* The compiler scheduled the first two instructions of the body before the prologue;
	   the handler's entry address is f_61e84 itself (startIntrDMA passes it). */
	lw	v0, _dma_icr		/* v0 = &DICR (lui + lw) */
	addiu	sp, sp, -0x30		/* frame: s0..s5 + ra */
	sw	ra, 0x28(sp)		/* save ra and the callee-saved registers */
	sw	s5, 0x24(sp)
	sw	s4, 0x20(sp)
	sw	s3, 0x1c(sp)
	sw	s2, 0x18(sp)
	sw	s1, 0x14(sp)
	sw	s0, 0x10(sp)
	lw	v0, 0(v0)		/* v0 = DICR */
	nop				/* load delay */
	srl	v0, v0, DICR_FLAG_SHIFT	/* v0 = master flag and channel flags */
	andi	s0, v0, DICR_FLAG_MASK	/* s0 = pending channel flags (bits 0..6) */
	beqz	s0, $check_error	/* nothing pending: go straight to the error check */
	nop				/* branch delay: nothing to do */
	ori	s4, zero, 1		/* s4 = 1, the bit to shift */
	lui	s3, 0xff		/* s3 = 0x00ffffff: bits 0..23 of DICR are written back unchanged */
	ori	s3, s3, 0xffff
	la	s5, _dma_callbacks	/* s5 = &_dma_callbacks[0] (lui + addiu) */

$round:
	beqz	s0, $reread		/* no flags left in this round: re-read DICR */
	move	s1, zero		/* delay slot (real work): channel = 0 */
	move	s2, s5			/* s2 = &_dma_callbacks[0] */

$scan:
	slti	v0, s1, DMA_CHANNELS	/* channel < 7 ? */
	beqz	v0, $reread		/* all seven channels handled */
	andi	v0, s0, 1		/* delay slot (real work): is this channel flagged? */
	beqz	v0, $advance		/* not flagged: next channel */
	addiu	v0, s1, DICR_FLAG_SHIFT	/* delay slot (real work): v0 = channel + 24 */

	/* Acknowledge: DICR = DICR & (0x00ffffff | (1 << (channel + 24))). Bits 0..23 are
	   written back as read, only this channel's flag is written as 1 (clears it); the
	   other channels' flags are written as 0 (left alone). */
	lw	a0, _dma_icr		/* a0 = &DICR (lui + lw) */
	sllv	v0, s4, v0		/* v0 = 1 << (channel + 24) */
	lw	v1, 0(a0)		/* v1 = DICR */
	or	v0, v0, s3		/* v0 = 0x00ffffff | this channel's flag bit */
	and	v0, v0, v1		/* keep the low bits and only this flag */
	sw	v0, 0(a0)		/* write DICR: acknowledge the channel */

	lw	v0, 0(s2)		/* v0 = callback of this channel */
	nop				/* load delay */
	beqz	v0, $advance		/* no callback registered */
	nop				/* branch delay: nothing to do */
	jalr	v0			/* call the channel callback */
	nop				/* branch delay: nothing to do */

$advance:
	addiu	s2, s2, 4		/* next callback slot */
	srl	s0, s0, 1		/* drop this channel's flag bit */
	bnez	s0, $scan		/* flags left: continue with the next channel */
	addiu	s1, s1, 1		/* delay slot (real work): channel++ */

$reread:
	/* A callback or a new transfer may have raised more flags: read DICR again */
	lw	v0, _dma_icr		/* v0 = &DICR (lui + lw) */
	nop				/* load delay */
	lw	v0, 0(v0)		/* v0 = DICR */
	nop				/* load delay */
	srl	v0, v0, DICR_FLAG_SHIFT
	andi	s0, v0, DICR_FLAG_MASK	/* s0 = flags raised meanwhile */
	bnez	s0, $round		/* some: another round */
	nop				/* branch delay: nothing to do */

$check_error:
	lw	a1, _dma_icr		/* a1 = &DICR (lui + lw) */
	nop				/* load delay */
	lw	v0, 0(a1)		/* v0 = DICR */
	lui	v1, DICR_FLAGS_AND_MASK_HI	/* v1 = 0xff000000 */
	and	v0, v0, v1		/* flags byte: master flag and channel flags */
	lui	v1, DICR_MASTER_FLAG_HI	/* v1 = 0x80000000 */
	beq	v0, v1, $report		/* master flag with no channel flag: report */
	nop				/* branch delay: nothing to do */
	lw	v0, 0(a1)		/* v0 = DICR again */
	nop				/* load delay */
	andi	v0, v0, DICR_BUS_ERROR	/* bit 15: bus error flag */
	beqz	v0, $exit		/* no bus error */
	nop				/* branch delay: nothing to do */

$report:
	lw	a1, 0(a1)		/* a1 = DICR, the "code" printed */
	la	a0, _dma_bus_error_msg	/* a0 = format (lui + addiu) */
	jal	printf			/* printf("DMA bus error: code=%08x\n", DICR) */
	nop				/* branch delay: nothing to do */

$exit:
	lw	ra, 0x28(sp)		/* restore */
	lw	s5, 0x24(sp)
	lw	s4, 0x20(sp)
	lw	s3, 0x1c(sp)
	lw	s2, 0x18(sp)
	lw	s1, 0x14(sp)
	lw	s0, 0x10(sp)
	addiu	sp, sp, 0x30		/* release the frame */
	jr	ra
	nop				/* return delay: nothing to do */

/*
 * f_61fd4 - set or clear one DMA channel's callback (local)
 *
 * Purpose:     void (*f_61fd4(int channel, void (*func)(void)))(void). Installs func as the
 *              callback of DMA channel `channel` and enables that channel's interrupt in DICR
 *              (with the IRQ master enable), or, for func == 0, removes the callback and
 *              disables the channel's interrupt (the master enable stays on). Does nothing
 *              when func equals the current callback. Returns the previous callback.
 *              This is the routine INTR's DMACallback reaches through its function table.
 * Inputs:      a0 = channel (0..6), a1 = func (0 to clear).
 * Outputs:     v0 = the previous callback of that channel.
 * Registers:   a2 = channel, v1 = &_dma_callbacks[channel], a3 = previous callback.
 * Hardware:    read-modify-write of DICR (0x1f8010f4): bit 16 + channel (enable) and bit 23
 *              (master enable) set, or only the channel enable bit cleared. Bits 0..23 are
 *              written back as read, and bits 24..31 (flags) are written 0, so no flag is
 *              acknowledged by accident.
 * Callers:     INTR DMACallback (through the function table), game code via DMACallback.
 * Callees:     none. Leaf routine, no frame.
 */
f_61fd4:
	move	a2, a0			/* a2 = channel */
	la	v1, _dma_callbacks	/* v1 = callback table (lui + addiu) */
	sll	v0, a2, 2		/* v0 = channel * 4 */
	addu	v1, v0, v1		/* v1 = &_dma_callbacks[channel] */
	lw	a3, 0(v1)		/* a3 = previous callback (the return value) */
	move	a0, a1			/* a0 = func */
	beq	a0, a3, $set_done	/* unchanged: nothing to do */
	nop				/* branch delay: nothing to do */
	beqz	a0, $clear		/* func == 0: remove the callback */
	lui	v0, 0xff		/* delay slot (real work): start building 0x00ffffff */

	/* Install: callback = func; DICR = (DICR & 0x00ffffff) | (1 << (channel + 16)) | master */
	lw	a1, _dma_icr		/* a1 = &DICR (lui + lw) */
	ori	v0, v0, 0xffff		/* v0 = 0x00ffffff */
	sw	a0, 0(v1)		/* _dma_callbacks[channel] = func */
	lw	a0, 0(a1)		/* a0 = DICR */
	addiu	v1, a2, DICR_ENABLE_SHIFT	/* v1 = channel + 16: the channel's enable bit */
	and	a0, a0, v0		/* keep bits 0..23, drop the flag bits (written as 0) */
	ori	v0, zero, 1
	sllv	v0, v0, v1		/* v0 = 1 << (channel + 16) */
	lui	v1, DICR_MASTER_ENABLE_HI	/* v1 = 0x00800000 */
	or	v0, v0, v1		/* channel enable | master enable */
	or	a0, a0, v0		/* DICR value to write */
	sw	a0, 0(a1)		/* write DICR: channel interrupt on */
	j	$set_done
	nop				/* jump delay: nothing to do */

$clear:
	/* Remove: callback = 0; DICR = (DICR & 0x00ffffff | master) & ~(1 << (channel + 16)) */
	lw	a1, _dma_icr		/* a1 = &DICR (lui + lw) */
	ori	v0, v0, 0xffff		/* v0 = 0x00ffffff (the lui is in the branch delay above) */
	sw	zero, 0(v1)		/* _dma_callbacks[channel] = 0 */
	lw	v1, 0(a1)		/* v1 = DICR */
	addiu	a0, a2, DICR_ENABLE_SHIFT	/* a0 = channel + 16 */
	and	v1, v1, v0		/* keep bits 0..23, flag bits written as 0 */
	lui	v0, DICR_MASTER_ENABLE_HI	/* v0 = 0x00800000 */
	or	v1, v1, v0		/* keep the IRQ master enable on */
	ori	v0, zero, 1
	sllv	v0, v0, a0		/* v0 = 1 << (channel + 16) */
	nor	v0, zero, v0		/* v0 = ~(1 << (channel + 16)) */
	and	v1, v1, v0		/* clear the channel's enable bit */
	sw	v1, 0(a1)		/* write DICR: channel interrupt off */

$set_done:
	jr	ra
	move	v0, a3			/* delay slot (real work): return the previous callback */

/*
 * f_6207c - zero a word array (local)
 *
 * Purpose:     void f_6207c(long *p, int n): p[0..n-1] = 0. The compiler left an unused 8-byte
 *              frame around the leaf body.
 * Inputs:      a0 = p, a1 = n (0 stores nothing).
 * Outputs:     none.
 * Clobbers:    v0, v1, a0.
 * Callers:     startIntrDMA.
 * Callees:     none.
 */
f_6207c:
	addiu	sp, sp, -8		/* unused frame (compiled that way) */
	beqz	a1, $zero_done		/* n == 0: nothing to clear */
	addiu	v0, a1, -1		/* delay slot (real work): v0 = n - 1, the loop counter */
	addiu	v1, zero, -1		/* v1 = -1, the loop end value */
$zero_loop:
	sw	zero, 0(a0)		/* *p = 0 */
	addiu	v0, v0, -1		/* counter-- */
	bne	v0, v1, $zero_loop	/* until the counter reaches -1 (n stores) */
	addiu	a0, a0, 4		/* delay slot (real work): p++ */
$zero_done:
	addiu	sp, sp, 8		/* release the frame */
	jr	ra
	nop				/* return delay: nothing to do */
