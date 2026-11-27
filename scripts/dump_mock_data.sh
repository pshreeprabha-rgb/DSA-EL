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

"$SERVER_BIN" --port "$PORT" --bind "$HOST" --data-dir "$ROOT_DIR/data" --seed 42 --paused &
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

kill -TERM "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""
echo "[mock] Mock data dump complete"

