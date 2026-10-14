/*
 * The game heap (src/main.c):
 * one malloc block used as a stack. Heap_Alloc takes word-aligned bytes from the
 * low end, Heap_Free returns the most recent ones, and Heap_Mark/Heap_Release save
 * and restore the whole state. Names invented.
 */
#ifndef HEAP_H
#define HEAP_H

#define HEAP_SNAPSHOT_COUNT 8
/* Heap_Init asks malloc for HEAP_SIZE_MAX less one step, then one step less until it succeeds. names: invented */
#define HEAP_SIZE_MAX 0x177000
#define HEAP_SIZE_STEP 0x400
/* _stacksize (src/main.c), the stack the SN startup module sets up. name: invented */
#define STACK_SIZE 0x2000

/* name: inferred from use: Heap_Mark saves the three counters, Heap_Release restores them
 * (member names follow the globals they save) */
typedef struct heapSnapshot_s {
    unsigned int used;
    unsigned int free;
    unsigned int next;
} heapSnapshot_t;

/* names: runtime evidence, analysis/runtime/globals.json (title and level 1 samples:
 * used + free == size == 0x15c400 throughout; death walks next back down) */
/* Heap_Alloc and Heap_Free round sizes up to this (a word). name: invented */
#define HEAP_ALIGN 4

/* the next free byte; Heap_Alloc returns it and moves it up */
extern unsigned char *g_heapNext;
/* bytes allocated so far */
extern unsigned int g_heapUsed;
/* bytes still free; Heap_Alloc stops with an error when too few */
extern unsigned int g_heapFree;
/* size of the malloc block Heap_Init claims */
extern unsigned int   g_heapSize;
extern void          *g_heapAllocation;
extern heapSnapshot_t g_heapSnapshots[HEAP_SNAPSHOT_COUNT];
extern short          g_heapSnapshotCurrent;

extern void Heap_Init( void );
extern void *Heap_Alloc( int bytes );
extern void Heap_Free( int bytes );
extern int Heap_Mark( void );
extern void Heap_Release( short slot );
extern void Sys_FatalError( const char *message );

#endif
