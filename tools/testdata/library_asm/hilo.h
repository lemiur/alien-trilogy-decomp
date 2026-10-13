/*
 * hilo.h: split HI16/LO16 halves for library assembly units (asm/lib).
 *
 * ASPSX emits the paired HI16/LO16 relocations (PsyQ patch types 82/84) only from its own
 * macro expansions (la, lw reg,sym ...), which keep the two halves back to back. Retail
 * Sony code also separates them (a lui before a jal and the addiu in its delay slot; two
 * luis then two addius). ASPSX has no %hi/%lo; these macros spell the halves as explicit
 * link-time expressions on a 16-bit immediate field:
 *
 *   lui   r, HI16(E)       patch type 26 (lui field), (E + 0x8000) >> 16
 *   addiu r, r, LO16(E)    patch type 30 (signed 16-bit field), E - (HI16(E) << 16)
 *   lw    r, LO16(E)(b)    the same LO16 half as a load/store offset
 *
 * PSYLINK evaluates them to exactly the bytes of a HI16/LO16 pair for E (the +0x8000 carries
 * the sign of the low half). tools/bytecheck.py, tools/gate.py and tools/generated_witness.py
 * accept these two expression shapes only, check each as that half of E (target symbol and
 * value), and require both halves of E inside the same routine. E is a symbol, a section
 * label, or either plus a constant; write the same E in both halves.
 */
#ifndef HILO_H
#define HILO_H
#define HI16(e) (((e)+0x8000)>>16)
#define LO16(e) ((e)-(HI16(e)<<16))
#endif
