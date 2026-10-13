/* VRAM upload queue: sprite frames waiting for LoadImage, flushed once a frame by
 * upload_queue_flush. Retail .text 0x800218a4..0x80021e88. One translation unit: its statics
 * (.sbss 0x8008d0d0..0x8008d0f8, .sdata 0x8008cfa8) are read only by these functions.
 * Name invented.
 */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>

#include <SPRITEUV.H>
#include <SPRITE.H>
#include <HEAP.H>
#include <KERNEL.H>
#include <LIBAPI.H>
#include <TEXTURE.H>

typedef struct TextureUpload {
    RECT rect;
    u_long *pixels;
    int compressed; /* invented name: pixels were unpacked into a staging buffer (sprite_frame_upload) */
    struct TextureUpload *next; /* free-list link, then the previously queued upload */
    struct TextureUpload *queued;
} TextureUpload;

/* Upload records in the pool. name: invented */
#define UPLOAD_RECORD_COUNT 4


/* name invented: horizontal blanks (GetRCnt(RCntCNT1)) spent unpacking frames; written
 * only. Module static in initialized .sdata (0x8008cfa8). */
static u_short g_unpack_hblanks = 0;

/* Module statics (.sbss 0x8008d0d0..0x8008d0f8), in their retail order.
 * names: runtime evidence, analysis/runtime/globals.json (write watches at level load
 * and in play: sprite_frame_upload unpacks into the cursor and queues, upload_queue_flush drains the queue
 * and rewinds the cursor every frame). */
/* Next free byte of the staging buffer g_unpack_buffer. */
static void *g_unpack_cursor;
/* -1 once the cursor reaches g_unpack_buffer_end, cleared every frame. */
static short g_unpack_full;
/* Free upload records. */
static void *g_upload_free;
/* Last queued upload. */
static TextureUpload *g_upload_queue_tail;
/* First queued upload. */
static TextureUpload *g_upload_queue;
/* The upload records (a heap block, upload_pool_alloc). */
static TextureUpload *g_upload_pool;

/* name: static evidence, analysis/runtime/functions.json */
void upload_pool_alloc(void) { g_upload_pool = heap_alloc(UPLOAD_RECORD_COUNT * sizeof(TextureUpload)); }

/* name: runtime evidence, analysis/runtime/functions.json */
void upload_queue_reset(void)
{
    TextureUpload *record = g_upload_pool;
    void **link;
    short i;
    int again;
    void *old_head;

    g_unpack_full = 0;
    g_upload_queue = 0;
    g_upload_queue_tail = 0;
    g_upload_free = 0;
    i = 0;
    g_unpack_cursor = g_unpack_buffer;
    link = (void **)&record->next;
    do {
        old_head = g_upload_free;
        link[1] = 0;
        g_upload_free = record;
        ++i;
        record++;
        again = i < UPLOAD_RECORD_COUNT;
        link[0] = old_head;
        link += sizeof(TextureUpload) / sizeof(*link);
    } while (again);
}

/* name: runtime evidence, analysis/runtime/functions.json */
void upload_queue_flush(void)
{
    TextureUpload *upload;

    while ((upload = g_upload_queue) != 0) {
        if (upload->queued) {
            g_upload_queue = upload->queued;
            upload->queued = 0;
            upload->next = g_upload_free;
            g_upload_free = upload;
        } else {
            g_upload_queue = 0;
            g_upload_queue_tail = 0;
            upload->next = g_upload_free;
            g_upload_free = upload;
        }
        LoadImage(&upload->rect, upload->pixels);
    }
    g_unpack_full = 0;
    g_unpack_cursor = g_unpack_buffer;
}

/* name: runtime evidence, analysis/runtime/functions.json */
int upload_can_queue_pair(Sprite *first, Sprite *second)
{
    TextureUpload *free_list = g_upload_free;
    int a, b;

    if (free_list == 0 || free_list->next == 0)
        return 0;
    a = first->flags & SPRITE_FLAG_PACKED;
    b = second->flags & SPRITE_FLAG_PACKED;
    if (a && b) {
        if (g_unpack_cursor != g_unpack_buffer)
            return 0;
    } else if ((a || b) && g_unpack_full) {
        return 0;
    }
    return -1;
}

/* name: SPRITE.H. The two arms are written in different statement orders, and only the
 * uncompressed one ends with its own queue tail and return: that keeps retail's layout, where
 * the compressed arm's queue stores stay separate from the uncompressed arm's. */
/* name: runtime evidence, analysis/runtime/functions.json */
int sprite_frame_upload(Sprite *sprite, short mode)
{
    TextureUpload *upload = g_upload_free;
    long start;

    if (upload == 0)
        return 0;
    if ((upload->compressed = sprite->flags & SPRITE_FLAG_PACKED)) {
        if (g_unpack_full)
            return 0;
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        g_upload_free = upload->next;
        upload->rect.h = sprite->frame->h;
        upload->pixels = g_unpack_cursor;
        start = GetRCnt(RCntCNT1);
        if (mode == 0)
            unpack_rle((u_char *)sprite->frame->pixels, g_unpack_cursor);
        else
            unpack_lz((u_char *)sprite->frame->pixels, g_unpack_cursor);
        g_unpack_hblanks = GetRCnt(RCntCNT1) - start + g_unpack_hblanks;
        if (g_unpack_cursor == g_unpack_buffer_end)
            g_unpack_full = -1;
        else
            g_unpack_cursor = g_unpack_buffer_end;
        upload->next = g_upload_queue_tail;
        if (g_upload_queue_tail == 0)
            g_upload_queue = upload;
        else
            g_upload_queue_tail->queued = upload;
    } else {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        upload->rect.h = sprite->frame->h;
        g_upload_free = upload->next;
        upload->pixels = sprite->frame->pixels;
        upload->next = g_upload_queue_tail;
        if (g_upload_queue_tail == 0)
            g_upload_queue = upload;
        else
            g_upload_queue_tail->queued = upload;
        g_upload_queue_tail = upload;
        return -1;
    }
    g_upload_queue_tail = upload;
    return -1;
}

/* name: SPRITE.H. sprite_frame_upload for the boss sprite sets: one encoding, unpacked into
 * g_boss_unpack_buffer, which is never full. */
/* name: static evidence, analysis/runtime/functions.json */
int boss_sprite_frame_upload(Sprite *sprite)
{
    TextureUpload *upload = g_upload_free;
    long start;

    if (upload == 0)
        return 0;
    if ((upload->compressed = sprite->flags & SPRITE_FLAG_PACKED)) {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        g_upload_free = upload->next;
        upload->rect.h = sprite->frame->h;
        upload->pixels = g_boss_unpack_buffer;
        start = GetRCnt(RCntCNT1);
        unpack_rle((u_char *)sprite->frame->pixels, g_boss_unpack_buffer);
        g_unpack_hblanks = GetRCnt(RCntCNT1) - start + g_unpack_hblanks;
        upload->next = g_upload_queue_tail;
        if (g_upload_queue_tail == 0)
            g_upload_queue = upload;
        else
            g_upload_queue_tail->queued = upload;
    } else {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        upload->rect.h = sprite->frame->h;
        g_upload_free = upload->next;
        upload->pixels = sprite->frame->pixels;
        upload->next = g_upload_queue_tail;
        if (g_upload_queue_tail == 0)
            g_upload_queue = upload;
        else
            g_upload_queue_tail->queued = upload;
        g_upload_queue_tail = upload;
        return -1;
    }
    g_upload_queue_tail = upload;
    return -1;
}
