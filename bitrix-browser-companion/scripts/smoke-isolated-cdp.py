#!/usr/bin/env python3
"""Non-destructive CDP smoke for an already-running isolated Chromium."""
import argparse
import json
import sys
import urllib.parse
import urllib.request


FIXTURE_TAG = "marta-browser-companion-smoke"


def request(url, method="GET"):
    req = urllib.request.Request(url, method=method)
    with urllib.request.urlopen(req, timeout=5) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cdp-port", type=int, required=True)
    parser.add_argument("--extension-id", required=True)
    args = parser.parse_args()
    base = f"http://127.0.0.1:{args.cdp_port}"
    targets = request(base + "/json/list")
    extension_prefix = f"chrome-extension://{args.extension_id}/"
    if not any(t.get("type") == "service_worker" and t.get("url", "").startswith(extension_prefix) for t in targets):
        raise RuntimeError("extension service worker is not visible in CDP")

    fixture_url = "data:text/html," + urllib.parse.quote(
        f"<!doctype html><title>{FIXTURE_TAG}</title><p>{FIXTURE_TAG}</p>", safe=""
    )
    created = request(base + "/json/new?" + urllib.parse.quote(fixture_url, safe=""), method="PUT")
    target_id = created.get("id")
    if not target_id or FIXTURE_TAG not in created.get("url", ""):
        raise RuntimeError("CDP did not create the tagged fixture page")
    current = {t.get("id"): t for t in request(base + "/json/list")}.get(target_id, {})
    if FIXTURE_TAG not in current.get("url", ""):
        raise RuntimeError("refusing to close a page that is no longer our tagged fixture")
    request(base + "/json/close/" + urllib.parse.quote(target_id, safe=""))
    print("CDP smoke passed: extension service worker found; tagged fixture page opened and closed.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"CDP smoke failed: {error}", file=sys.stderr)
        raise SystemExit(1)
