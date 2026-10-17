# Matching decompilation: agent guide

Reconstruct original source so the original compiler, with the original flags, emits object code that matches
the retail binary byte for byte, function by function. Correct structure outranks any score: classes, types,
ownership, storage, control flow, calling conventions and referents.

Read the documentation in /docs, including PORTING.md, to completion before performing any alteration or changes to this repository or to any derivative.

This file is project-independent. Fill in the **Project adapter** section once per repository. The skills
(`matcher`, `wall-identifier`, `helper-recovery`, `permute`, `orchestrator`) use the generic verbs defined there.

## Project adapter: Alien Trilogy (PS1 USA, `SLUS_000.07`)

Paths below are relative to this folder. The tools resolve `toolchain/`, `shared-tools/` and
`games/` two levels above `tools/`, so **every checkout or worktree must sit directly in
`E:\Compiler Identification\`** (for example `E:\Compiler Identification\alien_wt1`).

| verb | meaning | this project's command |
|---|---|---|
| `build` | compile every unit, compare every annotated function | `python tools/verify.py` (all units in `UNITS`, A234; writes `reports/latest.json`) |
| `match <unit>` | compile and compare one unit quickly | `python tools/verify.py --units <unit> --output builds/<unit>.json` |
| `diff <function>` | compiled vs retail disassembly | `python tools/build.py --source src/<unit>.c` then `python tools/diff.py --object builds/job-*/INPUT.OBJ --symbol-map config/<unit>.json --function <f>` |
| `report` | refresh progress | `python tools/reports.py --jobs 6` (clean tree; runs gate, verify, runtime analysis, link plan, progress in order) |
| `gate` | must stay green before commit/merge | `python tools/gate.py` on a clean tree (about 40 s). It rebuilds every unit into a fresh `builds/gate/`, links them over fallback bytes of the original (a Python linker; its docstring says why not PSYLINK), hashes the whole PS-X EXE against `config/gate.json`, and cross-checks its per-byte provenance record (`builds/gate/provenance.json`) against an independent `verify.py` run. Only the printed `GATE OK sha256=... source_bytes=N/514048` line counts as banked; on failure it names the unit and function or datum behind every differing byte. `--verify-report reports/latest.json` cross-checks against a current report instead (about 25 s); `--base-case` links original bytes only. Its own tests: `python -m unittest tools/test_gate.py` |
| `rules` | rule checker: [match] findings decide "matched under the rules" | `python tools/rules.py [--files F ...] [--match-only] [--summary-only] [--report-only]`: prints `file:line: RULE <id> [<tier>]: message (function)` and the summary (byte-matched, MATCH-PENDING, matched under the rules); exit 1 on an unexcused [match] finding. Reads markers, `config/match_pending.json` and `config/exceptions.json`. Its tests: `python -m unittest tools/test_rules.py` |
| `disasm` / `xref` | retail assembly, callers/callees | candidate starts: `analysis/inventory.json`; uncovered ranges: `reports/link_plan.json`; `capstone` on the reference (as in `tools/diff.py`). No Ghidra project or splat config in the workspace |
| `permute <function>` | permuter | decomp-permuter (pinned, `../shared-tools/decomp-permuter`): `python tools/permute_setup.py <f> [--context src/<unit>.c]` makes `builds/permute/<f>/`, then `python tools/permute_run.py builds/permute/<f> --minutes N -j 4`; outputs are leads only. See `docs/notes/permuter.md` |

- **Toolchain:** GCC 2.5.7 (`toolchain/C257`) `-O2 -G8`, ASPSX 2.34 (`toolchain/A234`); ASPSX 2.08
  (`toolchain/A208`, Runtime 3.0 tools) for library-variant assembly units only, selected per unit in
  `config/asm_build.json` (`docs/notes/library_asm.md`); run under
  DOSBox-X (`C:\DOSBox-X\dosbox-x.exe`) by `tools/build.py`. Sony Runtime Library 3.3. Pinned in
  `config/toolchain.lock.json`. GP `0x8008cd74`; load `0x80010000`. Use https://github.com/ethteck/splat to split binary. https://github.com/Druthulu/xsig
- **Units:** a unit is `src/<unit>.c` plus its symbol map `config/<unit>.json` (function address/size and
  the address of every referenced symbol), registered in `UNITS` in `tools/verify.py`. A `static` function
  carries `"static": true`; each static datum is a `static_data` entry (address, size, section), and one in
  `.sdata`/`.data`/`.rdata` is also placed by the `config/data.json` definition of its name (same address and size).
  Statics are scoped to their unit and audited against the object's local symbols, or for initialised data the
  listing's unexported labels (`tools/lnk_symbols.py`).
  Initialized data placement is `config/data.json`. Only fully matching functions live in `src/`; unmatched or superseded
  drafts live in `archive/drafts/` (the lost-match / near-miss pool).
- **Annotations:** the address is carried by the symbol map, not a source comment. Names are address
  based (`f_xxxxx`, `g_xxxxx`) until a semantic name is evidenced.
- **Reference sources:** Runtime Library 3.3 libraries and the PsyQ install in `../sdk` (trusted; the linker is
  `../sdk/psyq/bin/PSYLINK.EXE`), library matching in `../compiler-identification/library-matching` and
  `../shared-tools/libmatch.py`. `AT.C` symbol evidence in `analysis/inventory.json`. The audit in
  `../decomp-audit/README.md` lists known false claims; check it before trusting a historical verdict. Use https://psx-spx.consoledev.net/ and https://github.com/mkst/maspsx as resources. 
- **Policies:** Running the game is allowed, as is the use of the decompiler. Commit messages:
  `match: <unit> f_xxxxx`, `gate: ...`, `tools: ...`. Main branch is `main`.
- **Hand-written assembly:** allowed **only** for the hand-written GTE/renderer code at
  `0x80048f54`–`0x800588bb`, as real `.s` units assembled by ASPSX. Annotate them heavily: a header per
  routine (purpose, inputs/outputs, registers and scratchpad use, callers), and comments on every
  logical block and every non-obvious instruction (GTE ops, `$at` use, delay slots, live `$t` registers).
  Data tables that retail keeps inside this region's `.text` (after `0x800558e8`) are written as annotated
  tables (`.word`/`.half`/`.byte` with a comment per table on its purpose, layout and users, and
  symbolic relocations), also counted as "assembly".
  The gate and reports count these bytes as "assembly", never as matched C.
- **Library-variant assembly:** also allowed for the Sony Runtime Library modules that retail links
  as later rebuilds matching no archive in `../sdk` (so they cannot be certified as Runtime 3.3). Their
  `.text` ranges are `0x80058980`–`0x8005989b`, `0x80059984`–`0x8005bf53`, `0x8005bf9c`–`0x8005c633`,
  `0x8005c6b0`–`0x8005c717`, `0x8005c738`–`0x8005c883`, `0x8005c8d8`–`0x8005c98f`,
  `0x8005ca24`–`0x8005cd6f`, `0x8005d310`–`0x8005f94b`, `0x8005fa64`–`0x8005fad7`,
  `0x800607b0`–`0x800612cf`, `0x800613c0`–`0x800614fb`, `0x80061e34`–`0x800620a7`,
  `0x800623b8`–`0x80064fe7`, `0x80066250`–`0x80066e8f`, `0x80068030`–`0x800680cf`,
  `0x800684a0`–`0x800684fb` (the `library_text` / `library_variant` gaps of
  `analysis/data_survey/gaps.json`). The `.rdata`/`.data`/`.sdata` that belong to these same modules
  (`library_data` / `blocked_by_library_text` there) are written as annotated asm data tables. One `.s`
  unit per library module, named after it (for example `libspu_s_gks.s`), with the same annotation
  standard as above plus a module header naming the library, module and exports, and stating how
  it differs from the nearest archived version (for example "Runtime 3.3 S_GKS with its own
  `_spu_RXX` import"). Exported names are the library's real symbol names. The gate and reports
  count these bytes as "library assembly", separate from game assembly and never as matched C or as
  certified library. Everywhere else, assembly (including inline `__asm`) remains forbidden.

### Project profile

The code rules are those of the `decomp-code-rules` skill (rule ids R1-R37); this profile overrides it
where they differ.

- **Dialect (GCC 2.5.7 C, confirmed by scratch compiles through `tools/build.py`):** `/* */` comments
  only (CPPPSX passes `//` through and CC1 rejects it); declarations at the top of a block (mixed
  declarations and code, and `for (int i ...)`, are rejected); no designated initialisers in any form
  (`.b = 2`, GNU `b: 2` and `[2] 5` are all rejected); `static inline` is accepted. K&R definitions are
  accepted, and kept where the old-style promotion is what retail's prologue shows (`Actor_Spawn`,
  `Text_DrawGlyph`). No `stdint.h`/`stdbool.h`: use the SDK's `u_char`/`u_short`/`u_long`.
- **Line information:** none. Retail carries no debug data, `tools/build.py` compiles without `-g`, and
  no source uses `__LINE__`, `__FILE__` or `assert`. Layout, comments and names (outside strings) are
  [free]; still gate once per batch, and keep formatting in its own commits (R23).
- **Naming and layout (user ruling): id Software Quake III C style**, `style-defaults.md` Table 1 in full.
  The rename map is `config/style_renames.json` (old -> new by kind, with owner, `keep` reasons and
  `reworded` flags), regenerated by `python tools/rename_map.py` (approved entries carried over verbatim,
  hand decisions in `config/style_overrides.json`) and applied by `python tools/rename.py` (`--dry-run`
  prints counts per file and the collision check; it refuses a dirty tree). Existing enum values and
  constants keep their domain prefix (`DOOR_`, `PICKUP_`, `SOUND_`); the table's upper-case column is
  for new ones. The map has been applied to the whole tree (`style: apply the Quake III rename map`) and
  stays as the record of old -> new names; new code is written in the style directly. Indentation is 4 spaces (Quake III uses tabs). Layout (K&R braces, spaces inside
  parentheses, aligned declarations) is the root `.clang-format`, run as `clang-format -i src/*.c include/*.H` with the
  pinned `pip install clang-format==22.1.8`; it is idempotent, so format before committing. Function banners are
  written by `tools/banners.py`.

  | entity | style | example |
  |---|---|---|
  | global function | `Prefix_VerbNoun` | `Actor_Spawn`, `Door_Open` |
  | static function | as global, or plain `VerbNoun` | `Door_Blocked`, `FindFreeSlot` |
  | task or callback | role prefix or suffix | `Door_Task`, `Think_Hugger` |
  | struct type | `lowerCamel_t`; tag `lowerCamel_s` when self-referencing | `actor_t`, `struct actor_s` |
  | enum type | `typedef enum { ... } lowerCamel_t;` | `doorState_t` |
  | enum value | `PREFIX_NAME`, count last | `DOOR_CLOSED` ... `DOOR_NUM_STATES` |
  | constant, flag | `UPPER_SNAKE` `#define` or enum; flags carry their owner's prefix | `MAX_ACTORS`, `PLAYER_FLAG_LAMP` |
  | function-like macro | function style when it stands in for a function, else upper | `VectorCopy`, `CHEAT_IS_ACTIVE` |
  | struct member | `lowerCamel` | `nextThink`, `startTime` |
  | local, parameter | short `lowerCamel`; conventional names | `i`, `ent`, `dir`, `self` |
  | braces | K&R, spaces inside parentheses | `if ( cond ) {` |
  | declarations | C89, at the top of the block, aligned in columns | |
  | function banner | `/* ===== Name description ===== */` | |
  | section banner | the same box with an upper-case title | `/* ===== DOOR MODELS ===== */` |
  | unit opening | `/* unit.c -- purpose */` | `/* doors.c -- sliding doors and their models */` |

  Global variables, static data included, are `g_` + `lowerCamel` (`g_levelMap`); string data keeps
  its `s_` prefix (`s_doorActivated`). Static functions take the module prefix; plain `VerbNoun` only
  where the prefixed name is taken (`TickTimer`).

  **Never renamed (recorded spelling):** names evidenced by the Probe/Forsaken lineage
  (`analysis/forsaken_pc/`, `/* name: lineage ... */`): tasker.c's `TaskerInit`, `TASK` and its
  `Priority`/`Next`/`Prev`, `AddTASKToFree`, `TaskMEM` and the rest, and movie.c's Sony STR sample
  names (`strInit`, `strCallback`, `bound`, ...); SDK and library names (everything the Runtime 3.3
  headers declare, the `include/LIBAPI.H` exports, and every symbol of the `asm/lib/` units); the
  fixed-width typedefs of `include/TYPES.H` (`u8` ... `s32`); `main`; R7 placeholders (`f_xxxxx`,
  `g_xxxxx`, `unknown_xx`, `param_xx`), which get a semantic name only from evidence. Units
  `game_20000_*` and `game_helpers7` take no prefix until their identity is evidenced (case
  conventions still apply).
- **Module prefixes (user ruling):**

  | units | functions | enum values, constants |
  |---|---|---|
  | actor | `Actor_` | `ACTOR_` |
  | player, weapon_slots | `Player_`, `Weapon_` | `PLAYER_`, `WEAPON_` |
  | map_objects, object_render | `Object_` | `OBJECT_` |
  | doors, lift | `Door_`, `Lift_` | `DOOR_`, `LIFT_` |
  | level_triggers, command_sequencer | `Trigger_`, `Command_` | `TRIGGER_`, `COMMAND_` |
  | particle, particle_hits, debris, effects | `Particle_`, `Debris_`, `Fx_` | `PARTICLE_`, `DEBRIS_`, `FX_` |
  | model_draw, geometry, lights | `Model_`, `Geo_`, `Light_` | `MODEL_`, `GEO_`, `LIGHT_` |
  | sprites, texture_upload, text | `Sprite_`, `Tex_`, `Text_` | `SPRITE_`, `TEX_`, `TEXT_` |
  | hud, countdown_timer | `Hud_`, `Timer_` | `HUD_`, `TIMER_` |
  | frontend, pause_menu | `Menu_`, `Pause_` | `MENU_`, `PAUSE_` |
  | save_password, memcard | `Password_`, `Card_` | `PASSWORD_`, `CARD_` |
  | audio, audio_control, music_control | `Snd_`, `Music_` | `SND_`, `MUSIC_` |
  | movie | `Movie_` | `MOVIE_` |
  | main | `Sys_` (game loop, VRAM, loading, pad), `Heap_`, `Demo_` | `SYS_`, `HEAP_`, `DEMO_` |
  | tasker | lineage names (`Task...`), unchanged | `TASK_` |
  | game_20000_2/3/8, game_helpers7 | after the unit's identity is evidenced | |
  | renderer assembly (`asm/*.s`) | `Model_` | |

- **Placeholder names (R7, open naming work, advisory):** `[fgs]_[0-9a-f]{5,8}` (address names),
  `(local|param|var|field|unk|Unk|UNK|FUN|DAT|sub|LAB|PTR|m_unk|m_Unk)_\w*`, `(UnkType|unknown)_?\w*`, `var\d+`, and the
  address-like `[A-Za-z_]*?_?(?=[0-9a-fA-F]*[0-9])[0-9a-fA-F]{6,8}`.
- **Provenance tags:** `/* name: <evidence> */` above a definition gives the evidence for its name;
  `names invented` (or `names: invented`) marks invented names; `identity-TODO: <what is unclear>` marks
  an identity still to chase.
- **Rule tiers here:** [match] = R1, R2, R4, R17, R18 (R13 does not apply to C), as narrowed by the
  checker rows of `tools/rules.py`. R2 fires for an extern or prototype in a `.c` file whose symbol has an
  owner unit, a project header declaration or an SDK declaration; an extern for a symbol with no matched
  owner yet is advisory (R2-pending). "No magic numbers" (Source rules) is the advisory R11/R11-dec.
- **Parking and exceptions:**
  - a draft that does not match, or is superseded, goes to `archive/drafts/`, never `src/`;
  - a byte-exact function that fails a [match] rule gets `/* MATCH-PENDING: <rule>, <reason> */` at the
    function: it is not matched under the rules and is open work. `config/match_pending.json` holds
    entries whose markers are not in `src/` yet; the checker reads both;
  - a style rule the bytes defeat gets `/* RULE-EXCEPTION: <rule>, <reason> */` at the code and an entry
    in `config/exceptions.json` (function, unit, `rules_id`, deviation, reason). An exception never excuses
    a [match] rule: entries that excused R17 fake code or R4 offset access are revoked (`"status":
    "match_pending"`, evidence kept);
  - **dead locals (user ruling, R17's one exception):** an unused local that only reproduces retail's
    untouched frame space is genuine original code, matched under the rules, when its type and size are
    evidenced (for example two `RECT`s matching the callees' type) and a `wall-identifier` pass found no
    inlined callee, aggregate temporary, SDK macro local or dead-eliminated variable that explains the
    space. GCC 2.5.7 still allocates frame space for unused aggregate locals, so the original's leftover
    locals are plausible. Mark it `/* RULE-EXCEPTION: R17-dead-local, <evidence> */` directly above the
    declaration or at the function, the evidence naming the type/size source and what was ruled out
    (`...; ruled out: ...`), and record it in `config/exceptions.json` with `rules_id` `R17-dead-local`.
    Without that evidence the local stays MATCH-PENDING (R17); every other R17 finding stays unexcusable;
  - genuine hardware/IRQ `volatile` objects are listed in `volatile_allow` in `config/exceptions.json`,
    or carry a `/* volatile: <evidence> */` note at the declaration (R17).

  This is the Workflow's early-stop marker and the allow list of "Keep every exact match".
- **Headline figure:** matched under the rules (`tools/rules.py`), first in `PROGRESS.md`, beside the
  byte counts. `PROGRESS.md` is a table generated by `tools/progress.py` (through `tools/reports.py`); it
  is exempt from the no-score-snapshots hygiene rule and is never edited by hand.

### Campaign order

1. **Link gate.** Link the matched objects together with fallback bytes from the original for everything not yet
   matched, and require the result to hash to `f424660920764ac70f697b5b909a42fe259a76e60acbf5b01c6ca3ee8c8f8e98`.
   First prove that a link built entirely from original bytes reproduces the hash. Report how many bytes came from
   source using the link's own record, and cross-check that figure against `verify.py`.
2. **Tooling gaps**, gated by step 1: `f_588bc` (the start of SN's `2MBYTE.OBJ` startup module, not game code),
   the conflicting `_spu_RXX` bindings (`S_GKS` / `S_SCA`), the five ambiguous CD data placements, and the
   BSS/COMMON/section layout.
3. Matching lanes after that.


## Objective and scores

- The goal is an exact match for every function: identical instructions, constants and *ordered relocation
  targets*. Fuzzy and aggregate percentages are navigation only.
- If the project tracks per-function history, keep three numbers per function:
  - **current**: the score at the latest build;
  - **best**: the best score of the function's current source. It resets when the source changes;
  - **historical**: the best score ever, across all source revisions.

  `historical > best` means an earlier source matched better: a lost match to recover from version control
  before inventing anything new.
- A current-score dip in an *unchanged* function, caused by codegen shifting elsewhere in its translation unit,
  is not a regression.
- Masked bytes are not enough. A matching body that calls the wrong function, or names the wrong global, is a
  defect.

## Workflow

1. **Pick work.** Choose the lowest-scoring or never-matched functions, lost matches, or a unit close to complete.
   Check existing notes and verdicts (source comments, ledgers, history) first.
2. **Classify the gap** (`wall-identifier`): referent, then inline/call set, then control flow, then
   register/schedule. Fix the earliest class that still differs.
3. **Reconstruct** with `matcher`; recover hidden helpers with `helper-recovery`. Use `permute` only for a
   diagnosed register/schedule residue.
4. **Iterate** with `match <unit>`. Run `build` when a change spans units, for example a shared-header edit.
5. **Gate** before committing or merging. The gates must stay green.
6. **Stop.** Mark a complete body whose remaining difference is bounded by evidence, using the project's
   early-stop or stub marker, with a one-line reason.

## The byte gate

A function is done when **the whole binary, linked with that function compiled from source, still hashes to the
retail binary's hash**. Nothing else counts: not "looks equivalent", not a passing test, not a local diff of zero,
not a closeness score, not an agent's verdict. Every other signal is a filter feeding this gate. That is what makes
automation safe: a weak model or a buggy tool can waste time, but a wrong answer cannot land.

- **The gate has two states**, identical or failed; there is no third. It needs:
  - one hash contract per binary;
  - a build that links matched objects together with fallback bytes from the original for everything not yet
    matched;
  - a check that exits non-zero on any mismatch.

  Unmatched or non-matching source never enters the gated build.
- **Build it first.** A link made entirely from the original's bytes must reproduce the hash before progress is
  measured against it.
- **What the gate is blind to, and the rule that covers each blind spot:**

| blind to | rule |
|---|---|
| **coverage**: the gate is green at 0%, because the fallback supplies the original bytes | report how much of the image came from source, counted from the link's own record of where each byte came from; keep a second, independent check (per-function match) and treat any disagreement between the two as a bug |
| **which hash** | the gate verifies that its contract belongs to the binary it builds, and that the configured original still has that hash |
| **whether the build ran**: a failed link leaves the old binary in place | delete the output first, and judge from the linker's and the build's exit codes, never from the file |
| **incremental state** | gate only from a clean rebuild into a fresh directory |
| **whether the body is source**: pasted assembly (`__asm`, emitted bytes) matches trivially | inline-asm bodies are "verbatim", never counted as matched source; audit them |
| **what was committed**: a gate that substitutes drafts can destroy uncommitted work | commit matched work the moment it exists, and never gate on a dirty tree (other than tool-database churn) |

- **"Banked" is a printed line, not an intention.** Write "matched" or "banked" only from the gate's printed success
  line. A helper must refuse an empty work list. Count from the source, never from a ledger.
- **A red gate is a hash, not a diagnosis.** Turn it into a named symbol: attribute the differing bytes to the
  function and unit that hold them, or substitute the one draft into a copy of its unit and read the compiler's first
  conflict or redefinition error.
- **Batch, then bisect.** One clean rebuild verifies a batch of drafts. On red, bisect, or drop the units the byte
  attribution names, so that one bad draft cannot sink the rest. When parallel drafters declare a shared callee
  differently, the failure is the clash, not either draft: normalise the declarations, gate one at a time, and re-gate.
- **A slow gate is a bug.** Measure it before you fear it; run builds in parallel, and give the expensive binary its
  own lane rather than excluding it.

## Matching rules that are easy to get wrong

- **Levers are disposable experiments.** Never keep probes, unused declarations, fake locals, `volatile`
  carriers, padding or distorted source to move a score.
- **A dip is not a rejection.** If a change moves codegen toward retail's shape, keep it and compose the next
  lever on top. The gate decides only what is committed.
- **Prefer original spellings.** An inline function or macro is more likely the original spelling than a
  hand-expanded body; prefer it as the base unless the evidence says otherwise.
- **Trust surviving source.** Surviving original or lineage source is presumed authentic; record the decision
  to adopt it where the project keeps such decisions.
- **Bank exacts reached by experiment.** If unchanged source reaches exact under a disposable translation-unit
  experiment, record the exact state, then remove the experiment.
- **Keep every exact match.** Keep a 100% match even when its source breaks a project rule. Record the
  exception (function, rule, deviation) where the project keeps exceptions, and admit it through that gate's
  allow list.

## Source rules

- **One class, one definition**, in a shared header. No unit-local copies, layout views or placeholder shells.
- **Each function and global lives in its evidence-backed owner** unit and header. No scattered `extern`s and no
  macro aliases onto address names.
- **Access goes through typed members.** No raw offset casts, offset macros or casts of `this`. Use named casts
  only at real boundaries. A cast is usually a symptom: retype the member until it disappears.
- **Unclear identity: chase the evidence** (callers, storage, callees, mangling, vptr stores, RTTI, offsets). If
  it is still unclear, leave an identity-TODO marker. Never fabricate.
- **Names are semantic.** No address-derived names, compiler ordinals or `local_10`-style names in finished
  code. A name you invent is marked as invented wherever the project records provenance.
- **Use the era's real headers** (SDK, MFC, DirectX) instead of hand-rolled declarations. Mirror the original
  precompiled-header or prelude structure when the evidence shows one.
- **Typed enums for proven numeric domains.** Remember that retyping a parameter changes its mangled name.
- **No magic numbers.** Raw hex literals do not appear in C code: flags, masks, IDs, states, command
  codes, sizes, offsets and limits use an enum (for a domain of values) or a named constant (`#define`
  in the owning header), including bit masks (`#define GAME_FLAG_LAMP 0x100`, then `flags & GAME_FLAG_LAMP`).
  Hardware registers, GPU/GTE codes and other SDK concepts use the SDK's own names or macros.
  The hex lives only in the defining line. Name constants from evidence like any other name, and mark
  invented names. Exceptions: bulk initialiser tables that reproduce retail data values (for example the
  sine table) and address annotations in comments.
  The same applies to decimal literals: a number that carries meaning (a domain value such as a
  difficulty, kind or state; a case label on such a value; a shift, threshold, limit, size, count,
  timer, speed or other tuning parameter) is an enum member or named constant. Bare literals are
  allowed only where the number has no meaning beyond arithmetic: 0, 1 and -1 (initialisation,
  increments, sign), and 2 in halving or doubling.
- **Aggregates are proven from whole-object use** (`RECT`, `POINT`, vectors). Do not split one object into
  overlapping globals, and never model an interior address as separate storage: refine the owner.
- **Vtables are recovered mechanically:**
  - inherited slots are not redeclared;
  - overrides are marked;
  - new slots are `virtual`;
  - there are never dummy virtuals: a padded vtable can truncate the real one and crash.
- **No fake code**, storage, labels, aliases or padding to improve a score or the final layout.

## Data

- A datum gets a real definition with its address annotation. Compiler-generated data (string pools, FP
  constants, dynamic initialisers) is recorded as such, not given an invented owner.
- Aggregate data percentages do not prove that `.data` and `.bss` are correct. The compiler and linker choose
  layout; a field read through a separately declared global gets its own storage, so a link of the source alone
  can behave differently even when every function matches.

## Tests and verification

- Matching work runs no test suites: comparing against retail is the verification, and the gate runs before
  commit or merge.
- Tooling changes run the touched tool's own tests.
- Do not add tests that re-check a function's bytes against retail; the build already does that.
- Claim "matched" or "banked" only from the tool's own printed result (see "The byte gate"), never from
  memory or a ledger.

## Repository hygiene

- Tool inputs go in `config/`, generated output in an ignored `build/`, history in version control. Docs describe
  current usage and contracts only: no diaries, score snapshots or campaign logs.
- Source comments are operational only: markers, ABI or codegen constraints, explanations of unsafe seams.
  No history or scores. The unit opening comment and the function and section banners of the project profile
  are the one layout exception.
- Preserve concurrent changes: stage only your own unit of work, and never commit build state. Commit messages
  name the change (`match: reconstruct CThing::Method`, `tools: verify relocation targets`).
