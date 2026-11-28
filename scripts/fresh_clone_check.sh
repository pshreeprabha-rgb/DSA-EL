#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLONE_DIR="$(mktemp -d)"

cleanup() {
    rm -rf "$CLONE_DIR"
}
trap cleanup EXIT

run_check() {
    echo "[fresh-clone] Cloning $ROOT_DIR -> $CLONE_DIR"
    git clone "$ROOT_DIR" "$CLONE_DIR"

    echo "[fresh-clone] Creating fake .env from backend/.env.example"
    sed 's/=.*/=fake_fresh_clone_key/' "$CLONE_DIR/backend/.env.example" > "$CLONE_DIR/backend/.env"

    echo "[fresh-clone] Running backend make test"
    make -C "$CLONE_DIR/backend" test

    echo "[fresh-clone] Building backend cli"
    make -C "$CLONE_DIR/backend" cli

    echo "[fresh-clone] Running CLI command 1/3: demo"
    (cd "$CLONE_DIR/backend" && ./build/transport_cli demo)

    echo "[fresh-clone] Running CLI command 2/3: bench --synthetic"
    (cd "$CLONE_DIR/backend" && ./build/transport_cli bench --synthetic --pairs 50 --seed 42 --hour 9)

    echo "[fresh-clone] Running CLI command 3/3: traffic --offline"
    (cd "$CLONE_DIR/backend" && ./build/transport_cli traffic --offline)

    echo "[fresh-clone] Running golden-check"
    (cd "$CLONE_DIR/backend" && ./build/transport_cli golden-check --model ../data/demand_rf.txt --golden ../data/golden_predictions.csv)

    echo "[fresh-clone] Building backend server and running api_smoke.sh (--offline)"
    make -C "$CLONE_DIR/backend" server
    (cd "$CLONE_DIR" && timeout 180 bash scripts/api_smoke.sh)

    if command -v node >/dev/null 2>&1 && command -v npm >/dev/null 2>&1; then
        echo "[fresh-clone] Running frontend npm ci, npm test, npm run build"
        (cd "$CLONE_DIR/frontend" && npm ci --prefer-offline --no-audit --no-fund && npm test && npm run build)
    else
        echo "[fresh-clone] SKIP frontend: node/npm not found"
    fi

    echo "[fresh-clone] ALL FRESH CLONE CHECKS PASSED"
}

export -f run_check
export ROOT_DIR CLONE_DIR
timeout 1800 bash -c 'run_check'

