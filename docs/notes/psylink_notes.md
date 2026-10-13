# PSYLINK feasibility

`tools/gate.py` links in Python. For `../sdk/psyq/bin/PSYLINK.EXE` to produce `SLUS_000.07` instead, all
of the following must hold. Values below are re-derived by `tools/section_layout.py` into
`config/section_layout.json` and `config/bss_layout.json`.

## Prerequisites

1. **Original translation-unit boundaries.** PSYLINK places each object's section contiguously, in object
   order. Every source object must hold exactly one original TU: its functions contiguous in `.text` and
   its `.rdata`, `.data`, `.sdata`, `.sbss` and `.bss` contributions contiguous in their sections. The
   registered units are recovery groupings: one unit's functions are scattered across `.text`, and some
   units' data sits in a different retail section from the one the source emits (see
   `section_conformance` in `builds/gate/provenance.json`).
2. **Section order and a leading module.** The section order must reproduce
   `.rdata .text .data .sdata .sbss .bss`. A hand-written module at `0x80010000..0x800101e0` must come
   first. It is code with tables, called from the game. It is not in `.text`, so it needs its own section
   or must be assembled into `.rdata`.
3. **Object and archive order.** Game objects must come first, then `2MBYTE.OBJ` (`.text` at
   `0x800588bc`, `.sbss` at `0x8008d300`), then the Runtime Library 3.3 archives. PSYLINK must pull the
   modules in the order the image shows.
4. **COMMON allocation.** PSYLINK must allocate the COMMON symbols, game and library interleaved, in the
   retail order from `0x8009603c` to `0x800a1958`.
5. **Linker identity.** The PSYLINK build must lay out sections, align them, and round the payload to
   0x800 bytes the way the original link did.

## What the evidence pins

- `.sdata` = `[0x8008cd74, 0x8008d088)`. This comes from 2MBYTE.OBJ's `sectstart(.sdata)` (also `$gp`) and
  `sectstart(.sbss)` operands.
- `.rdata` ends and `.text` starts at `0x8001380c`. The certified LIBC2 PRNT `.rdata` ends there, and the
  first matched function starts there.
- The `.bss` sections end and COMMON starts at `0x8009603c`. The LIBGPU FONT `.bss` ends there, and the
  first library COMMON symbol (`dc_cr`) is there. COMMON ends at `0x800a1958`, which is `sectend(.bss)`.
- The header's bss fields are zero. Startup clears `sectstart(.sbss)..sectend(.bss)` itself.
- The library link order of the 94 certified modules, from their `.text` addresses:
  - 2MBYTE, LIBSPU, LIBAPI, LIBCD, LIBETC, LIBGPU, LIBGTE, LIBPRESS, LIBC2, LIBCARD, LIBAPI;
  - LIBAPI stub modules are interleaved where they are first needed.

  The certified `.rdata`, `.data`, `.sdata`, `.sbss` and `.bss` sections appear in the same module order.
- Library COMMON symbols and their sizes come from the XBSS records of the 3.3 archives
  (`config/bss_layout.json`).

## What it does not pin

- **Game TU boundaries and order.** No unit object defines an uninitialised global, and no
  string-pool or static grouping has been tied to a TU yet.
- **The leading module's section name**, and whether `.rdata` starts at `0x80010000` or at `0x800101e0`.
- **`.text` end / `.data` start**, only bounded to `[0x8006a578, 0x8006a58c]`. Retail operand pairs name
  `0x8006a578` as data, but they come from unmatched code.
- **`.sbss` end / `.bss` start**, bounded to `[0x8008d318, 0x8008f6ec]`.
- **Uncertified library gaps.** For example, `0x80058980..0x800590a8` lies between 2MBYTE and LIBSPU
  S_GKS. The modules there, and so the exact archive pull order, are unknown.
- **PSYLINK's COMMON ordering rule.** Game and library COMMON symbols interleave (for example `dc_cr` at
  `0x8009603c`, a game global at `0x80096060`). The rule must be measured by linking small test objects
  with PSYLINK.
- **`f_616c0`** (unit `accessors2`) lies at `0x800616c0`, between library modules. It is probably
  a library module recovered as C, and its owner must be settled before an archive link.
