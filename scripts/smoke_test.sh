#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="19001"
LOG="$(mktemp)"
trap 'kill "$SERVER_PID" 2>/dev/null || true; rm -f "$LOG"' EXIT

"$ROOT/build/observe" "$ROOT/www" "$PORT" >"$LOG" 2>&1 &
SERVER_PID=$!
sleep 0.3

"$ROOT/build/bcurl" "localhost:${PORT}/index.html" > /tmp/cn-smoke-body.txt
if ! grep -q "Hello from the binary networking project" /tmp/cn-smoke-body.txt; then
  echo "smoke test failed: expected body not found"
  exit 1
fi

if "$ROOT/build/bcurl" "localhost:${PORT}/missing.txt" >/tmp/cn-smoke-missing.txt 2>/tmp/cn-smoke-missing.err; then
  echo "smoke test failed: missing file unexpectedly returned success"
  exit 1
fi

echo "smoke test: PASS"
