/*
 * End-of-level results (the results screen's record, reached through g_levelResult).
 * Menu_ComputeLevelResults fills the three percentages; the screen counts shown[] up to them.
 * Names invented. Needs LIBGTE.H (CVECTOR).
 */
#ifndef RESULT_H
#define RESULT_H

/* levelResult_t.shown entries: the kills, secrets and targets percentages */
#define RESULT_PERCENT_COUNT 3

typedef struct levelResult_s {
    short    shown[RESULT_PERCENT_COUNT]; /* displayed kills, secrets and targets percentages */
    short    kills;
    short    secrets;
    short    targets;
    int      grade;
    short    par; /* target percentage the grade is judged against */
    short    pad;
    CVECTOR *colors; /* per-percent colours (cd unused), set by Menu_BuildResultColors */
} levelResult_t;

extern levelResult_t *g_levelResult;

#endif
