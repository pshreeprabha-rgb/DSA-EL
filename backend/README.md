# Smart Public Transport Fleet & Route Optimization System (Bangalore BMTC)
## Parts 1, 2, 3A & 3B-1: Road Graph, Routing, Demand Modelling, ML Inference, Fleet Allocation & Anomaly Core

### 1. Project Overview
The Smart Public Transport Fleet & Route Optimization System is an intelligent route planning, fleet scheduling, demand prediction, and live simulation platform designed for Bangalore's bus transit network (BMTC).
- **Part 1**: Reusable C11 data structure library (`LinkedList`, `Stack`, `Queue`, `Tree`, `Heap`, `AVL`, `HashTable`, and shared domain types).
- **Part 2**: Complete road network graph engine, A* and Dijkstra pathfinding with consistent heuristics, traffic-aware rerouting with atomic undo stack, Gaussian diurnal traffic simulation, live TomTom traffic flow integration with robust `.env` parsing, and OpenStreetMap Overpass road/stop ingestion.
- **Part 3A**: Strongly Connected Components analysis (`graph_scc`), diurnal passenger demand generation with Poisson sampling (`demand_model`), scikit-learn Random Forest C inference engine (`ai_inference`), and three demand predictors (Moving Average, OLS per time-band with ridge regularisation, and Random Forest adapter) with unified evaluation framework (`ai_demand`).

Engineered with zero warnings under `-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`, verified with AddressSanitizer, UndefinedBehaviorSanitizer, and Valgrind (zero leaks, zero memory errors).

---

### 2. Folder Structure
```text
project/
  backend/
    include/
      common.h         # Status codes, status_to_string, shared callback function pointer types
      types.h          # Domain entity models (Stop, Route, Bus, Trip, Edge, Passenger, etc.)
      linkedlist.h     # Doubly linked list with head, tail, and size tracking
      stack.h          # Dynamic array LIFO stack with 0-indexed top access
      queue.h          # Circular-buffer FIFO queue with wrap-around resizing
      tree.h           # General tree (first-child / next-sibling representation)
      heap.h           # Binary heap with dynamic increase/decrease-key indexing
      avl.h            # Self-balancing AVL tree with LL/RR/LR/RL rotations
      hashtable.h      # Separate chaining hash table (int64_t and string keys)
      graph.h          # Road network graph (nodes, edges, haversine, components, cache I/O)
      pathfind.h       # A* and Dijkstra pathfinding with PathResult and cost modes
      reroute.h        # RouteManager with route tracking, block/reroute, and undo stack
      traffic_sim.h    # Diurnal traffic simulation model (Gaussian rush-hour bumps)
      traffic_client.h # TomTom live traffic client, URL builder/redacter, .env loader
      osm_loader.h     # OpenStreetMap Overpass road parser, stop snapper, and cache loader
      graph_scc.h      # Tarjan's SCC decomposition and largest SCC extractor
      demand_model.h   # Inhomogeneous Poisson demand generator and anchored stop selector
      ai_inference.h   # Random Forest C inference engine (scikit-learn parity)
      ai_demand.h      # Demand predictors (MA, OLS band, RF) and evaluation metrics
      network.h        # Bus network model, strip partitioning, and travel-time matrix
      fleet_alloc.h    # Fleet allocation (Equal, Hamilton, Greedy) and move planner
      anomaly.h        # Real-time Poisson demand anomaly detector and cooldown gating
    src/
      common.c         # Implementation of status code to string mappings
      types.c          # Constructors, destructors, and entity comparator helpers
      linkedlist.c     # Doubly linked list implementation
      stack.c          # Stack implementation
      queue.c          # Queue implementation
      tree.c           # General tree implementation
      heap.c           # Binary heap implementation
      avl.c            # AVL tree implementation
      hashtable.c      # Separate chaining hash table implementation
      graph.c          # Graph implementation (adjacency lists, spatial search, components)
      pathfind.c       # A* and Dijkstra pathfinding implementations
      reroute.c        # RouteManager implementation
      traffic_sim.c    # Traffic simulation model implementation
      traffic_client.c # TomTom traffic client and .env loader implementation
      osm_loader.c     # OSM Overpass loader and stop snapper implementation
      graph_scc.c      # Tarjan's SCC implementation
      demand_model.c   # Diurnal demand generation and anchored stop selection
      ai_inference.c   # Random Forest parser and inference traversal
      ai_demand.c      # Moving Average, OLS band with ridge regularisation, RF adapter
      network.c        # Network construction, nearest-neighbour tour, distance matrix
      fleet_alloc.c    # Fleet allocation algorithms and move planner
      anomaly.c        # Anomaly detector implementation
    tests/
      test_util.h      # Minimalist assertion and test-runner macro framework
      test_linkedlist.c# Linked list test suite
      test_stack.c     # Stack test suite (1000-element stress test)
      test_queue.c     # Queue test suite (wrap-around growth test)
      test_tree.c      # Tree test suite (3-level hierarchy and subtree deletion)
      test_heap.c      # Heap test suite (min-heap, max-heap, decrease-key)
      test_avl.c       # AVL test suite (1000-element balance, 500 deletions, rotations)
      test_hashtable.c # Hash table test suite (10000-element resizing, collisions)
      test_types.c     # Entity constructors, destructors, and priority heap tests
      test_graph.c     # Graph creation, edge/node ops, spatial queries, save/load
      test_pathfind.c  # Hand-verified routing, Dijkstra vs A*, exploration counts
      test_reroute.c   # Route tracking, edge blocking, rerouting, atomic undo
      test_traffic_sim.c # Diurnal curves, rain multiplier, determinism
      test_traffic_client.c # URL parsing, flow JSON, live mock, .env loader
      test_osm_loader.c # OSM junction detection, road/stop snapping, LCC extraction
      test_graph_scc.c # Tarjan SCC unit tests
      test_demand_model.c # Demand model unit tests
      test_ai_inference.c # AI inference unit tests
      test_ai_demand.c # Demand predictors unit tests
      test_network.c   # Network model unit tests
      test_fleet_alloc.c # Fleet allocation unit tests
      test_anomaly.c   # Anomaly detection unit tests
    tools/
      transport_cli.c  # Command-line utility for demo, info, route, bench, osm, traffic
    Makefile           # Strict build system (all, test, asan, valgrind, cli, clean)
    .env.example       # Example environment template with mock API key
    README.md          # Comprehensive project documentation
  data/
    corridors.txt      # Central Bangalore traffic corridors with coordinates
    graph_cache.txt    # Cached Bangalore road graph (4,765 nodes, 8,156 edges)
    test_graph.txt     # 20-node hand-verified central Bangalore reference graph
  docs/
    .gitkeep
  .gitignore
```

---

### 3. Build and Test Commands
The build system uses GNU Make with strict compiler flags (`-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -g -Iinclude`). External libraries used: `libcurl` and `libcjson`.

```bash
cd project/backend

# Build the static library (build/libtransport_core.a)
make all

# Build and execute all 21 test suites
make test

# Build and run all 21 test suites with AddressSanitizer and UndefinedBehaviorSanitizer
make asan

# Run memory leak and access checks under Valgrind
make valgrind

# Build the transport_cli tool
make cli

# Clean all generated build artifacts and test binaries
make clean
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
All Part 1 data structures and Part 2 graph, pathfinding, and updater modules are **NOT thread-safe**. External synchronization (e.g. read-write mutexes) must be applied when accessed across multiple threads in subsequent microservices (Part 4).

---

### 7. Architectural Mapping to Future Project Parts
| Module | Role in Parts 3 - 5 |
| :--- | :--- |
| **`graph` & `pathfind`** | Core routing engine for BMTC bus navigation and schedule estimation. |
| **`reroute`** | Powers the dynamic dispatch and disruption management engine in Part 3 and the undo/redo REST actions in Part 4. |
| **`traffic_sim` & `traffic_client`** | Generates ambient city congestion for simulation ticks and live operational overlays. |
| **`osm_loader`** | Ingests real BMTC routes and stop topologies for central Bangalore. |

---

### 8. Error Codes & REST Mapping Contract
Part 4 maps these core C status codes to standard HTTP REST status codes:

| Library Status | Meaning & Preconditions | Future HTTP Status (Part 4) |
| :--- | :--- | :--- |
| **`STATUS_OK` (0)** | Operation succeeded without errors. | `200 OK` / `201 Created` |
| **`STATUS_ERR_NULL` (1)** | Required pointer argument is `NULL`. | `400 Bad Request` |
| **`STATUS_ERR_NOMEM` (2)** | Memory allocation (`malloc`/`calloc`/`realloc`) failed. | `500 Internal Server Error` |
| **`STATUS_ERR_NOT_FOUND` (3)** | Requested resource, route, or file not found. | `404 Not Found` |
| **`STATUS_ERR_DUPLICATE` (4)** | Identifier already exists (e.g. duplicate route ID). | `409 Conflict` |
| **`STATUS_ERR_EMPTY` (5)** | Container is empty (e.g. `rm_undo` on empty stack). | `400 Bad Request` |
| **`STATUS_ERR_INVALID` (6)** | Out-of-range index, invalid coordinates, NaN, or malformed file/JSON. | `422 Unprocessable Entity` |

---

### 9. Part 3A: Passenger Demand Modelling & C Inference Engine

#### 9.1 Mathematical Formulation
The demand model simulates realistic passenger arrivals at BMTC bus stops using an inhomogeneous Poisson process:
$$\lambda(\text{stop}, t) = \text{base}(\text{type}) \times \text{scale} \times \text{profile}(\text{type}, \text{hour}) \times \text{daymult} \times \text{rain\_mult} \times \text{event\_mult}$$

- **Stop Profiles & Base Rates**:
  - `STOP_HUB` (Majestic, Shivajinagar): Base 40 pax/hr. Morning peak 1.8x, evening peak 2.2x, midday 1.2x.
  - `STOP_OFFICE` (CBD, Whitefield, Electronics City): Base 35 pax/hr. Morning peak 2.5x, evening peak 2.2x, midday 0.8x.
  - `STOP_MARKET` (KR Market, Commercial St): Base 30 pax/hr. Midday peak 1.8x, evening peak 2.0x, morning 0.9x.
  - `STOP_RESIDENTIAL` (Jayanagar, Malleshwaram): Base 20 pax/hr. Morning outbound peak 2.2x, evening return peak 1.8x.
- **Multipliers**:
  - Rain: $+20\%$ surge ($\times 1.20$).
  - Events (matches, concerts): $+150\%$ surge ($\times 2.50$) between 17:00 and 20:00 at 3 designated stops.
  - Day Type: Weekday ($1.0\times$), Weekend ($0.7\times - 1.2\times$ depending on zone), Holiday ($0.5\times - 1.3\times$).
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
- **Threshold & Gating Rationale**:
  - `z_threshold = 3.0`: Under the null hypothesis of unperturbed Poisson arrivals, $P(Z > 3.0) < 0.0035$, ensuring an empirical false alarm rate of $\approx 0.35\%$ across independent windows.
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
  - **Non-Disruptive Move Execution**:
    - Buses selected for reassignment are marked with `pending_route`.
    - The bus completes its current leg to avoid stranding on active roads.
    - Upon arrival at its next stop, remaining onboard passengers are forced-dumped (`forced_dumps += load`), and the bus transitions to `SIM_BUS_DEADHEAD` heading to the entry stop of its new route.
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
- **Demand Surges (`SIM_SCEN_EVENT_ON` / `SIM_SCEN_EVENT_OFF`)**: Activates 3.0x demand surge at specific stops.
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
> **Important Note on Single-Seed Results**: All `sim-run` and `sim-compare` results documented below and generated by the CLI stem from a single PRNG seed (`seed = 42`). They illustrate individual system trajectories rather than statistically aggregated benchmarks across random seeds. Comprehensive multi-seed Monte Carlo evaluation across varied traffic conditions and seeds is formally reserved for Part 3C. Furthermore, the steady-state analytic proxy evaluated in `alloc-demo` represents a simplified formula ($W \approx \sum C_r^2 / (2 B_r)$) that assumes deterministic headway, whereas the discrete-event simulation captures stochastic arrivals, vehicle bunching, capacity overflows, and tour headway distortions.

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


