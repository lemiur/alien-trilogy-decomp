# Alien Trilogy (PS1) matching decompilation

Recovers C source for Alien Trilogy (USA, `SLUS_000.07`) that recompiles to the
original bytes. The point of decompilation is producing human readable code. 

- **Target:** `games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07`, SHA-256
  `f424660920764ac70f697b5b909a42fe259a76e60acbf5b01c6ca3ee8c8f8e98`. Loads at
  `0x80010000`, 514,048-byte payload, entry `0x800588c4`, GP `0x8008cd74`.
- **Toolchain:** GCC 2.5.7 (`toolchain/C257`), `-O2 -G8`, ASPSX 2.34 (`toolchain/A234`),
  Sony Runtime Library 3.3. Pinned by hash in `config/toolchain.lock.json`.

## Status

Figures from `PROGRESS.md`, which `tools/progress.py` counts from the gate's per-byte record
(`builds/gate/provenance.json`) and cross-checks against `verify.py` and the runtime reports:

| Category | Count | Bytes |
|---|---:|---:|
| Game C functions | 478 | 72,836 |
| Game C initialized data | 204 | 5,565 |
| Compiler-generated data of game C units | 12 | 375 |
| **Decompiled game C (gate `source_bytes`)** | | **78,776** |
| Hand-written assembly, GTE/renderer (not C) | 24 functions | 5,984 |
| Sony Runtime Library 3.3 text (certified, still from fallback) | 95 modules | 27,764 |
| Sony Runtime Library 3.3 initialized data (certified, still from fallback) | 32 sections | 100,933 |
| Fallback, not yet accounted for | | 300,591 |

Payload: 514,048 bytes. **Decompiled game C is 15.3% of it**; the Sony library rows are a
separate figure and are not decompiled code.

## What is not included

This repository contains no game data and no Sony or SN Systems software. To build and
verify you need your own copy of the retail executable (`SLUS_000.07`, SHA-256 above) under
`../games/`, and the GCC 2.5.7 / ASPSX toolchain under `../toolchain/` matching
`config/toolchain.lock.json`. The draft archive and per-round agent workspaces referred to
in `docs/` and some tool docstrings (`archive/`, `analysis/<round>/`) are not published.

## Layout

| Path | Contents |
|---|---|
| `src/` | The 95 registered C units (`UNITS` in `tools/verify.py`). Every function in them is byte-verified. A unit holds the functions of one subsystem in address order; it need not be contiguous in the executable. Subsystem names (`audio`, `audio_control`, `actor_pool`, `frontend`, `memcard`, `pause_menu`, ...) were chosen from the code; the remaining `recovery_*`, `game_*`, `fresh_*`, `strict12_*` and `*_helpers*` names come from the recovery round that produced them. No file name is evidence of the original source layout. |
| `include/` | Shared type and declaration headers, one owner per type or symbol (`README.TXT`): actors, audio, camera and view, display buffers, game state, heap, level, menu, model rows, particles, saves, sprites and textures, tasks, text. |
| `asm/` | The 11 hand-written GTE/renderer units (`ASM_UNITS`), reported apart from C. |
| `config/` | One symbol map per unit (function addresses/sizes and the addresses of everything it references), plus `data.json` (initialized data placement), `toolchain.lock.json` and `runtime_bindings.json`. |
| `tools/` | `build.py` (one unit through DOSBox-X), `bytecheck.py` / `data_check.py` (strict byte comparison), `verify.py` (rebuild and check everything), `gate.py` (whole-executable byte gate), `reports.py` (report chain), `progress.py`, `diff.py`. |
| `analysis/` | Library matching (`runtime_*.py`), startup/GP layout, link plan, and the validation scripts listed below. The few subfolders here are evidence that those scripts read. |
| `reports/` | Current results (`latest.json`, `progress.json`, `link_plan.json`, validation reports). |
| `docs/` | `HISTORY.md` (the previous README: a per-round log, with paths from before the 2026-10-08 reorganization) and `notes/`. |
| `builds/` | Build output (git-ignored). `verify.py` creates one `job-*` folder per unit. A few older jobs are kept because evidence reports cite them. |

## Verify

From this folder (needs Python and `C:\DOSBox-X\dosbox-x.exe`; `capstone` only for `diff.py`):

```
python tools/gate.py
python tools/verify.py
```

`gate.py` is the whole-executable byte gate. On a clean tree it rebuilds every registered
unit into a fresh `builds/gate/`, links the matched functions and the data placed by
`config/data.json` over fallback bytes of the original (its own Python linker, which applies
every object relocation), writes the complete PS-X EXE with a per-byte provenance record
(`builds/gate/provenance.json`), and requires the SHA-256 in `config/gate.json`. It also runs
`verify.py` independently and fails if the two disagree about which bytes came from source.
Success is exactly one line, `GATE OK sha256=<hash> source_bytes=N/514048 ...`; a failure lists
the unit and function or datum behind every differing byte. `--verify-report reports/latest.json`
reuses a current report instead of rerunning `verify.py`; `--base-case` links original bytes
only. Its tests: `python -m unittest tools/test_gate.py`.

`verify.py` rebuilds every unit and compares every function and data byte, resolving
relocations from the maps (`--jobs` sets the number of parallel builds). To regenerate the library, link and progress reports,
run the chain in this order (from a clean tree, since the gate goes first):

```
python tools/reports.py --jobs 6
```

which runs `tools/gate.py`, `tools/verify.py`, `analysis/runtime_symbols.py`,
`analysis/runtime_relocations.py`, `analysis/runtime_data.py`, `analysis/link_plan.py` and
`tools/progress.py`, and stops at the first failure. `progress.py` fails if the gate record,
`verify.py`, `generated_witness.py`, the runtime reports and the link plan disagree on any byte.
`analysis/inventory.py` (the candidate inventory) is rerun only when its inputs change.
Tests of the counting logic: `python -m unittest tools/test_progress.py`.

These validation scripts also pass on the current tree: `symbol_bindings_validation`,
`menu_choices_evidence`, `gp_access_layout`, `round5/6/7_integration_review`,
`certification_batch_validation`, `runtime_variant_diagnostics`,
`function_boundary_validation_v2`, `object_export_audit`, `object_export_validation`,
`original_entry_audit`, `menu_round6_evidence`, `menu_card_round7_evidence`,
`menu_input_round7_evidence`, `worker_range_audit`, `final_evidence_freshness` and
`historical_draft_reconciliation` (all in `analysis/`).

## Known issues

- `__main` at `0x800588bc` is not game code. It is the start of SN's `2MBYTE.OBJ`
  startup module (unique 196-byte match), which the runtime analysis does not yet
  recognise. `main` (`src/helpers.c`) calls it through the `__main` call GCC 2.5.7
  emits at the top of every `main`; the source spells no call.
- `_spu_RXX` has conflicting implied addresses (`S_GKS`: `0x80073afc`,
  `S_SCA`: `0x80073cf0`), so those two modules stay uncertified.
- Five identical CD data sections have ten possible placements each.
- `f_1d0e0` matches, but its C shift is undefined for some inputs the machine code accepts.
- BSS/COMMON/section layout is not done: the gate places source bytes at the addresses the
  maps give and takes everything else (string pools, BSS, unmatched code) from the original.
- A byte match proves compiled output, not names or types. Most identifiers are still
  address-based (`f_xxxxx`, `g_xxxxx`), and a few functions (e.g. `actor_play_animation`) are
  written with raw byte offsets rather than real struct types.

An independent audit of these claims is in `../decomp-audit/README.md`.

## Etiquette and Philosophy

#### Use this projects as templates for what your result should look like

###### Decompilation Projects

https://github.com/Xeeynamo/sotn-decomp 

https://github.com/shdecompilations/silent-hill-decomp/ 

https://github.com/doldecomp/melee

https://github.com/VetriTheRetri/ssb-decomp-re

###### Game Source Code Structure

https://github.com/id-Software/Quake-III-Arena

https://github.com/id-Software/DOOM-3-BFG

https://github.com/id-Software/DOOM

###### General Source Code (This is what good, human readable code looks like)

https://github.com/torvalds/linux

https://github.com/gcc-mirror/gcc
