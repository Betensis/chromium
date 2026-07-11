import pathlib
import re
import unittest


SOURCE = (pathlib.Path(__file__).parents[1] / "extension" / "service-worker.js").read_text()


class PageCaptureContractTests(unittest.TestCase):
    def test_visible_capture_activates_and_rechecks_tab_before_and_after_capture(self):
        self.assertIn("chrome.windows.update(tab.windowId, {focused: true})", SOURCE)
        self.assertIn("await chrome.tabs.update(tab.id, {active: true})", SOURCE)
        self.assertGreaterEqual(SOURCE.count("chrome.tabs.query({active: true, windowId:"), 2)
        self.assertIn("STALE_PAGE", SOURCE)

    def test_jpeg_policy_and_snapshot_limits_are_present(self):
        self.assertRegex(SOURCE, r"quality:\s*0\.82")
        self.assertIn("1920 / Math.max(bitmap.width, bitmap.height)", SOURCE)
        self.assertIn("MAX_NATIVE_CAPTURE_BYTES = 700 * 1024", SOURCE)
        self.assertIn("slice(0, 4096)", SOURCE)
        self.assertIn("256 * 1024", SOURCE)
        self.assertNotIn("document.cookie", SOURCE)

    def test_snapshot_rechecks_selected_tab_and_location(self):
        snapshot_body = re.search(r"async function snapshot\(tab\) \{(.*?)\n}\nasync function execute", SOURCE, re.S).group(1)
        self.assertIn("await checkedActive(tab)", snapshot_body)
        self.assertIn("STALE_PAGE", snapshot_body)

    def test_portal_transport_does_not_read_or_forward_csrf(self):
        self.assertNotIn('message.type === "portal.csrf"', SOURCE)
        self.assertNotIn('BX?.bitrix_sessid?.()', SOURCE)
