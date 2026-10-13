#define SQUARE(x) ((x) * 0x10)
#define PLAIN 0x10
#if 0x20
#endif
enum E { A = 1 };
struct S { int m; };
int g[0x10] = { 1, 2 };
int expr(int v)
{
    int local = 0x30;
    int arr[2] = { 0x31, 0x32 };
    struct S s;
    s.m = 0x40;
    v = v & 0xFF00;
    v = v + 0Xabu;
    g[0x4] = 0x50;
    switch (v) {
    case 0x60:
        break;
    }
    return call(v, 0x70);
}
int after_enum(void)
{
    return 0x80;
}
