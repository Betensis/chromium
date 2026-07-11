#!/usr/bin/env python3
"""Isolated, marker-scoped E2E harness for Marta tab management."""

import argparse
import http.server
import json
import pathlib
import subprocess
import threading
import time
import urllib.parse
import urllib.request
import uuid


EXTENSION_ID = "ldkkgojejbhlogpkhoclefempppipgmf"


def fixture_tab_ids(tabs, marker, origin):
    prefix = origin.rstrip("/") + "/"
    result = []
    for tab in tabs:
        url = str(tab.get("url", ""))
        parsed = urllib.parse.urlparse(url)
        query = urllib.parse.parse_qs(parsed.query)
        if url.startswith(prefix) and query.get("marta_e2e") == [marker]:
            result.append(tab["id"])
    return result


def evaluate_contract(pre, post, marker, origin):
    pre_tabs = {tab["id"]: tab for tab in pre["tabs"]}
    post_tabs = {tab["id"]: tab for tab in post["tabs"]}
    groups = {group["id"]: group for group in post["groups"]}
    checks = {}
    checks["preexisting_tabs_preserved"] = set(pre_tabs) <= set(post_tabs)

    marked = []
    for tab in post["tabs"]:
        parsed = urllib.parse.urlparse(str(tab.get("url", "")))
        query = urllib.parse.parse_qs(parsed.query)
        if str(tab.get("url", "")).startswith(origin.rstrip("/") + "/") and query.get("marta_e2e") == [marker]:
            marked.append((tab, query.get("kind", [""])[0]))
    by_kind = {}
    for tab, kind in marked:
        by_kind.setdefault(kind, []).append(tab)

    sentinel = by_kind.get("sentinel", [])
    checks["sentinel_preserved_ungrouped"] = len(sentinel) == 1 and sentinel[0].get("groupId", -1) == -1
    checks["one_mail_opened_ungrouped"] = len(by_kind.get("mail", [])) == 1 and by_kind["mail"][0].get("groupId", -1) == -1
    expected = {"search": ("Search", "blue"), "maps": ("Maps", "green")}
    used_group_ids = set()
    group_ok = True
    for kind, (title, color) in expected.items():
        tabs = by_kind.get(kind, [])
        ids = {tab.get("groupId", -1) for tab in tabs}
        if len(tabs) != 2 or len(ids) != 1 or -1 in ids:
            group_ok = False
            continue
        group_id = next(iter(ids))
        used_group_ids.add(group_id)
        group = groups.get(group_id, {})
        group_ok &= group.get("title") == title and group.get("color") == color and group.get("collapsed") is False
    checks["fixture_groups_exact"] = group_ok and len(used_group_ids) == 2
    checks["no_unexpected_marked_tabs"] = set(by_kind) == {"sentinel", "search", "maps", "mail"}
    new_ids = set(post_tabs) - set(pre_tabs)
    mail_ids = {tab["id"] for tab in by_kind.get("mail", [])}
    checks["no_unexpected_new_tabs"] = new_ids == mail_ids
    failed = [name for name, passed in checks.items() if not passed]
    return {"passed": not failed, "checks": checks, "failedChecks": failed}


class FixtureHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        marker = query.get("marta_e2e", ["missing"])[0]
        kind = query.get("kind", ["page"])[0]
        title = f"MARTA-E2E {kind.title()} {pathlib.PurePosixPath(parsed.path).name}"
        body = f"<!doctype html><title>{title}</title><h1>{title}</h1><p>run {marker}</p>".encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def http_json(url, method="GET"):
    request = urllib.request.Request(url, method=method)
    with urllib.request.urlopen(request, timeout=10) as response:
        raw = response.read()
        return json.loads(raw) if raw else {}


def new_tab(cdp, url):
    return http_json(cdp + "/json/new?" + urllib.parse.quote(url, safe=""), "PUT")


def close_target(cdp, target_id):
    request = urllib.request.Request(cdp + "/json/close/" + urllib.parse.quote(target_id, safe=""))
    with urllib.request.urlopen(request, timeout=10):
        pass


def cdp_eval(websocket_url, expression, timeout=15):
    node = r'''
const [wsUrl, expression, timeoutSeconds] = process.argv.slice(1);
const ws = new WebSocket(wsUrl);
const timer = setTimeout(() => { console.error("CDP timeout"); process.exit(2); }, Number(timeoutSeconds) * 1000);
ws.onopen = () => ws.send(JSON.stringify({id: 1, method: "Runtime.evaluate", params: {expression, awaitPromise: true, returnByValue: true}}));
ws.onmessage = event => {
  const msg = JSON.parse(event.data);
  if (msg.id !== 1) return;
  clearTimeout(timer);
  if (msg.error || msg.result?.exceptionDetails) { console.error(JSON.stringify(msg)); process.exit(3); }
  process.stdout.write(JSON.stringify(msg.result.result.value));
  ws.close();
};
ws.onerror = () => process.exit(4);
'''
    result = subprocess.run(["node", "-e", node, websocket_url, expression, str(timeout)], text=True, capture_output=True, timeout=timeout + 5)
    if result.returncode:
        raise RuntimeError(f"CDP Runtime.evaluate failed (exit {result.returncode}): {result.stderr.strip()}")
    return json.loads(result.stdout)


def extension_state(cdp):
    targets = http_json(cdp + "/json/list")
    prefix = f"chrome-extension://{EXTENSION_ID}/"
    worker = next((target for target in targets if target.get("type") == "service_worker" and target.get("url", "").startswith(prefix)), None)
    if not worker:
        raise RuntimeError("extension service worker is not visible in CDP")
    expression = "(async()=>({tabs:await chrome.tabs.query({}),groups:await chrome.tabGroups.query({})}))()"
    return cdp_eval(worker["webSocketDebuggerUrl"], expression)


def browser_thread_send(cdp, portal_origin, prompt, timeout):
    portal = portal_origin.rstrip("/")
    target = new_tab(cdp, portal + "/")
    page = None
    ready_deadline = time.time() + 30
    while time.time() < ready_deadline:
        targets = http_json(cdp + "/json/list")
        page = next((item for item in targets if item.get("id") == target.get("id")), None)
        if page and (page.get("url") == portal or page.get("url", "").startswith(portal + "/")):
            try:
                renderer = cdp_eval(page["webSocketDebuggerUrl"], "({origin:location.origin,ready:document.readyState})")
                if renderer.get("origin") == portal and renderer.get("ready") != "loading":
                    break
            except RuntimeError:
                pass
        time.sleep(1)
    else:
        raise RuntimeError(f"portal authentication did not return to {portal}; last target URL: {page and page.get('url')}")
    js = f'''(async()=>{{
const base={json.dumps(portal)}+"/bitrix/services/main/ajax.php?action=aiassistant.api.BrowserThread.";
const call=async(action,data={{}})=>{{const body=new URLSearchParams(); for(const [k,v] of Object.entries(data)) body.set(k,typeof v==="object"?JSON.stringify(v):String(v)); const r=await fetch(base+action,{{method:"POST",credentials:"include",headers:{{"Content-Type":"application/x-www-form-urlencoded;charset=UTF-8"}},body}}); const j=await r.json(); if(!r.ok||j.error||j.errors?.length) throw new Error(JSON.stringify(j)); return j;}};
const created=await call("create"), threadId=String(created.data?.threadId??created.threadId); if(!threadId||threadId==="undefined") throw new Error("no threadId");
const sent=await call("send",{{threadId,message:{json.dumps(prompt)}}}); const afterId=sent.data?.messageId??sent.messageId; const deadline=Date.now()+{int(timeout * 1000)};
while(Date.now()<deadline){{const query={{threadId}}; if(afterId!=null)query.afterId=afterId; const messages=await call("messages",query); const list=messages.data?.messages??messages.messages??[]; const answer=[...list].reverse().find(m=>["assistant","marta"].includes(String(m.role??m.authorType??"").toLowerCase())&&String(m.text??m.content??"").trim()); if(answer)return {{threadId,answer:String(answer.text??answer.content)}}; await new Promise(r=>setTimeout(r,1500));}}
throw new Error("Marta timeout");
}})()'''
    try:
        return cdp_eval(page["webSocketDebuggerUrl"], js, timeout + 15)
    finally:
        close_target(cdp, target["id"])


def prompt_for(marker, origin):
    return f"""Управляй вкладками через bitrix_browser. Сначала tabs_list. Работай ТОЛЬКО с вкладками URL на {origin} с параметром marta_e2e={marker}. Две вкладки kind=search сгруппируй в группу Search, color blue, collapsed false. Две вкладки kind=maps сгруппируй в группу Maps, color green, collapsed false. Открой ровно одну новую неактивную вкладку {origin}/mail?marta_e2e={marker}&kind=mail и не группируй её. Вкладку kind=sentinel не изменяй. Ничего не закрывай. В конце снова вызови tabs_list и кратко сообщи результат."""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--chromium-app", required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--cdp-port", type=int, required=True)
    parser.add_argument("--artifacts", required=True)
    parser.add_argument("--portal-origin")
    parser.add_argument("--auto-send", action="store_true")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    if args.auto_send and not args.portal_origin:
        parser.error("--auto-send requires --portal-origin")

    marker = str(uuid.uuid4())
    artifacts = pathlib.Path(args.artifacts).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FixtureHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{server.server_port}"
    cdp = f"http://127.0.0.1:{args.cdp_port}"
    root = pathlib.Path(__file__).parents[1]
    launched = False
    result = {"passed": False, "marker": marker, "origin": origin}
    try:
        subprocess.run([str(root / "scripts/start-isolated-chromium.sh"), "--chromium-app", args.chromium_app, "--user-data-dir", args.profile, "--cdp-port", str(args.cdp_port)], check=True)
        launched = True
        urls = [
            f"{origin}/sentinel?marta_e2e={marker}&kind=sentinel",
            f"{origin}/search/1?marta_e2e={marker}&kind=search",
            f"{origin}/search/2?marta_e2e={marker}&kind=search",
            f"{origin}/maps/1?marta_e2e={marker}&kind=maps",
            f"{origin}/maps/2?marta_e2e={marker}&kind=maps",
        ]
        for url in urls:
            new_tab(cdp, url)
        time.sleep(2)
        pre = extension_state(cdp)
        (artifacts / "prestate.json").write_text(json.dumps(pre, ensure_ascii=False, indent=2))
        prompt = prompt_for(marker, origin)
        (artifacts / "prompt.txt").write_text(prompt)
        if args.auto_send:
            response = browser_thread_send(cdp, args.portal_origin, prompt, args.timeout)
            (artifacts / "marta-response.json").write_text(json.dumps(response, ensure_ascii=False, indent=2))
        else:
            print(json.dumps({"phase": "prepared", "promptFile": str(artifacts / "prompt.txt"), "instruction": "Send prompt to Marta, then press Enter"}, ensure_ascii=False))
            input()
        time.sleep(2)
        post = extension_state(cdp)
        (artifacts / "poststate.json").write_text(json.dumps(post, ensure_ascii=False, indent=2))
        result.update(evaluate_contract(pre, post, marker, origin))
    except Exception as error:
        result["error"] = f"{type(error).__name__}: {error}"
    finally:
        try:
            if launched:
                state = extension_state(cdp)
                ids = fixture_tab_ids(state["tabs"], marker, origin)
                if ids:
                    targets = http_json(cdp + "/json/list")
                    prefix = f"chrome-extension://{EXTENSION_ID}/"
                    worker = next(target for target in targets if target.get("type") == "service_worker" and target.get("url", "").startswith(prefix))
                    cdp_eval(worker["webSocketDebuggerUrl"], f"chrome.tabs.remove({json.dumps(ids)}).then(()=>true)")
        except Exception as cleanup_error:
            result["cleanupError"] = str(cleanup_error)
            result["passed"] = False
        if launched:
            subprocess.run([str(root / "scripts/stop-isolated-chromium.sh"), "--user-data-dir", args.profile], check=False)
        server.shutdown()
        (artifacts / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2))
        print(json.dumps(result, ensure_ascii=False))
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    raise SystemExit(main())
