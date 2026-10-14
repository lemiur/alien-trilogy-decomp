# Main module notes

`src/main.c` (map `config/main.json`) is the first module of the image: retail `.text`
`0x8001380c..0x80015cb0`, ending where Tasker.c (`src/tasker.c`) starts. Its `.sdata`
(`0x8008cd74..0x8008cdb8`), `.rdata` (from `0x800101e0`) and `.data` (from `0x8006a578`:
`g_clutSets`, `g_pageSets`, the random seed, `g_fileTable`) lead their sections.
`g_idx` (`Demo_ReadInput`, `0x800143f8`) is the first `.sdata` item, before `Sys_SetButtonLayout`'s
mode masks, and `g_resetHoldStart` (`Sys_ResetComboCheck`) follows `g_actionIndex`, so at least
`0x8001380c..0x800147f4` is one module; nothing marks a boundary before Tasker.c.

- `Sys_MenuDisplayTask` (236 bytes): the frame task. Its argument is a `TASK`; the final store sets
  `g_nextTask` to `task->Next`. The display selection matches only as a conditional
  expression (`g_display == g_renderWorkspace ? g_displayDefault : g_renderWorkspace`).
- `Sys_StartLevel`: main passes it the level (`a0 = g_level`), so it takes a parameter; the body
  reads `g_level` directly.
- `s_heapOverflow`, `s_errorTitle`, `s_errorFormat`: named arrays sized to their
  aligned extents. They may be string literals in the original (identity-TODO);
  `tools/section_layout.py` bounds the start of `.rdata` by the first datum a unit map
  names, which literals would not provide.
