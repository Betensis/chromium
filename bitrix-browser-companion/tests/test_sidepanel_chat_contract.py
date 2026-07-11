import pathlib
import unittest


ROOT = pathlib.Path(__file__).parents[1]


class SidePanelChatContractTests(unittest.TestCase):
    def test_manifest_declares_side_panel_and_local_portal_permission(self):
        manifest = (ROOT / "extension" / "manifest.json").read_text(encoding="utf-8")
        self.assertIn('"sidePanel"', manifest)
        self.assertIn('"default_path": "sidepanel.html"', manifest)
        self.assertIn('"<all_urls>"', manifest)
        self.assertNotIn('portal-pull-main.js', manifest)
        self.assertNotIn('portal-pull-relay.js', manifest)

    def test_service_worker_keeps_native_host_for_workspace_and_opens_side_panel(self):
        source = (ROOT / "extension" / "service-worker.js").read_text(encoding="utf-8")
        self.assertNotIn('method: "chat.send"', source)
        self.assertNotIn("pendingChatRequests", source)
        self.assertIn('method: "workspace.choose"', source)
        self.assertIn("chrome.sidePanel.setPanelBehavior", source)
        self.assertIn('message.type === "page.context"', source)
        self.assertIn('page.screenshot', source)
        self.assertIn('page.snapshot', source)

    def test_action_opens_side_panel_before_async_storage_write(self):
        source = (ROOT / "extension" / "service-worker.js").read_text(encoding="utf-8")
        self.assertIn("chrome.sidePanel.setPanelBehavior", source)
        self.assertIn("openPanelOnActionClick: true", source)
        self.assertIn("chrome.runtime.onInstalled.addListener", source)
        self.assertIn("chrome.runtime.onStartup.addListener", source)

    def test_side_panel_uses_portal_transport_not_native_chat(self):
        source = (ROOT / "extension" / "sidepanel.js").read_text(encoding="utf-8")
        self.assertIn('BrowserThreadTransport', source)
        self.assertIn('type: "portal.getOrigin"', source)
        self.assertNotIn('type: "portal.csrf"', source)
        service_worker = (ROOT / "extension" / "service-worker.js").read_text(encoding="utf-8")
        self.assertNotIn("portalCsrfToken", service_worker)
        self.assertIn('page.context', source)
        self.assertNotIn('type: "chat.send"', source)
        self.assertNotIn('chat.getState', source)
        self.assertNotIn('portal.pull', source)

    def test_no_im_transport_or_pull_relay_remains(self):
        for path in (ROOT / "extension").glob("*.js"):
            source = path.read_text(encoding="utf-8")
            self.assertNotIn("im.v2.Chat.Message.send", source, path.name)
            self.assertNotIn("im.v2.Recent.load", source, path.name)
            self.assertNotIn("window.BX?.PULL", source, path.name)

    def test_side_panel_exposes_workspace_picker_and_selected_path(self):
        html = (ROOT / "extension" / "sidepanel.html").read_text(encoding="utf-8")
        source = (ROOT / "extension" / "sidepanel.js").read_text(encoding="utf-8")
        self.assertIn('id="choose-workspace"', html)
        self.assertIn('id="workspace-path"', html)
        self.assertIn('workspace.choose', source)
        self.assertIn('workspace.result.workspace', source)

    def test_native_host_no_longer_exposes_chat_endpoint(self):
        source = (ROOT / "native-host" / "marta_browser_host.py").read_text(encoding="utf-8")
        self.assertNotIn('"chat.send"', source)
        self.assertNotIn('/api/browser-bridge/chat', source)


if __name__ == "__main__":
    unittest.main()
