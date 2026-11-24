#!/usr/bin/env python3
"""
ml/train_model.py
Trains a Random Forest regressor on synthetic Bangalore bus demand data,
exports it to the custom C-readable RF_MODEL v1 text format, generates
golden test predictions, and exports training/evaluation metrics.
"""

import faulthandler
faulthandler.dump_traceback_later(240, exit=True)

import argparse
import json
import math
import os
import sys
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestRegressor

FEATURE_NAMES = [
    "hour",
    "weekend",
    "holiday",
    "rain",
    "event",
    "type_hub",
    "type_office",
    "type_market",
    "type_residential",
    "stop_scale"
]

def extract_features(df):
    """
    Extracts 10-feature matrix matching demand_features in demand_model.c:
    0: hour
    1: weekend (day_type == 1)
    2: holiday (day_type == 2)
    3: rain
    4: event
    5: type_hub (stop_type == 0)
    6: type_office (stop_type == 1)
    7: type_market (stop_type == 2)
    8: type_residential (stop_type == 3)
    9: stop_scale
    """
    hours = df["hour"].values.astype(float)
    day_types = df["day_type"].values
    weekends = (day_types == 1).astype(float)
    holidays = (day_types == 2).astype(float)
    rains = (df["rain"].values != 0).astype(float)
    events = (df["event"].values != 0).astype(float)

    stop_types = df["stop_type"].values
    type_hub = (stop_types == 0).astype(float)
    type_office = (stop_types == 1).astype(float)
    type_market = (stop_types == 2).astype(float)
    type_res = (stop_types == 3).astype(float)

    scale_col = "stop_scale" if "stop_scale" in df.columns else "scale"
    stop_scales = df[scale_col].values.astype(float)

    return np.column_stack([
        hours, weekends, holidays, rains, events,
        type_hub, type_office, type_market, type_res, stop_scales
    ])

def split_history_by_days(history_path, train_path, test_path, test_frac, seed, exclude_event_days):
    """
    Performs atomic day-level stratified split.
    """
    print(f"[Stage: Split] Loading history from {history_path}...", flush=True)
    with open(history_path, "r", encoding="utf-8") as f:
        header = f.readline()
        lines = f.readlines()

    header_cols = [c.strip() for c in header.strip().split(",")]
    col_idx = {name: i for i, name in enumerate(header_cols)}

    day_col = col_idx.get("day", None)
    if day_col is None:
        raise ValueError("History CSV must have 'day' column for day-level split")

    day_info = {} # day -> { 'has_event': bool, 'has_rain': bool, 'day_type': int, 'lines': [] }

    for line in lines:
        parts = line.strip().split(",")
        if len(parts) < len(header_cols):
            continue
        d = int(parts[day_col])
        dt = int(parts[col_idx["day_type"]])
        rain = int(parts[col_idx["rain"]])
        ev = int(parts[col_idx["event"]])

        if d not in day_info:
            day_info[d] = {
                "has_event": False,
                "has_rain": False,
                "day_type": dt,
                "lines": []
            }
        if ev != 0:
            day_info[d]["has_event"] = True
        if rain != 0:
            day_info[d]["has_rain"] = True
        day_info[d]["lines"].append(line)

    # Classify each day into 5 strata:
    # 0: event
    # 1: rain (and no event)
    # 2: holiday (dry, no event)
    # 3: weekend (dry, no event)
    # 4: weekday (dry, no event)
    strata = [[], [], [], [], []]
    for d, info in sorted(day_info.items()):
        if info["has_event"]:
            strata[0].append(d)
        elif info["has_rain"]:
            strata[1].append(d)
        elif info["day_type"] == 2: # holiday
            strata[2].append(d)
        elif info["day_type"] == 1: # weekend
            strata[3].append(d)
        else: # weekday
            strata[4].append(d)

    test_days = set()
    train_days = set()

    for s_idx, days in enumerate(strata):
        if not days:
            continue
        if s_idx == 0 and exclude_event_days:
            # All event days go to test
            for d in days:
                test_days.add(d)
            continue

        rng = np.random.default_rng(seed + s_idx)
        shuffled = list(days)
        rng.shuffle(shuffled)

        n_test = int(round(test_frac * len(shuffled)))
        if n_test == 0 and len(shuffled) > 1 and test_frac > 0:
            n_test = 1

        for d in shuffled[:n_test]:
            test_days.add(d)
        for d in shuffled[n_test:]:
            train_days.add(d)

    print(f"[Stage: Split] Day split: {len(train_days)} train days, {len(test_days)} test days.", flush=True)

    os.makedirs(os.path.dirname(os.path.abspath(train_path)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(test_path)), exist_ok=True)

    train_lines = []
    test_lines = []

    for d in sorted(train_days):
        train_lines.extend(day_info[d]["lines"])
    for d in sorted(test_days):
        test_lines.extend(day_info[d]["lines"])

    with open(train_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        f.writelines(train_lines)

    with open(test_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        f.writelines(test_lines)

    print(f"[Stage: Split] Wrote {len(train_lines)} train rows to {train_path}", flush=True)
    print(f"[Stage: Split] Wrote {len(test_lines)} test rows to {test_path}", flush=True)

    return len(train_days), len(test_days)

def export_forest_to_text(rf, feature_names, out_path):
    """
    Exports scikit-learn RandomForestRegressor to RF_MODEL v1 format.
    Format:
      RF_MODEL v1
      FEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale
      TREES <T>
      TREE <idx> NODES <m>
      <left>\t<right>\t<feat>\t<thresh>\t<leaf_val>
      ...
      END
    """
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("RF_MODEL v1\n")
        f.write(f"FEATURES {len(feature_names)} {' '.join(feature_names)}\n")
        f.write(f"TREES {len(rf.estimators_)}\n")

        for tree_idx, estimator in enumerate(rf.estimators_):
            tree_ = estimator.tree_
            f.write(f"TREE {tree_idx} NODES {tree_.node_count}\n")
            for i in range(tree_.node_count):
                left = tree_.children_left[i]
                right = tree_.children_right[i]
                if left == -1 and right == -1: # Leaf
                    val = float(tree_.value[i][0][0])
                    f.write(f"-1\t-1\t-1\t0\t{val:.17g}\n")
                else:
                    feat = tree_.feature[i]
                    thresh = repr(float(tree_.threshold[i]))
                    val = float(tree_.value[i][0][0])
                    f.write(f"{left}\t{right}\t{feat}\t{thresh}\t{val:.17g}\n")
            f.write("END\n")

    size_mb = os.path.getsize(out_path) / (1024 * 1024)
    print(f"[Stage: Export] Exported forest to {out_path} ({size_mb:.2f} MB)", flush=True)
    if size_mb > 3.0:
        raise ValueError(f"Exported model size {size_mb:.2f} MB exceeds 3.0 MB limit!")
    return size_mb

def load_exported_trees(model_path):
    """
    Parses exported model text file into numpy arrays per tree.
    Returns list of dicts: [{'left': ndarray, 'right': ndarray, 'feat': ndarray, 'thresh': ndarray, 'val': ndarray}]
    """
    with open(model_path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f if line.strip()]

    idx = 0
    assert lines[idx] == "RF_MODEL v1", f"Invalid header: {lines[idx]}"
    idx += 1
    assert lines[idx].startswith("FEATURES 10"), f"Invalid features: {lines[idx]}"
    idx += 1
    assert lines[idx].startswith("TREES "), f"Invalid trees line: {lines[idx]}"
    num_trees = int(lines[idx].split()[1])
    idx += 1

    trees = []
    for t in range(num_trees):
        header_parts = lines[idx].split()
        assert header_parts[0] == "TREE" and header_parts[2] == "NODES", f"Bad tree header: {lines[idx]}"
        node_count = int(header_parts[3])
        idx += 1

        left = np.zeros(node_count, dtype=np.int32)
        right = np.zeros(node_count, dtype=np.int32)
        feat = np.zeros(node_count, dtype=np.int32)
        thresh = np.zeros(node_count, dtype=np.float64)
        val = np.zeros(node_count, dtype=np.float64)

        for n in range(node_count):
            parts = lines[idx].split("\t")
            idx += 1
            left[n] = int(parts[0])
            right[n] = int(parts[1])
            feat[n] = int(parts[2])
            thresh[n] = float(parts[3])
            val[n] = float(parts[4])

        assert lines[idx] == "END", f"Expected END, got: {lines[idx]}"
        idx += 1

        trees.append({
            "left": left,
            "right": right,
            "feat": feat,
            "thresh": thresh,
            "val": val,
            "node_count": node_count
        })

    return trees

def vectorized_tree_predict(tree_data, X, tree_idx):
    """
    Vectorized traversal over all rows of X for a single tree.
    Capped at tree_data['node_count'] steps to strictly prevent infinite loops.
    """
    left = tree_data["left"]
    right = tree_data["right"]
    feat = tree_data["feat"]
    thresh = tree_data["thresh"]
    val = tree_data["val"]
    node_count = tree_data["node_count"]

    n_samples = len(X)
    curr_nodes = np.zeros(n_samples, dtype=np.int32)

    for step in range(node_count):
        is_leaf = (left[curr_nodes] == -1)
        if np.all(is_leaf):
            break

        active_indices = np.where(~is_leaf)[0]
        nodes = curr_nodes[active_indices]
        f_idx = feat[nodes]

        # Extract features for active rows and cast to float32 mirroring C engine
        f_vals = X[active_indices, f_idx].astype(np.float32)
        t_vals = thresh[nodes]

        go_left = (f_vals <= t_vals)
        curr_nodes[active_indices] = np.where(go_left, left[nodes], right[nodes])
    else:
        raise RuntimeError(f"Tree {tree_idx} traversal exceeded node count limit ({node_count})! Infinite loop detected.")

    return val[curr_nodes]

def run_self_check(model_path, rf, X_test, max_rows=5000):
    """
    Self-check verifying exported text model predictions match scikit-learn model within 1e-9.
    """
    print(f"[Stage: Self-Check] Starting self-check on exported {model_path}...", flush=True)
    trees = load_exported_trees(model_path)
    n_check = min(len(X_test), max_rows)
    X_sub = X_test[:n_check]

    sum_preds = np.zeros(n_check, dtype=np.float64)
    for tree_idx, t_data in enumerate(trees):
        preds = vectorized_tree_predict(t_data, X_sub, tree_idx)
        sum_preds += preds

    export_preds = sum_preds / len(trees)
    sk_preds = rf.predict(X_sub)
    max_diff = float(np.max(np.abs(export_preds - sk_preds)))

    print(f"[Stage: Self-Check] Completed on {n_check} test rows. Max diff: {max_diff:.3e}", flush=True)
    if max_diff >= 1e-9:
        raise ValueError(f"Self-check failed: max diff {max_diff:.3e} >= 1e-9")

def evaluate_metrics(y_true, y_pred, y_lambda=None):
    mae = float(np.mean(np.abs(y_pred - y_true)))
    rmse = float(np.sqrt(np.mean((y_pred - y_true) ** 2)))
    sum_true = float(np.sum(y_true))
    wape = float(np.sum(np.abs(y_pred - y_true)) / sum_true) if sum_true > 0 else 0.0
    res = {
        "mae_noisy": mae,
        "rmse_noisy": rmse,
        "wape_noisy": wape
    }
    if y_lambda is not None:
        mae_lambda = float(np.mean(np.abs(y_pred - y_lambda)))
        res["mae_lambda"] = mae_lambda
    return res

def main():
    parser = argparse.ArgumentParser(description="Train Random Forest demand model")
    parser.add_argument("--train", default="data/demand_train.csv", help="Train CSV path")
    parser.add_argument("--test", default="data/demand_test.csv", help="Test CSV path")
    parser.add_argument("--history", default=None, help="History CSV path to split")
    parser.add_argument("--split-seed", type=int, default=42, help="Seed for day split")
    parser.add_argument("--test-frac", type=float, default=0.30, help="Fraction of days for test")
    parser.add_argument("--trees", type=int, default=30, help="Number of trees")
    parser.add_argument("--depth", type=int, default=9, help="Max depth")
    parser.add_argument("--leaf", type=int, default=30, help="Min samples leaf")
    parser.add_argument("--seed", type=int, default=42, help="Random seed for forest")
    parser.add_argument("--out-model", default="data/demand_rf.txt", help="Model output path")
    parser.add_argument("--out-golden", default="data/golden_predictions.csv", help="Golden predictions output path")
    parser.add_argument("--out-metrics", default="data/ml_metrics.json", help="Metrics JSON output path")
    parser.add_argument("--golden-rows", type=int, default=300, help="Number of golden rows")
    parser.add_argument("--exclude-event-days-from-train", action="store_true", help="Exclude event days from train")

    args = parser.parse_args()

    train_days_cnt = 0
    test_days_cnt = 0
    if args.history:
        train_days_cnt, test_days_cnt = split_history_by_days(
            args.history, args.train, args.test, args.test_frac, args.split_seed, args.exclude_event_days_from_train
        )

    print(f"[Stage: Read CSV] Reading train dataset: {args.train}", flush=True)
    train_df = pd.read_csv(args.train)
    print(f"[Stage: Read CSV] Reading test dataset: {args.test}", flush=True)
    test_df = pd.read_csv(args.test)

    print("[Stage: Features] Extracting features...", flush=True)
    X_train = extract_features(train_df)
    y_train = train_df["passengers"].values.astype(np.float64)

    X_test = extract_features(test_df)
    y_test = test_df["passengers"].values.astype(np.float64)
    y_lambda = test_df["lambda"].values.astype(np.float64) if "lambda" in test_df.columns else None

    print(f"[Stage: Fit] Training RandomForestRegressor(n_estimators={args.trees}, max_depth={args.depth}, min_samples_leaf={args.leaf}, random_state={args.seed}, n_jobs=1)...", flush=True)
    rf = RandomForestRegressor(
        n_estimators=args.trees,
        max_depth=args.depth,
        min_samples_leaf=args.leaf,
        random_state=args.seed,
        n_jobs=1
    )
    rf.fit(X_train, y_train)
    print("[Stage: Fit] Training complete.", flush=True)

    export_forest_to_text(rf, FEATURE_NAMES, args.out_model)

    # Self-check
    run_self_check(args.out_model, rf, X_test, max_rows=5000)

    # Evaluate on test set
    y_pred_test = rf.predict(X_test)
    metrics = evaluate_metrics(y_test, y_pred_test, y_lambda)
    print(f"[Stage: Metrics] Test MAE (noisy): {metrics['mae_noisy']:.4f}", flush=True)
    print(f"[Stage: Metrics] Test RMSE (noisy): {metrics['rmse_noisy']:.4f}", flush=True)
    print(f"[Stage: Metrics] Test WAPE (noisy): {metrics['wape_noisy']*100:.2f}%", flush=True)
    if "mae_lambda" in metrics:
        print(f"[Stage: Metrics] Test MAE (lambda): {metrics['mae_lambda']:.4f}", flush=True)

    # Generate Golden Predictions CSV (300 evenly spaced rows)
    print(f"[Stage: Golden File] Generating {args.golden_rows} golden predictions...", flush=True)
    n_golden = min(args.golden_rows, len(test_df))
    indices = np.linspace(0, len(test_df) - 1, n_golden, dtype=int)
    golden_df = test_df.iloc[indices].copy()
    golden_preds = rf.predict(X_test[indices])

    # Assign category: 4=event, 3=rain, 2=holiday, 1=weekend, 0=weekday
    cats = np.zeros(len(golden_df), dtype=int)
    ev = (golden_df["event"].values != 0)
    rn = (golden_df["rain"].values != 0)
    hl = (golden_df["day_type"].values == 2)
    wk = (golden_df["day_type"].values == 1)
    cats[wk] = 1
    cats[hl] = 2
    cats[rn] = 3
    cats[ev] = 4
    golden_df["category"] = cats

    scale_col = "stop_scale" if "stop_scale" in golden_df.columns else "scale"
    golden_df["scale"] = golden_df[scale_col]
    golden_df["expected"] = [f"{p:.17g}" for p in golden_preds]

    golden_cols = ["stop_node", "stop_type", "scale", "hour", "day_type", "rain", "event", "passengers", "expected", "category"]
    os.makedirs(os.path.dirname(os.path.abspath(args.out_golden)), exist_ok=True)
    golden_df[golden_cols].to_csv(args.out_golden, index=False, lineterminator="\n")
    print(f"[Stage: Golden File] Wrote {len(golden_df)} golden rows to {args.out_golden}", flush=True)

    # Save metrics JSON
    metrics_data = {
        "train_rows": len(train_df),
        "test_rows": len(test_df),
        "train_days": train_days_cnt,
        "test_days": test_days_cnt,
        "trees": args.trees,
        "depth": args.depth,
        "leaf": args.leaf,
        "rf_mae": metrics["mae_noisy"],
        "rf_mae_noisy": metrics["mae_noisy"],
        "rf_rmse": metrics["rmse_noisy"],
        "rf_wape": metrics["wape_noisy"],
        "rf_mae_lambda": metrics.get("mae_lambda", 0.0)
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out_metrics)), exist_ok=True)
    with open(args.out_metrics, "w", encoding="utf-8", newline="\n") as f:
        json.dump(metrics_data, f, indent=2)
    print(f"[Stage: Metrics] Wrote metrics JSON to {args.out_metrics}", flush=True)

if __name__ == "__main__":
    main()
