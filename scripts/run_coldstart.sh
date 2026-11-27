#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ -x "$HOME/venvs/transport/bin/python" ]; then
    PYTHON="$HOME/venvs/transport/bin/python"
elif command -v python3 >/dev/null 2>&1; then
    PYTHON="python3"
else
    echo "Error: Python 3 not found!" >&2
    exit 1
fi

make -C backend cli
CLI="$REPO_ROOT/backend/build/transport_cli"

mkdir -p data/coldstart

$CLI gen-demand \
    --cache data/graph_cache.txt \
    --out data/coldstart/demand_history.csv \
    --days 120 \
    --stops 60 \
    --seed 42

$CLI coldstart-prepare --out-dir data/coldstart

$PYTHON ml/train_model.py \
    --history data/coldstart/demand_history.csv \
    --train data/coldstart/demand_train.csv \
    --test data/coldstart/demand_test.csv \
    --out-model data/coldstart/demand_rf.txt \
    --out-golden data/coldstart/golden_predictions.csv \
    --out-metrics data/coldstart/ml_metrics.json \
    --split-seed 42 \
    --test-frac 0.30 \
    --trees 30 \
    --depth 9 \
    --leaf 30 \
    --seed 42 \
    --holdout-stops-file data/coldstart/holdout_stops.txt

$CLI coldstart-eval \
    --train data/coldstart/demand_train.csv \
    --test data/coldstart/demand_test.csv \
    --model data/coldstart/demand_rf.txt \
    --holdout data/coldstart/holdout_stops.txt

