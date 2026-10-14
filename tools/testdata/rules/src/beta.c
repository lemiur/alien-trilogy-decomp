/* beta.c -- fixture unit owning beta_value, clean apart from a duplicate type */
#include "ALPHA.H"

typedef struct Shared {
    long a;
} Shared;

typedef unsigned char u8;

int g_beta_total;

/* ===== beta_value returns its argument ===== */
int beta_value(int n)
{
    return n;
}

/* ===== beta_shared reads the unit's own Shared ===== */
int beta_shared(Shared *s)
{
    return s->a;
}

/* ===== beta_clean has nothing to report ===== */
int beta_clean(Point *p)
{
    int sum;

    sum = p->x + p->y;
    g_beta_total += sum;
    return sum;
}
