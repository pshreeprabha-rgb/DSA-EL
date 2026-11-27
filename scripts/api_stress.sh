#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PORT="${STRESS_PORT:-18082}"
HOST="${STRESS_HOST:-127.0.0.1}"
BASE_URL="http://${HOST}:${PORT}"
SERVER_BIN="${SERVER_BIN:-backend/build/transport_server}"
STRESS_COUNT="${STRESS_COUNT:-1500}"
STRESS_SPEED="${STRESS_SPEED:-600}"
EXTERNAL="${STRESS_EXTERNAL:-0}"

SERVER_PID=""
cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

if [[ "$EXTERNAL" != "1" ]]; then
    if [[ ! -x "$SERVER_BIN" ]]; then
        echo "Server binary not found: $SERVER_BIN" >&2
        exit 1
    fi
    "$SERVER_BIN" --port "$PORT" --bind "$HOST" --data-dir "$ROOT_DIR/data" --offline --speed "$STRESS_SPEED" &
    SERVER_PID=$!

    ready=0
    for _ in $(seq 1 120); do
        if curl -sf "${BASE_URL}/api/health" >/dev/null 2>&1; then
            ready=1
            break
        fi
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "[stress] Server exited prematurely!" >&2
            exit 1
        fi
        sleep 0.5
    done
    [[ "$ready" -eq 1 ]] || { echo "[stress] Timed out waiting for /api/health" >&2; exit 1; }
fi

echo "[stress] Running $STRESS_COUNT mixed requests across 8 workers against $BASE_URL ..."

worker_cmd() {
    local idx="$1"
    local base="$2"
    local mod=$((idx % 12))
    local code="000"
    case "$mod" in
        0) code="$(curl -sS -o /dev/null -w '%{http_code}' "${base}/api/health")" ;;
        1) code="$(curl -sS -o /dev/null -w '%{http_code}' "${base}/api/state")" ;;
        2) code="$(curl -sS -o /dev/null -w '%{http_code}' "${base}/api/routes")" ;;
        3) code="$(curl -sS -o /dev/null -w '%{http_code}' "${base}/api/geometry")" ;;
        4) code="$(curl -sS -o /dev/null -w '%{http_code}' "${base}/api/metrics")" ;;
        5) code="$(curl -sS -o /dev/null -w '%{http_code}' "${base}/api/alerts")" ;;
        6) code="$(curl -sS -o /dev/null -w '%{http_code}' "${base}/api/schedule?from=360&to=480")" ;;
        7) code="$(curl -sS -o /dev/null -w '%{http_code}' "${base}/api/tree")" ;;
        8) code="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"stop":3,"count":5,"ride":1}' "${base}/api/inject")" ;;
        9) code="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"kind":"block_edge","route":0,"leg":0}' "${base}/api/scenario")" ;;
        10) code="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"policy":"greedy_spec","predictor":"ma","force":false}' "${base}/api/allocate")" ;;
        11) code="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{}' "${base}/api/undo")" ;;
    esac
    if [[ "$code" == "000" ]] || [[ "$code" =~ ^5 ]]; then
        echo "BAD:$idx:$mod:$code"
    else
        echo "OK:$code"
    fi
}
export -f worker_cmd

results_file="$(mktemp)"
seq 1 "$STRESS_COUNT" | xargs -P 8 -I {} bash -c 'worker_cmd "$1" "$2"' _ {} "$BASE_URL" > "$results_file"

bad_count="$(grep -c '^BAD:' "$results_file" || true)"
echo "[stress] Status distribution:"
sort "$results_file" | uniq -c

if [[ "$bad_count" -gt 0 ]]; then
    echo "[stress] FAILED: $bad_count server/transport errors:" >&2
    grep '^BAD:' "$results_file" | head -n 20 >&2
    rm -f "$results_file"
    exit 1
fi
rm -f "$results_file"

# Confirm server still healthy
health_code="$(curl -sS -o /dev/null -w '%{http_code}' "${BASE_URL}/api/health")"
[[ "$health_code" == "200" ]] || { echo "[stress] Final health check failed -> $health_code" >&2; exit 1; }
echo "[stress] Final health check PASS (200)"

if [[ -n "$SERVER_PID" ]]; then
    kill -TERM "$SERVER_PID"
    wait "$SERVER_PID"
    SERVER_PID=""
fi
echo "[stress] ALL $STRESS_COUNT REQUESTS COMPLETED WITH 0 5xx ERRORS"

