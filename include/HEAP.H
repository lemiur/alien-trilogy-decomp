/*
 * The game heap (src/main.c):
 * one malloc block used as a stack. heap_alloc takes word-aligned bytes from the
 * low end, heap_free returns the most recent ones, and heap_mark/heap_release save
 * and restore the whole state. Names invented.
 */
#ifndef HEAP_H
#define HEAP_H

#define HEAP_SNAPSHOT_COUNT 8
/* init_heap asks malloc for HEAP_SIZE_MAX less one step, then one step less until it succeeds. names: invented */
#define HEAP_SIZE_MAX  0x177000
#define HEAP_SIZE_STEP 0x400
/* _stacksize (src/main.c), the stack the SN startup module sets up. name: invented */
#define STACK_SIZE 0x2000

/* name: inferred from use: heap_mark saves the three counters, heap_release restores them
 * (member names follow the globals they save) */
typedef struct HeapSnapshot {
    unsigned int used;
    unsigned int free;
    unsigned int next;
} HeapSnapshot;

/* names: runtime evidence, analysis/runtime/globals.json (title and level 1 samples:
 * used + free == size == 0x15c400 throughout; death walks next back down) */
/* heap_alloc and heap_free round sizes up to this (a word). name: invented */
#define HEAP_ALIGN 4

/* the next free byte; heap_alloc returns it and moves it up */
extern unsigned char *g_heap_next;
/* bytes allocated so far */
extern unsigned int g_heap_used;
/* bytes still free; heap_alloc stops with an error when too few */
extern unsigned int g_heap_free;
/* size of the malloc block init_heap claims */
extern unsigned int g_heap_size;
extern void *g_heap_allocation;
extern HeapSnapshot g_heap_snapshots[HEAP_SNAPSHOT_COUNT];
extern short g_heap_snapshot_current;

extern void init_heap(void);
extern void *heap_alloc(int bytes);
extern void heap_free(int bytes);
extern int heap_mark(void);
extern void heap_release(short slot);
extern void fatal_error(const char *message);

#endif
