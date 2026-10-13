# Alien Trilogy PS1 matching decompilation

Target: USA `SLUS_000.07`, SHA-256
`f424660920764ac70f697b5b909a42fe259a76e60acbf5b01c6ca3ee8c8f8e98`.
The executable loads at `0x80010000`, has a 514,048-byte payload,
and enters at `0x800588c4`. Startup establishes GP as `0x8008cd74`.

The selected compiler is GCC 2.5.7 `[AL 1.1, MM 40]`, with
`-O2 -G8`, ASPSX 2.34, and Runtime Library 3.3. Compiler,
assembler, supporting files, and reference hashes are pinned in
`config/toolchain.lock.json`. The existing SDK and executable files remain
in the parent workspace.

ASPSX 2.21 and 2.34 both matched the original eight seeds. Two additional
functions distinguish them: identical compiler assembly inputs produce
68 versus 64 bytes for `f_3d62c`, and 84 versus 80 bytes for `f_1b674`.
Only 2.34 matches both original functions. The evidence is preserved in
`reports/compiler_identification.json`, and 2.34 is now the default.

## Rebuild and verify

From this directory:

```powershell
python tools/verify.py
```

This builds each source unit with ASPSX 2.34, resolves supported PsyQ
relocations using explicit address maps, and compares every function byte.
It requires the object function extent to equal the target size. Missing
symbols, unsupported relocations, length differences, and mismatches fail
the check. Results and source/map hashes are saved to `reports/latest.json`.
The verifier snapshots source, address maps, and the initialized-data manifest
for reproducible comparisons. Reports pin each complete object file hash, and
progress generation rejects changed objects or initialized-data definitions.
It also checks all mapped initialized definitions and five generated switch
tables, including all sixty switch-table relocations and the initialized menu pointer sites. The verifier
rejects functions registered in multiple translation units. ASPSX 2.21
remains available for diagnostic comparisons with
`--assemblers A221 A234`; that comparison intentionally fails the two
assembler discriminators above.
Each build retains its input C, preprocessed input, assembly, object, and
individual comparison reports under a unique `builds/job-*` directory.
It also retains per-tool stdout and DOSBox logs with a failing-stage label.
Guest compiler stderr remains a diagnostic limitation: DOSBox-X's shell
did not execute the tools with `2>` or `2>&1` redirection during testing.

The build runner mounts the existing tools read-only and gives each DOSBox-X
process a separate working drive. Parallel builds do not share `RUN.BAT`
or output files. Python and `C:\DOSBox-X\dosbox-x.exe` must be installed.
`capstone` is required only for the instruction diff command below.

For one translation unit and function:

```powershell
python tools/build.py --source src/seeds.c --json-report reports/build.json
# Use the object path returned by the command above:
python tools/bytecheck.py --object builds/job-NAME/INPUT.OBJ --symbol-map config/seeds.json --reference ../_work/AT_SLUS_000.07 --function f_15564 --json-report reports/f_15564.json
python tools/diff.py --object builds/job-NAME/INPUT.OBJ --symbol-map config/seeds.json --function f_15564
```

`diff.py` marks unresolved relocation instructions with `R`. It helps tune
C source but never certifies a match. The original `_work/atmatch.py`
experiment ignored relocation fields; its `EXACT` label is distinct from
the complete linked byte comparisons here.

## Evidence and scope

`src/seeds.c` preserves the original eight recovered functions.
`src/additional.c` contains newly recovered list, callback, and wrapper code.
`src/helpers.c` contains the next helper batch. Address-based names remain
where original names and signatures are unknown. Data types and names are
reconstruction choices; byte equality does not recover the original source
spelling or prove every C prototype.

Additional units now cover allocator logic, sorted task insertion, callbacks,
mode/string helpers, list operations, audio control, SPU attribute builders,
and the graphics task that switches rendering buffers. SPU structures follow
the supplied Runtime Library 3.3 `LIBSPU.H` layout.

`reports/latest.json` is the authoritative result for its recorded source
hashes. Its payload coverage includes data, padding, and runtime bytes in
the denominator, so it is a measured lower bound, not the percentage of
game functions decompiled. This project does **not** yet rebuild the whole
executable from C, and it does not count copied executable bytes as progress.

See `PROGRESS.md` and `reports/progress.json` for current recovered C,
initialized globals, semantic strings, colors, switch-table data, and measured
coverage. Independently matched Runtime initialized sections contribute
100,849 bytes.
`reports/checker_validation.json` records the
relocation and rejection checks, including the original nonzero `+48` GP
displacement in `level_unload`.

`analysis/inventory.py` records 1,038 candidate starts and 57 substantial
Runtime Library 3.3 relocation-masked module matches. Candidate boundaries
and spans remain provisional: JAL-looking words can occur in data, and the
next discovered start need not be the actual function end.

`analysis/runtime_symbols.py` expands the original and supplemental candidates
into 245 exported symbol records. It separately verifies 48 unrelocated runtime
module texts, covering 5,908 distinct bytes. The supplemental scan includes
unique aligned small modules excluded by the original size threshold, groups
duplicate aliases, and excludes overlapping ranges. It also identifies two
additional relocation-masked CD modules for subsequent strict verification.
These library object matches are separate
from recovered C functions. Relocated module matches remain provisional.
For example, `STRCPY` exports `strcpy` at `0x800699d0` and its complete
68-byte text matches exactly.

`analysis/runtime_relocations.py` resolves the relocated library candidates
and compares every text byte. It verifies 93 of the 95 candidate module
texts, covering 27,528 distinct bytes. `S_GKS` and `S_SCA` remain uncertified
because references to `_spu_RXX` imply conflicting addresses. Bindings
inferred from original operands are recorded with provenance and checked
for consistency; they are not independent golden evidence.

The latest investigation preserves the `_spu_RXX` conflict explicitly:
`S_GKS` implies `0x80073afc`, while fourteen paired references in `S_SCA`
imply `0x80073cf0`. Both relocation-masked module placements are unique.
`binding_conflict_diagnostics` in `analysis/runtime_linked.json` records the
participants; `config/runtime_bindings.json` retains the unresolved binding.

`analysis/runtime_data.py` verifies complete initialized Runtime 3.3 sections
from those matched modules. It certifies 23 `.data`, `.sdata`, and `.rdata`
sections, covering 9,977 distinct bytes and applying all 162 relocations.
It independently scans complete, unrelocated data-only modules in the pinned
archives. `CSTBL`, `DVLC`, and `S_RMP` have unique aligned full-section matches,
adding 90,872 bytes. `FONTTEX` is an exact alias within already certified FONT
data and contributes no additional coverage. The data-only module exports
and their addresses are recorded for the eventual link. Total certified
Runtime initialized data now covers 26 sections and 100,849 distinct bytes.
Five identical CD data sections have ten possible placements each and remain
unanchored. Runtime initialized data is counted separately from recovered C
data and runtime text. The analyzer independently replays the upstream text
and symbol bindings, checks archive and reference hashes, and rejects edited
or stale reports. `reports/runtime_data_validation.json` records replay and
report-tampering checks plus actual relocation rejection cases, including
the data-only module placement, extent, hash, and occurrence-count checks.

Regenerate these analyses from either the workspace or project directory:

```powershell
python analysis/inventory.py
python analysis/runtime_symbols.py
python analysis/runtime_relocations.py
python analysis/runtime_data.py
python tools/progress.py
python analysis/link_plan.py
python analysis/link_plan_validation.py
```

## Work remaining for a full match

1. Establish actual function boundaries and distinguish game code, runtime
   code, data, and padding. The candidate inventory provides a search list.
2. Recover remaining functions in readable C and add proven addresses,
   complete lengths, and bindings to their unit maps. Run the full verifier
   after each source change; compilation context can affect generated code.
3. Resolve the relocated Runtime 3.3 modules and original section placement.
   Check library definitions, constants, `.sdata`, `.sbss`, and BSS layout.
4. Reconstruct the executable entry/startup, linker layout, data, header,
   and padding, then compare a completely rebuilt executable against the
   reference SHA-256. No original-byte fallback qualifies as decompilation.

The earlier Luna-agent recovery batch added nineteen functions: heap setup and
snapshot restore, display controls, menu helpers, text measurement and number
parsing, resource preparation, game-state helpers, and a text-buffer reset.
New code lives in `heap_setup`, `heap_snapshot`, `game_helpers2`,
`game_helpers3`, `game_state`, and `game_text`; all six units are registered
in the default verifier. Seven additional initialized definitions also match.

`game_helpers4` uses `g_page_9` at `0x80090000` as a synthetic address anchor
for observed signed offsets to resource globals. It is a temporary linker
placeholder, not an initialized object or a claim that the original source
defined a page-sized array. Its bytes are not counted as recovered data;
the individual global definitions and placement remain part of the link backlog.

An earlier GPT-6 Luna batch added twenty-nine functions and 2,820 code bytes.
It resolves heap capture `f_14ecc` (188 bytes), card retry `f_1809c` (216 bytes),
the heap error display and counter, twelve menu/resource helpers, and thirteen
actor orientation, boundary, velocity, spawn, and action helpers. The new units
are `heap_helpers`, `game_helpers4`, and `game_helpers5`; capture and retry are
integrated into the existing units. The actor-kind switch table contributes
56 bytes of independently relocated data, and the menu head contributes four.

Matching source experiments are retained under `analysis/heap_recovery`,
`analysis/card_next`, and `analysis/actor_next`. Unmatched exploratory drafts
remain outside the certified source units. Source-level equivalence can change
the old compiler's register allocation, scheduling, and switch lowering.

The next six-agent GPT-6 Luna recovery pass adds actor distance classifiers,
card/menu callbacks and text layout, CD file-loading helpers, text rendering,
and audio helpers. Its certified source lives in `game_helpers6`,
`card_helpers`, `heap_helpers2`, `text_helpers2`, `audio_helpers2`, and
`accessors2`. The pass adds 28 byte-matched C functions and 2,744 C code bytes.
Builds and complete relocation-aware byte checks are retained under the
corresponding `analysis/luna_*_round2` directories. Unmatched experiments
remain outside the default source units, including the SPU key-status draft
under `analysis/spu_status_recovery`.

`analysis/runtime_variant_diagnostics.py` compares the conflicting SPU objects
across Runtime 3.0, 3.3, and 3.5 and checks their operand-implied data addresses.
Both candidate `_spu_RXX` addresses contain the same hardware pointer word;
neither surrounding window matches the full archive data section, so that
evidence does not resolve the binding conflict.

`analysis/certification_batch_validation.py` independently replays all 35 new
SDK module matches, checks unrelocated match uniqueness/alignment and imported
symbol reporting, groups duplicate ranges, and exercises rejection of duplicate
C function ownership. Its results are in `reports/certification_batch_validation.json`.

`reports/link_plan.json` records the union of certified ranges, every uncovered
payload gap, external code/data targets outside certified ranges, and names
that share an address and need reconciliation before a complete link. It
validates current source, map, object, data-manifest, parser, reference, and
runtime-library hashes and independently replays runtime initialized-data
evidence before generating that backlog. This inventory does
not reconstruct uncovered bytes. `reports/link_plan_validation.json` records
negative checks for altered verification evidence. Its executable validation
script also checks that the coverage union and uncovered gaps partition the
payload and agree with the progress report.

## Round-three recovery and placement pass

The next six-agent GPT-6 Luna pass adds nineteen recovered C functions and
2,492 code bytes. It covers startup/task initialization, display clearing,
percentage calculation, door and command helpers, resource setup, card helpers,
and CD command wrappers. New registered units are `heap_helpers3`,
`text_helpers3`, `game_helpers7`, `game_helpers8`, `audio_helpers3`, and the
data-only `game_constants`. Two functions are integrated into `music_control`
so they share the existing initialized flags definition without duplicating
its allocation. Thirteen new initialized definitions contribute 150 bytes.
The string/color definitions use meaningful C initializers and exact object
extents; their individual virtual addresses do not imply a single contiguous
original section.

The small relocated SDK scan adds `S_DCB`, `S_CB`, and `S_I`, totaling 104
complete linked text bytes. `analysis/certification_batch_validation.py`
independently checks their unique aligned masked placements, then replays all
relocations without masking any comparison bytes. Its nine new rejection
checks cover altered addresses, truncated extents, and relocated anchors.
All 38 supplemental SDK module texts pass independent replay. The unresolved
`_spu_RXX` contradiction is unchanged.

`analysis/startup_layout.py` records the observed zero-clear interval
`0x8008d088` through `0x800a1958` exclusive (84,176 bytes), the later heap-base
operand, and GP. This is a layout constraint with zero coverage credit;
individual `.bss`, `.sbss`, COMMON, and padding boundaries remain unresolved.
`game_helpers8` uses synthetic `g_page_7` only as an address anchor for observed
base-relative accesses to four records at `0x80072c40`. Like `g_page_9`, it
allocates no object and contributes no data bytes.

The verifier now snapshots source/map hashes for data-only units as well as
functions. Progress and link-plan generation reject stale source, map, or
object evidence for those units. The link-plan validator now has ten rejection
checks, including the three data-only freshness cases.

The final results of this pass are recorded in
`reports/session_20261007_round3_summary.json`, generated by
`analysis/session_20261007_round3_summary.py`. They preserve the earlier
`reports/session_20261007_summary.json` handoff and distinguish recovered C,
SDK text, initialized data, and uncounted placement constraints.

## Round-four recovery and menu data

The fourth pass used six GPT-6 Luna agents in two waves of three. It adds
thirteen complete C function matches totaling 1,188 bytes: menu text and
resource loading, resource counters, task initialization, text rendering,
and SPU pitch/note setters. New units are `heap_helpers4`, `text_helpers4`,
`game_helpers9`, `game_helpers10`, and `audio_helpers4`. Duplicate candidates
were excluded against every registered function map before integration.

`menu_data` defines six mutable 28-byte menu records; `menu_constants`
defines six labels and the eight-entry mode table. Together these thirteen
initialized definitions contribute 280 bytes and resolve twenty pointer
relocations. Existing functions in `accessors`, `tasks`, and `game_helpers2`
now reference the canonical records' value fields and still match. Placement
is specified per complete definition; this does not establish the original
section ordering. Text-resource globals use distinct names from the existing
mode table and voice/menu records at different addresses.

The link plan records the source address and owning initialized definition
for external pointer relocations. Its validator checks all twenty menu
reference sites against the byte-checked data, alongside the ten evidence
rejection checks and coverage arithmetic.

The expanded Runtime 3.3 diagnostic scans 680 unrepresented nonempty text
records from eighteen distinct archive files. It finds no new unique,
nonoverlapping placement, so SDK coverage is unchanged. Archive, parser,
reference, analyzer, and registered source/map hashes are retained in
`analysis/luna_sdk_round4/masked_scan.json`; these expanded archive hashes
are recorded evidence, not additional toolchain-lock pins. The `_spu_RXX`
conflict remains unresolved. No startup/BSS placement claim changed.

`f_1d0e0` matches every byte, but its lower input bound is not yet proven:
zero and values below -1 cause an undefined C shift, while the MIPS machine
shift masks the count. An explicit mask adds an instruction with this compiler.
The limitation is recorded in the source and handoff. Other unmatched drafts
remain under the round-four analysis folders, outside registered units.

Final evidence and exact remaining-byte/gap counts are in
`reports/session_20261007_round4_summary.json`, generated by
`analysis/session_20261007_round4_summary.py`. The fresh starting inventory is
`analysis/session_20261007_round4_ownership.json`; the final inventory is
`analysis/session_20261007_round4_final_ownership.json`. Earlier session
summaries are preserved. This pass adds 1,468 distinct certified payload bytes.
A complete executable link and full decompilation remain unfinished.

## Round-five recovery and integration review

Six GPT-6 Luna agents ran in two waves of three with separate source/map
ownership and nonoverlapping address ranges. The fresh starting manifest
imports every registered map, including `analysis/additional_symbols.json`,
and records 194 definitions. Imported symbol bindings are not definitions.
The final pass adds 25 complete C functions and 1,988 code bytes: fifteen
rendering wrappers, menu formatting and card selection, action callbacks,
timer/effect helpers, positional audio wrappers, and card event polling.
New units are `heap_helpers5`, `text_helpers5`, `game_helpers11`,
`game_helpers12`, `game_helpers13`, and `audio_helpers5`.

Two additional functions were integrated by the parent into existing units:
`f_16b20` shares the allocated `g_display_action` in `game_helpers2`, and
`f_3d814` shares `g_audio_flags` in `audio_control`. This preserves their
GP-relative operands. Original indirect formatter dispatch proves the third
context argument forwarded from `f_16b20` to `f_16994`; the positional audio
callee dereferences signed coordinate halfwords, supporting the reviewed
pointer prototype. Existing functions in the edited units still match.
`TestEvent` has an independently matched SDK export and a Runtime 3.3
unsigned-long event-handle prototype. The four polling slots are card events,
as shown by their original OpenEvent setup operands. The text wrappers and
existing actor-counter accessor now use compatible 16-byte record layouts.
`reports/round5_integration_review.json` records this review and its evidence.

`menu_choice_labels` and `menu_choices`, plus a seventh record in `menu_data`,
add thirteen initialized definitions and 172 bytes: nine semantic strings,
three choice arrays, and a video-mode record. All twelve new pointer
relocations resolve. An independent video-record consumer establishes that
three card choices and two video choices are separate adjacent tables.
The link-plan validator now checks all thirty-two menu reference sites.
These placements do not establish original section ordering.

The initial manifest found a remaining `g_mode` name collision. The heap/task
unit now uses canonical `g_item_mode` at `0x8008d378`; `g_mode` retains its
separate address `0x8006ca38`. The verifier rejects the same global name at
different addresses, with four disposable rejection checks. The GP access
analyzer records observed minimum access cells, source/map/object freshness,
and three rejection checks. It allocates no globals and earns zero payload
coverage; exact BSS/COMMON and section boundaries remain unresolved.

SDK discovery/binding evidence is unchanged, and the expanded round-four
scan was not repeated. The SPU contradiction, ambiguous CD data sections,
and `f_1d0e0` input-domain caveat remain open. Unmatched and duplicate
experiments are retained outside the registered definitions. Full-project
verification and independent SDK/data/link-plan checks pass. The pass adds
2,160 distinct certified payload bytes; a full executable link is unfinished.

The new handoff is `reports/session_20261007_round5_summary.json`, generated
by `analysis/session_20261007_round5_summary.py`. Starting and final ownership
snapshots have separate round-five filenames, and all previous handoffs are
preserved. Reproduce the additional review after the main verification and
progress/link-plan workflow with:

```powershell
python analysis/symbol_bindings_validation.py
python analysis/menu_choices_evidence.py
python analysis/gp_access_layout.py
python analysis/round5_integration_review.py
python analysis/session_20261007_round5_summary.py
```

## Relocation format references

The checker supports the observed HI16, LO16, MIPS26, and GP-relative
forms and rejects forms it cannot resolve. The
[PCSX-Redux PsyQ object parser](https://github.com/grumpycoders/pcsx-redux/blob/main/tools/psyq-obj-parser/psyq-obj-parser.cc)
documents expression opcodes and operand serialization. Installed PsyQ
tools supply additional independent checks; diagnostic fixtures and logs
are retained with build jobs. The existing `_work/psyqobj.py` is a pinned
dependency and is not modified by this project.

## Round-six recovery and menu layout correction

Six GPT-6 Luna agents ran in two waves of three, with disjoint code intervals
and separate source/map ownership. The fresh starting manifest imported every
registered map and confirmed 219 definitions. This pass adds 22 complete C
function matches and 1,332 code bytes: thirteen menu-state callbacks, three
rendering wrappers, a distance routine, four effect callbacks, and a voice
initializer. New registered code units are `text_helpers6`, `game_helpers14`,
`game_helpers16`, and `audio_helpers6`. Duplicate and unmatched experiments
remain outside registered certified definitions.

The menu callbacks need the existing GP allocations in the same translation
unit. `game_helpers2` now owns the existing `g_state_modes`, `g_8cdd8`,
`g_8cddc`, and `f_18174` alongside `g_display_action`. All migrated routines
and data still match; their migration earns zero bytes. Two individually
mapped initialized menu-state words at `0x8008cde0` and `0x8008cde4` add eight
bytes. No allocated global is duplicated.

Original dispatcher operands establish a flag-leading 28-byte menu layout:
packed flags/position at offset 0, value/limit at 4/6, label/choice pointers
at 8/12, and callbacks at 16/20/24. Seven earlier menu objects were mapped
four bytes into the actual records. Their maps now start four bytes earlier,
with correct leading words; accessors still reach the same value addresses
and match completely. The historical address-derived name
`g_menu_entry_6c550` remains, but its record now starts at `0x8006c54c` and
its value remains at `0x8006c550`. Per-definition placement still does not
establish complete original section ordering.

`menu_records6`, `menu_labels6`, and `menu_boolean_choices6` add 31 complete
records, 21 semantic labels, and the two-entry OFF/ON table, totaling 1,220
initialized bytes. Every pointer relocation resolves. Independent layout,
dispatch, table-consumer, and compiled-extent evidence is replayed by
`analysis/menu_round6_evidence.py` and `analysis/round6_integration_review.py`.
The link-plan validator includes every new menu pointer site.

Effect helpers consume integer frame tables rather than strings. Their
progress storage uses a C union for the observed halfword and word accesses,
and task removal uses the existing `Node` pointer declaration. The voice
initializer and earlier voice helpers use compatible tagged `VoiceEntry`
layouts. Prototype, type, and source changes are included in the full rebuild.

SDK discovery and bindings are unchanged. The conflicting `_spu_RXX`
bindings, ambiguous CD initialized sections, exact BSS/COMMON/section
boundaries, and `f_1d0e0` input-domain caveat remain unresolved. The retained
setup, allocator, and effect drafts earn no coverage. Final round-six totals,
remaining gaps, external references, and reproducible hashes are recorded in
`reports/session_20261007_round6_summary.json`, generated by
`analysis/session_20261007_round6_summary.py`. Round-five handoffs and starting
and final ownership snapshots remain preserved. A complete executable rebuild
and full decompilation remain unfinished.

Round six adds 2,560 distinct certified payload bytes. The final union is
149,783 / 514,048 bytes (29.137940%); 364,265 bytes remain across 172 uncovered
gaps, with 300 uncovered or unresolved external-reference records. All 56
registered source units pass the full rebuild. The two additional gaps and
nineteen additional external-reference records reflect new fragments and
exposed dependencies; they are reported alongside the increased byte union.

## Round-seven recovery and integration

Twelve requested GPT-6 Luna agents ran with separate source/map ownership and
nonoverlapping target ranges. This runtime provided four active slots including
the parent, so at most three workers ran concurrently. A fresh starting snapshot
imported all registered maps, including `analysis/additional_symbols.json`, and
recorded the round-six baseline of 241 definitions before dispatch.

This pass adds ten C functions and 1,076 code bytes: coordinate/transform helpers,
a 128-node pool initializer, a state-change helper, graphics buffer switching,
movie-stream setup, two GP-dependent notification/card callbacks, and a callable
empty stub. New code units are `game_round7_04`, `game_round7_05`,
`game_round7_08`, `game_round7_10`, `game_round7_11`, and `game_round7_12`.
`f_17bcc` and `f_17edc` integrate into existing `game_helpers4` and
`game_helpers2`, sharing their allocated globals. The notification task uses
the canonical `Node` payload, and the pool initializer uses the compatible
`N40` layout and callee declaration. No ownership migration or new GP allocation
is counted. Empty agent units remain unregistered.

The parent reconciled movie-stream fields, callbacks and ring-buffer pointers
against original callers and Runtime 3.3 `LIBCD.H`; transform wrappers now use
`LIBGTE.H` MATRIX/SVECTOR/VECTOR layouts and the ApplyMatrix prototype. All
matches survive these corrections. The unused decoder-registration return type,
the custom rotation helper return type, and complete imported allocation extents
remain unproven and are recorded in the round-seven integration review.

Fifty initialized definitions contribute 1,148 bytes: the Sound menu and its
four choices, a 36-character password keyboard and three captions, plus 29
complete card-menu records and ten prompts. Six original dispatcher base/count
pairs establish the latter record extents. Controller-button glyphs are preserved
in readable prompt strings. Independent replay is in
`analysis/menu_input_round7_evidence.py` and `analysis/menu_card_round7_evidence.py`.
Per-object placement still does not establish original section ordering.

SDK discovery/bindings are unchanged. All 38 supplemental SDK matches and
18 runtime-data rejection checks were replayed. The `_spu_RXX` contradiction,
ambiguous CD sections, retained unmatched drafts and `f_1d0e0` input-domain
caveat remain unresolved. Two agents caught header-offset mistakes during
exploration; their affected drafts are marked invalid and earn no coverage.

Round seven adds 2,224 distinct certified payload bytes. The final union is
152,007 / 514,048 bytes (29.570585%); 362,041 bytes remain across 175 gaps,
with 328 uncovered or unresolved external-reference records. The three extra
gaps and 28 extra reference records reflect fragments and newly exposed
relationships. All 67 registered units pass the full rebuild. This percentage
measures payload bytes, not game functions decompiled; full decompilation and a
whole-executable rebuild remain incomplete.

`reports/session_20261007_round7_summary.json` is generated by
`analysis/session_20261007_round7_summary.py` and records exact range subtraction,
starting/final ownership, semantic review, all twelve agent handoffs, unchanged
historical round-six evidence hashes, and final report freshness. Earlier
handoff reports and generators remain preserved.

## Current continue-until-stall recovery run

30 completed recovery rounds have added 91 complete C
functions (15,404 code bytes). The current registered total is
342 functions (35,952 code bytes),
verified across all 120 source units with C257 `-O2 -G8` and ASPSX 2.34.
Only complete, relocation-resolved matches with exact object extents receive credit.
The requested stop condition is met: three consecutive completed rounds added no new C function.

The retained first five rounds used three workers each. Rounds six onward use
twelve GPT-6 Luna workers dispatched together with separate file ownership and
nonoverlapping address intervals. Each completed round includes all worker
handoffs, parent integration, and fresh verification of registered inputs.
Unmatched experiments remain outside registered certified units.
The final range audit documents one historical assignment violation: round nine's
complete 424-byte f_1fea0 extends 72 bytes past its worker boundary. Its full match
is retained, all current certified C extents are disjoint, and no additional
function or coverage credit is awarded. The frozen assignment is preserved;
see reports/worker_range_audit.json.

Initialized C data was compared by symbol identity as well as address and size:
21 new definitions contribute 178 bytes;
0 existing initialized definitions changed address or extent in this run.
Other intervening GP cells still have unknown roles, and individual
matched definitions do not establish original allocation grouping or section order.
Existing declaration/name corrections and ownership moves receive zero new credit.
1 existing function and 1 initialized definition changed owning units;
their original addresses and extents are preserved and excluded from additions.
The compiler-generated switch table has a descriptive manifest identity; it is
not claimed as an original exported symbol. An unmapped duplicate transform
definition was removed without coverage credit, and compiled text exports are
now checked against certified ownership in addition to the maps.
Every compiled text byte is accounted for by complete certified function extents.
4 existing suffix certificates have corrected complete entry boundaries,
adding 48 prefix bytes to the exact union and zero new functions.
Two were first recorded during this run: the final new-function code total
includes 16 bytes of extent correction while retaining the original
historical round records. Caller and entry-reference evidence is retained
in reports/function_boundary_corrections.json.

The reviews include task and actor prefixes, SPU voice layouts, SDK DIRENTRY,
centered-text integer coordinate ABI, audio command count ABI, and the code
callback formerly misnamed `g_display_template` (now imported as `f_155ac`).
The old `g_action_context` alias is the established `g_callback_task` pointer;
matching task callbacks now use canonical `Node` fields and unsigned progress arithmetic.
Position helpers share compatible `ShortPosition3` tags and the SDK `SquareRoot0(long)` interface.
Unused return types and complete imported backing extents remain uncertain where
the original operands or SDK evidence cannot establish them.

SDK reconstruction remains separate: 93 matched object modules
contain 27,528 text bytes, and
26 matched initialized sections contain
100,849 bytes. These totals are unchanged by this run.
The exact certified range union is 167,621 / 514,048 payload bytes
(32.608044%), a net increase of 15,614 distinct bytes
from the run baseline. 346,427 bytes remain across 185 gaps, with 529
uncovered or unresolved external-reference records. New fragments may increase
gap and reference counts; this percentage measures payload bytes, not game
functions decompiled.

Full decompilation and a whole-executable rebuild remain incomplete. Remaining
work includes game routines, complete global/COMMON/BSS extents, section/link
layout, conflicting SPU bindings, ambiguous SDK CD sections, and retained input
domain/type caveats. The session ledger is `reports/luna_until_stall.json`;
`reports/link_plan.json` records current gaps and external references.
The final audit and exact union subtraction are reproducible with
`analysis/luna_until_stall_review.py` and `analysis/luna_until_stall_summary.py`
once the stop condition is met. Historical handoffs and object artifacts remain
preserved; project files remain untracked.
