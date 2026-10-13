/*
 * gte.h - shared definitions for the hand-written GTE/renderer units in asm/.
 *
 * The units are run through the C preprocessor (CPPPSX -undef) before ASPSX, as the PsyQ
 * driver does for a .s file, so this header and <asm.h> may use #define. Comments are
 * C comments; the preprocessor removes them before ASPSX sees the text.
 *
 * Register names come from Runtime Library 3.3 <asm.h> (zero, v0, a0..a3, t0..t9, s0..s7,
 * sp, ra ...). ASPSX 2.34 only accepts numbered registers, which is what <asm.h> expands to.
 */
#include <asm.h>

/*
 * The assembler temporary. <asm.h> spells it "$at", which ASPSX 2.34 rejects ("Destination
 * must be a register"); ASPSX knows it only as $1. Every explicit use is bracketed by
 * ".set noat" / ".set at" so that ASPSX does not warn (a warning fails the build), and so
 * that the reader sees exactly where the code borrows the register that ASPSX's own
 * load/store macros ("lw reg, symbol", "sw reg, symbol") use.
 */
#undef AT
#define AT	$1

/*
 * GTE (cop2) data registers, for mfc2/mtc2/lwc2/swc2. The names follow Sony's later
 * gtereg_s.h (Runtime Library 4.x); Runtime Library 3.3 ships no assembler GTE header.
 */
#define C2_VXY0		$0	/* vector 0 x,y (16.16) */
#define C2_VZ0		$1	/* vector 0 z */
#define C2_VXY1		$2
#define C2_VZ1		$3
#define C2_VXY2		$4
#define C2_VZ2		$5
#define C2_RGB		$6	/* colour + GPU code byte */
#define C2_OTZ		$7	/* AVSZ3/AVSZ4 result */
#define C2_IR0		$8	/* interpolation factor (DPCS depth cue) */
#define C2_IR1		$9
#define C2_IR2		$10
#define C2_IR3		$11
#define C2_SXY0		$12	/* screen x,y FIFO: oldest */
#define C2_SXY1		$13
#define C2_SXY2		$14	/* screen x,y FIFO: newest */
#define C2_SXYP		$15
#define C2_SZ0		$16	/* screen z FIFO: oldest (the value pushed out by the last RTPT) */
#define C2_SZ1		$17
#define C2_SZ2		$18
#define C2_SZ3		$19	/* screen z FIFO: newest */
#define C2_RGB0		$20
#define C2_RGB1		$21
#define C2_RGB2		$22
#define C2_MAC0		$24	/* NCLIP result: twice the signed screen area */
#define C2_MAC1		$25
#define C2_MAC2		$26
#define C2_MAC3		$27
#define C2_IRGB		$28
#define C2_ORGB		$29
#define C2_LZCS		$30
#define C2_LZCR		$31

/*
 * GPU primitive layout (Runtime Library 3.3 libgpu.h) used by the polygon emitters.
 * Every primitive starts with a tag word (next-pointer in bits 0..23, word length in
 * bits 24..31) followed by r,g,b and the GPU command code at offset 7.
 */
#define P_TAG		0	/* tag word: link to the next primitive */
#define P_LEN		3	/* tag high byte: primitive length in words, excluding the tag */
#define P_RGB0		4	/* r0,g0,b0 */
#define P_CODE		7	/* GPU command code */
#define P_XY0		8	/* x0,y0 */
#define P_UV0		12	/* u0,v0,clut */
#define P_XY1		16	/* x1,y1        (POLY_F4: x1,y1 at 12) */
#define P_UV1		20	/* u1,v1,tpage */
#define P_XY2		24	/* x2,y2 */
#define P_UV2		28	/* u2,v2 */
#define P_XY3		32	/* x3,y3 (quads) */
#define P_UV3		36	/* u3,v3 (quads) */
#define POLY_FT3_SIZE	32
#define POLY_FT4_SIZE	40
#define POLY_F4_SIZE	24
#define POLY_FT3_LEN	7
#define POLY_FT4_LEN	9
#define POLY_F4_LEN	5
#define CODE_POLY_FT3	0x24	/* textured triangle */
#define CODE_POLY_FT4	0x2c	/* textured quad */
#define CODE_POLY_FT4_ABE	0x2e	/* textured quad, semi-transparent (ABE bit 1) */
#define CODE_POLY_F4_ABE	0x2a	/* flat quad, semi-transparent */

/*
 * Ordering-table depth: the emitters and the depth helpers index the ordering table with
 * (largest screen z) >> OT_SHIFT, i.e. one OT entry per 32 z units.
 */
#define OT_SHIFT	5

/*
 * Depth cueing starts at this screen z: nearer polygons take their colour unchanged, farther
 * ones get DpqColor(colour, z - DEPTH_CUE_START), which blends towards the far colour.
 */
#define DEPTH_CUE_START	0x1200
