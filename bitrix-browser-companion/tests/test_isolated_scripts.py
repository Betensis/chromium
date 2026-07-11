import pathlib
import unittest


SCRIPTS = pathlib.Path(__file__).parents[1] / "scripts"


class IsolatedAutomationScriptTests(unittest.TestCase):
    def test_start_script_requires_explicit_isolation_and_installs_profile_manifest(self):
        script = (SCRIPTS / "start-isolated-chromium.sh").read_text(encoding="utf-8")
        self.assertIn("--chromium-app", script)
        self.assertIn("--user-data-dir", script)
        self.assertIn("--cdp-port", script)
        self.assertIn("install-native-host.sh", script)
        self.assertIn("open -na", script)
        self.assertIn("--remote-debugging-port", script)
        self.assertIn("pinned_extensions", script)
        self.assertIn("ldkkgojejbhlogpkhoclefempppipgmf", script)
        self.assertIn("--disable-features=SidePanelFlyoverAnimation", script)
        self.assertIn("print -u2 --", script)
        self.assertNotIn("rm -rf", script)

    def test_stop_script_checks_pid_command_and_profile_before_killing(self):
        script = (SCRIPTS / "stop-isolated-chromium.sh").read_text(encoding="utf-8")
        self.assertIn("--user-data-dir", script)
        self.assertIn("ps -p", script)
        self.assertIn("kill -TERM", script)
        self.assertIn("/Contents/MacOS/", script)
        self.assertIn("pidfile", script)
        self.assertIn("print -u2 --", script)
        self.assertNotIn("pkill", script)
        self.assertNotIn("killall", script)

    def test_cdp_smoke_limits_cleanup_to_tagged_fixture_pages(self):
        script = (SCRIPTS / "smoke-isolated-cdp.py").read_text(encoding="utf-8")
        self.assertIn("chrome-extension://", script)
        self.assertIn("service_worker", script)
        self.assertIn("marta-browser-companion-smoke", script)
        self.assertIn("/json/close/", script)


if __name__ == "__main__":
    unittest.main()
