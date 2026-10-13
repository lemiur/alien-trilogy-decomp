#define SQUARE(x) ((x) * 16)
#include "x.h"
#if 3
#endif
enum E { A = 1 };
int g[10] = { 1, 2 };
int expr(int v, int *a)
{
    int local = 30;
    int arr[2] = { 31, 32 };
    v = v & 255;
    v = v >> 4;
    a[5] = -3;
    switch (v) {
    case 6:
        break;
    }
    v = call(v, 070, 7u);
    return v + 8 * sizeof(int);
}
