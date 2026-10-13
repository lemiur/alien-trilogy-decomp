/*
 * Movie playback (src/movie.c).
 */
#ifndef MOVIE_H
#define MOVIE_H

/* movie_play's index into the stream records (src/movie.c g_movie_records).
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

/* movie_play results. names invented: MOVIE_ABORTED when the player presses Start or X
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

extern short movie_play(short index);

#endif
