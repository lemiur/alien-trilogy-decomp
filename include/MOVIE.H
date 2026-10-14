/*
 * Movie playback (src/movie.c).
 */
#ifndef MOVIE_H
#define MOVIE_H

#include <LIBGPU.H>

/* Movie_Play's index into the stream records (src/movie.c g_movieRecords).
 * names: from each record's stream file (FILE_*_STR); MOVIE_FOX_TITLE, the second FOX.STR
 * record, is the one the title sequence plays (invented) */
enum {
    MOVIE_PROBE,
    MOVIE_INTRO,
    MOVIE_DEATH1,
    MOVIE_DEATH2,
    MOVIE_DEATH3,
    MOVIE_DEATH4,
    MOVIE_DEATH5,
    MOVIE_DEATH6,
    MOVIE_ACCLAIM,
    MOVIE_LEGAL,
    MOVIE_S1_2,
    MOVIE_S2_3,
    MOVIE_GAMEOVER,
    MOVIE_FOX,
    MOVIE_FOX_TITLE
};

/* Movie_Play results. names invented: MOVIE_ABORTED when the player presses Start or X
 * (the title sequence then jumps to the title picture), MOVIE_SKIPPED when the press only
 * skips a skippable record */
enum {
    MOVIE_DONE,
    MOVIE_NO_MEMORY,
    MOVIE_ABORTED,
    MOVIE_NOT_FOUND,
    MOVIE_SKIPPED
};

/* Width of the movie display buffers (two side by side, or one above the other). name invented */
#define MOVIE_SCREEN_WIDTH 480

/* Stream record and decoder state (src/movie.c g_movieRecords, g_movieDecode); needs <sys/types.h> and <LIBGTE.H> before it */
/* The sample's DECENV: double VLC buffers, one image buffer, the ring
 * buffer, double transfer rectangles and the slice decoded per DecDCTout. */
typedef struct movieDecode_s {
    u_long  *vlcbuf[2];
    u_short *imgbuf;
    u_long  *ringbuf;
    int      vlcid;
    RECT     rect[2];
    int      rectid;
    RECT     slice;
    int      isdone;
    /* Invented names for the playback parameters that follow the sample's fields. */
    short param_34; /* identity-TODO: Movie_PlayRecord clears it after the first update; no other reader in movie.c or Movie_ShowFrame */
    short rgb24;
    short yOffset;
    short endScale;
    short endOffset;
    short skippable;
} movieDecode_t;

typedef struct movieStreamRecord_s movieStreamRecord_t;
struct movieStreamRecord_s {
    RECT          rect[2];
    short         yOffset;
    short         file;
    const char   *name;
    u_long        startFrame;
    u_long        endFrame;
    short         mode;
    short         decodeFlag;
    short         endScale;
    short         endOffset;
    short         skippable;
    unsigned char reserved2a[2];
    short ( *update )(); /* Movie_ShowFrame; Movie_PlayRecord passes (&g_movieDecode, 0, 0) */
};

extern short Movie_Play( short index );

#endif
