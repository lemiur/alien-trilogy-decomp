# Alien Trilogy progress

Counted by `tools/progress.py` from the per-byte record of a green `tools/gate.py` run (`builds/gate/provenance.json`) and cross-checked against `verify.py`, `generated_witness.py`, the runtime reports and `analysis/link_plan.py`.

Payload: 514,048 bytes. Compiler: GCC 2.5.7, ASPSX 2.34, `-O2 -G8`; Sony Runtime Library 3.3.

| Category | Count | Bytes | Payload |
|---|---:|---:|---:|
| Game C functions | 645 functions | 205,436 | 39.96% |
| Game C initialized data | 287 definitions | 40,515 | 7.88% |
| Compiler-generated data of game C units | 406 items | 8,079 | 1.57% |
| Hand-written assembly (GTE/renderer; not C) | 32 routines + 312 tables (51,604 + 12,244 bytes) | 63,848 | 12.42% |
| Library assembly (Sony Runtime Library variant modules; not C, not certified) | 153 routines + 184 data (45,096 + 5,141 bytes) | 50,237 | 9.77% |
| Inline asm in C units ("verbatim"; not C) | 0 items | 0 | 0.00% |
| Sony Runtime Library 3.3 text (certified; linked from fallback) | 96 modules | 27,796 | 5.41% |
| Sony Runtime Library 3.3 initialized data (certified; linked from fallback) | 33 sections | 100,949 | 19.64% |
| Fallback: original bytes not yet accounted for |  | 17,188 | 3.34% |
| **Total** | | **514,048** | 100.00% |

**Decompiled game C: 254,030 bytes (49.42% of the payload).** It is the sum of the first three rows and equals the gate's `source_bytes`. Assembly, library assembly, inline asm and the Sony library are separate figures and never part of it. Library assembly is the gate's `library_asm_bytes`: Sony Runtime Library modules that match no archived version, written as annotated `.s` units. The certified library rows are original bytes that the runtime reports match against Sony's objects; the gate still takes them from the fallback.

Runtime candidates with conflicting symbol bindings, not certified: none.

Runtime candidates rebuilt as library assembly (archive text with different relocations; not certified): S_GKS (libspu_s_gks), S_SCA (libspu_s_sca).

Reproduce: `python tools/reports.py` (gate, verify.py, runtime reports, link plan, this file). `reports/link_plan.json` lists the uncovered gaps.
