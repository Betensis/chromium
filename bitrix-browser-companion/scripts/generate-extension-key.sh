#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$ROOT/extension/manifest.json" <<'PY'
import base64, hashlib, json, sys
key = json.load(open(sys.argv[1], encoding="utf-8"))["key"]
digest = hashlib.sha256(base64.b64decode(key)).digest()[:16]
print("".join(chr(ord("a") + (byte >> shift & 15)) for byte in digest for shift in (4, 0)))
PY
