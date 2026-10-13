/* Password record: game progress packed into 20 bytes, checksummed and spelled
 * as 5-bit codes through the password key table. */
typedef struct PasswordRecord {
    unsigned short field_0;
    unsigned short field_2;
    unsigned char field_4;
    unsigned char checksum;
    unsigned char options;
    unsigned char field_7;
    unsigned char field_8;
    unsigned char field_9;
    unsigned short field_a;
    unsigned short field_c;
    unsigned char field_e;
    unsigned char field_f;
    unsigned char mode;
    unsigned char flags;
    unsigned char field_12;
    unsigned char field_13;
} PasswordRecord;

/* identity-TODO: progress fields of the game-state block that other units
 * still name one global at a time (g_9a036, g_9a040, ...). */
typedef struct ProgressState {
    short field_34;
    short level;
    short reserved_38[2];
    int flags;
    short field_40;
    short field_42;
    short reserved_44;
    short field_46;
    short field_48;
    short field_4a;
    short field_4c;
    short field_4e;
    short field_50;
    short field_52;
    short field_54;
    short field_56;
    short field_58;
} ProgressState;

extern ProgressState g_9a034;
extern unsigned char g_739c8[32];
extern char g_a0f6c[33];
extern short f_17760(void);
extern short f_17780(void);
extern unsigned char f_48598(const unsigned char *bytes, int length);
extern short f_471d0(const unsigned char *packed, unsigned char *decoded, short byte_count);

void f_47584(PasswordRecord *record, int mode)
{
    unsigned char codes[33];
    unsigned char options;
    int flags;
    int level;
    short count;
    short i;
    short length;

    options = f_17760() & 3;
    options |= (f_17780() & 3) << 2;
    flags = 0;
    if (g_9a034.flags & 4)
        flags |= 1;
    if (g_9a034.flags & 8)
        flags |= 2;
    if (g_9a034.flags & 0x10)
        flags |= 4;
    if (g_9a034.flags & 0x20)
        flags |= 8;
    if (g_9a034.flags & 0x40)
        flags |= 0x10;
    switch (g_9a034.level) {
    default: level = 0; break;
    case 0: level = 0; break;
    case 1: level = 1; break;
    case 2: level = 2; break;
    case 4: level = 3; break;
    case 3: level = 4; break;
    }
    record->field_0 = g_9a034.field_40;
    record->field_2 = g_9a034.field_42 & 0x3fff;
    record->field_4 = g_9a034.field_54;
    record->field_7 = g_9a034.field_46;
    record->options = options;
    record->field_8 = g_9a034.field_4a;
    record->field_9 = g_9a034.field_58;
    record->field_a = g_9a034.field_4c;
    record->field_c = g_9a034.field_52;
    record->field_e = g_9a034.field_48;
    record->field_f = g_9a034.field_4e;
    record->mode = mode;
    flags |= level << 5;
    record->flags = flags;
    record->field_12 = g_9a034.field_50;
    record->field_13 = g_9a034.field_56;
    record->checksum = 0;
    record->checksum = f_48598((unsigned char *)record, 20);
    count = f_471d0((unsigned char *)record, codes, 20);
    length = 0;
    for (i = 0; i < count; i++)
        g_a0f6c[length++] = g_739c8[codes[i]];
    g_a0f6c[length] = 0;
}
