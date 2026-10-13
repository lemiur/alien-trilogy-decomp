/* Eight 5-bit codes per five input bytes; the final group is zero-padded. */
short f_471d0(const unsigned char *packed, unsigned char *decoded, short byte_count)
{
    short rounded_bytes;
    short output_count;
    short index;
    int emitted = 0;
    if (byte_count % 5)
        rounded_bytes = (short)(((byte_count + 4) / 5) * 5);
    else
        rounded_bytes = byte_count;
    output_count = (short)((rounded_bytes / 5) * 8);
    index = 0;
    if (output_count > 0) {
      do {
        unsigned char bytes[2];
        int value = 0;
        bytes[0] = 0;
        bytes[1] = 0;
        if (byte_count > 0) bytes[0] = packed[0];
        if (byte_count > 1) bytes[1] = packed[1];
        switch (index & 7) {
        case 0: value = bytes[0] >> 3; break;
        case 1: value = ((bytes[0] & 7) << 2) | (bytes[1] >> 6); --byte_count; ++packed; break;
        case 2: value = (bytes[0] >> 1) & 31; break;
        case 3: value = ((bytes[0] & 1) << 4) | (bytes[1] >> 4); --byte_count; ++packed; break;
        case 4: value = ((bytes[0] & 15) << 1) | (bytes[1] >> 7); --byte_count; ++packed; break;
        case 5: value = (bytes[0] >> 2) & 31; break;
        case 6: value = ((bytes[0] & 3) << 3) | (bytes[1] >> 5); --byte_count; ++packed; break;
        case 7: value = bytes[0] & 31; --byte_count; ++packed; break;
        }
        *decoded++ = (unsigned char)value;
        ++index;
        ++emitted;
      } while (index < output_count);
    }
    return (short)emitted;
}
