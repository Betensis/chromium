import importlib.util
import pathlib
import tempfile
import unittest


HOST = pathlib.Path(__file__).parents[1] / "native-host" / "marta_browser_host.py"
spec = importlib.util.spec_from_file_location("marta_browser_host", HOST)
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class FileToolTests(unittest.TestCase):
    def test_write_read_list_delete_are_rooted_and_atomic(self):
        with tempfile.TemporaryDirectory() as tmp:
            dispatcher = host.Dispatcher(pathlib.Path(tmp))
            written = dispatcher.call("files.write", {"path": "a.txt", "content": "ok", "overwrite": True})
            self.assertTrue(written["created"])
            self.assertEqual(dispatcher.call("files.read", {"path": "a.txt"})["content"], "ok")
            self.assertEqual(dispatcher.call("files.list", {"path": "."})["entries"], [{"path": "a.txt", "type": "file", "size": 2}])
            self.assertTrue(dispatcher.call("files.delete", {"path": "a.txt"})["deleted"])

    def test_write_requires_explicit_overwrite_for_existing_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            dispatcher = host.Dispatcher(pathlib.Path(tmp))
            dispatcher.call("files.write", {"path": "a.txt", "content": "old", "overwrite": True})
            with self.assertRaises(host.CommandError) as caught:
                dispatcher.call("files.write", {"path": "a.txt", "content": "new", "overwrite": False})
            self.assertEqual(caught.exception.code, "INVALID_ARGUMENT")

    def test_read_larger_than_native_frame_is_structured_too_large(self):
        with tempfile.TemporaryDirectory() as tmp:
            pathlib.Path(tmp, "large.txt").write_text("x" * host.MAX_NATIVE_MESSAGE_BYTES)
            response = host.Dispatcher(pathlib.Path(tmp)).handle({"id": "large", "method": "files.read", "params": {"path": "large.txt"}})
            self.assertEqual(response["error"]["code"], "TOO_LARGE")

    def test_open_file_is_anchored_to_workspace_fd_after_root_path_is_replaced(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = pathlib.Path(tmp)
            root, moved = base / "root", base / "moved"
            root.mkdir()
            (root / "safe.txt").write_text("safe")
            dispatcher = host.Dispatcher(root)
            root.rename(moved)
            root.mkdir()
            (root / "safe.txt").write_text("attacker")
            self.assertEqual(dispatcher.call("files.read", {"path": "safe.txt"})["content"], "safe")
