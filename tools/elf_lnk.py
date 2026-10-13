"""Convert a GNU ``as`` MIPS ELF relocatable object into a PsyQ LNK object.

A library assembly unit is assembled by ASPSX (2.34, or 2.08 where retail is that assembler's
output) unless no pinned ASPSX can express it: LIBGTE PATCHGTE addresses its own ``.sbss``
block absolutely, where ASPSX addresses ``.sbss`` data $gp-relative. Such a unit is assembled by the pinned GNU assembler (``config/toolchain.lock.json``,
``gnu_as``; selected per unit by ``config/asm_build.json``), and this module turns its ELF
object into the LNK form that every other tool reads (psyqobj, lnk_symbols, bytecheck,
data_check, the gate's linker), so one object format and one set of checks serve every unit.
tools/build_asm.py runs it and keeps both objects in the job (INPUT.ELF, INPUT.OBJ).

The output is what ASPSX writes for the same source:

- the eight sections ASPSX always declares (GNU's ``.rodata``, where its ``.rdata`` directive
  puts data, is ``.rdata``), numbered as ASPSX numbers them
  (.rdata 1, .text 2, .data 3, .sdata 4, .sbss 5, .bss 6, .ctors 7, .dtors 8), group 0,
  alignment 8; each section's bytes in one code record per 0x8000 bytes, followed by the
  patches of that chunk; ``.sbss``/``.bss`` sizes as uninitialised-space records;
- an XDEF (record 12) per global symbol defined in one of those sections, an XREF (14) per
  undefined global, and a local-symbol record (18) per named local symbol of ``.sbss`` or
  ``.bss`` (an uninitialised static, as ASPSX writes for ``.lcomm``) and per local ``.text``
  label not spelled ``$name`` or ``.Lname`` (a static routine, located like a C ``static``
  function: ``"static": true`` in the unit map). Code labels (``$loop``) and initialised
  statics get no record, as with ASPSX;
- one patch (record 10) per relocation, with the relocated field cleared and the addend moved
  into the expression, as ASPSX leaves it: R_MIPS_32 is type 16 (word), R_MIPS_26 type 74,
  R_MIPS_HI16/R_MIPS_LO16 types 82/84. A target is ``[section] + offset`` for a symbol this
  object defines (and for section symbols), else the XREF. A HI16 takes the addend
  ``(hi << 16) + sext(lo)`` of the next LO16 against the same symbol; every HI16 must be
  followed by such a LO16, every LO16 must close at least one HI16 (no orphan halves), and the
  HI16 halves one LO16 closes must agree on the high part (no ambiguous pairing).

Refused (``ElfError``): anything but a little-endian ELF32 MIPS relocatable; RELA relocations;
any other relocation type (GP-relative ones included); a section holding bytes, symbols or
relocations other than the six data/code sections and the assembler's metadata sections
(.reginfo, .MIPS.abiflags, .pdr, .gnu.attributes, .comment, .note.GNU-stack, .mdebug);
COMMON, weak or absolute symbols; a symbol defined in a refused section.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SECTIONS = (".rdata", ".text", ".data", ".sdata", ".sbss", ".bss", ".ctors", ".dtors")
NUMBER = {name: index + 1 for index, name in enumerate(SECTIONS)}
UNINITIALISED = (".sbss", ".bss")
ALIASES = {".rodata": ".rdata"}  # GNU as's MIPS ".rdata" directive writes .rodata
METADATA = (".reginfo", ".MIPS.abiflags", ".pdr", ".gnu.attributes", ".comment",
            ".note.GNU-stack", ".mdebug")
R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16 = 2, 4, 5, 6
PATCH = {R_MIPS_32: 16, R_MIPS_26: 74, R_MIPS_HI16: 82, R_MIPS_LO16: 84}
FIELD = {R_MIPS_32: 0xffffffff, R_MIPS_26: 0x03ffffff, R_MIPS_HI16: 0xffff, R_MIPS_LO16: 0xffff}
CHUNK = 0x8000


class ElfError(Exception):
    pass


def _sext16(value):
    return value - 0x10000 if value & 0x8000 else value


class Elf:
    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
            raise ElfError("not a little-endian ELF32 object")
        (e_type, e_machine, _, _, _, e_shoff, _, _, _, _, e_shentsize, e_shnum,
         e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        if e_type != 1 or e_machine != 8:
            raise ElfError("not a MIPS relocatable object")
        self.data = data
        self.sections = []
        for i in range(e_shnum):
            (name, kind, flags, _, offset, size, link, info, _, entsize) = struct.unpack_from(
                "<IIIIIIIIII", data, e_shoff + i * e_shentsize)
            self.sections.append({"name": name, "type": kind, "flags": flags, "offset": offset,
                                  "size": size, "link": link, "info": info, "entsize": entsize})
        names = self.sections[e_shstrndx]
        for s in self.sections:
            s["name"] = self.string(names, s["name"])

    def string(self, table, offset):
        start = table["offset"] + offset
        return self.data[start:self.data.index(b"\0", start)].decode("latin1")

    def body(self, section):
        if section["type"] == 8:  # SHT_NOBITS
            return b""
        return self.data[section["offset"]:section["offset"] + section["size"]]

    def symbols(self):
        tables = [s for s in self.sections if s["type"] == 2]
        if len(tables) != 1:
            raise ElfError(f"expected one symbol table, found {len(tables)}")
        table = tables[0]
        strings = self.sections[table["link"]]
        out = []
        for i in range(table["size"] // 16):
            name, value, size, info, _, shndx = struct.unpack_from("<IIIBBH", self.data, table["offset"] + 16 * i)
            out.append({"name": self.string(strings, name), "value": value, "bind": info >> 4,
                        "type": info & 15, "shndx": shndx})
        return out


def convert(data):
    """LNK bytes for one ELF object (rules: module docstring)."""
    elf = Elf(data)
    kept = {}
    for index, s in enumerate(elf.sections):
        name = s["name"]
        if index == 0 or s["type"] in (2, 3):  # null, SYMTAB, STRTAB
            continue
        if s["type"] == 4:
            raise ElfError(f"RELA relocations ({name}) are not supported")
        if s["type"] == 9:  # SHT_REL
            continue
        name = ALIASES.get(name, name)
        if name in SECTIONS:
            kept[index] = name
        elif name not in METADATA:
            raise ElfError(f"section {name} is not one of {SECTIONS} or assembler metadata")
    if sorted(kept.values()) != sorted(set(kept.values())):
        raise ElfError("a section name appears twice")
    symbols = elf.symbols()
    xrefs, xdefs, locals_, refs = {}, [], [], {}
    next_number = len(SECTIONS) + 1
    for i, sym in enumerate(symbols):
        if i == 0 or sym["type"] in (3, 4):  # null, SECTION, FILE
            if sym["type"] == 3:
                refs[i] = ("section", sym["shndx"], 0)
            continue
        if sym["shndx"] in (0xfff1, 0xfff2):
            raise ElfError(f"absolute or COMMON symbol {sym['name']} is not supported")
        if sym["bind"] not in (0, 1):
            raise ElfError(f"symbol {sym['name']} has binding {sym['bind']} (only local and global)")
        if sym["shndx"] == 0:
            if sym["bind"] != 1:
                raise ElfError(f"undefined local symbol {sym['name']}")
            xrefs[i] = (next_number, sym["name"])
            refs[i] = ("xref", next_number, 0)
            next_number += 1
            continue
        if sym["shndx"] not in kept:
            raise ElfError(f"symbol {sym['name']} is defined in section {elf.sections[sym['shndx']]['name']}")
        refs[i] = ("section", sym["shndx"], sym["value"])
        section = kept[sym["shndx"]]
        if sym["bind"] == 1:
            xdefs.append((next_number, NUMBER[section], sym["value"], sym["name"]))
            next_number += 1
        elif section in UNINITIALISED or (section == ".text" and not sym["name"].startswith(("$", ".L"))):
            locals_.append((NUMBER[section], sym["value"], sym["name"]))

    def target(symbol_index, addend):
        if symbol_index not in refs:
            raise ElfError(f"relocation against unsupported symbol #{symbol_index}")
        kind, number, value = refs[symbol_index]
        if kind == "xref":
            base = (2, number)
        else:
            if number not in kept:
                raise ElfError("relocation against a symbol of a refused section")
            base, addend = (4, NUMBER[kept[number]]), addend + value
        addend &= 0xffffffff
        return base if addend == 0 else (44, ("c", addend), base)

    code, patches = {}, {}
    for index, name in kept.items():
        code[name] = bytearray(elf.body(elf.sections[index]))
        patches[name] = []
    for s in elf.sections:
        if s["type"] != 9:
            continue
        if s["info"] not in kept:
            raise ElfError(f"{s['name']} relocates a refused section")
        name = kept[s["info"]]
        if name in UNINITIALISED:
            raise ElfError(f"{s['name']}: relocations in {name}")
        body, pending, seen_hi = code[name], [], set()
        entries = [struct.unpack_from("<II", elf.data, s["offset"] + 8 * i) for i in range(s["size"] // 8)]
        for offset, info in entries:
            kind, symbol_index = info & 0xff, info >> 8
            if kind not in PATCH:
                raise ElfError(f"{name}+{offset:#x}: relocation type {kind} is not supported")
            if offset + 4 > len(body) or offset & 3:
                raise ElfError(f"{name}+{offset:#x}: relocation outside the section or unaligned")
            word = struct.unpack_from("<I", body, offset)[0]
            field = word & FIELD[kind]
            struct.pack_into("<I", body, offset, word & ~FIELD[kind] & 0xffffffff)
            if kind == R_MIPS_HI16:
                pending.append((offset, symbol_index, field))
                continue
            if kind == R_MIPS_LO16:
                mine = [p for p in pending if p[1] == symbol_index]
                if not mine:
                    raise ElfError(f"{name}+{offset:#x}: LO16 without a preceding HI16 against its symbol")
                if len({p[2] for p in mine}) != 1:
                    raise ElfError(f"{name}+{offset:#x}: LO16 closes HI16 halves {[hex(p[0]) for p in mine]} "
                                   "with different high parts (ambiguous pairing)")
                for hi_offset, _, hi_field in mine:
                    addend = (hi_field << 16) + _sext16(field)
                    patches[name].append((82, hi_offset, target(symbol_index, addend)))
                pending = [p for p in pending if p[1] != symbol_index]
                addend = (mine[-1][2] << 16) + _sext16(field)
                patches[name].append((84, offset, target(symbol_index, addend)))
                continue
            addend = field << 2 if kind == R_MIPS_26 else field
            patches[name].append((PATCH[kind], offset, target(symbol_index, addend)))
        if pending:
            raise ElfError(f"{name}: HI16 at {[hex(p[0]) for p in pending]} without a following LO16")
        patches[name].sort(key=lambda p: p[1])

    out = bytearray(b"LNK\x02")
    out += bytes([46, 7])  # processor type 7 (R3000), as ASPSX writes
    for name in SECTIONS:
        out += struct.pack("<BHHB", 16, NUMBER[name], 0, 8) + bytes([len(name)]) + name.encode()
    for name in SECTIONS:
        if name not in code:
            continue
        out += struct.pack("<BH", 6, NUMBER[name])
        if name in UNINITIALISED:
            size = elf.sections[next(i for i, n in kept.items() if n == name)]["size"]
            if size:
                out += struct.pack("<BI", 8, size)
            continue
        body = code[name]
        for start in range(0, len(body), CHUNK):
            chunk = body[start:start + CHUNK]
            out += struct.pack("<BH", 2, len(chunk)) + chunk
            for kind, offset, expr in patches[name]:
                if start <= offset < start + len(chunk):
                    out += struct.pack("<BBH", 10, kind, offset - start) + encode(expr)
    for number, name in sorted(xrefs.values()):
        out += struct.pack("<BH", 14, number) + bytes([len(name)]) + name.encode()
    for number, section, offset, name in xdefs:
        out += struct.pack("<BHHI", 12, number, section, offset) + bytes([len(name)]) + name.encode()
    for section, offset, name in locals_:
        out += struct.pack("<BHI", 18, section, offset) + bytes([len(name)]) + name.encode()
    out += b"\0"
    return bytes(out)


def encode(expr):
    """PsyQ expression bytes (psyqobj.expr's inverse; the right operand first)."""
    if expr[0] == "c":
        return struct.pack("<BI", 0, expr[1])
    if expr[0] in (2, 4):
        return struct.pack("<BH", expr[0], expr[1])
    return bytes([expr[0]]) + encode(expr[1]) + encode(expr[2])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", type=Path)
    ap.add_argument("lnk", type=Path)
    a = ap.parse_args()
    try:
        a.lnk.write_bytes(convert(a.elf.read_bytes()))
    except (ElfError, struct.error, ValueError, IndexError) as exc:
        print(f"elf_lnk.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
