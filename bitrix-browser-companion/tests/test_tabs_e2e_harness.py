import importlib.util
import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).parents[1] / "scripts" / "tabs-e2e-harness.py"
SPEC = importlib.util.spec_from_file_location("tabs_e2e_harness", SCRIPT)
HARNESS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HARNESS)


class TabsE2EHarnessSafetyTest(unittest.TestCase):
    def test_cleanup_selects_only_current_run_http_fixture_tabs(self):
        tabs = [
            {"id": 1, "url": "http://127.0.0.1:42100/search?marta_e2e=run-a&kind=search"},
            {"id": 2, "url": "http://127.0.0.1:42100/maps?marta_e2e=run-b&kind=maps"},
            {"id": 3, "url": "https://example.com/?marta_e2e=run-a"},
            {"id": 4, "url": "chrome://settings/"},
        ]

        self.assertEqual(
            HARNESS.fixture_tab_ids(tabs, "run-a", "http://127.0.0.1:42100"),
            [1],
        )

    def test_evaluate_requires_exact_groups_and_preserves_sentinel(self):
        marker = "run-a"
        origin = "http://127.0.0.1:42100"
        pre = {
            "tabs": [
                {"id": 10, "url": f"{origin}/sentinel?marta_e2e={marker}&kind=sentinel", "groupId": -1},
                {"id": 11, "url": f"{origin}/search/1?marta_e2e={marker}&kind=search", "groupId": -1},
                {"id": 12, "url": f"{origin}/search/2?marta_e2e={marker}&kind=search", "groupId": -1},
                {"id": 13, "url": f"{origin}/maps/1?marta_e2e={marker}&kind=maps", "groupId": -1},
                {"id": 14, "url": f"{origin}/maps/2?marta_e2e={marker}&kind=maps", "groupId": -1},
            ],
            "groups": [],
        }
        post = {
            "tabs": [
                {"id": 10, "url": pre["tabs"][0]["url"], "groupId": -1},
                {"id": 11, "url": pre["tabs"][1]["url"], "groupId": 20},
                {"id": 12, "url": pre["tabs"][2]["url"], "groupId": 20},
                {"id": 13, "url": pre["tabs"][3]["url"], "groupId": 21},
                {"id": 14, "url": pre["tabs"][4]["url"], "groupId": 21},
                {"id": 15, "url": f"{origin}/mail?marta_e2e={marker}&kind=mail", "groupId": -1},
            ],
            "groups": [
                {"id": 20, "title": "Search", "color": "blue", "collapsed": False},
                {"id": 21, "title": "Maps", "color": "green", "collapsed": False},
            ],
        }

        result = HARNESS.evaluate_contract(pre, post, marker, origin)

        self.assertTrue(result["passed"], result)

    def test_evaluate_fails_when_any_preexisting_tab_is_closed(self):
        origin = "http://127.0.0.1:42100"
        marker = "run-a"
        pre = {"tabs": [{"id": 99, "url": "https://untouched.invalid/", "groupId": -1}], "groups": []}
        post = {"tabs": [], "groups": []}

        result = HARNESS.evaluate_contract(pre, post, marker, origin)

        self.assertFalse(result["passed"])
        self.assertIn("preexisting_tabs_preserved", result["failedChecks"])

    def test_evaluate_fails_on_unexpected_new_tab(self):
        origin = "http://127.0.0.1:42100"
        marker = "run-a"
        pre = {"tabs": [{"id": 1, "url": "about:blank", "groupId": -1}], "groups": []}
        post = {"tabs": [*pre["tabs"], {"id": 2, "url": "https://unexpected.invalid/", "groupId": -1}], "groups": []}

        result = HARNESS.evaluate_contract(pre, post, marker, origin)

        self.assertFalse(result["checks"]["no_unexpected_new_tabs"])


if __name__ == "__main__":
    unittest.main()
