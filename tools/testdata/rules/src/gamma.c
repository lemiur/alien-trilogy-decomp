/* gamma.c -- fixture unit: R4 byte-cursor checks (tools/test_rules.py) */
#include "ALPHA.H"

typedef unsigned char u8;
typedef unsigned long u_long;

typedef struct Block {
    int tag;
    int unknown;
    int count;
    int vertexCount;
} Block;

typedef struct Prim {
    u8 r0;
    u8 g0;
    u8 b0;
    u8 code;
} Prim;

typedef struct FileSlot {
    void *buffer;
    int size;
} FileSlot;

#define GPU_DATA_REGISTER ((volatile u_long *)0x1f801810)
#define GPU_STATUS_REGISTER (*(volatile u_long *)0x1f801814)

/* ===== gamma_member_size_cursor steps a byte cursor by the sizes of a header's members ===== */
int gamma_member_size_cursor(u8 *base)
{
    u8 *p;
    int count;

    p = base;
    p += sizeof(((Block *)p)->tag) + sizeof(((Block *)p)->unknown);
    count = *(int *)p;
    p += sizeof(((Block *)p)->count) + sizeof(((Block *)p)->vertexCount);
    return count + p[0];
}

/* ===== gamma_member_size_plain steps a char cursor by sizeof of a plain member ===== */
void gamma_member_size_plain(Block *b)
{
    char *p = (char *)b;

    p += sizeof(b->tag);
}

/* ===== gamma_member_size_assign steps a void cursor by an assignment ===== */
void *gamma_member_size_assign(void *p)
{
    p = p + sizeof(((Block *)p)->tag);
    return p;
}

/* ===== gamma_offsetof_step steps a byte cursor by an offsetof ===== */
u8 *gamma_offsetof_step(u8 *p)
{
    p = p + offsetof(Block, count);
    return p;
}

/* ===== gamma_null_base_offset steps a byte pointer by an address-of-null member ===== */
u8 *gamma_null_base_offset(u8 *p)
{
    return p + (int)&((Block *)0)->count;
}

/* ===== gamma_cast_step offsets a byte-pointer cast by a member's size ===== */
Block *gamma_cast_step(Block *b)
{
    Block *next = (Block *)((u8 *)b + sizeof(b->tag));

    return next;
}

/* ===== gamma_int_cursor steps an int cursor by a cast member size ===== */
int gamma_int_cursor(int *p)
{
    p += sizeof(((Block *)p)->tag) / sizeof(*p);
    return *p;
}

/* ===== gamma_deref_short reads through a short cast of a byte cursor plus a constant ===== */
short gamma_deref_short(u8 *p)
{
    return *(short *)(p + 2);
}

/* ===== gamma_deref_parenthesised reads through a cast of a parenthesised cursor ===== */
int gamma_deref_parenthesised(char *p)
{
    return *(int *)(p);
}

/* ===== gamma_member_through_cast reads a member through a struct cast of a byte cursor ===== */
int gamma_member_through_cast(u8 *p)
{
    return ((Block *)p)->count;
}

/* ===== gamma_index_through_cast indexes a cast of a void cursor ===== */
int gamma_index_through_cast(void *p)
{
    return ((int *)p)[1];
}

/* ===== gamma_local_cursor reads through a cast of a local byte cursor ===== */
int gamma_local_cursor(Block *b)
{
    unsigned char *q = (unsigned char *)b;

    return *(int *)q;
}

/* ===== gamma_typed reads and steps a typed pointer ===== */
int gamma_typed(Block *b)
{
    int count = b->count;

    b++;
    return count + b->tag + sizeof(b->tag);
}

/* ===== gamma_plain_steps steps a byte cursor by counts and whole-record sizes ===== */
int gamma_plain_steps(u8 *p)
{
    int n = *p++;

    p += 4;
    p += sizeof(Block);
    return n + p[1];
}

/* ===== gamma_word_copy copies a word between primitives ===== */
void gamma_word_copy(Prim *prim, u_long *uv, u8 *rgb)
{
    *(u_long *)&prim->r0 = *uv++;
    *(u_long *)&prim->r0 = *(u_long *)rgb;
}

/* ===== gamma_register writes and polls the GPU registers ===== */
u_long gamma_register(u_long word)
{
    *GPU_DATA_REGISTER = word;
    *(volatile u_long *)0x1f801810 = word;
    return GPU_STATUS_REGISTER;
}

/* ===== gamma_file_buffer parses an untyped file buffer chunk by chunk ===== */
int gamma_file_buffer(FileSlot *file)
{
    u8 *p;
    u8 *q;
    int count;

    p = file->buffer;
    p += 8;
    count = ((Block *)p)->count;
    q = p + 4;
    return count + *(int *)q + *(short *)p;
}
