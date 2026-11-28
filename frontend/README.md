# Bengaluru Transit Frontend (`frontend/`)

React 19 + TypeScript + Vite dashboard and interactive Leaflet map for the Smart Public Transport Fleet and Route Optimization System.

## Prerequisites
- Node.js **v20+** (verified on `v22.22.1`) and `npm` (`v9+`)
- Running backend (`backend/build/transport_server` on `http://127.0.0.1:8080`) **or** the built-in mock API server (`npm run mock`)

## Setup & Commands
Run inside `frontend/`:

```bash
npm ci           # Install exact locked dependencies
npm run dev      # Start Vite dev server on http://localhost:5173 (proxies /api -> 127.0.0.1:8080)
npm run mock     # Start animated standalone mock API server on http://127.0.0.1:8080
npm run build    # Run strict tsc --noEmit and production Vite bundle into dist/
npm run preview  # Preview the production dist/ bundle
npm run lint     # Run ESLint across TS/TSX and mock/script .mjs files
npm test         # Run deterministic offline Vitest suite
```

### Ports & Proxy Overrides
- **Frontend UI**: `http://localhost:5173`
- **Backend / Mock API**: `http://127.0.0.1:8080` (`MOCK_PORT` overrides the mock server port)
- **Dev proxy target**: `vite.config.ts` proxies `/api/*` to `http://127.0.0.1:8080`. Override with:
  ```bash
  VITE_API_BASE=http://127.0.0.1:18080 npm run dev
  ```

### Contract Check Against Real Backend
From the repository root (after `make server` in `backend/`):
```bash
bash scripts/frontend_contract.sh
```
This starts `backend/build/transport_server --paused --offline --port 18080 --seed 42` in a cleanup trap and runs `frontend/scripts/contract_check.mjs` across every GET and POST endpoint.

## Troubleshooting
- **CORS errors when accessing backend directly**: pass `--cors http://localhost:5173` (default in `transport_server`), or load via Vite's `/api` proxy on `http://localhost:5173` so browser requests are same-origin.
- **WSL `localhost` connection refused / timeout (`Backend not reachable`)**:
  - Ensure the backend listens on `127.0.0.1` (`--bind 127.0.0.1`) rather than IPv6-only `::1`.
  - If Windows cannot reach WSL `localhost:5173` after sleep/hibernate, run `wsl --shutdown` in PowerShell once or open `http://127.0.0.1:5173`.
- **Empty / error panels**:
  - Stopped backend -> top alert banner displays **`Backend not reachable`** within `5 s` and clears automatically upon reconnection.
  - Missing `eval/summary.json` -> Comparison panel displays **`Run make eval in the backend to generate the evaluation`**.

## Attribution
Map data: OpenStreetMap contributors (`https://tile.openstreetmap.org/{z}/{x}/{y}.png`). Traffic source: TomTom live traffic (shown when backend reports `traffic_source: "live"`) or simulated diurnal traffic (`Simulated demand`).

