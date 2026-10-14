# GCC 2.5.7 codegen recipes (C257, `-O2 -G8`)

Proved on `toolchain/C257/CC1PSX.EXE` (GCC 2.5.7 [AL 1.1, MM 40]) with ASPSX 2.34. Every recipe below
was checked by building scratch units and reading the compiler's own RTL dumps. No recipe needs a
non-default flag: the 3.3 `PSYQ.INI` sets only `stdlib`, cc1 reports its defaults as
`-mgas -msoft-float`, and none of `-O`, `-O3`, `-g`, `-fno-schedule-insns`, `-fno-schedule-insns2`,
`-fno-strength-reduce`, `-fno-rerun-cse-after-loop`, `-fno-cse-follow-jumps`, `-fno-thread-jumps`,
`-fno-defer-pop` or `-fno-caller-saves` reproduces either pattern below.

## Reading the compiler's RTL dumps

`build.py` names the input `INPUT.I`, so cc1's `-d` dumps (`INPUT.I.rtl` and so on) are not valid DOS
names and cc1 fails. To read the dumps, rerun cc1 in a finished job directory on an 8.3 copy of the
input:

1. Copy `INPUT.I` to `A`.
2. Write `RUN2.BAT` containing `C:\C257\CC1PSX -O2 -G8 -dr -dj -dL -dc -dl -dg A -o A.S`.
3. In the job's `dosbox.conf`, change `call RUN.BAT` to `call RUN2.BAT`. Edit the file in place:
   DOSBox-X also loads `dosbox.conf` from its working directory, so a second conf file would be
   ignored.

The dumps appear as `A.RTL`, `A.JUM` (jump 1, or jump 2 with `-dJ`), `A.CSE`, `A.LOO`, `A.COM`,
`A.SCH`, `A.LRE` (local-alloc, with per-pseudo reference counts and classes) and `A.GRE` (global
allocation and register dispositions). Flags whose dumps share a three-letter extension overwrite each
other, so pass them in separate runs.

## Pattern A: a loop entered at its bottom test

**Retail signature.** The loop is entered by a `j` to the bottom test, and the top has no copy of that
test. The test checks the counter's *old* value, with the decrement in the delay slot:

```
        j     L_test            (often threaded into each arm of the counter's initialisation)
body:   ...
L_test: move  v0,sN
        [sll  v0,v0,16]         (signed short counter only)
        bnez  v0,body
        addiu sN,sN,-1
```

A variant is a test that loads memory (LoadLevelMapFile). There the top keeps its own loads and jumps into the
middle of the bottom test.

**Mechanism.**

1. `expand_end_loop` rotates the exit test of a `while`/`for` loop to the bottom and emits
   `LOOP_BEG; jump L_test` (`A.RTL`).
2. Jump pass 1 (`duplicate_loop_exit_test`) always copies a small exit test, one with no call or label,
   in front of `LOOP_BEG`. `A.JUM` shows the copy and a `NOTE_INSN_LOOP_VTOP` note. No spelling of the
   loop avoids this copy.
3. After register allocation, jump 2's cross-jumping merges the top copy into the bottom copy when the
   two are still instruction-for-instruction identical. `A.GRE` still shows both copies; the jump 2
   dump shows the top copy replaced by `jump L_test`. The retail shape is therefore a test that was
   copied and then merged back.
4. The copies stay identical only if the test is an **old-value** test (`tmp = n; n = n - 1; tmp != 0`).
   `fold()` rewrites `n-- != 0` (and the truth test `while (n--)`) into `--n != -1` whenever, after
   `STRIP_SIGN_NOPS`, the operand is the `POSTDECREMENT_EXPR` itself and the comparison is EQ or NE.
   The `-1` then becomes a loop invariant (`li s1,-1; bne s0,s1`), the copies differ and nothing
   merges. A sign-changing conversion (`u_short` to `int`, any type to `unsigned`) or an ordered
   comparison keeps the old-value test.

**Recipes**, chosen by the width retail tests:

| retail test | counter | condition |
|---|---|---|
| `move v0,sN; bnez v0` (32-bit) | `u_int count;` | `while (count-- > 0)` (an equivalent spelling is `int count; ... count-- != 0U`) |
| `move v0,sN; sll v0,v0,16; bnez v0` | `short count;` | `while (count-- > 0U)` (or `!= 0U`) |
| `andi v0,v0,0xffff; bnez v0` | `u_short count;` | `while (count--)` |

Two further conditions decide whether the top copy merges:

- **The copies must begin the same way.** In Sprite_LoadResource, `count` must come from the `s16` prototype of
  `f_1c538`. The prototype adds an `sll/sra` into `count`, so the copied test starts with `move v0,s1`
  like the bottom one. Unprototyped, the copy tests `v0` directly and stays at the top.
- **CSE can make the top copy differ.** In LoadLevelMapFile, `p = file->buffer + 12;` lets CSE reuse the buffer
  load in the copied test, and only the tail merges. The two-statement form
  `p = file->buffer; p += 12;` overwrites `p`, which drops CSE's memory equivalence. The copy then
  reloads `buffer`, as retail does.

**Anti-patterns.** These were tried, and every one produces a test at the top and at the bottom, or a
wrong width:

- `while (count--)` and `count-- != 0` with an `int`, `u_int`, `long`, pointer or `short` counter;
- `for (; count--; )`;
- a `goto` loop (`goto test; top: ...; test: if (count--) goto top;`): no copy is made, but the test is
  still folded to `--count != -1`;
- `if (count) do ... while (--count)`;
- `do { if (!count--) break; ... } while (1)`;
- `u_short` where retail has no `andi`;
- signed `count-- > 0` (gives `bgtz`).

**Related scheduling rule (LoadLevelMapFile).** `A.SCH` shows sched 1 hoisting `p += 4` above the
`Text_ReadBigEndian32` call. This is consistent with `sched.c` tying only pseudos that cross no call to the last
call, and `p` crosses calls. To keep
`p += 4` after a call, as retail does, place it after the code that follows the call:
`length = f(p); if (length & 1) length++; p += 4;`. Reorg then fills the `beqz` delay slot from the
join point.

**Proven on** (`verify.py` run with the research units appended to `UNITS`; sources and maps in
`archive/drafts/{src,config}/research/`):

| function | result |
|---|---|
| Particle_BloodSpray | match (348 bytes) |
| Particle_ImpactSpray (sibling of Particle_BloodSpray, reconstructed) | match (364 bytes) |
| f_379ac | match (492 bytes) |
| LoadLevelMapFile | match (560 bytes) |
| Sprite_LoadResource | chunk loop matches; the CLUT loops still swap the pointer and counter registers (register/schedule residue), and the build is 480 bytes against 484 |

## Pattern B: frame space the body never uses

**Retail signature.** Every instruction matches except the frame size. The `vars=` field in the
`.frame` comment differs, and no instruction in either build addresses the extra space.

**Mechanism.**

1. Flow computes `reg_n_refs` for every pseudo before combine runs.
2. When combine removes every use of a pseudo without decrementing that count, the pseudo keeps a
   positive count. `A.LRE` lists it as "used N times ... ST_REGS or none". This happens when combine
   splits a three-insn combination back into two insns, or when it rewrites a compare so that a
   constant copy is no longer read.
3. `global_alloc` gives the pseudo no hard register.
4. `reload` (`alter_reg`) gives every unallocated pseudo with a positive count a stack slot of its mode
   size. Frame `vars` is rounded up to 8, so 1 or 2 dead SImode pseudos give 8 bytes and 3 or 4 give 16.

The frame size is therefore an output of RTL shape, not of the declared locals. Observed producers:

- `short x = <lh>; ... (int)x ...` with `x` also used in HImode arithmetic. Combine folds
  `lh; sll; sra` into an `lh` plus a copy and strands the `sll` temporary (Menu_FormatCardSlot pseudo 84, f_16634
  pseudo 81).
- `while (n--)` on a parameter or copy: the folded `n != -1` test is rewritten to `a0 == 0` and the
  `-1` constant pseudo is stranded (8 bytes in `void f(int n) { while (n--) h(g()); }`).

**Diagnosis.** Dump with `-dl -dg`. In `A.LRE`, find the pseudos marked "or none" that have no use left
in `A.GRE`. The number of such pseudos, times 4 and rounded up to 8, accounts for the unexplained
`vars`. Comparing that count with retail's tells you how many dead pseudos the original spelling
created; compare the counts before choosing a lever.

**Not the cause** (tested):

- an inline helper's locals: an inlined local struct or array keeps visible stores, unlike an unused
  slot;
- per-function flags (see the list at the top of this note);
- the `strcpy` declaration (prototyped, K&R, implicit or `memcpy` with a literal length all give the
  same frame).

**Recipe status.** No spelling closes the two drafts yet:

- **Menu_FormatCardSlot** (retail 16 vars, ours 8): 384 combinations of `short`/`char`/`int` types, comparison
  forms, digit expressions and argument casts all stay at 8 bytes. An `int level` changes the code.
- **Menu_TitleSequenceTask** (retail 16 vars, ours 0): the four `status == 2` sites coincide with 16 bytes. Tried:
  `int`/`long`/`u_short` status, assignment inside the `if`, block-scoped status per case, an
  unprototyped or `int`-returning `Movie_Play`, and reversed operands. None leaves stranded pseudos with
  identical code.
- **f_16634** (game_10000_4_number_label) has the opposite sign: ours has one stranded pseudo (from the
  `value = entry->value` sign extension) that retail lacks. That pseudo sits second in global
  allocation order, which probably also causes that draft's a1/a2/a3 rotation.

An unused 8- or 16-byte local reproduces these frames, but it is padding unless a real purpose is
proven; never land one.

### Aggregate locals reserve frame space at declaration (Object_LevelObjectTask)

A second source of unused frame space is any aggregate local (`VECTOR`, `RECT`, an array). The
aggregate gets its stack slot when its declaration is expanded, and that slot stays in the frame even
when no instruction uses it.

Object_LevelObjectTask (0x8003276c) builds byte-identical, 0x40 frame included, with any of these spellings:

- an unused function-scope `VECTOR scale;`, the current rule exception;
- the f_32430 scaling block kept under a constant-false condition;
- a block-scoped `{ VECTOR scale; ... }` inside such a condition.

Sharing the render body with f_32430 through an inline helper that takes a `scaled` flag does not
work. The helper's `VECTOR` is reserved in the caller's frame, but the inline changes register
allocation: 3 saved registers against retail's 4, and a 0x38 frame. The dead-temporary mechanism above
does not apply here, because Object_LevelObjectTask has no stranded pseudo. So no spelling is better evidenced than
the existing exception; a constant-false block is "fake code" unless a real build switch is evidenced.

## Pattern C: a switch whose case tree survives arm merging (Particle_ActorFireBullet)

**Retail signature.** Cases 11 and 12 are tested as `beq ==12`, then `slti <13`, then `bne !=11`,
and all three branch to one arm.

**Mechanism.**

1. For a small switch, `expand_end_case` emits a balanced case tree.
2. With two case nodes (11, 12) the tree degenerates to a chain: `==11 -> A; ==12 -> B; else D`.
3. When the arms for 12 and the default are identical, jump 2 cross-jumps B into D. The `==12` test
   then branches to its own fall-through and is deleted, which leaves a single `bne 11`.
4. With three case nodes, the tree is rooted at the middle value: `==12 -> B; >12 -> right subtree;
   !=11 -> D`. The right subtree threads into D once arms merge, but the three compares are not
   redundant to jump 2, so they survive.

**Recipe.** Restore the missing third case label. Give it the default's arm (`case 13: default:`) and
keep case 12 as a separate, identical arm. Writing `case 12: case 13: default:` together collapses the
tree again. Neither an `enum`-typed switch index nor a cost table changes the shape.

**Proven on:** Particle_ActorFireBullet, `verify.py` match (412 bytes).

**Related, reassociation.** Two more fixes were needed for the Particle_ActorFireBullet match:

- **Aim pitch.** `fold` reassociates `a - C - b` into `a - (b + C)`. A narrowing cast on the inner
  difference, `(short)(position->vy - 0x280) - camera.vy`, stops the reassociation (the value is
  stored to a `short` anyway).
- **Release block.** An inline helper evaluates its arguments before its body, which loads
  `turn.vy` early. Writing the particle-release stores in place, in the order `rotation.vx`,
  `rotation.vy`, `rotation.vz`, keeps retail's late `lhu`.

## Pattern D: a store through a materialised address after a call (f_19dd0)

**Retail signature.** After a call, retail stores the result with `lui v1; addiu v1; sh v0,0(v1)`
where we produce `lui at; sh v0,lo(at)`.

**Mechanism** (seen in the RTL and register-allocation dumps). When the callee is named `strlen`, GCC
treats it as a builtin. For a non-constant argument the builtin falls back to a normal call, but the
assignment's destination address `&g.length` is expanded into a pseudo before the call. That pseudo
holds a constant, carries `REG_EQUIV`, and gets no hard register, so reload rematerialises it into a
reload register after the call. Under any other name, the call comes first and the constant address
folds into the store.

**Recipe.** Call the runtime's real name with its era header, `#include <STRINGS.H>` and `strlen(...)`,
instead of the address-named `f_6993c`. `config/hud.json` already binds `strlen` to
0x8006993c.

**Proven on:** f_19dd0, `verify.py` match (892 bytes).

## Pattern E: a constant that is never propagated (f_41ff8, closed)

**Retail signature.** Inside the loop, retail extends `x` (`sll s4,s6,16`, `sra s5,s4,16`) even
though `x` is only ever `0xa2`, and it uses 7 saved registers. After the loop, it uses `x`
unextended.

**Mechanism** (proved on micro tests):

- Combine can drop the sign extension of a hoisted `x` only when `x` has exactly one set and is not
  live at function entry (`reg_n_sets == 1` in `set_nonzero_bits_and_sign_copies`).
- Loop invariant motion hoists `t = x << 16` and `t >> 16`, and CSE2 folds them when `x` is known
  constant. An extension that stays in a register (`sll sN,sM,16`) means `x` had more than one set.
- In the f_41ff8 scratch, a second assignment to `x` (`x += 0x70` for the "Yes/No" column)
  reproduces retail's hoisted `sll/sra` pair and the 7 saved registers.
- A dead initial store (`x = 0; ... x = 0xa2;`) or a combined `for` initialiser does not count as a
  second set.

**Evidence of structure.** Retail recomputes `(short)y` and reuses `(short)x` the way the conversions
of an inline helper's `short` parameters would be shared, for a helper called once per option.

**Status: closed in `pause_menu` without a second assignment to `x`.** Retail's spelling passes
the colour as a ternary in each call (`Pause_DrawHUDText(label, x, y, value ? 0xc8 : 0x40)`) instead of
through a `color` variable, and draws the volume bars with an `if (i == 0 && ...) ... else if
(i == 1 && ...)` chain. With that shape `x` keeps one assignment (`short x = 0xa2`) and the build
still reproduces the hoisted `sll`/`sra` and the 7 saved registers, so the two-sets condition
above was a side effect of the `color` variable spelling, not a property of the original source.
Lesson: when an extension or register count is off, vary call-argument shape and where each
value is computed before inventing extra assignments.

## Pattern F: allocation order from loop-weighted references (f_28954, f_295ec, f_40edc)

**Retail signature.** Same instructions and schedule, but two values swap registers: two saved
registers (`s0`/`s1`) or two temporaries (`v1`/`a0`). The remaining difference is the order in
which the allocator ranks pseudos.

**Mechanism** (local-alloc and global-alloc dumps, `-dl -dg`). Both allocators rank a pseudo by
`floor_log2(refs) * refs / live_length`, and flow counts each reference with the current loop depth,
starting at 1. A reference inside a loop counts 2; inside a `do { } while (0)` nested in that loop,
3. GCC 2.5.7 emits loop notes for `do { } while (0)` as for any loop, so the block raises the weight
of every reference inside it even though it runs once.

**A/B.** f_28954: `health` has 7 weighted references and ranks below the record pointer
(13 words differ); with the `if (health != 0 && health < 16) health = 16;` clamp inside
`do { } while (0)`, `health` has 9 and the build is exact. The same wrapper closes f_295ec (around
its three scale stores; the sibling f_29558 stays exact with it) and, together with the colour set
moved into the loop, f_40edc (around the draw-character arm).

**Use.** A `do { } while (0)` is the expansion of a multi-statement macro, so a block that fixes
the order this way points at a macro in the original. It is evidence of where a macro boundary is,
not of its name or interface; a bare `do { } while (0)` in source is an artefact. Record the
exact state and spell it as a macro only when the wrapped statements make a coherent operation.

**More closures with the same lever:** Light_Init (Light_Init: `COPY_RGB` on the palette colours
only; the light-record copies stay open-coded, plus `light++` before `source++` in the for
increment), f_39440 (Object_PickupsTask: `MAP_OBJECT_REMOVE` around unlink and free puts the object
pointer in s1 above the sound id).

**Side effect on addressing (f_198f8).** Statements after a `do { } while (0)` can address a
global's fields through a register already holding a nearby address of the same symbol (CSE's
related-value use): in Menu_MessageBoxType the '$' case reads `x` and `position` at -0x1c/-0x12 from
the `&text` register only when the digit read (`c = *text++; c -= '0';`) is wrapped, and only in
that case. Look for this when retail uses `reg-offset` where the build uses `lui`/`%lo`.

## Pattern G: one variable for two results (f_23fa8)

When two values with disjoint lifetimes swap registers, check whether the original reused one
variable for both. Particle_HitFloorOrCeiling keeps `FloorHeight` and later
`CeilingHeight` in a single `height` local; with separate `floor`/`ceiling` locals the floor
copy is scheduled after the marker load and the clearance register swaps (decomp-permuter lead,
exact with plain C).

## Pattern H: identical arms that must stay apart

**Retail signature.** A switch or if/else keeps separate copies of arms or tails that our build
merges: one jump-table target or one shared tail where retail has two, or the opposite case
grouping in the table.

**Mechanism.** jump and cross-jump merge arms whose instruction sequences are identical, and they
compare from the arm's end backwards, so identical tails merge even when the heads differ. Anything
that keeps the sequences different, or reorders where the arms sit, keeps them apart.

**Levers** (each is a separate A/B; all keep plain, plausible source):

- **Default last.** Write the `default:` arm after the case arms. Its position decides which arm
  falls through and which sequences are compared, so the same arms written with `default` first
  can merge differently.
- **A store per arm.** When arms end in the same tail but differ in one value, put the store of
  that value inside each arm rather than after the switch (particle's line draw in Particle_Task:
  `setRGB0(line, ...)` in every `case` with the identical `end` computation, `default` last).
- **A value in its own variable.** Give an arm's operands their own locals instead of folding
  them into a shared expression (Menu_DrawTranslucentPanels: `int x0 = 256, x1 = 320, y0 = 0,
  y1 = 240;` in the two-panel arm, `int x0 = panel * 256, ...` in the other).
- **`+` instead of `|` on a narrow value.** `short_value | 8` is shortened by the front end and
  computed on the short register, so every arm uses the same register; `short_value + 8` is
  computed on the sign-extended copy, which CSE reuses where a comparison of the same value
  already made one (`move v1,a1` before `bne v1,...`). The arms then differ (`ori a1,v1,8`
  against `ori a1,a1,8`; combine still emits `ori` because the low bits are known clear) and
  stay apart. Actor_ColonistTask (src/actor.c): `Actor_PlayAnimation(actor, direction + 8)`, like its
  `direction + 0x1b`.

## Pattern I: duplicated constant arms make GCC hoist the constant

**Retail signature.** The same constant (often 1) sits in a register for the whole loop, and the
jump table sends several cases to arms that only store it.

**Mechanism.** When two separate arms store the same constant, loop optimisation sees a
loop-invariant constant used more than once and hoists it into a register; with one merged arm
there is a single use and the constant stays an immediate.

**Recipe.** Keep the arms separate even though they store the same value:
`case 2: ... case 10: face->selector = 1; break; default: face->selector = 1; break;`.
Merging them into `case 10: default:` is one instruction shorter and no longer matches.

**Proven on:** Door_FixupModelFaces (src/doors.c), A/B in `verify.py`; the unmatched
Object_FixupLevelObjectModel (src/object_render.c), same family.

## Pattern J: a macro, not an inline, keeps a constant folded into an index

**Retail signature.** An array element at a constant index is addressed directly
(`lui`/`lw %lo(table+4*n)`) where the build computes the index at run time.

**Mechanism.** A macro substitutes its argument textually, so `table[19 + (frame)]` with a constant
`frame` is one constant expression when the front end folds it. An inline function receives the
argument as a parameter, and at object_render's sites the inline spelling did not give the folded
constant index retail has.

**Recipe.** When a repeated block takes a constant at some sites and a variable at others, and
retail folds the constant sites, spell the block as a macro (object_render's
`LEVEL_OBJECT_SET_FRAME(object, 3)` and `LEVEL_OBJECT_SET_FRAME(object, 2 + frame)`).

## Pattern K: unsigned locals stop reassociation before a narrowing store

**Retail signature.** `(x << 9) + 0x100 - offset` stays `(x << 9) + 0x100`, then `- offset`, where
the build emits `offset - 0x100` and subtracts it.

**Mechanism.** When the result is stored to a signed `short` member, `fold` reassociates the
signed `int` expression into `a - (b - C)`. With `x` unsigned the arithmetic is unsigned and the
order of the source is kept.

**Recipe.** Declare the grid coordinates `u_int` (Lift_Create: `u_int x = record->x;` after
Lift's position became an `SVECTOR`). The same expression stored to a `u_short` member matched
with `int` locals, so a type change of the destination can expose this.

## Pattern L: a value read through a macro, not a local, leaves its extension temporary behind

**Retail signature.** Every instruction matches, but retail's frame `vars` is larger by 8 bytes per
value of some kind (here: per rotation angle). The source reads a `short` field into `int`
arithmetic in more than one place.

**Mechanism** (`-dr -dl -dg` dumps). A field read in a narrow context (`rotation->vy & 0xfff`,
shortened to a halfword AND) first loads a halfword pseudo; a later `int` use of the same field is
CSE'd onto it and sign-extended with `sll`/`sra` into a temporary. Combine folds load and
extension into one `lh` and the temporary keeps its reference count with no insns left, so reload
gives it an 8-byte slot (each stranded pseudo takes `BIGGEST_ALIGNMENT`). An `int` local
(`int ry = rotation->vy;`) or an inline function's `int` parameter loads with `lh` directly and
strands nothing. The halfword load only becomes `lh` (not `lhu`) when the first use in insn order
is the `int` one, so the statement order matters too.

**Recipe.** Read the field directly in each use, through the function-like macros the code shares
(`SINE(a)` / `COSINE(a)` in `src/geometry.c`), and put a use that needs the sign-extended value
first.

**Proven on:** Geo_RotationMatrix, f_20584 and f_2076c (src/geometry.c; +3 slots each, one per
angle). Geo_RotationMatrixInverse reads `-rotation->vx`: the negation is an `int` use from the
start, nothing is stranded, and its frame is 8 bytes per slot smaller, as in retail.

**Related, unsigned shifts.** A product stored as `x >> 12` into a `short` shows as `srl` in retail
when the shifted value was unsigned; signed arithmetic gives `sra` and no combine rewrites one into
the other. Where retail negates the shifted value it is `sra` (`-(x >> 12)` signed), so the
unsigned conversion belongs to the stored expressions only: `(u_long)(cy * cz) >> 12`.

## Pattern M: spelling levers that moved whole near-misses to exact

Each was found by sweeping one spelling at a time against `diff.py`; none adds code.

- **Read a global pointer at each use, not through a local copy.** Model_DrawNearFT3NoCull/Model_DrawNearFT3ABE/Model_DrawNearFT3 reread
  `g_spadSxy` in every emission block; a block-local `sxy = g_spadSxy;` copy moves the pointer
  and the OT mask into other registers (57 words to 9 in Model_DrawNearFT3NoCull).
- **An assignment inside the expression fixes load order.** `d = poly->ft3.u1 - (u0 = poly->ft3.u0);`
  loads u1 before u0 the way retail does; `u0 = ...; d = ... - u0;` loads them the other way and
  swaps the temporaries.
- **Block-scoped temporaries per repeated computation.** The two UV-midpoint computations each in
  their own `{ short d, ad; ... }` match where shared `du`/`dv`/`ad` locals tie registers across
  them.
- **Statement order inside if/else arms decides cross-jumping and scheduling.** Tex_UploadSpriteFrame/Tex_UploadBossSpriteFrame:
  only the uncompressed arm ends with its own `g_uploadQueueTail = upload; return -1;` (the
  compressed arm falls to the shared copy), and the arms store their fields in different orders
  (`g_uploadFree = upload->next;` before `rect.h` in one arm, after it in the other). With
  identical arms GCC cross-jumps the queue-head stores and the sizes differ.
- **Statement order of independent loads decides list scheduling.** Geo_RotationMatrixInverse
  matches only with `cy` computed before `cz`; Geo_RotationMatrix accepts any order with each angle's
  cosine before its sine (pattern L).

## Pattern N: a once-set temporary is glued to its consumer (Lift_MeasureModel)

- Retail: `lbu w; lbu d; lw; lhu; lhu; sll w; sll d; addu; addu; sh; addu; addu`, both shifts before
  the first add. Ours (`width * 0x100u` inline, or `u_int width = lift->width << 8;`): each
  shift directly before the add that consumes it, 10 words off.
- Mechanism: sched1 schedules a block backwards. When a scheduled insn makes a predecessor ready,
  `adjust_priority` raises that predecessor to the launch priority if it is a "birthing" insn:
  it sets a pseudo that is live here and has `reg_n_sets == 1`. Such an insn is placed next, so a
  once-set temporary lands directly before its consumer (the dump shows it as `(7f000001)` in the
  ready list). A pseudo set twice is not birthing; it is ranked by its plain priority and
  original order like everything else, so the shifts sink to where retail has them.
- Lever: a second assignment to the same variable: `width = lift->width; depth = lift->depth;
  width <<= 8; depth <<= 8;` (u_int locals; the statement order among the leading loads does
  not matter). `width *= 0x100` does not work: the multiply is not folded into a plain ashift.
- Proven: Lift_MeasureModel (src/lift.c), 408 B, verify.py.

## Pattern O: a constant object pointer makes member accesses absolute; direct spellings share a base register

Retail: in one function some members of a global object (`g_player`) are read through a base register
(`la t1, g_player+8` then `lhu (t1)`, `lhu -8(t1)`, `lhu 0xf4(t1)`) while the rest are absolute `lui`/`lhu`
pairs, and a null-checked pointer member is reloaded after the check (Player_PlayWeaponView).

Mechanism (RTL `-ds`): a direct `g_player.member` spelling goes through `memory_address`, which puts the
constant address in a pseudo; the first such pseudo in a CSE block becomes a related-value base and later
`g_player+c` constants are rewritten as `base + delta` (`use_related_value`), so single uses fold to
`delta(base)` and a multi-use pseudo survives as the `la`. A member reached through a `Player *` whose value
CSE knows to be constant (an inline function's parameter substituted with `&g_player`, or a local
`Player *player = &g_player;`) folds to `(mem (const g_player+c))` with no pseudo: absolute, never shared,
and not identified with the direct spelling's `(mem (reg))` (hence the reload). The local pointer itself
becomes the oldest base, so prefer the inline parameter when retail keeps another member's address as the
base. cse2 (`-frerun-cse-after-loop`) folds `delta(base)` back to the absolute address when the base's set
is in the same block, which is why Object_Spawn (a loop, set hoisted) keeps `2(s0)` for
`g_levelMap.offsetZ` and Player_Move's draft does not.

Proven: Player_PlayWeaponView (`entry = Player_WeaponEntry(&g_player)` for the indexed read, `g_player.weaponSequence`
direct for the null check); Player_DecreaseYaw, Player_IncreaseYaw, Player_Decelerate and
Player_BindWeaponSpriteSlot (`Player *player = &g_player;`, every member absolute and reloaded). Leads: Player_ViewBob (bob half absolute, pitch/position.vx/viewHeight direct).
