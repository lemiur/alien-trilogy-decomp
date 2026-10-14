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

/* Card_PresentMask bits; the front end shows 1 as "Left", 2 as "Right" (names invented) */
#define CARD_PRESENT_PORT0 0x1
#define CARD_PRESENT_PORT1 0x2
#define CARD_PRESENT_BOTH ( CARD_PRESENT_PORT0 | CARD_PRESENT_PORT1 )

/* Save header parts, as the PS1 card file format lays them out: a 64-byte Shift-JIS title,
 * a 16-colour CLUT of 16-bit entries, 16x16 4-bit icon frames (at most three), and the
 * reserved bytes between the title and the CLUT. names invented */
#define CARD_TITLE_SIZE 64
#define CARD_CLUT_SIZE 32
#define CARD_ICON_SIZE 128
#define CARD_ICON_FRAMES_MAX 3
#define CARD_HEADER_RESERVED_SIZE 28

/* The 0x200-byte header of a save file: title, icon CLUT and up to three icon frames. */
typedef struct cardTitle_s {
    unsigned char text[CARD_TITLE_SIZE];
} cardTitle_t;
typedef struct cardClut_s {
    unsigned char colors[CARD_CLUT_SIZE];
} cardClut_t;
typedef struct cardIcon_s {
    unsigned char pixels[CARD_ICON_SIZE];
} cardIcon_t;
typedef struct cardHeader_s {
    unsigned char magic[2];
    unsigned char iconFlag;
    unsigned char blocks;
    cardTitle_t   title;
    unsigned char reserved[CARD_HEADER_RESERVED_SIZE];
    cardClut_t    clut;
    cardIcon_t    icons[CARD_ICON_FRAMES_MAX];
} cardHeader_t;

/* cardHeader_t.iconFlag: the PsyQ icon display flag, 0x11..0x13 for one to three icon frames (invented name) */
#define CARD_ICON_FLAG_ONE_FRAME 0x11
/* open() on a card: O_CREAT | (blocks << CARD_OPEN_BLOCKS_SHIFT) creates a file of that many 8 KB blocks (LIBAPI) */
#define CARD_OPEN_BLOCKS_SHIFT 16

/* Directory entries and save headers the front end reads from a card (Menu_StartMenuTasks,
 * Menu_ReadCardDirectory). name invented */
#define CARD_DIRECTORY_SIZE 16

/* The card device name buffer ("bu00:"). name invented */
#define CARD_PREFIX_SIZE 8
/* The checksum block after the header: its length word and one sum per data block. name invented */
#define CARD_CHECKSUM_HEADER_SIZE 0x100

/* A card device: its name prefix ("bu00:"), BIOS channel and port selector. */
typedef struct cardDevice_s {
    char  prefix[CARD_PREFIX_SIZE];
    int   channel;
    short selector;
    short reserved;
} cardDevice_t;
/* The checksum block after a save file's header: its length word and one sum per data block. */
typedef struct cardChecksums_s {
    int           length;
    unsigned char sums[CARD_CHECKSUM_HEADER_SIZE - sizeof( int )];
} cardChecksums_t;

extern void Card_Init( void );
extern short Card_FindFiles( struct DIRENTRY *entries, const char *pattern, short port, short *blocks );
extern short Card_CountFiles( struct DIRENTRY *entries, const char *pattern, int port );
extern void Card_Format( short port );
extern short Card_PresentMask( void );
extern int Card_ReadHeader( const char *name, cardHeader_t *header, short port );
extern unsigned char Card_ChecksumBytes( const unsigned char *bytes, int length );

/* Reads a checksummed card file into destination; port 0 or 1, or any ready card. */
extern int Card_ReadFile( const char *name, void *destination, short port );
/* Deletes a card file (name inferred from the LIBAPI delete call). */
extern void Card_DeleteFile( const char *name, int port );
/* name: inferred from the save code retrying it on CARD_NOT_WRITE_* results */
extern int Card_WriteFile( const char *name, void *source, int size, short port );

#endif
