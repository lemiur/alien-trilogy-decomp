/* comment 300 */
// line comment 400
#define COUNT 16
#define SCALED (COUNT * 4)
enum Kind { KIND_A = 3, KIND_B = 7 };
static const char *msg = "text 55";
static int quote = '7';
static const int table[] = { 10, 20, 30, 40 };
static struct Pair { int a; int b; } pairs[2] = {
    { 11, 22 },
    { 33, 44 }
};
static float scale = 0.5f;
int fn(int v, int *a)
{
    int i;
    for (i = 0; i < v; i++) {
        a[i] = -1;
    }
    v = v * 2 + 1 - 0;
    v = v / 2;
    v += sizeof(char[16]) + sizeof(a[3]);
    return v + 0x30 + 1.5e3;
}
