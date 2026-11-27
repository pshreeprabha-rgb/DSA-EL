# Smart Public Transport Optimization System — HTTP API Contract (Part 4B)

Base URL (default): `http://127.0.0.1:8080`

All endpoints live under `/api/` and return `Content-Type: application/json`, `Cache-Control: no-store`, explicit `Content-Length`, and CORS headers configured via `--cors` (default `http://localhost:5173`).

---

## 0. Concurrency, Thread Model & Traffic Overlay Note

- **Thread Model (3 concurrent components)**:
  1. **CivetWeb HTTP workers** (`4` threads): parse parameters and up to 64 KB request bodies before locking, acquire `AppCtx.mutex` to read/mutate shared state and serialize JSON, release the mutex, and then write HTTP headers/body to the socket.
  2. **Simulation thread**: wakes every 50 ms of wall time, acquires `AppCtx.mutex`, advances `sim_run_until` by `speed * wall_dt` simulated seconds if running, runs `registry_sync`, bumps `geometry_version` if `rm_history_size` changed, and unlocks.
  3. **Traffic poller thread**: runs every 5 wall seconds; acquires `AppCtx.mutex` briefly only to read `(sim_now_min, rain)`, releases the mutex before calling `tu_update()` on its **private `Graph` copy** (so network I/O never blocks HTTP or sim threads), then briefly acquires `AppCtx.mutex` again to copy `traffic_factor` values onto the shared graph (`blocked` flags untouched) and update cached `/api/state` and `/api/metrics` traffic fields.
- **Traffic Overlay Semantics**: When live traffic fetching is enabled (no `--offline` or `--no-poll`), the TomTom overlay reflects **REAL-TIME traffic data applied to a SIMULATED simulation clock**, layered over `traffic_sim` time-of-day congestion.

---

## 1. Units & Conventions

- **Time units**:
  - `clock_min`, `start_min`, `end_min`, `slice_min`, `departure_min`, `first_flag_min`, `time_min`, journey `start_min`/`end_min`: **minutes since midnight** (e.g. `360` = `06:00`, `1320` = `22:00`).
  - Any field named `*_s` (`depart_s`, `arrive_s`, `time_s`, `mean_wait_boarded_s`, `mean_wait_incl_censored_s`, `p50_s`, `p95_s`, `wait_sum_s`, `wait_max_s`, `deadhead_s_total`, `stalled_s`): **simulated seconds**. Specifically, `bus.motion.depart_s` and `bus.motion.arrive_s` are simulated seconds since midnight (`clock_s = clock_min * 60.0`).
  - Fields named `*_min`: minutes (`loop_min`, `cycle_min`).
- **Coordinates**: decimal degrees `[lat, lng]` (WGS84).
- **Road classes** in `/api/graph` edges `[from, to, road_class]`: integer enum (`0 = primary`, `1 = secondary`, `2 = tertiary`, `3 = other`).
- **Tree node `kind`** in `/api/tree`: integer enum (`0 = CITY`, `1 = DEPOT`, `2 = ROUTE`, `3 = BUS`).
- **Numbers**: plain JSON numbers (finite floats or integers); nullable optional predictors/metrics return `null`.

---

## 2. Headers, Preflight & Error Envelope

### Response Headers (every response)

- `Content-Type: application/json`
- `Cache-Control: no-store`
- `Content-Length: <bytes>`
- `Access-Control-Allow-Origin: <configured origin>`
- `Access-Control-Allow-Methods: GET, POST, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type`
- `Access-Control-Max-Age: 600`

Preflight `OPTIONS /api/*` returns `204 No Content` with `Content-Length: 0`.

### Error Envelope

```json
{"error":{"code":"<code_string>","message":"<reason>"}}
```

| HTTP Status | `error.code` | Cause |
|---|---|---|
| `400` | `invalid_argument` | Malformed JSON, non-numeric/out-of-range IDs or query params, invalid control action/speed |
| `404` | `not_found` | Unknown route path, unknown `bus/:id`, or out-of-range `stop` index |
| `405` | `method_not_allowed` | Disallowed HTTP method on existing endpoint (includes `Allow` header) |
| `409` | `conflict` | Domain state conflict (`STATUS_ERR_EMPTY` / `STATUS_ERR_DUPLICATE`) |
| `413` | `payload_too_large` | Request body larger than 64 KB (`65536` bytes) |
| `500` | `internal` | Allocation or internal server error |

---

## 3. Polling & Map Interpolation Guidance

- **Startup (once)**: fetch `/api/config`, `/api/graph` (cache geometry statically for the session), `/api/stops`, `/api/geometry`.
- **Live tick (1 Hz)**: poll `/api/state`. Whenever `state.geometry_version` differs from the client's cached `geometry.version`, re-fetch `/api/geometry` and `/api/history`.
- **Secondary views**: poll or fetch `/api/routes`, `/api/metrics`, `/api/alerts`, `/api/tree`, `/api/schedule`, and `/api/demand/forecast?stop=IDX` when their tab/drawer is visible or upon `state` transitions.

### Bus Motion Interpolation Rule

For each bus in `/api/state`:
1. Let `clock_s = state.clock_min * 60.0`.
2. If `bus.motion.moving == true` and `bus.motion.arrive_s > bus.motion.depart_s`:
   - Compute `t = clamp((clock_s - bus.motion.depart_s) / (bus.motion.arrive_s - bus.motion.depart_s), 0.0, 1.0)`.
   - Look up leg `(leg_route, leg_index)` in `geometry.legs` matching `route == bus.motion.leg_route && leg == bus.motion.leg_index` (if `leg_index >= 0`), or interpolate straight between `stops[from_stop]` and `stops[to_stop]` for `DEADHEAD` legs (`leg_index == -1`).
   - Interpolate `[lat, lng]` at arc-length fraction `t` along the leg's polyline `path`.
3. If `bus.motion.moving == false`: place marker directly at `stops[bus.stop_idx]` (`[lat, lng]`).

---

## 4. Endpoints

### `GET /api/health`
- **Example**:
```json
{"ok":true,"sim_running":false}
```
- **Errors**: `405` if method is not `GET`/`OPTIONS`.

### `GET /api/config`
- **Example** (from `docs/mock/config.json`):
```json
{
    "stops": 60,
    "routes": 6,
    "buses": 36,
    "reserve": 3,
    "seed": 42,
    "start_min": 360,
    "end_min": 1320,
    "slice_min": 15,
    "policy": "fixed_equal",
    "predictor": "ma",
    "spacing": "time",
    "reassign": "drain",
    "models": {"ma": true, "rf": true},
    "offline": true
}
```
- **Errors**: `405`.

### `GET /api/graph`
- **Notes**: Compact array representation sent once (`nodes`: `[lat, lng, is_stop]`, `edges`: `[from, to, road_class]`).
- **Example** (truncated from `docs/mock/graph.json`):
```json
{
    "nodes": [[12.9838341, 77.5779982, 0], [12.9844674, 77.5784439, 0]],
    "edges": [[0, 1, 0], [1, 0, 0]]
}
```
- **Errors**: `405`.

### `GET /api/stops`
- **Example** (truncated from `docs/mock/stops.json`):
```json
{
    "stops": [
        {
            "idx": 0,
            "node": 7,
            "name": "Military Accounts Office",
            "type": "market",
            "scale": 0.640625,
            "lat": 12.9653451,
            "lng": 77.6175664,
            "route": 4,
            "pos": 6,
            "event_active": false,
            "first_flag_min": -1
        }
    ]
}
```
- **Errors**: `405`.

### `GET /api/routes`
- **Example** (truncated from `docs/mock/routes.json`):
```json
{
    "hour": 6,
    "basis": "forecast",
    "routes": [
        {
            "id": 0,
            "name": "R1: Timber Yard Layout - Ganesha Temple",
            "stops": [46, 21, 57, 42, 40, 28, 43, 7, 45, 31],
            "loop_min": 73.88070748210002,
            "cycle_min": 77.21404081543336,
            "buses": 6,
            "demand": 88.42105263157895,
            "priority": 12.631578947368421
        }
    ],
    "board": [
        {"route_id": 1, "priority": 20.879699248120307, "demand": 146.15789473684214, "buses": 6}
    ]
}
```
- **Errors**: `405`.

### `GET /api/geometry`
- **Example** (truncated from `docs/mock/geometry.json`):
```json
{
    "version": 1,
    "legs": [
        {
            "route": 0,
            "leg": 0,
            "from_stop": 46,
            "to_stop": 21,
            "time_s": 1128.393842161,
            "path": [[12.9479097, 77.5430024], [12.948204, 77.5432552]]
        }
    ],
    "blocked": []
}
```
- **Errors**: `405`.

### `GET /api/state`
- **Example** (truncated from `docs/mock/state.json`):
```json
{
    "clock_min": 360,
    "clock": "06:00",
    "running": false,
    "finished": false,
    "speed": 60,
    "day_type": "weekday",
    "rain": false,
    "policy": "fixed_equal",
    "traffic_source": "simulated",
    "geometry_version": 1,
    "history_size": 0,
    "buses": [
        {
            "id": 0,
            "state": "running",
            "route": 0,
            "stop_idx": 46,
            "load": 0,
            "pending_route": -2,
            "motion": {
                "moving": false,
                "leg_route": 0,
                "leg_index": -1,
                "from_stop": 46,
                "to_stop": 46,
                "depart_s": 21600,
                "arrive_s": 21600
            }
        }
    ],
    "queues": [{"stop": 0, "waiting": 0}],
    "summary": {
        "arrived": 0,
        "boarded": 0,
        "still_waiting": 0,
        "mean_wait_boarded_s": 0,
        "mean_wait_incl_censored_s": 0,
        "p95_s": -1
    }
}
```
- **Errors**: `405`.

### `GET /api/bus/:id`
- **Path parameter**: `id` (strict non-negative integer).
- **Example**:
```json
{"id":0,"state":"running","route":0,"stop_idx":46,"load":0,"capacity":60,"depot":0,"journeys":[]}
```
- **Errors**: `400 invalid_argument` (non-numeric/negative ID), `404 not_found` (unknown bus ID), `405`.

### `GET /api/schedule?from=&to=&route=`
- **Query parameters**:
  - `from` (optional integer in `[0, 1440)`, default `floor(clock_min)`)
  - `to` (optional integer in `(from, 1440]`, default `min(from + 120, 1440)`)
  - `route` (optional integer in `[-1, n_routes - 1]`, default `-1` = all routes)
  - Returns up to `500` trips in ascending `(departure_min, trip_id)` order.
- **Example** (truncated from `docs/mock/schedule.json`):
```json
{
    "from": 360,
    "to": 480,
    "count": 67,
    "avl": {"size": 506, "height": 10, "balanced": true},
    "trips": [
        {"trip_id": 1, "route_id": 0, "bus_id": 0, "departure_min": 360, "departure": "06:00"}
    ]
}
```
- **Errors**: `400 invalid_argument` (bad integer format, `from >= to`, out-of-range minute or route), `405`.

### `GET /api/tree`
- **Example** (truncated from `docs/mock/tree.json`):
```json
{
    "count": 45,
    "height": 3,
    "nodes": [
        {"row": 0, "parent": -1, "depth": 0, "kind": 0, "id": 0, "name": "Bengaluru"},
        {"row": 1, "parent": 0, "depth": 1, "kind": 1, "id": 0, "name": "Majestic (Kempegowda BS)"}
    ]
}
```
- **Errors**: `405`.

### `GET /api/demand/forecast?stop=IDX`
- **Query parameter**: `stop` (required strict non-negative integer).
- **Example** (truncated from `docs/mock/forecast_stop3.json`):
```json
{
    "stop": 3,
    "name": "Gandhi Bazaar, Basavanagudi",
    "type": "residential",
    "day_type": "weekday",
    "rain": false,
    "event_active": false,
    "hours": [
        {"hour": 0, "oracle": 0.4376486534119897, "ma": 0.368421052631579, "rf": 0.5142477184350434, "observed": 0}
    ]
}
```
- **Errors**: `400 invalid_argument` (missing or non-numeric/negative `stop`), `404 not_found` (`stop >= n_stops`), `405`.

### `GET /api/metrics`
- **Example** (truncated from `docs/mock/metrics.json`):
```json
{
    "sim": {
        "arrived": 0, "boarded": 0, "still_waiting": 0, "wait_sum_s": 0, "wait_max_s": 0,
        "mean_wait_boarded_s": 0, "mean_wait_incl_censored_s": 0, "p50_s": -1, "p95_s": -1,
        "full_bus_events": 0, "forced_dumps": 0, "stranded_by_breakdown": 0, "bus_arrivals": 0,
        "moves_applied": 0, "deadhead_s_total": 0, "anomaly_flags": 0, "emergency_dispatches": 0,
        "stalled_s": 0, "mean_load_factor": 0, "decisions": 0, "decisions_applied": 0
    },
    "routes": [{"route": 0, "boarded": 0, "still_waiting": 0, "mean_wait_s": 0, "max_wait_s": 0}],
    "ml": {"train_rows": 120960, "test_rows": 51840, "rf_mae": 2.4616830249977224},
    "last_optimize": null,
    "traffic": {"source": "simulated", "calls": 0, "live_corridors": 0, "last_error": ""}
}
```
- **Errors**: `405`.

### `GET /api/alerts`
- **Example** (from `docs/mock/alerts.json`):
```json
{
    "flags": [],
    "log": ["[06:00:00] sim created: 6 routes, 60 stops, 36 buses (3 reserve)"]
}
```
- **Errors**: `405`.

### `GET /api/history`
- **Example** (from `docs/mock/history.json`):
```json
{"size": 0, "entries": []}
```
- **Errors**: `405`.

### `POST /api/sim/control`
- **Request Body** (`<= 65536` bytes):
```json
{"action": "start|pause|resume|speed|reset|seek", "speed": 120, "time_min": 480}
```
  - `action`: optional string `"start"`, `"pause"`, `"resume"`, `"speed"`, `"reset"`, or `"seek"` (required if `speed` omitted).
  - `speed`: optional finite number in `[1, 3600]` (accepted with any action; required if `action` is `"speed"` or omitted).
  - `time_min`: required with `"seek"`; finite multiple of `slice_min` inside `[start_min, end_min]`. `"seek"` resets state, advances to `time_min` under lock, and leaves `running: false`. `"reset"` drains `RouteManager` undo history, resets traffic/blocked edges, recreates sim/registry cleanly with `running: false`, and bumps `geometry_version`.
- **Example Response (`200 OK`)**:
```json
{"ok":true,"clock_min":480,"running":false,"speed":60}
```
- **Errors**: `400 invalid_argument` (malformed JSON, unknown action, invalid `speed`, non-slice-multiple or out-of-window `time_min`), `405` (`Allow: POST, OPTIONS`), `413 payload_too_large`.

### `POST /api/scenario`
- **Request Body**:
  - Rain: `{"kind": "rain_on"}` or `{"kind": "rain_off"}`
  - Event: `{"kind": "event_on"|"event_off", "stop": 3}`
  - Breakdown/repair: `{"kind": "breakdown"|"repair", "bus": 0}`
  - Edge block/unblock: `{"kind": "block_edge"|"unblock_edge", "edge": 3614}` or `{"kind": "block_edge"|"unblock_edge", "route": 0, "leg": 0}` (resolves first edge of leg's current path).
  - Schedules at `ceil(now_min / slice_min) * slice_min` (may equal `now_min`).
- **Example Response (`200 OK`)**:
```json
{"scheduled":true,"time_min":480,"kind":"block_edge"}
```
- **Errors**: `400 invalid_argument` (unknown kind, bad IDs, malformed/mixed fields), `409 conflict` (next slice is at/past `end_min`, or target leg currently has no path), `405`, `413`.

### `POST /api/allocate`
- **Request Body** (either policy update or manual bus move):
  - Policy: `{"policy": "equal|proportional|greedy_spec|greedy_marginal", "predictor": "oracle|ma|rf", "force": false}` (`predictor` optional; unknown or unloaded predictor strictly returns `400`, never a silent substitute).
  - Manual move: `{"manual_move": {"bus": 0, "to_route": 1}}`
- **Example Responses (`200 OK`)**:
```json
{"policy":"greedy_spec","decision":{"forced":true,"moves_applied":1},"counts":[7,6,5,5,5,5]}
```
```json
{"moved":{"bus":0,"to_route":1}}
```
- **Errors**: `400 invalid_argument` (unknown/unloaded predictor, bad bus/route ID, bus not RUNNING or already has pending move, forcing on FIXED_* policy), `405`, `413`.

### `POST /api/inject`
- **Request Body**: `{"stop": 3, "count": 250, "ride": 1}` (`count` integer `1..500`; `ride` optional non-negative integer, `0` = random).
- **Example Response (`200 OK`)**:
```json
{"stop":3,"count":250,"time_s":28800}
```
- **Errors**: `400 invalid_argument` (bad stop, count outside `1..500`, invalid `ride`, injection past slice window), `405`, `413`.

### `POST /api/route/optimize`
- **Request Body**: `{"from_stop": 0, "to_stop": 5, "algo": "astar|dijkstra|both", "mode": "traffic|static"}`
- **Notes**: Evaluates shortest path on live `Graph` under lock and stores result in `last_optimize` for `/api/metrics`. `nodes_ratio` appears only when `algo` is `"both"`.
- **Example Response (`200 OK`)** (from `docs/mock/optimize.json`, path truncated):
```json
{
    "mode": "traffic",
    "astar": {
        "found": true,
        "time_s": 641.182110184,
        "length_m": 5053.6476748,
        "nodes_explored": 525,
        "time_static_s": 641.182110184,
        "time_traffic_s": 641.182110184,
        "path": [[12.9653451, 77.6175664], [12.9664386, 77.6141794]]
    },
    "dijkstra": {
        "found": true,
        "time_s": 641.182110184,
        "length_m": 5053.6476748,
        "nodes_explored": 1419,
        "time_static_s": 641.182110184,
        "time_traffic_s": 641.182110184,
        "path": [[12.9653451, 77.6175664], [12.9664386, 77.6141794]]
    },
    "nodes_ratio": 0.36997885835095137
}
```
- **Errors**: `400 invalid_argument` (bad stop index, unknown `algo` or `mode`), `405`, `413`.

### `POST /api/undo`
- **Request Body**: `{}` or empty body. Undoes top `RouteManager` ChangeSet and bumps `geometry_version`.
- **Example Response (`200 OK`)**:
```json
{"undone":"block edge 3614","history_size":0,"geometry_version":3}
```
- **Errors**: `409 conflict` (history stack empty), `400 invalid_argument` (malformed non-empty body), `405`, `413`.

### `GET /api/eval`
- **Notes**: Reads `<data_dir>/eval/summary.json` directly on each request; never reads any other path.
- **Example Response (`200 OK`)** (truncated from `docs/mock/eval.json`):
```json
{
    "config": {"seeds": 30, "base_seed": 1000, "stops": 60, "routes": 6, "buses": 36},
    "scenarios": [{"name": "normal", "metric": "mean_wait_incl_censored_s"}]
}
```
- **Errors**: `404 not_found` (file absent), `500 internal` (malformed JSON), `405`.

