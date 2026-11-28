# Smart Public Transport Fleet & Route Optimization System
## Parts 1–5: C11 Core Engine, Discrete-Event Simulation, ML Demand Pipeline, REST API Server & React Dashboard

### 1. Project Overview
The Smart Public Transport Fleet & Route Optimization System is a route planning, fleet scheduling, demand prediction, discrete-event simulation, and REST/web-dashboard platform set on the central Bengaluru road network. **Data provenance**: bus stops and roads are ingested from OpenStreetMap (Overpass API / cached `data/graph_cache.txt`), transit routes are **constructed by this project** (longitude-strip partitioning and nearest-neighbour tours in `network.c` — **not** real BMTC routes or schedules), and passenger demand is **simulated** via an inhomogeneous Poisson model (`demand_model.c`, **not** BMTC ridership data). Ambient congestion uses a simulated diurnal model, with an optional live TomTom flow overlay on 8 mapped corridors.

- **Part 1**: Reusable generic C11 data structure library (`LinkedList`, `Stack`, `Queue`, `Tree`, `Heap`, `AVL`, `HashTable`, and shared domain types).
- **Part 2**: Road network graph engine (`graph`), A* and Dijkstra pathfinding (`pathfind`), traffic-aware detour/undo stack (`reroute`), Gaussian diurnal traffic simulation (`traffic_sim`), live TomTom flow client with `.env` loading (`traffic_client`), and OpenStreetMap Overpass/cache ingestion (`osm_loader`).
- **Part 3A & ML Pipeline (`ml/`)**: Strongly Connected Components decomposition (`graph_scc`), inhomogeneous Poisson demand generation & anchored stop selection (`demand_model`), Python `scikit-learn` Random Forest training/export (`ml/train_model.py`), float32-parity C inference (`ai_inference`), and Moving Average / OLS / Random Forest predictors (`ai_demand`).
- **Part 3B-1**: Bus route network model & all-pairs travel-time matrix (`network`), fleet allocation strategies & greedy move planner (`fleet_alloc`), and real-time Poisson anomaly detector (`anomaly`).
- **Part 3B-2 & Part 3C-1**: Deterministic discrete-event simulation engine (`sim`) supporting time/stop initial bus spacing, `SIM_REASSIGN_DUMP` and `SIM_REASSIGN_DRAIN` fleet reassignment, live move/policy control, and loop observer callbacks.
- **Part 3C-2**: Multi-seed paired statistical evaluator (`eval`), cold-start holdout pipeline (`scripts/run_coldstart.sh`), and anomaly validation (`anomaly-eval`). Full 30-seed results are reported in [../docs/RESULTS.md](../docs/RESULTS.md).
- **Part 3D**: In-memory operational `Registry` (`registry`) unifying a hierarchical `Tree`, $O(1)$ `HashTable` indices, `Avl` timetable, `LinkedList` loop history, and `Heap` route priority board.
- **Part 4A & 4B**: Multi-threaded CivetWeb REST API server (`transport_server` across `api_platform`, `api_app`, `api_json`, `api_http`) with background simulation stepping and unlocked live traffic polling.
- **Part 5 (`../frontend/`)**: React 19 + TypeScript + Vite + Leaflet + Recharts operational dashboard (`npm test`, `npm run lint`, `npm run build`) plus standalone mock server (`frontend/mock/`).

Engineered with zero warnings under `-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`, with **27 backend test suites** (`206` tests, `156084` assertions) verified under AddressSanitizer, UndefinedBehaviorSanitizer, Valgrind (zero leaks/errors), and ThreadSanitizer (`make tsan`), alongside **10 Vitest files** (`65` tests) on the frontend.

---

### 2. Folder Structure
```text
transport/
  Makefile                    # Root targets: backend-test, eval, server, frontend-install, frontend-test, check
  README.md                   # Top-level project overview, quick start, and known limitations
  backend/
    include/
      common.h                # Status codes, status_to_string, shared callback typedefs
      types.h                 # Domain entity models (Stop, Route, Bus, Trip, Edge, Passenger, etc.)
      linkedlist.h            # Doubly linked list with head, tail, and size tracking
      stack.h                 # Dynamic array LIFO stack with 0-indexed top access
      queue.h                 # Circular-buffer FIFO queue with wrap-around resizing
      tree.h                  # General tree (first-child / next-sibling representation)
      heap.h                  # Binary heap with dynamic increase/decrease-key indexing
      avl.h                   # Self-balancing AVL tree with LL/RR/LR/RL rotations
      hashtable.h             # Separate chaining hash table (int64_t and string keys)
      graph.h                 # Road network graph (nodes, edges, haversine, components, cache I/O)
      pathfind.h              # A* and Dijkstra pathfinding with PathResult and cost modes
      reroute.h               # RouteManager with route tracking, block/reroute, and undo stack
      traffic_sim.h           # Diurnal traffic simulation model (Gaussian rush-hour bumps)
      traffic_client.h        # TomTom live traffic client, URL builder/redactor, .env loader
      osm_loader.h            # OpenStreetMap Overpass road parser, stop snapper, and cache loader
      graph_scc.h             # Tarjan's SCC decomposition and largest SCC extractor
      demand_model.h          # Inhomogeneous Poisson demand generator and anchored stop selector
      ai_inference.h          # Random Forest C inference engine (scikit-learn parity)
      ai_demand.h             # Demand predictors (MA, OLS band, RF) and evaluation metrics
      network.h               # Constructed bus route model, strip partitioning, travel-time matrix
      fleet_alloc.h           # Fleet allocation (Equal, Hamilton, Greedy) and move planner
      anomaly.h               # Real-time Poisson demand anomaly detector and cooldown gating
      sim.h                   # Discrete-event bus simulator, scenarios, dump/drain reassignment
      eval.h                  # Paired multi-seed statistical evaluator and anomaly validation
      registry.h              # Live transit registry (Tree, HashTable, Avl timetable, Heap, LinkedList)
      api_platform.h          # POSIX mutex/thread/clock abstractions and signal handling
      api_app.h               # Server application state, sim loop thread, live traffic poller thread
      api_json.h              # JSON serializers/parsers and status-to-HTTP mapping
      api_http.h              # CivetWeb HTTP server setup, routing, CORS, and handlers
    src/                      # C11 implementations for all 27 core/API modules
    tests/                    # test_util.h, test fixtures, and all 27 unit/integration test suites
      test_linkedlist.c       # Linked list suite
      test_stack.c            # Stack suite
      test_queue.c            # Queue suite
      test_tree.c             # Tree suite
      test_heap.c             # Heap suite
      test_avl.c              # AVL suite
      test_hashtable.c        # Hash table suite
      test_types.c            # Domain types & priority comparator suite
      test_graph.c            # Graph ops & save/load suite
      test_pathfind.c         # Dijkstra vs A* routing suite
      test_reroute.c          # RerouteManager & atomic undo suite
      test_traffic_sim.c      # Diurnal traffic curve suite
      test_traffic_client.c   # TomTom client & .env loader suite
      test_osm_loader.c       # OSM loader & stop snapper suite
      test_graph_scc.c        # Tarjan SCC suite
      test_demand_model.c     # Simulated Poisson demand suite
      test_ai_inference.c     # Random Forest C parity inference suite
      test_ai_demand.c        # Demand predictors (MA, OLS, RF) suite
      test_network.c          # Route construction & travel-time matrix suite
      test_fleet_alloc.c      # Fleet allocation & move planner suite
      test_anomaly.c          # Anomaly detector suite
      test_sim.c              # Discrete-event simulator core suite
      test_sim_ext.c          # Simulator drain mode, live control & observer suite
      test_eval.c             # Paired statistics & evaluation suite
      test_registry.c         # Operational registry suite
      test_api_json.c         # JSON serializer/parser & HTTP error mapping suite
      test_api_actions.c      # REST mutation actions & live handler state suite
    tools/
      transport_cli.c         # CLI tool (demo, info, route, bench, osm, traffic, eval-run, etc.)
      transport_server.c      # CivetWeb HTTP REST API server binary
    Makefile                  # Strict build targets (all, test, asan, valgrind, tsan, cli, server, smoke, eval, clean)
    .env.example              # Example environment template for optional TOMTOM_API_KEY
    README.md                 # Detailed backend technical documentation (this file)
  frontend/                   # React 19 + TypeScript + Vite + Leaflet + Recharts dashboard
    src/                      # App, api/, components/, hooks/, lib/, state/, test/, styles.css
    mock/                     # Standalone animated mock API server (`npm run mock`)
    scripts/                  # Frontend verification/helper scripts
    package.json              # Scripts: dev, build, test, lint, mock, preview
    vite.config.ts
    vitest.config.ts
    eslint.config.js
  ml/                         # Python Random Forest training & fixture generation (`train_model.py`, `requirements.txt`)
  scripts/                    # End-to-end ML, coldstart, smoke, stress, Valgrind & contract scripts
  data/                       # Cached OSM graph (`graph_cache.txt`), corridors, RF model, CSVs & eval outputs
  docs/                       # `RESULTS.md`, `ARCHITECTURE.md`, `SETUP.md`, `ATTRIBUTION.md`, `DEMO.md`, `api_contract.md`, `frontend_notes.md`, `raw/`, `mock/`
```

---

### 3. Build and Test Commands
The backend uses GNU Make with strict compiler flags (`-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -g -Iinclude`) plus `libcurl`, `libcjson`, `libcivetweb` (server), and `pthreads`.

```bash
# From repo root: run all backend (27 C suites) + frontend (10 Vitest files) checks
make check

# Backend build, test, sanitizer, server, and evaluation targets
cd backend
make all        # Build static library (build/libtransport_core.a)
make test       # Build and run all 27 test suites
make asan       # Build and run all 27 test suites with ASan + UBSan
make valgrind   # Run all 27 test suites under Valgrind memcheck
make cli        # Build tools/transport_cli -> build/transport_cli
make server     # Build tools/transport_server -> build/transport_server
make smoke      # Build server and run scripts/api_smoke.sh
make tsan       # ThreadSanitizer server + test + smoke + stress run
make eval-quick # 5-seed paired simulation quick check
make eval       # Full 30-seed eval-run + coldstart + anomaly-eval
make clean      # Remove build/, build_asan/, build_tsan/
```

```bash
# Frontend build, lint, test, and dev server (from repo root)
cd frontend
npm ci
npm test        # Vitest unit/component suites (10 files, 65 tests)
npm run lint    # ESLint static analysis
npm run build   # tsc --noEmit && vite build
npm run dev     # Start Vite dev server (connects to backend on port 8080)
npm run mock    # Start standalone mock server on port 8080
```

---

### 4. Part 2 Modules & Algorithms

#### 4.1 Graph Engine (`graph.h` / `graph.c`)
- **Structure**: Compact adjacency list representation with dense node indices `[0..N-1]`, O(1) OSM ID mapping via Hash Table, and pre-allocated out-degree arrays.
- **Components**: Largest Connected Component (LCC) extraction using BFS traversal. Drops disconnected island ways to guarantee full reachability.
- **Atomic Cache I/O**: Saves and loads graphs to/from plain text files (`graph_cache.txt`) atomically via temporary files and rename.

#### 4.2 Pathfinding (`pathfind.h` / `pathfind.c`)
- **Algorithms**: Dijkstra and A* search.
- **Heuristic Admissibility & Consistency**:
  $$h(n) = \frac{\text{haversine}(n, \text{goal})}{\text{graph\_max\_speed\_mps}(g)}$$
  Where `graph_max_speed_mps` is the maximum free-flow speed over all edges in the graph ($\ge 10^{-3}\text{ m/s}$). Since edge traversal cost in traffic mode is:
  $$\text{cost}(u, v) = \text{base\_time\_s}(u, v) \times \text{traffic\_factor}(u, v) \ge \text{base\_time\_s}(u, v) \ge \frac{\text{length}(u, v)}{\text{max\_speed}}$$
  the heuristic never overestimates the actual remaining travel time ($h(n) \le h^*(n)$) and satisfies the triangle inequality $h(u) \le \text{cost}(u, v) + h(v)$ (monotonic/consistent). No reopened nodes are required.
- **Exploration Count Definition**: `nodes_explored` measures the exact number of nodes popped/closed from the priority queue (including the goal).

#### 4.3 Traffic Simulation (`traffic_sim.h` / `traffic_sim.c`)
Deterministic Gaussian bump diurnal model:
- Morning rush peak: 09:00 ($\sigma = 1.0\text{ h}$)
- Evening rush peak: 18:30 ($\sigma = 1.25\text{ h}$)
- Mid-day shoulder: 13:00 ($\sigma = 3.5\text{ h}$, weight 0.2)
- Multipliers: Primary (2.6x peak), Secondary (2.0x peak), Tertiary (1.5x peak), Other (1.3x peak).
- Weather: Rain introduces an additional 1.2x multiplier. Clamped to $[1.0, 5.0]$.

#### 4.4 TomTom Live Traffic Client (`traffic_client.h` / `traffic_client.c`)
- **`.env` Loader**: Implements `env_load_default()` scanning `.env`, `../.env`, `../../.env`. Handles `export` prefixes, quoted strings, whitespace trimming, and non-overwriting `setenv`.
- **Flow Parsing**: Safely extracts `currentSpeed`, `freeFlowSpeed`, and `confidence` from TomTom's `flowSegmentData` JSON. Responses with confidence $< 0.4$ are rejected.
- **Fail-safe Fallback**: In offline mode, network failure, or API quota exhaustion, corridors automatically fall back to the simulated base layer.
- **Privacy & Security**: API keys are redacted in all debug output and logs (`key=***`).

#### 4.5 Reroute Manager (`reroute.h` / `reroute.c`)
- Tracks active transit routes by ID.
- Automatically reroutes vehicles when edges are blocked or congested above a threshold (default 1.8x).
- Pushes atomic `ChangeSet` records onto a LIFO `Stack`.
- `rm_undo` atomically restores prior edge states and affected route trajectories in $O(\text{changed})$ time.

---

### 5. CLI Commands (`transport_cli`)
The CLI provides complete administrative control and benchmarking:

```bash
# 1. 6-node Bangalore micro-graph demo (block, reroute, undo, traffic)
./build/transport_cli demo

# 2. Graph inspection
./build/transport_cli info [--synthetic]

# 3. Point-to-point route planning
./build/transport_cli route <src> <dst> [--synthetic] [--hour H] [--rain] [--algo astar|dijkstra] [--static]

# 4. Performance benchmark (A* vs Dijkstra equivalence & exploration savings)
./build/transport_cli bench [--synthetic] [--pairs N] [--seed S] [--hour H] [--rain]

# 5. OSM Overpass loader
./build/transport_cli osm [--refresh] [--offline]

# 6. Live TomTom traffic flow update
./build/transport_cli traffic [--offline]
```

---

### 6. Thread-Safety Notice
All Part 1 data structures and Part 2/3 graph, pathfinding, network, simulation, and registry modules are **not internally thread-safe**. In the Part 4 HTTP server (`api_app.c`), concurrent access from the HTTP worker threads, the 50 ms simulation advance thread, and the 5 s traffic poller thread is synchronized externally via a shared POSIX mutex (`app_lock()` / `app_unlock()`).

---

### 7. Architectural Mapping Across Modules
| Module | Role Across the System |
| :--- | :--- |
| **`graph` & `pathfind`** | Core routing engine for road paths, route leg construction, and travel-time estimation. |
| **`reroute`** | Powers dynamic road-block detours and atomic `rm_undo` actions (`POST /api/route/block`, `POST /api/undo`). |
| **`traffic_sim` & `traffic_client`** | Generates diurnal city congestion (`SIMULATED`) and overlays live TomTom flow telemetry onto 8 mapped corridors when enabled. |
| **`osm_loader`** | Ingests OpenStreetMap road networks and bus-stop locations (`stops` and `roads` from OSM); transit routes are constructed separately by `network.c`. |

---

### 8. Error Codes & REST Mapping Contract
`api_http_status_from_error()` in `src/api_json.c` maps core C `Status` codes to HTTP REST statuses and JSON error codes (while successful responses return `200 OK` in `src/api_http.c`):

| Library Status | Meaning & Preconditions | HTTP Status & Error Code (`src/api_json.c`) |
| :--- | :--- | :--- |
| **`STATUS_OK` (`0`)** | Operation succeeded without errors. | `200 OK` (`src/api_http.c` success path) |
| **`STATUS_ERR_NULL` (`1`)** | Required internal pointer argument is `NULL`. | `500 Internal Server Error` (`"internal"`) |
| **`STATUS_ERR_NOMEM` (`2`)** | Memory allocation (`malloc`/`calloc`/`realloc`) failed. | `500 Internal Server Error` (`"internal"`) |
| **`STATUS_ERR_NOT_FOUND` (`3`)** | Requested bus, stop, route, or resource was not found. | `404 Not Found` (`"not_found"`) |
| **`STATUS_ERR_DUPLICATE` (`4`)** | Identifier already exists (e.g., duplicate route ID). | `409 Conflict` (`"conflict"`) |
| **`STATUS_ERR_EMPTY` (`5`)** | Container/stack is empty (e.g., `rm_undo` on empty stack). | `409 Conflict` (`"conflict"`) |
| **`STATUS_ERR_INVALID` (`6`)** | Invalid JSON/argument, out-of-range index, NaN, or invalid state transition. | `400 Bad Request` (`"invalid_argument"`) |

---

### 9. Part 3A: Passenger Demand Modelling & C Inference Engine

#### 9.1 Mathematical Formulation
The demand model simulates synthetic passenger arrivals at OpenStreetMap-sourced bus stops using an inhomogeneous Poisson process (`demand_model.c`, not real ridership data):
$$\lambda(\text{stop}, t) = \text{base}(\text{type}) \times \text{scale} \times \text{profile}(\text{type}, \text{hour}) \times \text{daymult} \times \text{rain\_mult} \times \text{event\_mult}$$

- **Stop Profiles & Base Rates** (`src/demand_model.c`):
  - `STOP_HUB` (within `700` m of landmark centres Majestic and Shivajinagar): Base `60.0` pax/hr (`flr = 0.15`). Morning peak `0.85x` (`mu = 8.5, sigma = 1.4`), evening peak `0.90x` (`mu = 18.0, sigma = 1.6`), midday `0.35x` (`mu = 13.0, sigma = 3.0`).
  - `STOP_OFFICE` (within `1500` m of landmark centres MG Road, Indiranagar, and Koramangala): Base `28.0` pax/hr (`flr = 0.05`). Morning peak `1.00x` (`mu = 9.0, sigma = 1.0`), evening peak `0.80x` (`mu = 18.5, sigma = 1.2`), midday `0.20x` (`mu = 13.0, sigma = 2.0`).
  - `STOP_MARKET` (`u < 0.18` outside hub/office zones): Base `24.0` pax/hr (`flr = 0.05`). Morning `0.30x` (`mu = 9.5, sigma = 1.5`), evening peak `0.55x` (`mu = 17.5, sigma = 1.8`), midday peak `0.80x` (`mu = 12.5, sigma = 3.0`).
  - `STOP_RESIDENTIAL` (remaining stops outside hub/office zones): Base `14.0` pax/hr (`flr = 0.05`). Morning peak `1.00x` (`mu = 8.0, sigma = 1.2`), evening peak `0.55x` (`mu = 19.0, sigma = 1.5`), midday `0.10x` (`mu = 13.0, sigma = 3.0`).
- **Multipliers** (`src/demand_model.c`):
  - Rain: $+20\%$ surge ($\times 1.20$).
  - Events (`event != 0`): $+150\%$ surge ($\times 2.50$) during hours `17..20` (`hour >= 17 && hour <= 20`) at up to `3` selected stops in `demand_generate`.
  - Day Type: Weekday (`1.00x` all types); Weekend (`0.80x` hub, `0.30x` office, `1.25x` market, `0.70x` residential); Holiday (`0.70x` hub, `0.20x` office, `1.10x` market, `0.60x` residential).
- **Poisson Sampling**:
  - For $\lambda < 30$: Exact Knuth exponential inter-arrival multiplication algorithm.
  - For $\lambda \ge 30$: Box-Muller Gaussian approximation with continuity correction: $P = \max(0, \text{round}(\lambda + \sqrt{\lambda} \cdot Z))$.

#### 9.2 Determinism & PRNG
All synthetic simulation streams use a pure, portable 64-bit SplitMix64 hash function:
$$\text{mix64}(k) = \text{xor-shift-multiply mixing of } (seed, stream, salt)$$
Every day, stop, and hour has an independent, deterministic random stream.

#### 9.3 Machine Learning Pipeline
1. **Anchored Stop Selection Rule**:
   To ensure key transit hubs and employment corridors are always represented in demand datasets, stop selection uses an anchored rule over the graph's largest SCC:
   - **Hub Centres**: Majestic (radius 700 m, max 3 anchors), Shivajinagar (radius 700 m, max 3 anchors).
   - **Office Corridors**: MG Road (radius 1500 m, max 2 anchors), Indiranagar (radius 1500 m, max 2 anchors), Koramangala (radius 1500 m, max 2 anchors).
   - Candidates are prioritized by distance (`graph_haversine_m`, ties broken by smaller node index). Each stop is selected at most once across centres.
   - If `max_stops` < number of anchors, anchors are truncated in priority order (hubs first, then offices).
   - Remaining slots (`rem_slots = max_stops - anchors`) are filled by uniform stride over remaining non-anchor SCC candidates: `index_k = (k * rem_count) / rem_slots`.
   - The final combined stop list is sorted ascending by node index.
   - Legacy uniform stride without anchoring remains accessible via `--stride-only`.

2. **Day-Level Stratified Partition**:
   Rows belonging to the same day are never split across train and test. Days are grouped into strata across event, rain, holiday, weekend, and weekday. Shuffled deterministically and split into train (70%, 84 days) and test (30%, 36 days).
3. **Predictor Models**:
   - **Moving Average Baseline**: Maintains historical sums and counts. Uses hierarchical fallback:
     $(stop, hour, day\_type) \longrightarrow (type, hour, day\_type) \longrightarrow hour \longrightarrow 0.0$
   - **OLS Linear Regression per Time Band**: Partitions the 24 hours into 4 bands (Night, Morning, Midday, Evening). Fits independent normal equations $X^T X \beta = X^T y$ with $10^{-6}$ ridge regularization on non-intercept features and partial pivoting Gaussian elimination.
   - **Random Forest C Inference**: Trains 30 trees (max depth 9, min leaf 30) in scikit-learn. Exported to the `RF_MODEL v1` flat text format.
4. **Float32 Numerical Parity**:
   Scikit-learn internally casts feature values to IEEE 754 float32 before threshold comparisons. The C engine mirrors this via `(float)features[feat] <= threshold`, achieving strict $0.0$ absolute prediction difference (bit-identical) against Python across 300 golden test vectors.

#### 9.4 Evaluation Metrics
Evaluated on unseen test sets across overall and per-category slices (Weekday dry, Weekend dry, Holiday, Rain, Event):
- $\text{MAE} = \frac{1}{N} \sum |y_i - \hat{y}_i|$
- $\text{RMSE} = \sqrt{\frac{1}{N} \sum (y_i - \hat{y}_i)^2}$
- $\text{WAPE} = \frac{\sum |y_i - \hat{y}_i|}{\sum y_i}$
- $\text{MAE}(\lambda) = \frac{1}{N} \sum |\hat{y}_i - \lambda_i|$ (accuracy against true underlying Poisson intensity)

#### 9.5 Empirical Results & Honest Observations
Pipeline execution on 120 days $\times$ 60 stops with anchored selection (172,800 rows; 120,960 train / 51,840 test; 5 Hub, 15 Office, 9 Market, 31 Residential stops):
- **Predictor Performance (Standard Split, Test Set = 51,840 rows)**:
  - `moving_average`: MAE 2.160, RMSE 3.482, WAPE 21.84%, MAE($\lambda$) 0.712. Category MAE: Wkday 2.168, Wkend 1.809, Hol 1.838, Rain 2.562, Event 10.708.
  - `random_forest`: MAE 2.462, RMSE 4.078, WAPE 24.90%, MAE($\lambda$) 1.241. Category MAE: Wkday 2.445, Wkend 1.979, Hol 4.358, Rain 2.770, Event 11.043.
  - `ols_per_band`: MAE 4.471, RMSE 7.722, WAPE 45.23%, MAE($\lambda$) 3.929.
- **Observations & Hypotheses**:
  - *Measured Performance Difference*: `moving_average` achieved lower overall MAE (2.160 vs 2.462) than `random_forest` on this test set.
  - *Hypothesis (unmeasured)*: `moving_average` may benefit because the table-based lookup directly tracks each stop's mean arrival rate per hour, whereas the Random Forest tree depth (depth 9, leaf 30) regularizes predictions across broader leaves.
  - *Event Slice Sample Size Warning*: In Experiment 1, the test set contains only **$N = 24$ event rows** (1 event day with 24 hours, during which only 3 designated stops surge between 17:00 and 20:00). Event metrics in Experiment 1 rest entirely on these 24 rows and have wide confidence intervals.
  - In Experiment 2 (completely excluding event days from train: 80 train days, 40 test days with $N = 72$ event rows), Event MAE degrades to 21.009 for moving average and 21.595 for random forest, illustrating that both models fail to extrapolate to unobserved surge regimes without event indicators in training.

#### 9.6 CLI Usage
```bash
# Inquire about graph SCCs and stop classifications (anchored by default)
./build/transport_cli stops-info --cache data/graph_cache.txt --stops 60

# Inquire using legacy uniform stride only
./build/transport_cli stops-info --cache data/graph_cache.txt --stops 60 --stride-only

# Generate 120 days of synthetic demand history for 60 stops (anchored by default)
./build/transport_cli gen-demand --cache data/graph_cache.txt --days 120 --stops 60

# Evaluate all three predictors against test split
./build/transport_cli demand-eval --train data/demand_train.csv --test data/demand_test.csv --model data/demand_rf.txt

# Verify C inference engine against golden predictions fixture
./build/transport_cli golden-check --model data/demand_rf.txt --golden data/golden_predictions.csv
```

---

### 10. Part 3B-1: Bus Network Model, Fleet Allocation & Demand Anomaly Detection

#### 10.1 Bus Network Model & Travel-Time Matrix
The network model (`network.h` / `network.c`) constructs transit routes over selected candidate bus stops:
1. **West-to-East Geographic Partitioning**:
   - Stops are sorted lexicographically by longitude ascending, then latitude ascending, then node index ascending.
   - Stops are partitioned into $R$ contiguous longitude strips. Each route receives a base of $\lfloor N / R \rfloor$ stops, with the first $N \pmod R$ routes receiving one additional stop.
2. **Nearest-Neighbour Tour Construction**:
   - For each route partition, tour construction starts at the stop with minimum latitude (ties broken by node index).
   - It iteratively visits the nearest unvisited stop in the partition (using `graph_haversine_m`, ties broken by smaller node index).
   - The loop closes back from the last stop to the start stop.
   - Each leg is registered in the Part 2 `RouteManager` (`rm_track_route(rm, r * 1000 + k, src, dst)`) so dynamic traffic and road blocks propagate immediately to the network.
3. **Static All-Pairs Travel-Time Matrix**:
   - Precomputes shortest-path travel times between all pairs of network stops using A* static cost (`COST_STATIC`).
   - If a pair is disconnected, stores `-1.0`.
   - Used for constant-time $O(1)$ transfer lookups and deadhead travel time evaluation during fleet relocation.
4. **Loop Time, Cycle Time & Route Demands**:
   - Loop time $T_{\text{loop}} = \sum \text{leg\_time}(s_k, s_{k+1})$.
   - Cycle time $T_{\text{cycle}} = T_{\text{loop}} + S \times T_{\text{dwell}}$ (where default dwell time is 20.0 s per stop).
   - Route demand aggregates expected hourly demand across all route stops:
     $D_r = \sum_{s \in \text{Route } r} \mathbb{E}[\text{demand}(s, t)]$, evaluated via ground-truth oracle or trained predictor (`DemandPredictor`).

#### 10.2 Fleet Allocation Strategies
Given total fleet size $B$, $R$ routes, and minimum buses per route $M$:
1. **Equal Allocation**:
   - Distributes $\lfloor B / R \rfloor$ buses to each route.
   - Remaining $B \pmod R$ buses are assigned $+1$ to lowest route indices $0, 1, \dots, (B \pmod R) - 1$.
2. **Proportional Allocation (Hamilton's Largest-Remainder Method)**:
   - Each route is guaranteed $M$ buses. Remaining buses $B_{\text{rem}} = B - M \times R$ are allocated proportional to weights $w_r = D_r$.
   - Quotas $q_r = B_{\text{rem}} \times w_r / \sum w$. Integer parts $\lfloor q_r \rfloor$ are assigned.
   - Leftovers are assigned $+1$ to routes ranked by largest fractional parts $q_r - \lfloor q_r \rfloor$, ties broken by smaller route ID.
3. **Greedy Allocation with Heap**:
   - Uses Part 1 `Heap` (`heap_create`) configured as a max-heap where highest priority item pops first.
   - **Metric 1: Demand Per Bus (`FLEET_PRIO_DEMAND_PER_BUS`)**:
     $$\text{Priority}_r = \frac{D_r}{c_r + 1}$$
     Directly conforms to the dynamic priority formula in `types.h: route_compute_priority(r)`.
   - **Metric 2: Marginal Waiting Time Reduction (`FLEET_PRIO_MARGINAL_WAIT`)**:
     $$\Delta W_r = \frac{D_r \cdot C_r}{2 c_r} - \frac{D_r \cdot C_r}{2 (c_r + 1)} = \frac{D_r \cdot C_r}{2 c_r (c_r + 1)} \propto \frac{D_r \cdot C_r}{c_r (c_r + 1)}$$
     Requires $M \ge 1$ and valid cycle times $C_r > 0$.
     Ties broken strictly by: higher priority, then smaller bus count $c_r$, then smaller route ID.

#### 10.3 Waiting Time Proxy & Optimality Analysis
- **Waiting Time Proxy Formula**:
  Under assumptions of uniform passenger arrivals and deterministic, evenly spaced bus headways, average passenger wait on route $r$ with cycle time $C_r$ and $c_r$ buses is:
  $$W_r = \begin{cases} \frac{C_r}{2 c_r} & \text{if } c_r > 0 \\ 3600.0\text{ s (no-service penalty)} & \text{if } c_r = 0 \text{ and } D_r > 0 \\ 0.0 & \text{if } c_r = 0 \text{ and } D_r = 0 \end{cases}$$
  Total network average passenger wait:
  $$\bar{W} = \frac{\sum_{r=0}^{R-1} D_r \cdot W_r}{\sum_{r=0}^{R-1} D_r}$$
- **Optimality of Marginal Wait vs Demand-Per-Bus**:
  - The objective function $f(c_1, \dots, c_R) = \sum_{r=1}^R \frac{D_r C_r}{2 c_r}$ is strictly convex and separable across integer variables $c_r \ge 1$. By the discrete convexity property (Gross 1956 / Fox 1966), greedy selection of the element offering maximum marginal reduction $\Delta W_r$ is **provably globally optimal**.
  - **Suboptimality of Demand-Per-Bus**: When route cycle times differ, demand-per-bus fails because it ignores route length. For example, consider Route 0 ($D_0 = 120, C_0 = 600\text{ s}$) and Route 1 ($D_1 = 100, C_1 = 3600\text{ s}$) with $B = 3, M = 1$:
    - Demand-per-bus assigns the extra bus to Route 0 ($120/2 > 100/2$), resulting in allocations $[2, 1]$ and average wait of **900.0 s**.
    - Marginal wait assigns the extra bus to Route 1 ($100 \times 3600 / 2 = 180000 \gg 120 \times 600 / 2 = 36000$), resulting in allocations $[1, 2]$ and average wait of **572.7 s** (vs **900.0 s** under demand-per-bus).

#### 10.4 Fleet Move Planner
Transitions a fleet from current assignments to target assignments with minimal deadhead disruption:
1. Surplus routes ($c_r > \text{target}_r$) release buses with lowest current passenger load (ties broken by smaller bus ID).
2. Movers (unassigned reserve buses + released buses) are matched greedily to deficit route slots ($c_r < \text{target}_r$) by minimum deadhead travel time:
   $$\text{deadhead} = \min_{s \in \text{Route } r} \text{travel\_time}(\text{bus\_stop}, s)$$
   Ties broken by smaller bus ID, then smaller route ID.
3. Any released bus not assigned to a deficit route is returned to reserve/depot (`to_route = -1, entry_stop = -1, deadhead = 0.0`).
4. Output moves are deterministically sorted by `bus_id` ascending.

#### 10.5 Poisson Demand Anomaly Detector
Detects real-time demand spikes at bus stops (`anomaly.h` / `anomaly.c`):
- **Poisson Z-Score**:
  $$z = \frac{\text{observed} - \text{expected}}{\sqrt{\max(\text{expected}, v_{\text{floor}})}}$$
  where default variance floor $v_{\text{floor}} = 1.0$ prevents division by near-zero rates.
- **Threshold & Gating Rationale (and Measured Trade-offs)**:
  - `z_threshold = 3.0`: Evaluated in `../docs/raw/anomaly_summary.json` across `30` seeds (`base_seed = 1000`) with a single simulated event at `event_stop = 13`: at default `z = 3.0`, the detector averages **`17.233` false alarms per day** (SD `4.470`) with **`0.104` precision** (`detection_rate = 1.0`, `median_delay_min = 15`). Higher thresholds reduce false alarms per day while keeping `detection_rate = 1.0` and `15` min median delay on that test event: `z = 3.5` (`7.267` FA/day, SD `2.935`, precision `0.216`), `z = 4.0` (`3.2` FA/day, SD `2.219`, precision `0.385`), and `z = 5.0` (`0.367` FA/day, SD `0.765`, precision `0.845`).
  - `min_observed = 5`: Suppresses spurious false positives when expected demand is very small (e.g., $\text{exp} = 0.01, \text{obs} = 4 \implies z = 3.99$, which would otherwise flag despite low absolute volume).
  - `cooldown_min = 60.0`: Once a stop flags an anomaly, subsequent spikes at the same stop are suppressed for 60 minutes to prevent alert storms during sustained events.

#### 10.6 CLI Usage
```bash
# Display bus network topology and routes on Bangalore graph
./build/transport_cli network-info --cache data/graph_cache.txt --stops 60 --routes 6

# Run fleet allocation comparison and move planning
./build/transport_cli alloc-demo --cache data/graph_cache.txt --model-rf data/demand_rf.txt --buses 36 --hour 9
```

---

### 11. Part 3B-2: Discrete-Event Bus Simulation Engine & Scenarios (`sim.h`, `sim.c`)

#### 11.1 Architecture & Discrete-Event Core
Part 3B-2 introduces a deterministic, discrete-event simulation engine for the Bangalore public transit network. The engine simulates individual bus operations along circular route tours, passenger arrivals via an inhomogeneous Poisson process, queue dynamics at stops, dwell delays with boarding/alighting congestion, dynamic fleet reallocation under demand forecasts, real-time demand anomaly detection with emergency reserve dispatches, and disruptions (weather, surges, vehicle breakdowns, road blockages).

- **Event Engine (`Heap`)**:
  - All future events reside in a min-heap ordered by `(time_s, kind, seq)`.
  - **Deterministic Tie-Breaking**: When two events occur at the exact same physical timestamp `time_s`, ties are broken first by `kind` priority, then by a strictly increasing monotonic 64-bit sequence number `seq`.
  - **Event Kinds**:
    1. `SIM_EVT_SCENARIO` (kind = 0): Injected scenario changes (rain, surge, breakdown, blockage) applied before slice or bus events.
    2. `SIM_EVT_SLICE` (kind = 1): Periodic slice tick (default 15.0 min) executing anomaly window closure, dynamic fleet reallocation, traffic model updates, and demand arrival generation.
    3. `SIM_EVT_ARRIVE` (kind = 2): Bus arrival at a stop, alighting passengers, handling reassignments, and boarding passengers.
    4. `SIM_EVT_DEPART` (kind = 3): Bus departure from a stop after dwell, scheduling arrival at the subsequent route stop.

#### 11.2 Demand Generation & Pending Arrival Buffer
- **Slice-Based Arrival Batching**:
  - At the start of each slice $[t, t + \text{slice\_min})$, passenger arrival counts are generated using `demand_expected` and `demand_poisson_sample`.
  - Uniform fractional arrival timestamps within the slice $[t, t + \text{slice\_min})$ are assigned using 64-bit splitmix hashes.
  - **Pending Arrivals vs Stop Queue**:
    - Newly generated arrivals are placed into a pre-allocated per-stop array (`st->pending`).
    - Arrivals are NOT enqueued into the FIFO passenger queue (`st->queue`) until their physical arrival time has elapsed.
    - Whenever a bus arrives at a stop (or a slice tick occurs), `stop_flush(s, stop_idx, now_s)` moves all pending passengers with $\text{arrival\_s} \le \text{now\_s}$ into `st->queue`.
    - This ensures passengers arriving in the future cannot be prematurely boarded by buses arriving earlier in the slice.

#### 11.3 Bus Operations, Dwell & Passenger Flow
- **Bus States**:
  - `SIM_BUS_RESERVE`: Stationed at depot (`depot_stop`), available for emergency anomaly dispatch.
  - `SIM_BUS_RUNNING`: Active service on assigned route.
  - `SIM_BUS_DEADHEAD`: In transit to another route's entry stop following reallocation or emergency dispatch.
  - `SIM_BUS_BROKEN`: Out of service due to mechanical failure; passengers on board are stranded.
- **Boarding & Congestion**:
  - When a bus arrives at a stop, alighting passengers for this stop alight first, freeing capacity.
  - Waiting passengers board strictly in FIFO order up to available capacity (`capacity - load`).
  - **Dwell Time**:
    $$\text{dwell} = \text{dwell\_s} + \text{per\_pax\_s} \times (\text{alighted} + \text{boarded})$$
  - If passengers remain in queue after the bus is full, a `full_bus_event` is logged.

#### 11.4 Dynamic Fleet Control & Reallocation
- **Periodic Evaluation**:
  - Every `realloc_interval_min` (default 30 min), the simulation evaluates target fleet sizes using greedy optimization (`fleet_targets_greedy`).
  - **Hysteresis Filtering**:
    - Reallocation moves are executed only if the relative improvement in network waiting time proxy exceeds `realloc_hysteresis` (default 2% / 0.02):
      $$\frac{\text{proxy}_{\text{current}} - \text{proxy}_{\text{target}}}{\text{proxy}_{\text{current}}} \ge \text{realloc\_hysteresis}$$
  - **Reassignment Move Execution (`dump` vs `drain`)**:
    - Buses selected for reassignment are marked with `pending_route` and finish their current active leg before executing any transfer.
    - **`SIM_REASSIGN_DUMP` (`0`, library default in `sim_config_default()`)**: Upon arrival at its next stop, any remaining onboard passengers are forced-dumped (`forced_dumps += load`, `load = 0`) and the bus immediately transitions to `SIM_BUS_DEADHEAD` (or `SIM_BUS_RESERVE` when `pending_route == -1`).
    - **`SIM_REASSIGN_DRAIN` (`1`, CLI `--reassign drain`, `eval_run_single`, and server default)**: With `pending_route != -2`, the bus skips new boarding (`sim.c:457`), continues along its route alighting passengers normally, and executes the move (recomputing entry stop and deadhead time via `network_entry_time_s`) only once `load == 0`, keeping `forced_dumps = 0`.
- **Emergency Reserve Dispatch**:
  - If `anomaly_dispatch` is enabled, any anomaly flagged by the real-time Poisson detector triggers an immediate dispatch of the nearest reserve bus from the depot to the affected stop.

#### 11.5 Initial Bus Spacing: Stop Spacing vs. Time Spacing
The simulation engine provides two distinct initial bus positioning and dispatching strategies via the `initial_spacing` configuration:
- `SIM_SPACING_BY_STOP` (0, legacy): Distributes $c$ buses evenly by stop index:
  $$\text{pos}_j = \left\lfloor \frac{j \cdot m}{c} \right\rfloor$$
  All buses schedule their initial arrival at $t_0$. While uniform in stop index, on routes with asymmetric leg lengths this causes temporal bunching and elevated initial headways.
- `SIM_SPACING_BY_TIME` (1, default in CLI): Dispatches buses to ensure uniform temporal headways along the route cycle:
  - For route $r$ with $m$ stops, cumulative schedule offsets are computed under free-flow traffic:
    $$T_0 = 0, \quad T_{k+1} = T_k + t_{\text{leg}}(k, k+1) + \text{dwell\_s}, \quad \text{cycle} = T_m$$
  - For each bus $j \in [0, c-1]$, the target cycle phase is:
    $$\psi_j = \frac{j \cdot \text{cycle}}{c}$$
  - The initial stop index is the smallest $k^* \in [0, m]$ such that $T_{k^*} \ge \psi_j$, with starting position $pos = k^* \pmod m$.
  - The bus schedules its first arrival at:
    $$t_{\text{first}} = t_0 + (T_{k^*} - \psi_j)$$
  - *Worked Numerical Example*: Route with $m=4$ stops (legs: 100 s, 100 s, 100 s, 300 s; dwell: 20 s), giving $T = [0, 120, 240, 360, 680]$ s and cycle 680 s.
    - For $c=2$ buses: $\psi_0 = 0 \implies k^*=0$ (stop 0, delay 0 s); $\psi_1 = 340 \implies T_3 = 360 \ge 340 \implies k^*=3$ (stop 3, delay $360 - 340 = 20$ s). Bus 1 departs stop 3 at $t_0 + 40$ s, traversing the 300 s leg to reach stop 0 at $t_0 + 340$ s, exactly half the 680 s cycle after Bus 0!
    - For $c=3$ buses: $\psi_0 = 0 \implies k^*=0$ (stop 0, delay 0 s); $\psi_1 = 226.67 \implies T_2 = 240 \ge 226.67 \implies k^*=2$ (stop 2, delay 13.33 s); $\psi_2 = 453.33 \implies T_4 = 680 \ge 453.33 \implies k^*=4 \equiv 0 \pmod 4$ (stop 0, delay 226.67 s). Headways at stop 0 are perfectly uniform at $226.67$ s ($680/3$ s).

#### 11.6 Scenarios & Perturbations
- **Weather (`SIM_SCEN_RAIN_ON` / `SIM_SCEN_RAIN_OFF`)**: Toggles rain factor, modifying road congestion and demand multipliers.
- **Demand Surges (`SIM_SCEN_EVENT_ON` / `SIM_SCEN_EVENT_OFF`)**: Activates a **2.5x** (`+150%`) demand surge at the specified stop (`event_mult = 2.5` in `src/demand_model.c`, called via `st->is_event_active` in `src/sim.c`).
- **Breakdown & Repair (`SIM_SCEN_BREAKDOWN` / `SIM_SCEN_REPAIR`)**: Breaks down a bus, stranding onboard passengers and removing it from route cycle.
- **Road Blockages (`SIM_SCEN_BLOCK_EDGE` / `SIM_SCEN_UNBLOCK_EDGE`)**: Blocks road edges, rerouting bus legs via shortest detour using `RouteManager`.

#### 11.7 Statistics, Warm-Up & Censoring
- **Warm-Up Window**: Passengers arriving during the warm-up period $[t_{\text{start}}, t_{\text{start}} + \text{warmup\_min})$ (default 30 min) are excluded from global and per-route wait-time averages.
- **Right-Censored Wait Times**: Passengers remaining at stops when the simulation terminates are recorded as censored with wait time $t_{\text{end}} - t_{\text{arrival}}$. The global summary reports both boarded wait times and inclusive wait times.
- **Percentiles (P50, P95)**: Exact percentiles computed via a 1-second bin histogram spanning up to 4 hours.

#### 11.8 Predictor Reporting & Fallback Transparency
Both `sim-run` and `sim-compare` CLI commands explicitly log the loading status and file paths of all four candidate demand predictors at the top of execution:
```
[predictor] oracle: loaded (built-in)
[predictor] ma: loaded (../data/demand_train.csv)
[predictor] ols: loaded (../data/demand_train.csv)
[predictor] rf: loaded (../data/demand_rf.txt)
```
**No Silent Fallbacks**: Silent fallback between models is eliminated. In `sim-run`, if the requested predictor (`ma`, `ols`, `rf`, or the default `ma`) cannot be built, the command exits immediately with code 1. In `sim-compare`, if the training data cannot be loaded for `ma`, every row that uses it (`fixed_proportional`, `greedy_spec_ma`, `greedy_marginal_ma`) is skipped and reported as `skipped: training data not loaded: <path>`, and if the random forest model cannot be loaded, the `greedy_marginal_rf` row is skipped and reported as `skipped: model not loaded: <path>`.

#### 11.9 Evaluation Outputs & Single-Seed Disclaimer
> **Important Note on Single-Seed Results**: All `sim-run` and `sim-compare` results documented below and generated by the CLI stem from a single PRNG seed (`seed = 42`). They illustrate individual system trajectories rather than statistically aggregated benchmarks across random seeds. Full 30-seed paired Monte Carlo evaluation is implemented in **Part 3C-2** (`src/eval.c`, `transport_cli eval-run`, Section 13), and the complete 30-seed results are reported in **[../docs/RESULTS.md](../docs/RESULTS.md)**. Furthermore, the steady-state analytic proxy evaluated in `alloc-demo` represents a simplified formula ($W \approx \sum C_r^2 / (2 B_r)$) that assumes deterministic headway, whereas the discrete-event simulation captures stochastic arrivals, vehicle bunching, capacity overflows, and tour headway distortions.

##### Per-Route Comparison: Stop Spacing vs. Time Spacing (Equal Policy, Seed 42, Weekday)
On `seed = 42`, per-route and overall boarded mean wait times under stop spacing and time spacing (`fixed_equal` policy) are:

| Route | Stops | Cycle (s) | Buses | Stop Spacing Wait (s) | Time Spacing Wait (s) | Change (s) |
|:-----:|:-----:|:---------:|:-----:|:---------------------:|:---------------------:|:----------:|
| **R1** | 10 | 4632.8 | 6 | 895.60 | 716.40 | -179.20 |
| **R2** | 10 | 3510.9 | 6 | 1002.42 | 684.82 | -317.60 |
| **R3** | 10 | 3272.9 | 6 | 722.39 | 548.90 | -173.49 |
| **R4** | 10 | 3448.8 | 6 | 1398.64 | 910.14 | -488.50 |
| **R5** | 10 | 3474.3 | 6 | 1291.28 | 736.59 | -554.69 |
| **R6** | 10 | 4802.8 | 6 | 1249.62 | 766.34 | -483.28 |
| **All**| 60 | - | 36 | **1136.50** | **750.54** | **-385.96** |

Under `fixed_equal` on `seed = 42`, full bus events are **136** (`--spacing stop`) vs **68** (`--spacing time`), and P95 wait is **3390.00 s** vs **2130.00 s**.

##### Observations on Policy Mean Wait & Forced Dumps (`seed = 42`, No Scenarios)
- With `--spacing time` and no scenarios (`seed = 42`), `greedy_spec_ma` (**817.70 s**, 10 forced dumps, 4 moves), `greedy_marginal_ma` (**822.53 s**, 45 forced dumps, 7 moves), and `greedy_marginal_oracle` (**822.53 s**, 45 forced dumps, 7 moves) all have a higher mean wait than `fixed_equal` (**750.54 s**, 0 forced dumps, 0 moves), while `greedy_marginal_rf` has **731.68 s** (32 forced dumps, 6 moves) and `fixed_proportional` has **754.38 s** (0 forced dumps, 0 moves).
- In the current `--spacing stop` run with no scenarios (`seed = 42`, [part3b2_results.txt](file:///home/prabhashetty/transport/docs/part3b2_results.txt)), `greedy_spec_ma` (**1207.18 s**, 3 forced dumps, 3 moves), `greedy_marginal_ma` (**1200.11 s**, 13 forced dumps, 7 moves), and `greedy_marginal_oracle` (**1200.11 s**, 13 forced dumps, 7 moves) also have a higher mean wait than `fixed_equal` (**1136.50 s**, 0 forced dumps, 0 moves), while `greedy_marginal_rf` has **1119.12 s** (7 forced dumps, 6 moves) and `fixed_proportional` has **1186.34 s** (0 forced dumps, 0 moves). All observations are from a single seed (`seed = 42`) and the cause is unmeasured; no earlier `--spacing stop` run showing the opposite is present in this repository.
- **Forced Dumps**: When a bus is reassigned mid-route (`pending_route != -2`), `forced_dumps` increments by `b->load` and onboard passengers are removed (`b->load = 0`) without any wait-time penalty or re-queuing.

#### 11.10 CLI Commands
```bash
# Run single simulation with time spacing (default) and drain reassignment (CLI default)
./build/transport_cli sim-run --stops 60 --routes 6 --buses 36

# Run single simulation with legacy stop spacing and dump reassignment
./build/transport_cli sim-run --stops 60 --routes 6 --buses 36 --spacing stop --reassign dump

# Run single simulation with greedy marginal waiting time reduction
./build/transport_cli sim-run --stops 60 --routes 6 --buses 36 --policy greedy-marginal --predictor ma

# Compare all 6 policies under baseline conditions (time spacing, drain reassignment)
./build/transport_cli sim-compare --stops 60 --routes 6 --buses 36

# Compare all 6 policies with legacy dump reassignment
./build/transport_cli sim-compare --stops 60 --routes 6 --buses 36 --reassign dump

# Compare all 6 policies under rain, demand surge, reserve buses, and anomaly dispatch
./build/transport_cli sim-compare --stops 60 --routes 6 --buses 36 \
  --rain-at 08:00 --event-at 17:00 --event-stop 3 --event-len 120 --reserve 3 --anomaly dispatch

# Compare all 6 policies under a mechanical breakdown scenario
./build/transport_cli sim-compare --stops 60 --routes 6 --buses 36 --breakdown-at 09:00 --bus 0
```

#### 11.11 Thread Safety & Determinism Disclaimer
The simulation engine is single-threaded and strictly deterministic. Given the same pseudo-random seed, scenario inputs, and network configuration, identical event sequences and bit-exact statistical metrics are reproduced across all runs. All heap allocations are tracked and freed upon `sim_destroy()`.

---

### 12. Part 3C-1: Simulation Engine Additive Extensions (`sim.h`, `sim.c`)
- **Reassignment Mode (`SimConfig.reassign_mode`)**:
  - `SIM_REASSIGN_DUMP` (`0`, **library default** in `sim_config_default()`): preserves legacy behavior where a bus with a pending reassignment dumps remaining onboard load (`forced_dumps += load`) at its next arrival.
  - `SIM_REASSIGN_DRAIN` (`1`): a bus with a pending reassignment stops boarding, continues its route and alights passengers normally (with dwell `dwell_s + per_pax_s * alighted`), and executes the move (recomputing entry stop and deadhead via `network_entry_time_s` at execution) at the first arrival after alighting where `load == 0`, with `forced_dumps` remaining `0`.
  - **CLI Default Difference**: `transport_cli sim-run` and `sim-compare` accept `--reassign dump|drain` and default to **`drain`**, which is a deliberate difference from the library default `SIM_REASSIGN_DUMP` (`0`) so existing library callers retain legacy behavior unless they opt into `SIM_REASSIGN_DRAIN`.
- **Runtime Control, Inspection & Observer APIs**:
  - `sim_request_move(s, bus_id, to_route)`: queues an administrative reassignment for a `RUNNING` bus with no pending move (always recomputes entry stop and deadhead at execution).
  - `sim_set_policy(s, policy, predictor)` & `sim_force_decision(s, &moves_applied)`: dynamically switches allocation policy (borrowed predictor) and runs an immediate controller decision.
  - `sim_rain_active(s)`, `sim_event_active(s, stop)`, `sim_policy(s)`, `sim_stop_hour_arrivals(s, stop, hour)`: inspect live weather/event state, active policy, and per-stop hourly passenger arrivals.
  - `sim_bus_motion(s, bus, &out)`: retrieves instantaneous dwelling/leg/deadhead motion timestamps and stops.
  - `sim_set_loop_observer(s, fn, ctx)`: registers a callback fired upon completing `m` consecutive legs of a `RUNNING` bus; runs synchronously inside `sim_run_until()` on the caller's thread so the caller's lock covers it.
- **Test Suite**: Added 10 unit/integration tests in `tests/test_sim_ext.c` (23 suites total in `Makefile`). Saved `sim-compare` dump vs. drain outputs in [part3c1_results.txt](file:///home/prabhashetty/transport/docs/part3c1_results.txt).

---

### 13. Part 3C-2: Multi-Seed Evaluation Harness, Cold-Start & Anomaly Validation (`eval.h`, `eval.c`)
- **Statistical Module (`eval.h` / `eval.c`)**:
  - `eval_t_crit_975(df)`: two-sided 95% Student $t$ critical value table (conservative next-lower tabulated `df`).
  - `eval_mean_sd`, `eval_paired_stats`, `eval_poisson_tail_ge`, `eval_median`.
  - Paired scenario runner (`eval_run_single`) with `SIM_SPACING_BY_TIME`, `SIM_REASSIGN_DRAIN`, `graph_reset_traffic`, and blocked-edge assertion before every run.
  - Analytic anomaly expectation (`eval_anomaly_expected_flags`) summing `eval_poisson_tail_ge(lambda_w, k_w)` over the 63 closed windows (`[360, 1305)`) and all 60 stops.
- **CLI Commands & Scripts**:
  - `transport_cli eval-run [--quick] [--seeds S] [--base-seed B] [--scenarios ...] [--policies ...] [--out-dir D] [--sensitivity] [--diagnostic]`: runs paired comparisons, writes `runs_<scenario>.csv`, `summary_<scenario>.csv`, and `summary.json` (LF only, byte-identical across repeated runs).
  - `transport_cli coldstart-prepare` & `coldstart-eval` + [run_coldstart.sh](file:///home/prabhashetty/transport/scripts/run_coldstart.sh) + `--holdout-stops-file` in [train_model.py](file:///home/prabhashetty/transport/ml/train_model.py): holds out 15 of 60 stops (`i % 4 == 3`) from the training CSV while keeping the day split and test CSV unchanged. On the 15 unseen stops (`12,960` test rows across `36` test days), `random_forest` achieved `MAE(lambda) = 1.920` vs `moving_average` `MAE(lambda) = 2.778` (paired per-day `RF - MA` mean `-0.8581`, 95% CI `[-1.0896, -0.6266]`, `35` wins / `0` ties / `1` loss).
  - `transport_cli anomaly-eval [--seeds S] [--thresholds 3,3.5,4,5] [--out-dir D]`: evaluates `no-event` false alarms and `event` (`17:00-19:00` at `stop 13`) detection rate, median delay, and precision, and verifies `cooldown = 0, threshold = 3.0` against the analytic Poisson expectation within 4 SE.
- **Makefile Targets**: `make eval-quick` (5-seed `eval-run --quick`) and `make eval` (full 30-seed `eval-run`, `run_coldstart.sh`, and `anomaly-eval`). Results saved in [part3c2_results.txt](file:///home/prabhashetty/transport/docs/part3c2_results.txt).

---

### 14. Part 3D: Transit Registry Module (`registry.h`, `registry.c`)
- **Hierarchical Tree (`Tree`)**: Root `"Bengaluru"` -> Depots -> Routes (assigned to nearest depot to first tour stop, ties broken by smaller depot index) -> Buses in ascending `bus_id` (plus `RESERVE` buses directly under nearest depot). `registry_sync` mirrors `Sim` bus states, loads, stop coordinates, `DEADHEAD` target routes, and `BROKEN` buses (retaining their last route).
- **Hash Tables (`HashTable`, `HT_KEY_INT`)**: $O(1)$ lookups for `Bus*`, `Route*`, and `Stop*` via `registry_bus`, `registry_route`, `registry_stop`.
- **AVL Timetable (`Avl`, `trip_cmp`)**: Generates periodic departures over `[from_min, to_min)` (`registry_build_timetable`, `registry_replan_from`), supports lower-bound lookups (`registry_next_departure`) and range queries (`registry_trips`).
- **Journey History (`LinkedList`)**: `registry_sim_observer` (`SimLoopFn`, runs under the caller's external lock inside `sim_run_until`) records completed loops per bus capped at `REGISTRY_MAX_JOURNEYS` (200), evicting oldest entries first.
- **Route Priority Board (`Heap`)**: Ranks routes by `demand / (buses + 1)` descending (ties broken by smaller `route_id`).
- **CLI & Tests**: `transport_cli registry-info` output saved in [part3d_results.txt](file:///home/prabhashetty/transport/docs/part3d_results.txt); unit/integration suite in `tests/test_registry.c` (25 test suites total).

---

### 15. Part 4A: REST Server Core (`api_platform`, `api_app`, `api_json`, `api_http`)
- **Platform Layer (`api_platform.h`, `api_platform.c`)**: Isolates POSIX mutexes, threads, monotonic clock, `nanosleep`, and `SIGINT`/`SIGTERM` handlers into a single compilation unit.
- **Application Context & Sim Thread (`api_app.h`, `api_app.c`)**: Owns `Graph`, `DemandStop`s, `NetworkModel` (with matrix), optional Moving Average (`demand_train.csv`) and Random Forest (`demand_rf.txt`) predictors, `Sim`, `Registry` (depots at `"Majestic (Kempegowda BS)"` and `"Shivajinagar"`), simulation lock, generation/geometry counters, and 50 ms background simulation advance loop calling `registry_sync`.
- **JSON Builders & HTTP Glue (`api_json.h`, `api_json.c`, `api_http.h`, `api_http.c`)**: Strict numeric parsing, HTTP error mapping (`400`/`404`/`405`/`409`/`413`/`500`), CORS headers + `OPTIONS 204`, 64 KB body cap, and lock-bounded serialization for all Part 4A read endpoints and `POST /api/sim/control`.
- **Binary, Tests, Docs, & Scripts**:
  - `make server` builds `build/transport_server` (`tools/transport_server.c`, CivetWeb + pthreads + cJSON + libcurl).
  - `tests/test_api_json.c` tests JSON builders, escaping, strict parameter validation, and error mapping without sockets (26 test suites total).
  - `make smoke` runs [api_smoke.sh](file:///home/prabhashetty/transport/scripts/api_smoke.sh); [dump_mock_data.sh](file:///home/prabhashetty/transport/scripts/dump_mock_data.sh) saves mock payloads under `docs/mock/`; [api_contract.md](file:///home/prabhashetty/transport/docs/api_contract.md) documents the HTTP contract.

---

### 16. Part 4B: Control Endpoints, Evaluation Endpoint, Live Traffic Poller & Hardening
- **Control & Action Endpoints**: Added `reset` & `seek` actions to `/api/sim/control`, `POST /api/scenario`, `POST /api/allocate` (policy/predictor switch + manual bus move), `POST /api/inject`, `POST /api/route/optimize` (with `last_optimize` persisted for `/api/metrics`), `POST /api/undo`, and request-time `GET /api/eval` reading `data/eval/summary.json`.
- **Traffic Poller Thread**: Server creates `Sim` with `apply_traffic = 0` and spawns a third thread owning a private `Graph` copy and `TrafficUpdater` (`data/corridors.txt`, `.env` key when live). Every 5 s it reads `(sim_now_min, rain)` under lock, runs `tu_update()` unlocked so HTTP threads never stall on network timeouts, then copies edge `traffic_factor` values onto the shared graph under lock without touching blocked flags. Live traffic overlay is **REAL-TIME data applied to a SIMULATED clock** (`--offline` / `--no-poll` disables live calls).
- **Tests, Stress, Valgrind & TSan**: Added [test_api_actions.c](file:///home/prabhashetty/transport/backend/tests/test_api_actions.c) (27 test suites total), [api_stress.sh](file:///home/prabhashetty/transport/scripts/api_stress.sh), [api_valgrind.sh](file:///home/prabhashetty/transport/scripts/api_valgrind.sh), and `make tsan`; refreshed `docs/mock/` including `state_at_08_00.json` and active blocked/alert/pending-move snapshots.


