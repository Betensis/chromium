#!/bin/zsh
set -euo pipefail
[[ $# -eq 2 && "$1" == "--user-data-dir" ]] || { print -u2 "usage: $0 --user-data-dir DIR"; exit 2; }
rm -f "$2/NativeMessagingHosts/org.bitrix.marta_browser.json"
