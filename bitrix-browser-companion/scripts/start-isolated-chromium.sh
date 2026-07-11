#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$0"
APP=""
PROFILE=""
PORT=""
EXTENSION_DIR="$ROOT/extension"
WAIT_SECONDS=30

usage() {
  print -u2 -- "usage: $SCRIPT --chromium-app Chromium.app --user-data-dir DIR --cdp-port PORT [--extension-dir DIR] [--wait-seconds N]"
  exit 2
}

while (( $# )); do
  case "$1" in
    --chromium-app) APP="$2"; shift 2 ;;
    --user-data-dir) PROFILE="$2"; shift 2 ;;
    --cdp-port) PORT="$2"; shift 2 ;;
    --extension-dir) EXTENSION_DIR="$2"; shift 2 ;;
    --wait-seconds) WAIT_SECONDS="$2"; shift 2 ;;
    *) usage ;;
  esac
done

[[ "$(uname -s)" == "Darwin" ]] || { print -u2 -- "This launcher is macOS-only (uses open -na)."; exit 2; }
[[ -n "$APP" && -n "$PROFILE" && -n "$PORT" ]] || usage
[[ "$PORT" == <-> && "$PORT" -ge 1024 && "$PORT" -le 65535 ]] || { print -u2 -- "--cdp-port must be 1024..65535"; exit 2; }
[[ "$WAIT_SECONDS" == <-> && "$WAIT_SECONDS" -gt 0 ]] || { print -u2 -- "--wait-seconds must be positive"; exit 2; }
[[ -d "$APP" ]] || { print -u2 -- "--chromium-app must be a browser .app bundle"; exit 2; }
[[ -d "$EXTENSION_DIR" && -f "$EXTENSION_DIR/manifest.json" ]] || { print -u2 -- "--extension-dir must contain manifest.json"; exit 2; }
[[ "$PROFILE" = /* && "$PROFILE" != *$'\n'* && "$PROFILE" != *' '* ]] || { print -u2 -- "--user-data-dir must be an absolute path without whitespace"; exit 2; }

mkdir -p "$PROFILE"
PROFILE="$(cd "$PROFILE" && pwd -P)"
APP="$(cd "$APP" && pwd -P)"
APP_NAME="$(basename "$APP" .app)"
APP_BINARY="$APP/Contents/MacOS/$APP_NAME"
[[ -x "$APP_BINARY" ]] || { print -u2 -- "browser executable is missing: $APP_BINARY"; exit 2; }
EXTENSION_DIR="$(cd "$EXTENSION_DIR" && pwd -P)"
PIDFILE="$PROFILE/.marta-browser-companion.pid"
[[ ! -e "$PIDFILE" ]] || { print -u2 -- "Refusing to replace existing pidfile: $PIDFILE"; exit 1; }

# The unpacked companion has a stable ID because its manifest has a fixed key.
# Pin its action before Chromium reads the profile so the Marta button is always
# visible in the upper-right toolbar of a demo profile, rather than hidden in
# the extensions menu. Preserve any pre-existing profile preferences.
/usr/bin/python3 - "$PROFILE" <<'PY'
import json
import pathlib
import sys

profile = pathlib.Path(sys.argv[1]) / "Default"
profile.mkdir(parents=True, exist_ok=True)
preferences = profile / "Preferences"
try:
    data = json.loads(preferences.read_text(encoding="utf-8")) if preferences.exists() else {}
except (OSError, json.JSONDecodeError):
    data = {}
extensions = data.setdefault("extensions", {})
pinned = extensions.setdefault("pinned_extensions", [])
extension_id = "ldkkgojejbhlogpkhoclefempppipgmf"
if extension_id not in pinned:
    pinned.append(extension_id)
preferences.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
PY

if /usr/sbin/lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  print -u2 -- "CDP port $PORT is already listening; choose another port."
  exit 1
fi

"$ROOT/scripts/install-native-host.sh" --user-data-dir "$PROFILE" --extension-dir "$EXTENSION_DIR"

open -na "$APP" --args \
  "--user-data-dir=$PROFILE" \
  "--remote-debugging-address=127.0.0.1" \
  "--remote-debugging-port=$PORT" \
  "--no-first-run" \
  "--no-default-browser-check" \
  "--disable-features=SidePanelFlyoverAnimation" \
  "--load-extension=$EXTENSION_DIR"

deadline=$(( SECONDS + WAIT_SECONDS ))
until /usr/bin/curl -fsS --max-time 1 "http://127.0.0.1:$PORT/json/version" | /usr/bin/python3 -c 'import json, sys; assert json.load(sys.stdin)["webSocketDebuggerUrl"]' >/dev/null 2>&1; do
  (( SECONDS < deadline )) || { print -u2 -- "Timed out waiting for CDP on 127.0.0.1:$PORT; no pidfile was written."; exit 1; }
  sleep 1
done

PID="$(/usr/bin/python3 - "$PROFILE" "$APP_BINARY" <<'PY'
import subprocess, sys
profile, app_binary = sys.argv[1:]
rows = subprocess.check_output(["ps", "-axo", "pid=,command="], text=True).splitlines()
needle = "--user-data-dir=" + profile
matches = []
for row in rows:
    fields = row.strip().split(None, 1)
    if len(fields) == 2 and needle in fields[1] and app_binary in fields[1] and "--type=" not in fields[1]:
        matches.append(fields[0])
if len(matches) != 1:
    raise SystemExit("expected exactly one browser process for profile, found %d" % len(matches))
print(matches[0])
PY
)" || { print -u2 -- "CDP is ready but the isolated browser PID could not be identified; refusing to create pidfile."; exit 1; }

print -r -- "$PID" > "$PIDFILE"
print "Browser ready: profile=$PROFILE cdp=http://127.0.0.1:$PORT pid=$PID"
