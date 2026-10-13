# Library assembly

Sony Runtime Library modules that retail links as rebuilds matching no archive in `../sdk` cannot
be certified as Runtime 3.3. AGENTS.md ("Library-variant assembly") admits them as annotated
`.s` units, one per library module, assembled by ASPSX 2.34 (the default), by ASPSX 2.08 where
retail's bytes are that older assembler's output ("ASPSX 2.08 units" below), or by the pinned GNU
assembler where no pinned ASPSX can express the module ("GNU as units" below). Their bytes are **library assembly**: the gate's
`library_asm_bytes`, the `library_asm` row of `PROGRESS.md` and kind `library_asm` everywhere. They
are never part of `source_bytes`, never part of the hand-written `asm_bytes`, and never certified
library.

## Where they may go

`config/asm_regions.json`, category `library_asm` (ranges `[first, last]`, inclusive):

- `text`: the 16 `library_text`/`library_variant` gaps of `analysis/data_survey/gaps.json`
  (45,096 bytes);
- `data`: the `library_data`/`blocked_by_library_text` pieces of the same survey, adjacent pieces
  merged (5,273 bytes in `.rdata`/`.data`); LIBCD `C_010` text and data are excluded, because they
  certify as 3.3;
- `text_data`: none. A library module's `.text` holds instructions only.

`tools/asm_units.py` loads the file and refuses an inconsistent one (overlapping ranges, a byte in
two categories, data on text). verify.py and the gate refuse a library unit with a routine outside
`text` or a data definition outside `data`, a unit registered in the wrong category, and data
directives in `.text`.

## Adding a unit

1. **Identify the module.** `gaps.json` → the gap's `modules` list gives the archive module aligned
   at each address, its closest version and its exports. To recompute: `analysis/data_survey/libmodules.py`
   (indel-tolerant alignment of every SDK version), `analysis/data_survey/libfuncs.py` (per
   function), `../shared-tools/libmatch.py` (relocation-masked exact placement).
2. **Diff against the nearest archive.** From the checkout root:
   `python analysis/data_survey/moddiff.py "<library path under ../sdk>" <MODULE> <hex address> [.text|.data|.rdata]`
   lists every word that differs from retail (relocated words masked). State the result in the
   module header. Relocation differences are found by reading the retail operands of the archive's
   relocated words (`analysis/runtime_linked.json` → the candidate's `unresolved_relocations` and
   `hilo_pairs`, or the module's participant entry in `binding_conflict_diagnostics`).
3. **Derive names from the object.** Exports, imports and section layout come from the archive
   member:
   ```
   python -c "import sys; sys.path.insert(0, '../shared-tools'); import psyqobj
   for m in psyqobj.load(sys.argv[1]):
       if not isinstance(m, Exception) and m.name == sys.argv[2]:
           print(m.sections, {k: len(v) for k, v in m.code.items()}, m.xdefs, m.xrefs, m.patches)" "<LIB>" <MODULE>
   ```
   Exported routines and data keep the library's exact names (`xdefs`). Imports keep theirs
   (`xrefs`). A routine or datum with no name in any archive keeps an address name (`f_xxxxx`,
   `g_xxxxx`) until evidence gives one. A data word that the rebuilt module owns but the archive
   imported (the `_spu_RXX` copies) is a static of the module and keeps the imported name.
4. **Write `asm/lib/<library>_<module>.s`**, lower case (`libspu_s_gks.s`, `libcd_bios.s`). CRLF
   line endings, `#include <asm.h>` for register names, `.set noreorder`, every delay slot written
   out. Exported labels get `.globl`; a data label without `.globl` is a static.
5. **Write `config/<unit>.json`**: `functions` (every routine, address and size; a module's
   local routines too), `symbols` (each import and its retail address; it must agree with every
   other unit map, and with `config/runtime_bindings.json` where that resolves the name), and
   `static_data` (each static data label: address, size, section).
6. **Add the data to `config/data.json`** under the unit name: one definition per data label
   (exported or static) with its address and size. Each must lie in a `library_asm` data range.
7. **Register** the unit in `LIBRARY_ASM_UNITS` in `tools/verify.py`.
8. **Match**: `python tools/verify.py --units <unit> --output builds/<unit>.json`. Every routine
   and the unit's data check must be `match`. Relocation targets are checked by target, so a
   reference to the wrong symbol fails even when the bytes agree.
9. **Gate** on a clean tree (`python tools/gate.py`), and quote the `GATE OK` line with its
   `library_asm_bytes`.
10. **Runtime reports.** `analysis/runtime_relocations.py` gives every archive candidate that
    library assembly covers whole the status `library_asm` (with `library_asm_units` and the old
    `archive_status`); a unit covering part of a candidate is an error. Its relocations stay in the
    binding evidence. Run `python tools/reports.py` on main, or `analysis/runtime_relocations.py`
    and `analysis/runtime_data.py` in a lane, and commit the changed `analysis/runtime_linked.json`
    and `config/runtime_bindings.json` with the unit.

## Annotation standard

Same as the renderer units (AGENTS.md, "Hand-written assembly"), plus a module header:

- **Module header**: library, module, exports (name, offset, size), imports, the module's own data,
  and how it differs from the nearest archived version, in one quotable line (for example
  "Runtime 3.3 S_GKS with a module-local `_spu_RXX` instead of the SPU module's global").
- **Per routine**: purpose (with the C prototype when the library documents one), inputs, outputs,
  clobbered registers, memory and hardware registers read or written, scratchpad use, callers and
  callees.
- **Blocks and instructions**: a comment on every logical block and every non-obvious instruction:
  delay slots (say whether the slot does real work), macro expansions (`lw reg, symbol` is
  `lui reg` + `lw`, no `$at`), `$at` use, hardware register offsets, compiler idioms such as a
  `break` compiled as a jump back to the loop exit.
- **Data**: a comment per table or datum: purpose, layout (element type and count), users.
- Constants get `#define`s named after the library header (`libspu.h`, `libcd.h`) where one exists.

## ASPSX notes

- `lw reg, symbol` / `lw reg, static` assemble to `lui` + `lw` with a HI16/LO16 pair, against the
  symbol (import) or the section (static), as GCC's output does.
- `j label` to a local `$label` assembles to a section-relative MIPS26 relocation, like the archive's.
- `move rd, rs` is `addu rd, rs, zero`. Write `addiu`/`ori` for constants when the retail encoding
  is one of them, rather than `li`.
- Comments are `/* */`; the C preprocessor removes them before ASPSX. Any warning fails the build.
- Memory operands need an explicit offset (`0(v0)`). Strings are `.ascii "...\000"`; never put a
  raw NUL byte in a source (ASPSX accepts it, GNU as and the tools do not).
- `lui r, CONST >> 16` with a symbolic CONST is a type-26 patch, not a constant: write constants
  as literals or `#define`s that the preprocessor folds.

## Split HI16/LO16 pairs

ASPSX writes the HI16/LO16 relocations (PsyQ patch types 82/84) only for its own back-to-back
macros (`la`, `lw reg, sym`, `sw reg, sym`) and has no `%hi`/`%lo`. Retail Sony code also
separates the halves: a `lui` before a `jal` with the `addiu` in its delay slot, two `lui`s then
two `addiu`s, an `addu` between `lui` and `addiu`. Write them with `asm/lib/hilo.h`:

    lui     a0, HI16(g_80076984)        /* patch type 26: (E + 0x8000) >> 16 */
    jal     printf
    addiu   a0, a0, LO16(g_80076984)    /* patch type 30: E - (HI16(E) << 16) */

PsyQ types 26, 28 and 30 are the 16-bit immediate fields of `lui`, `ori`/`andi` and
`addiu`/loads/stores. PSYLINK evaluates the whole expression and writes it into the field after a
range check (type 26 takes 0..0xffff, type 30 -0x8000..0x7fff; a bare `lui r, sym` or
`addiu r, r, sym` is the full address and fails it). For the two hilo.h shapes PSYLINK writes
exactly the HI16/LO16 halves of E (the `+ 0x8000` carries the sign of the low half): the
ASPSX-assembled LIBGTE MTX_00 linked by PSYLINK alone reproduces retail's 2592 `.text` and 784
`.data` bytes. bytecheck.py, the gate's linker and generated_witness.py recognise exactly these
shapes (E is one symbol or section plus a constant; ASPSX folds the constants, so
`[.data] + 656 + 0x8000` is compared as `[.data] + 656`), check each as the HI16 or LO16 half of
E (target resolved through the unit map, anchors as for 82/84), and require both halves of E
inside the same routine. Any other type-26 or type-28 expression is refused; type 30 otherwise
is the GP-relative form. ASPSX accepts LO16 only on `addiu`: on a load or store it treats the
symbolic offset as a macro (it wants `$at`).

An end label (one past the last `.text` byte, PATCHGTE's copy bound) resolves to the end of the
routine that ends the section, in bytecheck.py and the gate; no other `.text` offset outside a
routine resolves.

## ASPSX 2.08 units

A unit selects ASPSX 2.08 (`A208`, from the Runtime Library 3.0 tools, pinned in
`config/toolchain.lock.json`: `assemblers`, its sha256 in `files` and `library_assemblers`) in
`config/asm_build.json` (`units`, keyed by the unit's path, `"assembler": "A208"` and a
`reason`). It is for library-variant units only; game code and the renderer units stay on 2.34.
`tools/build_asm.py` runs it exactly like the default ASPSX, from `toolchain/A208`, after checking
its hash against the lock; verify.py and the gate check every locked file as well. An unknown
assembler, an entry without a reason or a hash mismatch is refused.

Use it where retail's bytes are 2.08's output:

- its signed `div`/`rem` macros end in the overflow trap `tge zero, zero, 0x5d` (0x00001770)
  where 2.21 and 2.34 write `break 6`: `div rd, rs, rt` is `div zero, rs, rt; bnez rt; nop;
  break 7; addiu at, zero, -1; bne rt, at; lui at, 0x8000; bne rs, at; nop; tge; mflo rd`
  (LIBGTE FOG_00, LIBSPU S_N2P, the SPU module's `_spu_ioctl`). Retail's variant SPU/GTE modules
  were therefore assembled with the Runtime 3.0-era assembler; the variant runtime is the one
  the 3.6 kanji samples link (KANJIFNT `MAIN.EXE`, SJISCODE `CODEVIEW.EXE`: LIBGPU sys.c 1.104,
  intr.c 1.71);
- it inserts no hazard `nop` under `.set noreorder` (LIBSPU S_SRMP: a `mult` two instructions
  after an `mfhi`, where 2.34 adds one).

2.08 differences to write around:

- No `tge`/`teq` mnemonic and no `%hi`/`%lo`: the trap comes only from the `div`/`rem` macros.
- No `negu`: write `subu rd, zero, rs`. No `$at` name: write `$1` (asm.h's `AT` is `$at`). Under
  `.set at` (the default) an explicit `$1` is a warning; macros that need `$at` fail under
  `.set noat`.
- No `.section`: a module-local `.lcomm` goes to `.bss`, as with 2.34.
- A local routine (no `.globl`) needs `.ent`/`.end` for its local symbol record.
- An indexed symbol load, `lw rd, sym(rs)`, is `lui at; addiu at, at; addu at, at, rs;
  lw rd, 0(at)` (GCC's spelling of a table read). `li`, `la`, `move`, `rem`/`remu` work as in
  2.34.

## GNU as units

A module that no pinned ASPSX can express is assembled by the GNU assembler instead, chosen per
unit in `config/asm_build.json` (`"assembler": "GNU_AS"`, with a `reason`). The one case is an
absolute `lui`/`sw` reference to the module's own `.sbss` block (LIBGTE PATCHGTE): ASPSX 2.08
and 2.34 put a 16-byte `.lcomm` in `.bss` under their default `-G` and address `.sbss` data
`$gp`-relative, and 2.08 has no `.section`.

The unit is preprocessed by CPPPSX as usual, then `tools/build_asm.py` runs the GNU assembler
pinned in `config/toolchain.lock.json` (`gnu_as`: decompals binutils v0.10, GNU as 2.40, the
executable's and the release zip's sha256 verified before every run and by verify.py and the
gate) with `-march=r3000 -mabi=32 -EL -G0 --fatal-warnings`, and `tools/elf_lnk.py` converts the
ELF object into the PsyQ LNK object that every other tool reads (section numbering, XDEF/XREF,
local symbols, patches with the addends in the expressions, as ASPSX writes them; anything it
cannot express exactly is refused). PSYLINK links the converted objects to retail's bytes.
Every word is still an instruction written by its mnemonic: no `.word` in `.text`.

GNU as differences to write around:

- `move` is `or` in GNU as: write GCC's `addu rd, rs, zero`.
- GNU's `rem`/`remu`/`div` macros order the checks differently from ASPSX's: write the checked
  division out (`div zero, a, b`; `bnez b`; `nop`; `break 7`; the -1 / INT_MIN tests; the trap;
  `mflo`/`mfhi`).
- `%hi(sym)` / `%lo(sym)` work directly; `.rdata` goes to `.rodata`, which the converter maps
  back. Macros that need `$at` need `.set at`.
- A local routine label without `.globl` (not spelled `$name`) gets a local-symbol record: map
  it with `"static": true`, like a C static. Code labels are `$name`.

## Example

`asm/lib/libspu_s_gks.s` (LIBSPU `S_GKS`, `SpuGetKeyStatus` at 0x800590a8, 148 bytes, and its
`_spu_RXX` word at 0x80073afc).
