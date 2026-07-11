import importlib.util
import pathlib
import tempfile
import unittest


HOST = pathlib.Path(__file__).parents[1] / "native-host" / "marta_browser_host.py"
spec = importlib.util.spec_from_file_location("marta_browser_host", HOST)
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class ShellToolTests(unittest.TestCase):
    def test_returns_exit_streams_and_workspace_cwd_without_browser_token(self):
        with tempfile.TemporaryDirectory() as tmp:
            dispatcher = host.Dispatcher(pathlib.Path(tmp), browser_token="secret")
            result = dispatcher.call("shell.run", {"command": "printf '%s:%s' \"$PWD\" \"${MARTA_BROWSER_TOKEN-unset}\"; exit 7", "cwd": ".", "timeoutSeconds": 3})
            self.assertEqual(result["exitCode"], 7)
            self.assertIn(f"{tmp}:unset", result["stdout"])
            self.assertFalse(result["timedOut"])

    def test_times_out(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = host.Dispatcher(pathlib.Path(tmp)).call("shell.run", {"command": "sleep 2", "timeoutSeconds": 1})
            self.assertTrue(result["timedOut"])
