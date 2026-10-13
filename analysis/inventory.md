# Alien Trilogy SLUS_000.07 inventory

Executable SHA-256: `f424660920764ac70f697b5b909a42fe259a76e60acbf5b01c6ca3ee8c8f8e98`; 514,048 bytes at `0x80010000`; entry `0x800588C4`; header GP `0x00000000`. Startup code initializes GP to `0x8008CD74`.

Candidate starts: 1037; these include heuristic JAL/prologue discoveries and verified `AT.C` symbols. Candidate spans are not certified function sizes. `f_15564` is verified as 72 bytes through exclusive end `0x800155AC`. Runtime Library 3.3 full relocation-masked text matches: 57.

Direct JAL-looking words may occur in data; their targets remain heuristic unless independently supported. Header GP is zero; the nonzero GP value comes from startup code.

## First eight recovered source targets

| Function | Address | Start evidence | Candidate span | Verified size |
|---|---:|---|---:|---:|
| `f_13a1c` | `0x80013A1C` | compiler_verified_function_symbol | 40 | — |
| `f_143f8` | `0x800143F8` | compiler_verified_function_symbol | 68 | — |
| `f_15324` | `0x80015324` | compiler_verified_function_symbol | 60 | — |
| `f_15564` | `0x80015564` | compiler_verified_function_symbol | 212 | 72 |
| `f_157d0` | `0x800157D0` | compiler_verified_function_symbol | 68 | — |
| `f_15814` | `0x80015814` | compiler_verified_function_symbol | 76 | — |
| `f_15cc8` | `0x80015CC8` | compiler_verified_function_symbol | 96 | — |
| `f_15d28` | `0x80015D28` | compiler_verified_function_symbol | 40 | — |

## Runtime Library 3.3 matches

All entries below require relocation-value confirmation; these are not labeled byte exact.

- `COUNTER` from `LIBAPI.LIB` at `0x8005D120` (368 text bytes; 80 masked bytes).
- `ATOI` from `LIBC2.LIB` at `0x80069B0C` (348 text bytes; 32 masked bytes).
- `CTYPE` from `LIBC2.LIB` at `0x80069C68` (104 text bytes; 16 masked bytes).
- `MEMCHR` from `LIBC2.LIB` at `0x8006A3E0` (80 text bytes; 8 masked bytes).
- `MEMCPY` from `LIBC2.LIB` at `0x800697A4` (52 text bytes; 0 masked bytes).
- `PUTCHAR` from `LIBC2.LIB` at `0x8006A430` (168 text bytes; 44 masked bytes).
- `STRCAT` from `LIBC2.LIB` at `0x800697D8` (168 text bytes; 12 masked bytes).
- `STRCMP` from `LIBC2.LIB` at `0x8006996C` (100 text bytes; 8 masked bytes).
- `STRCPY` from `LIBC2.LIB` at `0x800699D0` (68 text bytes; 0 masked bytes).
- `STRLEN` from `LIBC2.LIB` at `0x8006993C` (48 text bytes; 4 masked bytes).
- `STRNCMP` from `LIBC2.LIB` at `0x80069A14` (128 text bytes; 8 masked bytes).
- `STRNCPY` from `LIBC2.LIB` at `0x80069A94` (120 text bytes; 12 masked bytes).
- `TODIGIT` from `LIBC2.LIB` at `0x80069CD0` (100 text bytes; 16 masked bytes).
- `PRINTF` from `LIBC2.LIB` at `0x80069880` (60 text bytes; 4 masked bytes).
- `PRNT` from `LIBC2.LIB` at `0x80069D44` (1692 text bytes; 220 masked bytes).
- `PUTS` from `LIBC2.LIB` at `0x8006A4D8` (80 text bytes; 16 masked bytes).
- `SETJMP` from `LIBC2.LIB` at `0x800698BC` (128 text bytes; 0 masked bytes).
- `MALLOC` from `LIBC2.LIB` at `0x80069748` (48 text bytes; 0 masked bytes).
- `TOC` from `LIBCD.LIB` at `0x80060568` (584 text bytes; 92 masked bytes).
- `EVENT` from `LIBCD.LIB` at `0x8005F94C` (264 text bytes; 68 masked bytes).
- `ISO9660` from `LIBCD.LIB` at `0x8005FB60` (2568 text bytes; 712 masked bytes).
- `C_005` from `LIBCD.LIB` at `0x8005FAD8` (136 text bytes; 68 masked bytes).
- `C_007` from `LIBCD.LIB` at `0x800614FC` (188 text bytes; 36 masked bytes).
- `C_008` from `LIBCD.LIB` at `0x800612D0` (64 text bytes; 8 masked bytes).
- `PAD` from `LIBETC.LIB` at `0x800615B8` (176 text bytes; 52 masked bytes).
- `VSYNC` from `LIBETC.LIB` at `0x800621D4` (484 text bytes; 160 masked bytes).
- `INTR` from `LIBETC.LIB` at `0x800616D0` (1812 text bytes; 528 masked bytes).
- `INTR_VB` from `LIBETC.LIB` at `0x800620A8` (300 text bytes; 88 masked bytes).
- `FONT` from `LIBGPU.LIB` at `0x80065788` (2760 text bytes; 464 masked bytes).
- `PRIM` from `LIBGPU.LIB` at `0x80064FF8` (1936 text bytes; 272 masked bytes).
- `MSC00` from `LIBGTE.LIB` at `0x80067FB0` (128 text bytes; 20 masked bytes).
- `MSC01` from `LIBGTE.LIB` at `0x80067340` (288 text bytes; 16 masked bytes).
- `MSC05` from `LIBGTE.LIB` at `0x80066E90` (128 text bytes; 0 masked bytes).
- `MSC06` from `LIBGTE.LIB` at `0x80067920` (656 text bytes; 0 masked bytes).
- `MTX` from `LIBGTE.LIB` at `0x80067460` (1216 text bytes; 0 masked bytes).
- `REG03` from `LIBGTE.LIB` at `0x800671C0` (384 text bytes; 0 masked bytes).
- `SMP` from `LIBGTE.LIB` at `0x80066F10` (688 text bytes; 0 masked bytes).
- `SMP_00` from `LIBGTE.LIB` at `0x800680E0` (528 text bytes; 0 masked bytes).
- `CMB_00` from `LIBGTE.LIB` at `0x80067F30` (128 text bytes; 0 masked bytes).
- `CMB_04` from `LIBGTE.LIB` at `0x80068360` (144 text bytes; 0 masked bytes).
- `CMB_05` from `LIBGTE.LIB` at `0x800683F0` (176 text bytes; 0 masked bytes).
- `CMB_16` from `LIBGTE.LIB` at `0x800682F0` (112 text bytes; 0 masked bytes).
- `NOM` from `LIBGTE.LIB` at `0x80067BB0` (896 text bytes; 0 masked bytes).
- `FGO_01` from `LIBGTE.LIB` at `0x800684FC` (656 text bytes; 60 masked bytes).
- `LIBPRESS` from `LIBPRESS.LIB` at `0x8006878C` (1640 text bytes; 444 masked bytes).
- `VLC` from `LIBPRESS.LIB` at `0x80068DF4` (868 text bytes; 172 masked bytes).
- `VLC2` from `LIBPRESS.LIB` at `0x80069158` (1520 text bytes; 628 masked bytes).
- `S_M_INT` from `LIBSPU.LIB` at `0x8005CDC0` (768 text bytes; 148 masked bytes).
- `S_M_UTIL` from `LIBSPU.LIB` at `0x8005989C` (232 text bytes; 32 masked bytes).
- `S_M_INIT` from `LIBSPU.LIB` at `0x8005C884` (84 text bytes; 36 masked bytes).
- `S_M_F` from `LIBSPU.LIB` at `0x8005C634` (124 text bytes; 24 masked bytes).
- `S_RRWA` from `LIBSPU.LIB` at `0x8005CD70` (80 text bytes; 32 masked bytes).
- `S_SKOWA` from `LIBSPU.LIB` at `0x8005C9F0` (52 text bytes; 8 masked bytes).
- `S_GKS` from `LIBSPU.LIB` at `0x800590A8` (148 text bytes; 28 masked bytes).
- `S_R` from `LIBSPU.LIB` at `0x8005C990` (96 text bytes; 20 masked bytes).
- `S_W` from `LIBSPU.LIB` at `0x8005C990` (96 text bytes; 20 masked bytes).
- `S_SCA` from `LIBSPU.LIB` at `0x8005BF9C` (916 text bytes; 208 masked bytes).

## Reproduction

From the workspace root, run `python alien_decomp/analysis/inventory.py`. The script reads the existing `shared-tools/psyqobj.py` parser but writes only this analysis directory.
