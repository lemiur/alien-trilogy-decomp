/*
 * The HUD (src/hud.c): health and ammunition panels, the HUD font and the message lines.
 * Requires <sys/types.h>.
 */
#ifndef HUD_H
#define HUD_H

/* fills the HUD font's glyph width tables from its UVs (main.c, at level start) */
extern void font_init_widths(void);

/* draws value (0..999) as three HUD font digits at x, y, red when alert is set */
extern void hud_draw_digits(short x, short y, int value, short alert);

/* the ammunition panel and item indicators */
extern void hud_draw_ammo(void);

/* the usage bars of the debug display (g_cheat_flags & CHEAT_FLAG_DEBUG) */
extern void hud_draw_usage_bars(void);

/* the health panel */
extern void hud_draw_health(void);

/* draws one HUD font glyph; returns the next x */
extern short hud_draw_glyph(u_short ch, short x, short y, u_char *rgb);

/* draws one font glyph immediately over a drop shadow; returns the next x */
extern short draw_glyph_now(u_short ch, short x, short y, u_char *rgb, u_char *shadow);

/* shows a message line on the HUD */
extern void hud_show_message(const char *message);

/* clears every message line and the message task */
extern void hud_messages_reset(void);

#endif
