# Experimental & Verification Results (`docs/RESULTS.md`)

All metrics below match the raw verification artifacts saved under `docs/raw/`. Long decimals (>3 decimal places, such as in the cold-start and anomaly JSON summaries) are rounded to **3 decimal places**; simulation wait-time tables in Section `(d)` match the 2-decimal-place output printed by `docs/raw/sensitivity.txt` and `docs/raw/diagnostic.txt`, and exact scientific-notation tolerances/zeros (`1e-6`, `0.000000e+00`) are kept verbatim.

---

## Summary of Findings

- **`fixed_proportional` had the lowest mean wait of all policies in all three scenarios**, and was significantly below `fixed_equal` in each (`692.31` vs `844.51` s in `normal`, `1306.72` vs `1997.02` s in `rain-event`, `725.74` vs `859.81` s in `breakdown`). It was not tested pairwise against the greedy policies.
- **The three `greedy_marginal` policies (`greedy_marginal_ma`, `greedy_marginal_rf`, `greedy_marginal_oracle`) were significantly below `fixed_equal` in all three scenarios.**
- **`greedy_spec_ma` was inconclusive in `normal` and `breakdown`** (95% CI crossed zero in both: `[-47.50, +2.96]` s and `[-38.87, +17.53]` s).
- **Moving average had lower MAE than random forest on seen stops; random forest was lower on unseen stops** (on holdout stops, unseen `MAE` / `MAE(lambda)` were `2.914` / `1.920` for random forest vs `3.452` / `2.778` for moving average; paired unseen `MAE(lambda)` difference `-0.858`, 95% CI `[-1.090, -0.627]`).

---

## (a) Verification Summary

| Check | Scope / Observed Result | Raw Artifact & Command |
| :--- | :--- | :--- |
| **Backend unit & integration suites** | **Total: `27` suites, `206` tests, `156084` assertions (`0` failed)** — `linkedlist` 8/89, `stack` 5/3052, `queue` 6/134, `tree` 4/57, `heap` 7/6127, `avl` 8/4563, `hashtable` 6/65090, `types` 7/78, `graph` 7/142, `pathfind` 4/9229, `reroute` 11/122, `traffic_sim` 7/147, `traffic_client` 11/74, `osm_loader` 4/60, `graph_scc` 9/36670, `demand_model` 12/23905, `ai_inference` 3/2515, `ai_demand` 7/213, `network` 7/570, `fleet_alloc` 9/1898, `anomaly` 7/45, `sim` 23/292, `sim_ext` 10/252, `eval` 5/126, `registry` 8/278, `api_json` 6/245, `api_actions` 5/111 (`ALL TEST SUITES PASSED SUCCESSFULLY`) | `docs/raw/make_test.txt` (`make -C backend clean && make -C backend test`) |
| **Valgrind leak & memory check** | Every suite binary (`27` suites) exited with `in use at exit: 0 bytes in 0 blocks` and `ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)` (`ALL TEST SUITES PASSED UNDER VALGRIND`) | `docs/raw/make_valgrind.txt` (`make -C backend valgrind`) |
| **ASan + UBSan check** | `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined`, `27` suites, `ALL TEST SUITES PASSED SUCCESSFULLY`, `Failed: 0` | `docs/raw/make_asan.txt` (`make -C backend asan`) |
| **Part 4B frozen files** | `58` files verified `: OK` via `sha256sum -c ~/part4b_frozen.sha` (`part2`, `part3a`, `part3b1` `.sha` files absent on machine: NOT CHECKED) | `docs/raw/frozen_check.txt` (`sha256sum -c ~/part4b_frozen.sha`) |
| **Frontend Vitest suite** | **Total: `10` test files, `65` tests (`0` failures)** (`Test Files 10 passed (10)`, `Tests 65 passed (65)`) | `docs/raw/frontend_test.txt` (`cd frontend && npm test`) |
| **Frontend ESLint & build** | `eslint .` (`0` errors); `vite build`: `674 modules transformed`, `dist/index.html 0.43 kB`, `dist/assets/index-Du8iaJ6a.css 25.26 kB`, `dist/assets/index-Bs8wHWRT.js 896.10 kB` (`gzip: 257.14 kB`) | `docs/raw/frontend_lint.txt`, `docs/raw/frontend_build.txt` (`cd frontend && npm run lint`, `npm run build`) |
| **Live backend REST contract check** | `OK GET` across all GET endpoints and `OK POST` across `sim/control`, `allocate`, `scenario`, `undo`, `inject`, `route/optimize` (`HTTP 200` on `--offline`, seed `42`) | `docs/raw/frontend_contract.txt` (`bash scripts/frontend_contract.sh`) |

---

## (b) Routing & Traffic Results

### A* vs Dijkstra Pathfinding Benchmark (Single Seed)

*Single-seed benchmark output (`seed=42`, `hour=9`, `rain=0`); represents query efficiency on the fixed sample rather than multi-seed generalisation. A\* explored fewer nodes than Dijkstra on both graphs (`28.2%` fewer on the synthetic grid and `19.3%` fewer on the cached Bengaluru OSM graph), but its average query time on the real graph was not lower (`2415.9` us/query vs `2399.0` us/query for Dijkstra).*

| Graph | Pairs Found / Cost Match | A* Nodes total (average per requested pair) | Dijkstra Nodes total (average per requested pair) | Nodes Ratio (`A* / Dijkstra`) | A* Avg Time | Dijkstra Avg Time | Raw Artifact & Command |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Synthetic `40x40` grid (`1600` nodes, `6240` edges) | `200 / 200` (`within 1e-6`) | `118047` (`590.2`) | `164475` (`822.4`) | `0.718` (`28.2%` fewer on these `200` pairs) | `792.3` us | `874.9` us | `docs/raw/bench_synthetic.txt` (`./build/transport_cli bench --synthetic --pairs 200 --seed 42 --hour 9`) |
| Cached Bengaluru OSM (`4765` nodes, `8156` edges) | `192 / 200` (`192 / 192` matched within `1e-6`) | `385728` (`1928.6`) | `478011` (`2390.1`) | `0.807` (`19.3%` fewer on these `200` pairs) | `2415.9` us | `2399.0` us | `docs/raw/bench_real.txt` (`./build/transport_cli bench --pairs 200 --seed 42 --hour 9`) |

### Static vs Traffic-Aware Routing & Undo Demo

| Benchmark / Walkthrough | Static Path Time | Traffic-Optimal Path Time | Notes | Raw Artifact & Command |
| :--- | :--- | :--- | :--- | :--- |
| Synthetic grid (`200` valid pairs, `hour=9`, `seed=42`) | `2572.60` s | `2471.46` s | Single-seed fixed pair comparison (`3.93%` lower mean over these `200` pairs) | `docs/raw/bench_synthetic.txt` (`./build/transport_cli bench --synthetic --pairs 200 --seed 42 --hour 9`) |
| Bengaluru OSM (`192` valid pairs, `hour=9`, `seed=42`) | `1944.79` s | `1828.02` s | Single-seed fixed pair comparison (`6.00%` lower mean over these `192` pairs) | `docs/raw/bench_real.txt` (`./build/transport_cli bench --pairs 200 --seed 42 --hour 9`) |
| `demo` 6-node reference graph (`A->F`) | `17.0` s (`A,C,B,D,E,F` under `3.0` factor) | `14.0` s (`A -> B -> D -> E -> F`) | Also verifies block `B->D` detour (`13.0` s -> `15.0` s) & `undo` restoration to `13.0` s (`History size before undo: 1`, `after undo: 0`) | `docs/raw/cli_demo.txt` (`./build/transport_cli demo`) |
| Offline diurnal traffic model | — | — | `4765` nodes, `8156` edges, `8` corridors, `Source: SIMULATED`, factors `min=1.09 avg=1.17 max=1.29` | `docs/raw/cli_traffic.txt` (`./build/transport_cli traffic --offline`) |

---

## (c) Demand Models & ML Inference Verification

### Predictor Accuracy on Split Test Set (`51840` rows, `36` test days)

Raw source: `docs/raw/part3a_results.txt` (Steps 4–6) and `docs/raw/ml_metrics.json` (`trees: 30`, `depth: 9`, `leaf: 30`, `train_rows: 120960`, `test_rows: 51840`).

| Model | MAE | RMSE | WAPE | MAE (`lambda`) | Weekday (`28776`) | Weekend (`11520`) | Holiday (`1440`) | Rain (`10080`) | Event (`24`) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `moving_average` | `2.160` | `3.482` | `21.84%` | `0.712` | `2.168` | `1.809` | `1.838` | `2.562` | `10.708` |
| `ols_per_band` | `4.471` | `7.722` | `45.23%` | `3.929` | `4.504` | `3.943` | `3.622` | `5.073` | `16.764` |
| `random_forest` | `2.462` | `4.078` | `24.90%` | `1.241` | `2.445` | `1.979` | `4.358` | `2.770` | `11.043` |

- **Event row sample size**: The `24` event rows in the split test set are too few to rank the models on event performance.
- **Experiment 2 (event days excluded from training: `train=115200` rows, `test=57600` rows, `Event=72` rows)**: `moving_average` overall MAE `2.197` (Event `21.009`), `ols_per_band` MAE `4.513` (Event `23.770`), `random_forest` MAE `2.503` (Event `21.595`) (`docs/raw/part3a_results.txt`).
- **Golden C inference parity (`docs/raw/golden_check.txt`, `./build/transport_cli golden-check --model ../data/demand_rf.txt --golden ../data/golden_predictions.csv`)**: `rows checked: 300`, `max diff: 0.000000e+00`, `golden MAE: 0.000000`, `tolerance: 1.000000e-06`, `status: PASS`.

### Cold-Start Holdout Evaluation (`15` holdout stops, `90720` train rows)

Raw source: `docs/raw/coldstart_summary.json` (`make -C backend eval` -> `scripts/run_coldstart.sh`; values below rounded to 3 decimal places). On seen stops, moving average achieves lower error (`MAE` `2.191`, `MAE(lambda)` `0.737`) than random forest (`2.672`, `1.441`); on unseen stops, random forest achieves lower error (`2.914`, `1.920`) than moving average (`3.452`, `2.778`).

| Split | Model | MAE (`mae_noisy`) | RMSE (`rmse_noisy`) | MAE (`mae_lambda`) |
| :--- | :--- | :--- | :--- | :--- |
| Seen (`38880` rows) | `moving_average` | `2.191` | `3.595` | `0.737` |
| Seen (`38880` rows) | `ols_per_band` | `4.607` | `8.141` | `4.074` |
| Seen (`38880` rows) | `random_forest` | `2.672` | `4.568` | `1.441` |
| Unseen (`12960` rows) | `moving_average` | `3.452` | `6.068` | `2.778` |
| Unseen (`12960` rows) | `ols_per_band` | `4.108` | `6.433` | `3.562` |
| Unseen (`12960` rows) | `random_forest` | `2.914` | `5.005` | `1.920` |

- **Paired unseen stop `mae_lambda` comparison (`n_days: 36`)**: `diff_mean_rf_minus_ma = -0.858`, `95% CI [-1.090, -0.627]`, wins/ties/losses `35 / 0 / 1`.

---

## (d) Fleet Allocation & Discrete-Event Simulation

Raw sources: `docs/raw/sensitivity.txt`, `docs/raw/eval_summary_normal.csv`, `docs/raw/eval_summary_rain-event.csv`, `docs/raw/eval_summary_breakdown.csv`, `docs/raw/diagnostic.txt` (`make -C backend eval` -> `transport_cli eval-run`). All comparisons are paired over `n=30` seeds (`base_seed=1000`, `60` stops, `6` routes, `36` buses, weekdays only) on `mean_wait_incl_censored_s` relative to `fixed_equal`. `15 comparisons are made at 95 percent: about one false positive is expected; borderline results are inconclusive.`

### Scenario: `normal` (`n=30`, `base_seed=1000`, `0` reserve buses)

| Policy | Mean +- SD (s) | Paired Diff vs `fixed_equal` [95% CI] (s) | W / T / L | Significant |
| :--- | :--- | :--- | :--- | :--- |
| `fixed_equal` | `844.51 +- 42.57` | base (`0`) | `0 / 30 / 0` | `no` |
| `fixed_proportional` | `692.31 +- 30.72` | `-152.20 [-169.62, -134.78]` | `30 / 0 / 0` | `yes` |
| `greedy_spec_ma` | `822.24 +- 60.15` | `-22.27 [-47.50, +2.96]` | `21 / 0 / 9` | `no` (inconclusive) |
| `greedy_marginal_ma` | `788.12 +- 43.90` | `-56.40 [-80.58, -32.21]` | `25 / 0 / 5` | `yes` |
| `greedy_marginal_rf` | `781.82 +- 33.86` | `-62.69 [-84.72, -40.66]` | `26 / 0 / 4` | `yes` |
| `greedy_marginal_oracle` | `788.71 +- 43.79` | `-55.80 [-80.14, -31.46]` | `25 / 0 / 5` | `yes` |

### Scenario: `rain-event` (`n=30`, `base_seed=1000`, `3` reserve buses)

*Methodological note: In the `rain-event` scenario, `fixed_equal`, `fixed_proportional`, and `greedy_marginal_oracle` pass `predictor = NULL` and therefore use the true demand model (`demand_expected`) for anomaly detection, whereas `greedy_spec_ma` and `greedy_marginal_ma` use the moving average (`dp_ma`) and `greedy_marginal_rf` uses the random forest (`dp_rf`). Cross-policy comparisons in `rain-event` are therefore confounded by differing anomaly-detection baselines, and scenario means should not be compared against `normal` or `breakdown` because `rain-event` adds `3` reserve buses (`0` in the other two scenarios).*

| Policy | Mean +- SD (s) | Paired Diff vs `fixed_equal` [95% CI] (s) | W / T / L | Significant |
| :--- | :--- | :--- | :--- | :--- |
| `fixed_equal` | `1997.02 +- 180.18` | base (`0`) | `0 / 30 / 0` | `no` |
| `fixed_proportional` | `1306.72 +- 80.20` | `-690.30 [-744.78, -635.82]` | `30 / 0 / 0` | `yes` |
| `greedy_spec_ma` | `1469.25 +- 80.43` | `-527.77 [-600.04, -455.50]` | `29 / 0 / 1` | `yes` |
| `greedy_marginal_ma` | `1582.60 +- 86.76` | `-414.42 [-479.17, -349.68]` | `30 / 0 / 0` | `yes` |
| `greedy_marginal_rf` | `1472.94 +- 84.75` | `-524.08 [-588.07, -460.09]` | `30 / 0 / 0` | `yes` |
| `greedy_marginal_oracle` | `1509.06 +- 81.65` | `-487.96 [-556.04, -419.88]` | `30 / 0 / 0` | `yes` |

### Scenario: `breakdown` (`n=30`, `base_seed=1000`, `0` reserve buses)

| Policy | Mean +- SD (s) | Paired Diff vs `fixed_equal` [95% CI] (s) | W / T / L | Significant |
| :--- | :--- | :--- | :--- | :--- |
| `fixed_equal` | `859.81 +- 45.09` | base (`0`) | `0 / 30 / 0` | `no` |
| `fixed_proportional` | `725.74 +- 38.83` | `-134.07 [-152.74, -115.40]` | `29 / 0 / 1` | `yes` |
| `greedy_spec_ma` | `849.14 +- 68.28` | `-10.67 [-38.87, +17.53]` | `19 / 0 / 11` | `no` (inconclusive) |
| `greedy_marginal_ma` | `813.88 +- 40.24` | `-45.93 [-66.11, -25.75]` | `25 / 0 / 5` | `yes` |
| `greedy_marginal_rf` | `828.15 +- 40.17` | `-31.66 [-55.12, -8.20]` | `22 / 0 / 8` | `yes` |
| `greedy_marginal_oracle` | `805.58 +- 42.08` | `-54.23 [-77.61, -30.84]` | `26 / 0 / 4` | `yes` |

### Sensitivity Sweeps (`greedy_marginal_ma` vs `fixed_equal`, `n=30`)

Raw source: `docs/raw/sensitivity.txt`. Note that the rows for `Hyst 0.10` and `0.25` at intervals `30` and `60` min are identical within both `normal` and `rain-event` (unexplained; see Limitations).

| Scenario | Hysteresis (`Hyst`) | Interval (min) | Mean +- SD (s) | Diff [95% CI] (s) | W / T / L | Sig |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `normal` | `0.02` | `30` | `788.12 +- 43.90` | `-56.40 [ -80.58, -32.21]` | `25/ 0/ 5` | `yes` |
| `normal` | `0.02` | `60` | `785.98 +- 37.75` | `-58.53 [ -81.23, -35.84]` | `26/ 0/ 4` | `yes` |
| `normal` | `0.10` | `30` | `773.62 +- 39.50` | `-70.89 [ -87.50, -54.28]` | `29/ 0/ 1` | `yes` |
| `normal` | `0.10` | `60` | `773.62 +- 39.50` | `-70.89 [ -87.50, -54.28]` | `29/ 0/ 1` | `yes` |
| `normal` | `0.25` | `30` | `773.62 +- 39.50` | `-70.89 [ -87.50, -54.28]` | `29/ 0/ 1` | `yes` |
| `normal` | `0.25` | `60` | `773.62 +- 39.50` | `-70.89 [ -87.50, -54.28]` | `29/ 0/ 1` | `yes` |
| `rain-event` | `0.02` | `30` | `1582.60 +- 86.76` | `-414.42 [-479.17, -349.68]` | `30/ 0/ 0` | `yes` |
| `rain-event` | `0.02` | `60` | `1586.93 +- 69.56` | `-410.09 [-474.66, -345.52]` | `30/ 0/ 0` | `yes` |
| `rain-event` | `0.10` | `30` | `1602.79 +- 131.06` | `-394.23 [-447.92, -340.54]` | `30/ 0/ 0` | `yes` |
| `rain-event` | `0.10` | `60` | `1602.79 +- 131.06` | `-394.23 [-447.92, -340.54]` | `30/ 0/ 0` | `yes` |
| `rain-event` | `0.25` | `30` | `1602.79 +- 131.06` | `-394.23 [-447.92, -340.54]` | `30/ 0/ 0` | `yes` |
| `rain-event` | `0.25` | `60` | `1602.79 +- 131.06` | `-394.23 [-447.92, -340.54]` | `30/ 0/ 0` | `yes` |

### Capacity / Traffic Diagnostic (`fixed_equal`, `n=10`, `base_seed=1000`)

Raw source: `docs/raw/diagnostic.txt`. Reference `fixed_equal` alloc proxy at `09:00` (weekday, dry): `308.51` s.

| Variant | Mean (s) | SD (s) | 95% CI (s) |
| :--- | :--- | :--- | :--- |
| `cap=60,traffic=1` | `835.56` | `20.26` | `[821.07, 850.05]` |
| `cap=60,traffic=0` | `473.89` | `17.81` | `[461.15, 486.63]` |
| `cap=1000,traffic=1` | `679.01` | `29.90` | `[657.63, 700.40]` |
| `cap=1000,traffic=0` | `473.34` | `17.59` | `[460.76, 485.93]` |

---

## (e) Anomaly Detector Thresholds & Analytic Validation

Raw source: `docs/raw/anomaly_summary.json` (`seeds: 30`, `base_seed: 1000`, `event_stop: 13`, `make -C backend eval` -> `transport_cli anomaly-eval`; long decimals rounded to 3 decimal places).

At the default threshold `3`, the detector produces `17.233` false alarms per day on average with a precision of `0.104` (while detecting every injected event with a median delay of `15` min). Detection was tested on a single simulated event at one stop (`event_stop: 13`).

| Threshold (`z`) | False Alarms Mean | False Alarms SD | Detection Rate | Median Delay (min) | Precision |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `3` | `17.233` | `4.470` | `1` | `15` | `0.104` |
| `3.5` | `7.267` | `2.935` | `1` | `15` | `0.216` |
| `4` | `3.2` | `2.219` | `1` | `15` | `0.385` |
| `5` | `0.367` | `0.765` | `1` | `15` | `0.845` |

- **Analytic Poisson expectation check**: `analytic_expected: 16.462`, `observed_mean: 17.433`, `standard_error: 0.819`, `pass: true`.

---

## (f) Limitations

- **Single network**: Simulation evaluation covers only one network configuration (`60` stops, `6` routes, `36` buses).
- **Weekdays only**: Multi-seed simulation evaluation runs exclusively under weekday demand (`DAY_WEEKDAY`), omitting weekends and holidays.
- **Scenarios differ in reserve buses**: The `rain-event` scenario includes `3` reserve buses whereas `normal` and `breakdown` have `0`, so wait-time means should not be compared across scenarios.
- **Unexplained identical sensitivity outputs**: In the sensitivity sweeps (`greedy_marginal_ma` vs `fixed_equal`), `Hyst` `0.10` and `0.25` at intervals `30` and `60` min yield identical numbers in both `normal` and `rain-event`; the cause of these identical rows is unexplained.
- **Rain-event anomaly-detector predictors**: In `rain-event`, `fixed_equal`, `fixed_proportional`, and `greedy_marginal_oracle` use the true demand model (`demand_expected`) for anomaly detection, whereas `greedy_spec_ma` and `greedy_marginal_ma` use the moving average and `greedy_marginal_rf` uses the random forest, confounding cross-policy comparisons in `rain-event`.
- **Simulation assumptions**: Demand, passenger arrivals, dwell rules, and baseline diurnal traffic are simulated; live TomTom flow data (when not `--offline`) is an overlay on mapped corridors above a simulated simulation clock rather than real-world transit execution.
- **In-flight HTTP shutdown**: Server shutdown behavior while an HTTP request is actively executing was not shown to be tested.
- **Browser measurements**: Live browser DevTools 60 s Performance busy-% and 5-minute console error checks count as verified only if supplied by the user from a desktop browser run (no headless Chromium/Playwright binary was available inside WSL).

