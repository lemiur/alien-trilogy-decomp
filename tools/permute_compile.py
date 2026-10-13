"""decomp-permuter glue: compile one candidate with the project toolchain, or disassemble a result.

Compile (the interface of decomp-permuter's compile.sh, ``compile.sh input.c -o output.o``)::

    python tools/permute_compile.py --map MAP --function F INPUT.c -o OUTPUT.o

builds INPUT.c exactly as tools/build.py does (GCC 2.5.7 CC1PSX ``-O2 -G8`` with the 3.3
driver predefines, ASPSX 2.34, under DOSBox-X, the pinned SDK headers on E: and include/ on
F:), in a fresh job directory of its own so parallel permuter workers never share one. It
then takes F's code from the PsyQ object (from its XDEF to the next XDEF in the section, or
the section end), resolves every PsyQ patch against MAP with F placed at its mapped address
(the same arithmetic as tools/bytecheck.py), and writes a minimal ELF32 MIPS relocatable
whose .text is those bytes. Referents are therefore compared as resolved values: a wrong
global shows up as a differing immediate. The job directory is removed unless --keep.

    python tools/permute_compile.py --target --map MAP --function F -o target.o

writes the retail bytes of F (address and size from MAP) in the same ELF form.

    python tools/permute_compile.py --disassemble FILE.o

prints FILE.o's .text in the line format of ``objdump -drz`` that decomp-permuter's scorer
parses (its ``objdump_command`` setting); no MIPS binutils are needed.

    python tools/permute_compile.py --check --map MAP --function F INPUT.c

compiles and byte-compares the resolved function with retail (extent and every byte), and
exits 0 only on an exact match. This is a scratch check for permuter leads; only
tools/verify.py and the gate decide what is matched.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
from types import SimpleNamespace

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import build  # noqa: E402

ROOT = build.ROOT
REFERENCE = ROOT / "games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07"
DOSBOX = Path(r"C:\DOSBox-X\dosbox-x.exe")


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bytecheck = _load("permute_bytecheck", TOOLS / "bytecheck.py")


class CompileError(Exception):
    pass


def num(value) -> int:
    return int(value, 0) if isinstance(value, str) else int(value)


def compile_object(source: Path, build_root: Path, retries: int = 1) -> Path:
    """Compile source in a fresh job directory under build_root; return the job directory."""
    tools = build.DEFAULT_TOOLS.resolve()
    _, include, _ = build.pinned_include_dir()
    project_include, _, _ = build.project_include_dir()
    build_root.mkdir(parents=True, exist_ok=True)
    args = SimpleNamespace(build_root=build_root, compiler="C257", assembler="A234", opt="-O2 -G8")
    for attempt in range(retries + 1):
        job = build.prepare_job(args, source, tools, include, project_include)
        proc = build.run_dosbox(DOSBOX, job / "dosbox.conf", job)
        status = job / "STATUS.TXT"
        status_text = status.read_text(errors="replace").strip() if status.is_file() else "MISSING"
        if build.is_crash(proc.returncode, status_text) and attempt < retries:
            shutil.rmtree(job, ignore_errors=True)
            continue
        if proc.returncode != 0 or status_text != "OK":
            raise CompileError(f"toolchain {status_text} (exit {proc.returncode}) in {job}")
        return job
    raise AssertionError("unreachable")


def resolve_function(obj_path: Path, mapping: dict, function: str) -> bytes:
    """F's object code with every PsyQ patch applied at F's mapped address."""
    try:
        o = bytecheck.load_object(obj_path)  # with local symbols: a static function is one of them
    except bytecheck.CheckError as exc:
        raise CompileError(str(exc))
    text = bytecheck.text_symbols(o)
    xdef = next((x for x in text if x[0] == function), None)
    if xdef is None:
        raise CompileError(f"{function} is not defined by the object")
    _, sec, start, _ = xdef
    code = bytes(o.code.get(sec, b""))
    end = min((off for _, sn, off, _ in text if sn == sec and off > start), default=len(code))
    entry = mapping["functions"][function]
    addr = num(entry["address"])
    # The candidate's own extent, not the retail size, anchors .text-relative values.
    local = json.loads(json.dumps(mapping))
    local["functions"][function] = {**entry, "size": end - start}  # keeps its linkage ("static")
    resolve = bytecheck.Resolver(o, local).resolve
    gp = local.get("gp")
    gp = num(gp) if gp is not None else None
    out = bytearray(code[start:end])
    try:
        for typ, off, expr in o.patches.get(sec, []):
            if not start <= off < end:
                continue
            rel = off - start
            word = struct.unpack_from("<I", out, rel)[0]
            if typ == 82:  # HI16
                word = (word & 0xffff0000) | (((resolve(expr) + 0x8000) >> 16) & 0xffff)
            elif typ == 84:  # LO16
                word = (word & 0xffff0000) | (resolve(expr) & 0xffff)
            elif typ == 74:  # MIPS26
                target = resolve(expr)
                if target & 3 or (target ^ (addr + rel + 4)) & 0xf0000000:
                    raise CompileError(f"bad MIPS26 target 0x{target:08x}")
                word = (word & 0xfc000000) | ((target >> 2) & 0x03ffffff)
            elif typ == 30:  # GP-relative; the expression carries the GP subtraction
                if gp is None:
                    raise CompileError("symbol map has no gp value")
                delta = resolve(expr)
                if delta & 0x80000000:
                    delta -= 0x100000000
                if not -32768 <= delta <= 32767:
                    raise CompileError(f"GP relocation out of range: {delta}")
                word = (word & 0xffff0000) | (delta & 0xffff)
            else:
                raise CompileError(f"unsupported PsyQ patch type {typ}")
            struct.pack_into("<I", out, rel, word)
    except bytecheck.CheckError as exc:
        raise CompileError(str(exc)) from exc
    return bytes(out)


def retail_bytes(mapping: dict, function: str, reference: Path = REFERENCE) -> bytes:
    entry = mapping["functions"][function]
    addr, size = num(entry["address"]), int(entry["size"])
    raw = reference.read_bytes()
    if raw[:8] != b"PS-X EXE":
        raise CompileError("reference is not a PS-X EXE")
    load, text_size = struct.unpack_from("<II", raw, 0x18)
    if addr < load or addr + size > load + text_size:
        raise CompileError("function lies outside the executable")
    return raw[0x800 + addr - load:0x800 + addr - load + size]


def write_elf(path: Path, name: str, text: bytes) -> None:
    """A minimal ELF32 little-endian MIPS relocatable: .text plus one global FUNC symbol."""
    shstr = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
    strtab = b"\0" + name.encode("ascii") + b"\0"
    symtab = bytes(16) + struct.pack("<IIIBBH", 1, 0, len(text), 0x12, 0, 1)
    body = bytearray(52)
    offsets = []
    for blob in (text, symtab, strtab, shstr):
        while len(body) % 4:
            body.append(0)
        offsets.append(len(body))
        body += blob
    while len(body) % 4:
        body.append(0)
    shoff = len(body)
    sections = [bytes(40),
                struct.pack("<10I", 1, 1, 6, 0, offsets[0], len(text), 0, 0, 4, 0),
                struct.pack("<10I", 7, 2, 0, 0, offsets[1], len(symtab), 3, 1, 4, 16),
                struct.pack("<10I", 15, 3, 0, 0, offsets[2], len(strtab), 0, 0, 1, 0),
                struct.pack("<10I", 23, 3, 0, 0, offsets[3], len(shstr), 0, 0, 1, 0)]
    for s in sections:
        body += s
    header = (b"\x7fELF\x01\x01\x01" + bytes(9) +
              struct.pack("<HHIIIIIHHHHHH", 1, 8, 1, 0, 0, shoff, 0, 52, 0, 0, 40, 5, 4))
    body[:52] = header
    path.write_bytes(bytes(body))


def read_elf(path: Path) -> tuple[str, bytes]:
    """(function name, .text bytes) of an ELF written by write_elf."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise CompileError(f"{path} is not ELF")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shnum, shstrndx = struct.unpack_from("<HH", data, 0x30)
    secs = [struct.unpack_from("<10I", data, shoff + 40 * i) for i in range(shnum)]
    names = secs[shstrndx]

    def sname(s):
        start = names[4] + s[0]
        return data[start:data.index(b"\0", start)].decode()

    by_name = {sname(s): s for s in secs[1:]}
    text = by_name[".text"]
    symtab, strtab = by_name[".symtab"], by_name[".strtab"]
    name = "func"
    for i in range(symtab[5] // 16):
        st_name, _, _, info, _, shndx = struct.unpack_from("<IIIBBH", data, symtab[4] + 16 * i)
        if info & 0xf == 2:
            start = strtab[4] + st_name
            name = data[start:data.index(b"\0", start)].decode()
            break
    return name, data[text[4]:text[4] + text[5]]


def disassemble(path: Path) -> str:
    import capstone
    name, text = read_elf(path)
    dis = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 | capstone.CS_MODE_LITTLE_ENDIAN)
    lines = [f"{path}:     file format elf32-tradlittlemips", "", f"00000000 <{name}>:"]
    for off in range(0, len(text) - len(text) % 4, 4):
        word = text[off:off + 4]
        ins = next(dis.disasm(word, off), None)
        if ins is None:
            mnemonic, ops = ".word", f"0x{struct.unpack('<I', word)[0]:08x}"
        else:
            mnemonic, ops = ins.mnemonic, ins.op_str.replace("$", "").replace(", ", ",")
        lines.append(f"{off:8x}:\t{struct.unpack('<I', word)[0]:08x} \t{mnemonic}\t{ops}".rstrip())
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", nargs="?", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("--map", type=Path, help="symbol map (functions, symbols, sections, gp)")
    ap.add_argument("--function")
    ap.add_argument("--build-root", type=Path, default=build.PROJECT / "builds" / "permute" / "jobs")
    ap.add_argument("--keep", action="store_true", help="keep the DOSBox-X job directory")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--target", action="store_true", help="write the retail function as ELF")
    mode.add_argument("--disassemble", type=Path, metavar="FILE", help="objdump-style listing of FILE")
    mode.add_argument("--check", action="store_true", help="compile and byte-compare with retail")
    a = ap.parse_args()
    if a.disassemble:
        sys.stdout.write(disassemble(a.disassemble))
        return 0
    if not a.map or not a.function:
        ap.error("--map and --function are required")
    mapping = json.loads(a.map.read_text(encoding="utf-8"))
    if a.target:
        if not a.output:
            ap.error("--target needs -o")
        write_elf(a.output, a.function, retail_bytes(mapping, a.function))
        return 0
    if not a.source or not (a.output or a.check):
        ap.error("a source and -o (or --check) are required")
    job = None
    try:
        job = compile_object(a.source.resolve(), a.build_root.resolve())
        code = resolve_function(job / "INPUT.OBJ", mapping, a.function)
        if a.output:
            write_elf(a.output, a.function, code)
        if a.check:
            ref = retail_bytes(mapping, a.function)
            diffs = sum(1 for i in range(0, max(len(code), len(ref)), 4) if code[i:i + 4] != ref[i:i + 4])
            exact = code == ref
            print(json.dumps({"function": a.function, "object_size": len(code), "retail_size": len(ref),
                              "differing_words": diffs, "status": "match" if exact else "mismatch",
                              "job": str(job) if a.keep else None}))
            return 0 if exact else 1
        return 0
    except (CompileError, build.subprocess.TimeoutExpired, KeyError, OSError, ValueError) as exc:
        print(f"permute_compile: {exc}", file=sys.stderr)
        return 1
    finally:
        if job is not None and not a.keep:
            shutil.rmtree(job, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
