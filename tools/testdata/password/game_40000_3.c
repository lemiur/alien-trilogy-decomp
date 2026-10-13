/* Inverse of f_471d0 (fresh_r1_06): packs one 5-bit code per input byte into bytes. */
void f_47404(const char *input, unsigned char *packed, short byte_count)
{
    const unsigned char *codes = (const unsigned char *)input;
    short i;

    for (i = 0; i < byte_count; i++) {
        unsigned char value = 0;
        switch (i % 5) {
        case 0:
            value = (codes[0] << 3) | ((codes[1] >> 2) & 7);
            codes += 1;
            break;
        case 1:
            value = ((codes[0] & 3) << 6) + ((codes[1] & 31) << 1) | ((codes[2] >> 4) & 1);
            codes += 2;
            break;
        case 2:
            value = ((codes[0] & 15) << 4) | ((codes[1] >> 1) & 15);
            codes += 1;
            break;
        case 3:
            value = ((codes[0] & 1) << 7) + ((codes[1] & 31) << 2) | ((codes[2] >> 3) & 3);
            codes += 2;
            break;
        case 4:
            value = ((codes[0] & 7) << 5) | (codes[1] & 31);
            codes += 2;
            break;
        }
        *packed++ = value;
    }
}
