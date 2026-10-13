/* Memory-card layer: BIOS card events, the two card devices, directory listing,
 * and checksummed save files (a 0x200-byte header, a 0x100-byte checksum block,
 * then the data). */
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
/* name: inferred from card_block_checksums, which sums the data in blocks of this many bytes */
#define CARD_CHECKSUM_BLOCK_SIZE 0x400
/* BIOS card channels (_card_info): port << 4 | card. names invented */
#define CARD_CHANNEL_PORT0 0x00
#define CARD_CHANNEL_PORT1 0x10
/* The card device name buffer ("bu00:"). name invented */
#define CARD_PREFIX_SIZE 8
/* The checksum block after the header: its length word and one sum per data block. name invented */
#define CARD_CHECKSUM_HEADER_SIZE 0x100
/* checksum_bytes starts each sum from this value. name invented */
#define CARD_CHECKSUM_SEED 5
/* card_read_file's buffer for the recomputed sums. name invented */
#define CARD_CHECKSUM_COUNT_MAX 32
/* card_read_header's path buffer, larger than CARD_PATH_SIZE in retail's frame. name invented */
#define CARD_HEADER_PATH_SIZE 416

typedef struct CardDevice {
    char prefix[CARD_PREFIX_SIZE];
    int channel;
    short selector;
    short reserved;
} CardDevice;
typedef struct CardChecksums { int length; unsigned char sums[CARD_CHECKSUM_HEADER_SIZE - sizeof(int)]; } CardChecksums;


extern unsigned long g_card_event_complete;
extern unsigned long g_card_event_error;
extern unsigned long g_card_event_timeout;
extern unsigned long g_card_event_new_device;
/* The save file's 16x16 4-bit icon frame and its 16-colour CLUT (.data 0x800739e8..0x80073a88,
 * retail bytes; card_write_file puts the one frame in all three header slots). */
CardIcon g_card_icon = {{
    0xff, 0xff, 0xff, 0xff, 0x8f, 0xf7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe4, 0x5c, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xc7, 0x7c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x78, 0x58, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xd9, 0x8d, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5f, 0xa8, 0xb9, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xaf, 0xee, 0xbc, 0xf8, 0xff, 0xff, 0xff, 0xff, 0x9f, 0xd9, 0xe9, 0xf7, 0xff,
    0xff, 0xff, 0xff, 0x6f, 0xeb, 0xbe, 0xf8, 0xff, 0xff, 0xff, 0xff, 0x3f, 0xed, 0xce, 0xf1, 0xff,
    0xff, 0xff, 0xff, 0x3f, 0xed, 0xde, 0xf2, 0xff, 0x9f, 0xbb, 0xbb, 0x7c, 0xed, 0xee, 0xff, 0xff,
    0xaa, 0xca, 0xcc, 0x4c, 0xdb, 0xee, 0x9f, 0x9a, 0xa8, 0xea, 0xce, 0x2a, 0x98, 0xd9, 0xbe, 0xca,
    0x35, 0x92, 0xaa, 0x78, 0x88, 0x88, 0xf3, 0xff, 0xff, 0x38, 0x73, 0x77, 0x98, 0x38, 0xf7, 0xff,
}};
CardClut g_card_clut = {{
    0x00, 0x00, 0x21, 0x04, 0x22, 0x04, 0x42, 0x08, 0x43, 0x04, 0x44, 0x08, 0x64, 0x08, 0x65, 0x08,
    0xa8, 0x10, 0x2e, 0x1d, 0x32, 0x19, 0x74, 0x21, 0xd5, 0x29, 0x15, 0x32, 0x99, 0x3a, 0x00, 0x00,
}};
/* "bu00:" and "bu10:" with their BIOS channels. */
static CardDevice g_card_devices[2] = {
    { "bu00:", CARD_CHANNEL_PORT0, 0, 0 },
    { "bu10:", CARD_CHANNEL_PORT1, 1, 0 }
};
/* name: inferred from its strings, indexed by the CARD_* results */
static char *g_card_error_messages[] = {
    "NoError", "NoCardPresent", "NotCreate", "NotOpen",
    "FileExists", "FileNotExists", "NotReadHeader", "NotReadData",
    "NotReadDataHeader", "NotWriteHeader", "NotWriteData", "NotWriteDataHeader",
    "ChecksumError", "SlotFree", "SlotUsed", "NoMemory"
};
/* name: inferred from its strings, indexed by the CARD_EVENT_* results */
static char *g_card_event_messages[] = { "Card ready", "Card fail", "No card", "New card" };
/* name: inferred from use: the card code formats directory paths with it. Extents include alignment padding. */
const char g_card_directory_format[20] = "Directory of %s\n";
/* name: inferred from use: the card code formats file entries with it */
const char g_card_entry_format[24] = "NAME: %s  LENGTH: %d\n";
extern void sjis_to_ascii(CardTitle *, CardTitle *);
/* name: inferred from its use on the save title (the inverse of sjis_to_ascii, 0x80010170) */
extern void ascii_to_sjis(const char *text, CardTitle *title);

/* name: inferred from its body, which starts the card libraries and opens the card events */
void card_init(void)
{
    InitCARD(1);
    StartCARD();
    _bu_init();
    EnterCriticalSection();
    g_card_event_complete = OpenEvent(SwCARD, EvSpIOE, EvMdNOINTR, 0);
    g_card_event_error = OpenEvent(SwCARD, EvSpERROR, EvMdNOINTR, 0);
    g_card_event_timeout = OpenEvent(SwCARD, EvSpTIMOUT, EvMdNOINTR, 0);
    g_card_event_new_device = OpenEvent(SwCARD, EvSpNEW, EvMdNOINTR, 0);
    ExitCriticalSection();
    EnableEvent(g_card_event_complete);
    EnableEvent(g_card_event_error);
    EnableEvent(g_card_event_timeout);
    EnableEvent(g_card_event_new_device);
    _card_auto(1);
}

/* name: inferred from its body, which waits for one of the four card events */
int card_wait_event(void)
{
    for (;;) {
        if (TestEvent(g_card_event_complete) == 1) return CARD_EVENT_READY;
        if (TestEvent(g_card_event_error) == 1) return CARD_EVENT_FAIL;
        if (TestEvent(g_card_event_timeout) == 1) return CARD_EVENT_NO_CARD;
        if (TestEvent(g_card_event_new_device) == 1) return CARD_EVENT_NEW_CARD;
    }
}

/* name: inferred from its body, which asks _card_info for a port and waits for the result */
int card_status(short channel)
{
    int result = _card_info(channel);
    if (result == 0)
        return CARD_EVENT_NO_CARD;
    return card_wait_event();
}

/* name: inferred from g_card_event_messages */
char *card_event_message(int event)
{
    return g_card_event_messages[event];
}

/* name: inferred from g_card_error_messages */
char *card_error_message(int error)
{
    return g_card_error_messages[error];
}

/* name: inferred from its "Directory of %s" listing through printf */
void card_print_directory(const char *pattern, int port)
{
    struct DIRENTRY item;
    char path[CARD_PATH_SIZE];
    char *buffer = path;
    strcpy(buffer, g_card_devices[(short)port].prefix);
    strcat(buffer, pattern);
    printf(g_card_directory_format, buffer);
    if (firstfile(buffer, &item) != 0) {
        do {
            printf(g_card_entry_format, &item, item.size);
        } while (nextfile(&item) != 0);
    }
}

/* name: inferred from its body, which lists the card files matching a pattern and counts their blocks */
short card_find_files(struct DIRENTRY *entries, const char *pattern, short port, short *blocks)
{
    struct DIRENTRY item;
    char path[CARD_PATH_SIZE];
    const char *suffix = pattern;
    struct DIRENTRY *entry = entries;
    short count = 0;
    *blocks = 0;
    strcpy(path, g_card_devices[port].prefix);
    strcat(path, suffix);
    if (firstfile(path, &item) != 0) {
        do {
            entry->size = item.size;
            strcpy(entry->name, item.name);
            ++count;
            ++entry;
            *blocks += (long)((unsigned long)item.size + (CARD_BLOCK_SIZE - 1UL)) / CARD_BLOCK_SIZE;
        } while (nextfile(&item) != 0);
    }
    return count;
}

/* name: inferred from its body, which returns card_find_files's count */
short card_count_files(struct DIRENTRY *entries, const char *pattern, int port)
{
    short blocks;
    return (short)card_find_files(entries, pattern, (short)port, &blocks);
}

/* name: inferred from its body, which deletes a card file */
void card_delete_file(const char *name, int port)
{
    char path[CARD_PATH_SIZE];
    strcpy(path, g_card_devices[(short)port].prefix);
    strcat(path, name);
    delete(path);
}

/* name: inferred from its body, which formats the card in a port */
void card_format(short port)
{
    format(g_card_devices[port].prefix);
}

/* name: inferred from its body, which sums the bytes plus one each from 5 */
unsigned char checksum_bytes(const unsigned char *bytes, int length)
{
    int result = CARD_CHECKSUM_SEED;
    while (length > 0) {
        result = (result + *bytes++) + 1;
        --length;
    }
    return result;
}

/* One checksum byte per block of at most 1024 bytes.
 * name: inferred from the checksum block that card_read_file compares */
short card_block_checksums(const void *source, int length, unsigned char *out)
{
    const unsigned char *bytes = source;
    int count = 0;
    while (length > CARD_CHECKSUM_BLOCK_SIZE) {
        out[count++] = checksum_bytes(bytes, CARD_CHECKSUM_BLOCK_SIZE);
        bytes += CARD_CHECKSUM_BLOCK_SIZE;
        length -= CARD_CHECKSUM_BLOCK_SIZE;
    }
    if (length > 0) {
        out[count] = checksum_bytes(bytes, length);
        ++count;
    }
    return (short)count;
}

/* The device in port 0 or 1 when its card is ready (or new); any other port picks
 * the first ready device. Returns 0 when there is none.
 * name: inferred from its use before every card file access */
CardDevice *card_find_device(int port)
{
    int status[2];
    status[0] = card_status(g_card_devices[0].channel);
    status[1] = card_status(g_card_devices[1].channel);
    if ((short)port < 2) {
        if ((short)port >= 0) {
            int value = status[(short)port];
            if (value == CARD_EVENT_READY || value == CARD_EVENT_NEW_CARD)
                return &g_card_devices[(short)port];
            return 0;
        }
    }
    if (status[0] == CARD_EVENT_READY || status[0] == CARD_EVENT_NEW_CARD)
        return &g_card_devices[0];
    if (status[1] == CARD_EVENT_READY || status[1] == CARD_EVENT_NEW_CARD)
        return &g_card_devices[1];
    return 0;
}

/* name: inferred from its body, which returns bit 0/1 for a card in port 0/1 */
short card_present_mask(void)
{
    int present = 0;
    if (card_find_device(0)) present |= CARD_PRESENT_PORT0;
    if (card_find_device(1)) present |= CARD_PRESENT_PORT1;
    return present;
}

/* Writes a checksummed card file: a header with the save title and icon, the
 * checksum block, then the data; the file must not exist yet.
 * codegen: size and source are locals copied from the parameters, in this
 * order, so that their copies are scheduled after the port extension.
 * name: inferred from the save code retrying it on CARD_NOT_WRITE_* results */
int card_write_file(const char *name, void *data, int length, int port)
{
    char path[CARD_PATH_SIZE];
    CardChecksums stored;
    CardDevice *card;
    CardHeader *header;
    unsigned int total;
    unsigned int rounded;
    int fd;
    int size = length;
    void *source = data;

    card = card_find_device((short)port);
    if (card == 0)
        return CARD_NO_CARD_PRESENT;
    header = heap_alloc(sizeof(CardHeader));
    strcpy(path, card->prefix);
    strcat(path, name);
    header->magic[0] = 'S';
    header->magic[1] = 'C';
    header->icon_flag = CARD_ICON_FLAG_ONE_FRAME;
    ascii_to_sjis("Alien Trilogy save games        ", &header->title);
    header->clut = g_card_clut;
    header->icons[0] = g_card_icon;
    header->icons[1] = g_card_icon;
    header->icons[2] = g_card_icon;
    total = size + sizeof(CardHeader) + sizeof(CardChecksums);
    rounded = total / CARD_BLOCK_SIZE * CARD_BLOCK_SIZE;
    if (total % CARD_BLOCK_SIZE)
        rounded += CARD_BLOCK_SIZE;
    header->blocks = rounded / CARD_BLOCK_SIZE;
    stored.length = size;
    card_block_checksums(source, size, stored.sums);
    fd = open(path, O_WRONLY);
    if (fd >= 0) {
        close(fd);
        heap_free(sizeof(CardHeader));
        return CARD_FILE_EXISTS;
    }
    fd = open(path, (header->blocks << CARD_OPEN_BLOCKS_SHIFT) | O_CREAT);
    if (fd == -1) {
        heap_free(sizeof(CardHeader));
        return CARD_NOT_CREATE;
    }
    close(fd);
    fd = open(path, O_WRONLY);
    if (fd == -1) {
        heap_free(sizeof(CardHeader));
        return CARD_NOT_OPEN;
    }
    if (write(fd, header, sizeof(CardHeader)) != sizeof(CardHeader)) {
        close(fd);
        card_delete_file(name, card->selector);
        heap_free(sizeof(CardHeader));
        return CARD_NOT_WRITE_HEADER;
    }
    heap_free(sizeof(CardHeader));
    if (write(fd, &stored, sizeof(CardChecksums)) != sizeof(CardChecksums)) {
        close(fd);
        card_delete_file(name, card->selector);
        return CARD_NOT_WRITE_DATA_HEADER;
    }
    if (write(fd, source, size) != size) {
        close(fd);
        card_delete_file(name, card->selector);
        return CARD_NOT_WRITE_DATA;
    }
    close(fd);
    return CARD_NO_ERROR;
}

/* name: inferred from its header, checksum-block and data reads */
int card_read_file(const char *name, void *destination, short port)
{
    char path[CARD_PATH_SIZE];
    unsigned char sums[CARD_CHECKSUM_COUNT_MAX];
    CardChecksums stored;
    CardDevice *card;
    unsigned char *header;
    int fd;
    int length;
    int size;
    short count;
    short i;
    card = card_find_device(port);
    if (card == 0)
        return CARD_NO_CARD_PRESENT;
    header = heap_alloc(sizeof(CardHeader));
    strcpy(path, card->prefix);
    strcat(path, name);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        heap_free(sizeof(CardHeader));
        return CARD_NOT_OPEN;
    }
    size = read(fd, header, sizeof(CardHeader));
    if (size != sizeof(CardHeader)) {
        close(fd);
        heap_free(sizeof(CardHeader));
        return CARD_NOT_READ_HEADER;
    }
    size = read(fd, &stored, sizeof(CardChecksums));
    if (size != sizeof(CardChecksums)) {
        close(fd);
        heap_free(sizeof(CardHeader));
        return CARD_NOT_READ_DATA_HEADER;
    }
    length = stored.length;
    size = read(fd, destination, length);
    if (size != length) {
        close(fd);
        heap_free(sizeof(CardHeader));
        return CARD_NOT_READ_DATA;
    }
    close(fd);
    heap_free(sizeof(CardHeader));
    count = card_block_checksums(destination, size, sums);
    for (i = 0; i < count; i++)
        if (sums[i] != stored.sums[i])
            return CARD_CHECKSUM_ERROR;
    return CARD_NO_ERROR;
}

/* name: inferred from its body, which reads a card file's header */
int card_read_header(const char *name, CardHeader *header, short port)
{
    char path[CARD_HEADER_PATH_SIZE];
    CardTitle title;
    CardDevice *card;
    int fd;
    card = card_find_device(port);
    if (card == 0)
        return CARD_NO_CARD_PRESENT;
    strcpy(path, card->prefix);
    strcat(path, name);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        heap_free(sizeof(CardHeader));
        return CARD_NOT_OPEN;
    }
    if (read(fd, header, sizeof(CardHeader)) != sizeof(CardHeader)) {
        close(fd);
        return CARD_NOT_READ_HEADER;
    }
    close(fd);
    title = header->title;
    sjis_to_ascii(&title, &header->title);
    return CARD_NO_ERROR;
}
