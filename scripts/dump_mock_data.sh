#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PORT="${MOCK_PORT:-18081}"
HOST="127.0.0.1"
BASE_URL="http://${HOST}:${PORT}"
SERVER_BIN="${SERVER_BIN:-backend/build/transport_server}"
OUT_DIR="$ROOT_DIR/docs/mock"
SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

mkdir -p "$OUT_DIR"

"$SERVER_BIN" --port "$PORT" --bind "$HOST" --data-dir "$ROOT_DIR/data" --seed 42 --offline --paused &
SERVER_PID=$!

ready=0
for _ in $(seq 1 120); do
    if curl -sf "${BASE_URL}/api/health" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.5
done
[[ "$ready" -eq 1 ]] || { echo "Server failed to start" >&2; exit 1; }

fetch_pretty() {
    local url_path="$1"
    local out_file="$2"
    curl -sf "${BASE_URL}${url_path}" | python3 -m json.tool > "${OUT_DIR}/${out_file}"
    echo "[mock] wrote docs/mock/${out_file}"
}

post_json() {
    local url_path="$1"
    local payload="$2"
    curl -sf -X POST -H 'Content-Type: application/json' -d "$payload" "${BASE_URL}${url_path}" >/dev/null
}

# Initial paused (06:00) snapshots
fetch_pretty "/api/config" "config.json"
fetch_pretty "/api/graph" "graph.json"
fetch_pretty "/api/stops" "stops.json"
fetch_pretty "/api/routes" "routes.json"
fetch_pretty "/api/geometry" "geometry.json"
fetch_pretty "/api/state" "state.json"
fetch_pretty "/api/schedule" "schedule.json"
fetch_pretty "/api/tree" "tree.json"
fetch_pretty "/api/demand/forecast?stop=3" "forecast_stop3.json"
fetch_pretty "/api/metrics" "metrics.json"
fetch_pretty "/api/alerts" "alerts.json"
fetch_pretty "/api/history" "history.json"
fetch_pretty "/api/eval" "eval.json"

# Deterministic 08:00 state via seek
post_json "/api/sim/control" '{"action":"seek","time_min":480}'
fetch_pretty "/api/state" "state_at_08_00.json"

# Active state with blocked edge, anomaly alert, route optimize, and pending move
post_json "/api/scenario" '{"kind":"block_edge","route":0,"leg":0}'
post_json "/api/scenario" '{"kind":"event_on","stop":3}'
post_json "/api/inject" '{"stop":3,"count":250,"ride":1}'
post_json "/api/sim/control" '{"action":"start","speed":3600}'

for _ in $(seq 1 100); do
    clk="$(curl -sf "${BASE_URL}/api/state" | python3 -c "import json,sys; print(json.load(sys.stdin)['clock_min'])")"
    if python3 -c "import sys; sys.exit(0 if float('$clk') >= 496.0 else 1)"; then
        break
    fi
    sleep 0.05
done
post_json "/api/sim/control" '{"action":"pause"}'

# Pick a running bus with no pending move and assign a manual move
move_bus="$(curl -sf "${BASE_URL}/api/state" | python3 -c "
import json, sys
s = json.load(sys.stdin)
for b in s['buses']:
    if b['state'] == 'running' and b['pending_route'] == -2:
        print(b['id'], 1 if b['route'] != 1 else 0)
        break
")"
b_id="$(echo "$move_bus" | awk '{print $1}')"
t_rt="$(echo "$move_bus" | awk '{print $2}')"
post_json "/api/allocate" "{\"manual_move\":{\"bus\":${b_id},\"to_route\":${t_rt}}}"
curl -sf -X POST -H 'Content-Type: application/json' \
    -d '{"from_stop":0,"to_stop":5,"algo":"both","mode":"traffic"}' \
    "${BASE_URL}/api/route/optimize" | python3 -m json.tool > "${OUT_DIR}/optimize.json"
echo "[mock] wrote docs/mock/optimize.json"

fetch_pretty "/api/state" "state_active.json"
fetch_pretty "/api/alerts" "alerts_active.json"
fetch_pretty "/api/geometry" "geometry_blocked.json"
fetch_pretty "/api/history" "history_blocked.json"
fetch_pretty "/api/metrics" "metrics_active.json"

python3 -c "
import json
st = json.load(open('${OUT_DIR}/state_active.json'))
al = json.load(open('${OUT_DIR}/alerts_active.json'))
geo = json.load(open('${OUT_DIR}/geometry_blocked.json'))
hs = json.load(open('${OUT_DIR}/history_blocked.json'))
assert any(b['pending_route'] >= 0 for b in st['buses']), 'Expected pending move'
assert len(al['flags']) >= 1, 'Expected alert flag'
assert len(geo['blocked']) >= 1, 'Expected blocked edge'
assert hs['size'] >= 1, 'Expected history entry'
print('[mock] Verified active snapshot: pending_move=True, flags=%d, blocked=%d, history=%d' % (len(al['flags']), len(geo['blocked']), hs['size']))
"

kill -TERM "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""
echo "[mock] Mock data dump complete"
