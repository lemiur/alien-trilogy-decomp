/* comment 0x1111 */
// line comment 0x2222
/* multi
   line 0x3333 */
#define PLAIN 0x10
#define MASKED (0x20 | 0x40u)
#define SPLIT \
    0x80 + \
    0x100
enum Mode { MODE_A = 0x1, MODE_B = 0x2UL };
typedef enum {
    FLAG_X = 0x4,
    FLAG_Y = 0x8
} Flag;
static const char *msg = "text 0x5555 \" 0x6666";
static int quote = '\'';
static int mask_char = '0x7777';
static const int table[4] = { 0xAAAA, 0xbbbb, 0xCCCCu, 0xDDDDL };
struct Pair { int a; int b; };
static struct Pair pairs[2] = {
    { 0x11, 0x22 },
    { 0x33, 0x44 }
};
static struct Pair named = { .a = 0x55, .b = { 0x66 } };
int dec = 10;
int fn(void) { return 077 + 1; }
