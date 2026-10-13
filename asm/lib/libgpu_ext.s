/*
 * libgpu_ext.s - LIBGPU module EXT: LoadTPage, LoadClut, SetDefDrawEnv, SetDefDispEnv.
 *
 * Library-variant assembly (AGENTS.md, "Library-variant assembly"): a Sony Runtime Library
 * module that retail links as a rebuild matching no archive in ../sdk. Built by
 * tools/build_asm.py (CPPPSX, then ASPSX 2.34); placed by config/libgpu_ext.json; counted as
 * "library assembly", never as matched C and never as certified library.
 *
 * Library:     LIBGPU.LIB (Sony Runtime Library, GPU interface), module EXT.
 * Exports:     LoadTPage      (.text+0x000, 236 bytes, 0x80066250)
 *              LoadClut       (.text+0x08c, 104 bytes, 0x8006633c)
 *              SetDefDrawEnv  (.text+0x0f4, 144 bytes, 0x800663a4)
 *              SetDefDispEnv  (.text+0x184,  60 bytes, 0x80066434)
 * Imports:     LoadImage (0x80062a80, LIBGPU SYS), GetTPage (0x80064ff8) and GetClut
 *              (0x800650c0), both LIBGPU PRIM. The module has no data of its own.
 *
 * Nearest archived version: Runtime Library 3.3 LIBGPU.LIB EXT (592 bytes; the same four exports
 * in the same order). LoadTPage, LoadClut and SetDefDispEnv are identical to it with relocations
 * masked (analysis/data_survey/moddiff.py: 0 differing words in 0x80066250..0x800663a4 and
 * 0x80066434..0x80066470). SetDefDrawEnv is not: 3.3 builds the environment in callee-saved
 * registers (192 bytes) and picks the dither/display-area-draw flags and the default texture
 * page by calling GetVideoMode and GetGraphType; the retail routine (144 bytes, no callee-saved
 * registers) computes the display-area-draw flag from the height alone and always asks
 * GetTPage(0, 0, 640, 0). No archive holds this body (Runtime 3.4/3.5/4.x EXT, and the split
 * E00..E04 modules of 3.5+, were compared; E03 SetDefDrawEnv of 4.4 is also 192 bytes), so the
 * module is "Runtime 3.3 EXT with a shorter SetDefDrawEnv", transcribed instruction for
 * instruction. All four routines are compiled C (GCC register use and scheduling).
 */
#include <asm.h>

	.set	noreorder	/* every delay slot below is written out by hand */

/* GPU texture-page colour depth argument of LoadTPage (libgpu.h GetTPage "tp") */
#define TP_4BIT		0	/* 4 bit CLUT: one 16-bit VRAM pixel holds four texels */
#define TP_8BIT		1	/* 8 bit CLUT: two texels per VRAM pixel */
#define TP_16BIT	2	/* 16 bit direct colour: one texel per VRAM pixel */

/* RECT (libgpu.h): four shorts, built on the stack by LoadTPage and LoadClut */
#define RECT_X		0x00	/* short x */
#define RECT_Y		0x02	/* short y */
#define RECT_W		0x04	/* short w, in 16-bit VRAM pixels */
#define RECT_H		0x06	/* short h */

/* DRAWENV (libgpu.h) members written by SetDefDrawEnv */
#define DRAWENV_CLIP	0x00	/* RECT clip: drawing area */
#define DRAWENV_OFS	0x08	/* short ofs[2]: drawing offset */
#define DRAWENV_TW	0x0c	/* RECT tw: texture window, 0 = off */
#define DRAWENV_TPAGE	0x14	/* u_short tpage: initial texture page */
#define DRAWENV_DTD	0x16	/* u_char dtd: dither processing flag */
#define DRAWENV_DFE	0x17	/* u_char dfe: draw-in-display-area permission */
#define DRAWENV_ISBG	0x18	/* u_char isbg: clear the drawing area at draw time */
#define DRAWENV_R0	0x19	/* u_char r0, g0, b0: background colour */

/* DISPENV (libgpu.h) members written by SetDefDispEnv */
#define DISPENV_DISP	0x00	/* RECT disp: display area in VRAM */
#define DISPENV_SCREEN	0x08	/* RECT screen: output position on the TV screen */
#define DISPENV_ISINTER	0x10	/* u_char isinter */
#define DISPENV_ISRGB24	0x11	/* u_char isrgb24 */
#define DISPENV_PAD0	0x12	/* u_char pad0, pad1 */

	.text

/*
 * LoadTPage - load a texture pattern into VRAM and return its texture page word
 *
 * Purpose:     u_short LoadTPage(u_long *pix, int tp, int abr, int x, int y, int w, int h).
 *              Builds RECT{x, y, w', h} on the stack, where w' converts the width w from texels
 *              to 16-bit VRAM pixels for the colour depth tp (w/4 for 4 bit, w/2 for 8 bit,
 *              w for 16 bit), calls LoadImage(&rect, pix), and returns
 *              GetTPage(tp, abr, x, y).
 * Inputs:      a0 = pix (pattern data), a1 = tp (TP_4BIT/8BIT/16BIT), a2 = abr (semi-transparency
 *              rate), a3 = x; stack (caller frame) y at +0x10, w at +0x14, h at +0x18, which are
 *              0x40/0x44/0x48(sp) after this routine's 0x30-byte frame.
 * Outputs:     v0 = texture page word (GetTPage, masked to 16 bits).
 * Clobbers:    v0, v1, a0-a3, t0 and the caller-saved registers LoadImage/GetTPage clobber.
 *              s0-s3 are saved and restored.
 * Memory:      stack frame 0x30: RECT at 0x10, s0..s3 at 0x18..0x24, ra at 0x28.
 * Callers:     game code (texture upload).
 * Callees:     LoadImage, GetTPage.
 */
	.globl	LoadTPage
LoadTPage:
	addiu	sp, sp, -0x30		/* frame: RECT + s0..s3 + ra */
	sw	s3, 0x24(sp)		/* save s3 (holds y) */
	lw	s3, 0x40(sp)		/* s3 = y, the fifth argument */
	lw	v1, 0x44(sp)		/* v1 = w, the sixth argument */
	lw	v0, 0x48(sp)		/* v0 = h, the seventh argument */
	move	t0, a0			/* t0 = pix, kept across the RECT setup */
	sw	s0, 0x18(sp)		/* save s0 */
	move	s0, a1			/* s0 = tp */
	sw	s2, 0x20(sp)		/* save s2 */
	move	s2, a2			/* s2 = abr */
	sw	s1, 0x1c(sp)		/* save s1 */
	move	s1, a3			/* s1 = x */
	sw	ra, 0x28(sp)		/* save the return address */

	/* Fill the stack RECT: x and h now, y in the branch delay, w' below by depth */
	sh	s1, 0x10+RECT_X(sp)	/* rect.x = x */
	sh	v0, 0x10+RECT_H(sp)	/* rect.h = h */
	ori	v0, zero, TP_8BIT
	beq	s0, v0, $tp_8bit	/* tp == 1: 8 bit pattern */
	sh	s3, 0x10+RECT_Y(sp)	/* delay slot (real work): rect.y = y */
	slti	v0, s0, TP_16BIT	/* tp < 2 ? (here tp != 1, so tp == 0 or tp < 0) */
	beqz	v0, $tp_ge16		/* tp >= 2: 16 bit (or invalid) */
	nop				/* branch delay: nothing to do */
	beqz	s0, $tp_4bit		/* tp == 0: 4 bit pattern */
	addiu	a0, sp, 0x10		/* delay slot (real work): a0 = &rect for LoadImage */
	j	$load_image		/* tp < 0: invalid, leave rect.w unset */
	nop				/* jump delay: nothing to do */

$tp_ge16:
	ori	v0, zero, TP_16BIT
	beq	s0, v0, $tp_16bit	/* tp == 2: 16 bit pattern */
	addiu	a0, sp, 0x10		/* delay slot (real work): a0 = &rect */
	j	$load_image		/* tp > 2: invalid, leave rect.w unset */
	nop				/* jump delay: nothing to do */

$tp_4bit:
	/* w' = w / 4 with the round-toward-zero bias for negative w */
	bgez	v1, $tp_4bit_nonneg	/* w >= 0: no bias needed */
	move	v0, v1			/* delay slot (real work): v0 = w */
	addiu	v0, v1, 3		/* w < 0: add 3 before the arithmetic shift */
$tp_4bit_nonneg:
	sra	v0, v0, 2		/* v0 = w / 4 */
	j	$store_w
	sh	v0, 0x10+RECT_W(sp)	/* delay slot (real work): rect.w = w / 4 */

$tp_8bit:
	/* w' = w / 2 as C does it: add the sign bit (w >> 31, logical) before the shift */
	srl	v0, v1, 31		/* v0 = 1 if w < 0 else 0 */
	addu	v0, v1, v0		/* round toward zero */
	sra	v0, v0, 1		/* v0 = w / 2 */
	j	$store_w
	sh	v0, 0x10+RECT_W(sp)	/* delay slot (real work): rect.w = w / 2 */

$tp_16bit:
	sh	v1, 0x10+RECT_W(sp)	/* rect.w = w, texels are VRAM pixels */

$store_w:
	addiu	a0, sp, 0x10		/* a0 = &rect */
$load_image:
	jal	LoadImage		/* LoadImage(&rect, pix) */
	move	a1, t0			/* delay slot (real work): a1 = pix */

	/* Return the texture page word: GetTPage(tp, abr, x, y) */
	move	a0, s0			/* a0 = tp */
	move	a1, s2			/* a1 = abr */
	move	a2, s1			/* a2 = x */
	jal	GetTPage
	move	a3, s3			/* delay slot (real work): a3 = y */
	andi	v0, v0, 0xffff		/* return value is a u_short */
	lw	ra, 0x28(sp)		/* restore */
	lw	s3, 0x24(sp)
	lw	s2, 0x20(sp)
	lw	s1, 0x1c(sp)
	lw	s0, 0x18(sp)
	addiu	sp, sp, 0x30		/* release the frame */
	jr	ra
	nop				/* return delay: nothing to do */

/*
 * LoadClut - load a colour look-up table (one 256-entry row) into VRAM
 *
 * Purpose:     u_short LoadClut(u_long *clut, int x, int y). Loads RECT{x, y, 256, 1} with
 *              LoadImage(&rect, clut) and returns the CLUT word GetClut(x, y).
 * Inputs:      a0 = clut (256 16-bit colours), a1 = x, a2 = y.
 * Outputs:     v0 = CLUT word (GetClut, masked to 16 bits).
 * Clobbers:    v0, a0-a2 and what LoadImage/GetClut clobber; s0, s1 saved and restored.
 * Memory:      stack frame 0x28: RECT at 0x10, s0/s1 at 0x18/0x1c, ra at 0x20.
 * Callers:     game code (CLUT upload).
 * Callees:     LoadImage, GetClut.
 */
	.globl	LoadClut
LoadClut:
	addiu	sp, sp, -0x28		/* frame: RECT + s0/s1 + ra */
	move	v0, a0			/* v0 = clut, kept for a1 below */
	sw	s0, 0x18(sp)		/* save s0 */
	move	s0, a1			/* s0 = x */
	sw	s1, 0x1c(sp)		/* save s1 */
	move	s1, a2			/* s1 = y */
	addiu	a0, sp, 0x10		/* a0 = &rect */
	move	a1, v0			/* a1 = clut */
	ori	v0, zero, 0x100
	sh	v0, 0x10+RECT_W(sp)	/* rect.w = 256: one CLUT row */
	ori	v0, zero, 1
	sw	ra, 0x20(sp)		/* save the return address */
	sh	s0, 0x10+RECT_X(sp)	/* rect.x = x */
	sh	s1, 0x10+RECT_Y(sp)	/* rect.y = y */
	jal	LoadImage		/* LoadImage(&rect, clut) */
	sh	v0, 0x10+RECT_H(sp)	/* delay slot (real work): rect.h = 1 */
	move	a0, s0			/* a0 = x */
	jal	GetClut			/* GetClut(x, y) */
	move	a1, s1			/* delay slot (real work): a1 = y */
	andi	v0, v0, 0xffff		/* return value is a u_short */
	lw	ra, 0x20(sp)		/* restore */
	lw	s1, 0x1c(sp)
	lw	s0, 0x18(sp)
	addiu	sp, sp, 0x28		/* release the frame */
	jr	ra
	nop				/* return delay: nothing to do */

/*
 * SetDefDrawEnv - fill a DRAWENV with the default drawing environment
 *
 * Purpose:     DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h). Drawing area
 *              and offset are (x, y, w, h)/(x, y); the texture window is off; dither on
 *              (dtd = 1); drawing into the display area is allowed unless h is 480
 *              (dfe = (h != 480), i.e. interlaced high-resolution frames may not); the
 *              background is not cleared (isbg = 0, r0/g0/b0 = 0); the texture page is
 *              GetTPage(0, 0, 640, 0).
 * Inputs:      a0 = env, a1 = x, a2 = y, a3 = w; stack h at +0x10 (0x28(sp) after the frame).
 * Outputs:     v0 = env.
 * Clobbers:    v0, v1, a0-a3 and what GetTPage clobbers; s0 saved and restored.
 * Memory:      writes the DRAWENV through env; stack frame 0x18: s0 at 0x10, ra at 0x14.
 * Callers:     game code (video setup).
 * Callees:     GetTPage.
 */
	.globl	SetDefDrawEnv
SetDefDrawEnv:
	addiu	sp, sp, -0x18		/* frame: s0 + ra */
	sw	s0, 0x10(sp)		/* save s0 */
	move	s0, a0			/* s0 = env */
	ori	v0, zero, 1		/* v0 = 1, the dither flag value */
	lw	v1, 0x28(sp)		/* v1 = h, the fifth argument */
	move	a0, zero		/* GetTPage argument tp = 0 (set early, used at the call) */
	sw	ra, 0x14(sp)		/* save the return address */
	sh	a1, DRAWENV_CLIP+RECT_X(s0)	/* clip.x = x */
	sh	a1, DRAWENV_OFS+0(s0)		/* ofs[0] = x */
	move	a1, zero		/* GetTPage argument abr = 0 */
	sh	a2, DRAWENV_CLIP+RECT_Y(s0)	/* clip.y = y */
	sh	a2, DRAWENV_OFS+2(s0)		/* ofs[1] = y */
	ori	a2, zero, 640		/* GetTPage argument x = 640 */
	sh	a3, DRAWENV_CLIP+RECT_W(s0)	/* clip.w = w */
	move	a3, zero		/* GetTPage argument y = 0 */
	sh	zero, DRAWENV_TW+RECT_X(s0)	/* tw.x = 0 (texture window off) */
	sh	zero, DRAWENV_TW+RECT_Y(s0)	/* tw.y = 0 */
	sh	zero, DRAWENV_TW+RECT_W(s0)	/* tw.w = 0 */
	sh	zero, DRAWENV_TW+RECT_H(s0)	/* tw.h = 0 */
	sb	zero, DRAWENV_R0+0(s0)		/* r0 = 0 */
	sb	zero, DRAWENV_R0+1(s0)		/* g0 = 0 */
	sb	zero, DRAWENV_R0+2(s0)		/* b0 = 0 */
	sb	v0, DRAWENV_DTD(s0)		/* dtd = 1 */
	sh	v1, DRAWENV_CLIP+RECT_H(s0)	/* clip.h = h */
	xori	v1, v1, 480		/* v1 = h ^ 480 */
	sltu	v1, zero, v1		/* v1 = (h != 480) */
	jal	GetTPage		/* GetTPage(0, 0, 640, 0) */
	sb	v1, DRAWENV_DFE(s0)	/* delay slot (real work): dfe = (h != 480) */
	sh	v0, DRAWENV_TPAGE(s0)	/* tpage = the returned texture page */
	move	v0, s0			/* return env ... */
	sb	zero, DRAWENV_ISBG(v0)	/* ... after clearing isbg (done through v0 as compiled) */
	lw	ra, 0x14(sp)		/* restore */
	lw	s0, 0x10(sp)
	addiu	sp, sp, 0x18		/* release the frame */
	jr	ra
	nop				/* return delay: nothing to do */

/*
 * SetDefDispEnv - fill a DISPENV with the default display environment
 *
 * Purpose:     DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h). The display
 *              area is (x, y, w, h); the screen rectangle, interlace, 24-bit and pad bytes are
 *              all zero.
 * Inputs:      a0 = env, a1 = x, a2 = y, a3 = w; stack h at +0x10 (leaf routine: no frame,
 *              so it is read at 0x10(sp)).
 * Outputs:     v0 = env.
 * Clobbers:    v0, v1. Leaf routine: no stack frame, no calls.
 * Memory:      writes the DISPENV through env.
 * Callers:     game code (video setup).
 * Callees:     none.
 */
	.globl	SetDefDispEnv
SetDefDispEnv:
	lw	v1, 0x10(sp)		/* v1 = h, the fifth argument */
	move	v0, a0			/* v0 = env (the return value) */
	sh	a1, DISPENV_DISP+RECT_X(v0)	/* disp.x = x */
	sh	a2, DISPENV_DISP+RECT_Y(v0)	/* disp.y = y */
	sh	a3, DISPENV_DISP+RECT_W(v0)	/* disp.w = w */
	sh	zero, DISPENV_SCREEN+RECT_X(v0)	/* screen.x = 0 */
	sh	zero, DISPENV_SCREEN+RECT_Y(v0)	/* screen.y = 0 */
	sh	zero, DISPENV_SCREEN+RECT_W(v0)	/* screen.w = 0 */
	sh	zero, DISPENV_SCREEN+RECT_H(v0)	/* screen.h = 0 */
	sb	zero, DISPENV_ISRGB24(v0)	/* isrgb24 = 0 */
	sb	zero, DISPENV_ISINTER(v0)	/* isinter = 0 */
	sb	zero, DISPENV_PAD0+1(v0)	/* pad1 = 0 */
	sb	zero, DISPENV_PAD0(v0)		/* pad0 = 0 */
	jr	ra
	sh	v1, DISPENV_DISP+RECT_H(v0)	/* delay slot (real work): disp.h = h */
