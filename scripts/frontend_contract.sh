#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PORT="${CONTRACT_PORT:-18080}"
HOST="${CONTRACT_HOST:-127.0.0.1}"
BASE_URL="http://${HOST}:${PORT}"
SERVER_BIN="${SERVER_BIN:-backend/build/transport_server}"

SERVER_PID=""
SRV_LOG="$(mktemp)"

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SRV_LOG"
}
trap cleanup EXIT

if [[ ! -x "$SERVER_BIN" ]]; then
    echo "Server binary not found: $SERVER_BIN" >&2
    exit 1
fi

"$SERVER_BIN" \
    --port "$PORT" \
    --bind "$HOST" \
    --data-dir "$ROOT_DIR/data" \
    --seed 42 \
    --paused \
    --offline \
    >"$SRV_LOG" 2>&1 &
SERVER_PID=$!

ready=0
for _ in $(seq 1 120); do
    if curl -sf "${BASE_URL}/api/health" >/dev/null 2>&1; then
        ready=1
        break
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "transport_server exited prematurely" >&2
        cat "$SRV_LOG" >&2
        exit 1
    fi
    sleep 0.25
done

if [[ "$ready" -ne 1 ]]; then
    echo "Timed out waiting for transport_server at ${BASE_URL}" >&2
    tail -n 30 "$SRV_LOG" >&2
    exit 1
fi

timeout 120 node "$ROOT_DIR/frontend/scripts/contract_check.mjs" "$BASE_URL"

