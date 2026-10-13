/*
 * Memory-card layer (src/memcard.c) as used by the save code (src/save_password.c)
 * and the front end. Requires <sys/types.h> and <KERNEL.H> (struct DIRENTRY).
 */
#ifndef MEMCARD_H
#define MEMCARD_H

/* Card results. Each value indexes the message table at 0x80073aa8, whose strings
 * ("NoError", "NoCardPresent", "NotCreate", "NotOpen", ... "NoMemory") give the names. */
enum {
    CARD_NO_ERROR,
    CARD_NO_CARD_PRESENT,
    CARD_NOT_CREATE,
    CARD_NOT_OPEN,
    CARD_FILE_EXISTS,
    CARD_FILE_NOT_EXISTS,
    CARD_NOT_READ_HEADER,
    CARD_NOT_READ_DATA,
    CARD_NOT_READ_DATA_HEADER,
    CARD_NOT_WRITE_HEADER,
    CARD_NOT_WRITE_DATA,
    CARD_NOT_WRITE_DATA_HEADER,
    CARD_CHECKSUM_ERROR,
    CARD_SLOT_FREE,
    CARD_SLOT_USED,
    CARD_NO_MEMORY
};

/* Card events, as the card event wait returns them. Each value indexes the message table at 0x80073ae8
 * ("Card ready", "Card fail", "No card", "New card"). */
enum {
    CARD_EVENT_READY,
    CARD_EVENT_FAIL,
    CARD_EVENT_NO_CARD,
    CARD_EVENT_NEW_CARD
};

/* card_present_mask bits; the front end shows 1 as "Left", 2 as "Right" (names invented) */
#define CARD_PRESENT_PORT0 0x1
#define CARD_PRESENT_PORT1 0x2
#define CARD_PRESENT_BOTH (CARD_PRESENT_PORT0 | CARD_PRESENT_PORT1)

/* The 0x200-byte header of a save file: title, icon CLUT and up to three icon frames. */
typedef struct CardTitle { unsigned char text[64]; } CardTitle;
typedef struct CardClut { unsigned char colors[32]; } CardClut;
typedef struct CardIcon { unsigned char pixels[128]; } CardIcon;
typedef struct CardHeader {
    unsigned char magic[2];
    unsigned char icon_flag;
    unsigned char blocks;
    CardTitle title;
    unsigned char reserved[28];
    CardClut clut;
    CardIcon icons[3];
} CardHeader;

/* CardHeader.icon_flag: the PsyQ icon display flag, 0x11..0x13 for one to three icon frames (invented name) */
#define CARD_ICON_FLAG_ONE_FRAME 0x11
/* open() on a card: O_CREAT | (blocks << CARD_OPEN_BLOCKS_SHIFT) creates a file of that many 8 KB blocks (LIBAPI) */
#define CARD_OPEN_BLOCKS_SHIFT 16

/* Directory entries and save headers the front end reads from a card (start_menu_tasks,
 * read_card_directory). name invented */
#define CARD_DIRECTORY_SIZE 16

extern void card_init(void);
extern short card_find_files(struct DIRENTRY *entries, const char *pattern, short port, short *blocks);
extern short card_count_files(struct DIRENTRY *entries, const char *pattern, int port);
extern void card_format(short port);
extern short card_present_mask(void);
extern int card_read_header(const char *name, CardHeader *header, short port);
extern unsigned char checksum_bytes(const unsigned char *bytes, int length);

/* Reads a checksummed card file into destination; port 0 or 1, or any ready card. */
extern int card_read_file(const char *name, void *destination, short port);
/* Deletes a card file (name inferred from the LIBAPI delete call). */
extern void card_delete_file(const char *name, int port);
/* name: inferred from the save code retrying it on CARD_NOT_WRITE_* results */
extern int card_write_file(const char *name, void *source, int size, int port);

#endif
