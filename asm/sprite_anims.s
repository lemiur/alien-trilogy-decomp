/*
 * sprite_anims.s - sprite animation tables kept in .text after Model_DrawVisibleCells
 *
 * Hand-written assembly region, table window (AGENTS.md, "Hand-written assembly"): retail keeps
 * these tables in .text at 0x800558e8..0x800588bb, after the last renderer routine. They are
 * written as annotated tables (tools/asm_units.py admits data directives in .text only inside
 * that window) and are counted as assembly. Built by tools/build_asm.py; placed by
 * config/sprite_anims.json, one label per table.
 *
 * Labels: g_<set>_anim<k>_frames and g_<set>_anim<k>_script, after the first user of each
 * animation (animation k of the sprite resource set, else of the actor kind table); names:
 * static evidence, analysis/runtime/globals.json round 2 (the sets are tied to their \GFX and
 * \NME .BND files through g_spriteResources).
 *
 * Contents: 156 animations, each a frame list immediately followed by its script. Nothing in
 * here is a pointer, so the tables carry no relocations; they are reached through trackEntry_t
 * pairs { frame list, script } in .data:
 *   - 0x8006ccf0..0x8006d1cf: 156 entries, grouped into 20 sprite sets by the records
 *     { id, trackEntry_t *animations, u16 *frame_source } at 0x8006d1d0 (read by the C code
 *     around 0x8001d168..0x8001d314);
 *   - 0x800725c8..0x80072bdf: 195 entries, read in groups by the C code at
 *     0x80028d50..0x80029630 (each group named below by its base address);
 *   - 0x80073074..0x800730eb: 15 entries, the weapon-view tables that Player_SelectWeapon selects by
 *     mode (g_pistolSequence mode 0, g_shotgunSequence mode 1, g_pulseRifleSequence mode 2, g_smartGunSequence mode 3, g_flamethrowerSequence mode 4).
 *
 * Frame list (TrackCommand.list_base): 12 bytes per frame; StepSprite selects frame i as
 * list_base + 12 * i (TrackCommand.current_packet). The fields are read elsewhere; their
 * names here are inferred from the values (signed offsets, then positive sizes):
 *     s16 x, y      offset of the frame's top-left corner from the sprite's anchor
 *     u16 w, h      size
 *     u32 offset    byte offset of the frame's image data (increases along a list)
 *
 * Script (trackEntry_t.stream), halfwords, read by Player_PlayWeaponView (start) and StepSprite / SpriteRunScript
 * (one step every "ticks" vertical blanks):
 *     u16 ticks     steps last this many ticks (TrackCommand.duration)
 *     u16 frames    number of frames in the list (not read by the interpreter)
 *     tokens...     a frame index (bit 15 clear), or a command 0x8n00 | arg (SpriteRunScript):
 *       TRK_SIGNAL(n)  0x81nn: flags |= 0x10 and the track's signal field = n; next token
 *       TRK_LOOP(n)    0x82nn: back to the first token; n > 0 repeats n times, then stops
 *                      (flags |= 2); n = 0 loops forever
 *       TRK_FLAG1_ON   0x8300: flags |= 1      TRK_FLAG1_OFF  0x8400: flags &= ~1
 *       TRK_NOP5       0x8500: no effect        TRK_NOP7       0x8700: no effect
 *       TRK_STOP       0x8600: flags |= 2 and stop (the frame no longer changes)
 *       TRK_STOP4      0x8800: flags |= 4 and stop
 *       TRK_FLAG8      0x8900: flags |= 8
 *     Commands run back to back until a frame index (or a stop); a script whose length is
 *     odd in halfwords ends with one pad halfword.
 */

/*
 * Rows are plain .half lists: ASPSX ends an expression at a space, so the preprocessor's
 * padding around macro arguments cannot be used inside data directives. Each script row's
 * comment spells its tokens with the names below; each frame row is x, y, w, h and the data
 * offset as its low and high halves.
 *
 * Script tokens (SpriteRunScript decodes token & 0x7f00 and token & 0xff):
 *   0x81nn TRK_SIGNAL(nn)   0x82nn TRK_LOOP(nn)   0x8300 TRK_FLAG1_ON   0x8400 TRK_FLAG1_OFF
 *   0x8500 TRK_NOP5         0x8600 TRK_STOP       0x8700 TRK_NOP7       0x8800 TRK_STOP4
 *   0x8900 TRK_FLAG8
 */

	.text

/*
 * Animation 0: frame list g_pulseRifleAnim0Frames (1 frame) + script g_pulseRifleAnim0Script
 *   used by sprite set 1 (record 0x8006d1e8) animation 0; weapon-view mode 2 (g_pulseRifleSequence) entry 0
 */
	.globl	g_pulseRifleAnim0Frames
g_pulseRifleAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -18,  -65,   84,   68, 0x0000, 0	/* frame 0 */
	.globl	g_pulseRifleAnim0Script
g_pulseRifleAnim0Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 1: frame list g_pulseRifleAnim2Frames (6 frames) + script g_pulseRifleAnim2Script
 *   used by sprite set 1 (record 0x8006d1e8) animation 2; weapon-view mode 2 (g_pulseRifleSequence) entry 1
 */
	.globl	g_pulseRifleAnim2Frames
g_pulseRifleAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -19,  -85,   84,   88, 0x0000, 0	/* frame 0 */
	.half	 -18,  -65,   84,   68, 0x0c3c, 0	/* frame 1 */
	.half	 -19,  -89,   84,   92, 0x1690, 0	/* frame 2 */
	.half	 -18,  -65,   84,   68, 0x0c3c, 0	/* frame 3 */
	.half	 -22,  -89,   88,   92, 0x2360, 0	/* frame 4 */
	.half	 -18,  -65,   84,   68, 0x0c3c, 0	/* frame 5 */
	.globl	g_pulseRifleAnim2Script
g_pulseRifleAnim2Script:
	.half	2, 6			/* 2 ticks per step, 6 frames */
	.half	0x8106, 0x8300, 0, 1, 2, 3, 4, 5	/* TRK_SIGNAL(6), TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5 */
	.half	0x8600	/* TRK_STOP */
					/* = signal 6, set flag 1, frames 0..5, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 2: frame list g_pulseRifleAnim3Frames (4 frames) + script g_pulseRifleAnim3Script
 *   used by sprite set 1 (record 0x8006d1e8) animation 3; weapon-view mode 2 (g_pulseRifleSequence) entry 2
 */
	.globl	g_pulseRifleAnim3Frames
g_pulseRifleAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -53,  -74,   92,   76, 0x0000, 0	/* frame 0 */
	.half	 -68,  -74,  128,   76, 0x0c50, 0	/* frame 1 */
	.half	 -72,  -74,  124,   76, 0x1e2c, 0	/* frame 2 */
	.half	 -53,  -74,   92,   76, 0x0000, 0	/* frame 3 */
	.globl	g_pulseRifleAnim3Script
g_pulseRifleAnim3Script:
	.half	2, 4			/* 2 ticks per step, 4 frames */
	.half	0x8300, 0, 1, 2, 3, 0x8600	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = set flag 1, frames 0..3, stop */

/*
 * Animation 3: frame list g_pulseRifleAnim1Frames (3 frames) + script g_pulseRifleAnim1Script
 *   used by sprite set 1 (record 0x8006d1e8) animation 1; weapon-view mode 2 (g_pulseRifleSequence) entry 3
 */
	.globl	g_pulseRifleAnim1Frames
g_pulseRifleAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -15,  -63,   80,   64, 0x0000, 0	/* frame 0 */
	.half	 -15,  -63,   80,   64, 0x0000, 0	/* frame 1 */
	.half	  -4,  -53,   72,   56, 0x09dc, 0	/* frame 2 */
	.globl	g_pulseRifleAnim1Script
g_pulseRifleAnim1Script:
	.half	2, 3			/* 2 ticks per step, 3 frames */
	.half	0x8107, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(7), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 7, set flag 1, frames 0..2, stop */

/*
 * Animation 4: frame list g_shotgunAnim0Frames (1 frame) + script g_shotgunAnim0Script
 *   used by sprite set 3 (record 0x8006d200) animation 0; weapon-view mode 1 (g_shotgunSequence) entry 0
 */
	.globl	g_shotgunAnim0Frames
g_shotgunAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -33,  -77,   84,   80, 0x0000, 0	/* frame 0 */
	.globl	g_shotgunAnim0Script
g_shotgunAnim0Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 5: frame list g_shotgunAnim1Frames (8 frames) + script g_shotgunAnim1Script
 *   used by sprite set 3 (record 0x8006d200) animation 1; weapon-view mode 1 (g_shotgunSequence) entry 1
 */
	.globl	g_shotgunAnim1Frames
g_shotgunAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -24, -102,   72,  104, 0x0000, 0	/* frame 0 */
	.half	 -26,  -92,   76,   96, 0x0d18, 0	/* frame 1 */
	.half	 -22,  -82,   72,   84, 0x1934, 0	/* frame 2 */
	.half	 -16,  -83,   64,   84, 0x2358, 0	/* frame 3 */
	.half	 -30,  -88,   76,   92, 0x2d94, 0	/* frame 4 */
	.half	 -30,  -88,   76,   92, 0x2d94, 0	/* frame 5 */
	.half	 -16,  -83,   64,   84, 0x2358, 0	/* frame 6 */
	.half	 -22,  -82,   72,   84, 0x1934, 0	/* frame 7 */
	.globl	g_shotgunAnim1Script
g_shotgunAnim1Script:
	.half	2, 8			/* 2 ticks per step, 8 frames */
	.half	0x8110, 0x8300, 0, 1, 2, 3, 0x810f, 4	/* TRK_SIGNAL(16), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_SIGNAL(15), 4 */
	.half	5, 6, 7, 0x8600	/* 5, 6, 7, TRK_STOP */
					/* = signal 16, set flag 1, frames 0..3, signal 15, frames 4..7, stop */

/*
 * Animation 6: frame list g_pistolAnim0Frames (1 frame) + script g_pistolAnim0Script
 *   used by sprite set 2 (record 0x8006d1d0) animation 0; weapon-view mode 0 (g_pistolSequence) entry 0
 */
	.globl	g_pistolAnim0Frames
g_pistolAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	   0,  -65,   40,   68, 0x0000, 0	/* frame 0 */
	.globl	g_pistolAnim0Script
g_pistolAnim0Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 7: frame list g_pistolAnim1Frames (3 frames) + script g_pistolAnim1Script
 *   used by sprite set 2 (record 0x8006d1d0) animation 1; weapon-view mode 0 (g_pistolSequence) entry 1
 */
	.globl	g_pistolAnim1Frames
g_pistolAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	  -4,  -86,   40,   88, 0x0000, 0	/* frame 0 */
	.half	   0,  -68,   40,   72, 0x0640, 0	/* frame 1 */
	.half	   1,  -65,   40,   68, 0x0b98, 0	/* frame 2 */
	.globl	g_pistolAnim1Script
g_pistolAnim1Script:
	.half	2, 3			/* 2 ticks per step, 3 frames */
	.half	0x810d, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(13), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 13, set flag 1, frames 0..2, stop */

/*
 * Animation 8: frame list g_pistolAnim2Frames (3 frames) + script g_pistolAnim2Script
 *   used by sprite set 2 (record 0x8006d1d0) animation 2; weapon-view mode 0 (g_pistolSequence) entry 2
 */
	.globl	g_pistolAnim2Frames
g_pistolAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -32,  -83,   76,   84, 0x0000, 0	/* frame 0 */
	.half	 -31, -106,  104,  108, 0x09c8, 0	/* frame 1 */
	.half	 -43,  -75,   84,   76, 0x1918, 0	/* frame 2 */
	.globl	g_pistolAnim2Script
g_pistolAnim2Script:
	.half	2, 3			/* 2 ticks per step, 3 frames */
	.half	0x8300, 0, 1, 2, 0x8600	/* TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = set flag 1, frames 0..2, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 9: frame list g_flamethrowerAnim0Frames (1 frame) + script g_flamethrowerAnim0Script
 *   used by sprite set 4 (record 0x8006d1f4) animation 0; weapon-view mode 4 (g_flamethrowerSequence) entry 0
 */
	.globl	g_flamethrowerAnim0Frames
g_flamethrowerAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	  -6,  -70,   68,   72, 0x0000, 0	/* frame 0 */
	.globl	g_flamethrowerAnim0Script
g_flamethrowerAnim0Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 10: frame list g_flamethrowerAnim1Frames (1 frame) + script g_flamethrowerAnim1Script
 *   used by sprite set 4 (record 0x8006d1f4) animation 1; weapon-view mode 4 (g_flamethrowerSequence) entry 1
 */
	.globl	g_flamethrowerAnim1Frames
g_flamethrowerAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	  -4,  -67,   64,   68, 0x0000, 0	/* frame 0 */
	.globl	g_flamethrowerAnim1Script
g_flamethrowerAnim1Script:
	.half	1, 1			/* 1 tick per step, 1 frame */
	.half	0x810b, 0x8300, 0, 0x8206	/* TRK_SIGNAL(11), TRK_FLAG1_ON, 0, TRK_LOOP(6) */
					/* = signal 11, set flag 1, frame 0, repeat 6 times */

/*
 * Animation 11: frame list g_flamethrowerAnim2Frames (5 frames) + script g_flamethrowerAnim2Script
 *   used by sprite set 4 (record 0x8006d1f4) animation 2; weapon-view mode 4 (g_flamethrowerSequence) entry 2
 */
	.globl	g_flamethrowerAnim2Frames
g_flamethrowerAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -14,  -68,   64,   72, 0x0000, 0	/* frame 0 */
	.half	 -19,  -82,   72,   84, 0x09f0, 0	/* frame 1 */
	.half	 -43,  -97,   92,  100, 0x15f4, 0	/* frame 2 */
	.half	 -19,  -82,   72,   84, 0x09f0, 0	/* frame 3 */
	.half	 -14,  -68,   64,   72, 0x0000, 0	/* frame 4 */
	.globl	g_flamethrowerAnim2Script
g_flamethrowerAnim2Script:
	.half	4, 5			/* 4 ticks per step, 5 frames */
	.half	0x8300, 0, 0x810a, 1, 2, 3, 4, 0x8600	/* TRK_FLAG1_ON, 0, TRK_SIGNAL(10), 1, 2, 3, 4, TRK_STOP */
					/* = set flag 1, frame 0, signal 10, frames 1..4, stop */

/*
 * Animation 12: frame list g_smartGunAnim1Frames (4 frames) + script g_smartGunAnim1Script
 *   used by sprite set 5 (record 0x8006d1dc) animation 1; weapon-view mode 3 (g_smartGunSequence) entry 1
 */
	.globl	g_smartGunAnim1Frames
g_smartGunAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -56, -108,  128,  112, 0x0000, 0	/* frame 0 */
	.half	 -49,  -52,  120,   56, 0x1124, 0	/* frame 1 */
	.half	 -56, -104,  128,  108, 0x1d2c, 0	/* frame 2 */
	.half	 -49,  -52,  120,   56, 0x1124, 0	/* frame 3 */
	.globl	g_smartGunAnim1Script
g_smartGunAnim1Script:
	.half	2, 4			/* 2 ticks per step, 4 frames */
	.half	0x8113, 0x8300, 0, 1, 2, 3, 0x8600	/* TRK_SIGNAL(19), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = signal 19, set flag 1, frames 0..3, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 13: frame list g_smartGunAnim2Frames (5 frames) + script g_smartGunAnim2Script
 *   used by sprite set 5 (record 0x8006d1dc) animation 2; weapon-view mode 3 (g_smartGunSequence) entry 2
 */
	.globl	g_smartGunAnim2Frames
g_smartGunAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -56,  -50,  128,   52, 0x0000, 0	/* frame 0 */
	.half	 -55,  -80,  124,   84, 0x0cec, 0	/* frame 1 */
	.half	 -57, -112,  116,  116, 0x1c74, 0	/* frame 2 */
	.half	 -55,  -80,  124,   84, 0x0cec, 0	/* frame 3 */
	.half	 -56,  -50,  128,   52, 0x0000, 0	/* frame 4 */
	.globl	g_smartGunAnim2Script
g_smartGunAnim2Script:
	.half	2, 5			/* 2 ticks per step, 5 frames */
	.half	0x8300, 0, 1, 0x8112, 2, 3, 4, 0x8600	/* TRK_FLAG1_ON, 0, 1, TRK_SIGNAL(18), 2, 3, 4, TRK_STOP */
					/* = set flag 1, frames 0,1, signal 18, frames 2..4, stop */

/*
 * Animation 14: frame list g_smartGunAnim0Frames (1 frame) + script g_smartGunAnim0Script
 *   used by sprite set 5 (record 0x8006d1dc) animation 0; weapon-view mode 3 (g_smartGunSequence) entry 0
 */
	.globl	g_smartGunAnim0Frames
g_smartGunAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -49,  -52,  120,   56, 0x0000, 0	/* frame 0 */
	.globl	g_smartGunAnim0Script
g_smartGunAnim0Script:
	.half	1, 1			/* 1 tick per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 15: frame list g_eggAnim0Frames (3 frames) + script g_eggAnim0Script
 *   used by sprite set 9 (record 0x8006d254) animation 0; g_eggAnimations[3] (read at 0x80028d50)
 */
	.globl	g_eggAnim0Frames
g_eggAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21,  -47,   48,   60, 0x0000, 0	/* frame 0 */
	.half	 -21,  -46,   48,   60, 0x05cc, 0	/* frame 1 */
	.half	 -21,  -48,   48,   60, 0x0b50, 0	/* frame 2 */
	.globl	g_eggAnim0Script
g_eggAnim0Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x812e, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(46), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 46, set flag 1, frames 0..2, stop */

/*
 * Animation 16: frame list g_eggAnim1Frames (3 frames) + script g_eggAnim1Script
 *   used by sprite set 9 (record 0x8006d254) animation 1; g_eggAnimations[4] (read at 0x80028d50)
 */
	.globl	g_eggAnim1Frames
g_eggAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -22,  -47,   52,   60, 0x0000, 0	/* frame 0 */
	.half	 -21,  -47,   48,   60, 0x056c, 0	/* frame 1 */
	.half	 -23,  -49,   52,   64, 0x0ae0, 0	/* frame 2 */
	.globl	g_eggAnim1Script
g_eggAnim1Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x812e, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(46), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 46, set flag 1, frames 0..2, stop */

/*
 * Animation 17: frame list g_eggAnim2Frames (5 frames) + script g_eggAnim2Script
 *   used by sprite set 9 (record 0x8006d254) animation 2; g_eggAnimations[1] (read at 0x80028d50)
 */
	.globl	g_eggAnim2Frames
g_eggAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -22,  -54,   52,   64, 0x0000, 0	/* frame 0 */
	.half	 -25,  -52,   52,   64, 0x0730, 0	/* frame 1 */
	.half	 -26,  -50,   56,   64, 0x0df0, 0	/* frame 2 */
	.half	 -26,  -33,   56,   48, 0x137c, 0	/* frame 3 */
	.half	 -26,  -22,   56,   36, 0x17b0, 0	/* frame 4 */
	.globl	g_eggAnim2Script
g_eggAnim2Script:
	.half	4, 5			/* 4 ticks per step, 5 frames */
	.half	0x812f, 0x8300, 0, 1, 2, 3, 0x8400, 4	/* TRK_SIGNAL(47), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG1_OFF, 4 */
	.half	0x8800	/* TRK_STOP4 */
					/* = signal 47, set flag 1, frames 0..3, clear flag 1, frame 4, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 18: frame list g_eggAnim3Frames (6 frames) + script g_eggAnim3Script
 *   used by sprite set 9 (record 0x8006d254) animation 3; g_eggAnimations[5] (read at 0x80028d50)
 */
	.globl	g_eggAnim3Frames
g_eggAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -26,  -22,   56,   36, 0x0000, 0	/* frame 0 */
	.half	 -26,  -22,   56,   36, 0x0340, 0	/* frame 1 */
	.half	 -26,  -22,   56,   36, 0x0684, 0	/* frame 2 */
	.half	 -26,  -22,   56,   36, 0x09cc, 0	/* frame 3 */
	.half	 -26,  -22,   56,   36, 0x0d1c, 0	/* frame 4 */
	.half	 -26,  -22,   56,   36, 0x1070, 0	/* frame 5 */
	.globl	g_eggAnim3Script
g_eggAnim3Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 0x8800	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, TRK_STOP4 */
					/* = set flag 1, frames 0..5, stop with flag 4 */

/*
 * Animation 19: frame list g_eggAnim4Frames (8 frames) + script g_eggAnim4Script
 *   used by sprite set 9 (record 0x8006d254) animation 4; g_eggAnimations[2] (read at 0x80028d50)
 */
	.globl	g_eggAnim4Frames
g_eggAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21,  -53,   48,   64, 0x0000, 0	/* frame 0 */
	.half	 -21,  -54,   48,   64, 0x0648, 0	/* frame 1 */
	.half	 -21,  -54,   48,   64, 0x0ce4, 0	/* frame 2 */
	.half	 -21,  -54,   48,   64, 0x13a8, 0	/* frame 3 */
	.half	 -21,  -54,   48,   64, 0x1a28, 0	/* frame 4 */
	.half	 -21,  -54,   48,   64, 0x20e4, 0	/* frame 5 */
	.half	 -21,  -54,   48,   64, 0x2788, 0	/* frame 6 */
	.half	 -23,  -49,   52,   64, 0x2e50, 0	/* frame 7 */
	.globl	g_eggAnim4Script
g_eggAnim4Script:
	.half	4, 8			/* 4 ticks per step, 8 frames */
	.half	0x812d, 0x8300, 0, 1, 2, 3, 4, 5	/* TRK_SIGNAL(45), TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5 */
	.half	6, 7, 0x8800	/* 6, 7, TRK_STOP4 */
					/* = signal 45, set flag 1, frames 0..7, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 20: frame list g_eggAnim5Frames (8 frames) + script g_eggAnim5Script
 *   used by sprite set 9 (record 0x8006d254) animation 5; g_eggAnimations[0] (read at 0x80028d50)
 */
	.globl	g_eggAnim5Frames
g_eggAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21,  -48,   48,   60, 0x0000, 0	/* frame 0 */
	.half	 -21,  -48,   48,   60, 0x05f0, 0	/* frame 1 */
	.half	 -21,  -50,   48,   64, 0x0bec, 0	/* frame 2 */
	.half	 -21,  -51,   48,   64, 0x1258, 0	/* frame 3 */
	.half	 -21,  -50,   48,   64, 0x18dc, 0	/* frame 4 */
	.half	 -21,  -48,   48,   60, 0x1f64, 0	/* frame 5 */
	.half	 -21,  -48,   48,   60, 0x258c, 0	/* frame 6 */
	.half	 -21,  -48,   48,   60, 0x2b7c, 0	/* frame 7 */
	.globl	g_eggAnim5Script
g_eggAnim5Script:
	.half	4, 8			/* 4 ticks per step, 8 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	7, 0x8800	/* 7, TRK_STOP4 */
					/* = set flag 1, frames 0..7, stop with flag 4 */

/*
 * Animation 21: frame list g_huggerAnim0Frames (4 frames) + script g_huggerAnim0Script
 *   used by sprite set 7 (record 0x8006d20c) animation 0; g_huggerAnimations[0] (read at 0x80028dcc)
 */
	.globl	g_huggerAnim0Frames
g_huggerAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -24,  -34,   48,   40, 0x0000, 0	/* frame 0 */
	.half	 -23,  -32,   52,   44, 0x02a0, 0	/* frame 1 */
	.half	 -21,  -32,   52,   36, 0x056c, 0	/* frame 2 */
	.half	 -22,  -32,   48,   40, 0x0810, 0	/* frame 3 */
	.globl	g_huggerAnim0Script
g_huggerAnim0Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8133, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(51), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 51, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 22: frame list g_huggerAnim4Frames (4 frames) + script g_huggerAnim4Script
 *   used by sprite set 7 (record 0x8006d20c) animation 4; g_huggerAnimations[4] (read at 0x80028dcc)
 */
	.globl	g_huggerAnim4Frames
g_huggerAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21,  -37,   48,   40, 0x0000, 0	/* frame 0 */
	.half	 -25,  -36,   52,   36, 0x0780, 0	/* frame 1 */
	.half	 -25,  -43,   48,   44, 0x0ed0, 0	/* frame 2 */
	.half	 -24,  -38,   52,   40, 0x1710, 0	/* frame 3 */
	.globl	g_huggerAnim4Script
g_huggerAnim4Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8133, 0x8400, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(51), TRK_FLAG1_OFF, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 51, clear flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 23: frame list g_huggerAnim2Frames (4 frames) + script g_huggerAnim2Script
 *   used by sprite set 7 (record 0x8006d20c) animation 2; g_huggerAnimations[1] (read at 0x80028dcc)
 *           g_huggerAnimations[7] (read at 0x80028dcc)
 */
	.globl	g_huggerAnim2Frames
g_huggerAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -69,  -33,   96,   32, 0x0000, 0	/* frame 0 */
	.half	 -67,  -32,   96,   36, 0x02e8, 0	/* frame 1 */
	.half	 -80,  -32,  108,   32, 0x05e8, 0	/* frame 2 */
	.half	 -80,  -31,  108,   32, 0x08f4, 0	/* frame 3 */
	.globl	g_huggerAnim2Script
g_huggerAnim2Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8133, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(51), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 51, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 24: frame list g_huggerAnim1Frames (4 frames) + script g_huggerAnim1Script
 *   used by sprite set 7 (record 0x8006d20c) animation 1; g_huggerAnimations[2] (read at 0x80028dcc)
 *           g_huggerAnimations[6] (read at 0x80028dcc)
 */
	.globl	g_huggerAnim1Frames
g_huggerAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -97,  -31,  128,   28, 0x0000, 0	/* frame 0 */
	.half	 -97,  -30,  128,   32, 0x02f8, 0	/* frame 1 */
	.half	 -90,  -31,  116,   32, 0x05e8, 0	/* frame 2 */
	.half	 -94,  -31,  120,   32, 0x08fc, 0	/* frame 3 */
	.globl	g_huggerAnim1Script
g_huggerAnim1Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8133, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(51), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 51, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 25: frame list g_huggerAnim3Frames (4 frames) + script g_huggerAnim3Script
 *   used by sprite set 7 (record 0x8006d20c) animation 3; g_huggerAnimations[3] (read at 0x80028dcc)
 *           g_huggerAnimations[5] (read at 0x80028dcc)
 */
	.globl	g_huggerAnim3Frames
g_huggerAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -67,  -32,   92,   36, 0x0000, 0	/* frame 0 */
	.half	 -69,  -30,  100,   32, 0x02d4, 0	/* frame 1 */
	.half	 -50,  -41,   80,   44, 0x0564, 0	/* frame 2 */
	.half	 -55,  -36,   80,   40, 0x081c, 0	/* frame 3 */
	.globl	g_huggerAnim3Script
g_huggerAnim3Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8133, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(51), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 51, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 26: frame list g_huggerAnim6Frames (6 frames) + script g_huggerAnim6Script
 *   used by sprite set 7 (record 0x8006d20c) animation 6; g_huggerAnimations[9] (read at 0x80028dcc)
 */
	.globl	g_huggerAnim6Frames
g_huggerAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -45,  -42,   76,   44, 0x0000, 0	/* frame 0 */
	.half	 -43,  -53,   92,   60, 0x0460, 0	/* frame 1 */
	.half	 -56,  -53,  116,   56, 0x0984, 0	/* frame 2 */
	.half	 -64,  -51,  128,   64, 0x1078, 0	/* frame 3 */
	.half	 -59,  -53,  120,   56, 0x1780, 0	/* frame 4 */
	.half	 -54,  -27,  108,   32, 0x1c18, 0	/* frame 5 */
	.globl	g_huggerAnim6Script
g_huggerAnim6Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8134, 0x8300, 0, 1, 2, 3, 4, 0x8400	/* TRK_SIGNAL(52), TRK_FLAG1_ON, 0, 1, 2, 3, 4, TRK_FLAG1_OFF */
	.half	5, 0x8800	/* 5, TRK_STOP4 */
					/* = signal 52, set flag 1, frames 0..4, clear flag 1, frame 5, stop with flag 4 */

/*
 * Animation 27: frame list g_huggerAnim7Frames (8 frames) + script g_huggerAnim7Script
 *   used by sprite set 7 (record 0x8006d20c) animation 7; g_huggerAnimations[10] (read at 0x80028dcc)
 */
	.globl	g_huggerAnim7Frames
g_huggerAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -23,  -54,   52,   64, 0x0000, 0	/* frame 0 */
	.half	 -25,  -56,   52,   64, 0x0d00, 0	/* frame 1 */
	.half	 -24,  -58,   48,   64, 0x1a00, 0	/* frame 2 */
	.half	 -24,  -58,   52,   64, 0x2600, 0	/* frame 3 */
	.half	 -24,  -61,   52,   64, 0x3300, 0	/* frame 4 */
	.half	 -25,  -61,   52,   64, 0x4000, 0	/* frame 5 */
	.half	 -24,  -56,   48,   52, 0x4d00, 0	/* frame 6 */
	.half	 -24,  -48,   52,   56, 0x56c0, 0	/* frame 7 */
	.globl	g_huggerAnim7Script
g_huggerAnim7Script:
	.half	4, 8			/* 4 ticks per step, 8 frames */
	.half	0x8130, 0x8400, 0, 1, 2, 3, 4, 5	/* TRK_SIGNAL(48), TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5 */
	.half	6, 0x8131, 7, 0x8600	/* 6, TRK_SIGNAL(49), 7, TRK_STOP */
					/* = signal 48, clear flag 1, frames 0..6, signal 49, frame 7, stop */

/*
 * Animation 28: frame list g_huggerAnim5Frames (4 frames) + script g_huggerAnim5Script
 *   used by sprite set 7 (record 0x8006d20c) animation 5; g_huggerAnimations[8] (read at 0x80028dcc)
 */
	.globl	g_huggerAnim5Frames
g_huggerAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -23,  -39,   64,   36, 0x0000, 0	/* frame 0 */
	.half	 -44,  -46,  100,   52, 0x0398, 0	/* frame 1 */
	.half	 -48,  -49,  112,   56, 0x08ac, 0	/* frame 2 */
	.half	 -60,  -51,  100,   64, 0x0d38, 0	/* frame 3 */
	.globl	g_huggerAnim5Script
g_huggerAnim5Script:
	.half	2, 4			/* 2 ticks per step, 4 frames */
	.half	0x8135, 0x8300, 0, 1, 2, 3, 0x8600	/* TRK_SIGNAL(53), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = signal 53, set flag 1, frames 0..3, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 29: frame list g_warriorAnim5Frames (7 frames) + script g_warriorAnim5Script
 *   used by sprite set 8 (record 0x8006d23c) animation 5; g_alienAnimations[2] (read at 0x80028ff8)
 *           g_alienAnimations[6] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim5Frames
g_warriorAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -60,  -87,  112,   84, 0x0000, 0	/* frame 0 */
	.half	 -59,  -89,  112,   88, 0x07fc, 0	/* frame 1 */
	.half	 -51,  -91,  112,   92, 0x103c, 0	/* frame 2 */
	.half	 -47,  -89,  108,   88, 0x18f8, 0	/* frame 3 */
	.half	 -46,  -89,  104,   88, 0x2180, 0	/* frame 4 */
	.half	 -50,  -89,  108,   92, 0x29ec, 0	/* frame 5 */
	.half	 -48,  -90,  104,   88, 0x334c, 0	/* frame 6 */
	.globl	g_warriorAnim5Script
g_warriorAnim5Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8147, 0x8300, 0, 1, 2, 3, 4, 5	/* TRK_SIGNAL(71), TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5 */
	.half	6, 0x8200	/* 6, TRK_LOOP(0) */
					/* = signal 71, set flag 1, frames 0..6, loop forever */

/*
 * Animation 30: frame list g_warriorAnim3Frames (7 frames) + script g_warriorAnim3Script
 *   used by sprite set 8 (record 0x8006d23c) animation 3; g_alienAnimations[0] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim3Frames
g_warriorAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -36,  -87,   56,   84, 0x0000, 0	/* frame 0 */
	.half	 -33,  -88,   56,   88, 0x0680, 0	/* frame 1 */
	.half	 -25,  -89,   48,   88, 0x0c98, 0	/* frame 2 */
	.half	 -19,  -88,   44,   84, 0x1280, 0	/* frame 3 */
	.half	 -18,  -87,   44,   84, 0x1884, 0	/* frame 4 */
	.half	 -22,  -88,   48,   88, 0x1eac, 0	/* frame 5 */
	.half	 -30,  -90,   56,   88, 0x2490, 0	/* frame 6 */
	.globl	g_warriorAnim3Script
g_warriorAnim3Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8147, 0x8300, 0, 1, 2, 3, 4, 5	/* TRK_SIGNAL(71), TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5 */
	.half	6, 0x8200	/* 6, TRK_LOOP(0) */
					/* = signal 71, set flag 1, frames 0..6, loop forever */

/*
 * Animation 31: frame list g_warriorAnim4Frames (7 frames) + script g_warriorAnim4Script
 *   used by sprite set 8 (record 0x8006d23c) animation 4; g_alienAnimations[1] (read at 0x80028ff8)
 *           g_alienAnimations[7] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim4Frames
g_warriorAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -56,  -87,   92,   84, 0x0000, 0	/* frame 0 */
	.half	 -53,  -89,   88,   88, 0x07b0, 0	/* frame 1 */
	.half	 -41,  -90,   84,   88, 0x0fc4, 0	/* frame 2 */
	.half	 -32,  -88,   80,   88, 0x17f8, 0	/* frame 3 */
	.half	 -27,  -88,   80,   88, 0x2060, 0	/* frame 4 */
	.half	 -31,  -89,   76,   92, 0x2890, 0	/* frame 5 */
	.half	 -37,  -90,   76,   88, 0x2ff8, 0	/* frame 6 */
	.globl	g_warriorAnim4Script
g_warriorAnim4Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 32: frame list g_warriorAnim7Frames (7 frames) + script g_warriorAnim7Script
 *   used by sprite set 8 (record 0x8006d23c) animation 7; g_alienAnimations[4] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim7Frames
g_warriorAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -20,  -88,   64,   88, 0x0000, 0	/* frame 0 */
	.half	 -21,  -89,   64,   88, 0x1600, 0	/* frame 1 */
	.half	 -22,  -91,   56,   92, 0x2c00, 0	/* frame 2 */
	.half	 -25,  -90,   48,   92, 0x4020, 0	/* frame 3 */
	.half	 -25,  -89,   48,   88, 0x5160, 0	/* frame 4 */
	.half	 -21,  -89,   48,   88, 0x61e0, 0	/* frame 5 */
	.half	 -20,  -92,   56,   92, 0x7260, 0	/* frame 6 */
	.globl	g_warriorAnim7Script
g_warriorAnim7Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8145, 0x8400, 0, 1, 2, 3, 4, 5	/* TRK_SIGNAL(69), TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5 */
	.half	6, 0x8200	/* 6, TRK_LOOP(0) */
					/* = signal 69, clear flag 1, frames 0..6, loop forever */

/*
 * Animation 33: frame list g_warriorAnim6Frames (7 frames) + script g_warriorAnim6Script
 *   used by sprite set 8 (record 0x8006d23c) animation 6; g_alienAnimations[3] (read at 0x80028ff8)
 *           g_alienAnimations[5] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim6Frames
g_warriorAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -31,  -88,   80,   84, 0x0000, 0	/* frame 0 */
	.half	 -34,  -89,   92,   88, 0x06e0, 0	/* frame 1 */
	.half	 -35,  -91,   92,   92, 0x0d1c, 0	/* frame 2 */
	.half	 -38,  -90,   84,   92, 0x1400, 0	/* frame 3 */
	.half	 -39,  -89,   84,   88, 0x1b74, 0	/* frame 4 */
	.half	 -40,  -89,   84,   88, 0x22e8, 0	/* frame 5 */
	.half	 -34,  -91,   84,   88, 0x2acc, 0	/* frame 6 */
	.globl	g_warriorAnim6Script
g_warriorAnim6Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 34: frame list g_warriorAnim0Frames (3 frames) + script g_warriorAnim0Script
 *   used by sprite set 8 (record 0x8006d23c) animation 0; g_alienAnimations[8] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim0Frames
g_warriorAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -35,  -96,   76,   96, 0x0000, 0	/* frame 0 */
	.half	 -33,  -95,   76,   96, 0x0890, 0	/* frame 1 */
	.half	 -57,  -98,   96,   96, 0x1168, 0	/* frame 2 */
	.globl	g_warriorAnim0Script
g_warriorAnim0Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x8300, 0, 1, 2, 0x8600	/* TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = set flag 1, frames 0..2, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 35: frame list g_warriorAnim1Frames (3 frames) + script g_warriorAnim1Script
 *   used by sprite set 8 (record 0x8006d23c) animation 1; g_alienAnimations[9] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim1Frames
g_warriorAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21, -101,   52,  100, 0x0000, 0	/* frame 0 */
	.half	 -27, -104,   64,  104, 0x06c8, 0	/* frame 1 */
	.half	 -35,  -99,   84,  100, 0x0eac, 0	/* frame 2 */
	.globl	g_warriorAnim1Script
g_warriorAnim1Script:
	.half	2, 3			/* 2 ticks per step, 3 frames */
	.half	0x8148, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(72), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 72, set flag 1, frames 0..2, stop */

/*
 * Animation 36: frame list g_warriorAnim2Frames (6 frames) + script g_warriorAnim2Script
 *   used by sprite set 8 (record 0x8006d23c) animation 2; g_alienAnimations[10] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim2Frames
g_warriorAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -30,  -98,   64,   96, 0x0000, 0	/* frame 0 */
	.half	 -39, -102,   80,  100, 0x06c8, 0	/* frame 1 */
	.half	 -49, -102,  108,   96, 0x0ecc, 0	/* frame 2 */
	.half	 -41, -102,   68,   92, 0x172c, 0	/* frame 3 */
	.half	 -50,  -71,   96,   72, 0x1e9c, 0	/* frame 4 */
	.half	 -58,  -31,  116,   36, 0x2448, 0	/* frame 5 */
	.globl	g_warriorAnim2Script
g_warriorAnim2Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8300, 0, 0x8146, 1, 2, 3, 4, 0x8400	/* TRK_FLAG1_ON, 0, TRK_SIGNAL(70), 1, 2, 3, 4, TRK_FLAG1_OFF */
	.half	5, 0x8800	/* 5, TRK_STOP4 */
					/* = set flag 1, frame 0, signal 70, frames 1..4, clear flag 1, frame 5, stop with flag 4 */

/*
 * Animation 37: frame list g_warriorAnim8Frames (4 frames) + script g_warriorAnim8Script
 *   used by sprite set 8 (record 0x8006d23c) animation 8; g_alienAnimations[11] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim8Frames
g_warriorAnim8Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -23,  -90,   56,   92, 0x0000, 0	/* frame 0 */
	.half	 -23,  -93,   64,   92, 0x1420, 0	/* frame 1 */
	.half	 -25,  -94,   72,   92, 0x2b20, 0	/* frame 2 */
	.half	 -26, -117,   60,  116, 0x4500, 0	/* frame 3 */
	.globl	g_warriorAnim8Script
g_warriorAnim8Script:
	.half	3, 4			/* 3 ticks per step, 4 frames */
	.half	0x8400, 0, 1, 0x8149, 2, 3, 0x8600	/* TRK_FLAG1_OFF, 0, 1, TRK_SIGNAL(73), 2, 3, TRK_STOP */
					/* = clear flag 1, frames 0,1, signal 73, frames 2,3, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 38: frame list g_warriorAnim9Frames (7 frames) + script g_warriorAnim9Script
 *   used by sprite set 8 (record 0x8006d23c) animation 9; g_alienAnimations[12] (read at 0x80028ff8)
 */
	.globl	g_warriorAnim9Frames
g_warriorAnim9Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -20, -100,   56,  100, 0x0000, 0	/* frame 0 */
	.half	 -22, -105,   60,  104, 0x15e0, 0	/* frame 1 */
	.half	 -23, -114,   64,  112, 0x2e40, 0	/* frame 2 */
	.half	 -26, -117,   68,  116, 0x4a40, 0	/* frame 3 */
	.half	 -60, -117,  120,  124, 0x6910, 0	/* frame 4 */
	.half	 -60, -117,  120,  124, 0x6910, 0	/* frame 5 */
	.half	 -26, -117,   68,  116, 0x4a40, 0	/* frame 6 */
	.globl	g_warriorAnim9Script
g_warriorAnim9Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8400, 0, 1, 2, 3, 0x8143, 4, 5	/* TRK_FLAG1_OFF, 0, 1, 2, 3, TRK_SIGNAL(67), 4, 5 */
	.half	6, 0x8600	/* 6, TRK_STOP */
					/* = clear flag 1, frames 0..3, signal 67, frames 4..6, stop */

/*
 * Animation 39: frame list g_handlerAnim0Frames (7 frames) + script g_handlerAnim0Script
 *   used by sprite set 10 (record 0x8006d260) animation 0; g_handlerAnimations[2] (read at 0x8002934c)
 *           g_handlerAnimations[6] (read at 0x8002934c)
 */
	.globl	g_handlerAnim0Frames
g_handlerAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -31, -107,   64,  104, 0x0000, 0	/* frame 0 */
	.half	 -17, -109,   52,  108, 0x0a70, 0	/* frame 1 */
	.half	 -22, -108,   56,  108, 0x1324, 0	/* frame 2 */
	.half	 -32, -106,   68,  104, 0x1e18, 0	/* frame 3 */
	.half	 -23, -108,   60,  108, 0x28fc, 0	/* frame 4 */
	.half	 -17, -109,   48,  108, 0x32a4, 0	/* frame 5 */
	.half	 -31, -106,   68,  104, 0x3cf8, 0	/* frame 6 */
	.globl	g_handlerAnim0Script
g_handlerAnim0Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 40: frame list g_handlerAnim1Frames (7 frames) + script g_handlerAnim1Script
 *   used by sprite set 10 (record 0x8006d260) animation 1; g_handlerAnimations[0] (read at 0x8002934c)
 */
	.globl	g_handlerAnim1Frames
g_handlerAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -30, -106,   56,  104, 0x0000, 0	/* frame 0 */
	.half	 -28, -109,   52,  108, 0x0a6c, 0	/* frame 1 */
	.half	 -24, -108,   52,  108, 0x1510, 0	/* frame 2 */
	.half	 -23, -106,   52,  108, 0x1f6c, 0	/* frame 3 */
	.half	 -27, -107,   52,  108, 0x2988, 0	/* frame 4 */
	.half	 -30, -108,   56,  108, 0x33dc, 0	/* frame 5 */
	.half	 -33, -106,   56,  104, 0x3ee4, 0	/* frame 6 */
	.globl	g_handlerAnim1Script
g_handlerAnim1Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 41: frame list g_handlerAnim2Frames (7 frames) + script g_handlerAnim2Script
 *   used by sprite set 10 (record 0x8006d260) animation 2; g_handlerAnimations[1] (read at 0x8002934c)
 *           g_handlerAnimations[7] (read at 0x8002934c)
 */
	.globl	g_handlerAnim2Frames
g_handlerAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -19, -107,   48,  104, 0x0000, 0	/* frame 0 */
	.half	 -19, -109,   48,  108, 0x08f4, 0	/* frame 1 */
	.half	 -23, -108,   56,  108, 0x127c, 0	/* frame 2 */
	.half	 -29, -106,   60,  108, 0x1cd8, 0	/* frame 3 */
	.half	 -23, -107,   52,  108, 0x2754, 0	/* frame 4 */
	.half	 -18, -108,   48,  108, 0x30e4, 0	/* frame 5 */
	.half	 -18, -106,   48,  108, 0x3910, 0	/* frame 6 */
	.globl	g_handlerAnim2Script
g_handlerAnim2Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 42: frame list g_handlerAnim3Frames (7 frames) + script g_handlerAnim3Script
 *   used by sprite set 10 (record 0x8006d260) animation 3; g_handlerAnimations[4] (read at 0x8002934c)
 */
	.globl	g_handlerAnim3Frames
g_handlerAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -22, -107,   56,  108, 0x0000, 0	/* frame 0 */
	.half	 -23, -109,   56,  108, 0x17a0, 0	/* frame 1 */
	.half	 -24, -108,   56,  108, 0x2f40, 0	/* frame 2 */
	.half	 -25, -106,   56,  104, 0x46e0, 0	/* frame 3 */
	.half	 -24, -108,   56,  108, 0x5da0, 0	/* frame 4 */
	.half	 -24, -109,   60,  108, 0x7540, 0	/* frame 5 */
	.half	 -22, -106,   60,  104, 0x8e90, 0	/* frame 6 */
	.globl	g_handlerAnim3Script
g_handlerAnim3Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8400, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = clear flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 43: frame list g_handlerAnim4Frames (7 frames) + script g_handlerAnim4Script
 *   used by sprite set 10 (record 0x8006d260) animation 4; g_handlerAnimations[3] (read at 0x8002934c)
 *           g_handlerAnimations[5] (read at 0x8002934c)
 */
	.globl	g_handlerAnim4Frames
g_handlerAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -28, -107,   72,  104, 0x0000, 0	/* frame 0 */
	.half	 -19, -109,   64,  108, 0x0bc4, 0	/* frame 1 */
	.half	 -19, -108,   64,  108, 0x16c4, 0	/* frame 2 */
	.half	 -20, -106,   64,  108, 0x2124, 0	/* frame 3 */
	.half	 -21, -108,   64,  108, 0x2be0, 0	/* frame 4 */
	.half	 -21, -109,   64,  108, 0x3648, 0	/* frame 5 */
	.half	 -27, -106,   68,  104, 0x4258, 0	/* frame 6 */
	.globl	g_handlerAnim4Script
g_handlerAnim4Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 44: frame list g_handlerAnim5Frames (3 frames) + script g_handlerAnim5Script
 *   used by sprite set 10 (record 0x8006d260) animation 5; g_handlerAnimations[8] (read at 0x8002934c)
 */
	.globl	g_handlerAnim5Frames
g_handlerAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -22, -108,   56,  108, 0x0000, 0	/* frame 0 */
	.half	 -65, -108,  108,  108, 0x0bf8, 0	/* frame 1 */
	.half	 -59, -118,   96,  116, 0x189c, 0	/* frame 2 */
	.globl	g_handlerAnim5Script
g_handlerAnim5Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x8156, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(86), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 86, set flag 1, frames 0..2, stop */

/*
 * Animation 45: frame list g_handlerAnim6Frames (5 frames) + script g_handlerAnim6Script
 *   used by sprite set 10 (record 0x8006d260) animation 6; g_handlerAnimations[9] (read at 0x8002934c)
 */
	.globl	g_handlerAnim6Frames
g_handlerAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -56, -112,   88,  124, 0x0000, 0	/* frame 0 */
	.half	 -58, -108,   88,  120, 0x0e44, 0	/* frame 1 */
	.half	 -62, -110,  108,  116, 0x1c98, 0	/* frame 2 */
	.half	 -62,  -80,  108,   88, 0x2a60, 0	/* frame 3 */
	.half	 -63,  -26,  112,   32, 0x3670, 0	/* frame 4 */
	.globl	g_handlerAnim6Script
g_handlerAnim6Script:
	.half	4, 5			/* 4 ticks per step, 5 frames */
	.half	0x8157, 0x8300, 0, 1, 2, 3, 0x8400, 4	/* TRK_SIGNAL(87), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG1_OFF, 4 */
	.half	0x8800	/* TRK_STOP4 */
					/* = signal 87, set flag 1, frames 0..3, clear flag 1, frame 4, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 46: frame list g_handlerAnim7Frames (8 frames) + script g_handlerAnim7Script
 *   used by sprite set 10 (record 0x8006d260) animation 7; g_handlerAnimations[10] (read at 0x8002934c)
 */
	.globl	g_handlerAnim7Frames
g_handlerAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21, -107,   48,  104, 0x0000, 0	/* frame 0 */
	.half	 -25, -116,   52,  112, 0x1380, 0	/* frame 1 */
	.half	 -21, -111,   48,  108, 0x2a40, 0	/* frame 2 */
	.half	 -21, -108,   48,  104, 0x3e80, 0	/* frame 3 */
	.half	 -21, -107,   48,  104, 0x5200, 0	/* frame 4 */
	.half	 -20, -105,   60,  104, 0x6580, 0	/* frame 5 */
	.half	 -20, -106,   48,  104, 0x7de0, 0	/* frame 6 */
	.half	 -20, -107,   48,  104, 0x9160, 0	/* frame 7 */
	.globl	g_handlerAnim7Script
g_handlerAnim7Script:
	.half	4, 8			/* 4 ticks per step, 8 frames */
	.half	0x8400, 0, 1, 0x8107, 2, 3, 4, 0x8900	/* TRK_FLAG1_OFF, 0, 1, TRK_SIGNAL(7), 2, 3, 4, TRK_FLAG8 */
	.half	0x8108, 5, 6, 7, 0x8600	/* TRK_SIGNAL(8), 5, 6, 7, TRK_STOP */
					/* = clear flag 1, frames 0,1, signal 7, frames 2..4, set flag 8, signal 8, frames 5..7, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 47: frame list g_handlerAnim8Frames (3 frames) + script g_handlerAnim8Script
 *   used by sprite set 10 (record 0x8006d260) animation 8; g_handlerAnimations[11] (read at 0x8002934c)
 */
	.globl	g_handlerAnim8Frames
g_handlerAnim8Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -23, -103,   44,  104, 0x0000, 0	/* frame 0 */
	.half	 -23, -103,   44,  104, 0x11e0, 0	/* frame 1 */
	.half	 -24, -103,   44,  104, 0x23c0, 0	/* frame 2 */
	.globl	g_handlerAnim8Script
g_handlerAnim8Script:
	.half	2, 3			/* 2 ticks per step, 3 frames */
	.half	0x8400, 0, 0x811e, 1, 0x8900, 2, 0x8600	/* TRK_FLAG1_OFF, 0, TRK_SIGNAL(30), 1, TRK_FLAG8, 2, TRK_STOP */
					/* = clear flag 1, frame 0, signal 30, frame 1, set flag 8, frame 2, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 48: frame list g_bursterAnim0Frames (4 frames) + script g_bursterAnim0Script
 *   used by sprite set 14 (record 0x8006d290) animation 0; g_bursterAnimations[12] (read at 0x80028e24)
 */
	.globl	g_bursterAnim0Frames
g_bursterAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -18,  -48,   40,   52, 0x0000, 0	/* frame 0 */
	.half	 -28,  -33,   36,   36, 0x0820, 0	/* frame 1 */
	.half	 -23,  -26,   36,   28, 0x0d30, 0	/* frame 2 */
	.half	 -17,  -42,   44,   40, 0x1120, 0	/* frame 3 */
	.globl	g_bursterAnim0Script
g_bursterAnim0Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8136, 0x8400, 0, 1, 2, 3, 0x8600	/* TRK_SIGNAL(54), TRK_FLAG1_OFF, 0, 1, 2, 3, TRK_STOP */
					/* = signal 54, clear flag 1, frames 0..3, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 49: frame list g_bursterAnim1Frames (8 frames) + script g_bursterAnim1Script
 *   used by sprite set 14 (record 0x8006d290) animation 1; g_bursterAnimations[13] (read at 0x80028e24)
 */
	.globl	g_bursterAnim1Frames
g_bursterAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -17,  -97,   36,   32, 0x0000, 0	/* frame 0 */
	.half	 -12, -109,   24,   40, 0x0480, 0	/* frame 1 */
	.half	 -14, -114,   32,   32, 0x0840, 0	/* frame 2 */
	.half	 -14, -106,   32,   36, 0x0c40, 0	/* frame 3 */
	.half	 -20,  -80,   40,   44, 0x10c0, 0	/* frame 4 */
	.half	 -27,  -59,   40,   36, 0x17a0, 0	/* frame 5 */
	.half	 -22,  -42,   40,   32, 0x1d40, 0	/* frame 6 */
	.half	 -27,  -33,   40,   36, 0x2240, 0	/* frame 7 */
	.globl	g_bursterAnim1Script
g_bursterAnim1Script:
	.half	4, 8			/* 4 ticks per step, 8 frames */
	.half	0x813b, 0x8400, 0, 1, 2, 3, 4, 5	/* TRK_SIGNAL(59), TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5 */
	.half	6, 7, 0x8600	/* 6, 7, TRK_STOP */
					/* = signal 59, clear flag 1, frames 0..7, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 50: frame list g_bursterAnim2Frames (4 frames) + script g_bursterAnim2Script
 *   used by sprite set 14 (record 0x8006d290) animation 2; g_bursterAnimations[8] (read at 0x80028e24)
 */
	.globl	g_bursterAnim2Frames
g_bursterAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -23,  -51,   52,   64, 0x0000, 0	/* frame 0 */
	.half	 -29,  -50,   72,   60, 0x03d0, 0	/* frame 1 */
	.half	 -38,  -48,   76,   60, 0x07e4, 0	/* frame 2 */
	.half	 -43,  -50,   84,   52, 0x0bf4, 0	/* frame 3 */
	.globl	g_bursterAnim2Script
g_bursterAnim2Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8138, 0x8300, 0, 1, 2, 3, 0x8600	/* TRK_SIGNAL(56), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = signal 56, set flag 1, frames 0..3, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 51: frame list g_bursterAnim3Frames (4 frames) + script g_bursterAnim3Script
 *   used by sprite set 14 (record 0x8006d290) animation 3; g_bursterAnimations[9] (read at 0x80028e24)
 */
	.globl	g_bursterAnim3Frames
g_bursterAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -23,  -36,   40,   40, 0x0000, 0	/* frame 0 */
	.half	 -42,  -43,   72,   44, 0x021c, 0	/* frame 1 */
	.half	 -57,  -37,  104,   48, 0x05a8, 0	/* frame 2 */
	.half	 -60,  -43,  112,   44, 0x08c8, 0	/* frame 3 */
	.globl	g_bursterAnim3Script
g_bursterAnim3Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8138, 0x8300, 0, 1, 2, 3, 0x8600	/* TRK_SIGNAL(56), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = signal 56, set flag 1, frames 0..3, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 52: frame list g_bursterAnim4Frames (6 frames) + script g_bursterAnim4Script
 *   used by sprite set 14 (record 0x8006d290) animation 4; g_bursterAnimations[10] (read at 0x80028e24)
 */
	.globl	g_bursterAnim4Frames
g_bursterAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -39,  -47,   88,   64, 0x0000, 0	/* frame 0 */
	.half	 -50,  -49,  108,   64, 0x0600, 0	/* frame 1 */
	.half	 -67,  -43,  128,   52, 0x0c7c, 0	/* frame 2 */
	.half	 -53,  -40,  112,   48, 0x126c, 0	/* frame 3 */
	.half	 -60,  -43,  124,   48, 0x1748, 0	/* frame 4 */
	.half	 -60,  -17,  124,   36, 0x1ccc, 0	/* frame 5 */
	.globl	g_bursterAnim4Script
g_bursterAnim4Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8139, 0x8300, 0, 1, 2, 3, 4, 0x8400	/* TRK_SIGNAL(57), TRK_FLAG1_ON, 0, 1, 2, 3, 4, TRK_FLAG1_OFF */
	.half	5, 0x8800	/* 5, TRK_STOP4 */
					/* = signal 57, set flag 1, frames 0..4, clear flag 1, frame 5, stop with flag 4 */

/*
 * Animation 53: frame list g_bursterAnim5Frames (6 frames) + script g_bursterAnim5Script
 *   used by sprite set 14 (record 0x8006d290) animation 5; g_bursterAnimations[11] (read at 0x80028e24)
 */
	.globl	g_bursterAnim5Frames
g_bursterAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -32,  -44,   56,   44, 0x0000, 0	/* frame 0 */
	.half	 -46,  -49,   84,   60, 0x0320, 0	/* frame 1 */
	.half	 -54,  -49,  108,   56, 0x0910, 0	/* frame 2 */
	.half	 -65,  -51,  124,   60, 0x0ef4, 0	/* frame 3 */
	.half	 -62,  -47,  120,   56, 0x1438, 0	/* frame 4 */
	.half	 -62,  -14,  124,   20, 0x1aa8, 0	/* frame 5 */
	.globl	g_bursterAnim5Script
g_bursterAnim5Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8139, 0x8300, 0, 1, 2, 3, 4, 0x8400	/* TRK_SIGNAL(57), TRK_FLAG1_ON, 0, 1, 2, 3, 4, TRK_FLAG1_OFF */
	.half	5, 0x8800	/* 5, TRK_STOP4 */
					/* = signal 57, set flag 1, frames 0..4, clear flag 1, frame 5, stop with flag 4 */

/*
 * Animation 54: frame list g_bursterAnim6Frames (4 frames) + script g_bursterAnim6Script
 *   used by sprite set 14 (record 0x8006d290) animation 6; g_bursterAnimations[1] (read at 0x80028e24)
 *           g_bursterAnimations[7] (read at 0x80028e24)
 */
	.globl	g_bursterAnim6Frames
g_bursterAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -18,  -38,   64,   44, 0x0000, 0	/* frame 0 */
	.half	 -65,  -33,  100,   44, 0x02bc, 0	/* frame 1 */
	.half	 -37,  -36,   76,   40, 0x0570, 0	/* frame 2 */
	.half	 -29,  -37,   64,   44, 0x0818, 0	/* frame 3 */
	.globl	g_bursterAnim6Script
g_bursterAnim6Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8137, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(55), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 55, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 55: frame list g_bursterAnim7Frames (4 frames) + script g_bursterAnim7Script
 *   used by sprite set 14 (record 0x8006d290) animation 7; g_bursterAnimations[3] (read at 0x80028e24)
 *           g_bursterAnimations[5] (read at 0x80028e24)
 */
	.globl	g_bursterAnim7Frames
g_bursterAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -48,  -34,   64,   36, 0x0000, 0	/* frame 0 */
	.half	 -50,  -27,   88,   28, 0x028c, 0	/* frame 1 */
	.half	 -42,  -31,   72,   32, 0x0530, 0	/* frame 2 */
	.half	 -45,  -34,   64,   36, 0x07d0, 0	/* frame 3 */
	.globl	g_bursterAnim7Script
g_bursterAnim7Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8137, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(55), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 55, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 56: frame list g_bursterAnim8Frames (4 frames) + script g_bursterAnim8Script
 *   used by sprite set 14 (record 0x8006d290) animation 8; g_bursterAnimations[2] (read at 0x80028e24)
 *           g_bursterAnimations[6] (read at 0x80028e24)
 */
	.globl	g_bursterAnim8Frames
g_bursterAnim8Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -35,  -38,   60,   44, 0x0000, 0	/* frame 0 */
	.half	 -82,  -32,  128,   32, 0x02a0, 0	/* frame 1 */
	.half	 -57,  -35,   96,   36, 0x0560, 0	/* frame 2 */
	.half	 -49,  -37,   80,   40, 0x07f4, 0	/* frame 3 */
	.globl	g_bursterAnim8Script
g_bursterAnim8Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8137, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(55), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 55, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 57: frame list g_bursterAnim9Frames (4 frames) + script g_bursterAnim9Script
 *   used by sprite set 14 (record 0x8006d290) animation 9; g_bursterAnimations[0] (read at 0x80028e24)
 */
	.globl	g_bursterAnim9Frames
g_bursterAnim9Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -14,  -38,   60,   40, 0x0000, 0	/* frame 0 */
	.half	 -16,  -34,   52,   60, 0x0288, 0	/* frame 1 */
	.half	 -21,  -36,   60,   48, 0x051c, 0	/* frame 2 */
	.half	 -17,  -38,   56,   48, 0x079c, 0	/* frame 3 */
	.globl	g_bursterAnim9Script
g_bursterAnim9Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8137, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(55), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 55, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 58: frame list g_bursterAnim10Frames (4 frames) + script g_bursterAnim10Script
 *   used by sprite set 14 (record 0x8006d290) animation 10; g_bursterAnimations[4] (read at 0x80028e24)
 */
	.globl	g_bursterAnim10Frames
g_bursterAnim10Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -24,  -37,   52,   36, 0x0000, 0	/* frame 0 */
	.half	 -13,  -29,   36,   28, 0x0210, 0	/* frame 1 */
	.half	 -18,  -33,   48,   32, 0x0398, 0	/* frame 2 */
	.half	 -17,  -36,   44,   36, 0x0588, 0	/* frame 3 */
	.globl	g_bursterAnim10Script
g_bursterAnim10Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8137, 0x8300, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(55), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 55, set flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 59: frame list g_bambiAnim0Frames (6 frames) + script g_bambiAnim0Script
 *   used by sprite set 15 (record 0x8006d29c) animation 0; g_bambiAnimations[12] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim0Frames
g_bambiAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -17,  -59,   36,   64, 0x0000, 0	/* frame 0 */
	.half	 -20,  -64,   36,   68, 0x0900, 0	/* frame 1 */
	.half	 -16,  -69,   36,   76, 0x1290, 0	/* frame 2 */
	.half	 -26,  -75,   56,   84, 0x1d40, 0	/* frame 3 */
	.half	 -18,  -70,   36,   80, 0x2fa0, 0	/* frame 4 */
	.half	 -16,  -66,   36,   72, 0x3ae0, 0	/* frame 5 */
	.globl	g_bambiAnim0Script
g_bambiAnim0Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8400, 0, 1, 2, 0x813c, 3, 4, 5	/* TRK_FLAG1_OFF, 0, 1, 2, TRK_SIGNAL(60), 3, 4, 5 */
	.half	0x8600	/* TRK_STOP */
					/* = clear flag 1, frames 0..2, signal 60, frames 3..5, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 60: frame list g_bambiAnim1Frames (3 frames) + script g_bambiAnim1Script
 *   used by sprite set 15 (record 0x8006d29c) animation 1; g_bambiAnimations[8] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim1Frames
g_bambiAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21,  -68,   40,   68, 0x0000, 0	/* frame 0 */
	.half	 -30,  -74,   72,   76, 0x05a0, 0	/* frame 1 */
	.half	 -32,  -89,   68,   88, 0x0cf8, 0	/* frame 2 */
	.globl	g_bambiAnim1Script
g_bambiAnim1Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x813d, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(61), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 61, set flag 1, frames 0..2, stop */

/*
 * Animation 61: frame list g_bambiAnim2Frames (3 frames) + script g_bambiAnim2Script
 *   used by sprite set 15 (record 0x8006d29c) animation 2; g_bambiAnimations[9] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim2Frames
g_bambiAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -17,  -71,   44,   76, 0x0000, 0	/* frame 0 */
	.half	 -37,  -76,   72,   76, 0x0624, 0	/* frame 1 */
	.half	 -30,  -88,   76,   96, 0x0d68, 0	/* frame 2 */
	.globl	g_bambiAnim2Script
g_bambiAnim2Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x813d, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(61), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 61, set flag 1, frames 0..2, stop */

/*
 * Animation 62: frame list g_bambiAnim3Frames (7 frames) + script g_bambiAnim3Script
 *   used by sprite set 15 (record 0x8006d29c) animation 3; g_bambiAnimations[2] (read at 0x80028e7c)
 *           g_bambiAnimations[6] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim3Frames
g_bambiAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -48,  -73,   92,   72, 0x0000, 0	/* frame 0 */
	.half	 -38,  -79,   88,   76, 0x0850, 0	/* frame 1 */
	.half	 -55,  -81,  116,   80, 0x109c, 0	/* frame 2 */
	.half	 -41,  -76,   96,   76, 0x1994, 0	/* frame 3 */
	.half	 -36,  -74,   76,   72, 0x2234, 0	/* frame 4 */
	.half	 -29,  -83,   72,   84, 0x2a0c, 0	/* frame 5 */
	.half	 -38,  -83,   96,   84, 0x3234, 0	/* frame 6 */
	.globl	g_bambiAnim3Script
g_bambiAnim3Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 63: frame list g_bambiAnim4Frames (7 frames) + script g_bambiAnim4Script
 *   used by sprite set 15 (record 0x8006d29c) animation 4; g_bambiAnimations[0] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim4Frames
g_bambiAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -13,  -74,   36,   72, 0x0000, 0	/* frame 0 */
	.half	 -13,  -78,   44,   76, 0x04fc, 0	/* frame 1 */
	.half	 -14,  -82,   36,   84, 0x0ac8, 0	/* frame 2 */
	.half	 -13,  -78,   32,   76, 0x1018, 0	/* frame 3 */
	.half	 -17,  -75,   36,   72, 0x1550, 0	/* frame 4 */
	.half	 -22,  -85,   40,   84, 0x1ab8, 0	/* frame 5 */
	.half	 -19,  -84,   40,   84, 0x206c, 0	/* frame 6 */
	.globl	g_bambiAnim4Script
g_bambiAnim4Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 64: frame list g_bambiAnim5Frames (7 frames) + script g_bambiAnim5Script
 *   used by sprite set 15 (record 0x8006d29c) animation 5; g_bambiAnimations[4] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim5Frames
g_bambiAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -18,  -72,   36,   72, 0x0000, 0	/* frame 0 */
	.half	 -27,  -80,   44,   80, 0x0a20, 0	/* frame 1 */
	.half	 -22,  -81,   40,   80, 0x17e0, 0	/* frame 2 */
	.half	 -16,  -76,   32,   80, 0x2460, 0	/* frame 3 */
	.half	 -15,  -73,   36,   76, 0x2e60, 0	/* frame 4 */
	.half	 -15,  -84,   40,   84, 0x3910, 0	/* frame 5 */
	.half	 -18,  -83,   44,   84, 0x4630, 0	/* frame 6 */
	.globl	g_bambiAnim5Script
g_bambiAnim5Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8400, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = clear flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 65: frame list g_bambiAnim6Frames (7 frames) + script g_bambiAnim6Script
 *   used by sprite set 15 (record 0x8006d29c) animation 6; g_bambiAnimations[1] (read at 0x80028e7c)
 *           g_bambiAnimations[7] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim6Frames
g_bambiAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -35,  -74,   68,   72, 0x0000, 0	/* frame 0 */
	.half	 -23,  -78,   72,   76, 0x06a8, 0	/* frame 1 */
	.half	 -44,  -82,   92,   80, 0x0e2c, 0	/* frame 2 */
	.half	 -36,  -77,   72,   76, 0x15d0, 0	/* frame 3 */
	.half	 -34,  -75,   64,   72, 0x1cf0, 0	/* frame 4 */
	.half	 -23,  -84,   56,   84, 0x235c, 0	/* frame 5 */
	.half	 -25,  -84,   64,   84, 0x2a28, 0	/* frame 6 */
	.globl	g_bambiAnim6Script
g_bambiAnim6Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 66: frame list g_bambiAnim7Frames (7 frames) + script g_bambiAnim7Script
 *   used by sprite set 15 (record 0x8006d29c) animation 7; g_bambiAnimations[3] (read at 0x80028e7c)
 *           g_bambiAnimations[5] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim7Frames
g_bambiAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -33,  -72,   68,   72, 0x0000, 0	/* frame 0 */
	.half	 -43,  -80,   80,   80, 0x0774, 0	/* frame 1 */
	.half	 -38,  -81,   80,   80, 0x0f04, 0	/* frame 2 */
	.half	 -26,  -76,   68,   76, 0x1724, 0	/* frame 3 */
	.half	 -20,  -73,   52,   76, 0x1ed4, 0	/* frame 4 */
	.half	 -26,  -84,   68,   84, 0x2578, 0	/* frame 5 */
	.half	 -31,  -83,   84,   84, 0x2d70, 0	/* frame 6 */
	.globl	g_bambiAnim7Script
g_bambiAnim7Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 67: frame list g_bambiAnim8Frames (6 frames) + script g_bambiAnim8Script
 *   used by sprite set 15 (record 0x8006d29c) animation 8; g_bambiAnimations[10] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim8Frames
g_bambiAnim8Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -23,  -71,   44,   72, 0x0000, 0	/* frame 0 */
	.half	 -40,  -87,   76,   88, 0x05c8, 0	/* frame 1 */
	.half	 -47,  -86,   88,   92, 0x0e94, 0	/* frame 2 */
	.half	 -42,  -73,   76,   76, 0x1764, 0	/* frame 3 */
	.half	 -46,  -59,   96,   60, 0x20bc, 0	/* frame 4 */
	.half	 -56,  -37,  116,   40, 0x2884, 0	/* frame 5 */
	.globl	g_bambiAnim8Script
g_bambiAnim8Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x813e, 0x8300, 0, 1, 2, 3, 4, 0x8400	/* TRK_SIGNAL(62), TRK_FLAG1_ON, 0, 1, 2, 3, 4, TRK_FLAG1_OFF */
	.half	5, 0x8800	/* 5, TRK_STOP4 */
					/* = signal 62, set flag 1, frames 0..4, clear flag 1, frame 5, stop with flag 4 */

/*
 * Animation 68: frame list g_bambiAnim9Frames (6 frames) + script g_bambiAnim9Script
 *   used by sprite set 15 (record 0x8006d29c) animation 9; g_bambiAnimations[11] (read at 0x80028e7c)
 */
	.globl	g_bambiAnim9Frames
g_bambiAnim9Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -26,  -72,   52,   68, 0x0000, 0	/* frame 0 */
	.half	 -24,  -81,   56,   80, 0x05b4, 0	/* frame 1 */
	.half	 -26,  -85,   68,   80, 0x0bb4, 0	/* frame 2 */
	.half	 -27,  -71,   72,   72, 0x1228, 0	/* frame 3 */
	.half	 -37, -110,   88,  116, 0x1928, 0	/* frame 4 */
	.half	 -42,  -35,   96,   40, 0x1ea8, 0	/* frame 5 */
	.globl	g_bambiAnim9Script
g_bambiAnim9Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x813e, 0x8300, 0, 1, 2, 3, 4, 0x8400	/* TRK_SIGNAL(62), TRK_FLAG1_ON, 0, 1, 2, 3, 4, TRK_FLAG1_OFF */
	.half	5, 0x8800	/* 5, TRK_STOP4 */
					/* = signal 62, set flag 1, frames 0..4, clear flag 1, frame 5, stop with flag 4 */

/*
 * Animation 69: frame list g_soldierAnim0Frames (4 frames) + script g_soldierAnim0Script
 *   used by sprite set 13 (record 0x8006d284) animation 0; g_soldierAnimations[8] (read at 0x80029294)
 */
	.globl	g_soldierAnim0Frames
g_soldierAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -24, -117,   48,  112, 0x0000, 0	/* frame 0 */
	.half	 -58, -117,  100,  116, 0x0948, 0	/* frame 1 */
	.half	 -61, -116,  100,  112, 0x12ec, 0	/* frame 2 */
	.half	 -61, -113,  100,  108, 0x1da4, 0	/* frame 3 */
	.globl	g_soldierAnim0Script
g_soldierAnim0Script:
	.half	2, 4			/* 2 ticks per step, 4 frames */
	.half	0x8152, 0x8300, 0, 1, 2, 3, 0x8600	/* TRK_SIGNAL(82), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = signal 82, set flag 1, frames 0..3, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 70: frame list g_soldierAnim1Frames (3 frames) + script g_soldierAnim1Script
 *   used by sprite set 13 (record 0x8006d284) animation 1; g_soldierAnimations[9] (read at 0x80029294)
 */
	.globl	g_soldierAnim1Frames
g_soldierAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -10, -114,   48,  112, 0x0000, 0	/* frame 0 */
	.half	 -13, -115,   44,  116, 0x1500, 0	/* frame 1 */
	.half	 -10, -116,   44,  116, 0x28f0, 0	/* frame 2 */
	.globl	g_soldierAnim1Script
g_soldierAnim1Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x8400, 0, 1, 0x8900, 0x811e, 2, 0x8600	/* TRK_FLAG1_OFF, 0, 1, TRK_FLAG8, TRK_SIGNAL(30), 2, TRK_STOP */
					/* = clear flag 1, frames 0,1, set flag 8, signal 30, frame 2, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 71: frame list g_soldierAnim2Frames (5 frames) + script g_soldierAnim2Script
 *   used by sprite set 13 (record 0x8006d284) animation 2; g_soldierAnimations[10] (read at 0x80029294)
 */
	.globl	g_soldierAnim2Frames
g_soldierAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -37,  -98,   60,  108, 0x0000, 0	/* frame 0 */
	.half	 -33, -101,   56,  108, 0x0904, 0	/* frame 1 */
	.half	 -49,  -90,   80,   92, 0x1120, 0	/* frame 2 */
	.half	 -53,  -69,   80,   68, 0x1934, 0	/* frame 3 */
	.half	 -60,  -24,   96,   32, 0x1eb4, 0	/* frame 4 */
	.globl	g_soldierAnim2Script
g_soldierAnim2Script:
	.half	4, 5			/* 4 ticks per step, 5 frames */
	.half	0x8153, 0x8300, 0, 1, 2, 3, 0x8400, 4	/* TRK_SIGNAL(83), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG1_OFF, 4 */
	.half	0x8800	/* TRK_STOP4 */
					/* = signal 83, set flag 1, frames 0..3, clear flag 1, frame 4, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 72: frame list g_soldierAnim3Frames (7 frames) + script g_soldierAnim3Script
 *   used by sprite set 13 (record 0x8006d284) animation 3; g_soldierAnimations[0] (read at 0x80029294)
 */
	.globl	g_soldierAnim3Frames
g_soldierAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -39, -114,   60,  116, 0x0000, 0	/* frame 0 */
	.half	 -40, -114,   60,  116, 0x08f8, 0	/* frame 1 */
	.half	 -35, -115,   56,  112, 0x11b4, 0	/* frame 2 */
	.half	 -26, -115,   48,  116, 0x1a40, 0	/* frame 3 */
	.half	 -21, -113,   44,  116, 0x22dc, 0	/* frame 4 */
	.half	 -21, -113,   44,  116, 0x2b0c, 0	/* frame 5 */
	.half	 -28, -115,   48,  112, 0x3324, 0	/* frame 6 */
	.globl	g_soldierAnim3Script
g_soldierAnim3Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 73: frame list g_soldierAnim5Frames (7 frames) + script g_soldierAnim5Script
 *   used by sprite set 13 (record 0x8006d284) animation 5; g_soldierAnimations[4] (read at 0x80029294)
 */
	.globl	g_soldierAnim5Frames
g_soldierAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -18, -112,   64,  112, 0x0000, 0	/* frame 0 */
	.half	 -17, -113,   64,  112, 0x1c00, 0	/* frame 1 */
	.half	 -17, -114,   60,  112, 0x3800, 0	/* frame 2 */
	.half	 -17, -114,   48,  112, 0x5240, 0	/* frame 3 */
	.half	 -19, -112,   48,  112, 0x6740, 0	/* frame 4 */
	.half	 -18, -112,   44,  112, 0x7c40, 0	/* frame 5 */
	.half	 -18, -113,   52,  112, 0x8f80, 0	/* frame 6 */
	.globl	g_soldierAnim5Script
g_soldierAnim5Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8400, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = clear flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 74: frame list g_soldierAnim4Frames (7 frames) + script g_soldierAnim4Script
 *   used by sprite set 13 (record 0x8006d284) animation 4; g_soldierAnimations[2] (read at 0x80029294)
 *           g_soldierAnimations[6] (read at 0x80029294)
 */
	.globl	g_soldierAnim4Frames
g_soldierAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -34, -113,   64,  116, 0x0000, 0	/* frame 0 */
	.half	 -45, -113,   72,  112, 0x08ec, 0	/* frame 1 */
	.half	 -37, -114,   68,  112, 0x11a4, 0	/* frame 2 */
	.half	 -20, -114,   56,  112, 0x19d4, 0	/* frame 3 */
	.half	 -34, -112,   72,  112, 0x217c, 0	/* frame 4 */
	.half	 -41, -112,   80,  112, 0x2b08, 0	/* frame 5 */
	.half	 -30, -114,   60,  112, 0x3450, 0	/* frame 6 */
	.globl	g_soldierAnim4Script
g_soldierAnim4Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 75: frame list g_soldierAnim6Frames (7 frames) + script g_soldierAnim6Script
 *   used by sprite set 13 (record 0x8006d284) animation 6; g_soldierAnimations[1] (read at 0x80029294)
 *           g_soldierAnimations[7] (read at 0x80029294)
 */
	.globl	g_soldierAnim6Frames
g_soldierAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -23, -113,   44,  116, 0x0000, 0	/* frame 0 */
	.half	 -31, -114,   52,  112, 0x0784, 0	/* frame 1 */
	.half	 -26, -114,   44,  112, 0x0f1c, 0	/* frame 2 */
	.half	 -20, -115,   40,  116, 0x1694, 0	/* frame 3 */
	.half	 -31, -112,   56,  116, 0x1ec0, 0	/* frame 4 */
	.half	 -38, -113,   60,  112, 0x2738, 0	/* frame 5 */
	.half	 -28, -114,   48,  112, 0x2f74, 0	/* frame 6 */
	.globl	g_soldierAnim6Script
g_soldierAnim6Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 76: frame list g_soldierAnim7Frames (7 frames) + script g_soldierAnim7Script
 *   used by sprite set 13 (record 0x8006d284) animation 7; g_soldierAnimations[3] (read at 0x80029294)
 *           g_soldierAnimations[5] (read at 0x80029294)
 */
	.globl	g_soldierAnim7Frames
g_soldierAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -27, -112,   72,  112, 0x0000, 0	/* frame 0 */
	.half	 -34, -112,   80,  112, 0x0968, 0	/* frame 1 */
	.half	 -28, -113,   76,  112, 0x1294, 0	/* frame 2 */
	.half	 -18, -114,   64,  112, 0x1bd0, 0	/* frame 3 */
	.half	 -19, -111,   64,  112, 0x23d0, 0	/* frame 4 */
	.half	 -23, -112,   68,  112, 0x2c7c, 0	/* frame 5 */
	.half	 -19, -113,   64,  112, 0x352c, 0	/* frame 6 */
	.globl	g_soldierAnim7Script
g_soldierAnim7Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 77: frame list g_guardAnim0Frames (3 frames) + script g_guardAnim0Script
 *   used by sprite set 12 (record 0x8006d278) animation 0; g_guardAnimations[8] (read at 0x80029238)
 */
	.globl	g_guardAnim0Frames
g_guardAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -24, -121,   56,  124, 0x0000, 0	/* frame 0 */
	.half	 -39, -120,   64,  120, 0x09e0, 0	/* frame 1 */
	.half	 -45, -120,   64,  124, 0x1208, 0	/* frame 2 */
	.globl	g_guardAnim0Script
g_guardAnim0Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x8158, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(88), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 88, set flag 1, frames 0..2, stop */

/*
 * Animation 78: frame list g_guardAnim1Frames (5 frames) + script g_guardAnim1Script
 *   used by sprite set 12 (record 0x8006d278) animation 1; g_guardAnimations[10] (read at 0x80029238)
 */
	.globl	g_guardAnim1Frames
g_guardAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -39, -108,   72,  120, 0x0000, 0	/* frame 0 */
	.half	 -27,  -91,   64,  100, 0x07f0, 0	/* frame 1 */
	.half	 -43,  -68,   84,   76, 0x0d94, 0	/* frame 2 */
	.half	 -44,  -50,   88,   60, 0x1144, 0	/* frame 3 */
	.half	 -42,  -17,   88,   32, 0x15d4, 0	/* frame 4 */
	.globl	g_guardAnim1Script
g_guardAnim1Script:
	.half	4, 5			/* 4 ticks per step, 5 frames */
	.half	0x8159, 0x8300, 0, 1, 2, 3, 0x8400, 4	/* TRK_SIGNAL(89), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG1_OFF, 4 */
	.half	0x8800	/* TRK_STOP4 */
					/* = signal 89, set flag 1, frames 0..3, clear flag 1, frame 4, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 79: frame list g_guardAnim2Frames (3 frames) + script g_guardAnim2Script
 *   used by sprite set 12 (record 0x8006d278) animation 2; g_guardAnimations[9] (read at 0x80029238)
 */
	.globl	g_guardAnim2Frames
g_guardAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -20, -117,   36,  120, 0x0000, 0	/* frame 0 */
	.half	 -24, -121,   40,  124, 0x10e0, 0	/* frame 1 */
	.half	 -25, -118,   40,  120, 0x2440, 0	/* frame 2 */
	.globl	g_guardAnim2Script
g_guardAnim2Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x811d, 0x8400, 0, 0x8900, 1, 2, 0x8203	/* TRK_SIGNAL(29), TRK_FLAG1_OFF, 0, TRK_FLAG8, 1, 2, TRK_LOOP(3) */
					/* = signal 29, clear flag 1, frame 0, set flag 8, frames 1,2, repeat 3 times */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 80: frame list g_guardAnim4Frames (7 frames) + script g_guardAnim4Script
 *   used by sprite set 12 (record 0x8006d278) animation 4; g_guardAnimations[0] (read at 0x80029238)
 */
	.globl	g_guardAnim4Frames
g_guardAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -27, -119,   56,  120, 0x0000, 0	/* frame 0 */
	.half	 -28, -121,   60,  120, 0x08f0, 0	/* frame 1 */
	.half	 -28, -119,   60,  116, 0x12a0, 0	/* frame 2 */
	.half	 -30, -118,   60,  120, 0x1bd8, 0	/* frame 3 */
	.half	 -33, -119,   60,  120, 0x24e0, 0	/* frame 4 */
	.half	 -30, -120,   56,  120, 0x2e44, 0	/* frame 5 */
	.half	 -27, -118,   52,  116, 0x375c, 0	/* frame 6 */
	.globl	g_guardAnim4Script
g_guardAnim4Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 81: frame list g_guardAnim3Frames (7 frames) + script g_guardAnim3Script
 *   used by sprite set 12 (record 0x8006d278) animation 3; g_guardAnimations[4] (read at 0x80029238)
 */
	.globl	g_guardAnim3Frames
g_guardAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -26, -121,   60,  120, 0x0000, 0	/* frame 0 */
	.half	 -30, -123,   60,  124, 0x1c20, 0	/* frame 1 */
	.half	 -31, -121,   60,  120, 0x3930, 0	/* frame 2 */
	.half	 -29, -120,   60,  120, 0x5550, 0	/* frame 3 */
	.half	 -25, -121,   60,  120, 0x7170, 0	/* frame 4 */
	.half	 -23, -123,   56,  120, 0x8d90, 0	/* frame 5 */
	.half	 -22, -120,   56,  116, 0xa7d0, 0	/* frame 6 */
	.globl	g_guardAnim3Script
g_guardAnim3Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8400, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = clear flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 82: frame list g_guardAnim5Frames (7 frames) + script g_guardAnim5Script
 *   used by sprite set 12 (record 0x8006d278) animation 5; g_guardAnimations[2] (read at 0x80029238)
 *           g_guardAnimations[6] (read at 0x80029238)
 */
	.globl	g_guardAnim5Frames
g_guardAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -27, -120,   60,  120, 0x0000, 0	/* frame 0 */
	.half	  -8, -122,   28,  120, 0x07b0, 0	/* frame 1 */
	.half	 -22, -121,   64,  120, 0x0dbc, 0	/* frame 2 */
	.half	 -32, -118,   76,  120, 0x15cc, 0	/* frame 3 */
	.half	 -21, -120,   48,  120, 0x1da0, 0	/* frame 4 */
	.half	 -10, -121,   36,  120, 0x2408, 0	/* frame 5 */
	.half	 -26, -119,   72,  116, 0x2b70, 0	/* frame 6 */
	.globl	g_guardAnim5Script
g_guardAnim5Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 83: frame list g_guardAnim7Frames (7 frames) + script g_guardAnim7Script
 *   used by sprite set 12 (record 0x8006d278) animation 7; g_guardAnimations[1] (read at 0x80029238)
 *           g_guardAnimations[7] (read at 0x80029238)
 */
	.globl	g_guardAnim7Frames
g_guardAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -27, -119,   56,  116, 0x0000, 0	/* frame 0 */
	.half	 -21, -122,   52,  120, 0x0874, 0	/* frame 1 */
	.half	 -31, -120,   76,  120, 0x11e4, 0	/* frame 2 */
	.half	 -34, -118,   76,  120, 0x1aac, 0	/* frame 3 */
	.half	 -26, -119,   60,  120, 0x2330, 0	/* frame 4 */
	.half	 -16, -120,   44,  116, 0x2c18, 0	/* frame 5 */
	.half	 -28, -118,   68,  116, 0x34b8, 0	/* frame 6 */
	.globl	g_guardAnim7Script
g_guardAnim7Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 84: frame list g_guardAnim6Frames (7 frames) + script g_guardAnim6Script
 *   used by sprite set 12 (record 0x8006d278) animation 6; g_guardAnimations[3] (read at 0x80029238)
 *           g_guardAnimations[5] (read at 0x80029238)
 */
	.globl	g_guardAnim6Frames
g_guardAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -27, -121,   64,  120, 0x0000, 0	/* frame 0 */
	.half	 -19, -123,   44,  124, 0x0858, 0	/* frame 1 */
	.half	 -20, -122,   64,  120, 0x10c4, 0	/* frame 2 */
	.half	 -27, -120,   72,  120, 0x18c4, 0	/* frame 3 */
	.half	 -20, -121,   52,  120, 0x20c0, 0	/* frame 4 */
	.half	 -20, -122,   60,  120, 0x2920, 0	/* frame 5 */
	.half	 -27, -120,   72,  116, 0x30e8, 0	/* frame 6 */
	.globl	g_guardAnim6Script
g_guardAnim6Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 85: frame list g_synthAnim0Frames (3 frames) + script g_synthAnim0Script
 *   used by sprite set 11 (record 0x8006d26c) animation 0; g_synthAnimations[8] (read at 0x800292f0)
 */
	.globl	g_synthAnim0Frames
g_synthAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -38, -107,   88,  108, 0x0000, 0	/* frame 0 */
	.half	 -49, -101,   72,  100, 0x0808, 0	/* frame 1 */
	.half	 -52, -106,   80,  104, 0x0fb0, 0	/* frame 2 */
	.globl	g_synthAnim0Script
g_synthAnim0Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x8154, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(84), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 84, set flag 1, frames 0..2, stop */

/*
 * Animation 86: frame list g_synthAnim1Frames (2 frames) + script g_synthAnim1Script
 *   used by sprite set 11 (record 0x8006d26c) animation 1; g_synthAnimations[9] (read at 0x800292f0)
 */
	.globl	g_synthAnim1Frames
g_synthAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -22, -113,   44,  112, 0x0000, 0	/* frame 0 */
	.half	 -33, -123,   72,  124, 0x072c, 0	/* frame 1 */
	.globl	g_synthAnim1Script
g_synthAnim1Script:
	.half	4, 2			/* 4 ticks per step, 2 frames */
	.half	0x8154, 0x8300, 0, 1, 0x8600	/* TRK_SIGNAL(84), TRK_FLAG1_ON, 0, 1, TRK_STOP */
					/* = signal 84, set flag 1, frames 0,1, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 87: frame list g_synthAnim2Frames (4 frames) + script g_synthAnim2Script
 *   used by sprite set 11 (record 0x8006d26c) animation 2; g_synthAnimations[11] (read at 0x800292f0)
 */
	.globl	g_synthAnim2Frames
g_synthAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -37, -108,   72,  120, 0x0000, 0	/* frame 0 */
	.half	 -40, -105,   88,  100, 0x0a0c, 0	/* frame 1 */
	.half	 -53, -106,  108,  108, 0x114c, 0	/* frame 2 */
	.half	 -58,  -25,  120,   28, 0x16c0, 0	/* frame 3 */
	.globl	g_synthAnim2Script
g_synthAnim2Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8155, 0x8300, 0, 1, 2, 0x8400, 3, 0x8800	/* TRK_SIGNAL(85), TRK_FLAG1_ON, 0, 1, 2, TRK_FLAG1_OFF, 3, TRK_STOP4 */
					/* = signal 85, set flag 1, frames 0..2, clear flag 1, frame 3, stop with flag 4 */

/*
 * Animation 88: frame list g_synthAnim3Frames (4 frames) + script g_synthAnim3Script
 *   used by sprite set 11 (record 0x8006d26c) animation 3; g_synthAnimations[12] (read at 0x800292f0)
 */
	.globl	g_synthAnim3Frames
g_synthAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -62, -113,   88,  128, 0x0000, 0	/* frame 0 */
	.half	 -49, -114,   76,  128, 0x08b0, 0	/* frame 1 */
	.half	 -59,  -93,   92,  100, 0x10d4, 0	/* frame 2 */
	.half	 -55,  -26,  108,   28, 0x16b4, 0	/* frame 3 */
	.globl	g_synthAnim3Script
g_synthAnim3Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8155, 0x8300, 0, 1, 2, 0x8400, 3, 0x8800	/* TRK_SIGNAL(85), TRK_FLAG1_ON, 0, 1, 2, TRK_FLAG1_OFF, 3, TRK_STOP4 */
					/* = signal 85, set flag 1, frames 0..2, clear flag 1, frame 3, stop with flag 4 */

/*
 * Animation 89: frame list g_synthAnim4Frames (3 frames) + script g_synthAnim4Script
 *   used by sprite set 11 (record 0x8006d26c) animation 4; g_synthAnimations[10] (read at 0x800292f0)
 */
	.globl	g_synthAnim4Frames
g_synthAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -27, -111,   44,  112, 0x0000, 0	/* frame 0 */
	.half	 -50, -111,   68,  112, 0x1340, 0	/* frame 1 */
	.half	 -28, -111,   44,  112, 0x3100, 0	/* frame 2 */
	.globl	g_synthAnim4Script
g_synthAnim4Script:
	.half	1, 3			/* 1 tick per step, 3 frames */
	.half	0x8400, 0, 0x811f, 1, 0x8900, 2, 0x8600	/* TRK_FLAG1_OFF, 0, TRK_SIGNAL(31), 1, TRK_FLAG8, 2, TRK_STOP */
					/* = clear flag 1, frame 0, signal 31, frame 1, set flag 8, frame 2, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 90: frame list g_synthAnim6Frames (7 frames) + script g_synthAnim6Script
 *   used by sprite set 11 (record 0x8006d26c) animation 6; g_synthAnimations[0] (read at 0x800292f0)
 */
	.globl	g_synthAnim6Frames
g_synthAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -34, -119,   68,  120, 0x0000, 0	/* frame 0 */
	.half	 -35, -120,   68,  120, 0x07c8, 0	/* frame 1 */
	.half	 -35, -120,   72,  120, 0x0f7c, 0	/* frame 2 */
	.half	 -32, -120,   68,  120, 0x1718, 0	/* frame 3 */
	.half	 -29, -120,   64,  120, 0x1ecc, 0	/* frame 4 */
	.half	 -30, -122,   64,  120, 0x2678, 0	/* frame 5 */
	.half	 -31, -122,   68,  124, 0x2e80, 0	/* frame 6 */
	.globl	g_synthAnim6Script
g_synthAnim6Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 91: frame list g_synthAnim5Frames (7 frames) + script g_synthAnim5Script
 *   used by sprite set 11 (record 0x8006d26c) animation 5; g_synthAnimations[4] (read at 0x800292f0)
 */
	.globl	g_synthAnim5Frames
g_synthAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -33, -123,   72,  124, 0x0000, 0	/* frame 0 */
	.half	 -34, -124,   76,  124, 0x22e0, 0	/* frame 1 */
	.half	 -34, -125,   76,  124, 0x47b0, 0	/* frame 2 */
	.half	 -35, -124,   72,  124, 0x6c80, 0	/* frame 3 */
	.half	 -35, -124,   68,  124, 0x8f60, 0	/* frame 4 */
	.half	 -35, -125,   72,  124, 0xb050, 0	/* frame 5 */
	.half	 -34, -126,   72,  128, 0xd330, 0	/* frame 6 */
	.globl	g_synthAnim5Script
g_synthAnim5Script:
	.half	3, 7			/* 3 ticks per step, 7 frames */
	.half	0x8400, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = clear flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 92: frame list g_synthAnim7Frames (7 frames) + script g_synthAnim7Script
 *   used by sprite set 11 (record 0x8006d26c) animation 7; g_synthAnimations[2] (read at 0x800292f0)
 */
	.globl	g_synthAnim7Frames
g_synthAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -28, -119,   72,  120, 0x0000, 0	/* frame 0 */
	.half	 -15, -119,   56,  120, 0x08ac, 0	/* frame 1 */
	.half	  -7, -120,   48,  120, 0x1078, 0	/* frame 2 */
	.half	 -22, -119,   60,  120, 0x1850, 0	/* frame 3 */
	.half	 -28, -120,   68,  120, 0x2108, 0	/* frame 4 */
	.half	 -19, -121,   60,  120, 0x29c8, 0	/* frame 5 */
	.half	 -13, -122,   56,  124, 0x3188, 0	/* frame 6 */
	.globl	g_synthAnim7Script
g_synthAnim7Script:
	.half	3, 7			/* 3 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 93: frame list g_synthAnim8Frames (7 frames) + script g_synthAnim8Script
 *   used by sprite set 11 (record 0x8006d26c) animation 8; g_synthAnimations[6] (read at 0x800292f0)
 */
	.globl	g_synthAnim8Frames
g_synthAnim8Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -37, -124,   68,  124, 0x0000, 0	/* frame 0 */
	.half	 -37, -125,   56,  124, 0x087c, 0	/* frame 1 */
	.half	 -37, -125,   48,  124, 0x1058, 0	/* frame 2 */
	.half	 -36, -124,   60,  124, 0x18b4, 0	/* frame 3 */
	.half	 -35, -124,   68,  124, 0x2194, 0	/* frame 4 */
	.half	 -35, -126,   56,  124, 0x2aac, 0	/* frame 5 */
	.half	 -36, -126,   52,  124, 0x32fc, 0	/* frame 6 */
	.globl	g_synthAnim8Script
g_synthAnim8Script:
	.half	3, 7			/* 3 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 94: frame list g_synthAnim11Frames (7 frames) + script g_synthAnim11Script
 *   used by sprite set 11 (record 0x8006d26c) animation 11; g_synthAnimations[1] (read at 0x800292f0)
 */
	.globl	g_synthAnim11Frames
g_synthAnim11Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -14, -118,   64,  120, 0x0000, 0	/* frame 0 */
	.half	 -13, -118,   64,  116, 0x06e8, 0	/* frame 1 */
	.half	 -13, -119,   60,  120, 0x0dcc, 0	/* frame 2 */
	.half	 -23, -119,   72,  120, 0x1590, 0	/* frame 3 */
	.half	 -27, -119,   76,  120, 0x1d40, 0	/* frame 4 */
	.half	 -19, -121,   68,  120, 0x24d0, 0	/* frame 5 */
	.half	 -13, -121,   64,  120, 0x2c14, 0	/* frame 6 */
	.globl	g_synthAnim11Script
g_synthAnim11Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 95: frame list g_synthAnim12Frames (7 frames) + script g_synthAnim12Script
 *   used by sprite set 11 (record 0x8006d26c) animation 12; g_synthAnimations[7] (read at 0x800292f0)
 */
	.globl	g_synthAnim12Frames
g_synthAnim12Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -44, -122,   76,  120, 0x0000, 0	/* frame 0 */
	.half	 -48, -122,   72,  120, 0x0728, 0	/* frame 1 */
	.half	 -48, -123,   64,  124, 0x0e2c, 0	/* frame 2 */
	.half	 -45, -122,   60,  124, 0x143c, 0	/* frame 3 */
	.half	 -41, -122,   60,  124, 0x1b00, 0	/* frame 4 */
	.half	 -40, -124,   60,  124, 0x2160, 0	/* frame 5 */
	.half	 -39, -124,   60,  124, 0x27d0, 0	/* frame 6 */
	.globl	g_synthAnim12Script
g_synthAnim12Script:
	.half	3, 7			/* 3 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 96: frame list g_synthAnim9Frames (7 frames) + script g_synthAnim9Script
 *   used by sprite set 11 (record 0x8006d26c) animation 9; g_synthAnimations[3] (read at 0x800292f0)
 */
	.globl	g_synthAnim9Frames
g_synthAnim9Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -27, -121,   76,  120, 0x0000, 0	/* frame 0 */
	.half	 -19, -122,   68,  120, 0x08d0, 0	/* frame 1 */
	.half	 -11, -122,   64,  124, 0x1108, 0	/* frame 2 */
	.half	 -12, -122,   60,  120, 0x18a8, 0	/* frame 3 */
	.half	 -16, -122,   60,  124, 0x207c, 0	/* frame 4 */
	.half	 -16, -123,   60,  124, 0x281c, 0	/* frame 5 */
	.half	 -18, -124,   60,  124, 0x2fe4, 0	/* frame 6 */
	.globl	g_synthAnim9Script
g_synthAnim9Script:
	.half	3, 7			/* 3 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 97: frame list g_synthAnim10Frames (7 frames) + script g_synthAnim10Script
 *   used by sprite set 11 (record 0x8006d26c) animation 10; g_synthAnimations[5] (read at 0x800292f0)
 */
	.globl	g_synthAnim10Frames
g_synthAnim10Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -48, -125,   64,  124, 0x0000, 0	/* frame 0 */
	.half	 -46, -126,   64,  128, 0x0858, 0	/* frame 1 */
	.half	 -45, -126,   64,  128, 0x115c, 0	/* frame 2 */
	.half	 -46, -125,   72,  124, 0x1b28, 0	/* frame 3 */
	.half	 -47, -125,   76,  124, 0x24ec, 0	/* frame 4 */
	.half	 -47, -126,   68,  124, 0x2e94, 0	/* frame 5 */
	.half	 -47, -126,   64,  128, 0x37c8, 0	/* frame 6 */
	.globl	g_synthAnim10Script
g_synthAnim10Script:
	.half	3, 7			/* 3 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 98: frame list g_dogAnim0Frames (3 frames) + script g_dogAnim0Script
 *   used by sprite set 6 (record 0x8006d224) animation 0; g_dogAnimations[8] (read at 0x80028ed4)
 */
	.globl	g_dogAnim0Frames
g_dogAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -60, -107,   92,  104, 0x0000, 0	/* frame 0 */
	.half	 -35, -102,   72,  100, 0x0738, 0	/* frame 1 */
	.half	 -42, -102,   84,  100, 0x0ed0, 0	/* frame 2 */
	.globl	g_dogAnim0Script
g_dogAnim0Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x8140, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(64), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 64, set flag 1, frames 0..2, stop */

/*
 * Animation 99: frame list g_dogAnim1Frames (3 frames) + script g_dogAnim1Script
 *   used by sprite set 6 (record 0x8006d224) animation 1; g_dogAnimations[9] (read at 0x80028ed4)
 */
	.globl	g_dogAnim1Frames
g_dogAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -19, -109,   48,  108, 0x0000, 0	/* frame 0 */
	.half	 -18, -109,   48,  108, 0x06ac, 0	/* frame 1 */
	.half	 -31, -102,   68,  104, 0x0d2c, 0	/* frame 2 */
	.globl	g_dogAnim1Script
g_dogAnim1Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x8140, 0x8300, 0, 1, 2, 0x8600	/* TRK_SIGNAL(64), TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = signal 64, set flag 1, frames 0..2, stop */

/*
 * Animation 100: frame list g_dogAnim2Frames (5 frames) + script g_dogAnim2Script
 *   used by sprite set 6 (record 0x8006d224) animation 2; g_dogAnimations[12] (read at 0x80028ed4)
 */
	.globl	g_dogAnim2Frames
g_dogAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -36, -120,   80,  120, 0x0000, 0	/* frame 0 */
	.half	 -30, -111,   84,   92, 0x0758, 0	/* frame 1 */
	.half	 -33,  -82,   88,   72, 0x0ebc, 0	/* frame 2 */
	.half	 -44,  -70,  104,   64, 0x1708, 0	/* frame 3 */
	.half	 -52,  -36,  112,   32, 0x1eac, 0	/* frame 4 */
	.globl	g_dogAnim2Script
g_dogAnim2Script:
	.half	4, 5			/* 4 ticks per step, 5 frames */
	.half	0x8141, 0x8300, 0, 1, 2, 3, 0x8400, 4	/* TRK_SIGNAL(65), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG1_OFF, 4 */
	.half	0x8800	/* TRK_STOP4 */
					/* = signal 65, set flag 1, frames 0..3, clear flag 1, frame 4, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 101: frame list g_dogAnim3Frames (5 frames) + script g_dogAnim3Script
 *   used by sprite set 6 (record 0x8006d224) animation 3; g_dogAnimations[13] (read at 0x80028ed4)
 */
	.globl	g_dogAnim3Frames
g_dogAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -37,  -99,   60,   96, 0x0000, 0	/* frame 0 */
	.half	 -40, -111,  100,  108, 0x0860, 0	/* frame 1 */
	.half	 -50, -105,  116,   96, 0x1428, 0	/* frame 2 */
	.half	 -52,  -82,  112,   80, 0x1f04, 0	/* frame 3 */
	.half	 -56,  -31,  120,   32, 0x285c, 0	/* frame 4 */
	.globl	g_dogAnim3Script
g_dogAnim3Script:
	.half	4, 5			/* 4 ticks per step, 5 frames */
	.half	0x8141, 0x8300, 0, 1, 2, 3, 0x8400, 4	/* TRK_SIGNAL(65), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG1_OFF, 4 */
	.half	0x8800	/* TRK_STOP4 */
					/* = signal 65, set flag 1, frames 0..3, clear flag 1, frame 4, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 102: frame list g_dogAnim4Frames (7 frames) + script g_dogAnim4Script
 *   used by sprite set 6 (record 0x8006d224) animation 4; g_dogAnimations[0] (read at 0x80028ed4)
 */
	.globl	g_dogAnim4Frames
g_dogAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -24,  -90,   56,   80, 0x0000, 0	/* frame 0 */
	.half	 -17,  -89,   56,   88, 0x0630, 0	/* frame 1 */
	.half	 -16,  -93,   56,   92, 0x0c68, 0	/* frame 2 */
	.half	 -34,  -89,   68,   88, 0x1268, 0	/* frame 3 */
	.half	 -49,  -89,   72,   84, 0x193c, 0	/* frame 4 */
	.half	 -47,  -89,   72,   88, 0x1fac, 0	/* frame 5 */
	.half	 -44,  -89,   76,   88, 0x2618, 0	/* frame 6 */
	.globl	g_dogAnim4Script
g_dogAnim4Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 103: frame list g_dogAnim5Frames (7 frames) + script g_dogAnim5Script
 *   used by sprite set 6 (record 0x8006d224) animation 5; g_dogAnimations[1] (read at 0x80028ed4)
 *           g_dogAnimations[7] (read at 0x80028ed4)
 */
	.globl	g_dogAnim5Frames
g_dogAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -40,  -90,   84,   84, 0x0000, 0	/* frame 0 */
	.half	 -29,  -89,   76,   88, 0x0714, 0	/* frame 1 */
	.half	 -34,  -93,   92,   92, 0x0eb8, 0	/* frame 2 */
	.half	 -52,  -89,  112,   88, 0x174c, 0	/* frame 3 */
	.half	 -55,  -90,  112,   84, 0x1f24, 0	/* frame 4 */
	.half	 -50,  -89,  104,   84, 0x273c, 0	/* frame 5 */
	.half	 -49,  -89,  100,   84, 0x2f4c, 0	/* frame 6 */
	.globl	g_dogAnim5Script
g_dogAnim5Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 104: frame list g_dogAnim6Frames (7 frames) + script g_dogAnim6Script
 *   used by sprite set 6 (record 0x8006d224) animation 6; g_dogAnimations[2] (read at 0x80028ed4)
 *           g_dogAnimations[6] (read at 0x80028ed4)
 */
	.globl	g_dogAnim6Frames
g_dogAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -42,  -91,  100,   84, 0x0000, 0	/* frame 0 */
	.half	 -44,  -90,  104,   88, 0x0880, 0	/* frame 1 */
	.half	 -49,  -90,  108,   88, 0x1174, 0	/* frame 2 */
	.half	 -49,  -90,  104,   92, 0x1aa8, 0	/* frame 3 */
	.half	 -42,  -91,  108,   88, 0x2408, 0	/* frame 4 */
	.half	 -43,  -90,  104,   88, 0x2ccc, 0	/* frame 5 */
	.half	 -47,  -89,   96,   84, 0x3554, 0	/* frame 6 */
	.globl	g_dogAnim6Script
g_dogAnim6Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 105: frame list g_dogAnim7Frames (7 frames) + script g_dogAnim7Script
 *   used by sprite set 6 (record 0x8006d224) animation 7; g_dogAnimations[3] (read at 0x80028ed4)
 *           g_dogAnimations[5] (read at 0x80028ed4)
 */
	.globl	g_dogAnim7Frames
g_dogAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -41,  -93,  104,   88, 0x0000, 0	/* frame 0 */
	.half	 -39,  -92,   84,   92, 0x0814, 0	/* frame 1 */
	.half	 -33,  -91,   72,   92, 0x1024, 0	/* frame 2 */
	.half	 -29,  -91,   72,   88, 0x175c, 0	/* frame 3 */
	.half	 -37,  -92,   84,   92, 0x1f38, 0	/* frame 4 */
	.half	 -28,  -91,   76,   88, 0x2630, 0	/* frame 5 */
	.half	 -23,  -90,   72,   84, 0x2d3c, 0	/* frame 6 */
	.globl	g_dogAnim7Script
g_dogAnim7Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 106: frame list g_dogAnim8Frames (7 frames) + script g_dogAnim8Script
 *   used by sprite set 6 (record 0x8006d224) animation 8; g_dogAnimations[4] (read at 0x80028ed4)
 */
	.globl	g_dogAnim8Frames
g_dogAnim8Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -31,  -94,   64,   88, 0x0000, 0	/* frame 0 */
	.half	 -39,  -92,   64,   92, 0x1600, 0	/* frame 1 */
	.half	 -42,  -91,   64,   88, 0x2d00, 0	/* frame 2 */
	.half	 -37,  -91,   68,   88, 0x4300, 0	/* frame 3 */
	.half	 -24,  -93,   72,   92, 0x5a60, 0	/* frame 4 */
	.half	 -24,  -92,   68,   92, 0x7440, 0	/* frame 5 */
	.half	 -29,  -91,   80,   88, 0x8cb0, 0	/* frame 6 */
	.globl	g_dogAnim8Script
g_dogAnim8Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8400, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = clear flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 107: frame list g_dogAnim9Frames (8 frames) + script g_dogAnim9Script
 *   used by sprite set 6 (record 0x8006d224) animation 9; g_dogAnimations[10] (read at 0x80028ed4)
 */
	.globl	g_dogAnim9Frames
g_dogAnim9Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -18,  -97,   64,   96, 0x0000, 0	/* frame 0 */
	.half	 -23, -105,   64,  104, 0x067c, 0	/* frame 1 */
	.half	 -19, -110,   52,  108, 0x0e08, 0	/* frame 2 */
	.half	 -19, -115,   56,  112, 0x1588, 0	/* frame 3 */
	.half	 -35, -120,   60,  120, 0x1d18, 0	/* frame 4 */
	.half	 -48, -110,   72,  112, 0x2444, 0	/* frame 5 */
	.half	 -27,  -90,   64,   92, 0x2b60, 0	/* frame 6 */
	.half	 -22,  -96,   60,   96, 0x3368, 0	/* frame 7 */
	.globl	g_dogAnim9Script
g_dogAnim9Script:
	.half	2, 8			/* 2 ticks per step, 8 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 0x8142, 5	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, TRK_SIGNAL(66), 5 */
	.half	6, 7, 0x8600	/* 6, 7, TRK_STOP */
					/* = set flag 1, frames 0..4, signal 66, frames 5..7, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 108: frame list g_dogAnim10Frames (6 frames) + script g_dogAnim10Script
 *   used by sprite set 6 (record 0x8006d224) animation 10; g_dogAnimations[11] (read at 0x80028ed4)
 */
	.globl	g_dogAnim10Frames
g_dogAnim10Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -18,  -97,   64,   96, 0x0000, 0	/* frame 0 */
	.half	 -19, -114,   52,  112, 0x06a0, 0	/* frame 1 */
	.half	 -22, -112,   64,  112, 0x0e90, 0	/* frame 2 */
	.half	 -28, -115,   72,  116, 0x16bc, 0	/* frame 3 */
	.half	 -48,  -95,   96,  100, 0x1f68, 0	/* frame 4 */
	.half	 -54,  -88,  112,   92, 0x2884, 0	/* frame 5 */
	.globl	g_dogAnim10Script
g_dogAnim10Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8300, 0, 1, 2, 3, 0x813f, 4, 5	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_SIGNAL(63), 4, 5 */
	.half	0x8600	/* TRK_STOP */
					/* = set flag 1, frames 0..3, signal 63, frames 4,5, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 109: frame list g_fingersAnim1Frames (4 frames) + script g_fingersAnim1Script
 *   used by sprite set 16 (record 0x8006d218) animation 1; g_huggerAnimations[11] (read at 0x80028dcc)
 */
	.globl	g_fingersAnim1Frames
g_fingersAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -58,  -57,   60,   92, 0x0000, 0	/* frame 0 */
	.half	 -50,  -55,   52,   92, 0x1590, 0	/* frame 1 */
	.half	 -53,  -53,   56,   88, 0x2840, 0	/* frame 2 */
	.half	 -62,  -54,   64,   88, 0x3b80, 0	/* frame 3 */
	.globl	g_fingersAnim1Script
g_fingersAnim1Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8400, 0, 1, 2, 3, 0x8200	/* TRK_FLAG1_OFF, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = clear flag 1, frames 0..3, loop forever */

/*
 * Animation 110: frame list g_fingersAnim0Frames (4 frames) + script g_fingersAnim0Script
 *   used by sprite set 16 (record 0x8006d218) animation 0; g_huggerAnimations[12] (read at 0x80028dcc)
 */
	.globl	g_fingersAnim0Frames
g_fingersAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	   0,  -54,   60,   88, 0x0000, 0	/* frame 0 */
	.half	   0,  -53,   64,   88, 0x14a0, 0	/* frame 1 */
	.half	   0,  -55,   60,   92, 0x2aa0, 0	/* frame 2 */
	.half	   0,  -59,   52,   96, 0x4030, 0	/* frame 3 */
	.globl	g_fingersAnim0Script
g_fingersAnim0Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8132, 0x8400, 0, 1, 2, 3, 0x8200	/* TRK_SIGNAL(50), TRK_FLAG1_OFF, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = signal 50, clear flag 1, frames 0..3, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 111: frame list g_queenAnim0Frames (4 frames) + script g_queenAnim0Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 0; g_queenAnimations[8] (read at 0x8002905c)
 */
	.globl	g_queenAnim0Frames
g_queenAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -58, -229,  148,  196, 0x0000, 0	/* frame 0 */
	.half	 -55, -203,  144,  172, 0x1a78, 0	/* frame 1 */
	.half	 -52, -199,  140,  176, 0x34fc, 0	/* frame 2 */
	.half	 -54, -199,  144,  180, 0x4fc4, 0	/* frame 3 */
	.globl	g_queenAnim0Script
g_queenAnim0Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8300, 0, 1, 2, 3, 0x8200	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_LOOP(0) */
					/* = set flag 1, frames 0..3, loop forever */

/*
 * Animation 112: frame list g_queenAnim1Frames (11 frames) + script g_queenAnim1Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 1; g_queenAnimations[9] (read at 0x8002905c)
 */
	.globl	g_queenAnim1Frames
g_queenAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -55, -194,  144,  176, 0x0000, 0	/* frame 0 */
	.half	 -46, -193,  124,  180, 0x1a8c, 0	/* frame 1 */
	.half	 -46, -182,  128,  176, 0x3208, 0	/* frame 2 */
	.half	 -46, -178,  128,  168, 0x4774, 0	/* frame 3 */
	.half	 -45, -176,  128,  164, 0x5bf0, 0	/* frame 4 */
	.half	 -48, -176,  132,  168, 0x70c4, 0	/* frame 5 */
	.half	 -48, -176,  132,  168, 0x70c4, 0	/* frame 6 */
	.half	 -48, -176,  132,  168, 0x70c4, 0	/* frame 7 */
	.half	 -45, -176,  128,  164, 0x5bf0, 0	/* frame 8 */
	.half	 -46, -178,  128,  168, 0x4774, 0	/* frame 9 */
	.half	 -46, -182,  128,  176, 0x3208, 0	/* frame 10 */
	.globl	g_queenAnim1Script
g_queenAnim1Script:
	.half	5, 11			/* 5 ticks per step, 11 frames */
	.half	0x8300, 0, 1, 0x814b, 2, 3, 4, 5	/* TRK_FLAG1_ON, 0, 1, TRK_SIGNAL(75), 2, 3, 4, 5 */
	.half	6, 7, 8, 9, 10, 0x8600	/* 6, 7, 8, 9, 10, TRK_STOP */
					/* = set flag 1, frames 0,1, signal 75, frames 2..10, stop */

/*
 * Animation 113: frame list g_queenAnim2Frames (10 frames) + script g_queenAnim2Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 2; g_queenAnimations[10] (read at 0x8002905c)
 */
	.globl	g_queenAnim2Frames
g_queenAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -56, -179,  132,  156, 0x0000, 0	/* frame 0 */
	.half	 -65, -179,  140,  156, 0x13b8, 0	/* frame 1 */
	.half	 -63, -178,  140,  152, 0x2840, 0	/* frame 2 */
	.half	 -53, -180,  132,  152, 0x3c84, 0	/* frame 3 */
	.half	 -56, -188,  140,  160, 0x4fe4, 0	/* frame 4 */
	.half	 -69, -182,  184,  156, 0x66ec, 0	/* frame 5 */
	.half	 -74, -181,  180,  160, 0x7e44, 0	/* frame 6 */
	.half	 -74, -180,  144,  172, 0x971c, 0	/* frame 7 */
	.half	 -64, -175,  144,  184, 0xb200, 0	/* frame 8 */
	.half	 -58, -165,  112,  176, 0xcfa4, 0	/* frame 9 */
	.globl	g_queenAnim2Script
g_queenAnim2Script:
	.half	4, 10			/* 4 ticks per step, 10 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	7, 8, 9, 0x8600	/* 7, 8, 9, TRK_STOP */
					/* = set flag 1, frames 0..9, stop */

/*
 * Animation 114: frame list g_queenAnim3Frames (6 frames) + script g_queenAnim3Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 3; g_queenAnimations[11] (read at 0x8002905c)
 */
	.globl	g_queenAnim3Frames
g_queenAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -56, -191,  148,  204, 0x0000, 0	/* frame 0 */
	.half	 -76, -198,  168,  212, 0x1a2c, 0	/* frame 1 */
	.half	 -90, -225,  212,  240, 0x3768, 0	/* frame 2 */
	.half	-121, -217,  252,  232, 0x5a58, 0	/* frame 3 */
	.half	-126, -183,  252,  200, 0x7f98, 0	/* frame 4 */
	.half	 -88, -197,  184,  212, 0xa458, 0	/* frame 5 */
	.globl	g_queenAnim3Script
g_queenAnim3Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8300, 0, 1, 2, 3, 0x814d, 4, 5	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_SIGNAL(77), 4, 5 */
	.half	0x8600	/* TRK_STOP */
					/* = set flag 1, frames 0..3, signal 77, frames 4,5, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 115: frame list g_queenAnim4Frames (11 frames) + script g_queenAnim4Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 4; g_queenAnimations[1] (read at 0x8002905c)
 *           g_queenAnimations[2] (read at 0x8002905c); g_queenAnimations[3] (read at 0x8002905c)
 *           g_queenAnimations[5] (read at 0x8002905c); g_queenAnimations[6] (read at 0x8002905c)
 *           g_queenAnimations[7] (read at 0x8002905c)
 */
	.globl	g_queenAnim4Frames
g_queenAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	-132, -183,  208,  192, 0x0000, 0	/* frame 0 */
	.half	-118, -183,  192,  192, 0x1e30, 0	/* frame 1 */
	.half	-104, -182,  180,  188, 0x3c38, 0	/* frame 2 */
	.half	 -89, -185,  160,  188, 0x5a08, 0	/* frame 3 */
	.half	 -64, -187,  128,  188, 0x78f8, 0	/* frame 4 */
	.half	 -66, -184,  124,  180, 0x97d4, 0	/* frame 5 */
	.half	 -76, -179,  132,  172, 0xb484, 0	/* frame 6 */
	.half	 -84, -176,  144,  176, 0xcfdc, 0	/* frame 7 */
	.half	 -85, -178,  148,  172, 0xebf0, 0	/* frame 8 */
	.half	-108, -183,  184,  176, 0x0a6c, 1	/* frame 9 */
	.half	-131, -183,  212,  176, 0x2a10, 1	/* frame 10 */
	.globl	g_queenAnim4Script
g_queenAnim4Script:
	.half	2, 11			/* 2 ticks per step, 11 frames */
	.half	0x8300, 0, 1, 0x814b, 2, 3, 4, 5	/* TRK_FLAG1_ON, 0, 1, TRK_SIGNAL(75), 2, 3, 4, 5 */
	.half	6, 7, 8, 9, 10, 0x8200	/* 6, 7, 8, 9, 10, TRK_LOOP(0) */
					/* = set flag 1, frames 0,1, signal 75, frames 2..10, loop forever */

/*
 * Animation 116: frame list g_queenAnim5Frames (11 frames) + script g_queenAnim5Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 5; g_queenAnimations[0] (read at 0x8002905c)
 *           g_queenAnimations[4] (read at 0x8002905c)
 */
	.globl	g_queenAnim5Frames
g_queenAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -68, -170,  128,  180, 0x0000, 0	/* frame 0 */
	.half	 -68, -166,  124,  176, 0x192c, 0	/* frame 1 */
	.half	 -63, -164,  120,  172, 0x315c, 0	/* frame 2 */
	.half	 -61, -164,  120,  168, 0x4994, 0	/* frame 3 */
	.half	 -65, -168,  120,  172, 0x6208, 0	/* frame 4 */
	.half	 -62, -169,  140,  172, 0x7c08, 0	/* frame 5 */
	.half	 -60, -170,  148,  176, 0x97dc, 0	/* frame 6 */
	.half	 -61, -169,  140,  180, 0xb2c4, 0	/* frame 7 */
	.half	 -63, -169,  128,  180, 0xcdb8, 0	/* frame 8 */
	.half	 -67, -171,  128,  176, 0xe7fc, 0	/* frame 9 */
	.half	 -71, -173,  136,  176, 0x021c, 1	/* frame 10 */
	.globl	g_queenAnim5Script
g_queenAnim5Script:
	.half	2, 11			/* 2 ticks per step, 11 frames */
	.half	0x8300, 0, 0x8150, 1, 2, 3, 4, 5	/* TRK_FLAG1_ON, 0, TRK_SIGNAL(80), 1, 2, 3, 4, 5 */
	.half	6, 7, 8, 9, 10, 0x8200	/* 6, 7, 8, 9, 10, TRK_LOOP(0) */
					/* = set flag 1, frame 0, signal 80, frames 1..10, loop forever */

/*
 * Animation 117: frame list g_queenAnim6Frames (6 frames) + script g_queenAnim6Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 6; g_queenAnimations[12] (read at 0x8002905c)
 */
	.globl	g_queenAnim6Frames
g_queenAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -67, -172,  168,  188, 0x0000, 0	/* frame 0 */
	.half	 -78, -178,  172,  192, 0x17b4, 0	/* frame 1 */
	.half	 -80, -192,  188,  208, 0x3104, 0	/* frame 2 */
	.half	-122, -210,  212,  224, 0x4ec0, 0	/* frame 3 */
	.half	 -82, -187,  156,  200, 0x7320, 0	/* frame 4 */
	.half	 -70, -168,  144,  184, 0x903c, 0	/* frame 5 */
	.globl	g_queenAnim6Script
g_queenAnim6Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8300, 0, 1, 2, 0x814e, 3, 4, 5	/* TRK_FLAG1_ON, 0, 1, 2, TRK_SIGNAL(78), 3, 4, 5 */
	.half	0x8600	/* TRK_STOP */
					/* = set flag 1, frames 0..2, signal 78, frames 3..5, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 118: frame list g_queenAnim7Frames (6 frames) + script g_queenAnim7Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 7; g_queenAnimations[15] (read at 0x8002905c)
 */
	.globl	g_queenAnim7Frames
g_queenAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -65, -168,  148,  148, 0x0000, 0	/* frame 0 */
	.half	 -86, -127,  148,  128, 0x1624, 0	/* frame 1 */
	.half	 -93, -126,  160,  120, 0x2b20, 0	/* frame 2 */
	.half	-123, -100,  236,  100, 0x402c, 0	/* frame 3 */
	.half	-118,  -76,  248,   80, 0x58ac, 0	/* frame 4 */
	.half	-127,  -71,  228,   76, 0x6fc4, 0	/* frame 5 */
	.globl	g_queenAnim7Script
g_queenAnim7Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8300, 0, 1, 2, 3, 0x814c, 4, 5	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_SIGNAL(76), 4, 5 */
	.half	0x8800	/* TRK_STOP4 */
					/* = set flag 1, frames 0..3, signal 76, frames 4,5, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 119: frame list g_queenAnim8Frames (6 frames) + script g_queenAnim8Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 8; g_queenAnimations[16] (read at 0x8002905c)
 */
	.globl	g_queenAnim8Frames
g_queenAnim8Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -55, -205,  136,  184, 0x0000, 0	/* frame 0 */
	.half	 -64, -191,  164,  172, 0x14e8, 0	/* frame 1 */
	.half	 -83, -198,  200,  180, 0x2c48, 0	/* frame 2 */
	.half	 -92, -204,  204,  188, 0x4788, 0	/* frame 3 */
	.half	-109, -202,  232,  188, 0x63f0, 0	/* frame 4 */
	.half	 -67, -189,  152,  176, 0x7dac, 0	/* frame 5 */
	.globl	g_queenAnim8Script
g_queenAnim8Script:
	.half	4, 6			/* 4 ticks per step, 6 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 0x8600	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, TRK_STOP */
					/* = set flag 1, frames 0..5, stop */

/*
 * Animation 120: frame list g_queenAnim9Frames (3 frames) + script g_queenAnim9Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 9; g_queenAnimations[17] (read at 0x8002905c)
 */
	.globl	g_queenAnim9Frames
g_queenAnim9Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -67, -175,  152,  184, 0x0000, 0	/* frame 0 */
	.half	 -72, -180,  164,  184, 0x1a84, 0	/* frame 1 */
	.half	 -76, -187,  172,  188, 0x34b4, 0	/* frame 2 */
	.globl	g_queenAnim9Script
g_queenAnim9Script:
	.half	4, 3			/* 4 ticks per step, 3 frames */
	.half	0x8300, 0, 0x814a, 1, 2, 0x8600	/* TRK_FLAG1_ON, 0, TRK_SIGNAL(74), 1, 2, TRK_STOP */
					/* = set flag 1, frame 0, signal 74, frames 1,2, stop */

/*
 * Animation 121: frame list g_queenAnim10Frames (4 frames) + script g_queenAnim10Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 10; g_queenAnimations[13] (read at 0x8002905c)
 */
	.globl	g_queenAnim10Frames
g_queenAnim10Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -50, -204,  128,  176, 0x0000, 0	/* frame 0 */
	.half	 -57, -210,  132,  184, 0x14d0, 0	/* frame 1 */
	.half	 -52, -215,  120,  188, 0x2708, 0	/* frame 2 */
	.half	 -63, -222,  140,  188, 0x38f8, 0	/* frame 3 */
	.globl	g_queenAnim10Script
g_queenAnim10Script:
	.half	8, 4			/* 8 ticks per step, 4 frames */
	.half	0x8300, 0, 1, 2, 3, 0x8600, 0x8900, 0	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP, TRK_FLAG8, 0 */
					/* = set flag 1, frames 0..3, stop, set flag 8, frame 0 */

/*
 * Animation 122: frame list g_queenAnim11Frames (7 frames) + script g_queenAnim11Script
 *   used by sprite set 18 (record 0x8006d2a8) animation 11; g_queenAnimations[14] (read at 0x8002905c)
 */
	.globl	g_queenAnim11Frames
g_queenAnim11Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -58, -176,  140,  192, 0x0000, 0	/* frame 0 */
	.half	 -73, -193,  148,  208, 0x1654, 0	/* frame 1 */
	.half	 -83, -220,  200,  236, 0x2ee0, 0	/* frame 2 */
	.half	 -90, -198,  196,  212, 0x4c40, 0	/* frame 3 */
	.half	 -84, -192,  208,  208, 0x6854, 0	/* frame 4 */
	.half	 -75, -190,  200,  204, 0x84d4, 0	/* frame 5 */
	.half	 -66, -186,  188,  200, 0xa1cc, 0	/* frame 6 */
	.globl	g_queenAnim11Script
g_queenAnim11Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 0x8151, 2, 3, 4, 5	/* TRK_FLAG1_ON, 0, 1, TRK_SIGNAL(81), 2, 3, 4, 5 */
	.half	6, 0x8600	/* 6, TRK_STOP */
					/* = set flag 1, frames 0,1, signal 81, frames 2..6, stop */

/*
 * Animation 123: frame list g_warriorCeilingAnim2Frames (7 frames) + script g_warriorCeilingAnim2Script
 *   used by sprite set 17 (record 0x8006d248) animation 2; g_alienCeilingAnimations[0] (read at 0x80028f98)
 *           g_alienCeilingAnimations[3] (read at 0x80028f98)
 */
	.globl	g_warriorCeilingAnim2Frames
g_warriorCeilingAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -33,   -2,   64,   76, 0x0000, 0	/* frame 0 */
	.half	 -35,   -4,   64,   80, 0x1300, 0	/* frame 1 */
	.half	 -34,   -3,   64,   84, 0x2700, 0	/* frame 2 */
	.half	 -32,   -1,   64,   64, 0x3c00, 0	/* frame 3 */
	.half	 -31,   -3,   64,   64, 0x4c00, 0	/* frame 4 */
	.half	 -35,    1,   68,   68, 0x5c00, 0	/* frame 5 */
	.half	 -33,   -4,   64,   84, 0x6e10, 0	/* frame 6 */
	.globl	g_warriorCeilingAnim2Script
g_warriorCeilingAnim2Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8400, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_OFF, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = clear flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 124: frame list g_warriorCeilingAnim3Frames (7 frames) + script g_warriorCeilingAnim3Script
 *   used by sprite set 17 (record 0x8006d248) animation 3; g_alienCeilingAnimations[1] (read at 0x80028f98)
 *           g_alienCeilingAnimations[2] (read at 0x80028f98); g_alienCeilingAnimations[4] (read at 0x80028f98)
 *           g_alienCeilingAnimations[5] (read at 0x80028f98); g_alienCeilingAnimations[6] (read at 0x80028f98)
 *           g_alienCeilingAnimations[7] (read at 0x80028f98)
 */
	.globl	g_warriorCeilingAnim3Frames
g_warriorCeilingAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -19,   -3,   60,   80, 0x0000, 0	/* frame 0 */
	.half	 -19,   -4,   52,   80, 0x045c, 0	/* frame 1 */
	.half	 -20,   -4,   56,   84, 0x0988, 0	/* frame 2 */
	.half	 -33,   -2,   80,   64, 0x0e74, 0	/* frame 3 */
	.half	 -30,   -2,   84,   64, 0x1364, 0	/* frame 4 */
	.half	 -29,    1,   80,   68, 0x1820, 0	/* frame 5 */
	.half	 -26,   -4,   72,   84, 0x1cc8, 0	/* frame 6 */
	.globl	g_warriorCeilingAnim3Script
g_warriorCeilingAnim3Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 125: frame list g_warriorCeilingAnim0Frames (5 frames) + script g_warriorCeilingAnim0Script
 *   used by sprite set 17 (record 0x8006d248) animation 0; g_alienCeilingAnimations[8] (read at 0x80028f98)
 */
	.globl	g_warriorCeilingAnim0Frames
g_warriorCeilingAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -62, -107,  116,   84, 0x0000, 0	/* frame 0 */
	.half	 -42, -121,   88,  124, 0x0828, 0	/* frame 1 */
	.half	 -29,  -91,   84,   92, 0x12e8, 0	/* frame 2 */
	.half	 -55,  -65,  120,   72, 0x1d40, 0	/* frame 3 */
	.half	 -58,  -34,  124,   40, 0x27dc, 0	/* frame 4 */
	.globl	g_warriorCeilingAnim0Script
g_warriorCeilingAnim0Script:
	.half	2, 5			/* 2 ticks per step, 5 frames */
	.half	0x8300, 0, 1, 2, 3, 0x8400, 4, 0x8800	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG1_OFF, 4, TRK_STOP4 */
					/* = set flag 1, frames 0..3, clear flag 1, frame 4, stop with flag 4 */

/*
 * Animation 126: frame list g_warriorCeilingAnim1Frames (3 frames) + script g_warriorCeilingAnim1Script
 *   used by sprite set 17 (record 0x8006d248) animation 1; g_alienCeilingAnimations[9] (read at 0x80028f98)
 */
	.globl	g_warriorCeilingAnim1Frames
g_warriorCeilingAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -19, -105,   64,  100, 0x0000, 0	/* frame 0 */
	.half	 -27,  -66,   76,   68, 0x06d8, 0	/* frame 1 */
	.half	 -24,  -70,   64,   68, 0x0cb0, 0	/* frame 2 */
	.globl	g_warriorCeilingAnim1Script
g_warriorCeilingAnim1Script:
	.half	2, 3			/* 2 ticks per step, 3 frames */
	.half	0x8300, 0, 1, 2, 0x8600	/* TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = set flag 1, frames 0..2, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 127: frame list g_warriorCeilingAnim4Frames (4 frames) + script g_warriorCeilingAnim4Script
 *   used by sprite set 17 (record 0x8006d248) animation 4; g_alienCeilingAnimations[10] (read at 0x80028f98)
 */
	.globl	g_warriorCeilingAnim4Frames
g_warriorCeilingAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -24,  -87,   68,   88, 0x0000, 0	/* frame 0 */
	.half	 -11,  -96,   64,   96, 0x0618, 0	/* frame 1 */
	.half	 -24, -119,   60,  116, 0x0cc4, 0	/* frame 2 */
	.half	 -28, -115,   80,  100, 0x14fc, 0	/* frame 3 */
	.globl	g_warriorCeilingAnim4Script
g_warriorCeilingAnim4Script:
	.half	4, 4			/* 4 ticks per step, 4 frames */
	.half	0x8300, 0, 1, 2, 3, 0x8600	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = set flag 1, frames 0..3, stop */

/*
 * Animation 128: frame list g_colonistAnim0Frames (1 frame) + script g_colonistAnim0Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 0; g_colonistAnimations[3] (read at 0x800290e0)
 */
	.globl	g_colonistAnim0Frames
g_colonistAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -18, -111,   36,   60, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim0Script
g_colonistAnim0Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 129: frame list g_colonistAnim1Frames (1 frame) + script g_colonistAnim1Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 1; g_colonistAnimations[5] (read at 0x800290e0)
 */
	.globl	g_colonistAnim1Frames
g_colonistAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -15, -111,   36,   60, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim1Script
g_colonistAnim1Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 130: frame list g_colonistAnim2Frames (1 frame) + script g_colonistAnim2Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 2; g_colonistAnimations[6] (read at 0x800290e0)
 */
	.globl	g_colonistAnim2Frames
g_colonistAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -14, -112,   32,   60, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim2Script
g_colonistAnim2Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 131: frame list g_colonistAnim3Frames (1 frame) + script g_colonistAnim3Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 3; g_colonistAnimations[0] (read at 0x800290e0)
 *           g_colonistAnimations[1] (read at 0x800290e0); g_colonistAnimations[4] (read at 0x800290e0)
 *           g_colonistAnimations[7] (read at 0x800290e0)
 */
	.globl	g_colonistAnim3Frames
g_colonistAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -22, -111,   44,   60, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim3Script
g_colonistAnim3Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 132: frame list g_colonistAnim4Frames (1 frame) + script g_colonistAnim4Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 4; g_colonistAnimations[2] (read at 0x800290e0)
 */
	.globl	g_colonistAnim4Frames
g_colonistAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -15, -112,   32,   60, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim4Script
g_colonistAnim4Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 133: frame list g_colonistAnim5Frames (1 frame) + script g_colonistAnim5Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 5; g_colonistAnimations[11] (read at 0x800290e0)
 */
	.globl	g_colonistAnim5Frames
g_colonistAnim5Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -20, -115,   36,   64, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim5Script
g_colonistAnim5Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 134: frame list g_colonistAnim6Frames (1 frame) + script g_colonistAnim6Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 6; g_colonistAnimations[13] (read at 0x800290e0)
 */
	.globl	g_colonistAnim6Frames
g_colonistAnim6Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -15, -115,   36,   64, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim6Script
g_colonistAnim6Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 135: frame list g_colonistAnim7Frames (1 frame) + script g_colonistAnim7Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 7; g_colonistAnimations[14] (read at 0x800290e0)
 */
	.globl	g_colonistAnim7Frames
g_colonistAnim7Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -10, -115,   28,   64, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim7Script
g_colonistAnim7Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 136: frame list g_colonistAnim8Frames (1 frame) + script g_colonistAnim8Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 8; g_colonistAnimations[10] (read at 0x800290e0)
 */
	.globl	g_colonistAnim8Frames
g_colonistAnim8Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -16, -115,   28,   64, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim8Script
g_colonistAnim8Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 137: frame list g_colonistAnim9Frames (1 frame) + script g_colonistAnim9Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 9; g_colonistAnimations[8] (read at 0x800290e0)
 *           g_colonistAnimations[9] (read at 0x800290e0); g_colonistAnimations[12] (read at 0x800290e0)
 *           g_colonistAnimations[15] (read at 0x800290e0)
 */
	.globl	g_colonistAnim9Frames
g_colonistAnim9Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -22, -115,   44,   64, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim9Script
g_colonistAnim9Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 138: frame list g_colonistAnim10Frames (2 frames) + script g_colonistAnim10Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 10; g_colonistAnimations[18] (read at 0x800290e0)
 */
	.globl	g_colonistAnim10Frames
g_colonistAnim10Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -15, -115,   28,   60, 0x0000, 0	/* frame 0 */
	.half	 -15, -115,   28,   60, 0x0000, 0	/* frame 1 */
	.globl	g_colonistAnim10Script
g_colonistAnim10Script:
	.half	4, 2			/* 4 ticks per step, 2 frames */
	.half	0x8300, 0, 1, 0x8600	/* TRK_FLAG1_ON, 0, 1, TRK_STOP */
					/* = set flag 1, frames 0,1, stop */

/*
 * Animation 139: frame list g_colonistAnim11Frames (2 frames) + script g_colonistAnim11Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 11; g_colonistAnimations[16] (read at 0x800290e0)
 *           g_colonistAnimations[17] (read at 0x800290e0); g_colonistAnimations[20] (read at 0x800290e0)
 *           g_colonistAnimations[23] (read at 0x800290e0)
 */
	.globl	g_colonistAnim11Frames
g_colonistAnim11Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21, -115,   40,   64, 0x0000, 0	/* frame 0 */
	.half	 -21, -115,   40,   64, 0x0000, 0	/* frame 1 */
	.globl	g_colonistAnim11Script
g_colonistAnim11Script:
	.half	4, 2			/* 4 ticks per step, 2 frames */
	.half	0x8300, 0, 1, 0x8600	/* TRK_FLAG1_ON, 0, 1, TRK_STOP */
					/* = set flag 1, frames 0,1, stop */

/*
 * Animation 140: frame list g_colonistAnim12Frames (2 frames) + script g_colonistAnim12Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 12; g_colonistAnimations[19] (read at 0x800290e0)
 */
	.globl	g_colonistAnim12Frames
g_colonistAnim12Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -19, -115,   32,   64, 0x0000, 0	/* frame 0 */
	.half	 -19, -115,   32,   64, 0x0000, 0	/* frame 1 */
	.globl	g_colonistAnim12Script
g_colonistAnim12Script:
	.half	4, 2			/* 4 ticks per step, 2 frames */
	.half	0x8300, 0, 1, 0x8600	/* TRK_FLAG1_ON, 0, 1, TRK_STOP */
					/* = set flag 1, frames 0,1, stop */

/*
 * Animation 141: frame list g_colonistAnim13Frames (2 frames) + script g_colonistAnim13Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 13; g_colonistAnimations[21] (read at 0x800290e0)
 */
	.globl	g_colonistAnim13Frames
g_colonistAnim13Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -15, -115,   32,   64, 0x0000, 0	/* frame 0 */
	.half	 -15, -115,   32,   64, 0x0000, 0	/* frame 1 */
	.globl	g_colonistAnim13Script
g_colonistAnim13Script:
	.half	4, 2			/* 4 ticks per step, 2 frames */
	.half	0x8300, 0, 1, 0x8600	/* TRK_FLAG1_ON, 0, 1, TRK_STOP */
					/* = set flag 1, frames 0,1, stop */

/*
 * Animation 142: frame list g_colonistAnim14Frames (2 frames) + script g_colonistAnim14Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 14; g_colonistAnimations[22] (read at 0x800290e0)
 */
	.globl	g_colonistAnim14Frames
g_colonistAnim14Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -11, -115,   28,   60, 0x0000, 0	/* frame 0 */
	.half	 -11, -115,   28,   60, 0x0000, 0	/* frame 1 */
	.globl	g_colonistAnim14Script
g_colonistAnim14Script:
	.half	4, 2			/* 4 ticks per step, 2 frames */
	.half	0x8300, 0, 1, 0x8600	/* TRK_FLAG1_ON, 0, 1, TRK_STOP */
					/* = set flag 1, frames 0,1, stop */

/*
 * Animation 143: frame list g_colonistAnim15Frames (4 frames) + script g_colonistAnim15Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 15; g_colonistAnimations[24] (read at 0x800290e0)
 */
	.globl	g_colonistAnim15Frames
g_colonistAnim15Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -22, -115,   44,   64, 0x0000, 0	/* frame 0 */
	.half	 -22, -115,   44,   64, 0x0590, 0	/* frame 1 */
	.half	 -22, -115,   44,   64, 0x0b20, 0	/* frame 2 */
	.half	 -22, -115,   44,   64, 0x10a8, 0	/* frame 3 */
	.globl	g_colonistAnim15Script
g_colonistAnim15Script:
	.half	6, 4			/* 6 ticks per step, 4 frames */
	.half	0x816e, 0x8300, 0, 1, 2, 3, 0x8600	/* TRK_SIGNAL(110), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = signal 110, set flag 1, frames 0..3, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 144: frame list g_colonistAnim16Frames (4 frames) + script g_colonistAnim16Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 16; g_colonistAnimations[25] (read at 0x800290e0)
 */
	.globl	g_colonistAnim16Frames
g_colonistAnim16Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -20, -115,   40,   64, 0x0000, 0	/* frame 0 */
	.half	 -19, -120,   40,   68, 0x04fc, 0	/* frame 1 */
	.half	 -19, -123,   40,   68, 0x09a0, 0	/* frame 2 */
	.half	 -19,  -99,   40,   44, 0x0dd8, 0	/* frame 3 */
	.globl	g_colonistAnim16Script
g_colonistAnim16Script:
	.half	2, 4			/* 2 ticks per step, 4 frames */
	.half	0x8300, 0, 1, 2, 3, 0x8800	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP4 */
					/* = set flag 1, frames 0..3, stop with flag 4 */

/*
 * Animation 145: frame list g_colonistAnim17Frames (7 frames) + script g_colonistAnim17Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 17; g_colonistAnimations[26] (read at 0x800290e0)
 */
	.globl	g_colonistAnim17Frames
g_colonistAnim17Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -21, -115,   40,   64, 0x0000, 0	/* frame 0 */
	.half	 -22, -115,   44,   64, 0x04c8, 0	/* frame 1 */
	.half	 -21, -115,   40,   64, 0x0000, 0	/* frame 2 */
	.half	 -21, -114,   40,   60, 0x0a54, 0	/* frame 3 */
	.half	 -21, -116,   44,   64, 0x0f1c, 0	/* frame 4 */
	.half	 -22, -111,   44,   60, 0x1430, 0	/* frame 5 */
	.half	 -22, -111,   44,   60, 0x1430, 0	/* frame 6 */
	.globl	g_colonistAnim17Script
g_colonistAnim17Script:
	.half	4, 7			/* 4 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 0x8900, 4, 5	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG8, 4, 5 */
	.half	6, 0x8600	/* 6, TRK_STOP */
					/* = set flag 1, frames 0..3, set flag 8, frames 4..6, stop */

/*
 * Animation 146: frame list g_colonistAnim18Frames (1 frame) + script g_colonistAnim18Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 18; g_colonistAnimations[29] (read at 0x800290e0)
 */
	.globl	g_colonistAnim18Frames
g_colonistAnim18Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -16,  -98,   24,   44, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim18Script
g_colonistAnim18Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 147: frame list g_colonistAnim19Frames (1 frame) + script g_colonistAnim19Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 19; g_colonistAnimations[27] (read at 0x800290e0)
 *           g_colonistAnimations[28] (read at 0x800290e0); g_colonistAnimations[31] (read at 0x800290e0)
 *           g_colonistAnimations[34] (read at 0x800290e0)
 */
	.globl	g_colonistAnim19Frames
g_colonistAnim19Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -19,  -99,   40,   44, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim19Script
g_colonistAnim19Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 148: frame list g_colonistAnim20Frames (1 frame) + script g_colonistAnim20Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 20; g_colonistAnimations[30] (read at 0x800290e0)
 */
	.globl	g_colonistAnim20Frames
g_colonistAnim20Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -20,  -97,   36,   44, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim20Script
g_colonistAnim20Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 149: frame list g_colonistAnim21Frames (1 frame) + script g_colonistAnim21Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 21; g_colonistAnimations[32] (read at 0x800290e0)
 */
	.globl	g_colonistAnim21Frames
g_colonistAnim21Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -15,  -97,   36,   44, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim21Script
g_colonistAnim21Script:
	.half	4, 1			/* 4 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 150: frame list g_colonistAnim22Frames (1 frame) + script g_colonistAnim22Script
 *   used by sprite set 20 (record 0x8006d2b4) animation 22; g_colonistAnimations[33] (read at 0x800290e0)
 */
	.globl	g_colonistAnim22Frames
g_colonistAnim22Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	  -8,  -98,   24,   44, 0x0000, 0	/* frame 0 */
	.globl	g_colonistAnim22Script
g_colonistAnim22Script:
	.half	0, 1			/* 0 ticks per step, 1 frame */
	.half	0x8300, 0, 0x8200	/* TRK_FLAG1_ON, 0, TRK_LOOP(0) */
					/* = set flag 1, frame 0, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 151: frame list g_dogCeilingAnim0Frames (7 frames) + script g_dogCeilingAnim0Script
 *   used by sprite set 19 (record 0x8006d230) animation 0; g_dogCeilingAnimations[0] (read at 0x80028f34)
 *           g_dogCeilingAnimations[3] (read at 0x80028f34)
 */
	.globl	g_dogCeilingAnim0Frames
g_dogCeilingAnim0Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -29,    1,   60,   60, 0x0000, 0	/* frame 0 */
	.half	 -30,   -2,   64,   56, 0x04b0, 0	/* frame 1 */
	.half	 -30,   -1,   64,   64, 0x09a0, 0	/* frame 2 */
	.half	 -29,    2,   60,   60, 0x0e30, 0	/* frame 3 */
	.half	 -29,    2,   60,   60, 0x12f0, 0	/* frame 4 */
	.half	 -31,    4,   60,   56, 0x17a4, 0	/* frame 5 */
	.half	 -32,    0,   60,   60, 0x1c60, 0	/* frame 6 */
	.globl	g_dogCeilingAnim0Script
g_dogCeilingAnim0Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 152: frame list g_dogCeilingAnim1Frames (7 frames) + script g_dogCeilingAnim1Script
 *   used by sprite set 19 (record 0x8006d230) animation 1; g_dogCeilingAnimations[1] (read at 0x80028f34)
 *           g_dogCeilingAnimations[2] (read at 0x80028f34); g_dogCeilingAnimations[4] (read at 0x80028f34)
 *           g_dogCeilingAnimations[5] (read at 0x80028f34); g_dogCeilingAnimations[6] (read at 0x80028f34)
 *           g_dogCeilingAnimations[7] (read at 0x80028f34)
 */
	.globl	g_dogCeilingAnim1Frames
g_dogCeilingAnim1Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -16,    3,   56,   56, 0x0000, 0	/* frame 0 */
	.half	 -15,   -2,   52,   56, 0x047c, 0	/* frame 1 */
	.half	 -15,   -1,   52,   64, 0x0874, 0	/* frame 2 */
	.half	 -30,    1,   80,   60, 0x0cb0, 0	/* frame 3 */
	.half	 -41,    1,   96,   60, 0x118c, 0	/* frame 4 */
	.half	 -52,    3,  108,   56, 0x1680, 0	/* frame 5 */
	.half	 -56,    0,  104,   60, 0x1b8c, 0	/* frame 6 */
	.globl	g_dogCeilingAnim1Script
g_dogCeilingAnim1Script:
	.half	2, 7			/* 2 ticks per step, 7 frames */
	.half	0x8300, 0, 1, 2, 3, 4, 5, 6	/* TRK_FLAG1_ON, 0, 1, 2, 3, 4, 5, 6 */
	.half	0x8200	/* TRK_LOOP(0) */
					/* = set flag 1, frames 0..6, loop forever */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 153: frame list g_dogCeilingAnim2Frames (3 frames) + script g_dogCeilingAnim2Script
 *   used by sprite set 19 (record 0x8006d230) animation 2; g_dogCeilingAnimations[8] (read at 0x80028f34)
 */
	.globl	g_dogCeilingAnim2Frames
g_dogCeilingAnim2Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -37, -121,   84,  116, 0x0000, 0	/* frame 0 */
	.half	 -27,  -97,   72,   96, 0x0858, 0	/* frame 1 */
	.half	 -24,  -87,   64,   88, 0x0e68, 0	/* frame 2 */
	.globl	g_dogCeilingAnim2Script
g_dogCeilingAnim2Script:
	.half	2, 3			/* 2 ticks per step, 3 frames */
	.half	0x8300, 0, 1, 2, 0x8600	/* TRK_FLAG1_ON, 0, 1, 2, TRK_STOP */
					/* = set flag 1, frames 0..2, stop */
	.half	0				/* pad to a word; never read: the token before it stops or loops */

/*
 * Animation 154: frame list g_dogCeilingAnim3Frames (4 frames) + script g_dogCeilingAnim3Script
 *   used by sprite set 19 (record 0x8006d230) animation 3; g_dogCeilingAnimations[9] (read at 0x80028f34)
 */
	.globl	g_dogCeilingAnim3Frames
g_dogCeilingAnim3Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -20,  -85,   76,   84, 0x0000, 0	/* frame 0 */
	.half	  -4,  -98,   72,   96, 0x06d4, 0	/* frame 1 */
	.half	 -10, -119,   56,  116, 0x0de0, 0	/* frame 2 */
	.half	 -32, -119,   72,  104, 0x14f0, 0	/* frame 3 */
	.globl	g_dogCeilingAnim3Script
g_dogCeilingAnim3Script:
	.half	2, 4			/* 2 ticks per step, 4 frames */
	.half	0x8300, 0, 1, 2, 3, 0x8600	/* TRK_FLAG1_ON, 0, 1, 2, 3, TRK_STOP */
					/* = set flag 1, frames 0..3, stop */

/*
 * Animation 155: frame list g_dogCeilingAnim4Frames (5 frames) + script g_dogCeilingAnim4Script
 *   used by sprite set 19 (record 0x8006d230) animation 4; g_dogCeilingAnimations[10] (read at 0x80028f34)
 */
	.globl	g_dogCeilingAnim4Frames
g_dogCeilingAnim4Frames:
	/*	   x,    y,    w,    h, offset (low, high) */
	.half	 -30,  -99,   92,   80, 0x0000, 0	/* frame 0 */
	.half	 -40, -116,   88,  124, 0x06c0, 0	/* frame 1 */
	.half	 -19,  -91,   68,   92, 0x10dc, 0	/* frame 2 */
	.half	 -26,  -58,   92,   60, 0x199c, 0	/* frame 3 */
	.half	 -43,  -29,  112,   32, 0x2114, 0	/* frame 4 */
	.globl	g_dogCeilingAnim4Script
g_dogCeilingAnim4Script:
	.half	2, 5			/* 2 ticks per step, 5 frames */
	.half	0x8141, 0x8300, 0, 1, 2, 3, 0x8400, 4	/* TRK_SIGNAL(65), TRK_FLAG1_ON, 0, 1, 2, 3, TRK_FLAG1_OFF, 4 */
	.half	0x8800	/* TRK_STOP4 */
					/* = signal 65, set flag 1, frames 0..3, clear flag 1, frame 4, stop with flag 4 */
	.half	0				/* pad to a word; never read: the token before it stops or loops */
