/*
 * Passwords and card saves (src/save_password.c) as the front end and main use them.
 * Requires <sys/types.h>.
 */
#ifndef SAVE_H
#define SAVE_H

/* Letters in a password: the 20-byte record spelled as 5-bit codes. name invented */
#define PASSWORD_LENGTH 32
/* save_to_card and load_from_card slot arguments: SAVE_SLOT_AUTO saves into the oldest slot or
 * loads the newest, SAVE_SLOT_REWRITE (save only) rewrites the file unchanged. names invented */
/* Save slots in the card's save file. */
#define SAVE_SLOT_COUNT 4
#define SAVE_SLOT_AUTO (-1)
#define SAVE_SLOT_REWRITE (-2)

/* name: inferred from use: password_encode spells into it, the password screen edits it */
extern char g_password_text[PASSWORD_LENGTH + 1];

extern void password_store_level(int level);
extern void password_restore(void);
extern short password_decode(void);
extern void password_set_full_record(short level);
extern int save_to_card(short slot, short port);
extern int load_from_card(short id, short port);
extern int save_slot_levels(short *levels, int port);

#endif
