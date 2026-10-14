/* movie.c -- movie playback, from the lineage of Sony's STR samples (SAMPLE/MOVIE/ANIM/MAIN.C:
 * strInit, strCallback, strNext, strNextVlc, strSync) */
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
#define VLC_BUFF_SIZE ( 320 / 2 * 256 )
#define bound( val, n ) ( ( ( ( val ) - 1 ) / ( n ) + 1 ) * ( n ) )
/* An MDEC macroblock is 16 by 16 pixels; at 24 bits per pixel its 16 pixels span 24 VRAM
 * halfwords, three for each pair of pixels. names invented */
#define MACROBLOCK_SIZE 16
#define MACROBLOCK_WIDTH_RGB24 24
#define RGB24_PAIR_HALFWORDS 3
/* A raw (mode 2) CD sector in bytes. name invented */
#define CD_RAW_SECTOR_SIZE 2352
/* movieDecode_t.endScale is in eighths of the stream's frames. name invented */
#define MOVIE_END_SCALE_ONE 8
/* CdControlB's parameter buffer. name invented */
#define CD_PARAM_SIZE 4
/* Image buffer bytes allocated per frame row (24-bit and 16-bit). names invented */
#define MOVIE_IMAGE_ROW_BYTES_RGB24 96
#define MOVIE_IMAGE_ROW_BYTES 64
/* Ticks before a skippable record lets Start or X skip it. name invented */
#define MOVIE_SKIP_DELAY 300
/* DecDCTin mode bit set from movieStreamRecord_t.decodeFlag (bit 0 is 24-bit output). name invented */
#define DCT_MODE_DECODE_FLAG 2

/* name: inferred from the sample's DECENV "dec" */
extern movieDecode_t g_movieDecode;
short Movie_ShowFrame( movieDecode_t *dec );

/* name: inferred from the stream records Movie_Play plays by index */
static movieStreamRecord_t g_movieRecords[] = {
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 16, FILE_PROBE_STR, 0, 0, 1071, -1, 0, 8, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_INTRO_STR, 0, 0, 13500, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH1_STR, 0, 0, 710, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH2_STR, 0, 0, 1010, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH3_STR, 0, 0, 670, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH4_STR, 0, 0, 430, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH5_STR, 0, 0, 770, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 40, FILE_DEATH6_STR, 0, 0, 720, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 16, FILE_ACCLAIM_STR, 0, 0, 1910, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 16, FILE_LEGAL_STR, 0, 0, 1900, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 0, 240, 480, 240 } }, 40, FILE_S1_2_STR, 0, 0, 6500, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 0, 240, 480, 240 } }, 40, FILE_S2_3_STR, 0, 0, 8700, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 0, 240, 480, 240 } }, 40, FILE_GAMEOVER_STR, 0, 0, 9319, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 0, FILE_FOX_STR, 0, 0, 1410, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame },
    { { { 0, 0, 480, 240 }, { 480, 0, 480, 240 } }, 0, FILE_FOX_STR, 0, 0, 1410, -1, 0, 7, 0, -1, { 0, 0 }, Movie_ShowFrame }
};
/* names: inferred from SetDefDispEnv/SetDefDrawEnv in Movie_ShowFrame */
extern DISPENV g_movieDisp;
extern DRAWENV g_movieDraw;
/* Set once the stream reaches its end frame; -1 from the stream's end callback.
 * name: inferred from that */
extern int g_movieFinished;
/* name: inferred from strCallback, which narrows the first slice of a frame by it */
extern int g_firstSlice;
/* LIBCD streaming global (asm/lib/libcd_c_011.s, Runtime 3.3 COMMON); LIBCD.H does not declare it. */
extern int StCdIntrFlag;
/* name: inferred from its division by the 2352-byte raw sector size */
extern int g_streamBytes;
/* name: inferred from strNext, which sets g_movieFinished at this frame */
extern int g_movieEndFrame;
/* names: inferred from strNext, which copies the sector header's width and height */
int g_frameWidth = 0;
int g_frameHeight = 0;
/* name: inferred from strNext computing g_movieEndFrame once while it is 0 */
int g_endFrameReady = 0;
/* name: inferred from Movie_ShowFrame, which allows skipping 300 ticks after it */
int g_movieStartTime = 0;
/* name: inferred from strNext, which divides it by the sectors per frame */
int g_movieSectors = 0;
/* name: inferred from the sector header's frameCount, stored per frame */
int g_movieFrame = 0;
/* identity-TODO: Movie_Play stores 0; no jal, pointer word, lui/lo pair or $gp operand in retail reaches it other than that store. */
short g_8d058 = -1;
/* name: inferred from its increment per VSync in Movie_ShowFrame */
int g_movieVsyncs = 0;
/* Runtime 3.3 LIBPRESS.H omits this LIBPRESS export. */
extern void DecDCToutCallback( void ( *func )() );
/* name: inferred from Movie_Play, which plays a record through it */
extern short Movie_PlayRecord( movieStreamRecord_t *record );

/* Seek target of the movie stream. name: inferred from the CdlSeekL in strKickCD */
extern CdlLOC g_movieLoc;

/* name: inferred from CdSearchFile, which fills it with the record's stream file */
extern CdlFILE g_movieFile;
extern void Movie_SetupDisplay( const movieStreamRecord_t *record );

/*
==================
strKickCD
==================
*/
/* name: from the lineage sample (strKickCD), which seeks and starts streaming */
void strKickCD( void )
{
    while ( CdControl( CdlSeekL, (u_char *)&g_movieLoc, 0 ) == 0 )
        ;
    while ( CdRead2( MOVIE_MODE_STREAM | CdlModeSpeed | CdlModeRT ) == 0 )
        ;
}

/*
==================
strInit
==================
*/
/* name: from the lineage sample (strInit) */
void strInit( const movieStreamRecord_t *record, void ( *decodeCallback )(), int ( *streamCallback )() )
{
    DecDCTReset( 0 );
    g_movieFinished = 0;
    DecDCToutCallback( decodeCallback );
    StSetRing( g_movieDecode.ringbuf, RING_SIZE );
    StSetStream( record->mode, record->startFrame, record->endFrame, 0, streamCallback );
    strKickCD();
}

/*
==================
strCallback
==================
*/
/* name: from the lineage sample (strCallback) */
void strCallback( void )
{
    int mod;

    if ( g_movieDecode.rgb24 && StCdIntrFlag ) {
        StCdInterrupt();
        StCdIntrFlag = 0;
    }
    g_movieDecode.slice.y += g_movieDecode.yOffset;
    LoadImage( &g_movieDecode.slice, (u_long *)g_movieDecode.imgbuf );
    g_movieDecode.slice.y -= g_movieDecode.yOffset;
    if ( g_movieDecode.rgb24 )
        mod = g_movieDecode.rect[g_movieDecode.rectid].w % MACROBLOCK_WIDTH_RGB24;
    else
        mod = g_movieDecode.rect[g_movieDecode.rectid].w % MACROBLOCK_SIZE;
    if ( g_firstSlice && mod ) {
        if ( g_movieDecode.rgb24 )
            g_movieDecode.slice.x += mod * RGB24_PAIR_HALFWORDS / 2;
        else
            g_movieDecode.slice.x += mod;
        g_firstSlice = 0;
    } else {
        if ( g_movieDecode.rgb24 )
            g_movieDecode.slice.x += MACROBLOCK_WIDTH_RGB24;
        else
            g_movieDecode.slice.x += MACROBLOCK_SIZE;
    }
    if ( g_movieDecode.slice.x < g_movieDecode.rect[g_movieDecode.rectid].x + g_movieDecode.rect[g_movieDecode.rectid].w ) {
        if ( g_movieDecode.rgb24 )
            DecDCTout( (u_long *)g_movieDecode.imgbuf, bound( g_movieDecode.slice.w, MACROBLOCK_WIDTH_RGB24 )
                                                           * bound( g_movieDecode.slice.h, MACROBLOCK_SIZE ) / 2 );
        else
            DecDCTout( (u_long *)g_movieDecode.imgbuf, bound( g_movieDecode.slice.w, MACROBLOCK_SIZE )
                                                           * bound( g_movieDecode.slice.h, MACROBLOCK_SIZE ) / 2 );
    } else {
        g_movieDecode.isdone = 1;
        g_firstSlice = 1;
        g_movieDecode.rectid = g_movieDecode.rectid ? 0 : 1;
        g_movieDecode.slice.x = g_movieDecode.rect[g_movieDecode.rectid].x;
        g_movieDecode.slice.y = g_movieDecode.rect[g_movieDecode.rectid].y;
    }
}

/*
==================
strNext
==================
*/
/* name: from the lineage sample (strNext) */
u_long *strNext( void )
{
    RECT      rect;
    u_long   *addr;
    StHEADER *sector;
    int       cnt = WAIT_TIME;

    while ( StGetNext( &addr, (u_long **)&sector ) ) {
        if ( --cnt == 0 )
            return 0;
    }
    if ( addr[0] != sector->dummy1 || addr[1] != sector->dummy2 ) {
        StFreeRing( addr );
        return 0;
    }
    if ( g_endFrameReady == 0 ) {
        g_movieEndFrame = g_streamBytes / CD_RAW_SECTOR_SIZE;
        g_movieEndFrame = g_movieSectors;
        g_movieEndFrame = g_movieEndFrame / sector->nSectors;
        g_movieEndFrame = g_movieDecode.endScale * g_movieEndFrame / MOVIE_END_SCALE_ONE;
        g_endFrameReady = 1;
        g_movieEndFrame = g_movieDecode.endOffset + g_movieEndFrame;
    }
    g_movieFrame = sector->frameCount;
    if ( sector->frameCount >= g_movieEndFrame )
        g_movieFinished = 1;
    if ( g_frameWidth != sector->width || g_frameHeight != sector->height ) {
        g_frameWidth = sector->width;
        g_frameHeight = sector->height;
        rect.x = 0;
        rect.y = 0;
        rect.w = g_frameWidth;
        rect.h = g_frameHeight;
        ClearImage( &rect, 0, 0, 0 );
    }
    if ( g_movieDecode.rgb24 )
        g_movieDecode.rect[0].w = g_movieDecode.rect[1].w = g_frameWidth * RGB24_PAIR_HALFWORDS / 2;
    else
        g_movieDecode.rect[0].w = g_movieDecode.rect[1].w = g_frameWidth;
    g_movieDecode.rect[0].h = g_movieDecode.rect[1].h = g_frameHeight;
    g_movieDecode.slice.h = g_frameHeight;
    return addr;
}

/*
==================
strNextVlc
==================
*/
/* name: from the lineage sample (strNextVlc) */
int strNextVlc( void )
{
    u_long *next;
    int     count = WAIT_TIME;

    while ( ( next = strNext() ) == 0 ) {
        if ( --count == 0 )
            return -1;
    }
    g_movieDecode.vlcid ^= 1;
    DecDCTvlc( next, g_movieDecode.vlcbuf[g_movieDecode.vlcid] );
    StFreeRing( next );
    return 0;
}

/*
==================
strSync
==================
*/
/* name: from the lineage sample (strSync) */
void strSync( void )
{
    volatile u_long cnt = WAIT_TIME;

    while ( g_movieDecode.isdone == 0 ) {
        if ( --cnt == 0 ) {
            g_movieDecode.isdone = 1;
            g_movieDecode.rectid = g_movieDecode.rectid ? 0 : 1;
            g_movieDecode.slice.x = g_movieDecode.rect[g_movieDecode.rectid].x;
            g_movieDecode.slice.y = g_movieDecode.rect[g_movieDecode.rectid].y;
        }
    }
    g_movieDecode.isdone = 0;
}

/*
==================
Movie_EndCallback
==================
*/
/* name: inferred from it ending playback through g_movieFinished */
int Movie_EndCallback( void )
{
    g_movieFinished = -1;
    return 0;
}

/*
==================
Movie_PlayRecord

Plays one stream record: finds the file, allocates the ring, image and VLC
buffers, then decodes frames until the stream ends or update says stop.
==================
*/
short Movie_PlayRecord( movieStreamRecord_t *record )
{
    u_char param[CD_PARAM_SIZE];
    short  i;
    int    mode;
    short  result;

    g_firstSlice = 1;
    g_endFrameReady = 0;
    if ( CdSearchFile( &g_movieFile, (char *)record->name ) == 0 )
        return MOVIE_NOT_FOUND;
    g_movieLoc.minute = g_movieFile.pos.minute;
    g_movieLoc.second = g_movieFile.pos.second;
    g_movieLoc.sector = g_movieFile.pos.sector;
    g_streamBytes = g_movieFile.size;
    g_movieDecode.ringbuf = Heap_Alloc( RING_SIZE * SECTOR_SIZE * sizeof( u_long ) );
    if ( g_movieDecode.ringbuf == 0 )
        return MOVIE_NO_MEMORY;
    if ( record->mode )
        g_movieDecode.imgbuf = Heap_Alloc( record->rect[0].h * MOVIE_IMAGE_ROW_BYTES_RGB24 );
    else
        g_movieDecode.imgbuf = Heap_Alloc( record->rect[0].h * MOVIE_IMAGE_ROW_BYTES );
    if ( g_movieDecode.imgbuf == 0 )
        return MOVIE_NO_MEMORY;
    for ( i = 0; i < 2; i++ ) {
        g_movieDecode.vlcbuf[i] = Heap_Alloc( VLC_BUFF_SIZE * sizeof( u_long ) );
        if ( g_movieDecode.vlcbuf[i] == 0 )
            return MOVIE_NO_MEMORY;
    }
    g_movieFrame = 0;
    Movie_SetupDisplay( record );
    {
        int m = record->mode != 0;

        if ( record->decodeFlag )
            m |= DCT_MODE_DECODE_FLAG;
        mode = m;
    }
    strInit( record, strCallback, Movie_EndCallback );
    strNextVlc();
    result = 0;
    while ( g_movieFinished == 0 && result == 0 ) {
        DecDCTin( g_movieDecode.vlcbuf[g_movieDecode.vlcid], mode );
        if ( g_movieDecode.rgb24 )
            DecDCTout( (u_long *)g_movieDecode.imgbuf, bound( g_movieDecode.slice.w, MACROBLOCK_WIDTH_RGB24 )
                                                           * bound( g_movieDecode.slice.h, MACROBLOCK_SIZE ) / 2 );
        else
            DecDCTout( (u_long *)g_movieDecode.imgbuf, bound( g_movieDecode.slice.w, MACROBLOCK_SIZE )
                                                           * bound( g_movieDecode.slice.h, MACROBLOCK_SIZE ) / 2 );
        strNextVlc();
        strSync();
        result = record->update( &g_movieDecode, 0, 0 );
        g_movieDecode.param_34 = 0;
    }
    param[0] = CdlModeSpeed;
    CdControlB( CdlSetmode, param, 0 );
    DecDCToutCallback( 0 );
    CdDataCallback( 0 );
    CdReadyCallback( 0 );
    CdControlB( CdlPause, 0, 0 );
    return result;
}

/*
==================
Movie_ShowFrame

Waits a VSync, lets Start or X skip, and shows the finished frame buffer.
==================
*/
/* name: inferred from that */
short Movie_ShowFrame( movieDecode_t *dec )
{
    static unsigned short pad;
    int                   id;

    VSync( 0 );
    g_movieVsyncs++;
    if ( g_frameCount - g_movieStartTime > MOVIE_SKIP_DELAY || dec->skippable == 0 ) {
        pad = PadRead( 0 );
        if ( pad & PADstart || pad & PADRdown ) {
            if ( dec->skippable )
                return MOVIE_SKIPPED;
            return MOVIE_ABORTED;
        }
    }
    id = dec->rectid ? 0 : 1;
    SetDefDispEnv( &g_movieDisp, dec->rect[id].x, dec->rect[id].y, dec->rect[id].w, dec->rect[id].h );
    SetDefDrawEnv( &g_movieDraw, dec->rect[id].x, dec->rect[id].y, dec->rect[id].w, dec->rect[id].h );
    g_movieDraw.ofs[0] = 0;
    g_movieDraw.ofs[1] = 0;
    g_movieDraw.clip.x = 0;
    g_movieDraw.clip.y = 0;
    g_movieDraw.clip.h = VRAM_HEIGHT;
    g_movieDraw.clip.w = VRAM_WIDTH;
    if ( dec->rgb24 ) {
        g_movieDisp.isrgb24 = 1;
        g_movieDisp.disp.w = g_movieDisp.disp.w * 2 / RGB24_PAIR_HALFWORDS;
    }
    PutDispEnv( &g_movieDisp );
    PutDrawEnv( &g_movieDraw );
    SetDispMask( 1 );
    return 0;
}

/*
==================
Movie_Play
==================
*/
/* name: inferred from its body, which plays a movie by index between heap marks */
short Movie_Play( short index )
{
    movieStreamRecord_t *record = &g_movieRecords[index];
    short                slot;
    short                result;

    switch ( index ) {
    case MOVIE_PROBE:
    case MOVIE_ACCLAIM:
    case MOVIE_LEGAL:
    case MOVIE_FOX_TITLE:
        if ( record->skippable ) {
            g_movieStartTime = g_frameCount;
            break;
        }
    default:
        g_movieStartTime = g_frameCount - MOVIE_SKIP_DELAY;
        break;
    }
    slot = Heap_Mark();
    record->name = g_fileTable[record->file].name;
    g_8d058 = 0;
    g_movieSectors = record->endFrame;
    result = Movie_PlayRecord( record );
    Heap_Release( slot );
    record->skippable = 0;
    return result;
}
