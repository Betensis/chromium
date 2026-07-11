import importlib.util
import pathlib
import tempfile
import unittest
from unittest.mock import patch


HOST = pathlib.Path(__file__).parents[1] / "native-host" / "marta_browser_host.py"
spec = importlib.util.spec_from_file_location("marta_browser_host", HOST)
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class DispatchTests(unittest.TestCase):
    def test_echo_keeps_correlation_id_and_unknown_method_is_structured(self):
        with tempfile.TemporaryDirectory() as tmp:
            dispatcher = host.Dispatcher(pathlib.Path(tmp))
            self.assertEqual(dispatcher.handle({"id": "same", "method": "smoke.echo", "params": {"value": "ok"}}), {"id": "same", "ok": True, "result": {"value": "ok"}})
            error = dispatcher.handle({"id": "same", "method": "tabs.list", "params": {}})
            self.assertEqual(error["id"], "same")
            self.assertFalse(error["ok"])
            self.assertEqual(error["error"]["code"], "INVALID_ARGUMENT")

    def test_workspace_choose_replaces_the_live_workspace(self):
        with tempfile.TemporaryDirectory() as tmp:
            first = pathlib.Path(tmp, "first")
            second = pathlib.Path(tmp, "second")
            first.mkdir()
            second.mkdir()
            dispatcher = host.Dispatcher(first)
            with patch.object(host, "choose_workspace", return_value=second):
                response = dispatcher.handle({"id": "pick", "method": "workspace.choose", "params": {}})
            self.assertEqual(response, {"id": "pick", "ok": True, "result": {"workspace": str(second.resolve())}})
            self.assertEqual(dispatcher.call("shell.run", {"command": "pwd", "timeoutSeconds": 3})["stdout"].strip(), str(second.resolve()))

    def test_workspace_choose_is_unsupported_off_macos(self):
        with tempfile.TemporaryDirectory() as tmp:
            dispatcher = host.Dispatcher(pathlib.Path(tmp))
            with patch.object(host.sys, "platform", "linux"):
                response = dispatcher.handle({"id": "pick", "method": "workspace.choose", "params": {}})
            self.assertFalse(response["ok"])
            self.assertEqual(response["error"]["code"], "UNSUPPORTED_PLATFORM")
