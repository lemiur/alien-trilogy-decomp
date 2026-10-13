"""Tests for tools/duckstation.py that need no emulator (a fake transport)."""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import duckstation as ds  # noqa: E402


class FakeServer:
    """Answers initialize, tools/call read_memory/get_status, and can expire a session once."""

    def __init__(self, sse=False, expire_once=False):
        self.sse = sse
        self.expire_once = expire_once
        self.sessions = 0
        self.calls = []

    def __call__(self, data, headers):
        message = json.loads(data)
        self.calls.append((message, dict(headers)))
        if message["method"] == "initialize":
            self.sessions += 1
            return self._reply(message, {"protocolVersion": "2025-06-18"}, {"MCP-Session-Id": f"s{self.sessions}"})
        if message["method"] == "notifications/initialized":
            return 202, {}, ""
        if self.expire_once and headers.get("MCP-Session-Id") == "s1":
            self.expire_once = False
            return 404, {}, "session not found"
        name = message["params"]["name"]
        args = message["params"]["arguments"]
        if name == "read_memory":
            base = int(args["address"], 16) & 0xff
            data = bytes((base + i) & 0xff for i in range(args["size"]))
            text = json.dumps({"address": args["address"], "size": args["size"], "data": data.hex()})
        elif name == "breakpoint" and args.get("action") == "add" and "address" not in args:
            return self._reply(message, {"content": [{"type": "text", "text": "address required"}], "isError": True})
        else:
            text = json.dumps({"tool": name, "arguments": args})
        return self._reply(message, {"content": [{"type": "text", "text": text}]})

    def _reply(self, message, result, headers=None):
        body = json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": result})
        headers = dict(headers or {})
        if self.sse:
            headers["Content-Type"] = "text/event-stream"
            body = f"event: message\ndata: {body}\n\n"
        else:
            headers["Content-Type"] = "application/json"
        return 200, headers, body


class ParsingTests(unittest.TestCase):
    def test_sse_and_json_bodies(self):
        reply = {"jsonrpc": "2.0", "id": 3, "result": {"content": [{"type": "text", "text": "{\"a\": 1}"}]}}
        sse = "event: message\ndata: " + json.dumps(reply) + "\n\n"
        self.assertEqual(ds.parse_response(sse, "text/event-stream"), reply)
        self.assertEqual(ds.parse_response(json.dumps(reply)), reply)
        self.assertEqual(ds.tool_result(reply), {"a": 1})

    def test_plain_text_and_errors(self):
        text = {"result": {"content": [{"type": "text", "text": "hello"}]}}
        self.assertEqual(ds.tool_result(text), "hello")
        with self.assertRaises(ds.DuckStationError):
            ds.tool_result({"result": {"content": [{"type": "text", "text": "bad"}], "isError": True}})
        with self.assertRaises(ds.DuckStationError):
            ds.tool_result({"error": {"code": -1}})

    def test_addresses(self):
        self.assertEqual(ds.address("0x8d560"), 0x8008d560)
        self.assertEqual(ds.address(0x8008d560), 0x8008d560)
        self.assertEqual(ds.hexaddr(0x1000), "0x80001000")

    def test_input_steps(self):
        self.assertEqual(ds.input_steps(("Start", 5), (None, 30), ("Up+Cross", 2)),
                         [{"buttons": ["Start"], "duration_frames": 5},
                          {"buttons": [], "duration_frames": 30},
                          {"buttons": ["Up", "Cross"], "duration_frames": 2}])
        with self.assertRaises(ValueError):
            ds.input_steps(("Jump", 1))
        with self.assertRaises(ValueError):
            ds.input_steps(("Start", 0))

    def test_diff_and_runs(self):
        old = bytes(16)
        new = bytearray(16)
        new[1] = 5
        new[2] = 6
        new[12] = 1
        self.assertEqual(ds.diff_bytes(old, bytes(new), 0x80000000),
                         [(0x80000001, 0, 5), (0x80000002, 0, 6), (0x8000000c, 0, 1)])
        self.assertEqual(ds.diff_bytes(old, bytes(new), 0x80000000, 2),
                         [(0x80000000, 0, 0x0500), (0x80000002, 0, 6), (0x8000000c, 0, 1)])
        self.assertEqual(ds.group_runs(ds.diff_bytes(old, bytes(new), 0x80000000)),
                         [(0x80000001, 0x80000003), (0x8000000c, 0x8000000d)])


class ClientTests(unittest.TestCase):
    def test_session_and_read(self):
        for sse in (False, True):
            server = FakeServer(sse=sse)
            client = ds.DuckStation(transport=server)
            self.assertEqual(client.read(0x80001010, 4), bytes([0x10, 0x11, 0x12, 0x13]))
            self.assertEqual(client.read_u16(0x80000002), 0x0302)
            self.assertEqual(server.sessions, 1)
            self.assertEqual(server.calls[-1][1]["MCP-Session-Id"], "s1")

    def test_reconnects_on_expired_session(self):
        server = FakeServer(expire_once=True)
        client = ds.DuckStation(transport=server)
        self.assertEqual(client.status()["tool"], "get_status")
        self.assertEqual(server.sessions, 2)

    def test_none_arguments_dropped_and_errors_raised(self):
        server = FakeServer()
        client = ds.DuckStation(transport=server)
        reply = client.scan_start(value=100, size="halfword")
        self.assertNotIn("start", reply["arguments"])
        self.assertEqual(reply["arguments"]["value"], 100)
        with self.assertRaises(ds.DuckStationError):
            client.call("breakpoint", action="add", type="write")

    def test_snapshot_diff(self):
        client = ds.DuckStation(transport=FakeServer())
        snap = client.snapshot(0x80000000, 8)
        self.assertEqual(client.diff(snap), [])


if __name__ == "__main__":
    unittest.main()
