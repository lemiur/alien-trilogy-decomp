"""Replay startup operands to record placement constraints, without coverage credit.

The cleared memory interval is observable. Its division among .bss, .sbss,
COMMON, and linker padding cannot be inferred from the loop alone.
"""
from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
REFERENCE = PROJECT.parent / "games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07"


def signed16(value):
    value &= 0xffff
    return value - 0x10000 if value & 0x8000 else value


def build_report(raw, expected_hash):
    actual_hash = hashlib.sha256(raw).hexdigest()
    if actual_hash != expected_hash:
        raise ValueError("reference hash differs from the pinned USA executable")
    if raw[:8] != b"PS-X EXE":
        raise ValueError("reference is not PS-X EXE")
    entry, header_gp, load, size = struct.unpack_from("<4I", raw, 0x10)
    if len(raw) != 0x800 + size:
        raise ValueError("reference payload extent is inconsistent")

    def word(address):
        offset = address - load
        if offset < 0 or offset + 4 > size:
            raise ValueError("startup instruction outside loaded payload")
        return struct.unpack_from("<I", raw, 0x800 + offset)[0]

    def address_pair(address, register):
        hi, lo = word(address), word(address + 4)
        if hi >> 16 != (15 << 10 | register):
            raise ValueError("expected LUI at startup address pair")
        if lo >> 16 != (9 << 10 | register << 5 | register):
            raise ValueError("expected same-register ADDIU at startup address pair")
        return (((hi & 0xffff) << 16) + signed16(lo)) & 0xffffffff

    clear_start = address_pair(entry, 2)
    clear_end = address_pair(entry + 8, 3)
    # SW zero,0(v0); ADDIU v0,v0,4; SLTU at,v0,v1;
    # BNE at,zero,back-to-store; NOP. Check every field, including branch.
    expected_loop = (0xac400000, 0x24420004, 0x0043082b, 0x1420fffc, 0)
    if tuple(word(entry + 16 + 4 * i) for i in range(5)) != expected_loop:
        raise ValueError("startup zero-clear loop shape differs")
    if clear_start & 3 or clear_end & 3 or clear_end <= clear_start:
        raise ValueError("invalid startup clear interval")
    gp = address_pair(entry + 0x80, 28)
    heap_base_operand = address_pair(entry + 0x50, 4)
    if heap_base_operand != clear_end:
        raise ValueError("post-clear heap operand disagrees with cleared end")
    loaded_clear_end = min(clear_end, load + size)
    return {
        "reference_sha256": actual_hash,
        "analyzer_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "header": {"entry": hex(entry), "gp": hex(header_gp),
                   "load": hex(load), "payload_size": size,
                   "header_bss_address": hex(word_header(raw, 0x28)),
                   "header_bss_size": word_header(raw, 0x2c)},
        "startup_gp": hex(gp),
        "zero_clear_interval": {"start": hex(clear_start), "end_exclusive": hex(clear_end),
                                "size": clear_end - clear_start,
                                "instruction_start": hex(entry), "instruction_size": 36,
                                "loaded_payload_intersection_bytes": max(0, loaded_clear_end - max(clear_start, load))},
        "post_clear_heap_base_operand": hex(heap_base_operand),
        "new_certified_payload_bytes": 0,
        "limitations": ["The interval is a startup clearing constraint, not a recovered BSS section map.",
                        "Header BSS fields are zero; .bss/.sbss/COMMON boundaries and allocation sizes remain unresolved.",
                        "No executable bytes are reconstructed by this analyzer."],
    }


def word_header(raw, offset):
    return struct.unpack_from("<I", raw, offset)[0]


def main():
    lock = json.loads((PROJECT / "config/toolchain.lock.json").read_text())
    raw = REFERENCE.read_bytes()
    report = build_report(raw, lock["reference_sha256"])
    # Validate the operand and loop checks on disposable, rehashed inputs.
    cases = []
    for name, offset in (("bad_clear_loop", 16), ("bad_gp_pair", 0x83), ("bad_heap_end", 0x54)):
        changed = bytearray(raw)
        file_offset = 0x800 + int(report["header"]["entry"], 0) - int(report["header"]["load"], 0) + offset
        changed[file_offset] ^= 1
        try:
            build_report(bytes(changed), hashlib.sha256(changed).hexdigest())
        except ValueError as exc:
            cases.append({"case": name, "status": "rejected", "reason": str(exc)})
        else:
            raise AssertionError(f"invalid startup accepted: {name}")
    report["negative_checks"] = cases
    output = PROJECT / "reports/startup_layout.json"
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
