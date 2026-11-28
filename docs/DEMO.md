# Live Operations Demo Script (`docs/DEMO.md`)

## Pre-Demo Launch
Start the C backend with cached graph, fixed seed (`42`), paused clock, and explicit offline or live mode:
```bash
# Terminal 1 (Backend, offline rehearsal or fallback)
cd ~/transport
backend/build/transport_server --port 8080 --data-dir data --seed 42 --paused --offline
```
```bash
# Terminal 2 (Frontend UI)
cd ~/transport/frontend
npm run dev
```
Open `http://localhost:5173`.

---

## 8-Step Walkthrough

### 1. Start Normal Operation
- **Clicks**: Confirm the topbar shows `06:00`, `Paused`, `fixed_equal`, and the `Simulated` (or `Live`) badge. In **Controls** (or topbar), click **Start**.
- **What the audience must see**: All 6 color-coded Bengaluru routes, 60 stop markers by land-use type (`hub`, `office`, `market`, `residential`), and numbered bus pills moving smoothly every `100 ms` along road polylines, with footer attribution (`OpenStreetMap contributors`, `Simulated demand`, and `TomTom live traffic` when online).

### 2. Jump to 08:00 Morning Rush & Switch to Greedy Marginal
- **Clicks**: Open the **Controls** tab -> enter `08:00` in **Seek (HH:MM)** -> click **Seek** -> select `300x` speed -> click **Resume**. Switch back to **Dashboard** for 5 seconds, then return to **Controls** -> select policy **`greedy_marginal`** (predictor **`ma`** or **`rf`**) -> click **Re-allocate now**.
- **What the audience must see**: Waiting queues grow sharply on morning commuter stops (`Top 15 Longest Stop Queues` bar chart updates in place), the **Route Priority Board** ranks high-demand corridors (`R4`, `R2` at top in heap order), and clicking **Re-allocate now** shifts idle capacity toward high-priority routes cleanly via the allocation table and toast confirmation.

### 3. Rain On
- **Clicks**: In **Controls**, click **Rain On**.
- **What the audience must see**: Toast confirms scheduling at the next 15-minute slice (`time_min`); once active, `rain` appears in the topbar pill and road speeds slow, driving higher wait times and queue build-up.

### 4. Block a Road
- **Clicks**: In **Controls**, pick `Block Route` (e.g., `R1`) and `Leg 0` -> click **Block Road**.
- **What the audience must see**:
  - A toast appears showing the scheduled slice time (`Road block scheduled — applies at HH:MM (<time_min> min)`).
  - Once the simulation crosses that 15-minute slice, `geometry_version` increments, the map **automatically pans** to the blocked midpoint, a **red dashed line** renders **above** the route polylines, and a fixed-size **`Road blocked`** red badge marks its exact midpoint cleanly at every zoom level.
  - Rerouted geometry detours around the blocked segment while the **Undo Stack** lists the new ChangeSet at `TOP`.

### 5. Undo
- **Clicks**: Switch briefly to **Data structures -> History (Stack)** to highlight `TOP`, then return to **Controls** and click **Undo**.
- **What the audience must see**: Toast reports the undone action (`history_size` decrements), the `TOP` item is popped from the history stack, the red blocked segment and `Road blocked` marker disappear from the map, and routes restore.

### 6. Breakdown & Recovery
- **Clicks**: In **Controls**, pick an active bus (e.g., `Bus 0`) -> click **Breakdown**. Wait one slice, inspect **Dashboard -> Fleet Allocation**, then optionally click **Repair**.
- **What the audience must see**: Bus pill changes color and status text to **`broken`** and pauses on screen; the allocation table reflects `Broken / out of service` and reserve pool updates.

### 7. Hub Demand Burst & Anomaly Alert
- **Clicks**: In **Controls**, choose a hub stop under **Inject Stop**, set **Count** to `250`, click **Inject Burst** (and optionally **Event On**). Open the **Demand** tab -> click the new anomaly alert card at the bottom.
- **What the audience must see**:
  - The 24h **Demand** chart shows the `Now` vertical line, observed spike vs oracle/RF, and the visible note confirming **the moving average ignores events**.
  - Clicking the alert selects that stop in the Demand dropdown, highlights the stop marker, and **pans the map** straight to the affected hub.

### 8. Scientific Comparison Page
- **Clicks**: Open the **Comparison** tab (scroll through Scenarios, Cold-Start Holdout, Anomaly Thresholds, Random Forest MAE, and the disclaimer box).
- **What the audience must see**:
  - Paired multi-seed scenario bar charts (`± 1 SD`) with paired wait differences, 95% confidence intervals, W/T/L counts, and explicit **`significant`** vs **`inconclusive`** verdicts relative to `fixed_equal` (zero bare percentages).
  - Cold-start unseen holdout advantage of Random Forest over Moving Average and per-band OLS.
  - Anomaly detector precision vs delay tradeoff across `z = 3..5`.
  - Fixed **Simulation Assumptions & Scope** disclaimer explicitly scoping results to simulated arrivals/demand.

---

## Demo Day Safety Checklist
- [ ] Start backend with cached OSM road graph (`--data-dir data` -> reads `data/graph_cache.txt`, avoiding Overpass network stalls).
- [ ] Rehearse the full walkthrough with **`--offline`** enabled so poor venue Wi-Fi cannot disrupt telemetry.
- [ ] Pin **`--seed 42`** so queues, anomalies, and bus positions match rehearsed timings cleanly.
- [ ] Keep both services on **`localhost` / `127.0.0.1`** (`backend: 8080`, `frontend: 5173` or `vite preview`).
- [ ] Pre-test the TomTom API key in `.env` on demo morning **before** going on stage: confirm the topbar badge explicitly reads **`Live`** when online (or **`Simulated`** if offline mode is intentional).
- [ ] Keep a local screen recording of the 8-step walkthrough ready as zero-risk backup.

