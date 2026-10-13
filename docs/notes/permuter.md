# decomp-permuter

The `permute` verb. A permuter result is a lead, never source: translate an improved or exact state
into an authentic change (declaration order, expression shape, a temporary a programmer would write)
and verify it with `match <unit>`. Never land permuter artefacts (`(0, x)`, pointless casts,
unexplained reorderings, fake temporaries).

## Install

- Checkout: `E:\Compiler Identification\shared-tools\decomp-permuter`, outside the repository,
  cloned from https://github.com/simonlindholm/decomp-permuter and pinned at
  **`8556c81d80d1c1af98a858c8f4dc951357f29139`** (2026-10-06). `tools/permute_run.py` refuses any
  other commit and a checkout with local modifications.
- Python dependencies: a venv at `decomp-permuter\venv` with `toml` only (the README's other
  packages are optional: `pynacl` is for permuter@home, `Levenshtein` for another diff
  algorithm). `pycparser` is vendored by the permuter.
  `python -m venv venv; venv\Scripts\python -m pip install toml`
- No MIPS binutils, host `cpp` or host compiler are needed; see "How it is adapted".

## Run

```
python tools/permute_setup.py f_40edc --context src/pause_menu.c
python tools/permute_run.py builds/permute/f_40edc --minutes 45 -j 4 --quiet
```

- `permute_setup.py <f>` finds the draft defining `<f>` under `archive/drafts/src/` (or `--draft`),
  appends it to `--context` (a live unit supplying types and declarations) when the draft is not
  self-contained, and writes `builds/permute/<f>/` with `base.c`, `target.o`, `compile.sh`,
  `settings.toml` and `symbols.json`. Maps merged, later winning: `analysis/runtime_symbols.json`
  exports (SDK names), the context unit's `config/<unit>.json`, the draft's
  `archive/drafts/config/<draft>.json`, then each `--map`. `--section .rdata=0x...` places a
  jump table. The context's own `extern` for `<f>` is dropped (the draft's signature wins). It prints
  the base's word difference from retail; check it against the draft's recorded wall before searching.
- When a draft needs a different context than its file provides (several drafts in one file, or a
  draft that duplicates the context's typedefs), put a copy of just the function, unchanged, in
  `archive/drafts/src/research/<f>.c` and pass it with `--draft`.
- `permute_run.py` runs the permuter with `--stack-diffs` (frame layout is part of the match) for
  `--minutes`, then kills the whole process tree. Flags after `--` go to the permuter
  (`--debug` scores the base only). Progress is in `builds/permute/<f>/run.log`; results are
  `output-<score>-<n>/source.c` (lower is better, 0 means the disassembly matches).
- Workers: at most 4 (`-j`), because every compile is a DOSBox-X process and other lanes share
  the machine. Run one function at a time.
- Check a candidate exactly, against retail bytes:
  `python tools/permute_compile.py --check --map builds/permute/<f>/symbols.json --function <f> <file.c>`.
  This is a scratch check; only `verify.py` and the gate decide what is matched.

## How it is adapted

- `compile.sh` calls `tools/permute_compile.py`, which compiles with `build.py`'s own job functions
  (`prepare_job`, `run_dosbox`: GCC 2.5.7 `CC1PSX -O2 -G8` with the 3.3 driver predefines, ASPSX
  2.34, SDK headers on E:, `include/` on F:) in a fresh job directory per compile, so workers never
  share one. It takes the function from the PsyQ object, applies every patch against
  `symbols.json` at the function's retail address (the arithmetic of `bytecheck.py`) and writes a
  minimal ELF32 MIPS object. Referents are compared as resolved values, so a wrong global shows
  as a differing immediate.
- `target.o` is the retail bytes in the same ELF form; the scorer's `objdump_command` is
  `permute_compile.py --disassemble` (capstone, objdump `-drz` line format).
- The permuter's preprocess step runs a host `cpp`; `base.c` is already CPPPSX output, so
  `permute_run.py` replaces that step with reading the file (a monkeypatch in the launcher; the
  checkout is not edited). Git for Windows' bash runs `compile.sh`.
- `base.c` holds only the target function, the static or inline functions it may inline, and
  prototypes for functions named outside other bodies; other definitions are removed.

## Speed

One compile is about 2.7 s on an idle machine (DOSBox-X start-up dominates; the SDK header hash
check is 12 ms). With `-j 4` and other lanes building: 3,061 candidates in 45 minutes (1.1/s) for
f_40edc, and 1,600-2,200 per 20-minute run (1.3-1.8/s) for the smaller functions. The scorer's
disassembly subprocess takes about 15% of the time.

## Reading the frontier

- Diff an output against `base.c` with the formatting normalised (strip indentation and braces):
  most outputs differ only by artefacts, and the mechanism is what to keep.
- For a register-order residue, read the allocator's ranking: rebuild a candidate with
  `permute_compile.py --check --keep`, rerun cc1 in that job with `-dl -dg` (see
  `gcc257_codegen_recipes.md`, "Reading the compiler's RTL dumps") and compare each pseudo's
  references and live length. Pattern F there (loop-weighted references, `do { } while (0)`) came
  out of this lane's frontiers.

## Caveats

- The score ignores what `objdump` output loses: it compares mnemonics and operands of the
  resolved function only, not data. Branch targets are compared as offsets.
- A draft whose literals or jump tables live in a section the context also uses resolves them
  at the merged map's section base, which can differ from retail; fix it with `--section`
  before reading the diff, or ignore that immediate.
- Random passes rename types, add casts, temporaries and `(0, x)`-style no-ops; most improvements
  are of that kind. Read an output's diff from `base.c`, name the mechanism, and test a real
  spelling with `permute_compile.py --check`; record an artefact-only exact state in the draft's
  reason and leave the function parked.
