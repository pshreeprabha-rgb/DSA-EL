#!/bin/bash
set -e
cd ~/transport_verify

if [ -x "$HOME/venvs/transport/bin/python" ]; then
    PYTHON="$HOME/venvs/transport/bin/python"
elif command -v python3 >/dev/null 2>&1; then
    PYTHON="python3"
else
    echo "Python not found" >&2
    exit 1
fi

echo "--- Running train_model.py into /tmp ---"
$PYTHON ml/train_model.py \
    --history data/demand_history.csv \
    --train /tmp/demand_train.csv \
    --test /tmp/demand_test.csv \
    --out-model /tmp/demand_rf.txt \
    --out-golden /tmp/golden_predictions.csv \
    --out-metrics /tmp/ml_metrics.json \
    --split-seed 42 \
    --test-frac 0.30 \
    --trees 30 \
    --depth 9 \
    --leaf 30 \
    --seed 42

echo ""
echo "--- Comparing /tmp against data/ ---"
echo -n "cmp /tmp/demand_rf.txt data/demand_rf.txt: "
if cmp /tmp/demand_rf.txt data/demand_rf.txt; then
    echo "MATCH (exact match, exit 0)"
fi

echo -n "cmp /tmp/golden_predictions.csv data/golden_predictions.csv: "
if cmp /tmp/golden_predictions.csv data/golden_predictions.csv; then
    echo "MATCH (exact match, exit 0)"
fi

echo -n "diff -u /tmp/ml_metrics.json data/ml_metrics.json: "
if diff -u /tmp/ml_metrics.json data/ml_metrics.json; then
    echo "MATCH (exact match, exit 0)"
fi
