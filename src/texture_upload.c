/* texture_upload.c -- VRAM upload queue: sprite frames waiting for LoadImage, flushed once a frame
 * by Tex_FlushUploadQueue */
/* Retail .text 0x800218a4..0x80021e88. One translation unit: its statics (.sbss
 * 0x8008d0d0..0x8008d0f8, .sdata 0x8008cfa8) are read only by these functions. Name invented. */
#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>

#include <SPRITEUV.H>
#include <SPRITE.H>
#include <HEAP.H>
#include <KERNEL.H>
#include <LIBAPI.H>
#include <TEXTURE.H>

/* Upload records in the pool. name: invented */
#define UPLOAD_RECORD_COUNT 4


/* name invented: horizontal blanks (GetRCnt(RCntCNT1)) spent unpacking frames; written
 * only. Module static in initialized .sdata (0x8008cfa8). */
static u_short g_unpackHblanks = 0;

/* Module statics (.sbss 0x8008d0d0..0x8008d0f8), in their retail order.
 * names: runtime evidence, analysis/runtime/globals.json (write watches at level load
 * and in play: Tex_UploadSpriteFrame unpacks into the cursor and queues, Tex_FlushUploadQueue drains the queue
 * and rewinds the cursor every frame). */
/* Next free byte of the staging buffer g_unpackBuffer. */
static void *g_unpackCursor;
/* -1 once the cursor reaches g_unpackBufferEnd, cleared every frame. */
static short g_unpackFull;
/* Free upload records. */
static textureUpload_t *g_uploadFree;
/* Last queued upload. */
static textureUpload_t *g_uploadQueueTail;
/* First queued upload. */
static textureUpload_t *g_uploadQueue;
/* The upload records (a heap block, Tex_UploadPoolAlloc). */
static textureUpload_t *g_uploadPool;

/*
==================
Tex_UploadPoolAlloc
==================
*/
/* name: static evidence, analysis/runtime/functions.json */
void Tex_UploadPoolAlloc( void )
{
    g_uploadPool = Heap_Alloc( UPLOAD_RECORD_COUNT * sizeof( textureUpload_t ) );
}

/*
==================
Tex_ResetUploadQueue
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Tex_ResetUploadQueue( void )
{
    textureUpload_t *record = g_uploadPool;
    short            i;

    g_unpackFull = 0;
    g_uploadQueue = 0;
    g_uploadQueueTail = 0;
    g_uploadFree = 0;
    g_unpackCursor = g_unpackBuffer;
    for ( i = 0; i < UPLOAD_RECORD_COUNT; i++, record++ ) {
        record->queued = 0;
        record->next = g_uploadFree;
        g_uploadFree = record;
    }
}

/*
==================
Tex_FlushUploadQueue
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
void Tex_FlushUploadQueue( void )
{
    textureUpload_t *upload;

    while ( ( upload = g_uploadQueue ) != 0 ) {
        if ( upload->queued ) {
            g_uploadQueue = upload->queued;
            upload->queued = 0;
            upload->next = g_uploadFree;
            g_uploadFree = upload;
        } else {
            g_uploadQueue = 0;
            g_uploadQueueTail = 0;
            upload->next = g_uploadFree;
            g_uploadFree = upload;
        }
        LoadImage( &upload->rect, upload->pixels );
    }
    g_unpackFull = 0;
    g_unpackCursor = g_unpackBuffer;
}

/*
==================
Tex_UploadCanQueuePair
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
int Tex_UploadCanQueuePair( sprite_t *first, sprite_t *second )
{
    textureUpload_t *freeList = g_uploadFree;
    int              a, b;

    if ( freeList == 0 || freeList->next == 0 )
        return 0;
    a = first->flags & SPRITE_FLAG_PACKED;
    b = second->flags & SPRITE_FLAG_PACKED;
    if ( a && b ) {
        if ( g_unpackCursor != g_unpackBuffer )
            return 0;
    } else if ( ( a || b ) && g_unpackFull ) {
        return 0;
    }
    return -1;
}

/*
==================
Tex_UploadSpriteFrame
==================
*/
/* name: SPRITE.H. The two arms are written in different statement orders, and only the
 * uncompressed one ends with its own queue tail and return: that keeps retail's layout, where
 * the compressed arm's queue stores stay separate from the uncompressed arm's. */
/* name: runtime evidence, analysis/runtime/functions.json */
int Tex_UploadSpriteFrame( sprite_t *sprite, short mode )
{
    textureUpload_t *upload = g_uploadFree;
    long             start;

    if ( upload == 0 )
        return 0;
    if ( ( upload->compressed = sprite->flags & SPRITE_FLAG_PACKED ) ) {
        if ( g_unpackFull )
            return 0;
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        g_uploadFree = upload->next;
        upload->rect.h = sprite->frame->h;
        upload->pixels = g_unpackCursor;
        start = GetRCnt( RCntCNT1 );
        if ( mode == 0 )
            UnpackRLE( (u_char *)sprite->frame->pixels, g_unpackCursor );
        else
            UnpackLZ( (u_char *)sprite->frame->pixels, g_unpackCursor );
        g_unpackHblanks = GetRCnt( RCntCNT1 ) - start + g_unpackHblanks;
        if ( g_unpackCursor == g_unpackBufferEnd )
            g_unpackFull = -1;
        else
            g_unpackCursor = g_unpackBufferEnd;
        upload->next = g_uploadQueueTail;
        if ( g_uploadQueueTail == 0 )
            g_uploadQueue = upload;
        else
            g_uploadQueueTail->queued = upload;
    } else {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        upload->rect.h = sprite->frame->h;
        g_uploadFree = upload->next;
        upload->pixels = sprite->frame->pixels;
        upload->next = g_uploadQueueTail;
        if ( g_uploadQueueTail == 0 )
            g_uploadQueue = upload;
        else
            g_uploadQueueTail->queued = upload;
        g_uploadQueueTail = upload;
        return -1;
    }
    g_uploadQueueTail = upload;
    return -1;
}

/*
==================
Tex_UploadBossSpriteFrame
==================
*/
/* name: SPRITE.H. Tex_UploadSpriteFrame for the boss sprite sets: one encoding, unpacked into
 * g_bossUnpackBuffer, which is never full. */
/* name: static evidence, analysis/runtime/functions.json */
int Tex_UploadBossSpriteFrame( sprite_t *sprite )
{
    textureUpload_t *upload = g_uploadFree;
    long             start;

    if ( upload == 0 )
        return 0;
    if ( ( upload->compressed = sprite->flags & SPRITE_FLAG_PACKED ) ) {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        g_uploadFree = upload->next;
        upload->rect.h = sprite->frame->h;
        upload->pixels = g_bossUnpackBuffer;
        start = GetRCnt( RCntCNT1 );
        UnpackRLE( (u_char *)sprite->frame->pixels, g_bossUnpackBuffer );
        g_unpackHblanks = GetRCnt( RCntCNT1 ) - start + g_unpackHblanks;
        upload->next = g_uploadQueueTail;
        if ( g_uploadQueueTail == 0 )
            g_uploadQueue = upload;
        else
            g_uploadQueueTail->queued = upload;
    } else {
        upload->rect.x = sprite->slot->x;
        upload->rect.y = sprite->slot->y;
        upload->rect.w = (short)sprite->frame->w >> 1;
        upload->rect.h = sprite->frame->h;
        g_uploadFree = upload->next;
        upload->pixels = sprite->frame->pixels;
        upload->next = g_uploadQueueTail;
        if ( g_uploadQueueTail == 0 )
            g_uploadQueue = upload;
        else
            g_uploadQueueTail->queued = upload;
        g_uploadQueueTail = upload;
        return -1;
    }
    g_uploadQueueTail = upload;
    return -1;
}
