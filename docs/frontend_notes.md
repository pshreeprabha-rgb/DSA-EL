# Frontend Architecture & Developer Notes

## Stack & Commands
- **Stack**: Vite, React 19, TypeScript (strict), React-Leaflet + Leaflet, Recharts, Vitest + Testing Library (`jsdom`), ESLint (`typescript-eslint` with `@typescript-eslint/no-explicit-any: error`).
- **Location**: `frontend/`
- **Commands** (run inside `frontend/`):
  - `npm ci` — clean install from `package-lock.json`
  - `npm run mock` — starts dependency-free Node mock server (`mock/server.mjs`, default port `8080`, override via `MOCK_PORT`)
  - `npm run dev` — starts Vite dev server on `http://localhost:5173` proxying `/api` to `http://127.0.0.1:8080` (override target via `VITE_API_BASE`)
  - `npm run lint` — runs ESLint across TypeScript/TSX and mock server sources
  - `npm run build` — runs `tsc --noEmit` and `vite build`
  - `npm test` — runs Vitest unit and component tests deterministically offline

## Design Notes
1. **Strict API Contract (`src/api/types.ts`, `src/api/client.ts`)**:
   - Every endpoint in `docs/api_contract.md` has explicit TypeScript types with zero `any`.
   - `getJson` and `postJson` enforce a 5 s `AbortController` timeout by default and normalize transport/parse/HTTP errors into `ApiError { status, code, message }` (`"network"`, `"timeout"`, `"bad_json"`, or backend codes).
2. **Polling & Connectivity (`src/hooks/usePolling.ts`, `src/state/AppState.tsx`)**:
   - `usePolling` runs on an interval, skips execution when `document.hidden` is true, avoids overlapping in-flight requests, and cleans up timers on unmount.
   - `AppStateProvider` polls `/api/state` every `1000 ms` (with `4500 ms` request timeout so failures surface within `5 s`), fetches `/api/config` and `/api/stops` once, and refetches `/api/geometry` only when `geometry_version` changes.
   - Connection errors set `"Backend not reachable"` in `ConnectionBanner` (`role="alert"`) and clear automatically upon poll recovery.
3. **Interpolation & Map Rendering (`src/lib/interpolate.ts`, `BusLayer.tsx`, `MapView.tsx`)**:
   - `haversineMeters`, `pointAlong`, `legProgress`, `simNowS`, and `busPosition` compute continuous positions for moving, dwelling, reserve, and deadheading buses.
   - `BusLayer` updates Leaflet markers imperatively every `100 ms` via `setLatLng` without storing per-frame coordinates in React state.
   - `RouteGeometryLayer` in `MapView` is memoized on `geometry.version` so polling `/api/state` does not re-render static route polylines or blocked red edges.
4. **Animated Mock Server (`mock/server.mjs`)**:
   - Dependency-free Node HTTP server serving `docs/mock/*.json` under `/api/*` with CORS headers.
   - Advances `clock_min` proportionally to `real_dt * speed` and animates every `running` bus around its route's `geometry.json` legs (`leg.time_s` travel + `20 s` dwell, looping), while accepting `POST /api/sim/control` (`start`, `pause`, `resume`, `speed`, `reset`, `seek`).

