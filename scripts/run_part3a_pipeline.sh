#!/bin/bash
set -e

# scripts/run_part3a_pipeline.sh
# End-to-end Part 3A ML Pipeline

# 0. Locate repo root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Select Python
if [ -x "$HOME/venvs/transport/bin/python" ]; then
    PYTHON="$HOME/venvs/transport/bin/python"
elif command -v python3 >/dev/null 2>&1; then
    PYTHON="python3"
else
    echo "Error: Python 3 not found!" >&2
    exit 1
fi

mkdir -p data docs data/exp_noevent

RESULTS_FILE="$REPO_ROOT/docs/part3a_results.txt"
exec > >(tee "$RESULTS_FILE") 2>&1

echo "================================================================="
echo "        PART 3A END-TO-END DEMAND & ML PIPELINE RUN              "
echo "================================================================="
echo "Timestamp: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
echo "Python:    $($PYTHON --version)"
echo "Repo root: $REPO_ROOT"
echo ""

# 1. Build CLI
echo "--- Step 1: Build CLI ---"
make -C backend cli
CLI="$REPO_ROOT/backend/build/transport_cli"
echo "CLI built successfully: $CLI"
echo ""

# 2. Stops and Graph SCC Info
echo "--- Step 2: Stops and Graph SCC Info ---"
$CLI stops-info --cache data/graph_cache.txt --stops 60 --seed 42
echo ""

# 3. Generate Synthetic Demand Dataset (120 days, 60 stops)
echo "--- Step 3: Generate Demand Dataset ---"
$CLI gen-demand --cache data/graph_cache.txt --out data/demand_history.csv --days 120 --stops 60 --seed 42

EXPECTED_DAYS=120
EXPECTED_STOPS=60
EXPECTED_ROWS=$((EXPECTED_DAYS * EXPECTED_STOPS * 24))
awk -F',' -v exp_days="$EXPECTED_DAYS" -v exp_stops="$EXPECTED_STOPS" -v exp_rows="$EXPECTED_ROWS" '
NR > 1 {
    day = $1 + 0;
    stop = $2;
    if (day > max_day) max_day = day;
    if (!seen_stop[stop]++) distinct_stops++;
    rows++;
}
END {
    err = 0;
    if (rows != exp_rows) { printf("ERROR: Expected %d rows, got %d\n", exp_rows, rows); err = 1; }
    if (max_day != exp_days - 1) { printf("ERROR: Expected max day %d, got %d\n", exp_days - 1, max_day); err = 1; }
    if (distinct_stops != exp_stops) { printf("ERROR: Expected %d distinct stops, got %d\n", exp_stops, distinct_stops); err = 1; }
    if (err) exit 1;
    printf("Validation passed: %d rows, max day %d (0..%d), %d distinct stops.\n", rows, max_day, exp_days - 1, distinct_stops);
}' data/demand_history.csv
echo ""

# 4. Train Model and Export Golden Fixture (Baseline)
echo "--- Step 4: Train Random Forest Model ---"
$PYTHON ml/train_model.py \
    --history data/demand_history.csv \
    --train data/demand_train.csv \
    --test data/demand_test.csv \
    --out-model data/demand_rf.txt \
    --out-golden data/golden_predictions.csv \
    --out-metrics data/ml_metrics.json \
    --split-seed 42 \
    --test-frac 0.30 \
    --trees 30 \
    --depth 9 \
    --leaf 30 \
    --seed 42
echo ""

# 5. Evaluate Predictors (Moving Average, OLS per band, Random Forest)
echo "--- Step 5: Evaluate Predictors ---"
$CLI demand-eval \
    --train data/demand_train.csv \
    --test data/demand_test.csv \
    --model data/demand_rf.txt \
    --tol 0.5
echo ""

# 6. Golden Check (C Inference vs Golden Predictions)
echo "--- Step 6: Golden Predictions Verification ---"
$CLI golden-check \
    --model data/demand_rf.txt \
    --golden data/golden_predictions.csv \
    --tol 1e-6
echo ""

# 7. Experiment 2: Out-of-Distribution Event Evaluation
echo "--- Step 7: Experiment 2 (Exclude Event Days from Train) ---"
$PYTHON ml/train_model.py \
    --history data/demand_history.csv \
    --train data/exp_noevent/demand_train.csv \
    --test data/exp_noevent/demand_test.csv \
    --out-model data/exp_noevent/demand_rf.txt \
    --out-golden data/exp_noevent/golden_predictions.csv \
    --out-metrics data/exp_noevent/ml_metrics.json \
    --split-seed 42 \
    --test-frac 0.30 \
    --trees 30 \
    --depth 9 \
    --leaf 30 \
    --seed 42 \
    --exclude-event-days-from-train
echo ""

echo "--- Experiment 2: Evaluate Predictors on Unseen Events ---"
$CLI demand-eval \
    --train data/exp_noevent/demand_train.csv \
    --test data/exp_noevent/demand_test.csv \
    --model data/exp_noevent/demand_rf.txt \
    --tol 1.0
echo ""

echo "================================================================="
echo "Pipeline execution finished successfully."
echo "Results saved to: $RESULTS_FILE"
echo "================================================================="
