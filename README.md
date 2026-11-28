# Smart Public Transport Fleet & Route Optimization System (Bengaluru BMTC)

A C11 simulation and optimization engine paired with a scikit-learn ML demand pipeline and a React + TypeScript + Leaflet operational dashboard for central Bengaluru bus transit.

## Architecture Overview
- **C11 Core & HTTP Server (`backend/`)**: Custom generic data structures (`LinkedList`, `Stack`, `Queue`, `Tree`, `Heap`, `AVL`, `HashTable`), OSM graph & Tarjan SCC extractor, A* / Dijkstra routing, `RouteManager` atomic detour/undo stack, diurnal/live TomTom traffic model, Poisson demand & RF inference engine, discrete-event fleet simulator, multi-seed paired evaluator, and CivetWeb REST API server (`transport_server`).
- **ML Pipeline (`ml/`)**: Python scikit-learn `RandomForestRegressor` training, deterministic export to `data/demand_rf.txt`, and golden parity predictions verified in C within `1e-6`.
- **Interactive UI (`frontend/`)**: Vite + React 19 + TypeScript dashboard featuring a decoupled `React.memo` Leaflet live bus map (`100 ms` interpolation, fixed `"Road blocked"` markers), controls console, 24h demand/anomaly viewer, data-structure inspector, and multi-seed policy comparison page.

## Quick Start
```bash
# Run all backend + frontend unit checks
make check

# Start backend HTTP server (offline simulated traffic, paused at 06:00, fixed seed)
make server
backend/build/transport_server --port 8080 --data-dir data --seed 42 --paused --offline

# In a second terminal, start the frontend UI on http://localhost:5173
cd frontend && npm ci && npm run dev
```

Or run the frontend standalone with the animated mock server (`npm run mock` inside `frontend/`).

## Documentation & Results
- Setup & operations: [docs/SETUP.md](docs/SETUP.md)
- System architecture & data-structure map: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- Quantitative results bundle: [docs/RESULTS.md](docs/RESULTS.md)
- Live 8-step demo script: [docs/DEMO.md](docs/DEMO.md)
- REST contract & frontend notes: [docs/api_contract.md](docs/api_contract.md), [docs/frontend_notes.md](docs/frontend_notes.md)
- Data & library credits: [docs/ATTRIBUTION.md](docs/ATTRIBUTION.md)

## Known Limitations
- **Scope & simulation assumptions**: Evaluation runs on a single Bengaluru network (`60` stops, `6` routes, `36` buses) and on weekdays only (`DAY_WEEKDAY`). Arrivals, passenger behavior, and baseline diurnal traffic are simulated; live TomTom flow telemetry overlays only 8 mapped corridors when enabled.
- **Rain-event anomaly-detector predictor**: In `rain-event` (evaluated with `3` reserve buses, whereas `normal` and `breakdown` use `0` reserve buses), `fixed_equal`, `fixed_proportional`, and `greedy_marginal_oracle` use the true demand model (`predictor = NULL`) for anomaly detection, `greedy_spec_ma` and `greedy_marginal_ma` use the moving average, and `greedy_marginal_rf` uses the random forest — confounding cross-policy comparisons in `rain-event`. Scenario means should not be compared across scenarios because reserve bus counts differ.
- **Unexplained sensitivity plateau**: Sensitivity runs for `Hyst 0.10` and `0.25` at reallocation intervals `30` and `60` min produced identical results in both `normal` and `rain-event`, and that identical output is unexplained.
- **In-flight HTTP shutdown**: Server shutdown during an active HTTP request handler was not shown to be tested.
- **Live browser measurements**: Browser DevTools 60 s Performance main-thread busy-% and 5-minute console error checks count as verified only if supplied by a desktop browser run (no headless Chromium/Playwright binary was installed in WSL).
