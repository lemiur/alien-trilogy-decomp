/* weapon_slots.c -- the player's weapon slots: selecting, owning and giving pickups */
#include <sys/types.h>
#include <LIBGTE.H>
#include <CAMERA.H>
#include <ITEMS.H>
#include <LEVEL.H>
#include <TYPES.H>
/* Module statics (.sbss 0x8008d1a8..0x8008d1a8), in their retail order. */
static s16          g_weaponSlot;
extern weaponSlot_t g_weaponSlots[WEAPON_COUNT];

/*
==================
SelectSlot
==================
*/
static inline void SelectSlot( s16 index )
{
    g_weaponSlot = index;
    switch ( g_weaponSlots[index].id ) {
    case WEAPON_PISTOL:
        Player_SelectWeapon( WEAPON_PISTOL );
        break;
    case WEAPON_SHOTGUN:
        Player_SelectWeapon( WEAPON_SHOTGUN );
        break;
    case WEAPON_PULSE_RIFLE:
        Player_SelectWeapon( WEAPON_PULSE_RIFLE );
        break;
    case WEAPON_FLAMETHROWER:
        Player_SelectWeapon( WEAPON_FLAMETHROWER );
        break;
    case WEAPON_SMART_GUN:
        Player_SelectWeapon( WEAPON_SMART_GUN );
        break;
    }
}

/*
==================
Weapon_ResetSlots
==================
*/
void Weapon_ResetSlots( void )
{
    int           i;
    weaponSlot_t *slot;
    g_weaponSlot = 0;
    slot = g_weaponSlots;
    i = 0;
    do {
        slot->id = i;
        slot->owned = 0;
        i++;
        slot++;
    } while ( i < WEAPON_COUNT );
}

/*
==================
Weapon_SyncSlot
==================
*/
void Weapon_SyncSlot( void )
{
    g_weaponSlot = g_player.weapon;
}

/*
==================
Weapon_SlotOwned

Whether weapon slot index is owned (Weapon_PlayerGivePickup).
==================
*/
static inline s16 Weapon_SlotOwned( s16 index )
{
    return g_weaponSlots[index].owned;
}

/*
==================
Weapon_PlayerGivePickup

Gives the player a pickup: kind selects the weapon, ammunition or item, amount and
extra are the rounds and clips it carries. A new weapon also marks its slot owned.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Weapon_PlayerGivePickup( short kind, short amount, short extra )
{
    switch ( kind ) {
    case PICKUP_PISTOL:
        if ( !( g_player.flags & PLAYER_HAS_PISTOL ) )
            g_player.flags |= PLAYER_HAS_PISTOL;
        g_player.pistolAmmo += amount;
        g_player.pistolClips += extra;
        if ( g_player.pistolAmmo > PISTOL_CLIP_ROUNDS )
            g_player.pistolAmmo = PISTOL_CLIP_ROUNDS;
        if ( g_player.pistolClips > CLIPS_MAX )
            g_player.pistolClips = CLIPS_MAX;
        g_weaponSlots[WEAPON_PISTOL].owned = -1;
        break;
    case PICKUP_SHOTGUN:
        g_player.flags |= PLAYER_HAS_SHOTGUN;
        g_player.shotgunAmmo += amount;
        if ( g_player.shotgunAmmo > SHOTGUN_AMMO_MAX )
            g_player.shotgunAmmo = SHOTGUN_AMMO_MAX;
        g_weaponSlots[WEAPON_SHOTGUN].owned = g_player.shotgunAmmo;
        break;
    case PICKUP_PULSE_RIFLE:
        g_player.flags |= PLAYER_HAS_PULSE_RIFLE;
        g_player.pulseRifleAmmo += amount;
        g_player.pulseRifleClips += extra;
        if ( g_player.pulseRifleAmmo > PULSE_RIFLE_CLIP_ROUNDS )
            g_player.pulseRifleAmmo = PULSE_RIFLE_CLIP_ROUNDS;
        if ( g_player.pulseRifleClips > CLIPS_MAX )
            g_player.pulseRifleClips = CLIPS_MAX;
        {
            weaponSlot_t *slot = &g_weaponSlots[WEAPON_PULSE_RIFLE];
            slot->owned = g_player.pulseRifleAmmo || g_player.pulseRifleGrenades;
        }
        break;
    case PICKUP_FLAMETHROWER:
        g_player.flags |= PLAYER_HAS_FLAMETHROWER;
        g_player.flamethrowerFuel += amount;
        if ( g_player.flamethrowerFuel > FLAMETHROWER_FUEL_MAX )
            g_player.flamethrowerFuel = FLAMETHROWER_FUEL_MAX;
        g_weaponSlots[WEAPON_FLAMETHROWER].owned = g_player.flamethrowerFuel;
        break;
    case PICKUP_SMART_GUN:
        g_player.flags |= PLAYER_HAS_SMART_GUN;
        g_player.smartGunAmmo += amount;
        g_player.smartGunClips += extra;
        if ( g_player.smartGunAmmo > SMART_GUN_CLIP_ROUNDS )
            g_player.smartGunAmmo = SMART_GUN_CLIP_ROUNDS;
        if ( g_player.smartGunClips > CLIPS_MAX )
            g_player.smartGunClips = CLIPS_MAX;
        g_weaponSlots[WEAPON_SMART_GUN].owned = g_player.smartGunAmmo;
        break;
    case PICKUP_SEISMIC_CHARGES:
        g_player.chargeGrenades += amount;
        break;
    case PICKUP_BATTERY:
        g_player.batteries++;
        break;
    case PICKUP_NIGHT_VISION:
        g_player.nightVisionCharges++;
        break;
    case PICKUP_SHOULDER_LAMP:
        g_player.lampCharges++;
        break;
    case PICKUP_PISTOL_CLIP:
        g_player.pistolAmmo += amount;
        g_player.pistolClips += extra;
        if ( g_player.pistolAmmo > PISTOL_CLIP_ROUNDS )
            g_player.pistolAmmo = PISTOL_CLIP_ROUNDS;
        if ( g_player.pistolClips > CLIPS_MAX )
            g_player.pistolClips = CLIPS_MAX;
        if ( !Weapon_SlotOwned( WEAPON_PISTOL ) && ( g_player.flags & PLAYER_HAS_PISTOL ) )
            g_weaponSlots[WEAPON_PISTOL].owned = -1;
        break;
    case PICKUP_SHOTGUN_CARTRIDGES:
        g_player.shotgunAmmo += amount;
        if ( g_player.shotgunAmmo > SHOTGUN_AMMO_MAX )
            g_player.shotgunAmmo = SHOTGUN_AMMO_MAX;
        if ( !Weapon_SlotOwned( WEAPON_SHOTGUN ) && ( g_player.flags & PLAYER_HAS_SHOTGUN ) )
            g_weaponSlots[WEAPON_SHOTGUN].owned = -1;
        break;
    case PICKUP_PULSE_RIFLE_CLIP:
        g_player.pulseRifleAmmo += amount;
        g_player.pulseRifleClips += extra;
        if ( g_player.pulseRifleAmmo > PULSE_RIFLE_CLIP_ROUNDS )
            g_player.pulseRifleAmmo = PULSE_RIFLE_CLIP_ROUNDS;
        if ( g_player.pulseRifleClips > CLIPS_MAX )
            g_player.pulseRifleClips = CLIPS_MAX;
        if ( !Weapon_SlotOwned( WEAPON_PULSE_RIFLE ) && ( g_player.flags & PLAYER_HAS_PULSE_RIFLE ) )
            g_weaponSlots[WEAPON_PULSE_RIFLE].owned = -1;
        break;
    case PICKUP_GRENADES:
        g_player.pulseRifleGrenades += amount;
        if ( g_player.pulseRifleGrenades > PULSE_RIFLE_GRENADES_MAX )
            g_player.pulseRifleGrenades = PULSE_RIFLE_GRENADES_MAX;
        if ( !Weapon_SlotOwned( WEAPON_PULSE_RIFLE ) && ( g_player.flags & PLAYER_HAS_PULSE_RIFLE ) )
            g_weaponSlots[WEAPON_PULSE_RIFLE].owned = -1;
        break;
    case PICKUP_FLAMETHROWER_FUEL:
        g_player.flamethrowerFuel += FLAMETHROWER_FUEL_PICKUP;
        if ( g_player.flamethrowerFuel > FLAMETHROWER_FUEL_MAX )
            g_player.flamethrowerFuel = FLAMETHROWER_FUEL_MAX;
        if ( !Weapon_SlotOwned( WEAPON_FLAMETHROWER ) && ( g_player.flags & PLAYER_HAS_FLAMETHROWER ) )
            g_weaponSlots[WEAPON_FLAMETHROWER].owned = -1;
        break;
    case PICKUP_SMART_GUN_AMMO:
        g_player.smartGunAmmo += amount;
        g_player.smartGunClips += extra;
        if ( g_player.smartGunAmmo > SMART_GUN_CLIP_ROUNDS )
            g_player.smartGunAmmo = SMART_GUN_CLIP_ROUNDS;
        if ( g_player.smartGunClips > CLIPS_MAX )
            g_player.smartGunClips = CLIPS_MAX;
        if ( !Weapon_SlotOwned( WEAPON_SMART_GUN ) && ( g_player.flags & PLAYER_HAS_SMART_GUN ) )
            g_weaponSlots[WEAPON_SMART_GUN].owned = -1;
        break;
    case PICKUP_AUTO_MAPPER:
        g_player.flags |= PLAYER_HAS_AUTO_MAPPER;
        break;
    case PICKUP_ACID_VEST:
    case PICKUP_BODY_SUIT:
        g_player.armour = amount;
        break;
    case PICKUP_PROTECTIVE_BOOTS:
        g_player.bootsTimer += amount * TICKS_PER_SECOND;
        break;
    case PICKUP_MEDIKIT:
        g_player.health += amount;
        if ( g_player.health > PLAYER_HEALTH_MAX )
            g_player.health = PLAYER_HEALTH_MAX;
        break;
    case PICKUP_HYPO_PACK:
        if ( g_player.health < PLAYER_HEALTH_MAX )
            g_player.health = PLAYER_HEALTH_MAX;
        g_player.invulnerableTimer = PLAYER_HYPO_INVULNERABLE_TIME;
        break;
    case PICKUP_ADRENALINE_BURST:
        g_player.health = PLAYER_HEALTH_ADRENALINE;
        break;
    case PICKUP_DERM_PATCH:
        g_player.health += amount;
        break;
    case PICKUP_IDENTITY_TAG:
        g_player.identityTags++;
        break;
    }
}

/*
==================
Weapon_SlotOtherOwned
==================
*/
int Weapon_SlotOtherOwned( void )
{
    s16 found = 0;
    s16 i;
    for ( i = g_weaponSlot + 1; i < WEAPON_COUNT; i++ ) {
        weaponSlot_t *slot = &g_weaponSlots[i];
        if ( slot->owned != 0 ) {
            found = -1;
            break;
        }
    }
    if ( found )
        return -1;
    if ( g_weaponSlot == 0 )
        return 0;
    for ( i = 0; i < g_weaponSlot; i++ ) {
        weaponSlot_t *slot = &g_weaponSlots[i];
        if ( slot->owned != 0 ) {
            found = -1;
            break;
        }
    }
    return found;
}

/*
==================
Weapon_SelectNext
==================
*/
s16 Weapon_SelectNext( void )
{
    s16 found = 0;
    s16 i;
    for ( i = g_weaponSlot + 1; i < WEAPON_COUNT; i++ ) {
        weaponSlot_t *slot = &g_weaponSlots[i];
        if ( slot->owned != 0 ) {
            found = -1;
            break;
        }
    }
    if ( found ) {
        SelectSlot( i );
        return -1;
    }
    if ( g_weaponSlot == 0 )
        return 0;
    for ( i = 0; i < g_weaponSlot; i++ ) {
        weaponSlot_t *slot = &g_weaponSlots[i];
        if ( slot->owned != 0 ) {
            found = -1;
            break;
        }
    }
    if ( found == 0 )
        return 0;
    SelectSlot( i );
    return -1;
}

/*
==================
Weapon_DiscardSelectNext
==================
*/
s16 Weapon_DiscardSelectNext( void )
{
    s16 found = 0;
    s16 i;
    for ( i = g_weaponSlot + 1; i < WEAPON_COUNT; i++ ) {
        weaponSlot_t *slot = &g_weaponSlots[i];
        if ( slot->owned != 0 ) {
            found = -1;
            break;
        }
    }
    if ( found ) {
        g_weaponSlots[g_weaponSlot].owned = 0;
        SelectSlot( i );
        return -1;
    }
    if ( g_weaponSlot == 0 )
        return 0;
    for ( i = 0; i < g_weaponSlot; i++ ) {
        weaponSlot_t *slot = &g_weaponSlots[i];
        if ( slot->owned != 0 ) {
            found = -1;
            break;
        }
    }
    if ( found == 0 )
        return 0;
    g_weaponSlots[g_weaponSlot].owned = 0;
    SelectSlot( i );
    return -1;
}

/*
==================
Weapon_ResetDefault
==================
*/
void Weapon_ResetDefault( void )
{
    int index = g_weaponSlot;
    g_weaponSlot = 0;
    g_weaponSlots[index].owned = 0;
    g_weaponSlots[WEAPON_PISTOL].owned = -1;
    Player_SelectWeapon( WEAPON_PISTOL );
}

/*
==================
Weapon_SelectSlot
==================
*/
/* name: inferred from use: the pause menu selects a weapon through it */
int Weapon_SelectSlot( s16 index )
{
    weaponSlot_t *slot = &g_weaponSlots[index];
    if ( slot->owned == 0 )
        return 0;
    g_weaponSlot = index;
    switch ( slot->id ) {
    case WEAPON_PISTOL:
        Player_SelectWeapon( WEAPON_PISTOL );
        break;
    case WEAPON_SHOTGUN:
        Player_SelectWeapon( WEAPON_SHOTGUN );
        break;
    case WEAPON_PULSE_RIFLE:
        Player_SelectWeapon( WEAPON_PULSE_RIFLE );
        break;
    case WEAPON_FLAMETHROWER:
        Player_SelectWeapon( WEAPON_FLAMETHROWER );
        break;
    case WEAPON_SMART_GUN:
        Player_SelectWeapon( WEAPON_SMART_GUN );
        break;
    }
    return -1;
}
