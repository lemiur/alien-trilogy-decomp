# Runtime evidence for globals and functions

`globals.json` records what the retail game does with address-named (`g_xxxxx`) and
weakly named ("name: inferred from use") globals when it runs in DuckStation, and the
names, types and sizes that evidence supports. Round 1 renames were applied by the owning lanes; round 2 (functions, below) applied its own.

## Record format

One record per address (`globals[]`):

| field | meaning |
|---|---|
| `address`, `current_name` | the storage and the name the sources use today (`(unnamed)` for globals the sources do not declare) |
| `declared`, `region`, `static_size`, `max_size`, `static_accesses` | static context: declaration, section, size from `config/data.json` / `config/bss_layout.json`, and every lui/gp access in the retail image grouped by function (`r`/`w`/`a` + width + member offset) |
| `runtime_values` | per scenario: first/last value, distinct values (first 8), number of changes over the recorded frames, access width used |
| `runtime_writers` | per write-watch scenario: `function+offset@store_pc` of the stores that hit the watch (first three hits per address) |
| `proposed_name`, `proposed_type`, `proposed_size`, `confidence`, `kind`, `evidence`, `note` | the curated finding; `kind` is `rename`, `interior` (the address is a member or element of a larger object: spell it through that object), `new` (not declared yet), `confirm`, `unresolved` or `observed` (runtime data only) |

Confidence: `high` = runtime observation of the behaviour the name states (or a runtime
layout fact for `interior`), plus static agreement; `medium` = static meaning with
runtime values consistent but the behaviour not exercised; `low` = value only.

## Method

1. Worklist (`builds/lane_tools/worklist.py`): every `g_xxxxx` in `src/` and `include/`
   and every declaration under a "name: inferred from use" comment, with addresses from
   the symbol maps and static accesses from a lui/addiu/gp scan of the retail EXE
   (`builds/lane_tools/xref.py`).
2. Value series (`builds/lane_tools/series.py`): from a save state, an input script runs
   frame by frame (`frame_step`) while `0x8006a000..0x80074000` and
   `0x8008cd74..0x800a1960` (.sdata, .sbss, .bss, COMMON) are read every frame;
   `analyze.py` reduces each global to the values above.
3. Write watches (`builds/lane_tools/sweep.py`): write breakpoints on the first word of
   every target, 40 at a time, from a save state with an input script; each hit is
   resolved to the store instruction whose effective address is the watched word
   (DuckStation reports the PC one or two instructions after the store) and mapped to a
   function through the symbol maps, `analysis/inventory.json` starts and the Runtime
   library modules.
4. Targeted experiments (in `findings.py` evidence strings): button presses with
   `input_sequence`, execute breakpoints, and RAM pokes into a loaded save state (for
   example `g_game_flags |= 0x900` to run the lamp ramp). Pokes only touch the emulated
   RAM of a state that is reloaded afterwards.
5. `builds/lane_tools/gen_globals.py` merges the worklist, the automatic evidence and
   `builds/lane_tools/findings.py` into `globals.json`.

The emulator is driven through `tools/duckstation.py` (DuckStation's MCP server,
`http://127.0.0.1:2346/mcp`). Use 127.0.0.1, not `localhost` (about 2 s per request
on Windows), and keep inline reads at 16 KB or less (larger inline replies stall;
the client switches to a file read automatically).

## Save-state slots (DuckStation, game SLUS-00007)

| slot | state |
|---|---|
| 8 | title main menu ("Start game" / "Options"), music started, idle counter running |
| 9 | level 1 mission briefing screen, level fully loaded ("Press X to continue") |
| 10 | level 1 first frame of play: health 100, pistol 15 + 2 clips (HUD 045), facing the start wall |
| 7 | options menu (from slot 8: Down, Cross), cursor on "Sound" |
| 5 | level 11 (Queen) mission briefing, reached with the password cheat G0LVL11 from the options menu |
| 4 | level 14 first frame of play (round 3; it held the level-14 briefing before), reached with G0LVL14 |
| 6 | level 1, one frame after `player_damage_health(200, 3)` was forced from `player_task_walk` (health 0, `g_game_flags` 0x30); running on plays the death movie and returns to the title |

Slots 1-3 were left untouched (slots 1-7 were empty before this lane); the server only accepts slots 1-10, so the requested 20+ range was not available.

## Reproduce

```
python builds/lane_tools/xref.py          # static accesses -> builds/runtime/xrefs.json
python builds/lane_tools/worklist.py      # -> builds/runtime/worklist.json
cd builds/lane_tools
python series.py title_idle 8 200 '[[null,100],["Down",3],[null,30],["Up",3],[null,60]]'
python series.py brief 9 200
python series.py level_idle 10 120
python series.py level_play 10 400 '[[null,10],["Up",60],["Cross",3],[null,20],["Left",30],["Up+Cross",40],["Right",30],["L1",20],["R1",20],["Cross",3],[null,20],["Cross",3],[null,20],["Triangle",3],[null,30],["Down",40],[null,60]]'
python analyze.py title_idle brief level_idle level_play
python sweep.py menu 8 4; python sweep.py newgame 8 25; python sweep.py levelload 9 12; python sweep.py level_play 10 6
python gen_globals.py
```

`builds/` is ignored; the lane scripts are working tools, and the dumps they write
land in `builds/runtime/`.

## Cheats and passwords

Published GameShark codes and password cheats (supplied by the user as leads) are
verified, not trusted: each one is recorded as one evidence string next to the poke,
watch or write-breakpoint result that confirms or refutes it. `G0LVLxx` on the
password keyboard (`builds/lane_tools/pw.py`: `warp(level)`, `try_password(text)`)
sets `g_start_level`, which reaches late levels without play.

## Not reached

Scenarios that need human play (see the lane report): the results screen for
`g_8d5b0`, a secret area for `g_96c54`, a memory card directory for
`g_card_block_names`, and enough weapons/actors to tie the remaining actor CLUT rows
(`g_8d398`..`g_8d450`) to creatures.

# Functions (round 2)

`functions.json` records, for every address-named function (`f_xxxxx`) the sources
named or declared, the name the evidence supports. Round 2 also extended `globals.json`
(records with `"round": 2`) and applied the high- and medium-confidence names to `src/`,
`include/`, `asm/`, `config/` and `docs/`.

## Record format

One record per function (`functions[]`):

| field | meaning |
|---|---|
| `address`, `current_name`, `proposed_name` | the code and its names before and after this round |
| `kind` | `rename`; `alias` (the address already had a name in another unit: the callers now use it); `library` (a Runtime 3.3 function called by address); `unresolved` |
| `size`, `unit`, `callers`, `pointer_from` | static context: retail size, owning unit, `jal` callers and the functions that take its address (task callbacks, tables) |
| `confidence` | `high`: the static meaning is clear and the function ran at runtime in the role the name states (or the evidence is decisive on its own: the same address, a library function printing its own name, a dispatch-table slot, the asm unit's documented routine); `medium`: static meaning clear, not exercised; `low`: not settled (not applied) |
| `evidence` | static evidence, then the experiments and census scenarios it ran in |
| `runtime` | per scenario: hits (first three, at most four), frame of each hit, callers, `a0..a3` of the first two, `v0` on return and RAM words the first call changed in `.data`/`.sdata`/`.bss` |

## Method

1. Static index (`builds/lane_tools/np/static.py`): function ranges from the symbol maps
   and `analysis/inventory.json`, `jal` call graph, lui/gp data references, pointers to
   code (task callbacks, dispatch tables), referenced strings. `dossier.py` prints one
   function's static context and source comments.
2. Census (`np/census.py`, `np/run_census.py <scenario>`): execute breakpoints on every
   worklist function at once from a save state with an input script; each hit records
   the frame, arguments and caller, and the first hit also a return breakpoint (on `ra`
   with the entry `sp`) for `v0` and a RAM diff. A function's breakpoint is removed
   after K hits. Scenarios: `title` (slot 8, idle with the attract demo), `title_menu`,
   `options` (slot 7), `password`, `levelload` (slot 9), `play` (slot 10), `death`
   (slot 6), `queen` (slot 5), `l14` (slot 4).
3. Experiments: `exp_weapons.py` (every weapon and its ammunition poked into `g_player`
   and `g_weapon_slots`, then weapon change, fire and grenade per weapon);
   `exp_blood.py` (live actors moved in front of the player, then fire: level 1 aliens
   and, after `G0LVL03`, level 3 guards); `exp_levels.py` (warp, start, list live actor
   kinds); `exp_undef.py` (read watchpoints on the undefined data blocks).
4. `gen_functions.py` merges the curated findings (`ffind.py`, `gfind.py`,
   `experiments.json`) with the census dumps into `functions.json` and `globals.json`;
   `rename.py <group>` applies a group's high/medium names (`apply_names.py`: whole-word
   byte-level replacement, a provenance comment before each C definition).

The `boot` scenario (reset, BIOS, intro) is not usable: the BIOS shell runs code at the
same addresses as the game and floods the breakpoints. Boot-time functions are named from
their bodies.

## Findings worth knowing

- Sprite resource files and creatures: `g_file_table` names (`\NME\DOG.BND`, `HUGGER`,
  `WAR`, `EGGS`, `HANDLER`, `SYNTH`, `GUARD`, `SOLDIER`, `BURSTER`, `BAMBI`, `FINGERS`,
  `WARCEIL`, `QUEEN`, `DOGCEIL`, `COLONIST`) pair with the CLUT rows in
  `g_sprite_resources`, which ties actor kinds to creatures: 1 egg, 2 face-hugger,
  3 chest-burster, 4 "bambi", 5/9 dog alien (floor/ceiling), 6/8 warrior (floor/ceiling),
  7 Queen, 10 cocooned colonist, 11 guard, 12 soldier, 13 synthetic, 14 handler,
  16/18 steam jets, 17/19 flame jets. Level 3 (`G0LVL03`) runs guards and steam jets.
- Particle kinds: 0 pistol, 1 shotgun, 2 pulse rifle, 3 flame, 4 smart gun, 5 enemy
  bullet, 6/15 wall debris, 7 impact spark, 8/9/10 blood (human red, alien acid green,
  synthetic white), 12 pulse grenade, 13 enemy grenade, 14/16/17/18 jets, 19 hand
  grenade, 20/21 spent shells.
- Renderer: three row dispatch tables by distance (near `g_near_draw_table`, subdividing;
  mid `g_mid_draw_table`; far `g_far_draw_table`, depth-cued), indexed by the face
  selector (even quads, odd triangles; 2 mirrored, 4 no culling, 12-14 semi-transparent,
  8/10/26..32 the texture picked by a command state).
- `f_62cc4` is `ClearOTagR` (it prints its own name), not `ClearOTag` as its old comment said.
- `g_flag` is set on the three Queen levels; it gates the green pulsing backdrop
  (`boss_backdrop_draw`), seen on level 11.

## Undefined data blocks

Read watchpoints on every word of `.data` 0x8006cc64..0x8006ccf0 (the per-level table of
short pairs), 0x8006bed4..0x8006bf10 ("Acclaim US", "Version 00.02") and
0x8006c05c..0x8006c084 (the two possible PlayRequest records) fired in none of: title
idle with the attract demo (2400 frames), title menu to a new game, options, level load,
play with the pause menu, death to the title. Together with no static reference, they
stay undefined (no reader, so no owner or shape can be settled). The same holds for
`g_8d240`, `g_8d270`, `g_8d530`, `g_8d058`, `g_8cd8a`..`g_8cd90`, `g_6c07c`, `g_8cedc`
and `g_8cd7c` (never read in those scenarios), which keep their address names.

## Save-state slots (round 2)

No slot was written. Slots 4-10 (round 1) were loaded; slots 1-3 were empty. Levels other
than 1, 11 and 14 were reached with `G0LVLxx` from slot 7 (`np/pw2.py`); the warp to
levels 8 and 9 did not take (the game stayed on level 1).

## Not reached

The results screen (`compute_level_results`, `build_result_colors`), the credits, memory
card formatting (`format_card_slot`), the colonist, egg-hatch and Queen-attack paths, the
flame jets, lifts in motion beyond level 14's idle lifts, switch activation
(`switch_panel_active_task`) and door power-up messages need human play or a save state
there; their names rest on static evidence (`medium`).

## Not applied

`f_48f54` and `f_48f8c` (`ot_index_tri`, `ot_index_quad`, high) keep their address names:
`tools/test_gate.py` picks these two `ot_depth` labels by name, so they are renamed together
with that test. Low-confidence records (`f_1760c`, `f_1b674`, `f_1d0e0`, `f_20584`, `f_2076c`,
`f_2603c`, `f_40228`, `f_43e98`, the five LIBSPU guesses and three unknown LIBSPU exports) are
not applied.

# Constants (round 3)

`constants.json` answers the runtime questions of the named-constants lanes (items 1-21):
per item the value, the question, the scenario, the observation, the reader/writer PCs
mapped to functions, a recommended name and a confidence. Nothing in `src/`, `include/` or
`config/` was changed.

## Method

- Sound ids: execute breakpoints on `sound_play`, `sound_play_at`, `play_menu_sound` and
  `voice_play` log the sample id and the caller while a scenario runs (`builds/lane_tools/k.py`,
  `snd_log`). Ids 0..40 are `PERMSFX.BIN` (bank key 0x70, loaded at 0); ids from 41 are the
  level bank (`g_level_sample_banks`, loaded at 41). `np/banks2.py <entries>` hashes those
  entries in every bank on the disc, which shows whether an id is the same sample on every
  level; `np/banks.py` decodes SPU ADPCM, and the decoded samples are in
  `builds/runtime/samples/` for listening. No audio was captured: samples are identified by
  who plays them, the weapon animation scripts that contain them and the bank data.
- Flags: `np/bits.py <addr>` prints every gp/lui access to a global with the instructions
  after it, `np/andscan.py <masks> <offsets>` every halfword load of a member tested with a
  mask, `np/setscan.py` ori/andi/addiu masks near a member access. Runtime pokes then show the
  effect (cheat 0x10 debug display, cheat 0x100 actor wake-up, lamp and night vision expiry).
- Pictures: the player is teleported in front of an object (`set_player`) and 25 frames are
  stepped before a screenshot; glyphs are cut out of a VRAM dump through their tpage and CLUT.

## Answers

| item | value | answer | confidence |
|---|---|---|---|
| 1 | door 0x2a/0x2b | start / reach the end (60 frames apart); one variant per section group | high |
| 1 | 0x5c, 0x5e/0x5f, 0x6c | switch, lift, shatter: one sample in every bank that has them | high/medium |
| 1 | pickups | not reload samples (reload is silent); 0x0f is the shotgun pump, 0x07, 0x0a, 0x12 are handling sounds of the weapon scripts; 0x05 and 0x0c are pickup-only | high/medium |
| 2 | sprite 0x01 / 0x08 | 0x01 frames are packed (unpack_rle/unpack_lz before upload); 0x08 read only by creature_task (fire on the cue frame) | high/medium |
| 3 | object types | 0x17 explosive barrels, 0x14 low crate, 0x19 tall crate; 0x20..0x25 not on levels 1, 11, 14, 22 | medium |
| 4 | DOOR_ALONG_Z | confirmed from cell strides (row = 92 cells) | high |
| 5 | 0xfe / 0xfd | left / right arrow buttons (0xff the Cross button) | high |
| 6 | cheat 0x100 / 0x200 | idle actors wake only with 0x100; lift_draw draws only with 0x200 | high / medium-high |
| 7 | game 0x4000 / 0x10000 / 0x400 | restored (password, card load; cleared at level start; no reader); best grade (set only); light flash | medium-high |
| 8 | MENU_ENTRY_HEADING | no reader | medium-high |
| 9 | g_8cedc | allocated, never read | high |
| 10 | 0x27 | Mission Assessment bar tick, every 8 frames | high |
| 13 | cheat 0x10 / 0x8 | debug display (usage bars, numbers, frame time, doors hidden) / floor snap | high / medium |
| 17 | 0x23/0x24/0x25 | levels 12..21 / other levels / cell types 8-9 | high |
| 18 | ITEM_HUGGER | set and cleared by the hugger cling code | medium |
| 20 | 0x19 / 0x1a | lamp and night vision expiry / automap scroll | high / medium |
| 15, 16, 19, 21 | | see `constants.json`; not settled | low |

## Not reached

The memory card screens (item 5 was answered from VRAM instead), a lift in motion, a wall
block shattering, a countdown level, a face-hugger cling, levels with object types
0x20..0x25, particle kind 11 at runtime, and the password path for `GAME_FLAG_RESTORED`
(the keyboard helper typed the wrong text from slot 7 in this round).

# Constants (round 4)

Items 22-32 of `constants.json` follow up the questions round 3 left open (19, 3, 15, 16,
21, 18) and the "Not reached" list above, plus three questions from the player constants
lane (cell types, `g_8d270`, automap colour 3). Nothing in `src/`, `include/` or `config/` changed.

## Method

- Level warp (`builds/lane_tools/r4/go.py <level>`): from slot 8, Cross starts a new game; an
  execute breakpoint on `password_store_level` (the first call of main's level loop) stops
  before the level loads, `g_level` is written, and the briefing, map, sample bank and
  countdown all follow the written level. This replaces the `G0LVLxx` keyboard route, which
  missed levels 6, 8 and 9 (and from slot 7 can land in the memory card menu).
- Map census (`r4/maps.py`): the MAP0 chunk of every `LxxxLEV.MAP` parsed with
  `level_parse_map`'s layout (a 36-byte header, then vertices 8*n, faces 20*n, cells
  16*w*h, layout records 8*n, actor slots 20*n, object records 8*n, level objects 16*n;
  MapCell.type at +0xa), checked against RAM on levels 0, 4, 11 and 14. Output
  `builds/runtime/r4_maps.json`: level-object types, actor-slot kinds and cell types per level.
- Countdown (`r4/cd.py`): the countdown TASK is found by its callback, its seconds poked to
  23, and `sound_play` logged with the seconds left.
- Static censuses with capstone over the retail EXE: every non-immediate store to
  `Particle.kind`, every store to `Actor.kind`, every store to `item_flags`, every access to
  `0x8008d270`.
- Objects are identified by moving `LevelObject.position` out of view and comparing
  screenshots; walls, lifts and the hugger by teleporting the player (`k.set_player`).

## Answers

| item | value | answer | confidence |
|---|---|---|---|
| 22 | 0x62..0x65 | timed re-arm levels; 0x65 (levels 2, 5) / 0x64 (7, 9, 16, 19) at 20 s and 17 s, 0x63 at 14 s; 0x62 (27, 30) every 3 s from 20 s is a klaxon, the others look like speech; the level completes at 0 | high / medium |
| 23 | objects 0x20..0x25 | 0x20 on levels 4 and 12, 0x21 on 2, 6, 12, 13, 18, 0x22..0x25 on none; models not identified; 0x15 = hazard-topped drums | high / low |
| 24 | particle 11 | no writer in the EXE (dead kind) | medium-high |
| 25 | actor 15 | no level's slot table uses it; never produced | high |
| 26 | item 0x4000 | no setter in the EXE; item_flags is +0x68 (0x8009a03c), +0x6c is health | medium-high |
| 27 | lift 0x5e/0x5f | lifts start when cell triggers fill Lift.count up to Lift.wait; 0x5e on start, 0x5f on arrival | high |
| 28 | ITEM_HUGGER_ATTACHED | set at a class-0 attack, cleared when the hugger drops off (about 95 frames) | high |
| 29 | 0x6c | a bullet (or blast) hit uses up a wall's state; block_object_task shatters it | high |
| 30 | cell types | 9 deep liquid, 8 shallow, 5 slow floor, 6 door cell, 20..24 solid, 25..44 player-passable | medium |
| 31 | g_8d270 | written once by mission_text_draw, never read | high |
| 32 | automap colour 3 | no pixel uses it | medium-high |

## Save-state slots (round 4)

| slot | state |
|---|---|
| 6 | level 4 (131 Med Lab), first frames of play after `go.py 4` (overwrote round 1's death state) |

Slots 4, 5 and 7-10 are unchanged; slots 1-3 were not touched.

## Not reached

On-screen pictures of object types 0x20 and 0x21 and of cell types 5 and 9 (dark rooms); the
words of the spoken countdown samples (not transcribed). The lift run poked Lift.count to 6
instead of operating the six switch panels that come before the last trigger.

# Struct fields and pictures (round 5)

Items 33-50 of `constants.json`: the struct members still named `unknown_*` (items 33-45)
and the pictures and samples round 4 left open (46-50). The names the evidence settles are
applied in `include/` and `src/`.

## Method

- Watchpoints (`builds/lane_tools/r5/w.py`): read and write breakpoints on **every byte** of
  a member; each hit is resolved to the load or store whose effective address covers it
  (DuckStation stops one instruction after it, before a load has written its register) and
  mapped to a function. DuckStation fires a memory breakpoint only when an access covers the
  breakpoint's exact address: a breakpoint on a word does not see a byte or halfword access to
  its other bytes. The write sweeps of rounds 1 and 2 watched the first word of each global,
  so a global they report as unwritten may still be written at another offset.
- Scenarios (`s.py`, `load.py`, `fight.py`, `drop.py`, `end.py`, `lifts.py`; `all.py` runs them
  and merges `builds/runtime/r5_summary.json`): see item 33. `load.py` stops at the first
  record consumer of the level load (`lift_records_prepare`) and watches the map records
  before they are read. `fight.py` places actors in front of the player so they are
  projected; `drop.py` arms the level objects that carry a pickup so they throw it.
- Level data (`census.py`): the unnamed record bytes of every MAP0 chunk, with the record
  kind they belong to.
- Pictures (`pics.py`, `ab2.py`): the player is put 0x380..0x600 units from an object or cell
  on each of four sides, at the floor height of its cell (player y = MapCell.value << 5),
  and 25 frames run before the screenshot. For an A/B picture the object's position.vx is
  moved by 0x4000 and 16 frames run: the game draws a frame behind, and 4 frames showed no
  difference.

## Answers

| item | member or value | answer | confidence |
|---|---|---|---|
| 33 | Player.unknown_102 | written 1 by player_task_start; no reader | high |
| 34 | Sprite.unknown_2e | never written or read; actors keep stale heap bytes there | high |
| 35 | MessageBox.unknown_12 / 1a | no access, the briefing boxes included | high |
| 36 | MapObject +0x23, +0x26 | alignment holes, no access also when pickups are thrown: removed | high |
| 37 | ObjectRecord.unknown_07 | no reader; a per-kind constant on the ammunition kinds: `default_amount` (inferred) | low-medium |
| 38-43 | LevelObjectRecord.unknown_05, LiftRecord.unknown_06 / Lift.unknown_14, LiftRecord.unknown_0d, ActorSlot.unknown_03 / 09, Level.unknown_4a | no runtime reader (lifts_create only copies unknown_06); value patterns in the identity-TODOs | high (no reader) |
| 44 | ModelBlock.unknown_04 | no reader; the second word looks like an address in the exporting tool | high / inferred |
| 45 | AudioMix | write-only; its address appears only in its five stores | high |
| 46 | object 0x20 | a long low slab (1344 x 416 x 576); the task tilts it and never spins: LEVEL_OBJECT_TILTING | medium |
| 47 | object 0x21 | a hazard-striped drum | medium |
| 48 | cell 9 | water (sunken pool, sewer channels) | high |
| 49 | cell 5 | looks like ordinary floor | low |
| 50 | samples 0x62..0x65 | 0x63/0x64/0x65 speech-like (sibilant bursts), 0x62 a three-pulse klaxon; words not transcribed | medium |

## Save-state slots (round 5)

No slot was written; slots 4-10 were loaded, slots 1-3 were not touched.

## Not reached

The words of the countdown samples (no speech recogniser); level 4's 0x20 objects in view;
what ObjectRecord.default_amount was for (no reader).
