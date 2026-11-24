#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
from sklearn.ensemble import RandomForestRegressor

def build_mini_fixture():
    rng = np.random.default_rng(7)
    n_rows = 600

    hours = rng.integers(0, 24, size=n_rows)
    day_types = rng.integers(0, 3, size=n_rows) # 0=weekday, 1=weekend, 2=holiday
    weekends = (day_types == 1).astype(float)
    holidays = (day_types == 2).astype(float)
    rains = (rng.uniform(0, 1, size=n_rows) < 0.2).astype(float)
    events = (rng.uniform(0, 1, size=n_rows) < 0.1).astype(float)

    stop_types = rng.integers(0, 4, size=n_rows) # 0=hub, 1=office, 2=market, 3=res
    type_hub = (stop_types == 0).astype(float)
    type_office = (stop_types == 1).astype(float)
    type_market = (stop_types == 2).astype(float)
    type_res = (stop_types == 3).astype(float)

    k_vals = rng.integers(38, 116, size=n_rows)
    stop_scales = k_vals / 64.0

    X = np.column_stack([
        hours, weekends, holidays, rains, events,
        type_hub, type_office, type_market, type_res, stop_scales
    ])

    # Smooth target function + noise
    y = (
        15.0 * stop_scales +
        2.5 * np.sin(hours / 24.0 * 2.0 * np.pi) +
        10.0 * type_hub +
        5.0 * rains +
        20.0 * events +
        rng.normal(0, 1.0, size=n_rows)
    )

    rf = RandomForestRegressor(
        n_estimators=3,
        max_depth=4,
        min_samples_leaf=5,
        random_state=7,
        n_jobs=1
    )
    rf.fit(X, y)

    # Determine paths
    repo_root = Path(__file__).resolve().parent.parent
    fixtures_dir = repo_root / "backend" / "tests" / "fixtures"
    fixtures_dir.mkdir(parents=True, exist_ok=True)
    rf_txt_path = fixtures_dir / "mini_rf.txt"
    golden_csv_path = fixtures_dir / "mini_golden.csv"

    # Export model to mini_rf.txt
    feature_names = [
        "hour", "weekend", "holiday", "rain", "event",
        "type_hub", "type_office", "type_market", "type_residential", "stop_scale"
    ]
    with open(rf_txt_path, "w", newline="\n") as f:
        f.write("RF_MODEL v1\n")
        f.write(f"FEATURES {len(feature_names)} {' '.join(feature_names)}\n")
        f.write(f"TREES {len(rf.estimators_)}\n")

        for tree_idx, estimator in enumerate(rf.estimators_):
            tree_ = estimator.tree_
            f.write(f"TREE {tree_idx} NODES {tree_.node_count}\n")
            for i in range(tree_.node_count):
                left = tree_.children_left[i]
                right = tree_.children_right[i]
                if left == -1: # Leaf
                    val = float(tree_.value[i][0][0])
                    f.write(f"-1\t-1\t-1\t0\t{val:.17g}\n")
                else:
                    feat = tree_.feature[i]
                    thresh = repr(float(tree_.threshold[i]))
                    val = float(tree_.value[i][0][0])
                    f.write(f"{left}\t{right}\t{feat}\t{thresh}\t{val:.17g}\n")
            f.write("END\n")

    # Pure-Python self-check
    trees_parsed = []
    with open(rf_txt_path, "r", newline="\n") as f:
        lines = [line.strip() for line in f if line.strip()]
    idx = 0
    assert lines[idx] == "RF_MODEL v1"; idx += 1
    assert lines[idx].startswith("FEATURES 10"); idx += 1
    assert lines[idx] == "TREES 3"; idx += 1
    for _ in range(3):
        header = lines[idx].split()
        assert header[0] == "TREE" and header[2] == "NODES"
        m = int(header[3])
        idx += 1
        nodes = []
        for _ in range(m):
            parts = lines[idx].split("\t")
            idx += 1
            nodes.append((int(parts[0]), int(parts[1]), int(parts[2]), float(parts[3]), float(parts[4])))
        assert lines[idx] == "END"
        idx += 1
        trees_parsed.append(nodes)

    def py_predict_row(row):
        tree_preds = []
        for nodes in trees_parsed:
            curr = 0
            while nodes[curr][0] != -1: # internal
                feat = nodes[curr][2]
                thresh = nodes[curr][3]
                val_f32 = float(np.float32(row[feat]))
                if val_f32 <= thresh:
                    curr = nodes[curr][0]
                else:
                    curr = nodes[curr][1]
            tree_preds.append(nodes[curr][4])
        return sum(tree_preds) / len(tree_preds)

    sk_preds = rf.predict(X)
    diffs = [abs(py_predict_row(X[i]) - sk_preds[i]) for i in range(len(X))]
    max_diff = max(diffs)
    assert max_diff < 1e-9, f"Self-check failed: max diff = {max_diff}"

    # Generate mini_golden.csv: first 200 rows + 24 custom hourly rows
    extra_rows = []
    for h in range(24):
        # hour, weekend, holiday, rain, event, type_hub, type_office, type_market, type_residential, stop_scale
        extra_rows.append([float(h), 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.75])
    golden_X = np.vstack([X[:200], np.array(extra_rows)])
    golden_y = rf.predict(golden_X)

    with open(golden_csv_path, "w", newline="\n") as f:
        f.write("hour,weekend,holiday,rain,event,type_hub,type_office,type_market,type_residential,stop_scale,expected\n")
        for i in range(len(golden_X)):
            row = golden_X[i]
            exp_val = golden_y[i]
            # Write exact formats
            f.write(f"{int(row[0])},{int(row[1])},{int(row[2])},{int(row[3])},{int(row[4])},"
                    f"{int(row[5])},{int(row[6])},{int(row[7])},{int(row[8])},"
                    f"{row[9]:.6f},{exp_val:.17g}\n")

    print(f"mini_rf.txt and mini_golden.csv generated successfully (max self-check diff = {max_diff:.2e})")

if __name__ == "__main__":
    build_mini_fixture()
