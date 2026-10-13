# String and mode helper routines

These functions rebuild at GCC 2.5.7 `-O2 -G8` and match the complete executable spans with the A234 assembler. The first four also match A221.

- `f_1a14c` (`0x8001a14c`, 64 bytes) waits until `g_wait_flag` clears, calling `VSync(0)` while it remains set.
- `message_box_advance` (`0x80019db4`, 28 bytes) increments the first 16-bit value in the counter area. The source uses an incomplete array declaration (no guessed extent) to represent that address as non-small-data and produce the target's absolute HI16/LO16 reference.
- `f_1b340` (`0x8001b340`, 60 bytes) performs the mode setup, invokes `f_19dd0(mode, 7)`, and then resets the mode.
- `mode_in_table` (`0x8001b624`, 80 bytes) returns `-1` if the signed-short input matches one of the eight entries in `g_mode_table`, otherwise zero.
- `f_1b674` (`0x8001b674`, 80 bytes) maps the selected values from `0x17` through `0x21` to `-1` or zero through the target's switch table. A local signed-short result, returned as `int`, preserves the target sign-extension sequence.

The 44-byte dispatch table generated for `f_1b674` maps to `.rdata` at `0x80011950`. Its 11 R_MIPS_32 entries relocate to `f_1b674 + 0x38` or `f_1b674 + 0x40`; after resolving those relocations, all 44 bytes equal the executable's table. A234 produces the exact 80-byte function body. A221 emits an 84-byte extent and its 80-byte body differs from the executable, so A221 is not treated as a match for this function.

Addresses, extents, and external bindings of the matched ones are in the unit maps (`config/frontend.json`, `config/countdown_timer.json`).
