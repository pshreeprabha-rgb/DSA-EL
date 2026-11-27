#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PORT="${SMOKE_PORT:-18080}"
HOST="${SMOKE_HOST:-127.0.0.1}"
BASE_URL="http://${HOST}:${PORT}"
SERVER_BIN="${SERVER_BIN:-backend/build/transport_server}"
USE_VALGRIND="${SMOKE_VALGRIND:-0}"

SERVER_PID=""
VG_LOG=""

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

if [[ ! -x "$SERVER_BIN" ]]; then
    echo "Server binary not found: $SERVER_BIN" >&2
    exit 1
fi

if [[ "$USE_VALGRIND" == "1" ]]; then
    VG_LOG="$(mktemp)"
    valgrind --leak-check=full --error-exitcode=1 \
        "$SERVER_BIN" --port "$PORT" --bind "$HOST" --data-dir "$ROOT_DIR/data" --speed 10 \
        >"$VG_LOG" 2>&1 &
    SERVER_PID=$!
    MAX_POLLS=230
else
    "$SERVER_BIN" --port "$PORT" --bind "$HOST" --data-dir "$ROOT_DIR/data" --paused &
    SERVER_PID=$!
    MAX_POLLS=120
fi

# Wait for /api/health
echo "[smoke] Waiting for server at $BASE_URL/api/health ..."
ready=0
for _ in $(seq 1 "$MAX_POLLS"); do
    if curl -sf "${BASE_URL}/api/health" >/dev/null 2>&1; then
        ready=1
        break
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "[smoke] Server exited prematurely!" >&2
        if [[ -n "$VG_LOG" && -f "$VG_LOG" ]]; then cat "$VG_LOG" >&2; rm -f "$VG_LOG"; fi
        exit 1
    fi
    sleep 0.5
done

if [[ "$ready" -ne 1 ]]; then
    echo "[smoke] Timed out waiting for /api/health" >&2
    if [[ -n "$VG_LOG" && -f "$VG_LOG" ]]; then tail -n 30 "$VG_LOG" >&2; rm -f "$VG_LOG"; fi
    exit 1
fi
echo "[smoke] Server is ready"

check_get() {
    local path="$1"
    local expr="$2"
    local tmp
    tmp="$(mktemp)"
    local code
    code="$(curl -sS -w '%{http_code}' -o "$tmp" "${BASE_URL}${path}")"
    if [[ "$code" != "200" ]]; then
        echo "[smoke] FAIL GET $path -> HTTP $code" >&2
        cat "$tmp" >&2
        rm -f "$tmp"
        exit 1
    fi
    python3 -c "
import json, sys
with open(sys.argv[1], 'r') as f:
    d = json.load(f)
assert ($expr), f'Assertion failed for {sys.argv[2]}'
" "$tmp" "$path"
    echo "[smoke] PASS GET $path (200)"
    rm -f "$tmp"
}

check_get "/api/health" "d['ok'] is True and 'sim_running' in d"
check_get "/api/config" "d['stops'] == 60 and d['routes'] == 6 and d['buses'] == 36 and d['seed'] == 42"
check_get "/api/graph" "len(d['nodes']) > 100 and len(d['edges']) > 100"
check_get "/api/stops" "len(d['stops']) == 60 and 'route' in d['stops'][0]"
check_get "/api/routes" "len(d['routes']) == 6 and len(d['board']) == 6"
check_get "/api/geometry" "d['version'] >= 1 and len(d['legs']) == 60"
check_get "/api/state" "len(d['buses']) == 36 and len(d['queues']) == 60 and 'summary' in d"
check_get "/api/bus/0" "d['id'] == 0 and 'capacity' in d and 'journeys' in d"
check_get "/api/schedule?from=360&to=480" "d['count'] > 0 and d['avl']['balanced'] is True"
check_get "/api/tree" "d['count'] >= 45 and d['height'] == 3"
check_get "/api/demand/forecast?stop=3" "d['stop'] == 3 and len(d['hours']) == 24"
check_get "/api/metrics" "'arrived' in d['sim'] and len(d['routes']) == 6"
check_get "/api/alerts" "'flags' in d and 'log' in d"
check_get "/api/history" "d['size'] == 0 and d['entries'] == []"

# POST /api/sim/control
ctrl_tmp="$(mktemp)"
ctrl_code="$(curl -sS -w '%{http_code}' -o "$ctrl_tmp" -X POST \
    -H 'Content-Type: application/json' \
    -d '{"action":"start","speed":120}' \
    "${BASE_URL}/api/sim/control")"
[[ "$ctrl_code" == "200" ]] || { echo "[smoke] FAIL POST /api/sim/control -> $ctrl_code"; exit 1; }
python3 -c "import json, sys; d=json.load(open(sys.argv[1])); assert d['ok'] is True and d['running'] is True and d['speed'] == 120" "$ctrl_tmp"
rm -f "$ctrl_tmp"
echo "[smoke] PASS POST /api/sim/control (200)"

ctrl_pause="$(curl -sS -w '%{http_code}' -o /dev/null -X POST \
    -H 'Content-Type: application/json' -d '{"action":"pause"}' "${BASE_URL}/api/sim/control")"
[[ "$ctrl_pause" == "200" ]] || { echo "[smoke] FAIL POST pause -> $ctrl_pause"; exit 1; }

# OPTIONS preflight headers check
opt_hdrs="$(mktemp)"
opt_code="$(curl -sS -D "$opt_hdrs" -o /dev/null -w '%{http_code}' -X OPTIONS "${BASE_URL}/api/state")"
[[ "$opt_code" == "204" ]] || { echo "[smoke] FAIL OPTIONS -> $opt_code"; rm -f "$opt_hdrs"; exit 1; }
grep -qi "Access-Control-Allow-Origin:" "$opt_hdrs"
grep -qi "Access-Control-Allow-Methods:.*GET.*POST.*OPTIONS" "$opt_hdrs"
grep -qi "Access-Control-Allow-Headers:.*Content-Type" "$opt_hdrs"
grep -qi "Access-Control-Max-Age: 600" "$opt_hdrs"
grep -qi "Cache-Control: no-store" "$opt_hdrs"
rm -f "$opt_hdrs"
echo "[smoke] PASS OPTIONS /api/state (204 + CORS headers)"

# 404 unknown path
code_404="$(curl -sS -o /dev/null -w '%{http_code}' "${BASE_URL}/api/does_not_exist")"
[[ "$code_404" == "404" ]] || { echo "[smoke] FAIL 404 test -> $code_404"; exit 1; }
echo "[smoke] PASS unknown path -> 404"

# 405 wrong method
hdr_405="$(mktemp)"
code_405="$(curl -sS -D "$hdr_405" -o /dev/null -w '%{http_code}' -X POST "${BASE_URL}/api/health")"
[[ "$code_405" == "405" ]] || { echo "[smoke] FAIL 405 test -> $code_405"; rm -f "$hdr_405"; exit 1; }
grep -qi "^Allow:" "$hdr_405"
rm -f "$hdr_405"
echo "[smoke] PASS wrong method -> 405 + Allow"

# 413 70 KB body
big_body="$(mktemp)"
python3 -c "import sys; sys.stdout.write('{\"action\":\"pause\",\"pad\":\"' + ('x'*71680) + '\"}')" > "$big_body"
code_413="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' --data-binary @"$big_body" "${BASE_URL}/api/sim/control")"
rm -f "$big_body"
[[ "$code_413" == "413" ]] || { echo "[smoke] FAIL 413 test -> $code_413"; exit 1; }
echo "[smoke] PASS 70 KB body -> 413"

# 400 bad id
code_400="$(curl -sS -o /dev/null -w '%{http_code}' "${BASE_URL}/api/bus/notanumber")"
[[ "$code_400" == "400" ]] || { echo "[smoke] FAIL 400 test -> $code_400"; exit 1; }
echo "[smoke] PASS bad id -> 400"

# Clean shutdown & capture Valgrind status if active
kill -TERM "$SERVER_PID"
set +e
wait "$SERVER_PID"
vg_rc=$?
set -e
SERVER_PID=""

if [[ "$USE_VALGRIND" == "1" ]]; then
    echo "=== VALGRIND SERVER SUMMARY (exit=$vg_rc) ==="
    grep -E "in use at exit|All heap blocks|ERROR SUMMARY|Command:" "$VG_LOG" || cat "$VG_LOG"
    rm -f "$VG_LOG"
    exit "$vg_rc"
fi

echo "[smoke] ALL SMOKE CHECKS PASSED"
