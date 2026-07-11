import importlib.util
import pathlib
import tempfile
import unittest


HOST = pathlib.Path(__file__).parents[1] / "native-host" / "marta_browser_host.py"
spec = importlib.util.spec_from_file_location("marta_browser_host", HOST)
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class PathTests(unittest.TestCase):
    def test_rejects_escape_absolute_nul_and_symlink_components(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "link").symlink_to("/tmp")
            resolver = host.Workspace(root)
            for value in ("../escape", "/tmp/escape", "bad\x00name", "link/file"):
                with self.subTest(value=value), self.assertRaises(host.CommandError) as caught:
                    resolver.path(value)
                self.assertEqual(caught.exception.code, "INVALID_ARGUMENT")
