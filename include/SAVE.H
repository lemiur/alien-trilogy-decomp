/*
 * Passwords and card saves (src/save_password.c) as the front end and main use them.
 * Requires <sys/types.h>.
 */
#ifndef SAVE_H
#define SAVE_H

/* Letters in a password: the 20-byte record spelled as 5-bit codes. name invented */
#define PASSWORD_LENGTH 32
/* Password_SaveToCard and Password_LoadFromCard slot arguments: SAVE_SLOT_AUTO saves into the oldest slot or
 * loads the newest, SAVE_SLOT_REWRITE (save only) rewrites the file unchanged. names invented */
/* Save slots in the card's save file. */
#define SAVE_SLOT_COUNT 4
#define SAVE_SLOT_LAST ( SAVE_SLOT_COUNT - 1 )
#define SAVE_SLOT_AUTO ( -1 )
#define SAVE_SLOT_REWRITE ( -2 )

/* Field names inferred from Password_Encode, which copies them from the Player
 * members, and from the pause menu's use of those members. */
typedef struct passwordRecord_s {
    unsigned short health;
    unsigned short armour;
    unsigned char  smartGunAmmo; /* name: inferred from Player.smartGunAmmo, the pause menu's smart gun count */
    unsigned char  checksum;
    unsigned char  options; /* two 2-bit settings from Menu_GetDifficulty and Menu_GetControlMethod */
    unsigned char  pistolAmmo;
    unsigned char  shotgunAmmo;
    unsigned char  chargeGrenades; /* name: Player.chargeGrenades, which Password_Encode copies here */
    short          pulseRifleAmmo;
    short flamethrowerFuel;    /* name: inferred from Player.flamethrowerFuel, the pause menu's flamethrower count */
    unsigned char pistolClips; /* name: inferred from the pause menu's pistol count, ammo + 15 x Player.pistolClips */
    unsigned char pulseRifleClips; /* name: inferred from the pause menu's count, ammo + 100 x Player.pulseRifleClips */
    unsigned char level;
    unsigned char weapons;            /* PASSWORD_WEAPON_* bits 0..4, the selected weapon code in bits 5..7 */
    unsigned char pulseRifleGrenades; /* name: Player.pulseRifleGrenades, which Password_Encode copies here */
    unsigned char smartGunClips; /* name: inferred from the pause menu's count, ammo + 100 x Player.smartGunClips */
} passwordRecord_t;

/* One of the four save slots in the card's save file. */
typedef struct saveSlot_s {
    int header;
    int age; /* name: inferred from Password_StoreSaveSlot, which ages every slot per save and reuses the oldest */
    int level;
    passwordRecord_t record;
} saveSlot_t;

/* name: inferred from use: Password_Encode spells into it, the password screen edits it */
extern char g_passwordText[PASSWORD_LENGTH + 1];

extern void Password_StoreLevel( int level );
extern void Password_Restore( void );
extern short Password_Decode( void );
extern void Password_SetFullRecord( short level );
extern int Password_SaveToCard( short slot, short port );
extern int Password_LoadFromCard( short id, short port );
extern int Password_SaveSlotLevels( short *levels, int port );

#endif
