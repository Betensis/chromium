# Marta Browser Companion

Local-only MVP companion for Marta: an unpacked MV3 extension plus a Python Native Messaging host. It controls tabs and observes only the selected `http`/`https` page; it never exports cookies, storage, DOM HTML, or browser credentials.

The chat is a separate browser conversation. The Side Panel calls the authenticated
Bitrix Ajax actions `aiassistant.api.BrowserThread.create/send/messages` with the
portal cookie; it does not call Bitrix IM and does not use the Native Messaging
host for chat. The portal owns the thread and makes the server-to-server
`POST /browser-chat` request to Marta. Page URL/title/snapshot and the current
JPEG screenshot travel in that request, so the browser never receives a Marta
signer or internal bridge secret.

## Local configuration

Copy `config/config.example.json` to ignored `config/config.local.json`, set the local Marta URL, browser token, portal identity and an existing absolute workspace. This initial workspace is the fallback at host startup. In the side panel, **Выбрать рабочую папку** opens the native macOS folder chooser through the Native Messaging host; the selected directory immediately becomes the live root for `files.*` and the default `shell.run` cwd. On non-macOS the host returns a structured unsupported-platform error. The workspace restriction applies to file tools only: `shell.run` is intentionally unsandboxed for the hackathon MVP, although its cwd starts in that workspace and the host removes browser-token environment variables from children.

## Install and smoke

```zsh
cd /Users/byankin/proj/хакатон/browser-companion
/usr/bin/python3 -m unittest discover -s tests -v
/usr/bin/python3 scripts/smoke-native-protocol.py
PROFILE=/tmp/marta-browser-spike
rm -rf "$PROFILE"
./scripts/install-native-host.sh --user-data-dir "$PROFILE" --extension-dir "$PWD/extension"
ID=$(./scripts/generate-extension-key.sh)
"/Users/byankin/proj/chromium-hackathon/vsevolod/src/out/mac-arm64/Bitrix Browser.app/Contents/MacOS/Bitrix Browser" \
  --user-data-dir="$PROFILE" --no-first-run --load-extension="$PWD/extension" \
  "chrome-extension://$ID/smoke.html?nativeSmoke=true"
```

`smoke-native-protocol.py` validates framing only. For the actual Native Messaging smoke, inspect `chrome.storage.session.get('nativeSmokeResult')` in the extension service-worker console: it must contain `ok: true` and the same UUID as `nativeSmokeRequestId`. The installer writes the precise generated extension origin to `$PROFILE/NativeMessagingHosts/org.bitrix.marta_browser.json`.

## Isolated Chromium automation

The helpers below are intentionally limited to an explicit, separate profile.
They never remove that profile. `start-isolated-chromium.sh` refuses an occupied
CDP port or an existing companion pidfile, installs the Native Messaging manifest
inside that profile, waits for CDP, and records only the browser process whose
command line names that exact profile. The stop helper sends `TERM` only after
re-checking the recorded PID and profile in its command line.

The final demo runs entirely on the user's Mac: the current local `Bitrix
Browser.app` artifact, local Marta, and the local portal. Use a persistent
isolated profile outside `/tmp`; it preserves the authenticated Bitrix24 Network
session across browser restarts. Do not delete this profile between rehearsals.
On the first run, sign in manually with the current credentials supplied by the
demo owner. Never put those credentials in tracked files, command arguments,
shell history, screenshots, or smoke-test output. The profile itself contains
session material and must remain local and untracked.

```zsh
PROFILE="$HOME/Library/Application Support/Bitrix Browser/Marta Demo Profile"
APP="/Users/byankin/proj/chromium-hackathon/vsevolod/src/out/mac-arm64/Bitrix Browser.app"
PORT=19222
./scripts/start-isolated-chromium.sh \
  --chromium-app "$APP" --user-data-dir "$PROFILE" --cdp-port "$PORT"
./scripts/smoke-isolated-cdp.py \
  --cdp-port "$PORT" --extension-id "$(./scripts/generate-extension-key.sh)"
./scripts/stop-isolated-chromium.sh --user-data-dir "$PROFILE"
```

The CDP smoke requires the extension service worker to be visible, creates one
`data:` fixture page tagged `marta-browser-companion-smoke`, and closes only that
same page. It does not inspect, close, or modify any other browser tabs.

The browser-control MCP bridge remains separate from chat: the Native Messaging
host polls the authenticated command endpoint only for tab/file/shell tools. A
browser-originated chat request never enters that queue and never creates an IM
message.

## Browser command contract

The extension receives dotted methods from the native host: `tabs.list`, `tabs.open`, `tabs.close`, `tabs.group`, `page.screenshot`, and `page.snapshot`. Screenshot sequence is activate/focus → verify active ID/URL → visible capture → reverify; any mismatch returns `STALE_PAGE`. It JPEG-encodes at quality `0.82`, limits the long side to 1920 px, and uses a conservative 700 KiB cap so its base64 result always fits the 1 MiB Native Messaging frame boundary (the Marta API independently accepts up to 4 MiB). Snapshot values are capped at 4 KiB and total result at 256 KiB.

The host executes `files.list/read/write/delete` and `shell.run`, then long-polls `/api/browser-bridge/poll` and posts results/captures to the corresponding Marta endpoints. It prints native protocol frames only to stdout and diagnostics only to stderr.
