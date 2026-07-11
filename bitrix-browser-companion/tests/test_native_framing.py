import importlib.util
import io
import base64
import json
import pathlib
import unittest


HOST = pathlib.Path(__file__).parents[1] / "native-host" / "marta_browser_host.py"
spec = importlib.util.spec_from_file_location("marta_browser_host", HOST)
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class NativeFramingTests(unittest.TestCase):
    def test_round_trip_uses_little_endian_json_length(self):
        stream = io.BytesIO()
        host.write_message(stream, {"id": "abc", "ok": True})
        self.assertEqual(stream.getvalue()[:4], (22).to_bytes(4, "little"))
        stream.seek(0)
        self.assertEqual(host.read_message(stream), {"id": "abc", "ok": True})

    def test_rejects_oversized_message(self):
        class HeaderOnlyStream:
            def __init__(self): self.calls = 0
            def read(self, size):
                self.calls += 1
                if self.calls == 1:
                    return (host.MAX_NATIVE_INBOUND_MESSAGE_BYTES + 1).to_bytes(4, "little")
                raise AssertionError("oversized frame must fail before waiting for payload")
        stream = HeaderOnlyStream()
        with self.assertRaises(host.ProtocolError):
            host.read_message(stream)

    def test_rejects_outgoing_frame_larger_than_one_mib(self):
        self.assertEqual(host.MAX_NATIVE_MESSAGE_BYTES, 1024 * 1024)
        with self.assertRaises(host.ProtocolError):
            host.write_message(io.BytesIO(), {"payload": "x" * host.MAX_NATIVE_MESSAGE_BYTES})

    def test_accepts_bounded_inbound_capture_with_long_url(self):
        capture = base64.b64encode(b"x" * (700 * 1024)).decode("ascii")
        message = {"id": "capture", "ok": True, "result": {"tabId": 1, "url": "https://example.test/" + "a" * 180_000, "imageDataUrl": "data:image/jpeg;base64," + capture}}
        payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
        self.assertGreater(len(payload), host.MAX_NATIVE_MESSAGE_BYTES)
        self.assertLess(len(payload), 8 * 1024 * 1024)
        stream = io.BytesIO(len(payload).to_bytes(4, "little") + payload)
        self.assertEqual(host.read_message(stream)["id"], "capture")
