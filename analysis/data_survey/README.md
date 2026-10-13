# Uncovered payload survey (outside matched game C)

Classifies all 206 gaps (108,910 bytes) of `reports/link_plan.json` → `uncovered_payload_gaps`
at commit 9f5fa031. For each gap it records the section, what the bytes are, who reads them in the retail
image, which unit owns them, and what it would take to close them. Per-gap detail is in `gaps.json`.
This is evidence only. Nothing here certifies bytes. Only the gate's `GATE OK` line banks anything.

## Method

Run these from the checkout root. Scratch output goes to the git-ignored `builds/data_survey/`.

| step | script | what it does |
|---|---|---|
| 1 | `libscan.py` | Every section of every PsyQ object in `../sdk` (786 unique `.LIB`/`.OBJ` files, 6,445 unique sections), relocation-masked and anchored inside every gap. Reports exact masked matches and the best word-aligned partial match. |
| 2 | `libfuncs.py` | Splits the library-range gaps into functions (inventory starts, `jal` targets, symbol-map names). Each function is compared with 9,757 SDK functions, first on relocation-blind instruction tokens, then word for word. |
| 3 | `libmodules.py` | Aligns whole modules of the runtime libraries the game links, from every SDK version, against each library gap with an indel-tolerant (difflib) comparison. |
| 4 | `survey.py` | Decodes all retail `.text` and the leading module. Finds every address the code names (`lui`+`addiu`/load/store pairs, GCC switch-table `lui`/`addu`/`lw`, `$gp`-relative accesses, `jal`), plus every aligned word in `.rdata`/`.data`/`.sdata` that holds a payload address. Splits each gap at the addresses named inside it. Each reader is attributed to a matched C function, an asm unit, a certified or uncertified library module, or an unmatched game function. |
| 5 | `classify.py` | Assigns each piece a kind, owner and status, and writes `gaps.json`. |
| – | `moddiff.py` | One module against retail, word by word, with disassembly. Used for the library findings below. |

**Owner rule.** The matched units occupy contiguous `.text` ranges in a fixed order (`gaps.json` →
`link_order`, main … memcard, then the asm units). That order is the link order, and `.rdata`, `.data` and
`.sdata` follow it. For each data section, a dynamic programme picks a non-decreasing link-order owner for
every gap piece:

- Placed objects (gate provenance) are fixed to their unit.
- A piece prefers a unit that reads it: weight 1 per reader unit.
- A table and the string literals it points at must come from one unit: weight 2, applied in both directions.
- A piece with no reader is owned only when the evidence-backed neighbours on both sides agree.
- Unmatched game functions are placed in their own slot ("object between A and B") or in the unit whose
  range contains them.

`owner_confidence` is `high` when the owner is one of the piece's own readers, or is bounded on both sides
with no reader. It is `medium` when its readers have to be extern users. Medium covers only 188 bytes:
small main globals read by sprites, text, pause_menu, object_render and frontend, `g_zero_vector`, and two
model_draw tables read by particle. The coupling between tables and strings decides the main/frontend
boundary in `.data`. The file-name strings at 0x80010204..0x80010284 lead main's `.rdata`, and they are
pointed at by 0x8006bd2c..0x8006bdbc, so main's `.data` runs to 0x8006be04 and frontend's starts at
`s_no_message`.

**Status rule (game data).**
- `definable_now`: the owner is a matched C unit and the type evidence is a matched reader (or the matched
  table that points at it). Unmatched functions that also read it do not block the definition.
- `blocked_by_unmatched`: the owner is an unmatched object, or only unmatched code reads it.
- `owner_unresolved`: more than one unit fits the link-order bounds.

## Findings: bytes by category

| category | bytes | status |
|---|---:|---|
| Library text (Sony runtime, variant objects) | 45,128 | `library_variant` |
| Game initialised data: owner known, readers matched | 29,088 | `definable_now` |
| Game `.text`, 18 unmatched ranges (known walls; not in scope) | 16,068 | `unmatched_game_function` |
| Game data: owner between two units | 10,350 | `owner_unresolved` |
| Library `.rdata`/`.data`/`.sdata` of uncertified modules | 5,289 | `blocked_by_library_text` |
| `.sdata` alignment byte plus the first 1,912 bytes of `.sbss` carried as file zeros | 1,913 | `layout_tooling` |
| Leading hand-written module 0x80010000..0x800101e0 | 480 | `not_reconstructible_as_c` |
| Alignment padding (312 inside one unit's section, 30 between objects) | 342 | `tooling_padding` |
| Game data read only by unmatched functions | 252 | `blocked_by_unmatched` |
| **total** | **108,910** | |

Outside game `.text` the uncovered bytes total 92,842. About half is library code (45,128). About
39,690 is game data, and three quarters of that has a known owner and matched readers.

## Ranked plan (bytes closable per effort)

### 1. Define game data now: 29,088 bytes, C initialisers in the owner unit

All readers are matched. Each item is a `config/data.json` entry plus a C definition in the owner unit, in
address order. The string literals in table initialisers become the unit's `.rdata` `$LC` data. Coordinate
with the `lane/static-initialised` lane (alien_wt1), which is changing data definitions.

| owner | bytes | what |
|---|---:|---|
| main | 10,062 | `.data` 6,284: `g_clut_sets`, `g_page_sets`, `s_13a1c`, `g_file_table` and its 3,804-byte file table, pickup tables 0x8006b4e0/0x8006b4e8 (1,944), `init_cd` and `level_load` tables up to 0x8006be04 (all of 0x8006a5f8..0x8006be04 is the 171-entry `HeapFile g_file_table`; the pickup, `init_cd`, mission-text and `level_load` reads are its entries 106.., 160.., 165.. and 167..). `.rdata` 3,756: the `\GFX\…`, `\STR\…`, `\SFX…` file-name strings those tables point at (0x80010204..0x800110b0, reverse definition order). `.sdata` 22: `_stacksize` = 0x2000 at 0x8008cd78 (imported by 2MBYTE.OBJ), the pad-mask halfwords, `g_zero_vector`. |
| geometry | 9,672 | `g_atan_table` 1,480 and `g_sine_table` 8,192. Also read by unmatched f_3982c, which does not block. |
| frontend | 1,792 | `.data` 1,084 (`s_no_message` …, message-box tables, `g_texture_records`, `g_palette_records`, `g_level_names`, `g_name_table`). `.rdata` 708 (level-name strings 0x800110b0..0x80011374). |
| actor | 1,560 | alien animation sequence tables 0x800725c8..0x80072be0 |
| sprites | 1,488 | `g_6d1d0` records and code-pointer tables 0x8006cd68.. |
| map_objects | 992 | `g_map_object_scales`, `g_72fcc` with its item-name strings (`.rdata` 0x800121c0, 352), `g_object_heights`, `.sdata` 24 |
| movie | 720 | `g_movie_records` |
| particle | 656 | 0x80072338..0x800725c8 |
| text | 532 | 0x8006cadc.. |
| hud | 460 | `g_message_lines`, glyph table, `g_extra_health_heights` |
| model_draw | 420 | three dispatch tables, plus two tables read by particle |
| memcard | 380 | card error and event message tables and their strings (0x80012a58), `g_card_devices`, `.sdata` 24 |
| player, object_render, game_20000_3, doors | 354 | weapon sequences, `s_demo`, blast offsets, `s_battery_required`, `g_level_map_files`, `g_door_rotations` |

### 2. Decide one owner, then define: 10,350 bytes

- **10,332 bytes, view-cell ray tables** at 0x8006fa74 (9,308) and 0x80071ed0 (1,024). The only reader is
  asm `view_cells` (f_5533c), through the `.sdata` pointers `g_8cfa0`/`g_8cf9c`; `g_8cfa4` points at
  0x80096060. Link order puts the tables after geometry's `g_sine_table` and before game_20000_3's
  `g_level_map_files`, and the 12 `.sdata` bytes between model_draw and texture_upload. Owner is geometry
  or game_20000_3; the layout cannot tell them apart. The tables are `short` arrays, so each is word
  aligned (`DATA_ALIGNMENT`) and the ray table's last 2 bytes (9,306 of records, 1,551 `{angle, dx, dz}`)
  are padding; its start 0x8006fa74 is where `g_sine_table` ends, and the bucket table ends where
  `g_level_map_files` starts, which fits the end of geometry's `.data` and the start of game_20000_3's
  alike. Nothing in the image names the tables except the two pointers. The function evidence points to
  **game_20000_3**, not geometry: its `f_20980` (0x80020980) is the only C caller of `f_5533c`, and it
  computes the walker's other inputs (`g_player_cell_x`/`_z`, `g_visible_cell_mask`) from `g_screen`, the
  map header that the third pointer `g_8cfa4` holds (0x80096060 is `g_screen`); geometry is pure vector math
  and names none of `g_screen`, the cell globals or `f_5533c`. This is a probable owner, not a proven one,
  and game_20000_3 belongs to the other data lane, so the tables stay with the fallback for now. When
  defined, the pointers and the two tables belong in one unit (the tables could then be `static`: no other
  reference exists).
- 18 small `.sdata` bytes, music_control|pause_menu and texture_upload|doors (padding-sized).

### 3. Tooling, no C: 2,303 bytes

- **LIBCD `C_010` (`StSetMask`)**: 32 bytes of `.text` at 0x80061310 plus 16 bytes of `.data` at
  0x800748a0. Both are identical to Runtime Library 3.3, and the text relocations go to its own BSS commons.
  The binding `StSetMask = 0x80061310` is already resolved through certified C_005. The module was simply
  missing from the runtime candidate list (`analysis/runtime_symbols.json`). Add it and these 48 bytes
  should certify.
- **Padding** (342): 312 bytes sit between two placed objects of the same unit (frontend 181,
  pause_menu 37, actor 25, …), mostly 1–4 zero bytes after `$LC` strings and jump tables. Another 30 sit
  between objects. If the gate attributed intra-object alignment it already compiles, this would close; it
  needs a provenance-rule change, not C.
- **`.sbss` file fill** (1,913): this is section layout. It is counted once the BSS layout is modelled
  (`config/bss_layout.json`), not by C.

### 4. Blocked by an unmatched game function: 252 bytes

- memcard 196: f_4879c's tables at 0x800739e8 (128 + 32) and its strings at 0x80012b78 (36).
- 56 bytes owned by the unmatched object between lights and particle_hits (f_23890): its switch table at
  0x80011a3c (24) and data at 0x80072318 (32).

### 5. Library variant: 45,128 text + 5,289 data bytes (see below)

These cannot be certified from any archive in `../sdk`. The leading module (480 bytes) is hand-written
assembly outside the AGENTS.md asm region. Keeping it as fallback needs no change; reconstructing it needs
a policy decision.

## Library findings

The game links a Runtime Library set that is **3.3 with later, unarchived rebuilds of several modules**.
No archive in `../sdk` reproduces it: 786 unique `.LIB`/`.OBJ` files were checked, covering 2.0–4.7, the floppy
"isa board" set, the OLD_LIBS 3.4/3.5/3.7/4.0x/4.2E zips and the samples. The module-level evidence
(`gaps.json` → `modules` per library gap):

| retail range | module | closest archive | match | why it is not certified |
|---|---|---|---|---|
| 0x800590a8 (148), 0x8005bf9c (916) | LIBSPU `S_GKS`, `S_SCA` | 3.3 | 100% of masked words | The text is identical, but the relocations prove a different object. `_spu_RXX` (0x1f801c00) exists three times in retail `.data`: 0x80073af8 (read by the S_SVA variant, 13 sites), 0x80073afc (S_GKS) and 0x80073cf0 (S_SCA, next to S_INI's `.data`). In 3.3 both modules import the single extern from `SPU` `.data`+8, so no 3.3 object links to this. This is the `_spu_RXX` conflict in the campaign notes. Each retail module carries its own copy. |
| 0x80061310 (32) | LIBCD `C_010` | 3.3 | exact | Certifiable now; see plan item 3. |
| 0x80066470 (2,592) | LIBGTE `MTX_00` | 3.3 | 3 words differ | `mfc2 $t0..$t2` read GTE regs 9–11 (IR1–3) where 3.3 reads 25–27 (MAC1–3). This is older code, not a 3.6 fix: `CompMatrix` with the IR reads is word-exact in the Runtime 3.0 sample `psx\SAMPLE\GRAPHICS\FASTER\TAKE\TUTO0.CPE` (psx.zip), and 3.6's split `MTX_000`…`MTX_00B` modules have it too. The 3.0 `MTX` module holds the IR code in a different, larger layout, and no archive has the unsplit module with it. |
| 0x8005913c (588), 0x800684a0 (92) | LIBSPU `S_N2P`, LIBGTE `FOG_00` | 3.3 | 2 / 1 words | Divide-overflow trap encoded `tge $0,$0,0x5d` (0x00001770) instead of `break 6`: the expansion of ASPSX 2.08's `div` macro (Runtime 3.0 tools; 2.21/2.34/2.56 write `break 6`). Assembled with 2.08 (`toolchain/A208`, `docs/notes/library_asm.md`). |
| 0x8005b578 (1,232) | LIBSPU `S_SRA` | 4.0 | 1 word | Final delay slot `move $v0,$zero` where 4.0 has `nop`; retail's form is token-identical in 10 Runtime 3.0 sample CPEs (for example `psx\SAMPLE\SOUND\BALLS\MAIN.CPE`), so it is the older code. |
| 0x80058980 (1,832) | LIBSPU `S_SVA` | 3.3 | 97% tokens, restructured | Different revision of `_SpuSetVoiceAttr`, with modulo `divu` and `_spu_RXX` loads. |
| 0x80059388 (1,300) | LIBSPU (`SpuSetReverbModeParam`-like) | 4.0 `S_SRMP` | 62% | Unarchived revision |
| 0x80059984 (~6 KB) | LIBSPU `SPU`, `SPU_IO` (+ others) | 3.3 | 84% / 75% | Unarchived revision. `_spu_init` and `_spu_ioctl` differ most. |
| 0x8005c330..0x8005cd70 | LIBSPU `S_CRWA`, `S_INI`, `S_SR`, `S_STM`, `S_SK`, `S_M_M`, `S_SRV` | 3.3 / floppy | 61–85% | Unarchived revision. Several function bodies match the floppy (pre-3.0) set better than 3.3. |
| 0x8005d310 (9,788) | LIBCD `SYS` (2,116), `BIOS` (8,056) | 3.3 | 87% / 76% | Mixed. `CdMix`, `CdGetSector`, `CdDataCallback`, `CdRead`, `CD_sync`, `CD_ready`, `CD_vol`, `CD_initvol` and `CD_datasync` are word-exact to 3.3; `CdControl*`, `CD_cw`, `CD_init` and `CD_readm` differ. |
| 0x800607b0 (2,848), 0x800613c0, 0x8005fa64 | LIBCD `C_011` (`StCdInterrupt`), `C_009`, `C_004`, `CDREAD2` | 3.3 / 3.4 | 63–92% | Unarchived revision |
| 0x80061e34 (628) | LIBETC `INTR_DMA` | 3.3 | 81% | Shorter variant (628 vs 680 bytes) |
| 0x800623b8 (11,312) | LIBGPU `SYS` | 3.4 (OLD_LIBS in the 3.5 CD) | 94% (3.3: 91%) | Size 11,312, against 11,316 for 3.3 and 11,300 for 3.4. `ResetGraph`, `SetDispMask`, `ClearImage`, `LoadImage`, `StoreImage`, `DrawPrim`, `PutDrawEnv`, `PutDispEnv` (1,060 bytes), `SetTexWindow`, `SetDrawArea` and `SetDrawMode` are word-exact to 3.3; the queue and `_otc`/`_exeque` internals differ. Its 3.3 `.data` (360) and 3.0 `.sdata` (72) match exactly. |
| 0x80066250 (544) | LIBGPU `EXT` | 3.3 | 70% | `LoadTPage` and `LoadClut` are exact; the module differs |
| 0x80068030 (160) | LIBGTE `PATCHGTE` | 3.3 | 78% | Unarchived revision |

Provenance of the variant set: the 3.6 kanji samples `psyq\psx\sample\kanji\KANJIFNT\MAIN.EXE` and `SJISCODE\CODEVIEW.EXE` link retail's exact LIBGPU `SYS` (`$Id: sys.c,v 1.104 1995/09/01`, all 2,828 words, only relocation fields differ) and `intr.c,v 1.71 1995/08/29` (LIBETC `INTR_DMA`), and LIBGPU `EXT` (`SetDefDrawEnv`) token-exactly. They are linked executables, so they are lineage evidence for the library-assembly module headers, not a certification source.

Library data that matches an archive exactly but belongs to a variant text module: LIBSPU `S_INI` `.data`
(1,136, 3.3) at 0x80073cf4, LIBCD `BIOS` `.data` (816, 3.3) at 0x800744f8, LIBCD `C_011` `.data` (80, 3.3),
LIBGPU `SYS` `.data` (360, 3.3) at 0x800759fc, and LIBGTE `MTX` `.data` (784, floppy set) at 0x800766f4.
The rest of the 5,289 library data bytes are the variant SPU/CD/ETC modules' own `.data`/`.rdata`: switch
tables, `_spu_RXX` copies, and debug strings such as the `ResetGraph:` messages at 0x800131b4.

These bytes can only be certified by recovering the actual variant archive (a 3.3-era patch set that is
not in `../sdk`), or by a policy that admits relocation-masked identical text from a different object
(S_GKS/S_SCA) or data-only sections of uncertified modules. Rebuilding these modules from source is out of
scope.

## Limitations

- Readers come from static pairing of `lui` with the next `addiu`/load/store, `$gp` displacements and data
  words. Indexed accesses that start before a piece, and pointers built at run time, are not seen. A piece
  with no reader takes its owner from link-order bounds and is marked as such.
- `game_text_unmatched` slots use the matched units' `.text` ranges. An unmatched function inside a unit's
  range is attributed to that unit.
- The library function split cuts at every `jal` target, so local helpers become their own pieces.
  Module-level coverage (`modules`) is the reliable figure.
