# Runtime evidence for globals, functions and constants

Observations of the retail game running in DuckStation, with the names, types and sizes they
support. Nothing here certifies bytes; names taken from it are marked in the sources
(`name: analysis/runtime/globals.json`, `identity-TODO`, `invented`).

| file | records |
|---|---|
| `globals.json` | what the game does with address-named (`g_xxxxx`) and weakly named globals: static accesses, runtime values, writers, and the curated finding |
| `functions.json` | for every address-named function (`f_xxxxx`) the sources named or declared: callers, runtime hits and arguments, and the name the evidence supports |
| `constants.json` | one answer per runtime question about a constant, flag, sound id, object or cell type, or struct member still named `unknown_*`: the value, the observation, the reader/writer functions, a recommended name and a confidence |

## Record format

`globals.json`, one record per address (`globals[]`):

| field | meaning |
|---|---|
| `address`, `current_name` | the storage and the name the sources use (`(unnamed)` for globals the sources do not declare) |
| `declared`, `region`, `static_size`, `max_size`, `static_accesses` | static context: declaration, section, size from `config/data.json` / `config/bss_layout.json`, and every lui/gp access in the retail image grouped by function (`r`/`w`/`a` + width + member offset) |
| `runtime_values` | per scenario: first/last value, distinct values (first 8), number of changes over the recorded frames, access width used |
| `runtime_writers` | per write-watch scenario: `function+offset@store_pc` of the stores that hit the watch (first three hits per address) |
| `proposed_name`, `proposed_type`, `proposed_size`, `confidence`, `kind`, `evidence`, `note` | the curated finding; `kind` is `rename`, `interior` (the address is a member or element of a larger object: spell it through that object), `new` (not declared yet), `confirm`, `unresolved` or `observed` (runtime data only) |

`functions.json`, one record per function (`functions[]`):

| field | meaning |
|---|---|
| `address`, `current_name`, `proposed_name` | the code and its names before and after the evidence |
| `kind` | `rename`; `alias` (the address already had a name in another unit: callers use it); `library` (a Runtime 3.3 function called by address); `unresolved` |
| `size`, `unit`, `callers`, `pointer_from` | static context: retail size, owning unit, `jal` callers and the functions that take its address (task callbacks, tables) |
| `confidence` | `high`: the static meaning is clear and the function ran at runtime in the role the name states, or the evidence is decisive on its own (the same address, a library function printing its own name, a dispatch-table slot, an asm unit's documented routine); `medium`: static meaning clear, not exercised; `low`: not settled, not applied |
| `evidence` | static evidence, then the experiments and census scenarios it ran in |
| `runtime` | per scenario: hits (first three, at most four), frame of each hit, callers, `a0..a3` of the first two, `v0` on return and RAM words the first call changed in `.data`/`.sdata`/`.bss` |

Confidence for globals: `high` = runtime observation of the behaviour the name states (or a
runtime layout fact for `interior`), plus static agreement; `medium` = static meaning with
runtime values consistent but the behaviour not exercised; `low` = value only.

## Method

1. **Static index.** Function ranges from the symbol maps and `analysis/inventory.json`, the
   `jal` call graph, lui/gp data references, pointers to code (task callbacks, dispatch
   tables) and referenced strings, from a lui/addiu/gp scan of the retail EXE.
2. **Value series.** From a save state an input script runs frame by frame while
   `0x8006a000..0x80074000` and `0x8008cd74..0x800a1960` (.sdata, .sbss, .bss, COMMON) are
   read every frame, and each global is reduced to the values above.
3. **Write watches.** Write breakpoints on the first word of every target, 40 at a time; each
   hit is resolved to the store instruction whose effective address is the watched word
   (DuckStation reports the PC one or two instructions after the store) and mapped to a
   function through the symbol maps, the `analysis/inventory.json` starts and the Runtime
   Library modules. To watch a struct member, break on **every byte** of it: DuckStation fires
   a memory breakpoint only when an access covers the breakpoint's exact address, so a
   breakpoint on a word does not see a byte or halfword access to its other bytes, and a
   global reported unwritten by a first-word watch may be written at another offset.
4. **Census.** Execute breakpoints on every worklist function at once from a save state with
   an input script; a hit records the frame, arguments and caller, and the first hit also a
   return breakpoint (on `ra` with the entry `sp`) for `v0` and a RAM diff. A function's
   breakpoint is removed after K hits. Scenarios: `title` (slot 8, idle with the attract
   demo), `title_menu`, `options` (slot 7), `password`, `levelload` (slot 9), `play`
   (slot 10), `death`, `queen` (slot 5), `l14` (slot 4). The `boot` scenario (reset, BIOS,
   intro) is not usable: the BIOS shell runs code at the same addresses as the game and
   floods the breakpoints, so boot-time functions are named from their bodies.
5. **Experiments.** Button presses with `input_sequence`, execute breakpoints, and RAM pokes
   into a loaded save state (for example `g_game_flags |= 0x900` to run the lamp ramp; a poke
   only touches the emulated RAM of a state that is reloaded afterwards). Weapons and
   ammunition poked into `g_player` and `g_weapon_slots`, live actors moved in front of the
   player, level warps (from slot 8, Cross starts a new game; an execute breakpoint on
   `password_store_level` stops before the level loads, `g_level` is written, and the
   briefing, map, sample bank and countdown follow it; the `G0LVLxx` password-keyboard route
   misses some levels and can land in the memory card menu), a map census (the MAP0 chunk of
   every `LxxxLEV.MAP` parsed with the layout of `level_parse_map`), and sound logging with
   execute breakpoints on `sound_play`, `sound_play_at`, `play_menu_sound` and `voice_play`.
   Pictures: the player is teleported in front of an object or cell and 25 frames are stepped
   before a screenshot (the game draws a frame behind).
6. **Merge.** The curated findings and the census dumps are merged into the three JSON files;
   a group's high and medium names are applied whole-word, with a provenance comment before
   each C definition.

The working scripts live in the git-ignored `builds/lane_tools/`, and the dumps they write land
in `builds/runtime/`. The emulator is driven through `tools/duckstation.py` (DuckStation's MCP
server, `http://127.0.0.1:2346/mcp`). Use 127.0.0.1, not `localhost` (about 2 s per request on
Windows), and keep inline reads at 16 KB or less (larger inline replies stall; the client
switches to a file read automatically).

Published GameShark codes and password cheats are leads, not evidence: each one is recorded as
an evidence string next to the poke, watch or write-breakpoint result that confirms or refutes
it.

## Save-state slots (DuckStation, game SLUS-00007)

The server only accepts slots 1-10; slots 1-3 are unused.

| slot | state |
|---|---|
| 4 | level 14 first frame of play, reached with `G0LVL14` |
| 5 | level 11 (Queen) mission briefing, reached with the password cheat `G0LVL11` from the options menu |
| 6 | level 4 (131 Med Lab), first frames of play after a level warp |
| 7 | options menu (from slot 8: Down, Cross), cursor on "Sound" |
| 8 | title main menu ("Start game" / "Options"), music started, idle counter running |
| 9 | level 1 mission briefing screen, level fully loaded ("Press X to continue") |
| 10 | level 1 first frame of play: health 100, pistol 15 + 2 clips (HUD 045), facing the start wall |

## Facts the records settled

- Sprite resource files and creatures: `g_file_table` names (`\NME\DOG.BND`, `HUGGER`, `WAR`,
  `EGGS`, `HANDLER`, `SYNTH`, `GUARD`, `SOLDIER`, `BURSTER`, `BAMBI`, `FINGERS`, `WARCEIL`,
  `QUEEN`, `DOGCEIL`, `COLONIST`) pair with the CLUT rows in `g_sprite_resources`, which ties
  actor kinds to creatures: 1 egg, 2 face-hugger, 3 chest-burster, 4 "bambi", 5/9 dog alien
  (floor/ceiling), 6/8 warrior (floor/ceiling), 7 Queen, 10 cocooned colonist, 11 guard,
  12 soldier, 13 synthetic, 14 handler, 16/18 steam jets, 17/19 flame jets.
- Particle kinds: 0 pistol, 1 shotgun, 2 pulse rifle, 3 flame, 4 smart gun, 5 enemy bullet,
  6/15 wall debris, 7 impact spark, 8/9/10 blood (human red, alien acid green, synthetic
  white), 12 pulse grenade, 13 enemy grenade, 14/16/17/18 jets, 19 hand grenade, 20/21 spent
  shells.
- Renderer: three row dispatch tables by distance (near `g_near_draw_table`, subdividing; mid
  `g_mid_draw_table`; far `g_far_draw_table`, depth-cued), indexed by the face selector (even
  quads, odd triangles; 2 mirrored, 4 no culling, 12-14 semi-transparent, 8/10/26..32 the
  texture picked by a command state).
- `f_62cc4` is `ClearOTagR` (it prints its own name).
- `g_flag` is set on the three Queen levels; it gates the green pulsing backdrop
  (`boss_backdrop_draw`).
- Sound ids 0..40 are `PERMSFX.BIN` (bank key 0x70, loaded at 0); ids from 41 are the level
  bank (`g_level_sample_banks`, loaded at 41).
- Undefined data blocks: `.data` 0x8006cc64..0x8006ccf0 (the per-level table of short pairs),
  0x8006bed4..0x8006bf10 ("Acclaim US", "Version 00.02") and 0x8006c05c..0x8006c084 (the two
  possible PlayRequest records) have no static reference and no runtime reader, so no owner
  or shape can be settled. The same holds for `g_8d240`, `g_8d270`, `g_8d530`, `g_8d058`,
  `g_8cd8a`..`g_8cd90`, `g_6c07c`, `g_8cedc` and `g_8cd7c`, which keep their address names.
- No scenario reached the results screen, the credits, memory card formatting, the colonist,
  egg-hatch and Queen-attack paths, flame jets, lifts in motion, switch activation or door
  power-up messages; their names rest on static evidence (`medium`).
- `f_48f54` and `f_48f8c` (`ot_index_tri`, `ot_index_quad`) keep their address names because
  `tools/test_gate.py` picks these two `ot_depth` labels by name. Low-confidence records are
  not applied.
