/* Password and memory-card save module: progress is packed into a 20-byte
 * checksummed record, spelled as 5-bit codes through the password key table,
 * and kept in four card save slots. */
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

/* PasswordRecord.options: the difficulty in bits 0..1, the control method in bits 2..3 */
#define PASSWORD_OPTION_MASK 0x3
#define PASSWORD_CONTROL_SHIFT 2
/* PasswordRecord.weapons: one bit per carried weapon, the selected weapon's code from bit 5.
 * names: from the ITEM_* bits password_encode tests for each */
#define PASSWORD_WEAPON_PISTOL 0x01
#define PASSWORD_WEAPON_SHOTGUN 0x02
#define PASSWORD_WEAPON_PULSE_RIFLE 0x04
#define PASSWORD_WEAPON_FLAMETHROWER 0x08
#define PASSWORD_WEAPON_SMART_GUN 0x10
#define PASSWORD_WEAPONS_ALL 0x1f
#define PASSWORD_WEAPON_CODE_SHIFT 5
/* PasswordRecord.armour keeps the low 14 bits of Player.armour (name invented) */
#define PASSWORD_ARMOUR_MASK 0x3fff
/* PasswordRecord.weapons bits 5..7: the selected weapon (names: as the PASSWORD_WEAPON_* bits) */
enum {
    PASSWORD_CODE_PISTOL,
    PASSWORD_CODE_SHOTGUN,
    PASSWORD_CODE_PULSE_RIFLE,
    PASSWORD_CODE_FLAMETHROWER,
    PASSWORD_CODE_SMART_GUN
};
/* The password spells each group of five record bytes (40 bits) as eight 5-bit letter codes;
 * the shifts and partial masks in password_bytes_to_codes and password_codes_to_bytes are the
 * bit positions of that regrouping. names invented */
#define PASSWORD_GROUP_BYTES 5
#define PASSWORD_GROUP_CODES 8
#define PASSWORD_CODE_MASK 0x1f
/* Letters in the password alphabet (one per 5-bit code). name invented */
#define PASSWORD_ALPHABET_SIZE 32
/* password_decode's code buffer and the code it holds for a letter not in the alphabet. names invented */
#define PASSWORD_DECODE_BUFFER_SIZE 28
#define PASSWORD_LETTER_INVALID 100
/* save_to_card's attempts at deleting and rewriting the save file. name invented */
#define SAVE_WRITE_ATTEMPTS 16
/* password_set_full_record's stock. names invented */
#define FULL_RECORD_AMMO 10
#define FULL_RECORD_CLIPS 10
#define FULL_RECORD_FUEL 1000
#define FULL_RECORD_CHARGE_GRENADES 2
#define FULL_RECORD_GRENADES 100

/* Field names inferred from password_encode, which copies them from the Player
 * members, and from the pause menu's use of those members. */
typedef struct PasswordRecord {
    unsigned short health;
    unsigned short armour;
    unsigned char smart_gun_ammo; /* name: inferred from Player.smart_gun_ammo, the pause menu's smart gun count */
    unsigned char checksum;
    unsigned char options; /* two 2-bit settings from get_difficulty and get_control_method */
    unsigned char pistol_ammo;
    unsigned char shotgun_ammo;
    unsigned char charge_grenades; /* name: Player.charge_grenades, which password_encode copies here */
    short pulse_rifle_ammo;
    short flamethrower_fuel; /* name: inferred from Player.flamethrower_fuel, the pause menu's flamethrower count */
    unsigned char pistol_clips; /* name: inferred from the pause menu's pistol count, ammo + 15 x Player.pistol_clips */
    unsigned char pulse_rifle_clips; /* name: inferred from the pause menu's count, ammo + 100 x Player.pulse_rifle_clips */
    unsigned char level;
    unsigned char weapons; /* PASSWORD_WEAPON_* bits 0..4, the selected weapon code in bits 5..7 */
    unsigned char pulse_rifle_grenades; /* name: Player.pulse_rifle_grenades, which password_encode copies here */
    unsigned char smart_gun_clips; /* name: inferred from the pause menu's count, ammo + 100 x Player.smart_gun_clips */
} PasswordRecord;

/* One of the four save slots in the card's save file. */
typedef struct SaveSlot {
    int header;
    int age; /* name: inferred from save_slot_store, which ages every slot per save and reuses the oldest */
    int level;
    PasswordRecord record;
} SaveSlot;

/* password_decode compares exactly 32 alphabet bytes; this is not a C string. */
/* name: inferred from use: password_encode spells codes with it, password_decode looks letters up in it */
unsigned char g_password_alphabet[PASSWORD_ALPHABET_SIZE] = {
    'B', 'C', 'D', 'F', 'G', 'H', 'J', 'K',
    'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T',
    'V', 'W', 'X', 'Y', 'Z', '0', '1', '2',
    '3', '4', '5', '6', '7', '8', '9', '?'
};
extern const char g_card_save_name[20];
extern int password_apply_record(PasswordRecord *record);
extern PasswordRecord g_password_record;

/* Eight 5-bit codes per five input bytes; the final group is zero-padded.
 * name: inferred from password_encode, which spells its codes through the password alphabet */
short password_bytes_to_codes(const unsigned char *packed, unsigned char *decoded, short byte_count)
{
    short rounded_bytes;
    short output_count;
    short index;
    int emitted = 0;
    if (byte_count % PASSWORD_GROUP_BYTES)
        rounded_bytes = (short)(((byte_count + PASSWORD_GROUP_BYTES - 1) / PASSWORD_GROUP_BYTES) * PASSWORD_GROUP_BYTES);
    else
        rounded_bytes = byte_count;
    output_count = (short)((rounded_bytes / PASSWORD_GROUP_BYTES) * PASSWORD_GROUP_CODES);
    index = 0;
    if (output_count > 0) {
      do {
        unsigned char bytes[2];
        int value = 0;
        bytes[0] = 0;
        bytes[1] = 0;
        if (byte_count > 0) bytes[0] = packed[0];
        if (byte_count > 1) bytes[1] = packed[1];
        switch (index & (PASSWORD_GROUP_CODES - 1)) {
        case 0: value = bytes[0] >> 3; break;
        case 1: value = ((bytes[0] & 7) << 2) | (bytes[1] >> 6); --byte_count; ++packed; break;
        case 2: value = (bytes[0] >> 1) & PASSWORD_CODE_MASK; break;
        case 3: value = ((bytes[0] & 1) << 4) | (bytes[1] >> 4); --byte_count; ++packed; break;
        case 4: value = ((bytes[0] & 15) << 1) | (bytes[1] >> 7); --byte_count; ++packed; break;
        case 5: value = (bytes[0] >> 2) & PASSWORD_CODE_MASK; break;
        case 6: value = ((bytes[0] & 3) << 3) | (bytes[1] >> 5); --byte_count; ++packed; break;
        case 7: value = bytes[0] & PASSWORD_CODE_MASK; --byte_count; ++packed; break;
        }
        *decoded++ = (unsigned char)value;
        ++index;
        ++emitted;
      } while (index < output_count);
    }
    return (short)emitted;
}

/* Inverse of password_bytes_to_codes: packs one 5-bit code per input byte into bytes.
 * name: inferred from password_decode, which unpacks the typed password with it */
void password_codes_to_bytes(const char *input, unsigned char *packed, short byte_count)
{
    const unsigned char *codes = (const unsigned char *)input;
    short i;

    for (i = 0; i < byte_count; i++) {
        unsigned char value = 0;
        switch (i % PASSWORD_GROUP_BYTES) {
        case 0:
            value = (codes[0] << 3) | ((codes[1] >> 2) & 7);
            codes += 1;
            break;
        case 1:
            value = ((codes[0] & 3) << 6) + ((codes[1] & PASSWORD_CODE_MASK) << 1) | ((codes[2] >> 4) & 1);
            codes += 2;
            break;
        case 2:
            value = ((codes[0] & 15) << 4) | ((codes[1] >> 1) & 15);
            codes += 1;
            break;
        case 3:
            value = ((codes[0] & 1) << 7) + ((codes[1] & PASSWORD_CODE_MASK) << 2) | ((codes[2] >> 3) & 3);
            codes += 2;
            break;
        case 4:
            value = ((codes[0] & 7) << 5) | (codes[1] & PASSWORD_CODE_MASK);
            codes += 2;
            break;
        }
        *packed++ = value;
    }
}

/* Fills a record from the player's state and spells it as the password text. */
/* name: inferred from its body, which fills a record from the player's state and spells the password */
void password_encode(PasswordRecord *record, int level)
{
    unsigned char codes[PASSWORD_LENGTH + 1];
    unsigned char options;
    int weapons;
    int weapon_code;
    short count;
    short i;
    short length;

    options = get_difficulty() & PASSWORD_OPTION_MASK;
    options |= (get_control_method() & PASSWORD_OPTION_MASK) << PASSWORD_CONTROL_SHIFT;
    weapons = 0;
    if (g_player.flags & PLAYER_HAS_PISTOL)
        weapons |= PASSWORD_WEAPON_PISTOL;
    if (g_player.flags & PLAYER_HAS_SHOTGUN)
        weapons |= PASSWORD_WEAPON_SHOTGUN;
    if (g_player.flags & PLAYER_HAS_PULSE_RIFLE)
        weapons |= PASSWORD_WEAPON_PULSE_RIFLE;
    if (g_player.flags & PLAYER_HAS_FLAMETHROWER)
        weapons |= PASSWORD_WEAPON_FLAMETHROWER;
    if (g_player.flags & PLAYER_HAS_SMART_GUN)
        weapons |= PASSWORD_WEAPON_SMART_GUN;
    switch (g_player.weapon) {
    default: weapon_code = PASSWORD_CODE_PISTOL; break;
    case WEAPON_PISTOL: weapon_code = PASSWORD_CODE_PISTOL; break;
    case WEAPON_SHOTGUN: weapon_code = PASSWORD_CODE_SHOTGUN; break;
    case WEAPON_PULSE_RIFLE: weapon_code = PASSWORD_CODE_PULSE_RIFLE; break;
    case WEAPON_FLAMETHROWER: weapon_code = PASSWORD_CODE_FLAMETHROWER; break;
    case WEAPON_SMART_GUN: weapon_code = PASSWORD_CODE_SMART_GUN; break;
    }
    record->health = g_player.health;
    record->armour = g_player.armour & PASSWORD_ARMOUR_MASK;
    record->smart_gun_ammo = g_player.smart_gun_ammo;
    record->pistol_ammo = g_player.pistol_ammo;
    record->options = options;
    record->shotgun_ammo = g_player.shotgun_ammo;
    record->charge_grenades = g_player.charge_grenades;
    record->pulse_rifle_ammo = g_player.pulse_rifle_ammo;
    record->flamethrower_fuel = g_player.flamethrower_fuel;
    record->pistol_clips = g_player.pistol_clips;
    record->pulse_rifle_clips = g_player.pulse_rifle_clips;
    record->level = level;
    weapons |= weapon_code << PASSWORD_WEAPON_CODE_SHIFT;
    record->weapons = weapons;
    record->pulse_rifle_grenades = g_player.pulse_rifle_grenades;
    record->smart_gun_clips = g_player.smart_gun_clips;
    record->checksum = 0;
    record->checksum = checksum_bytes((unsigned char *)record, PASSWORD_RECORD_SIZE);
    count = password_bytes_to_codes((unsigned char *)record, codes, PASSWORD_RECORD_SIZE);
    length = 0;
    for (i = 0; i < count; i++)
        g_password_text[length++] = g_password_alphabet[codes[i]];
    g_password_text[length] = 0;
}



/* name: invented; empties one weapon's ammunition and clips */
static inline void weapon_ammo_clear(short *ammo, short *clips)
{
    *ammo = 0;
    *clips = 0;
}

/* name: inferred from its body, which checks a password record and gives the player its weapons, ammunition and level */
int password_apply_record(PasswordRecord *record)
{
    unsigned char codes[PASSWORD_LENGTH]; /* unused; reserves retail's 32 bytes of frame (config/exceptions.json) */
    unsigned char checksum;
    unsigned char options;
    unsigned char weapons;
    int bits;
    unsigned char code;
    int weapon;

    checksum = record->checksum;
    record->checksum = 0;
    if (checksum != checksum_bytes((unsigned char *)record, PASSWORD_RECORD_SIZE))
        return 1;
    options = record->options;
    set_difficulty(options & PASSWORD_OPTION_MASK);
    options_set_control_method((options >> PASSWORD_CONTROL_SHIFT) & PASSWORD_OPTION_MASK);
    weapons = record->weapons;
    code = record->weapons >> PASSWORD_WEAPON_CODE_SHIFT;
    weapon_ammo_clear(&g_player.pistol_ammo, &g_player.pistol_clips);
    DISABLE_FLAG(PLAYER_HAS_PISTOL | PLAYER_HAS_SHOTGUN | PLAYER_HAS_PULSE_RIFLE | PLAYER_HAS_FLAMETHROWER | PLAYER_HAS_SMART_GUN);
    bits = weapons & PASSWORD_WEAPONS_ALL;
    if (weapons & PASSWORD_WEAPON_PISTOL)
        player_give_pickup(PICKUP_PISTOL, record->pistol_ammo, record->pistol_clips);
    if (weapons & PASSWORD_WEAPON_SHOTGUN)
        player_give_pickup(PICKUP_SHOTGUN, record->shotgun_ammo, 0);
    if (weapons & PASSWORD_WEAPON_PULSE_RIFLE) {
        player_give_pickup(PICKUP_PULSE_RIFLE, record->pulse_rifle_ammo, record->pulse_rifle_clips);
        if (record->pulse_rifle_ammo == 0 && record->pulse_rifle_clips == 0) {
            player_give_pickup(PICKUP_PULSE_RIFLE, 1, 0);
            g_player.pulse_rifle_ammo = 0;
        }
    }
    if (bits & PASSWORD_WEAPON_FLAMETHROWER)
        player_give_pickup(PICKUP_FLAMETHROWER, record->flamethrower_fuel, 0);
    if (bits & PASSWORD_WEAPON_SMART_GUN)
        player_give_pickup(PICKUP_SMART_GUN, record->smart_gun_ammo, record->smart_gun_clips);
    switch (code) {
    default: weapon = WEAPON_PISTOL; break;
    case PASSWORD_CODE_PISTOL: weapon = WEAPON_PISTOL; break;
    case PASSWORD_CODE_SHOTGUN: weapon = WEAPON_SHOTGUN; break;
    case PASSWORD_CODE_PULSE_RIFLE: weapon = WEAPON_PULSE_RIFLE; break;
    case PASSWORD_CODE_FLAMETHROWER: weapon = WEAPON_FLAMETHROWER; break;
    case PASSWORD_CODE_SMART_GUN: weapon = WEAPON_SMART_GUN; break;
    }
    weapon_select(weapon);
    g_player.health = record->health;
    g_player.armour = record->armour & PASSWORD_ARMOUR_MASK;
    g_player.pulse_rifle_grenades = record->pulse_rifle_grenades;
    g_player.charge_grenades = record->charge_grenades;
    g_level = record->level;
    return 0;
}

/* name: inferred from its body, which encodes the player's state for level into g_password_record and the password text */
void password_store_level(int level)
{
    password_encode(&g_password_record, level);
}

/* name: inferred from its body, which resets the weapon slots and applies g_password_record */
void password_restore(void)
{
    weapon_slots_reset();
    password_apply_record(&g_password_record);
}

/* Parses the password text back into a record and applies it; 1 for a bad letter. */
/* name: inferred from its body, which parses the password text back into a record and applies it */
short password_decode(void)
{
    PasswordRecord output;
    char indices[PASSWORD_DECODE_BUFFER_SIZE];
    short i, j;
    for (i = 0; (unsigned short)i < PASSWORD_DECODE_BUFFER_SIZE; ++i)
        indices[i] = 0;
    i = 0;
    while ((unsigned int)i < strlen(g_password_text)) {
        indices[i] = PASSWORD_LETTER_INVALID;
        for (j = 0; (unsigned short)j < PASSWORD_ALPHABET_SIZE; ++j) {
            if ((unsigned char)g_password_text[i] == g_password_alphabet[j]) {
                indices[i] = (char)j;
                break;
            }
        }
        if (indices[i] == PASSWORD_LETTER_INVALID)
            return 1;
        ++i;
    }
    password_codes_to_bytes(indices, (unsigned char *)&output, PASSWORD_RECORD_SIZE);
    return (short)password_apply_record(&output);
}

/* Stores the player's progress in a slot, or in the oldest slot for -1.
 * name: inferred from its password_encode call into the slot's record */
int save_slot_store(short slot, SaveSlot *slots)
{
    short i;

    if (slot == SAVE_SLOT_AUTO) {
        int lowest = SHRT_MAX;
        short best = -1;

        for (i = 0; i < SAVE_SLOT_COUNT; i++) {
            if (slots[i].age <= lowest) {
                best = i;
                lowest = slots[best].age;
            }
        }
        if (best == -1)
            return CARD_SLOT_USED;
        slots[best].age = 0;
        slot = best;
    }
    password_encode(&slots[slot].record, g_level);
    slots[slot].level = g_level;
    {
        SaveSlot *entry = slots;
        for (i = 0; i < SAVE_SLOT_COUNT; i++, entry++)
            entry->age++;
    }
    return CARD_NO_ERROR;
}

/* Applies a slot's record, or the newest used slot's for -1.
 * name: inferred from its password_apply_record call on the slot's record */
int save_slot_load(short id, SaveSlot *entries)
{
    SaveSlot *chosen;
    short selected = id;
    if (id == SAVE_SLOT_AUTO) {
        int smallest = SHRT_MAX;
        short i;
        id = -1;
        for (i = 0; i < SAVE_SLOT_COUNT; ++i) {
            int value = entries[i].age;
            if (value > 0 && value <= smallest) {
                id = i;
                smallest = entries[id].age;
            }
        }
        if (id == -1) return CARD_SLOT_USED;
        selected = id;
    }
    chosen = entries + selected;
    if (chosen->age == 0) return CARD_SLOT_FREE;
    password_apply_record(&chosen->record);
    return CARD_NO_ERROR;
}

/* Saves the player's progress into a slot of the card's save file (-2 rewrites the
 * file unchanged), creating the file when it cannot be opened. */
/* name: inferred from its body, which saves progress into a slot of the card save file */
int save_to_card(short slot, short port)
{
    void *buffer = heap_alloc(SAVE_BUFFER_SIZE);
    SaveSlot *slots;
    int result;
    short attempts;
    short i;

    if (buffer == 0)
        return CARD_NO_MEMORY;
    slots = buffer;
    do {
        result = card_read_file(g_card_save_name, buffer, port);
    } while (result == CARD_NOT_READ_HEADER
             || (result == CARD_NOT_READ_DATA || result == CARD_NOT_READ_DATA_HEADER));
    if (result == CARD_NOT_OPEN) {
        for (i = 0; i < SAVE_SLOT_COUNT; i++) {
            slots[i].header = 0;
            slots[i].age = 0;
            slots[i].level = -1;
        }
    } else if (result != CARD_NO_ERROR) {
        heap_free(SAVE_BUFFER_SIZE);
        return result;
    }
    if (slot != SAVE_SLOT_REWRITE)
        result = save_slot_store(slot, slots);
    if (result == CARD_NO_ERROR) {
        attempts = 0;
        do {
            card_delete_file(g_card_save_name, port);
            result = card_write_file(g_card_save_name, buffer, SAVE_BUFFER_SIZE, port);
            attempts++;
        } while (attempts < SAVE_WRITE_ATTEMPTS
                 && (result == CARD_NOT_WRITE_HEADER
                     || (result == CARD_NOT_WRITE_DATA || result == CARD_NOT_WRITE_DATA_HEADER)
                     || (result == CARD_NOT_CREATE || result == CARD_NOT_OPEN)));
    }
    heap_free(SAVE_BUFFER_SIZE);
    return result;
}

/* Loads a slot (or the newest, for -1) from the card's save file. */
/* name: inferred from its body, which loads a slot from the card save file */
int load_from_card(short id, short port)
{
    void *buffer = heap_alloc(SAVE_BUFFER_SIZE);
    void *saved;
    int result;
    if (buffer == 0) return CARD_NO_MEMORY;
    saved = buffer;
    do {
        result = card_read_file(g_card_save_name, buffer, port);
        if (result == CARD_NOT_READ_HEADER) continue;
        if (result == CARD_NOT_READ_DATA || result == CARD_NOT_READ_DATA_HEADER) continue;
        break;
    } while (1);
    if (result == CARD_NO_ERROR) result = save_slot_load(id, saved);
    heap_free(SAVE_BUFFER_SIZE);
    return result;
}

/* Fills the current record with full health, every weapon and stocked ammunition
 * for a level, then applies it (password_restore).
 * name: inferred from those values */
void password_set_full_record(short level)
{
    g_password_record.health = PLAYER_HEALTH_MAX;
    g_password_record.armour = 0;
    g_password_record.pistol_ammo = FULL_RECORD_AMMO;
    g_password_record.shotgun_ammo = FULL_RECORD_AMMO;
    g_password_record.smart_gun_ammo = FULL_RECORD_AMMO;
    g_password_record.pulse_rifle_ammo = FULL_RECORD_AMMO;
    g_password_record.flamethrower_fuel = FULL_RECORD_FUEL;
    g_password_record.charge_grenades = FULL_RECORD_CHARGE_GRENADES;
    g_password_record.pulse_rifle_grenades = FULL_RECORD_GRENADES;
    g_password_record.pulse_rifle_clips = FULL_RECORD_CLIPS;
    g_password_record.smart_gun_clips = FULL_RECORD_CLIPS;
    g_password_record.pistol_clips = FULL_RECORD_CLIPS;
    g_password_record.level = (unsigned char)level;
    g_password_record.weapons = PASSWORD_WEAPONS_ALL;
    g_password_record.checksum = 0;
    g_password_record.checksum = checksum_bytes((unsigned char *)&g_password_record, PASSWORD_RECORD_SIZE);
    password_restore();
}

/* Reads the level saved in each slot of the card's save file (-1 when unread). */
/* name: inferred from its body, which reads the level of each slot of the card save file */
int save_slot_levels(short *levels, int port)
{
    short i;
    int result;
    SaveSlot *entry;
    unsigned char *buffer;
    short *out;
    out = levels;
    i = 0;
    do {
        *out++ = -1;
        ++i;
    } while (i < SAVE_SLOT_COUNT);
    buffer = heap_alloc(SAVE_BUFFER_SIZE);
    if (buffer == 0)
        return CARD_NO_MEMORY;
    entry = (SaveSlot *)buffer;
    result = CARD_NO_ERROR;
    do {
        result = card_read_file(g_card_save_name, buffer, (short)port);
    } while (result == CARD_NOT_READ_HEADER
             || (result == CARD_NOT_READ_DATA || result == CARD_NOT_READ_DATA_HEADER));
    if (result == CARD_NO_ERROR) {
        i = 0;
        do {
            *levels++ = entry->level;
            ++entry;
            ++i;
        } while (i < SAVE_SLOT_COUNT);
    }
    heap_free(SAVE_BUFFER_SIZE);
    return result;
}

/* The save file on the card (.rdata 0x80012a44, after this module's tables).
 * name: inferred from use: the save and load code open the card file by it. Its extent includes its alignment padding. */
const char g_card_save_name[20] = "BASLUS-00007Trilogy";
