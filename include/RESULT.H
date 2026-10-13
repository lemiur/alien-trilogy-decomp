/*
 * End-of-level results (the results screen's record, reached through g_level_result).
 * compute_level_results fills the three percentages; the screen counts shown[] up to them.
 * Names invented.
 */
#ifndef RESULT_H
#define RESULT_H

/* LevelResult.shown entries: the kills, secrets and targets percentages */
#define RESULT_PERCENT_COUNT 3

typedef struct LevelResult {
    short shown[RESULT_PERCENT_COUNT]; /* displayed kills, secrets and targets percentages */
    short kills;
    short secrets;
    short targets;
    int grade;
    short par;            /* target percentage the grade is judged against */
    short pad;
    unsigned char *cells; /* per-percent colours, set by build_result_colors */
} LevelResult;

extern LevelResult *g_level_result;

#endif
