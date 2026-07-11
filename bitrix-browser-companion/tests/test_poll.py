import importlib.util
import io
import pathlib
import tempfile
import unittest


HOST = pathlib.Path(__file__).parents[1] / "native-host" / "marta_browser_host.py"
spec = importlib.util.spec_from_file_location("marta_browser_host", HOST)
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class PollTests(unittest.TestCase):
    def test_malformed_poll_payload_is_ignored_and_next_poll_continues(self):
        with tempfile.TemporaryDirectory() as tmp:
            bridge = host.MartaBridge({"martaUrl": "http://example", "browserToken": "x", "portalHost": "p", "userId": 1, "clientId": "c"}, host.Dispatcher(tmp), io.BytesIO())
            with self.assertRaises(ValueError):
                bridge.validate_command_json(b"[]")
            self.assertEqual(bridge.validate_command_json(b'{"id":"1","method":"tabs.list","params":{}}')["method"], "tabs.list")
