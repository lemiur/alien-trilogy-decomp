#include <sys/types.h>
#include <LIBGTE.H>
#include <CAMERA.H>
#include <ITEMS.H>
#include <LEVEL.H>
typedef signed short s16;
typedef struct { s16 id; s16 owned; } Slot;
/* Module statics (.sbss 0x8008d1a8..0x8008d1a8), in their retail order. */
static s16 g_weapon_slot;
extern Slot g_weapon_slots[WEAPON_COUNT];

static inline void select_slot(s16 index)
{
    g_weapon_slot = index;
    switch (g_weapon_slots[index].id) {
    case WEAPON_PISTOL:
        weapon_select(WEAPON_PISTOL);
        break;
    case WEAPON_SHOTGUN:
        weapon_select(WEAPON_SHOTGUN);
        break;
    case WEAPON_PULSE_RIFLE:
        weapon_select(WEAPON_PULSE_RIFLE);
        break;
    case WEAPON_FLAMETHROWER:
        weapon_select(WEAPON_FLAMETHROWER);
        break;
    case WEAPON_SMART_GUN:
        weapon_select(WEAPON_SMART_GUN);
        break;
    }
}

void weapon_slots_reset(void)
{
    int i;
    Slot *slot;
    g_weapon_slot = 0;
    slot = g_weapon_slots;
    i = 0;
    do {
        slot->id = i;
        slot->owned = 0;
        i++;
        slot++;
    } while (i < WEAPON_COUNT);
}

void weapon_slot_sync(void)
{
    g_weapon_slot = g_player.weapon;
}

/* Whether weapon slot index is owned (player_give_pickup). */
static inline s16 weapon_slot_owned(s16 index)
{
    return g_weapon_slots[index].owned;
}

/* Gives the player a pickup: kind selects the weapon, ammunition or item, amount and
 * extra are the rounds and clips it carries. A new weapon also marks its slot owned. */
/* name: runtime evidence, analysis/runtime/functions.json */
void player_give_pickup(short kind, short amount, short extra)
{
    switch (kind) {
    case PICKUP_PISTOL:
        if (!(g_player.flags & PLAYER_HAS_PISTOL))
            g_player.flags |= PLAYER_HAS_PISTOL;
        g_player.pistol_ammo += amount;
        g_player.pistol_clips += extra;
        if (g_player.pistol_ammo > PISTOL_CLIP_ROUNDS)
            g_player.pistol_ammo = PISTOL_CLIP_ROUNDS;
        if (g_player.pistol_clips > CLIPS_MAX)
            g_player.pistol_clips = CLIPS_MAX;
        g_weapon_slots[WEAPON_PISTOL].owned = -1;
        break;
    case PICKUP_SHOTGUN:
        g_player.flags |= PLAYER_HAS_SHOTGUN;
        g_player.shotgun_ammo += amount;
        if (g_player.shotgun_ammo > SHOTGUN_AMMO_MAX)
            g_player.shotgun_ammo = SHOTGUN_AMMO_MAX;
        g_weapon_slots[WEAPON_SHOTGUN].owned = g_player.shotgun_ammo;
        break;
    case PICKUP_PULSE_RIFLE:
        g_player.flags |= PLAYER_HAS_PULSE_RIFLE;
        g_player.pulse_rifle_ammo += amount;
        g_player.pulse_rifle_clips += extra;
        if (g_player.pulse_rifle_ammo > PULSE_RIFLE_CLIP_ROUNDS)
            g_player.pulse_rifle_ammo = PULSE_RIFLE_CLIP_ROUNDS;
        if (g_player.pulse_rifle_clips > CLIPS_MAX)
            g_player.pulse_rifle_clips = CLIPS_MAX;
        {
            Slot *slot = &g_weapon_slots[WEAPON_PULSE_RIFLE];
            slot->owned = g_player.pulse_rifle_ammo || g_player.pulse_rifle_grenades;
        }
        break;
    case PICKUP_FLAMETHROWER:
        g_player.flags |= PLAYER_HAS_FLAMETHROWER;
        g_player.flamethrower_fuel += amount;
        if (g_player.flamethrower_fuel > FLAMETHROWER_FUEL_MAX)
            g_player.flamethrower_fuel = FLAMETHROWER_FUEL_MAX;
        g_weapon_slots[WEAPON_FLAMETHROWER].owned = g_player.flamethrower_fuel;
        break;
    case PICKUP_SMART_GUN:
        g_player.flags |= PLAYER_HAS_SMART_GUN;
        g_player.smart_gun_ammo += amount;
        g_player.smart_gun_clips += extra;
        if (g_player.smart_gun_ammo > SMART_GUN_CLIP_ROUNDS)
            g_player.smart_gun_ammo = SMART_GUN_CLIP_ROUNDS;
        if (g_player.smart_gun_clips > CLIPS_MAX)
            g_player.smart_gun_clips = CLIPS_MAX;
        g_weapon_slots[WEAPON_SMART_GUN].owned = g_player.smart_gun_ammo;
        break;
    case PICKUP_SEISMIC_CHARGES:
        g_player.charge_grenades += amount;
        break;
    case PICKUP_BATTERY:
        g_player.batteries++;
        break;
    case PICKUP_NIGHT_VISION:
        g_player.night_vision_charges++;
        break;
    case PICKUP_SHOULDER_LAMP:
        g_player.lamp_charges++;
        break;
    case PICKUP_PISTOL_CLIP:
        g_player.pistol_ammo += amount;
        g_player.pistol_clips += extra;
        if (g_player.pistol_ammo > PISTOL_CLIP_ROUNDS)
            g_player.pistol_ammo = PISTOL_CLIP_ROUNDS;
        if (g_player.pistol_clips > CLIPS_MAX)
            g_player.pistol_clips = CLIPS_MAX;
        if (!weapon_slot_owned(WEAPON_PISTOL) && (g_player.flags & PLAYER_HAS_PISTOL))
            g_weapon_slots[WEAPON_PISTOL].owned = -1;
        break;
    case PICKUP_SHOTGUN_CARTRIDGES:
        g_player.shotgun_ammo += amount;
        if (g_player.shotgun_ammo > SHOTGUN_AMMO_MAX)
            g_player.shotgun_ammo = SHOTGUN_AMMO_MAX;
        if (!weapon_slot_owned(WEAPON_SHOTGUN) && (g_player.flags & PLAYER_HAS_SHOTGUN))
            g_weapon_slots[WEAPON_SHOTGUN].owned = -1;
        break;
    case PICKUP_PULSE_RIFLE_CLIP:
        g_player.pulse_rifle_ammo += amount;
        g_player.pulse_rifle_clips += extra;
        if (g_player.pulse_rifle_ammo > PULSE_RIFLE_CLIP_ROUNDS)
            g_player.pulse_rifle_ammo = PULSE_RIFLE_CLIP_ROUNDS;
        if (g_player.pulse_rifle_clips > CLIPS_MAX)
            g_player.pulse_rifle_clips = CLIPS_MAX;
        if (!weapon_slot_owned(WEAPON_PULSE_RIFLE) && (g_player.flags & PLAYER_HAS_PULSE_RIFLE))
            g_weapon_slots[WEAPON_PULSE_RIFLE].owned = -1;
        break;
    case PICKUP_GRENADES:
        g_player.pulse_rifle_grenades += amount;
        if (g_player.pulse_rifle_grenades > PULSE_RIFLE_GRENADES_MAX)
            g_player.pulse_rifle_grenades = PULSE_RIFLE_GRENADES_MAX;
        if (!weapon_slot_owned(WEAPON_PULSE_RIFLE) && (g_player.flags & PLAYER_HAS_PULSE_RIFLE))
            g_weapon_slots[WEAPON_PULSE_RIFLE].owned = -1;
        break;
    case PICKUP_FLAMETHROWER_FUEL:
        g_player.flamethrower_fuel += FLAMETHROWER_FUEL_PICKUP;
        if (g_player.flamethrower_fuel > FLAMETHROWER_FUEL_MAX)
            g_player.flamethrower_fuel = FLAMETHROWER_FUEL_MAX;
        if (!weapon_slot_owned(WEAPON_FLAMETHROWER) && (g_player.flags & PLAYER_HAS_FLAMETHROWER))
            g_weapon_slots[WEAPON_FLAMETHROWER].owned = -1;
        break;
    case PICKUP_SMART_GUN_AMMO:
        g_player.smart_gun_ammo += amount;
        g_player.smart_gun_clips += extra;
        if (g_player.smart_gun_ammo > SMART_GUN_CLIP_ROUNDS)
            g_player.smart_gun_ammo = SMART_GUN_CLIP_ROUNDS;
        if (g_player.smart_gun_clips > CLIPS_MAX)
            g_player.smart_gun_clips = CLIPS_MAX;
        if (!weapon_slot_owned(WEAPON_SMART_GUN) && (g_player.flags & PLAYER_HAS_SMART_GUN))
            g_weapon_slots[WEAPON_SMART_GUN].owned = -1;
        break;
    case PICKUP_AUTO_MAPPER:
        g_player.flags |= PLAYER_HAS_AUTO_MAPPER;
        break;
    case PICKUP_ACID_VEST:
    case PICKUP_BODY_SUIT:
        g_player.armour = amount;
        break;
    case PICKUP_PROTECTIVE_BOOTS:
        g_player.boots_timer += amount * TICKS_PER_SECOND;
        break;
    case PICKUP_MEDIKIT:
        g_player.health += amount;
        if (g_player.health > PLAYER_HEALTH_MAX)
            g_player.health = PLAYER_HEALTH_MAX;
        break;
    case PICKUP_HYPO_PACK:
        if (g_player.health < PLAYER_HEALTH_MAX)
            g_player.health = PLAYER_HEALTH_MAX;
        g_player.invulnerable_timer = PLAYER_HYPO_INVULNERABLE_TIME;
        break;
    case PICKUP_ADRENALINE_BURST:
        g_player.health = PLAYER_HEALTH_ADRENALINE;
        break;
    case PICKUP_DERM_PATCH:
        g_player.health += amount;
        break;
    case PICKUP_IDENTITY_TAG:
        g_player.identity_tags++;
        break;
    }
}

int weapon_slot_other_owned(void)
{
    s16 found = 0;
    s16 i;
    for (i = g_weapon_slot + 1; i < WEAPON_COUNT; i++) {
        Slot *slot = &g_weapon_slots[i];
        if (slot->owned != 0) {
            found = -1;
            break;
        }
    }
    if (found)
        return -1;
    if (g_weapon_slot == 0)
        return 0;
    for (i = 0; i < g_weapon_slot; i++) {
        Slot *slot = &g_weapon_slots[i];
        if (slot->owned != 0) {
            found = -1;
            break;
        }
    }
    return found;
}

s16 weapon_select_next(void)
{
    s16 found = 0;
    s16 i;
    for (i = g_weapon_slot + 1; i < WEAPON_COUNT; i++) {
        Slot *slot = &g_weapon_slots[i];
        if (slot->owned != 0) {
            found = -1;
            break;
        }
    }
    if (found) {
        select_slot(i);
        return -1;
    }
    if (g_weapon_slot == 0)
        return 0;
    for (i = 0; i < g_weapon_slot; i++) {
        Slot *slot = &g_weapon_slots[i];
        if (slot->owned != 0) {
            found = -1;
            break;
        }
    }
    if (found == 0)
        return 0;
    select_slot(i);
    return -1;
}

s16 weapon_discard_select_next(void)
{
    s16 found = 0;
    s16 i;
    for (i = g_weapon_slot + 1; i < WEAPON_COUNT; i++) {
        Slot *slot = &g_weapon_slots[i];
        if (slot->owned != 0) {
            found = -1;
            break;
        }
    }
    if (found) {
        g_weapon_slots[g_weapon_slot].owned = 0;
        select_slot(i);
        return -1;
    }
    if (g_weapon_slot == 0)
        return 0;
    for (i = 0; i < g_weapon_slot; i++) {
        Slot *slot = &g_weapon_slots[i];
        if (slot->owned != 0) {
            found = -1;
            break;
        }
    }
    if (found == 0)
        return 0;
    g_weapon_slots[g_weapon_slot].owned = 0;
    select_slot(i);
    return -1;
}

void weapon_reset_default(void)
{
    int index = g_weapon_slot;
    g_weapon_slot = 0;
    g_weapon_slots[index].owned = 0;
    g_weapon_slots[WEAPON_PISTOL].owned = -1;
    weapon_select(WEAPON_PISTOL);
}

/* name: inferred from use: the pause menu selects a weapon through it */
int weapon_select_slot(s16 index)
{
    Slot *slot = &g_weapon_slots[index];
    if (slot->owned == 0)
        return 0;
    g_weapon_slot = index;
    switch (slot->id) {
    case WEAPON_PISTOL:
        weapon_select(WEAPON_PISTOL);
        break;
    case WEAPON_SHOTGUN:
        weapon_select(WEAPON_SHOTGUN);
        break;
    case WEAPON_PULSE_RIFLE:
        weapon_select(WEAPON_PULSE_RIFLE);
        break;
    case WEAPON_FLAMETHROWER:
        weapon_select(WEAPON_FLAMETHROWER);
        break;
    case WEAPON_SMART_GUN:
        weapon_select(WEAPON_SMART_GUN);
        break;
    }
    return -1;
}
