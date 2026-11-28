# Frontend Architecture & Developer Notes

## Stack & Commands
- **Stack**: Vite, React 19, TypeScript (strict), React-Leaflet + Leaflet, Recharts, Vitest + Testing Library (`jsdom`), ESLint (`typescript-eslint` with `@typescript-eslint/no-explicit-any: error`).
- **Location**: `frontend/`
- **Commands** (run inside `frontend/`):
  - `npm ci` — clean install from `package-lock.json`
  - `npm run mock` — starts dependency-free Node mock server (`mock/server.mjs`, default port `8080`, override via `MOCK_PORT`)
  - `npm run dev` — starts Vite dev server on `http://localhost:5173` proxying `/api` to `http://127.0.0.1:8080` (override target via `VITE_API_BASE`)
  - `npm run lint` — runs ESLint across TypeScript/TSX and `.mjs` scripts
  - `npm run build` — runs `tsc --noEmit` and `vite build`
  - `npm test` — runs Vitest unit and component tests deterministically offline

## Design Notes
1. **Strict API Contract & Runtime Validators (`src/api/types.ts`, `src/api/validators.ts`, `src/api/client.ts`)**:
   - Every endpoint in `docs/api_contract.md` has explicit TypeScript types (zero `any`) and matching schema validators with zero external libraries.
   - When `import.meta.env.DEV` is true, `client.ts` validates parsed responses and emits a structured `console.warn` naming endpoint, field, and reason on any mismatch.
   - `scripts/contract_check.mjs` runs identical schema validation against a live server (`bash scripts/frontend_contract.sh`), verified via unit parity tests in `src/api/validators.test.ts`.
2. **Polling & Connectivity (`src/hooks/usePolling.ts`, `src/state/AppState.tsx`)**:
   - `usePolling` runs on an interval, skips execution when `document.hidden` is true, avoids overlapping in-flight requests, and cleans up timers on unmount.
   - `AppStateProvider` polls `/api/state` every `1000 ms` (with `4500 ms` request timeout), `/api/routes` and `/api/metrics` every `5000 ms`, fetches `/api/config` and `/api/stops` once, and refetches `/api/geometry` only when `geometry_version` changes.
   - Connection errors set `"Backend not reachable"` in `ConnectionBanner` (`role="alert"`) and clear automatically upon poll recovery.
3. **Interpolation, Blocked Overlays & Map Rendering (`src/lib/interpolate.ts`, `BusLayer.tsx`, `MapView.tsx`)**:
   - `haversineMeters`, `pointAlong`, `legProgress`, `simNowS`, `busPosition`, and `segmentMidpoint` compute continuous bus coordinates and blocked-edge midpoints.
   - `BusLayer` updates Leaflet markers imperatively every `100 ms` via `setLatLng` without storing per-frame coordinates in React state.
   - `MapView` is wrapped in `React.memo` and delegates `state`, `geometry`, `stops`, `panTarget`, and `optimizedPath` subscriptions to leaf child layers (`StopMarkersLayer`, `RouteGeometryLayer`, `PanController`, `OptimizedPathLayer`, `FitBoundsOnStops`), keeping `MapView` React renders constant (`0` additional renders/sec) during steady 1 Hz `/api/state` polling.
   - Blocked segments render in Leaflet `<Pane name="blocked-roads" style={{ zIndex: 450 }}>` above route polylines, with fixed-size `"Road blocked"` badges in `<Pane name="blocked-markers" style={{ zIndex: 650 }}>`.
4. **Performance Verification Protocol (Real Server at Speed 300x)**:
   - **React Profiler automated measurement (`src/components/Performance.test.tsx`)**: Observed `mapRenders === initialBootRenders` (`0` extra `MapView` renders across `10` sequential `/api/state` ticks).
   - **Browser DevTools Performance measurement (60 s @ `speed=300`)**:
     1. Start `backend/build/transport_server --port 8080 --data-dir data --seed 42 --speed 300 --offline` and `npm run preview` (`http://localhost:4173`) or `npm run dev` (`http://localhost:5173`).
     2. Open Chrome DevTools -> **Performance** -> **Record** for `60 s`. Verify Main Thread scripting + rendering total time stays `< 30 s` (`< 50%` busy) and confirm Leaflet bus DOM updates occur without full React tree commits.
     3. Optional DevTools Console console-error & React render counter snippet:
        ```js
        // Paste in browser DevTools Console while running at speed=300 for 60 s:
        (() => {
          const errs = [];
          const origErr = console.error;
          console.error = (...a) => { errs.push(a.join(' ')); origErr(...a); };
          let frames = 0, busyMs = 0;
          const t0 = performance.now();
          function tick() {
            const s = performance.now();
            frames++;
            busyMs += performance.now() - s;
            if (performance.now() - t0 < 60000) requestAnimationFrame(tick);
            else console.log({ elapsedMs: performance.now() - t0, frames, errsCount: errs.length, errs });
          }
          requestAnimationFrame(tick);
        })();
        ```
     4. *Environment note*: No Chromium/Playwright binary was installed in the WSL environment during T5C build, so live Chrome 60 s busy-% and 5-minute browser console runs should be executed on the host browser.

## Human Demo Walkthrough Checklist
See [docs/DEMO.md](file:///home/prabhashetty/transport/docs/DEMO.md) for the complete 8-step live demonstration script and safety checklist.
