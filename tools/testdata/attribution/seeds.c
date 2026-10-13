/* Frozen fixture for test_gate's attribution test: f_15324 and g_idx as the seeds
 * unit held them, with no project headers so later header edits cannot break it. */
#include <sys/types.h>
#include <LIBCD.H>

typedef struct HeapFile {
    const char *name;
    unsigned int loaded_size;
    void *buffer;
    CdlFILE file;
} HeapFile;
extern HeapFile g_file_table[171];

short g_idx = 0;

void f_15324(int n) { CdControl(0x15, (unsigned char *)&g_file_table[n].file.pos, 0); }
