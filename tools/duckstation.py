"""Client for DuckStation's built-in MCP server (Streamable HTTP, JSON-RPC).

One persistent MCP session per DuckStation object. Tool results are the text
content of the JSON-RPC result, decoded as JSON when possible. Helpers cover
the runtime-RE loop: memory reads and local RAM snapshots/diffs, breakpoints,
waiting for a pause, input sequences, memory scans and watches, save states.

    python tools/duckstation.py status
    python tools/duckstation.py tools
    python tools/duckstation.py call <tool> ['<json args>']
    python tools/duckstation.py read <address> <size>
    python tools/duckstation.py dump <path> [--address A --size N]
    python tools/duckstation.py cleanup      # pause, clear breakpoints and watches

The emulator side is documented by the DuckStation MCP plugin; argument names
here follow its tool schemas (`python tools/duckstation.py tools --schema`).
"""
from __future__ import annotations

import argparse
import http.client
import json
import os
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_URL = "http://127.0.0.1:2346/mcp"  # not localhost: IPv6-first resolution costs ~2 s per request on Windows
RAM_BASE = 0x80000000
RAM_SIZE = 0x200000
INLINE_LIMIT = 0x4000  # server limit is 0x10000, but inline replies above ~16 KB stall the HTTP reply
BUTTONS = {"Cross", "Circle", "Square", "Triangle", "Up", "Down", "Left", "Right",
           "L1", "L2", "R1", "R2", "Start", "Select", "L3", "R3"}


class DuckStationError(RuntimeError):
    """A JSON-RPC error or a tool result flagged isError."""


def parse_response(body: str, content_type: str = "application/json"):
    """Return the JSON-RPC message in a response body (plain JSON or SSE)."""
    if "text/event-stream" in content_type or body.lstrip().startswith(("event:", "data:")):
        messages = []
        for line in body.splitlines():
            if line.startswith("data:"):
                payload = line[5:].strip()
                if payload:
                    messages.append(json.loads(payload))
        replies = [m for m in messages if "result" in m or "error" in m]
        if not replies:
            raise DuckStationError("no JSON-RPC reply in event stream")
        return replies[-1]
    return json.loads(body) if body.strip() else None


def tool_result(message):
    """Decode a tools/call reply: the text content, parsed as JSON when it is JSON."""
    if message is None:
        raise DuckStationError("empty reply")
    if "error" in message:
        raise DuckStationError(json.dumps(message["error"]))
    result = message["result"]
    texts = [c.get("text", "") for c in result.get("content", []) if c.get("type") == "text"]
    text = "\n".join(texts)
    if result.get("isError"):
        raise DuckStationError(text)
    try:
        return json.loads(text)
    except ValueError:
        return text


def address(value) -> int:
    """Normalise an address (int, '0x...', kuseg or kseg0) to a kseg0 integer."""
    value = int(value, 0) if isinstance(value, str) else int(value)
    if value < RAM_SIZE:
        value |= RAM_BASE
    return value


def hexaddr(value) -> str:
    return f"0x{address(value):08x}"


def input_steps(*steps):
    """Build an input_sequence list from (buttons, frames) pairs.

    `buttons` may be a name, a list of names, or '' / None for released."""
    sequence = []
    for buttons, frames in steps:
        if not buttons:
            names = []
        elif isinstance(buttons, str):
            names = [b for b in buttons.split("+") if b]
        else:
            names = list(buttons)
        unknown = [b for b in names if b not in BUTTONS]
        if unknown:
            raise ValueError(f"unknown buttons {unknown}")
        if int(frames) <= 0:
            raise ValueError("duration_frames must be positive")
        sequence.append({"buttons": names, "duration_frames": int(frames)})
    return sequence


def diff_bytes(old: bytes, new: bytes, base: int = RAM_BASE, width: int = 1):
    """Changed cells between two equally sized images.

    Returns [(address, old_value, new_value)] for each `width`-byte aligned cell
    (1, 2 or 4, little endian) that differs."""
    if len(old) != len(new):
        raise ValueError("images differ in size")
    if width not in (1, 2, 4):
        raise ValueError("width must be 1, 2 or 4")
    changes = []
    for offset in range(0, len(old) - len(old) % width, width):
        a, b = old[offset:offset + width], new[offset:offset + width]
        if a != b:
            changes.append((base + offset, int.from_bytes(a, "little"), int.from_bytes(b, "little")))
    return changes


def group_runs(changes, gap: int = 4):
    """Merge diff cells into [start, end_exclusive) runs separated by more than `gap` bytes."""
    runs = []
    for addr, _, _ in sorted(changes):
        if runs and addr - runs[-1][1] <= gap:
            runs[-1][1] = addr + 1
        else:
            runs.append([addr, addr + 1])
    return [tuple(r) for r in runs]


class DuckStation:
    def __init__(self, url: str = DEFAULT_URL, timeout: float = 120.0, transport=None):
        self.url = url
        self.timeout = timeout
        self.session_id = None
        self._next_id = 1
        # transport(body_bytes, headers) -> (status, headers_dict, body_text); replaceable in tests
        self._transport = transport or self._http

    # -- transport -------------------------------------------------------
    def _http(self, data: bytes, headers: dict):
        """POST over one kept-alive connection. A new socket per request leaves a TIME_WAIT
        port behind each time; long breakpoint loops then run out of ports (WinError 10048)."""
        parts = urllib.parse.urlsplit(self.url)
        for attempt in (0, 1):
            if getattr(self, "_connection", None) is None:
                self._connection = http.client.HTTPConnection(parts.hostname, parts.port or 80,
                                                              timeout=self.timeout)
            try:
                self._connection.request("POST", parts.path or "/", body=data, headers=headers)
                reply = self._connection.getresponse()
                body = reply.read().decode("utf-8", "replace")
                if reply.getheader("Connection", "").lower() == "close":
                    self._connection.close()
                    self._connection = None
                return reply.status, dict(reply.getheaders()), body
            except (http.client.HTTPException, OSError):
                self._connection.close()
                self._connection = None
                if attempt:
                    raise

    def _post(self, message, notify=False):
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.session_id:
            headers["MCP-Session-Id"] = self.session_id
        status, reply_headers, body = self._transport(json.dumps(message).encode(), headers)
        lowered = {k.lower(): v for k, v in reply_headers.items()}
        if lowered.get("mcp-session-id"):
            self.session_id = lowered["mcp-session-id"]
        if status in (404, 400) and self.session_id and not notify:
            raise _SessionExpired(body)
        if status >= 400:
            raise DuckStationError(f"HTTP {status}: {body[:300]}")
        if notify:
            return None
        return parse_response(body, lowered.get("content-type", "application/json"))

    def _id(self):
        self._next_id += 1
        return self._next_id

    def connect(self):
        self.session_id = None
        reply = self._post({"jsonrpc": "2.0", "id": self._id(), "method": "initialize",
                            "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                                       "clientInfo": {"name": "alien-decomp", "version": "1"}}})
        if reply is None or "error" in reply:
            raise DuckStationError(f"initialize failed: {reply}")
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"}, notify=True)
        return reply["result"]

    def _request(self, method, params=None):
        if self.session_id is None:
            self.connect()
        message = {"jsonrpc": "2.0", "id": self._id(), "method": method}
        if params is not None:
            message["params"] = params
        try:
            return self._post(message)
        except _SessionExpired:
            self.connect()
            message["id"] = self._id()
            return self._post(message)

    def list_tools(self):
        return self._request("tools/list")["result"]["tools"]

    def call(self, tool: str, **arguments):
        arguments = {k: v for k, v in arguments.items() if v is not None}
        return tool_result(self._request("tools/call", {"name": tool, "arguments": arguments}))

    # -- system ----------------------------------------------------------
    def status(self):
        return self.call("get_status")

    def pause(self):
        return self.call("pause")

    def resume(self):
        return self.call("continue")

    def is_paused(self):
        return self.call("wait_for_pause").get("status") == "paused"

    def wait_for_pause(self, timeout: float = 30.0, poll: float = 0.05):
        """Poll until the CPU stops; return the pc, or None on timeout (still running)."""
        end = time.monotonic() + timeout
        while True:
            state = self.call("wait_for_pause")
            if state.get("status") == "paused":
                return int(state["pc"], 16)
            if time.monotonic() >= end:
                return None
            time.sleep(poll)

    def run_frames(self, frames: int):
        """Run `frames` frames from a pause and stop again (frame_step loop)."""
        for _ in range(frames):
            self.call("frame_step")

    def run_for(self, seconds: float):
        """Run free for a wall-clock interval, then pause."""
        self.resume()
        time.sleep(seconds)
        self.pause()
        self.wait_for_pause(5)

    def set_speed(self, speed: float):
        return self.call("set_speed", speed=speed)

    def save_state(self, slot: int):
        return self.call("save_state", slot=slot)

    def load_state(self, slot: int):
        return self.call("load_state", slot=slot)

    def screenshot(self, path=None):
        return self.call("take_screenshot", path=path)

    def registers(self, group="gpr"):
        return {k: int(v, 16) for k, v in self.call("read_registers", group=group).items()}

    def disassemble(self, addr, count=20):
        return self.call("disassemble", address=hexaddr(addr), count=count)

    # -- input -----------------------------------------------------------
    def input_sequence(self, *steps, slot: int = 0):
        """Run timed input. Steps are (buttons, frames) pairs, see input_steps."""
        return self.call("input_sequence", sequence=input_steps(*steps), slot=slot)

    def press(self, button: str, frames: int = 4, slot: int = 0):
        return self.call("press_button", button=button, duration_frames=frames, slot=slot)

    # -- memory ----------------------------------------------------------
    def read(self, addr, size: int) -> bytes:
        """Read memory. Inline replies are limited to INLINE_LIMIT bytes; larger
        reads go through a raw file written by the emulator (same host)."""
        addr = address(addr)
        if size > INLINE_LIMIT:
            handle, path = tempfile.mkstemp(suffix=".bin", prefix="ds_read_")
            os.close(handle)
            try:
                self.call("read_memory", address=hexaddr(addr), size=size, path=path)
                with open(path, "rb") as raw:
                    data = raw.read()
            finally:
                os.unlink(path)
            if len(data) != size:
                raise DuckStationError(f"short read: {len(data)} of {size} bytes")
            return data
        reply = self.call("read_memory", address=hexaddr(addr), size=size, format="hex")
        return bytes.fromhex(reply["data"])

    def read_u8(self, addr):
        return self.read(addr, 1)[0]

    def read_u16(self, addr):
        return int.from_bytes(self.read(addr, 2), "little")

    def read_s16(self, addr):
        return int.from_bytes(self.read(addr, 2), "little", signed=True)

    def read_u32(self, addr):
        return int.from_bytes(self.read(addr, 4), "little")

    def read_s32(self, addr):
        return int.from_bytes(self.read(addr, 4), "little", signed=True)

    def write(self, addr, data: bytes):
        return self.call("write_memory", address=hexaddr(addr), data=data.hex(), format="hex")

    def dump_ram(self, path=None, addr=RAM_BASE, size=RAM_SIZE) -> bytes:
        """Read a RAM region (default all 2 MB); optionally save the raw bytes."""
        data = self.read(addr, size)
        if path:
            with open(path, "wb") as handle:
                handle.write(data)
        return data

    def snapshot(self, addr=RAM_BASE, size=RAM_SIZE):
        """A local snapshot: (base, bytes). Diff later with diff()."""
        return address(addr), self.read(addr, size)

    def diff(self, snapshot, width: int = 1):
        base, old = snapshot
        return diff_bytes(old, self.read(base, len(old)), base, width)

    def search(self, pattern: bytes, mask: bytes = None, start=0):
        return self.call("search_memory", pattern=pattern.hex(),
                         mask=mask.hex() if mask else None, start=hexaddr(start) if start else None)

    # -- breakpoints -----------------------------------------------------
    def breakpoint_add(self, addr, kind="execute"):
        return self.call("breakpoint", action="add", type=kind, address=hexaddr(addr))

    def breakpoint_remove(self, addr, kind="execute"):
        return self.call("breakpoint", action="remove", type=kind, address=hexaddr(addr))

    def breakpoints(self):
        return self.call("breakpoint", action="list")

    def breakpoints_clear(self):
        return self.call("breakpoint", action="clear")

    def catch(self, addr, kind="write", timeout: float = 10.0, inputs=None):
        """Set one breakpoint, run (optionally with input), and return the
        register file at the first hit, or None. The breakpoint is removed."""
        self.breakpoint_add(addr, kind)
        try:
            if inputs:
                self.call("input_sequence", sequence=input_steps(*inputs), slot=0)
            self.resume()
            pc = self.wait_for_pause(timeout)
            if pc is None:
                self.pause()
                self.wait_for_pause(5)
                return None
            return self.registers()
        finally:
            self.breakpoint_remove(addr, kind)

    # -- scans and watches -----------------------------------------------
    def scan_start(self, value=None, operator="equal", size="word", signed=False, start=None, end=None):
        return self.call("memory_scan", action="start", operator=operator, value=value, size=size,
                         signed=signed, start=hexaddr(start) if start else None,
                         end=hexaddr(end) if end else None)

    def scan_refine(self, operator, value=None):
        return self.call("memory_scan", action="refine", operator=operator, value=value)

    def scan_results(self, count=100, offset=0):
        return self.call("memory_scan", action="results", count=count, offset=offset)

    def scan_reset(self):
        return self.call("memory_scan", action="reset")

    def watch_add(self, addr, description, size="word", signed=False):
        return self.call("memory_watch", action="add", address=hexaddr(addr),
                         description=description, size=size, signed=signed)

    def watches(self):
        return self.call("memory_watch", action="list")

    def watch_remove(self, addr):
        return self.call("memory_watch", action="remove", address=hexaddr(addr))

    def cleanup(self):
        """Leave the emulator paused with no breakpoints, watches or scan state."""
        if not self.is_paused():
            self.pause()
            self.wait_for_pause(5)
        self.breakpoints_clear()
        for watch in self.watches().get("watches", []):
            self.watch_remove(watch["address"])
        self.scan_reset()
        for button in sorted(BUTTONS):
            try:
                self.call("release_button", button=button)
            except DuckStationError:
                pass


class _SessionExpired(Exception):
    pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--url", default=DEFAULT_URL)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status")
    tools = sub.add_parser("tools")
    tools.add_argument("--schema", action="store_true")
    call = sub.add_parser("call")
    call.add_argument("tool")
    call.add_argument("arguments", nargs="?", default="{}")
    read = sub.add_parser("read")
    read.add_argument("address")
    read.add_argument("size", type=lambda s: int(s, 0))
    dump = sub.add_parser("dump")
    dump.add_argument("path")
    dump.add_argument("--address", default=hex(RAM_BASE))
    dump.add_argument("--size", type=lambda s: int(s, 0), default=RAM_SIZE)
    sub.add_parser("cleanup")
    args = parser.parse_args(argv)
    ds = DuckStation(args.url)
    if args.command == "status":
        print(json.dumps(ds.status(), indent=1))
    elif args.command == "tools":
        for tool in ds.list_tools():
            if args.schema:
                print(tool["name"], json.dumps(tool.get("inputSchema", {}).get("properties", {})))
            else:
                print(tool["name"])
    elif args.command == "call":
        result = ds.call(args.tool, **json.loads(args.arguments))
        print(result if isinstance(result, str) else json.dumps(result, indent=1))
    elif args.command == "read":
        data = ds.read(args.address, args.size)
        base = address(args.address)
        for offset in range(0, len(data), 16):
            print(f"{base + offset:08x}  {data[offset:offset + 16].hex(' ')}")
    elif args.command == "dump":
        ds.dump_ram(args.path, args.address, args.size)
        print(f"wrote {args.size:#x} bytes from {address(args.address):#010x} to {args.path}")
    elif args.command == "cleanup":
        ds.cleanup()
        print(json.dumps(ds.status()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
