/* fixture.c -- frozen input for tools/test_banners.py */
#include "FIXTURE.H"

/* A prototype comment is not a function comment. */
int Fx_Forward( int n );

/*
==================
Fx_Bare
==================
*/
int Fx_Bare( void )
{
    return 0;
}

/*
==================
Fx_Add

Adds two numbers.
==================
*/
int Fx_Add( int a, int b )
{
    return a + b;
}

/*
==================
Fx_Spawn

Spawns a thing and its update task. The K&R definition (short
parameter under the int prototype) gives retail's prologue.

A second paragraph survives.
==================
*/
/* name: runtime evidence, analysis/runtime/functions.json */
thing_t *Fx_Spawn( index )
short index;
{
    return 0;
}

/*
==================
Fx_Clear
==================
*/
/* name: inferred from its body: clears the thing
 * and its task */
void Fx_Clear( thing_t *thing )
{
    thing->task = 0;
}

/*
==================
Fx_Walk

Walks the list.
==================
*/
/* name: inferred from that */
static void Fx_Walk( thing_t *head )
{
    head = head->next;
}

/*
==================
f_12345

Second copy of Fx_Add; nothing distinguishes the two.
==================
*/
/* identity-TODO: no jal or pointer word in retail reaches it. */
int f_12345( int a, int b )
{
    return a + b;
}

/*
==================
Fx_Stride
==================
*/
/* name: lineage, analysis/forsaken_pc/tasker.c */
/* MATCH-PENDING: R4, byte-stride cast; no typed spelling matches */
/* RULE-EXCEPTION: R14, shape the bytes need */
void Fx_Stride( char *p, int stride )
{
    *( p + stride ) = 0;
}

/*
==================
Fx_Wrapped

A signature the formatter wrapped over two lines.
==================
*/
int Fx_Wrapped( int alpha, int beta,
                int gamma )
{
    return alpha + beta + gamma;
}

/*
==================
Fx_Long

A single very long description line that has to be re-wrapped because it runs past the hundredth
column, and keeps its words.
==================
*/
void Fx_Long( void )
{
}

/*
==================
Fx_Done

Already bannered.
==================
*/
void Fx_Done( void )
{
}

/* Separated from its function by a blank line, so it is not its description. */

/*
==================
Fx_Orphan
==================
*/
void Fx_Orphan( void )
{
}

struct fx_s {
    int a;
};

/*
==================
Fx_Mention

Starts with the marker in the middle of a sentence: see MATCH-PENDING in other units.
==================
*/
int Fx_Mention( void )
{
    return 1;
}
