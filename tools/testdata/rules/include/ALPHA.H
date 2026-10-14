/* ALPHA.H -- fixture header for tools/test_rules.py */
#ifndef ALPHA_H
#define ALPHA_H

typedef struct Point {
    short x;
    short y;
} Point;

typedef struct Shared {
    int a;
} Shared;

extern volatile int g_vblank_count;
extern volatile int g_unproven_flag;
extern int beta_value(int n);
extern int g_header_only;

#endif
