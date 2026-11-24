#!/usr/bin/env bash
# Smoke test of the Standalone app: it must start, keep running for a few seconds
# (audio device / UI initialisation) and terminate cleanly when asked.
# Usage: standalone_smoke.sh <path to executable> [seconds]
set -u
APP="$1"
SECONDS_ALIVE="${2:-5}"
LOG="$(mktemp)"
"$APP" > "$LOG" 2>&1 &
PID=$!
sleep "$SECONDS_ALIVE"
if ! kill -0 "$PID" 2>/dev/null; then
  wait "$PID"; CODE=$?
  echo "Standalone exited early with code $CODE"; cat "$LOG"; exit 1
fi
kill -TERM "$PID"
for _ in $(seq 1 50); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$PID" 2>/dev/null; then
  echo "Standalone did not terminate on SIGTERM"; kill -KILL "$PID"; exit 1
fi
echo "Standalone smoke test OK (ran ${SECONDS_ALIVE}s, terminated cleanly)"
