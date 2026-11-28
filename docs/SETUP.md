# Setup & Operational Commands

## 1. WSL System Packages
On Ubuntu/Debian inside WSL:
```bash
sudo apt update && sudo apt install -y \
  build-essential gcc make pkg-config valgrind curl \
  libcurl4-openssl-dev libcjson-dev libcivetweb-dev \
  python3 python3-pip python3-venv nodejs npm git
```
Node.js **v20+** (verified on `v22.22.1`) and GCC (`-std=c11`) are required.

## 2. Python ML Environment (`ml/`)
```bash
python3 -m venv ~/venvs/transport
~/venvs/transport/bin/pip install -r ml/requirements.txt
```
Dependencies (`ml/requirements.txt`): `numpy>=2.5.5`, `pandas>=3.1.1`, `scikit-learn>=1.9.1`.

## 3. Environment Variables (`.env`)
Copy `backend/.env.example` to `backend/.env`:
```bash
cp backend/.env.example backend/.env
```
- **Offline mode (`--offline`)**: No TomTom key or internet access needed. Always use `--offline` in automated tests and offline rehearsals.
- **Live TomTom mode**: Add a valid key to `TOMTOM_API_KEY` inside `backend/.env` (never commit `.env` or print keys).

## 4. Network Ports
- **Backend HTTP API / Mock Server**: `http://127.0.0.1:8080`
- **Frontend Vite Dev Server**: `http://localhost:5173` (proxies `/api/*` -> `http://127.0.0.1:8080`, override via `VITE_API_BASE`)
- **Frontend Production Preview**: `http://localhost:4173` (`cd frontend && npm run preview`)

## 5. Common Commands
Run from repository root:
```bash
# Build & test suites
make check                                      # Run backend C tests then frontend Vitest suite
make -C backend asan                            # AddressSanitizer + UBSan build & test
make -C backend valgrind                        # Valgrind leak check on all 27 C test suites
make eval                                       # Run multi-seed eval-run, coldstart, anomaly-eval

# Backend CLI workflows
make -C backend cli
(cd backend && ./build/transport_cli bench --synthetic --pairs 200 --seed 42 --hour 9)
(cd backend && ./build/transport_cli bench --pairs 200 --seed 42 --hour 9)
(cd backend && ./build/transport_cli demo)
(cd backend && ./build/transport_cli traffic --offline)
(cd backend && ./build/transport_cli golden-check --model ../data/demand_rf.txt --golden ../data/golden_predictions.csv)

# Server & smoke / contract checks (all server tests use --offline)
make server
./backend/build/transport_server --port 8080 --data-dir data --seed 42 --paused --offline
bash scripts/api_smoke.sh                       # Real C server API smoke check (--offline)
bash scripts/frontend_contract.sh               # Runtime validator contract check (--offline)
bash scripts/fresh_clone_check.sh               # Clean git clone end-to-end verification

# Frontend development & build
cd frontend && npm ci
cd frontend && npm run mock                     # Standalone animated mock server (:8080)
cd frontend && npm run dev                      # Live Vite UI (:5173)
cd frontend && npm run lint && npm run build    # ESLint + TypeScript check + production bundle
```

