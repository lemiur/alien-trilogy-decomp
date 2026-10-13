# Main module notes

`src/main.c` (map `config/main.json`) is the first module of the image: retail `.text`
`0x8001380c..0x80015cb0`, ending where Tasker.c (`src/tasker.c`) starts. Its `.sdata`
(`0x8008cd74..0x8008cdb8`), `.rdata` (from `0x800101e0`) and `.data` (from `0x8006a578`:
`g_clut_sets`, `g_page_sets`, the random seed, `g_file_table`) lead their sections.
`g_idx` (`read_demo_input`, `0x800143f8`) is the first `.sdata` item, before `set_button_layout`'s
mode masks, and `g_reset_hold_start` (`reset_combo_check`) follows `g_action_index`, so at least
`0x8001380c..0x800147f4` is one module; nothing marks a boundary before Tasker.c.

- `menu_display_task` (236 bytes): the frame task. Its argument is a `TASK`; the final store sets
  `g_next_task` to `task->Next`. The display selection matches only as a conditional
  expression (`g_display == g_render_workspace ? g_display_default : g_render_workspace`).
- `level_start`: main passes it the level (`a0 = g_level`), so it takes a parameter; the body
  reads `g_level` directly.
- `s_heap_overflow`, `s_error_title`, `s_error_format`: named arrays sized to their
  aligned extents. They may be string literals in the original (identity-TODO);
  `tools/section_layout.py` bounds the start of `.rdata` by the first datum a unit map
  names, which literals would not provide.
