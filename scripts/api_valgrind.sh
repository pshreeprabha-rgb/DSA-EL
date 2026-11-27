#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PORT="${VG_PORT:-18083}"
HOST="127.0.0.1"
BASE_URL="http://${HOST}:${PORT}"
SERVER_BIN="${SERVER_BIN:-backend/build/transport_server}"

SERVER_PID=""
VG_LOG="$(mktemp)"

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$VG_LOG"
}
trap cleanup EXIT

if [[ ! -x "$SERVER_BIN" ]]; then
    echo "Server binary not found: $SERVER_BIN" >&2
    exit 1
fi

valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
    "$SERVER_BIN" --port "$PORT" --bind "$HOST" --data-dir "$ROOT_DIR/data" --offline --speed 5 \
    >"$VG_LOG" 2>&1 &
SERVER_PID=$!

echo "[api_valgrind] Waiting for server at $BASE_URL/api/health ..."
ready=0
for _ in $(seq 1 240); do
    if curl -sf "${BASE_URL}/api/health" >/dev/null 2>&1; then
        ready=1
        break
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "[api_valgrind] Server exited prematurely!" >&2
        cat "$VG_LOG" >&2
        exit 1
    fi
    sleep 0.5
done
[[ "$ready" -eq 1 ]] || { echo "[api_valgrind] Timed out waiting for /api/health" >&2; tail -n 30 "$VG_LOG" >&2; exit 1; }
echo "[api_valgrind] Server ready"

# Quick GET + POST + eval checks against running Valgrind server
for ep in /api/health /api/config /api/stops /api/routes /api/geometry /api/state /api/bus/0 /api/schedule /api/tree "/api/demand/forecast?stop=3" /api/metrics /api/alerts /api/history /api/eval; do
    c="$(curl -sS -o /dev/null -w '%{http_code}' "${BASE_URL}${ep}")"
    [[ "$c" == "200" ]] || { echo "[api_valgrind] GET $ep failed ($c)" >&2; exit 1; }
done
echo "[api_valgrind] GET endpoints PASS (including /api/eval)"

c_opt="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"from_stop":0,"to_stop":5,"algo":"both","mode":"traffic"}' "${BASE_URL}/api/route/optimize")"
[[ "$c_opt" == "200" ]] || { echo "[api_valgrind] POST /api/route/optimize failed ($c_opt)" >&2; exit 1; }

# Run 200 mixed GET/POST stress requests while server runs
STRESS_EXTERNAL=1 STRESS_PORT="$PORT" STRESS_COUNT=200 bash scripts/api_stress.sh

# Clean SIGTERM shutdown & verify Valgrind exit status
kill -TERM "$SERVER_PID"
set +e
wait "$SERVER_PID"
vg_rc=$?
set -e
SERVER_PID=""

echo "=== API_VALGRIND SUMMARY (exit=$vg_rc) ==="
grep -E "Command:|in use at exit|All heap blocks|ERROR SUMMARY" "$VG_LOG" || cat "$VG_LOG"
exit "$vg_rc"

