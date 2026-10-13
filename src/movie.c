/* Movie playback, from the lineage of Sony's STR samples
 * (SAMPLE/MOVIE/ANIM/MAIN.C: strInit, strCallback, strNext, strNextVlc, strSync). */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <LIBCD.H>
#include <LIBPRESS.H>
#include <LIBETC.H>
#include <HEAPFILE.H>
#include <HEAP.H>
#include <GAME.H>
#include <DISPLAY.H>
#include <MOVIE.H>

/* The sample's ring buffer size in sectors (StSetRing), here 64. */
#define RING_SIZE 64
/* The streaming mode bit alone, as the sample spells it (0x100); Runtime 3.3 LIBCD.H's
 * CdlModeStream adds CdlModeSize1. name invented */
#define MOVIE_MODE_STREAM 0x100
/* name: invented, after the sample's VLC buffers (u_long vlcbuf0[320/2*256]) */
#define VLC_BUFF_SIZE (320 / 2 * 256)
#define bound(val, n) ((((val) - 1) / (n) + 1) * (n))
/* An MDEC macroblock is 16 by 16 pixels; at 24 bits per pixel its 16 pixels span 24 VRAM
 * halfwords, three for each pair of pixels. names invented */
#define MACROBLOCK_SIZE 16
#define MACROBLOCK_WIDTH_RGB24 24
#define RGB24_PAIR_HALFWORDS 3
/* A raw (mode 2) CD sector in bytes. name invented */
#define CD_RAW_SECTOR_SIZE 2352
/* MovieDecode.end_scale is in eighths of the stream's frames. name invented */
#define MOVIE_END_SCALE_ONE 8
/* CdControlB's parameter buffer. name invented */
#define CD_PARAM_SIZE 4
/* Image buffer bytes allocated per frame row (24-bit and 16-bit). names invented */
#define MOVIE_IMAGE_ROW_BYTES_RGB24 96
#define MOVIE_IMAGE_ROW_BYTES 64
/* Ticks before a skippable record lets Start or X skip it. name invented */
#define MOVIE_SKIP_DELAY 300
/* DecDCTin mode bit set from MovieStreamRecord.decode_flag (bit 0 is 24-bit output). name invented */
#define DCT_MODE_DECODE_FLAG 2

/* The sample's DECENV: double VLC buffers, one image buffer, the ring
 * buffer, double transfer rectangles and the slice decoded per DecDCTout. */
typedef struct MovieDecode {
    u_long *vlcbuf[2];
    u_short *imgbuf;
    u_long *ringbuf;
    int vlcid;
    RECT rect[2];
    int rectid;
    RECT slice;
    int isdone;
    /* Invented names for the playback parameters that follow the sample's fields. */
    short param_34;
    short rgb24;
    short y_offset;
    short end_scale;
    short end_offset;
    short skippable;
} MovieDecode;


typedef struct MovieStreamRecord MovieStreamRecord;
struct MovieStreamRecord {
    RECT rect[2];
    short y_offset;
    short file;
    const char *name;
    u_long start_frame;
    u_long end_frame;
    short mode;
    short decode_flag;
    short end_scale;
    short end_offset;
    short skippable;
    unsigned char reserved_2a[2];
    short (*update)(); /* movie_show_frame; movie_play_record passes (&g_movie_decode, 0, 0) */
};

/* name: inferred from the sample's DECENV "dec" */
extern MovieDecode g_movie_decode;
short movie_show_frame(MovieDecode *dec);

/* name: inferred from the stream records movie_play plays by index */
static MovieStreamRecord g_movie_records[] = {
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 16, FILE_PROBE_STR, 0, 0, 1071, -1, 0, 8, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_INTRO_STR, 0, 0, 13500, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH1_STR, 0, 0, 710, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH2_STR, 0, 0, 1010, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH3_STR, 0, 0, 670, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH4_STR, 0, 0, 430, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH5_STR, 0, 0, 770, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH6_STR, 0, 0, 720, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 16, FILE_ACCLAIM_STR, 0, 0, 1910, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 16, FILE_LEGAL_STR, 0, 0, 1900, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 0, 240, 480, 240 } }, 40, FILE_S1_2_STR, 0, 0, 6500, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 0, 240, 480, 240 } }, 40, FILE_S2_3_STR, 0, 0, 8700, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 0, 240, 480, 240 } }, 40, FILE_GAMEOVER_STR, 0, 0, 9319, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 0, FILE_FOX_STR, 0, 0, 1410, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 0, FILE_FOX_STR, 0, 0, 1410, -1, 0, 7, 0, -1, { 0, 0 }, movie_show_frame }
};
/* names: inferred from SetDefDispEnv/SetDefDrawEnv in movie_show_frame */
extern DISPENV g_movie_disp;
extern DRAWENV g_movie_draw;
/* Set once the stream reaches its end frame; -1 from the stream's end callback.
 * name: inferred from that */
extern int g_movie_finished;
/* name: inferred from strCallback, which narrows the first slice of a frame by it */
extern int g_first_slice;
extern int g_9a120; /* StCdIntrFlag */
/* name: inferred from its division by the 2352-byte raw sector size */
extern int g_stream_bytes;
/* name: inferred from strNext, which sets g_movie_finished at this frame */
extern int g_movie_end_frame;
/* names: inferred from strNext, which copies the sector header's width and height */
int g_frame_width = 0;
int g_frame_height = 0;
/* name: inferred from strNext computing g_movie_end_frame once while it is 0 */
int g_end_frame_ready = 0;
/* name: inferred from movie_show_frame, which allows skipping 300 ticks after it */
int g_movie_start_time = 0;
/* name: inferred from strNext, which divides it by the sectors per frame */
int g_movie_sectors = 0;
/* name: inferred from the sector header's frameCount, stored per frame */
int g_movie_frame = 0;
short g_8d058 = -1;
/* name: inferred from its increment per VSync in movie_show_frame */
int g_movie_vsyncs = 0;
/* Runtime 3.3 LIBPRESS.H omits this LIBPRESS export. */
extern void DecDCToutCallback(void (*func)());
/* name: inferred from movie_play, which plays a record through it */
extern short movie_play_record(MovieStreamRecord *record);

/* Seek target of the movie stream. name: inferred from the CdlSeekL in strKickCD */
extern CdlLOC g_movie_loc;

/* name: inferred from CdSearchFile, which fills it with the record's stream file */
extern CdlFILE g_movie_file;
extern void movie_display_setup(const MovieStreamRecord *record);

/* name: from the lineage sample (strKickCD), which seeks and starts streaming */
void strKickCD(void)
{
    while (CdControl(CdlSeekL, (u_char *)&g_movie_loc, 0) == 0)
        ;
    while (CdRead2(MOVIE_MODE_STREAM | CdlModeSpeed | CdlModeRT) == 0)
        ;
}

/* name: from the lineage sample (strInit) */
void strInit(const MovieStreamRecord *record, void (*decode_callback)(),
             int (*stream_callback)())
{
    DecDCTReset(0);
    g_movie_finished = 0;
    DecDCToutCallback(decode_callback);
    StSetRing(g_movie_decode.ringbuf, RING_SIZE);
    StSetStream(record->mode, record->start_frame, record->end_frame,
                0, stream_callback);
    strKickCD();
}

/* name: from the lineage sample (strCallback) */
void strCallback(void)
{
    int mod;

    if (g_movie_decode.rgb24 && g_9a120) {
        StCdInterrupt();
        g_9a120 = 0;
    }
    g_movie_decode.slice.y += g_movie_decode.y_offset;
    LoadImage(&g_movie_decode.slice, (u_long *)g_movie_decode.imgbuf);
    g_movie_decode.slice.y -= g_movie_decode.y_offset;
    if (g_movie_decode.rgb24)
        mod = g_movie_decode.rect[g_movie_decode.rectid].w % MACROBLOCK_WIDTH_RGB24;
    else
        mod = g_movie_decode.rect[g_movie_decode.rectid].w % MACROBLOCK_SIZE;
    if (g_first_slice && mod) {
        if (g_movie_decode.rgb24)
            g_movie_decode.slice.x += mod * RGB24_PAIR_HALFWORDS / 2;
        else
            g_movie_decode.slice.x += mod;
        g_first_slice = 0;
    } else {
        if (g_movie_decode.rgb24)
            g_movie_decode.slice.x += MACROBLOCK_WIDTH_RGB24;
        else
            g_movie_decode.slice.x += MACROBLOCK_SIZE;
    }
    if (g_movie_decode.slice.x < g_movie_decode.rect[g_movie_decode.rectid].x + g_movie_decode.rect[g_movie_decode.rectid].w) {
        if (g_movie_decode.rgb24)
            DecDCTout((u_long *)g_movie_decode.imgbuf, bound(g_movie_decode.slice.w, MACROBLOCK_WIDTH_RGB24) * bound(g_movie_decode.slice.h, MACROBLOCK_SIZE) / 2);
        else
            DecDCTout((u_long *)g_movie_decode.imgbuf, bound(g_movie_decode.slice.w, MACROBLOCK_SIZE) * bound(g_movie_decode.slice.h, MACROBLOCK_SIZE) / 2);
    } else {
        g_movie_decode.isdone = 1;
        g_first_slice = 1;
        g_movie_decode.rectid = g_movie_decode.rectid ? 0 : 1;
        g_movie_decode.slice.x = g_movie_decode.rect[g_movie_decode.rectid].x;
        g_movie_decode.slice.y = g_movie_decode.rect[g_movie_decode.rectid].y;
    }
}

/* name: from the lineage sample (strNext) */
u_long *strNext(void)
{
    RECT rect;
    u_long *addr;
    StHEADER *sector;
    int cnt = WAIT_TIME;

    while (StGetNext(&addr, (u_long **)&sector)) {
        if (--cnt == 0)
            return 0;
    }
    if (addr[0] != sector->dummy1 || addr[1] != sector->dummy2) {
        StFreeRing(addr);
        return 0;
    }
    if (g_end_frame_ready == 0) {
        g_movie_end_frame = g_stream_bytes / CD_RAW_SECTOR_SIZE;
        g_movie_end_frame = g_movie_sectors;
        g_movie_end_frame = g_movie_end_frame / sector->nSectors;
        g_movie_end_frame = g_movie_decode.end_scale * g_movie_end_frame / MOVIE_END_SCALE_ONE;
        g_end_frame_ready = 1;
        g_movie_end_frame = g_movie_decode.end_offset + g_movie_end_frame;
    }
    g_movie_frame = sector->frameCount;
    if (sector->frameCount >= g_movie_end_frame)
        g_movie_finished = 1;
    if (g_frame_width != sector->width || g_frame_height != sector->height) {
        g_frame_width = sector->width;
        g_frame_height = sector->height;
        rect.x = 0;
        rect.y = 0;
        rect.w = g_frame_width;
        rect.h = g_frame_height;
        ClearImage(&rect, 0, 0, 0);
    }
    if (g_movie_decode.rgb24)
        g_movie_decode.rect[0].w = g_movie_decode.rect[1].w = g_frame_width * RGB24_PAIR_HALFWORDS / 2;
    else
        g_movie_decode.rect[0].w = g_movie_decode.rect[1].w = g_frame_width;
    g_movie_decode.rect[0].h = g_movie_decode.rect[1].h = g_frame_height;
    g_movie_decode.slice.h = g_frame_height;
    return addr;
}

/* name: from the lineage sample (strNextVlc) */
int strNextVlc(void)
{
    u_long *next;
    int count = WAIT_TIME;

    while ((next = strNext()) == 0) {
        if (--count == 0)
            return -1;
    }
    g_movie_decode.vlcid ^= 1;
    DecDCTvlc(next, g_movie_decode.vlcbuf[g_movie_decode.vlcid]);
    StFreeRing(next);
    return 0;
}

/* name: from the lineage sample (strSync) */
void strSync(void)
{
    volatile u_long cnt = WAIT_TIME;

    while (g_movie_decode.isdone == 0) {
        if (--cnt == 0) {
            g_movie_decode.isdone = 1;
            g_movie_decode.rectid = g_movie_decode.rectid ? 0 : 1;
            g_movie_decode.slice.x = g_movie_decode.rect[g_movie_decode.rectid].x;
            g_movie_decode.slice.y = g_movie_decode.rect[g_movie_decode.rectid].y;
        }
    }
    g_movie_decode.isdone = 0;
}

/* name: inferred from it ending playback through g_movie_finished */
int movie_end_callback(void)
{
    g_movie_finished = -1;
    return 0;
}

/* Plays one stream record: finds the file, allocates the ring, image and VLC
 * buffers, then decodes frames until the stream ends or update says stop. */
short movie_play_record(MovieStreamRecord *record)
{
    u_char param[CD_PARAM_SIZE];
    short i;
    int mode;
    short result;

    g_first_slice = 1;
    g_end_frame_ready = 0;
    if (CdSearchFile(&g_movie_file, (char *)record->name) == 0)
        return MOVIE_NOT_FOUND;
    g_movie_loc.minute = g_movie_file.pos.minute;
    g_movie_loc.second = g_movie_file.pos.second;
    g_movie_loc.sector = g_movie_file.pos.sector;
    g_stream_bytes = g_movie_file.size;
    g_movie_decode.ringbuf = heap_alloc(RING_SIZE * SECTOR_SIZE * sizeof(u_long));
    if (g_movie_decode.ringbuf == 0)
        return MOVIE_NO_MEMORY;
    if (record->mode)
        g_movie_decode.imgbuf = heap_alloc(record->rect[0].h * MOVIE_IMAGE_ROW_BYTES_RGB24);
    else
        g_movie_decode.imgbuf = heap_alloc(record->rect[0].h * MOVIE_IMAGE_ROW_BYTES);
    if (g_movie_decode.imgbuf == 0)
        return MOVIE_NO_MEMORY;
    for (i = 0; i < 2; i++) {
        g_movie_decode.vlcbuf[i] = heap_alloc(VLC_BUFF_SIZE * sizeof(u_long));
        if (g_movie_decode.vlcbuf[i] == 0)
            return MOVIE_NO_MEMORY;
    }
    g_movie_frame = 0;
    movie_display_setup(record);
    {
        int m = record->mode != 0;

        if (record->decode_flag)
            m |= DCT_MODE_DECODE_FLAG;
        mode = m;
    }
    strInit(record, strCallback, movie_end_callback);
    strNextVlc();
    result = 0;
    while (g_movie_finished == 0 && result == 0) {
        DecDCTin(g_movie_decode.vlcbuf[g_movie_decode.vlcid], mode);
        if (g_movie_decode.rgb24)
            DecDCTout((u_long *)g_movie_decode.imgbuf, bound(g_movie_decode.slice.w, MACROBLOCK_WIDTH_RGB24) * bound(g_movie_decode.slice.h, MACROBLOCK_SIZE) / 2);
        else
            DecDCTout((u_long *)g_movie_decode.imgbuf, bound(g_movie_decode.slice.w, MACROBLOCK_SIZE) * bound(g_movie_decode.slice.h, MACROBLOCK_SIZE) / 2);
        strNextVlc();
        strSync();
        result = record->update(&g_movie_decode, 0, 0);
        g_movie_decode.param_34 = 0;
    }
    param[0] = CdlModeSpeed;
    CdControlB(CdlSetmode, param, 0);
    DecDCToutCallback(0);
    CdDataCallback(0);
    CdReadyCallback(0);
    CdControlB(CdlPause, 0, 0);
    return result;
}

/* Waits a VSync, lets Start or X skip, and shows the finished frame buffer.
 * name: inferred from that */
short movie_show_frame(MovieDecode *dec)
{
    static unsigned short pad;
    int id;

    VSync(0);
    g_movie_vsyncs++;
    if (g_frame_count - g_movie_start_time > MOVIE_SKIP_DELAY || dec->skippable == 0) {
        pad = PadRead(0);
        if (pad & PADstart || pad & PADRdown) {
            if (dec->skippable)
                return MOVIE_SKIPPED;
            return MOVIE_ABORTED;
        }
    }
    id = dec->rectid ? 0 : 1;
    SetDefDispEnv(&g_movie_disp, dec->rect[id].x, dec->rect[id].y, dec->rect[id].w, dec->rect[id].h);
    SetDefDrawEnv(&g_movie_draw, dec->rect[id].x, dec->rect[id].y, dec->rect[id].w, dec->rect[id].h);
    g_movie_draw.ofs[0] = 0;
    g_movie_draw.ofs[1] = 0;
    g_movie_draw.clip.x = 0;
    g_movie_draw.clip.y = 0;
    g_movie_draw.clip.h = VRAM_HEIGHT;
    g_movie_draw.clip.w = VRAM_WIDTH;
    if (dec->rgb24) {
        g_movie_disp.isrgb24 = 1;
        g_movie_disp.disp.w = g_movie_disp.disp.w * 2 / RGB24_PAIR_HALFWORDS;
    }
    PutDispEnv(&g_movie_disp);
    PutDrawEnv(&g_movie_draw);
    SetDispMask(1);
    return 0;
}

/* name: inferred from its body, which plays a movie by index between heap marks */
short movie_play(short index)
{
    MovieStreamRecord *record = &g_movie_records[index];
    short slot;
    short result;

    switch (index) {
    case MOVIE_PROBE:
    case MOVIE_ACCLAIM:
    case MOVIE_LEGAL:
    case MOVIE_FOX_TITLE:
        if (record->skippable) {
            g_movie_start_time = g_frame_count;
            break;
        }
    default:
        g_movie_start_time = g_frame_count - MOVIE_SKIP_DELAY;
        break;
    }
    slot = heap_mark();
    record->name = g_file_table[record->file].name;
    g_8d058 = 0;
    g_movie_sectors = record->end_frame;
    result = movie_play_record(record);
    heap_release(slot);
    record->skippable = 0;
    return result;
}
