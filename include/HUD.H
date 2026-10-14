/*
 * The HUD (src/hud.c): health and ammunition panels, the HUD font and the message lines.
 * Requires <sys/types.h>.
 */
#ifndef HUD_H
#define HUD_H

/* fills the HUD font's glyph width tables from its UVs (main.c, at level start) */
extern void Hud_InitFontWidths( void );

/* draws value (0..999) as three HUD font digits at x, y, red when alert is set */
extern void Hud_DrawDigits( short x, short y, int value, short alert );

/* the ammunition panel and item indicators */
extern void Hud_DrawAmmo( void );

/* the usage bars of the debug display (g_cheatFlags & CHEAT_FLAG_DEBUG) */
extern void Hud_DrawUsageBars( void );

/* the health panel */
extern void Hud_DrawHealth( void );

/* draws value (clamped to 0..999) as three HUD font digits at x, y in rgb */
extern void Hud_DrawNumber( short x, short y, int value, u_char *rgb );

/* draws one HUD font glyph; returns the next x */
extern short Hud_DrawGlyph( u_short ch, short x, short y, u_char *rgb );

/* draws one font glyph immediately over a drop shadow; returns the next x */
extern short Hud_DrawGlyphNow( u_short ch, short x, short y, u_char *rgb, u_char *shadow );

/* shows a message line on the HUD */
extern void Hud_ShowMessage( const char *message );

/* clears every message line and the message task */
extern void Hud_ResetMessages( void );

/* One on-screen message line (g_messageLines, Hud_MessageLineActive): position, text length, the characters typed so far. names: invented */
typedef struct messageLine_s {
    short       x;
    short       y;
    short       length;
    short       progress;
    const char *text;
} messageLine_t;

#endif
