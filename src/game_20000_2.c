#include <sys/types.h>
#include <LIBGTE.H>
#include <LIBGPU.H>
#include <SPRITEUV.H>
#include <SPRITE.H>

typedef unsigned char u8;
typedef unsigned int u32;

/* unpack_lz stream: a flag byte governs the next eight items (bit set: a copy, clear: a literal
 * byte); LZ_FLAGS_SENTINEL above it marks when a new one is due. A copy code from LZ_SHORT_COPY
 * up is a two-byte copy from LZ_SHORT_COPY_BASE - code back; a lower one holds the length in
 * its high nibble and the top bits of a 12-bit distance in LZ_DISTANCE_HIGH_MASK.
 * names: invented */
#define LZ_FLAGS_SENTINEL 0xff00
#define LZ_SHORT_COPY 0x60
#define LZ_SHORT_COPY_BASE 0x100
#define LZ_DISTANCE_HIGH_MASK 0xf
/* unpack_rle code byte: code >> RLE_VALUE_SHIFT is the value; a zero RLE_RUN_MASK field means a
 * count byte follows, otherwise the RLE_PAIR and RLE_SINGLE bits add two and one copies.
 * unpack_lz lengths: a short copy is LZ_SHORT_COPY_LENGTH bytes; a long one's length nibble
 * (code >> LZ_LENGTH_SHIFT) plus LZ_LENGTH_BASE, or, at LZ_LONG_LENGTH, a length byte plus
 * LZ_LONG_LENGTH_BASE; the distance's top bits sit LZ_DISTANCE_HIGH_SHIFT above its low byte.
 * names: invented */
#define RLE_VALUE_SHIFT 2
#define RLE_RUN_MASK 3
#define RLE_PAIR 2
#define RLE_SINGLE 1
#define LZ_SHORT_COPY_LENGTH 2
#define LZ_LENGTH_SHIFT 4
#define LZ_DISTANCE_HIGH_SHIFT 8
#define LZ_LONG_LENGTH 5
#define LZ_LONG_LENGTH_BASE 8
#define LZ_LENGTH_BASE 3

/* name: runtime evidence, analysis/runtime/functions.json */
int unpack_rle(u8 *src, u8 *dst)
{
    u8 *start = dst;
    int code;
    int value;
    int n;

    for (;;) {
        code = *src++;
        value = code >> RLE_VALUE_SHIFT;
        if ((code & RLE_RUN_MASK) == 0) {
            n = *src++;
            if (n == 0)
                return dst - start;
            for (; n; n--)
                *dst++ = value;
        } else {
            if (code & RLE_PAIR) {
                dst[0] = value;
                dst[1] = value;
                dst += 2;
            }
            if (code & RLE_SINGLE)
                *dst++ = value;
        }
    }
}

/* name: runtime evidence, analysis/runtime/functions.json */
void unpack_lz(u8 *src, u8 *dst)
{
    u32 flags = 0;
    int code;
    int length;
    int offset;

    for (;;) {
        flags >>= 1;
        if ((flags & LZ_FLAGS_SENTINEL) == 0)
            flags = *src++ | LZ_FLAGS_SENTINEL;
        if ((flags & 1) == 0) {
            *dst++ = *src++;
        } else {
            code = *src++;
            if (code >= LZ_SHORT_COPY) {
                offset = LZ_SHORT_COPY_BASE - code;
                length = LZ_SHORT_COPY_LENGTH;
            } else {
                length = code >> LZ_LENGTH_SHIFT;
                offset = (code & LZ_DISTANCE_HIGH_MASK) << LZ_DISTANCE_HIGH_SHIFT;
                offset |= *src++;
                if (offset == 0)
                    return;
                if (length == LZ_LONG_LENGTH)
                    length = *src++ + LZ_LONG_LENGTH_BASE;
                else
                    length += LZ_LENGTH_BASE;
            }
            offset = -offset;
            for (; length; length--) {
                *dst = dst[offset];
                dst++;
            }
        }
    }
}
