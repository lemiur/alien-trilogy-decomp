/* alpha.c -- fixture unit: one function per rule check (tools/test_rules.py) */
#include "ALPHA.H"

typedef unsigned char u8;

extern int g_beta_total;
extern int g_unowned;
extern int g_header_only;
static int alpha_forward(int n);

#define SWAP_ONCE(a, b) do { int t_ = (a); (a) = (b); (b) = t_; } while (0)

/* ===== alpha_extern_user reads another unit's global through a local extern ===== */
int alpha_extern_user(void)
{
    return g_beta_total + g_unowned;
}

/* ===== alpha_header_user reads a global its header already declares ===== */
int alpha_header_user(void)
{
    return g_header_only;
}

/* ===== alpha_forward is declared above ===== */
static int alpha_forward(int n)
{
    return n;
}

/* ===== alpha_offset reads a member by byte offset ===== */
short alpha_offset(Point *p)
{
    return *(short *)((u8 *)p + 2);
}

/* ===== alpha_int_step steps a pointer through an integer by a constant ===== */
Point *alpha_int_step(Point *p)
{
    return (Point *)((unsigned int)p + sizeof(Point));
}

/* ===== alpha_relocate adds a run-time base to a pointer (not an offset access) ===== */
Point *alpha_relocate(Point *p, int base)
{
    return (Point *)((unsigned int)p + base);
}

/* ===== alpha_volatile_local keeps a volatile carrier ===== */
int alpha_volatile_local(Point *p)
{
    volatile Point *q = p;

    return q->x;
}

/* ===== alpha_volatile_cast forces a load through a volatile cast ===== */
int alpha_volatile_cast(Point *p)
{
    return *(volatile short *)&p->x;
}

/* ===== alpha_vblank polls the allowed IRQ counter ===== */
void alpha_vblank(void)
{
    while (g_vblank_count == 0)
        ;
}

/* ===== alpha_unproven polls a volatile without evidence ===== */
int alpha_unproven(void)
{
    return g_unproven_flag;
}

/* ===== alpha_dummy declares a frame-size local ===== */
int alpha_dummy(int n)
{
    int dummy[4];
    int result;

    result = n;
    return result;
}

/* ===== alpha_empty_if has an empty if body ===== */
void alpha_empty_if(int n)
{
    if (n) {
    }
}

/* ===== alpha_bare_semicolon has a stray statement ===== */
void alpha_bare_semicolon(int n)
{
    n++;;
}

/* ===== alpha_do_while has a macro body pasted in ===== */
void alpha_do_while(int n)
{
    do {
        n++;
    } while (0);
}

/* ===== alpha_self_assign assigns a member to itself ===== */
void alpha_self_assign(Point *p)
{
    p->x = p->x;
}

/* ===== alpha_neutral adds zero and ors zero ===== */
int alpha_neutral(int n)
{
    n |= 0;
    return n + 0;
}

/* ===== alpha_asm has an inline assembly body ===== */
void alpha_asm(void)
{
    __asm__("nop");
}

/* ===== alpha_goto jumps ===== */
int alpha_goto(int n)
{
    if (n)
        goto done;
    n = 1;
done:
    return n;
}

/* ===== f_80012345 keeps its address name ===== */
int f_80012345(int n)
{
    return n * 0x10;
}

/* round 2: this comment is history */
/* ===== alpha_swap uses a one-use do-while macro ===== */
void alpha_swap(int *a, int *b)
{
    SWAP_ONCE(*a, *b);
}

#if 0
int alpha_parked(void)
{
    return 0;
}
#endif

/* ===== alpha_knr keeps its old-style definition ===== */
int alpha_knr(n)
short n;
{
    return n + 0;
}

/* MATCH-PENDING: R17, dummy local sized for the frame */
/* ===== alpha_pending is recorded as MATCH-PENDING by marker ===== */
int alpha_pending(int n)
{
    int frame[2];

    return n;
}

/* ===== alpha_pending_file is recorded in match_pending.json ===== */
int alpha_pending_file(int n)
{
    int frame[2];

    return n;
}

/* ===== alpha_revoked has a revoked exceptions.json entry ===== */
int alpha_revoked(int n)
{
    int frame[2];

    return n;
}

/* RULE-EXCEPTION: R14, the failure block sits after the shared store */
/* ===== alpha_excused_goto is excused by marker ===== */
int alpha_excused_goto(int n)
{
    if (n)
        goto fail;
    return 0;
fail:
    return n;
}

/* RULE-EXCEPTION: R4, an exception cannot excuse a [match] rule */
/* ===== alpha_bad_exception tries to excuse an offset access ===== */
short alpha_bad_exception(Point *p)
{
    return *(short *)((u8 *)p + 2);
}

/* ===== alpha_json_excused is excused in exceptions.json ===== */
int alpha_json_excused(int n)
{
    if (n)
        goto out;
    n = 2;
out:
    return n;
}

/* ===== neutral_helper is an inline helper outside the symbol map ===== */
static int neutral_helper(int n)
{
    return n * 1;
}

/* ===== alpha_helper_caller inherits its helper's finding ===== */
int alpha_helper_caller(int n)
{
    return neutral_helper(n);
}

/* ===== alpha_dead_local excuses its unused local directly above the declaration ===== */
int alpha_dead_local(int n)
{
    /* RULE-EXCEPTION: R17-dead-local, Point type and size as the callee's two points; ruled out: inline callee, struct copy */
    Point spare;

    return n;
}

/* RULE-EXCEPTION: R17-dead-local, 8-byte size matches the sibling's Point type;
   ruled out: an inlined helper, an aggregate temporary, an SDK macro local */
/* ===== alpha_dead_local_header carries the exception in its header ===== */
int alpha_dead_local_header(int n)
{
    Point spare;

    return n;
}

/* RULE-EXCEPTION: R17-dead-local, frame */
/* ===== alpha_dead_local_vague gives no evidence ===== */
int alpha_dead_local_vague(int n)
{
    Point spare;

    return n;
}

/* RULE-EXCEPTION: R17-dead-local, the Point type and size come from the callee's own point parameter */
/* ===== alpha_dead_local_unruled names no ruled-out alternative ===== */
int alpha_dead_local_unruled(int n)
{
    Point spare;

    return n;
}

/* ===== alpha_dead_local_misplaced puts the marker away from the declaration ===== */
int alpha_dead_local_misplaced(int n)
{
    Point spare;

    /* RULE-EXCEPTION: R17-dead-local, Point type and size as the callee's two points; ruled out: inline callee, struct copy */
    return n;
}

/* RULE-EXCEPTION: R17-dead-local, Point type and size as the callee's two points; ruled out: inline callee, struct copy */
/* ===== alpha_dead_local_other keeps its other R17 finding ===== */
int alpha_dead_local_other(int n)
{
    Point spare;

    ;
    return n;
}
