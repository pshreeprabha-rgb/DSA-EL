# Data Attribution & Third-Party Licences

## Map & Traffic Data Sources
- **OpenStreetMap Road Network & Map Tiles**:
  - Attribution: **© OpenStreetMap contributors** (rendered visibly on map and footer via `https://tile.openstreetmap.org/{z}/{x}/{y}.png`).
  - Licence: Open Database License (**ODbL 1.0**) for OpenStreetMap map data (`data/graph_cache.txt`).
- **TomTom Traffic Flow API v4** (`traffic_client.c`):
  - Attribution: **TomTom** (rendered in footer when active mode reports `traffic_source: "live"`).
  - Terms: Governed by TomTom Developer Portal API Terms of Service; optional real-time corridor overlay over a simulated clock (`--offline` disables external HTTP calls).
- **Synthetic Demand & Simulation Model**:
  - Passenger arrivals, stop queues, and diurnal congestion curves are generated synthetically by `demand_model.c`, `traffic_sim.c`, and `sim.c`.

## Backend C & System Dependencies
| Component | Role | Source / Header Evidence | Licence |
| :--- | :--- | :--- | :--- |
| **cJSON** | JSON parsing & serialization | `/usr/include/cjson/cJSON.h` | **MIT** |
| **CivetWeb** | Embedded HTTP server (`transport_server`) | `/usr/include/civetweb.h` | **MIT** |
| **libcurl** | HTTPS client for Overpass & TomTom | `pkg-config libcurl` | **curl (MIT-like)** |
| **pthreads / libc / libm** | Synchronization & math primitives | GNU/Linux system libraries | **LGPL / System** |

## Python ML Dependencies (`ml/requirements.txt`)
| Package | Version Pin | Licence |
| :--- | :--- | :--- |
| **numpy** | `>=2.5.5` | **BSD-3-Clause** |
| **pandas** | `>=3.1.1` | **BSD-3-Clause** |
| **scikit-learn** | `>=1.9.1` | **BSD-3-Clause** |

## Frontend npm Packages (`frontend/package-lock.json`)
Verified directly from installed `frontend/node_modules/*/package.json`:

| Package | Version | Licence (`package.json`) |
| :--- | :--- | :--- |
| `react` | `19.3.1` | `MIT` |
| `react-dom` | `19.3.1` | `MIT` |
| `leaflet` | `1.9.4` | `BSD-2-Clause` |
| `react-leaflet` | `5.0.0` | `Hippocratic-2.1` |
| `recharts` | `3.11.2` | `MIT` |
| `vite` | `8.4.0` | `MIT` |
| `vitest` | `5.1.2` | `MIT` |
| `typescript` | `5.8.3` | `Apache-2.0` |
| `eslint` | `10.16.0` | `MIT` |
| `typescript-eslint` | `8.74.0` | `MIT` |
| `jsdom` | `31.0.1` | `MIT` |
| `@testing-library/react` | `16.3.3` | `MIT` |
| `@testing-library/dom` | `10.4.2` | `MIT` |
| `@testing-library/jest-dom` | `8.0.0` | `MIT` |

