#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec /usr/bin/python3 "$ROOT/native-host/marta_browser_host.py"
