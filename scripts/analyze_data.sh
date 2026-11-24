#!/bin/bash
set -e
cd ~/transport_verify

if [ -x "$HOME/venvs/transport/bin/python" ]; then
    PYTHON="$HOME/venvs/transport/bin/python"
elif command -v python3 >/dev/null 2>&1; then
    PYTHON="python3"
else
    echo "Error: Python not found" >&2
    exit 1
fi

echo "=== Data Analysis: Days, Holidays, Events ==="
$PYTHON -c "
import pandas as pd
df = pd.read_csv('data/demand_history.csv')
train = pd.read_csv('data/demand_train.csv')
test = pd.read_csv('data/demand_test.csv')

total_days = df['day'].nunique()
train_days = train['day'].nunique()
test_days = test['day'].nunique()

holidays = sorted([int(x) for x in df[df['day_type'] == 2]['day'].unique()])
events = sorted([int(x) for x in df[df['event'] == 1]['day'].unique()])

print(f'Total days in demand_history.csv: {total_days}')
print(f'Train days in demand_train.csv:    {train_days}')
print(f'Test days in demand_test.csv:      {test_days}')
print(f'Holiday days count: {len(holidays)}')
print(f'Holiday days list:  {holidays}')
print(f'Event days count:   {len(events)}')
print(f'Event days list:    {events}')

if len(holidays) < 4:
    print(f'NOTICE: Generator produced {len(holidays)} holiday days (< 4 expected).')
if len(events) < 8:
    print(f'NOTICE: Generator produced {len(events)} event days (< 8 expected).')
"

echo ""
echo "=== Compare Forest MAE vs ml_metrics.json ==="
CLI_MAE=$(awk '/Forest MAE \(noisy\):/ { print $4 }' docs/part3a_results.txt | head -1)
JSON_MAE=$(grep -o -E '"rf_mae(_noisy)?": [0-9.]*' data/ml_metrics.json | head -1 | awk '{print $2}')
echo "CLI Forest MAE (noisy):   $CLI_MAE"
echo "JSON rf_mae_noisy:        $JSON_MAE"
$PYTHON -c "
cli = float('$CLI_MAE')
js = float('$JSON_MAE')
diff = abs(cli - js)
print(f'Absolute difference: {diff:.10e}')
assert diff < 1e-6, f'Mismatch: {diff}'
print('Assertion passed: CLI Forest MAE matches ml_metrics.json to >= 6 decimal places.')
"
