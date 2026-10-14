/* memcard.c -- memory-card layer: BIOS card events, the two card devices, directory listing, and
 * checksummed save files (a 0x200-byte header, a 0x100-byte checksum block, then the data) */
#include <sys/types.h>
#include <sys/file.h>
#include <KERNEL.H>
#include <LIBAPI.H>
#include <STRINGS.H>
#include <STDIO.H>
#include <MEMCARD.H>
#include <HEAP.H>

/* name: inferred from LIBAPI's 8 KB card block (firstfile sizes are rounded up to it) */
#define CARD_BLOCK_SIZE 0x2000
/* name: inferred from the card path buffers ("bu00:" plus a file name) */
#define CARD_PATH_SIZE 128
/* name: inferred from Card_BlockChecksums, which sums the data in blocks of this many bytes */
#define CARD_CHECKSUM_BLOCK_SIZE 0x400
/* BIOS card channels (_card_info): port << 4 | card. names invented */
#define CARD_CHANNEL_PORT0 0x00
#define CARD_CHANNEL_PORT1 0x10
/* Card_ChecksumBytes starts each sum from this value. name invented */
#define CARD_CHECKSUM_SEED 5
/* Card_ReadFile's buffer for the recomputed sums. name invented */
#define CARD_CHECKSUM_COUNT_MAX 32
/* Card_ReadHeader's path buffer, larger than CARD_PATH_SIZE in retail's frame. name invented */
#define CARD_HEADER_PATH_SIZE 416
/* Extents of the two directory listing formats: each string and its alignment padding. names invented */
#define CARD_DIRECTORY_FORMAT_SIZE 20
#define CARD_ENTRY_FORMAT_SIZE 24

extern unsigned long g_cardEventComplete;
extern unsigned long g_cardEventError;
extern unsigned long g_cardEventTimeout;
extern unsigned long g_cardEventNewDevice;
/* The save file's 16x16 4-bit icon frame and its 16-colour CLUT (.data 0x800739e8..0x80073a88,
 * retail bytes; Card_WriteFile puts the one frame in all three header slots). */
cardIcon_t g_cardIcon = { {
    0xff, 0xff, 0xff, 0xff, 0x8f, 0xf7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe4, 0x5c, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xc7, 0x7c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x78, 0x58, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xd9, 0x8d,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x5f, 0xa8, 0xb9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xaf, 0xee, 0xbc, 0xf8, 0xff, 0xff,
    0xff, 0xff, 0x9f, 0xd9, 0xe9, 0xf7, 0xff, 0xff, 0xff, 0xff, 0x6f, 0xeb, 0xbe, 0xf8, 0xff, 0xff, 0xff, 0xff, 0x3f,
    0xed, 0xce, 0xf1, 0xff, 0xff, 0xff, 0xff, 0x3f, 0xed, 0xde, 0xf2, 0xff, 0x9f, 0xbb, 0xbb, 0x7c, 0xed, 0xee, 0xff,
    0xff, 0xaa, 0xca, 0xcc, 0x4c, 0xdb, 0xee, 0x9f, 0x9a, 0xa8, 0xea, 0xce, 0x2a, 0x98, 0xd9, 0xbe, 0xca, 0x35, 0x92,
    0xaa, 0x78, 0x88, 0x88, 0xf3, 0xff, 0xff, 0x38, 0x73, 0x77, 0x98, 0x38, 0xf7, 0xff,
} };
cardClut_t g_cardCLUT = { {
    0x00, 0x00, 0x21, 0x04, 0x22, 0x04, 0x42, 0x08, 0x43, 0x04, 0x44, 0x08, 0x64, 0x08, 0x65, 0x08,
    0xa8, 0x10, 0x2e, 0x1d, 0x32, 0x19, 0x74, 0x21, 0xd5, 0x29, 0x15, 0x32, 0x99, 0x3a, 0x00, 0x00,
} };
/* "bu00:" and "bu10:" with their BIOS channels. */
static cardDevice_t g_cardDevices[2] = { { "bu00:", CARD_CHANNEL_PORT0, 0, 0 }, { "bu10:", CARD_CHANNEL_PORT1, 1, 0 } };
/* name: inferred from its strings, indexed by the CARD_* results */
static char *g_cardErrorMessages[] = { "NoError",           "NoCardPresent",  "NotCreate",     "NotOpen",
                                       "FileExists",        "FileNotExists",  "NotReadHeader", "NotReadData",
                                       "NotReadDataHeader", "NotWriteHeader", "NotWriteData",  "NotWriteDataHeader",
                                       "ChecksumError",     "SlotFree",       "SlotUsed",      "NoMemory" };
/* name: inferred from its strings, indexed by the CARD_EVENT_* results */
static char *g_cardEventMessages[] = { "Card ready", "Card fail", "No card", "New card" };
/* name: inferred from use: the card code formats directory paths with it */
const char g_cardDirectoryFormat[CARD_DIRECTORY_FORMAT_SIZE] = "Directory of %s\n";
/* name: inferred from use: the card code formats file entries with it */
const char g_cardEntryFormat[CARD_ENTRY_FORMAT_SIZE] = "NAME: %s  LENGTH: %d\n";
extern void Card_SJISToASCII( cardTitle_t *, cardTitle_t * );
/* name: inferred from its use on the save title (the inverse of Card_SJISToASCII, 0x80010170) */
extern void Card_ASCIIToSJIS( const char *text, cardTitle_t *title );

/*
==================
Card_Init
==================
*/
/* name: inferred from its body, which starts the card libraries and opens the card events */
void Card_Init( void )
{
    InitCARD( 1 );
    StartCARD();
    _bu_init();
    EnterCriticalSection();
    g_cardEventComplete = OpenEvent( SwCARD, EvSpIOE, EvMdNOINTR, 0 );
    g_cardEventError = OpenEvent( SwCARD, EvSpERROR, EvMdNOINTR, 0 );
    g_cardEventTimeout = OpenEvent( SwCARD, EvSpTIMOUT, EvMdNOINTR, 0 );
    g_cardEventNewDevice = OpenEvent( SwCARD, EvSpNEW, EvMdNOINTR, 0 );
    ExitCriticalSection();
    EnableEvent( g_cardEventComplete );
    EnableEvent( g_cardEventError );
    EnableEvent( g_cardEventTimeout );
    EnableEvent( g_cardEventNewDevice );
    _card_auto( 1 );
}

/*
==================
Card_WaitEvent
==================
*/
/* name: inferred from its body, which waits for one of the four card events */
int Card_WaitEvent( void )
{
    for ( ;; ) {
        if ( TestEvent( g_cardEventComplete ) == 1 )
            return CARD_EVENT_READY;
        if ( TestEvent( g_cardEventError ) == 1 )
            return CARD_EVENT_FAIL;
        if ( TestEvent( g_cardEventTimeout ) == 1 )
            return CARD_EVENT_NO_CARD;
        if ( TestEvent( g_cardEventNewDevice ) == 1 )
            return CARD_EVENT_NEW_CARD;
    }
}

/*
==================
Card_Status
==================
*/
/* name: inferred from its body, which asks _card_info for a port and waits for the result */
int Card_Status( short channel )
{
    int result = _card_info( channel );
    if ( result == 0 )
        return CARD_EVENT_NO_CARD;
    return Card_WaitEvent();
}

/*
==================
Card_EventMessage
==================
*/
/* name: inferred from g_cardEventMessages */
char *Card_EventMessage( int event )
{
    return g_cardEventMessages[event];
}

/*
==================
Card_ErrorMessage
==================
*/
/* name: inferred from g_cardErrorMessages */
char *Card_ErrorMessage( int error )
{
    return g_cardErrorMessages[error];
}

/*
==================
Card_PrintDirectory
==================
*/
/* name: inferred from its "Directory of %s" listing through printf */
void Card_PrintDirectory( const char *pattern, int port )
{
    struct DIRENTRY item;
    char            path[CARD_PATH_SIZE];
    char           *buffer = path;
    strcpy( buffer, g_cardDevices[(short)port].prefix );
    strcat( buffer, pattern );
    printf( g_cardDirectoryFormat, buffer );
    if ( firstfile( buffer, &item ) != 0 ) {
        do {
            printf( g_cardEntryFormat, &item, item.size );
        } while ( nextfile( &item ) != 0 );
    }
}

/*
==================
Card_FindFiles
==================
*/
/* name: inferred from its body, which lists the card files matching a pattern and counts their blocks */
short Card_FindFiles( struct DIRENTRY *entries, const char *pattern, short port, short *blocks )
{
    struct DIRENTRY  item;
    char             path[CARD_PATH_SIZE];
    const char      *suffix = pattern;
    struct DIRENTRY *entry = entries;
    short            count = 0;
    *blocks = 0;
    strcpy( path, g_cardDevices[port].prefix );
    strcat( path, suffix );
    if ( firstfile( path, &item ) != 0 ) {
        do {
            entry->size = item.size;
            strcpy( entry->name, item.name );
            ++count;
            ++entry;
            *blocks += (long)( (unsigned long)item.size + ( CARD_BLOCK_SIZE - 1UL ) ) / CARD_BLOCK_SIZE;
        } while ( nextfile( &item ) != 0 );
    }
    return count;
}

/*
==================
Card_CountFiles
==================
*/
/* name: inferred from its body, which returns Card_FindFiles's count */
short Card_CountFiles( struct DIRENTRY *entries, const char *pattern, int port )
{
    short blocks;
    return (short)Card_FindFiles( entries, pattern, (short)port, &blocks );
}

/*
==================
Card_DeleteFile
==================
*/
/* name: inferred from its body, which deletes a card file */
void Card_DeleteFile( const char *name, int port )
{
    char path[CARD_PATH_SIZE];
    strcpy( path, g_cardDevices[(short)port].prefix );
    strcat( path, name );
    delete ( path );
}

/*
==================
Card_Format
==================
*/
/* name: inferred from its body, which formats the card in a port */
void Card_Format( short port )
{
    format( g_cardDevices[port].prefix );
}

/*
==================
Card_ChecksumBytes
==================
*/
/* name: inferred from its body, which sums the bytes plus one each from 5 */
unsigned char Card_ChecksumBytes( const unsigned char *bytes, int length )
{
    int result = CARD_CHECKSUM_SEED;
    while ( length > 0 ) {
        result = ( result + *bytes++ ) + 1;
        --length;
    }
    return result;
}

/*
==================
Card_BlockChecksums

One checksum byte per block of at most 1024 bytes.
==================
*/
/* name: inferred from the checksum block that Card_ReadFile compares */
short Card_BlockChecksums( const void *source, int length, unsigned char *out )
{
    const unsigned char *bytes = source;
    int                  count = 0;
    while ( length > CARD_CHECKSUM_BLOCK_SIZE ) {
        out[count++] = Card_ChecksumBytes( bytes, CARD_CHECKSUM_BLOCK_SIZE );
        bytes += CARD_CHECKSUM_BLOCK_SIZE;
        length -= CARD_CHECKSUM_BLOCK_SIZE;
    }
    if ( length > 0 ) {
        out[count] = Card_ChecksumBytes( bytes, length );
        ++count;
    }
    return (short)count;
}

/*
==================
Card_FindDevice

The device in port 0 or 1 when its card is ready (or new); any other port picks
the first ready device. Returns 0 when there is none.
==================
*/
/* name: inferred from its use before every card file access */
cardDevice_t *Card_FindDevice( short port )
{
    int status[2];
    status[0] = Card_Status( g_cardDevices[0].channel );
    status[1] = Card_Status( g_cardDevices[1].channel );
    if ( port < 2 ) {
        if ( port >= 0 ) {
            int value = status[port];
            if ( value == CARD_EVENT_READY || value == CARD_EVENT_NEW_CARD )
                return &g_cardDevices[port];
            return 0;
        }
    }
    if ( status[0] == CARD_EVENT_READY || status[0] == CARD_EVENT_NEW_CARD )
        return &g_cardDevices[0];
    if ( status[1] == CARD_EVENT_READY || status[1] == CARD_EVENT_NEW_CARD )
        return &g_cardDevices[1];
    return 0;
}

/*
==================
Card_PresentMask
==================
*/
/* name: inferred from its body, which returns bit 0/1 for a card in port 0/1 */
short Card_PresentMask( void )
{
    int present = 0;
    if ( Card_FindDevice( 0 ) )
        present |= CARD_PRESENT_PORT0;
    if ( Card_FindDevice( 1 ) )
        present |= CARD_PRESENT_PORT1;
    return present;
}

/*
==================
Card_WriteFile

Writes a checksummed card file: a header with the save title and icon, the
checksum block, then the data; the file must not exist yet.
codegen: size and source are locals copied from the parameters, in this
order, so that their copies are scheduled after the port extension.
==================
*/
/* name: inferred from the save code retrying it on CARD_NOT_WRITE_* results */
/* MATCH-PENDING: R17, parameter copies int size = length and void *source = data only steer registers: with the
 * parameters used directly (int or short port) the a1/a2 moves schedule before the port sign extension instead of after
 * it (4 words). decomp-permuter randomizer (20 min, ~1,800 candidates from the direct-parameter base) found nothing better */
int Card_WriteFile( const char *name, void *data, int length, short port )
{
    char            path[CARD_PATH_SIZE];
    cardChecksums_t stored;
    cardDevice_t   *card;
    cardHeader_t   *header;
    unsigned int    total;
    unsigned int    rounded;
    int             fd;
    int             size = length;
    void           *source = data;

    card = Card_FindDevice( port );
    if ( card == 0 )
        return CARD_NO_CARD_PRESENT;
    header = Heap_Alloc( sizeof( cardHeader_t ) );
    strcpy( path, card->prefix );
    strcat( path, name );
    header->magic[0] = 'S';
    header->magic[1] = 'C';
    header->iconFlag = CARD_ICON_FLAG_ONE_FRAME;
    Card_ASCIIToSJIS( "Alien Trilogy save games        ", &header->title );
    header->clut = g_cardCLUT;
    header->icons[0] = g_cardIcon;
    header->icons[1] = g_cardIcon;
    header->icons[2] = g_cardIcon;
    total = size + sizeof( cardHeader_t ) + sizeof( cardChecksums_t );
    rounded = total / CARD_BLOCK_SIZE * CARD_BLOCK_SIZE;
    if ( total % CARD_BLOCK_SIZE )
        rounded += CARD_BLOCK_SIZE;
    header->blocks = rounded / CARD_BLOCK_SIZE;
    stored.length = size;
    Card_BlockChecksums( source, size, stored.sums );
    fd = open( path, O_WRONLY );
    if ( fd >= 0 ) {
        close( fd );
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_FILE_EXISTS;
    }
    fd = open( path, ( header->blocks << CARD_OPEN_BLOCKS_SHIFT ) | O_CREAT );
    if ( fd == -1 ) {
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_NOT_CREATE;
    }
    close( fd );
    fd = open( path, O_WRONLY );
    if ( fd == -1 ) {
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_NOT_OPEN;
    }
    if ( write( fd, header, sizeof( cardHeader_t ) ) != sizeof( cardHeader_t ) ) {
        close( fd );
        Card_DeleteFile( name, card->selector );
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_NOT_WRITE_HEADER;
    }
    Heap_Free( sizeof( cardHeader_t ) );
    if ( write( fd, &stored, sizeof( cardChecksums_t ) ) != sizeof( cardChecksums_t ) ) {
        close( fd );
        Card_DeleteFile( name, card->selector );
        return CARD_NOT_WRITE_DATA_HEADER;
    }
    if ( write( fd, source, size ) != size ) {
        close( fd );
        Card_DeleteFile( name, card->selector );
        return CARD_NOT_WRITE_DATA;
    }
    close( fd );
    return CARD_NO_ERROR;
}

/*
==================
Card_ReadFile
==================
*/
/* name: inferred from its header, checksum-block and data reads */
int Card_ReadFile( const char *name, void *destination, short port )
{
    char            path[CARD_PATH_SIZE];
    unsigned char   sums[CARD_CHECKSUM_COUNT_MAX];
    cardChecksums_t stored;
    cardDevice_t   *card;
    unsigned char  *header;
    int             fd;
    int             length;
    int             size;
    short           count;
    short           i;
    card = Card_FindDevice( port );
    if ( card == 0 )
        return CARD_NO_CARD_PRESENT;
    header = Heap_Alloc( sizeof( cardHeader_t ) );
    strcpy( path, card->prefix );
    strcat( path, name );
    fd = open( path, O_RDONLY );
    if ( fd < 0 ) {
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_NOT_OPEN;
    }
    size = read( fd, header, sizeof( cardHeader_t ) );
    if ( size != sizeof( cardHeader_t ) ) {
        close( fd );
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_NOT_READ_HEADER;
    }
    size = read( fd, &stored, sizeof( cardChecksums_t ) );
    if ( size != sizeof( cardChecksums_t ) ) {
        close( fd );
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_NOT_READ_DATA_HEADER;
    }
    length = stored.length;
    size = read( fd, destination, length );
    if ( size != length ) {
        close( fd );
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_NOT_READ_DATA;
    }
    close( fd );
    Heap_Free( sizeof( cardHeader_t ) );
    count = Card_BlockChecksums( destination, size, sums );
    for ( i = 0; i < count; i++ )
        if ( sums[i] != stored.sums[i] )
            return CARD_CHECKSUM_ERROR;
    return CARD_NO_ERROR;
}

/*
==================
Card_ReadHeader
==================
*/
/* name: inferred from its body, which reads a card file's header */
int Card_ReadHeader( const char *name, cardHeader_t *header, short port )
{
    char          path[CARD_HEADER_PATH_SIZE];
    cardTitle_t   title;
    cardDevice_t *card;
    int           fd;
    card = Card_FindDevice( port );
    if ( card == 0 )
        return CARD_NO_CARD_PRESENT;
    strcpy( path, card->prefix );
    strcat( path, name );
    fd = open( path, O_RDONLY );
    if ( fd < 0 ) {
        Heap_Free( sizeof( cardHeader_t ) );
        return CARD_NOT_OPEN;
    }
    if ( read( fd, header, sizeof( cardHeader_t ) ) != sizeof( cardHeader_t ) ) {
        close( fd );
        return CARD_NOT_READ_HEADER;
    }
    close( fd );
    title = header->title;
    Card_SJISToASCII( &title, &header->title );
    return CARD_NO_ERROR;
}
