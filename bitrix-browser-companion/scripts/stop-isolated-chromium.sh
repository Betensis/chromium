#!/bin/zsh
set -euo pipefail

PROFILE=""
while (( $# )); do
  case "$1" in
    --user-data-dir) PROFILE="$2"; shift 2 ;;
    *) print -u2 -- "usage: $0 --user-data-dir DIR"; exit 2 ;;
  esac
done
[[ "$PROFILE" = /* && -d "$PROFILE" && "$PROFILE" != *$'\n'* && "$PROFILE" != *' '* ]] || { print -u2 -- "--user-data-dir must be an existing absolute path without whitespace"; exit 2; }
PROFILE="$(cd "$PROFILE" && pwd -P)"
PIDFILE="$PROFILE/.marta-browser-companion.pid"
[[ -f "$PIDFILE" ]] || { print -u2 -- "No pidfile for this isolated profile: $PIDFILE"; exit 1; }
PID="$(<"$PIDFILE")"
[[ "$PID" == <-> ]] || { print -u2 -- "Invalid pidfile; refusing to kill anything."; exit 1; }
COMMAND="$(ps -p "$PID" -o command= 2>/dev/null || true)"
[[ -n "$COMMAND" ]] || { rm -f "$PIDFILE"; print "Recorded process is already gone."; exit 0; }
[[ "$COMMAND" == *"--user-data-dir=$PROFILE"* && "$COMMAND" == *"/Contents/MacOS/"* && "$COMMAND" != *"--type="* ]] || {
  print -u2 -- "Recorded PID $PID does not match the isolated browser profile; refusing to kill it."
  exit 1
}

kill -TERM "$PID"
rm -f "$PIDFILE"
print "Sent TERM to isolated browser PID $PID. Profile was preserved: $PROFILE"
