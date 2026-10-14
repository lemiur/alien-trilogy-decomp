/* save_password.c -- password and memory-card save module: progress is packed into a 20-byte
 * checksummed record, spelled as 5-bit codes through the password key table, and kept in four card
 * save slots */
#include <sys/types.h>
#include <LIMITS.H>
#include <STRINGS.H>
#include <LIBGTE.H>
#include <CAMERA.H>
#include <ITEMS.H>
#include <KERNEL.H>
#include <MEMCARD.H>
#include <HEAP.H>
#include <GAME.H>
#include <SAVE.H>
#include <MENU.H>
#include <LEVEL.H>

/* Bytes in the password record and the save buffer read from the card (SAVE_SLOT_COUNT slots). */
#define PASSWORD_RECORD_SIZE 20
#define SAVE_BUFFER_SIZE 0x200

/* passwordRecord_t.options: the difficulty in bits 0..1, the control method in bits 2..3 */
#define PASSWORD_OPTION_MASK 0x3
#define PASSWORD_CONTROL_SHIFT 2
/* passwordRecord_t.weapons: one bit per carried weapon, the selected weapon's code from bit 5.
 * names: from the ITEM_* bits Password_Encode tests for each */
#define PASSWORD_WEAPON_PISTOL 0x01
#define PASSWORD_WEAPON_SHOTGUN 0x02
#define PASSWORD_WEAPON_PULSE_RIFLE 0x04
#define PASSWORD_WEAPON_FLAMETHROWER 0x08
#define PASSWORD_WEAPON_SMART_GUN 0x10
#define PASSWORD_WEAPONS_ALL 0x1f
#define PASSWORD_WEAPON_CODE_SHIFT 5
/* passwordRecord_t.armour keeps the low 14 bits of Player.armour (name invented) */
#define PASSWORD_ARMOUR_MASK 0x3fff
/* passwordRecord_t.weapons bits 5..7: the selected weapon (names: as the PASSWORD_WEAPON_* bits) */
enum {
    PASSWORD_CODE_PISTOL,
    PASSWORD_CODE_SHOTGUN,
    PASSWORD_CODE_PULSE_RIFLE,
    PASSWORD_CODE_FLAMETHROWER,
    PASSWORD_CODE_SMART_GUN
};
/* The password spells each group of five record bytes (40 bits) as eight 5-bit letter codes;
 * the shifts and partial masks in Password_BytesToCodes and Password_CodesToBytes are the
 * bit positions of that regrouping. names invented */
#define PASSWORD_GROUP_BYTES 5
#define PASSWORD_GROUP_CODES 8
#define PASSWORD_CODE_MASK 0x1f
/* Letters in the password alphabet (one per 5-bit code). name invented */
#define PASSWORD_ALPHABET_SIZE 32
/* Password_Decode's code buffer and the code it holds for a letter not in the alphabet. names invented */
#define PASSWORD_DECODE_BUFFER_SIZE 28
#define PASSWORD_LETTER_INVALID 100
/* Password_SaveToCard's attempts at deleting and rewriting the save file. name invented */
#define SAVE_WRITE_ATTEMPTS 16
/* Password_SetFullRecord's stock. names invented */
#define FULL_RECORD_AMMO 10
#define FULL_RECORD_CLIPS 10
#define FULL_RECORD_FUEL 1000
#define FULL_RECORD_CHARGE_GRENADES 2
#define FULL_RECORD_GRENADES 100

/* Password_Decode compares exactly 32 alphabet bytes; this is not a C string. */
/* name: inferred from use: Password_Encode spells codes with it, Password_Decode looks letters up in it */
unsigned char     g_passwordAlphabet[PASSWORD_ALPHABET_SIZE] = { 'B', 'C', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N',
                                                                 'P', 'Q', 'R', 'S', 'T', 'V', 'W', 'X', 'Y', 'Z', '0',
                                                                 '1', '2', '3', '4', '5', '6', '7', '8', '9', '?' };
extern const char g_cardSaveName[];
extern int Password_ApplyRecord( passwordRecord_t *record );
extern passwordRecord_t g_passwordRecord;

/*
==================
Password_BytesToCodes

Eight 5-bit codes per five input bytes; the final group is zero-padded.
==================
*/
/* name: inferred from Password_Encode, which spells its codes through the password alphabet */
short Password_BytesToCodes( const unsigned char *packed, unsigned char *decoded, short byteCount )
{
    short roundedBytes;
    short outputCount;
    short index;
    int   emitted = 0;
    if ( byteCount % PASSWORD_GROUP_BYTES )
        roundedBytes = (short)( ( ( byteCount + PASSWORD_GROUP_BYTES - 1 ) / PASSWORD_GROUP_BYTES ) * PASSWORD_GROUP_BYTES );
    else
        roundedBytes = byteCount;
    outputCount = (short)( ( roundedBytes / PASSWORD_GROUP_BYTES ) * PASSWORD_GROUP_CODES );
    index = 0;
    if ( outputCount > 0 ) {
        do {
            unsigned char bytes[2];
            int           value = 0;
            bytes[0] = 0;
            bytes[1] = 0;
            if ( byteCount > 0 )
                bytes[0] = packed[0];
            if ( byteCount > 1 )
                bytes[1] = packed[1];
            switch ( index & ( PASSWORD_GROUP_CODES - 1 ) ) {
            case 0:
                value = bytes[0] >> 3;
                break;
            case 1:
                value = ( ( bytes[0] & 7 ) << 2 ) | ( bytes[1] >> 6 );
                --byteCount;
                ++packed;
                break;
            case 2:
                value = ( bytes[0] >> 1 ) & PASSWORD_CODE_MASK;
                break;
            case 3:
                value = ( ( bytes[0] & 1 ) << 4 ) | ( bytes[1] >> 4 );
                --byteCount;
                ++packed;
                break;
            case 4:
                value = ( ( bytes[0] & 15 ) << 1 ) | ( bytes[1] >> 7 );
                --byteCount;
                ++packed;
                break;
            case 5:
                value = ( bytes[0] >> 2 ) & PASSWORD_CODE_MASK;
                break;
            case 6:
                value = ( ( bytes[0] & 3 ) << 3 ) | ( bytes[1] >> 5 );
                --byteCount;
                ++packed;
                break;
            case 7:
                value = bytes[0] & PASSWORD_CODE_MASK;
                --byteCount;
                ++packed;
                break;
            }
            *decoded++ = (unsigned char)value;
            ++index;
            ++emitted;
        } while ( index < outputCount );
    }
    return (short)emitted;
}

/*
==================
Password_CodesToBytes

Inverse of Password_BytesToCodes: packs one 5-bit code per input byte into bytes.
==================
*/
/* name: inferred from Password_Decode, which unpacks the typed password with it */
void Password_CodesToBytes( const char *input, unsigned char *packed, short byteCount )
{
    const unsigned char *codes = (const unsigned char *)input;
    short                i;

    for ( i = 0; i < byteCount; i++ ) {
        unsigned char value = 0;
        switch ( i % PASSWORD_GROUP_BYTES ) {
        case 0:
            value = ( codes[0] << 3 ) | ( ( codes[1] >> 2 ) & 7 );
            codes += 1;
            break;
        case 1:
            value = ( ( codes[0] & 3 ) << 6 ) + ( ( codes[1] & PASSWORD_CODE_MASK ) << 1 ) | ( ( codes[2] >> 4 ) & 1 );
            codes += 2;
            break;
        case 2:
            value = ( ( codes[0] & 15 ) << 4 ) | ( ( codes[1] >> 1 ) & 15 );
            codes += 1;
            break;
        case 3:
            value = ( ( codes[0] & 1 ) << 7 ) + ( ( codes[1] & PASSWORD_CODE_MASK ) << 2 ) | ( ( codes[2] >> 3 ) & 3 );
            codes += 2;
            break;
        case 4:
            value = ( ( codes[0] & 7 ) << 5 ) | ( codes[1] & PASSWORD_CODE_MASK );
            codes += 2;
            break;
        }
        *packed++ = value;
    }
}

/*
==================
Password_Encode

Fills a record from the player's state and spells it as the password text.
==================
*/
/* name: inferred from its body, which fills a record from the player's state and spells the password */
void Password_Encode( passwordRecord_t *record, int level )
{
    unsigned char codes[PASSWORD_LENGTH + 1];
    unsigned char options;
    int           weapons;
    int           weaponCode;
    short         count;
    short         i;
    short         length;

    options = Menu_GetDifficulty() & PASSWORD_OPTION_MASK;
    options |= ( Menu_GetControlMethod() & PASSWORD_OPTION_MASK ) << PASSWORD_CONTROL_SHIFT;
    weapons = 0;
    if ( g_player.flags & PLAYER_HAS_PISTOL )
        weapons |= PASSWORD_WEAPON_PISTOL;
    if ( g_player.flags & PLAYER_HAS_SHOTGUN )
        weapons |= PASSWORD_WEAPON_SHOTGUN;
    if ( g_player.flags & PLAYER_HAS_PULSE_RIFLE )
        weapons |= PASSWORD_WEAPON_PULSE_RIFLE;
    if ( g_player.flags & PLAYER_HAS_FLAMETHROWER )
        weapons |= PASSWORD_WEAPON_FLAMETHROWER;
    if ( g_player.flags & PLAYER_HAS_SMART_GUN )
        weapons |= PASSWORD_WEAPON_SMART_GUN;
    switch ( g_player.weapon ) {
    default:
        weaponCode = PASSWORD_CODE_PISTOL;
        break;
    case WEAPON_PISTOL:
        weaponCode = PASSWORD_CODE_PISTOL;
        break;
    case WEAPON_SHOTGUN:
        weaponCode = PASSWORD_CODE_SHOTGUN;
        break;
    case WEAPON_PULSE_RIFLE:
        weaponCode = PASSWORD_CODE_PULSE_RIFLE;
        break;
    case WEAPON_FLAMETHROWER:
        weaponCode = PASSWORD_CODE_FLAMETHROWER;
        break;
    case WEAPON_SMART_GUN:
        weaponCode = PASSWORD_CODE_SMART_GUN;
        break;
    }
    record->health = g_player.health;
    record->armour = g_player.armour & PASSWORD_ARMOUR_MASK;
    record->smartGunAmmo = g_player.smartGunAmmo;
    record->pistolAmmo = g_player.pistolAmmo;
    record->options = options;
    record->shotgunAmmo = g_player.shotgunAmmo;
    record->chargeGrenades = g_player.chargeGrenades;
    record->pulseRifleAmmo = g_player.pulseRifleAmmo;
    record->flamethrowerFuel = g_player.flamethrowerFuel;
    record->pistolClips = g_player.pistolClips;
    record->pulseRifleClips = g_player.pulseRifleClips;
    record->level = level;
    weapons |= weaponCode << PASSWORD_WEAPON_CODE_SHIFT;
    record->weapons = weapons;
    record->pulseRifleGrenades = g_player.pulseRifleGrenades;
    record->smartGunClips = g_player.smartGunClips;
    record->checksum = 0;
    record->checksum = Card_ChecksumBytes( (unsigned char *)record, PASSWORD_RECORD_SIZE );
    count = Password_BytesToCodes( (unsigned char *)record, codes, PASSWORD_RECORD_SIZE );
    length = 0;
    for ( i = 0; i < count; i++ )
        g_passwordText[length++] = g_passwordAlphabet[codes[i]];
    g_passwordText[length] = 0;
}


/*
==================
Password_ClearWeaponAmmo
==================
*/
/* name: invented; empties one weapon's ammunition and clips */
static inline void Password_ClearWeaponAmmo( short *ammo, short *clips )
{
    *ammo = 0;
    *clips = 0;
}

/*
==================
Password_ApplyRecord
==================
*/
/* name: inferred from its body, which checks a password record and gives the player its weapons, ammunition and level */
int Password_ApplyRecord( passwordRecord_t *record )
{
    /* RULE-EXCEPTION: R17-dead-local, retail's 0x48 frame holds 25-32 unused bytes (sweep: 24 and 33 miss); unsigned char codes[PASSWORD_LENGTH] is the unit's password code buffer (Password_Encode's codes, Password_BytesToCodes' output); ruled out: Password_ClearWeaponAmmo (the only inline) has no locals, Card_ChecksumBytes/Weapon_PlayerGivePickup/Player_SelectWeapon are jal calls, passwordRecord_t (20 bytes) is too small for a record copy */
    unsigned char codes[PASSWORD_LENGTH];
    unsigned char checksum;
    unsigned char options;
    unsigned char weapons;
    int           bits;
    unsigned char code;
    int           weapon;

    checksum = record->checksum;
    record->checksum = 0;
    if ( checksum != Card_ChecksumBytes( (unsigned char *)record, PASSWORD_RECORD_SIZE ) )
        return 1;
    options = record->options;
    Menu_SetDifficulty( options & PASSWORD_OPTION_MASK );
    Menu_OptionsSetControlMethod( ( options >> PASSWORD_CONTROL_SHIFT ) & PASSWORD_OPTION_MASK );
    weapons = record->weapons;
    code = record->weapons >> PASSWORD_WEAPON_CODE_SHIFT;
    Password_ClearWeaponAmmo( &g_player.pistolAmmo, &g_player.pistolClips );
    DISABLE_FLAG( PLAYER_HAS_PISTOL | PLAYER_HAS_SHOTGUN | PLAYER_HAS_PULSE_RIFLE | PLAYER_HAS_FLAMETHROWER | PLAYER_HAS_SMART_GUN );
    bits = weapons & PASSWORD_WEAPONS_ALL;
    if ( weapons & PASSWORD_WEAPON_PISTOL )
        Weapon_PlayerGivePickup( PICKUP_PISTOL, record->pistolAmmo, record->pistolClips );
    if ( weapons & PASSWORD_WEAPON_SHOTGUN )
        Weapon_PlayerGivePickup( PICKUP_SHOTGUN, record->shotgunAmmo, 0 );
    if ( weapons & PASSWORD_WEAPON_PULSE_RIFLE ) {
        Weapon_PlayerGivePickup( PICKUP_PULSE_RIFLE, record->pulseRifleAmmo, record->pulseRifleClips );
        if ( record->pulseRifleAmmo == 0 && record->pulseRifleClips == 0 ) {
            Weapon_PlayerGivePickup( PICKUP_PULSE_RIFLE, 1, 0 );
            g_player.pulseRifleAmmo = 0;
        }
    }
    if ( bits & PASSWORD_WEAPON_FLAMETHROWER )
        Weapon_PlayerGivePickup( PICKUP_FLAMETHROWER, record->flamethrowerFuel, 0 );
    if ( bits & PASSWORD_WEAPON_SMART_GUN )
        Weapon_PlayerGivePickup( PICKUP_SMART_GUN, record->smartGunAmmo, record->smartGunClips );
    switch ( code ) {
    default:
        weapon = WEAPON_PISTOL;
        break;
    case PASSWORD_CODE_PISTOL:
        weapon = WEAPON_PISTOL;
        break;
    case PASSWORD_CODE_SHOTGUN:
        weapon = WEAPON_SHOTGUN;
        break;
    case PASSWORD_CODE_PULSE_RIFLE:
        weapon = WEAPON_PULSE_RIFLE;
        break;
    case PASSWORD_CODE_FLAMETHROWER:
        weapon = WEAPON_FLAMETHROWER;
        break;
    case PASSWORD_CODE_SMART_GUN:
        weapon = WEAPON_SMART_GUN;
        break;
    }
    Player_SelectWeapon( weapon );
    g_player.health = record->health;
    g_player.armour = record->armour & PASSWORD_ARMOUR_MASK;
    g_player.pulseRifleGrenades = record->pulseRifleGrenades;
    g_player.chargeGrenades = record->chargeGrenades;
    g_level = record->level;
    return 0;
}

/*
==================
Password_StoreLevel
==================
*/
/* name: inferred from its body, which encodes the player's state for level into g_passwordRecord and the password text */
void Password_StoreLevel( int level )
{
    Password_Encode( &g_passwordRecord, level );
}

/*
==================
Password_Restore
==================
*/
/* name: inferred from its body, which resets the weapon slots and applies g_passwordRecord */
void Password_Restore( void )
{
    Weapon_ResetSlots();
    Password_ApplyRecord( &g_passwordRecord );
}

/*
==================
Password_Decode

Parses the password text back into a record and applies it; 1 for a bad letter.
==================
*/
/* name: inferred from its body, which parses the password text back into a record and applies it */
short Password_Decode( void )
{
    passwordRecord_t output;
    char             indices[PASSWORD_DECODE_BUFFER_SIZE];
    short            i, j;
    for ( i = 0; (unsigned short)i < PASSWORD_DECODE_BUFFER_SIZE; ++i )
        indices[i] = 0;
    i = 0;
    while ( (unsigned int)i < strlen( g_passwordText ) ) {
        indices[i] = PASSWORD_LETTER_INVALID;
        for ( j = 0; (unsigned short)j < PASSWORD_ALPHABET_SIZE; ++j ) {
            if ( (unsigned char)g_passwordText[i] == g_passwordAlphabet[j] ) {
                indices[i] = (char)j;
                break;
            }
        }
        if ( indices[i] == PASSWORD_LETTER_INVALID )
            return 1;
        ++i;
    }
    Password_CodesToBytes( indices, (unsigned char *)&output, PASSWORD_RECORD_SIZE );
    return (short)Password_ApplyRecord( &output );
}

/*
==================
Password_StoreSaveSlot

Stores the player's progress in a slot, or in the oldest slot for -1.
==================
*/
/* name: inferred from its Password_Encode call into the slot's record */
int Password_StoreSaveSlot( short slot, saveSlot_t *slots )
{
    short i;

    if ( slot == SAVE_SLOT_AUTO ) {
        int   lowest = SHRT_MAX;
        short best = -1;

        for ( i = 0; i < SAVE_SLOT_COUNT; i++ ) {
            if ( slots[i].age <= lowest ) {
                best = i;
                lowest = slots[best].age;
            }
        }
        if ( best == -1 )
            return CARD_SLOT_USED;
        slots[best].age = 0;
        slot = best;
    }
    Password_Encode( &slots[slot].record, g_level );
    slots[slot].level = g_level;
    {
        saveSlot_t *entry = slots;
        for ( i = 0; i < SAVE_SLOT_COUNT; i++, entry++ )
            entry->age++;
    }
    return CARD_NO_ERROR;
}

/*
==================
Password_LoadSaveSlot

Applies a slot's record, or the newest used slot's for -1.
==================
*/
/* name: inferred from its Password_ApplyRecord call on the slot's record */
int Password_LoadSaveSlot( short id, saveSlot_t *entries )
{
    saveSlot_t *chosen;
    short       selected = id;
    if ( id == SAVE_SLOT_AUTO ) {
        int   smallest = SHRT_MAX;
        short i;
        id = -1;
        for ( i = 0; i < SAVE_SLOT_COUNT; ++i ) {
            int value = entries[i].age;
            if ( value > 0 && value <= smallest ) {
                id = i;
                smallest = entries[id].age;
            }
        }
        if ( id == -1 )
            return CARD_SLOT_USED;
        selected = id;
    }
    chosen = entries + selected;
    if ( chosen->age == 0 )
        return CARD_SLOT_FREE;
    Password_ApplyRecord( &chosen->record );
    return CARD_NO_ERROR;
}

/*
==================
Password_SaveToCard

Saves the player's progress into a slot of the card's save file (-2 rewrites the
file unchanged), creating the file when it cannot be opened.
==================
*/
/* name: inferred from its body, which saves progress into a slot of the card save file */
int Password_SaveToCard( short slot, short port )
{
    void       *buffer = Heap_Alloc( SAVE_BUFFER_SIZE );
    saveSlot_t *slots;
    int         result;
    short       attempts;
    short       i;

    if ( buffer == 0 )
        return CARD_NO_MEMORY;
    slots = buffer;
    do {
        result = Card_ReadFile( g_cardSaveName, buffer, port );
    } while ( result == CARD_NOT_READ_HEADER || ( result == CARD_NOT_READ_DATA || result == CARD_NOT_READ_DATA_HEADER ) );
    if ( result == CARD_NOT_OPEN ) {
        for ( i = 0; i < SAVE_SLOT_COUNT; i++ ) {
            slots[i].header = 0;
            slots[i].age = 0;
            slots[i].level = -1;
        }
    } else if ( result != CARD_NO_ERROR ) {
        Heap_Free( SAVE_BUFFER_SIZE );
        return result;
    }
    if ( slot != SAVE_SLOT_REWRITE )
        result = Password_StoreSaveSlot( slot, slots );
    if ( result == CARD_NO_ERROR ) {
        attempts = 0;
        do {
            Card_DeleteFile( g_cardSaveName, port );
            result = Card_WriteFile( g_cardSaveName, buffer, SAVE_BUFFER_SIZE, port );
            attempts++;
        } while ( attempts < SAVE_WRITE_ATTEMPTS
                  && ( result == CARD_NOT_WRITE_HEADER || ( result == CARD_NOT_WRITE_DATA || result == CARD_NOT_WRITE_DATA_HEADER )
                       || ( result == CARD_NOT_CREATE || result == CARD_NOT_OPEN ) ) );
    }
    Heap_Free( SAVE_BUFFER_SIZE );
    return result;
}

/*
==================
Password_LoadFromCard

Loads a slot (or the newest, for -1) from the card's save file.
==================
*/
/* name: inferred from its body, which loads a slot from the card save file */
int Password_LoadFromCard( short id, short port )
{
    void *buffer = Heap_Alloc( SAVE_BUFFER_SIZE );
    void *saved;
    int   result;
    if ( buffer == 0 )
        return CARD_NO_MEMORY;
    saved = buffer;
    do {
        result = Card_ReadFile( g_cardSaveName, buffer, port );
        if ( result == CARD_NOT_READ_HEADER )
            continue;
        if ( result == CARD_NOT_READ_DATA || result == CARD_NOT_READ_DATA_HEADER )
            continue;
        break;
    } while ( 1 );
    if ( result == CARD_NO_ERROR )
        result = Password_LoadSaveSlot( id, saved );
    Heap_Free( SAVE_BUFFER_SIZE );
    return result;
}

/*
==================
Password_SetFullRecord

Fills the current record with full health, every weapon and stocked ammunition
for a level, then applies it (Password_Restore).
==================
*/
/* name: inferred from those values */
void Password_SetFullRecord( short level )
{
    g_passwordRecord.health = PLAYER_HEALTH_MAX;
    g_passwordRecord.armour = 0;
    g_passwordRecord.pistolAmmo = FULL_RECORD_AMMO;
    g_passwordRecord.shotgunAmmo = FULL_RECORD_AMMO;
    g_passwordRecord.smartGunAmmo = FULL_RECORD_AMMO;
    g_passwordRecord.pulseRifleAmmo = FULL_RECORD_AMMO;
    g_passwordRecord.flamethrowerFuel = FULL_RECORD_FUEL;
    g_passwordRecord.chargeGrenades = FULL_RECORD_CHARGE_GRENADES;
    g_passwordRecord.pulseRifleGrenades = FULL_RECORD_GRENADES;
    g_passwordRecord.pulseRifleClips = FULL_RECORD_CLIPS;
    g_passwordRecord.smartGunClips = FULL_RECORD_CLIPS;
    g_passwordRecord.pistolClips = FULL_RECORD_CLIPS;
    g_passwordRecord.level = (unsigned char)level;
    g_passwordRecord.weapons = PASSWORD_WEAPONS_ALL;
    g_passwordRecord.checksum = 0;
    g_passwordRecord.checksum = Card_ChecksumBytes( (unsigned char *)&g_passwordRecord, PASSWORD_RECORD_SIZE );
    Password_Restore();
}

/*
==================
Password_SaveSlotLevels

Reads the level saved in each slot of the card's save file (-1 when unread).
==================
*/
/* name: inferred from its body, which reads the level of each slot of the card save file */
int Password_SaveSlotLevels( short *levels, int port )
{
    short          i;
    int            result;
    saveSlot_t    *entry;
    unsigned char *buffer;
    short         *out;
    out = levels;
    i = 0;
    do {
        *out++ = -1;
        ++i;
    } while ( i < SAVE_SLOT_COUNT );
    buffer = Heap_Alloc( SAVE_BUFFER_SIZE );
    if ( buffer == 0 )
        return CARD_NO_MEMORY;
    entry = (saveSlot_t *)buffer;
    result = CARD_NO_ERROR;
    do {
        result = Card_ReadFile( g_cardSaveName, buffer, (short)port );
    } while ( result == CARD_NOT_READ_HEADER || ( result == CARD_NOT_READ_DATA || result == CARD_NOT_READ_DATA_HEADER ) );
    if ( result == CARD_NO_ERROR ) {
        i = 0;
        do {
            *levels++ = entry->level;
            ++entry;
            ++i;
        } while ( i < SAVE_SLOT_COUNT );
    }
    Heap_Free( SAVE_BUFFER_SIZE );
    return result;
}

/* The save file on the card (.rdata 0x80012a44, after this module's tables).
 * name: inferred from use: the save and load code open the card file by it. Its extent includes its alignment padding. */
const char g_cardSaveName[] = "BASLUS-00007Trilogy";
