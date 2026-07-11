#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROFILE=""
EXTENSION_DIR="$ROOT/extension"
while (( $# )); do
  case "$1" in
    --user-data-dir) PROFILE="$2"; shift 2 ;;
    --extension-dir) EXTENSION_DIR="$2"; shift 2 ;;
    *) print -u2 "usage: $0 --user-data-dir DIR [--extension-dir DIR]"; exit 2 ;;
  esac
done
[[ -n "$PROFILE" ]] || { print -u2 "--user-data-dir is required"; exit 2; }
ID="$($ROOT/scripts/generate-extension-key.sh)"
DEST="$PROFILE/NativeMessagingHosts"
mkdir -p "$DEST"
python3 - "$DEST/org.bitrix.marta_browser.json" "$ROOT/native-host/host-launcher.sh" "$ID" <<'PY'
import json, os, sys
path, launcher, extension_id = sys.argv[1:]
with open(path, "w", encoding="utf-8") as out:
    json.dump({"name": "org.bitrix.marta_browser", "description": "Marta Browser Companion", "path": os.path.abspath(launcher), "type": "stdio", "allowed_origins": ["chrome-extension://%s/" % extension_id]}, out, indent=2)
    out.write("\n")
PY
print "Installed profile-local Native Messaging manifest: $DEST/org.bitrix.marta_browser.json"
print "Expected extension ID: $ID"
