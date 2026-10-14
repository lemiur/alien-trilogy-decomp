/*
 * Player weapons and pickups, the domain of Player.weapon, and the bits of Player.flags.
 * Names inferred from the pause menu (src/pause_menu.c): option labels "9mm Pistol",
 * "Shotgun", "Flamethrower", "Pulse Rifle", "Smart Gun" and "Auto Mapper" select or
 * test these values.
 */
#ifndef ITEMS_H
#define ITEMS_H

/* Player.weapon, also the argument of the pause menu's weapon-select call */
#define WEAPON_PISTOL 0
#define WEAPON_SHOTGUN 1
#define WEAPON_PULSE_RIFLE 2
#define WEAPON_SMART_GUN 3
#define WEAPON_FLAMETHROWER 4
#define WEAPON_COUNT 5 /* also the weapon slots (g_weaponSlots); slot i holds weapon i */

/* Ammunition limits (Weapon_PlayerGivePickup, Player_Reset). A pistol, pulse rifle or smart
 * gun clip holds *_CLIP_ROUNDS: the loaded rounds stop there, and the pause menu counts
 * ammo + clips x *_CLIP_ROUNDS. names: invented */
#define PISTOL_CLIP_ROUNDS 15
#define PULSE_RIFLE_CLIP_ROUNDS 100
#define SMART_GUN_CLIP_ROUNDS 100
#define SHOTGUN_AMMO_MAX 100
#define CLIPS_MAX 9 /* spare clips of each clip weapon */
#define PULSE_RIFLE_GRENADES_MAX 20
#define FLAMETHROWER_FUEL_MAX 500
#define FLAMETHROWER_FUEL_PICKUP 100 /* PICKUP_FLAMETHROWER_FUEL adds it, not the pickup's amount */

/* Player.flags: weapons and the auto mapper owned. PLAYER_HAS_PISTOL is inferred from the password, which stores
 * 0x04, 0x08, 0x10, 0x20, 0x40 as bits 0..4 in the order of its weapon codes
 * (pistol, shotgun, pulse rifle, flamethrower, smart gun). */
#define PLAYER_HAS_PISTOL 0x04
#define PLAYER_HAS_SHOTGUN 0x08
#define PLAYER_HAS_PULSE_RIFLE 0x10
#define PLAYER_HAS_FLAMETHROWER 0x20
#define PLAYER_HAS_SMART_GUN 0x40
#define PLAYER_HAS_AUTO_MAPPER 0x80

/* src/weapon_slots.c */
/* One weapon slot (g_weaponSlots[WEAPON_COUNT]): the weapon it holds and whether the player owns it. names: invented */
typedef struct weaponSlot_s {
    short id;
    short owned;
} weaponSlot_t;

extern void Weapon_ResetSlots( void );
extern void Weapon_SyncSlot( void );
/* -1 when a slot other than the current one holds an owned weapon, else 0 */
extern int Weapon_SlotOtherOwned( void );
extern short Weapon_SelectNext( void );
extern short Weapon_DiscardSelectNext( void );
extern void Weapon_ResetDefault( void );
extern int Weapon_SelectSlot( short index );

/* Player.flags control state, rewritten every frame by Player_Controls (runtime evidence). names: invented */
#define PLAYER_STRAFE 0x1
#define PLAYER_RUN 0x2
/* Player.flags: set while a face-hugger holds the player (Actor_CreatureTask with
 * Actor_StartHuggerOverlay; cleared by Actor_HuggerClingTask). name: invented */
#define PLAYER_HUGGER_ATTACHED 0x100
/* Player.flags: what the next weapon change does once the empty weapon is lowered
 * (Player_Controls). names: invented */
#define PLAYER_CHANGE_DISCARD 0x200 /* Weapon_DiscardSelectNext */
#define PLAYER_CHANGE_DEFAULT 0x400 /* Weapon_ResetDefault */
/* Player.flags: the footstep code (f_3982c) plays sample 0x26 instead of 0x2c while it is set;
 * nothing sets it, Player_Reset clears it. name: inferred (runtime evidence) */
#define PLAYER_FOOTSTEP_ALT 0x4000

/* clears bits (one PLAYER_* bit or an OR of several) in g_player.flags. name: invented */
#define DISABLE_FLAG( bits ) ( g_player.flags &= ~( bits ) )

#endif
