# Engine survey: Probe/Acclaim PS1 games against Alien Trilogy

Reproduction: `analysis/engine_survey/README.md`. Function-level hits: `matches.json`.

Calibration and control give each score its meaning:

- **Calibration.** AT's own matched source (558 functions), rebuilt with GCC 2.7.2, scores a
  median token ratio of 0.79 against AT's retail bytes. With GCC 2.8.1 the median is 0.81.
- **Control.** Medal of Honor (DreamWorks Interactive) cannot share AT's code. It produces 3
  "strong" hits, all tiny generic wrappers. That is the noise floor.

"Strong" means one of two things:

- AT's source, rebuilt by the other game's compiler, reproduces the other function exactly, with
  relocations masked, or as the same opcode-and-register skeleton (bodies of 8 words or more).
- BSim similarity >= 0.8 with AT self-significance >= 18.

## Alien Trilogy (reference)

Runtime Library 3.3 (62 of 70 version-discriminating modules), GCC 2.5.7, `-G8`. `li` is spelled
`ori` in 98.5% of cases, which is the 2.5.7 hallmark.

## Forsaken (PS1: Europe `SLES_007.45`, Japan `SLPS_014.88`, Europe demo)

**Verdict: shared libraries and modules.** Forsaken uses Probe's PS1 support framework and asset
pipeline. It does not share the game engine.

**Toolchain.**

- **Runtime Library 4.1.** Europe: 73 of 84 discriminating modules (4.2E is next at 65). The demo
  scores 83 of 93. `$Id: sys.c,v 1.135 1997/09/02`.
- **GCC 2.7.2** (the RL 4.1 Win32 build). The `jr ra` delay slot is left empty in 82% of cases
  (2.7.2 corpus: 93-96%; 2.8.x: 5%). `li` is not spelled `ori`. The `break 7` + `break 6` divide
  trap is present.
- **Rebuild test.** AT's source rebuilt with 2.7.2 reproduces 20 AT functions as Forsaken bodies
  (8 exact or masked, 12 skeleton). 2.8.1 reproduces 12.
- **Small data.** Game code is `-G0`: only 2 gp-relative accesses, against AT's 385.

**Shared code.** There are 24 strong AT functions. They map to 11 distinct Forsaken functions,
because AT carries per-type copies of the generic list routines.

| AT unit / functions | Forsaken (EU) | evidence |
|---|---|---|
| heap `f_14dfc` (bump alloc), helpers `f_14e7c` (free last) | 0x8002fc1c, 0x8002fca4 (adjacent, as in AT) | BSim 0.94 / 1.0; `f_14e7c` rebuilt with 2.7.2: masked-exact |
| task system: additional `f_15cb0` `f_15d50` `f_15e14` `f_15e4c` `f_15ea4`, seeds `reset_tasks` (256-entry pool), `alloc_task_pool` | 0x8001281c-0x80012b54, the same function order | BSim 1.0 for 6 of them; `f_15d50` masked-exact; the rest skeleton |
| list/pool idiom: `actor_alloc/link/unlink/free`, `map_object_alloc/link/unlink`, `f_247bc`, `f_24790`, `f_218a4`, `f_24768`, `f_2889c`, `f_3e640` | the same generic routines 0x80012a98 / 0x80012ac0 / 0x80012aec / 0x8001281c / 0x800122e4 | masked or skeleton under 2.7.2 |
| accessors `f_1c50c` (big-endian u32 read for IFF sizes), game_helpers3 `parse_decimal` | 0x800305e0, 0x8003060c (adjacent in both) | byte-exact under 2.7.2 and 2.8.1; ratio 0.82 |
| display `install_vblank_handler` | 0x8003f96c | ratio 0.92 when rebuilt with 2.7.2 |
| audio `f_4392c` (all voices volume 0), `f_4330c` (24 x 12-byte voice table) | 0x800434a0, 0x800437ac | masked-exact; BSim 0.83 |
| audio `f_44138` (SPU init), `sound_reverb_on/off`, `f_43d50` | demo 0x800469ec, 0x80046694, 0x800466e0, 0x80046608, in AT order | ratio 0.93 (rebuilt) and 0.72-0.77 |
| seeds `upload_clut_row` | 0x8004f208 | BSim 0.84 |
| asm ot_depth `f_48f8c` (max SZ of a quad) | 0x800244dc | BSim 0.93; the code is different (a reimplementation) |

**Not shared.**

- The renderer: none of the 32 asm routines (subdivision, polygon emitters, view cells) is found.
  The longest masked run is 8 words, and there are no GTE-stream matches.
- Game logic and front end: at the level of the control.
- Strings: only generic words are shared ("OPTIONS", "EXIT GAME").
- The 4096-entry sine table.
- Asset bytes: no 32-byte window is shared.

**Shared file formats.** These are exact layout matches:

- IFF `FORM` + `PSXT` texture pages with `INFO`/`TPnn`/`CLnn`/`BXnn` chunks (`GFX\TPAGE16.BND`
  against AT's `*GFX.BND`/`*GFX.BIN`).
- `FORM nnnn` + `Mnnn` model banks: 58 `.MP`/`.PCO` files. The chunk payloads differ (AT's start
  with `OBJ1`).
- AT's sound-bank header: `PERMSFX.BIN`/`PERMSFX2.BIN`/`INTROSFX.SND`; AT has `SFX\PERMSFX.BIN`.
- The `\GFX\` and `.BND` naming.

**Debug artefacts.** None found.

- No SYM, MAP, ELF, CPE, COFF, OBJ or source files. The `.MAP` files are level data.
- No non-blank unreferenced sectors except directory records.
- No source paths in the executables or overlays. The only `$Id` lines are Sony's.

## Forsaken (USA) zip: Windows PC release, not PS1

The ISO system ID is `Win95` and the executables are x86 PE. The executables embed source names
of the separate PC codebase "ProjectX": `E:\Projects\ProjectX\Sfx.c`, `Tload.c`, `Title.c` and
others, plus `g:\projects\Projectx\Forsaken.pdb`. The PDB is not on the disc. It was not
compared for code, because the architecture differs.

## Armorines: Project S.W.A.R.M. (prototype 1999-10-20, `PSX.EXE` + `ENGINE.OVL`/`FRONTEND.OVL`)

**Verdict: unrelated** (only Sony libraries are shared).

**Toolchain.**

- Runtime Library 4.6: 47 of 58 discriminating modules (4.7: 45, 4.3/4.4: 44).
  `$Id: sys.c,v 1.140 1998/01/12`, the same as MoH.
- GCC 2.8.x: the `jr ra` slot is left empty in only 7-21% of cases. `-G0`.

**Evidence.**

- 3 strong hits, equal to the control: `f_13a1c`, `f_44118` and `f_479f0`, all 8-10-word
  wrappers. `f_44118` also hits MoH.
- BSim finds nothing above self-significance 15. There are no renderer, string or format matches.
- Its own containers (`root`/`TINF` `.DAT`, raw `.BTX`/`.TEX`) are unlike AT's.

**Debug artefacts.**

- `FILES\DBGFONT.TIM` and an on-screen debug overlay: `CPU %d`, `DRAW %d`, `POLY %d`, `COLL %d`,
  `ALOC %d`, `FREE %d`, `OUT OF MEMORY`, `VERSION 0.13`, `WARP %d`.
- A developer logo file, `DDLLOGO.TIM`.
- No symbol, map or source files, and no data in unreferenced sectors.

## Medal of Honor (control)

Runtime Library 4.6, GCC 2.8.x. Unrelated, as expected: 3 strong hits, all generic.

## How Forsaken can help this decomp

- **Naming and structure leads:**
  - `f_14dfc`/`f_14e7c` are the heap allocator's alloc and free-last.
  - additional.c is the task manager: `f_15cb0` free-push, `f_15d50` pop-free, `f_15d7c` priority
    insert, `f_15e14` unlink, `f_15e4c` create(value, callback), `f_15ea4` kill, `f_15ee4` run-all.
  - `f_1c50c` is a big-endian u32 reader for the IFF loaders.
  - `f_4330c`/`f_4392c` belong to the SPU voice layer.
  - No symbol names exist on any disc, so these are behavioural names only.
- **Helper recovery.** Forsaken has one generic list/pool routine where AT has 3-5 per-type
  copies (actor, map object, task, `f_247bc` family). This points to a shared macro or inline in
  the original source.
- **Codegen reference.** AT's source rebuilt with GCC 2.7.2 reproduces Forsaken's functions
  byte-for-byte or masked. Forsaken therefore holds a clean later-compiler build of the same
  framework source, and a body that is ambiguous in AT can be cross-checked against it. It does
  not explain 2.5.7 walls directly; the compiler differs.
- **Format documentation.** The texture-page, model-bank and sound-bank layouts are confirmed
  across two games. A Forsaken loader is a second witness for AT's loader structure.

## Caveats

- **Overlay load addresses are estimated** from `jal` agreement: Forsaken INTRO 0x800d4a60,
  Armorines overlays 0x8005398c, MoH 0x80039d90. The Forsaken boss overlays have no reliable base,
  so they were searched without one and left out of BSim.
- **Function boundaries in the other games are heuristic** (`jal` targets and `jr ra` splits).
  AT's unmatched functions use inventory boundaries.
- **Fuzzy (token-ratio) and BSim hits below the strong criteria are candidates only.** Short
  functions match generically; compare them with the control rows in `matches.json`.
- **The rebuild test uses the Win32 GCC 2.7.2/2.8.1 builds at `-O2 -G8` with Runtime 3.3
  headers.** Forsaken is `-G0`, so a gp-free rebuild could add matches.
