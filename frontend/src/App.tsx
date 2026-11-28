import { useState } from 'react';
import { postJson } from './api/client';
import type { SimControlResponse } from './api/types';
import { Badge } from './components/Badge';
import { ClockDisplay } from './components/ClockDisplay';
import { CompareView } from './components/CompareView';
import { ConnectionBanner } from './components/ConnectionBanner';
import { ControlPanel } from './components/ControlPanel';
import { DashboardPanel } from './components/DashboardPanel';
import { DemandPanel } from './components/DemandPanel';
import { DsView } from './components/DsView';
import { MapView } from './components/MapView';
import { busStateColor, minutesLabel, routeColor, stopTypeColor } from './lib/format';
import { AppStateProvider, useAppState } from './state/AppState';

const SPEED_OPTIONS = [1, 10, 60, 300, 600];

type RightTabId = 'dashboard' | 'controls' | 'demand' | 'ds' | 'compare';

const RIGHT_TABS: readonly { id: RightTabId; label: string }[] = [
  { id: 'dashboard', label: 'Dashboard' },
  { id: 'controls', label: 'Controls' },
  { id: 'demand', label: 'Demand' },
  { id: 'ds', label: 'Data structures' },
  { id: 'compare', label: 'Comparison' },
];

function DashboardShell() {
  const {
    state,
    config,
    connectionStatus,
    connectionMessage,
    pollStateNow,
  } = useAppState();

  const [activeTab, setActiveTab] = useState<RightTabId>('dashboard');

  const clockMin = state?.clock_min ?? config?.start_min ?? 360;
  const speed = state?.speed ?? 60;
  const policy = state?.policy ?? config?.policy ?? 'fixed_equal';
  const statusLabel = state?.finished
    ? 'Finished'
    : state?.running
      ? 'Running'
      : 'Paused';
  const isLive = state?.traffic_source === 'live';

  const handleToggleRun = () => {
    const action = state?.running ? 'pause' : 'resume';
    void postJson<SimControlResponse>('/api/sim/control', { action })
      .then(() => pollStateNow())
      .catch(() => {
        // State polling banner handles connectivity loss safely.
      });
  };

  const handleSpeedChange = (nextSpeed: number) => {
    void postJson<SimControlResponse>('/api/sim/control', {
      action: 'speed',
      speed: nextSpeed,
    })
      .then(() => pollStateNow())
      .catch(() => {
        // Ignored; polling banner surfaces backend unavailability.
      });
  };

  return (
    <div className="app-shell">
      <ConnectionBanner
        connected={connectionStatus === 'connected'}
        message={connectionMessage}
      />

      <header className="topbar">
        <div className="topbar__brand">
          <h1>Bengaluru Transit Control</h1>
          <span className="meta-pill">
            {state?.day_type ?? 'weekday'}
            {state?.rain ? ' • rain' : ''}
          </span>
        </div>

        <div className="topbar__telemetry">
          <div className="metric-chip">
            <span className="metric-label">Clock</span>
            <ClockDisplay clockMin={clockMin} />
          </div>

          <div className="metric-chip">
            <span className="metric-label">Status</span>
            <span
              className={`status-pill status-pill--${statusLabel.toLowerCase()}`}
            >
              {statusLabel}
            </span>
          </div>

          <div className="metric-chip">
            <span className="metric-label">Speed</span>
            <span className="metric-value">{speed}x</span>
          </div>

          <div className="metric-chip">
            <span className="metric-label">Policy</span>
            <span className="metric-value">{policy}</span>
          </div>

          <Badge trafficSource={state?.traffic_source} />
        </div>

        <div className="topbar__controls">
          <button
            type="button"
            className="control-btn"
            onClick={handleToggleRun}
            disabled={Boolean(state?.finished)}
          >
            {state?.running ? 'Pause' : 'Resume'}
          </button>

          <label className="speed-select-label">
            <span>Speed</span>
            <select
              aria-label="Simulation speed"
              value={speed}
              onChange={(e) => handleSpeedChange(Number(e.target.value))}
            >
              {SPEED_OPTIONS.map((opt) => (
                <option key={opt} value={opt}>
                  {opt}x
                </option>
              ))}
            </select>
          </label>
        </div>
      </header>

      <main className="main-split">
        <div className="layout-map-col">
          <MapView />

          <aside className="map-legend" aria-label="Map legend">
            <section className="legend-group">
              <h2>Summary</h2>
              <div className="legend-row">
                <span>Boarded</span>
                <strong>{state?.summary.boarded ?? 0}</strong>
              </div>
              <div className="legend-row">
                <span>Waiting</span>
                <strong>{state?.summary.still_waiting ?? 0}</strong>
              </div>
              <div className="legend-row">
                <span>Mean wait</span>
                <strong>
                  {state
                    ? minutesLabel(state.summary.mean_wait_incl_censored_s)
                    : '0.0 min'}
                </strong>
              </div>
            </section>

            <section className="legend-group">
              <h2>Routes</h2>
              <div className="legend-swatches">
                {[0, 1, 2, 3, 4, 5].map((r) => (
                  <span key={r} className="swatch-item">
                    <i
                      className="swatch-line"
                      style={{ backgroundColor: routeColor(r) }}
                    />
                    R{r + 1}
                  </span>
                ))}
                <span className="swatch-item">
                  <i
                    className="swatch-line swatch-line--blocked"
                    style={{ backgroundColor: '#dc2626' }}
                  />
                  Blocked
                </span>
              </div>
            </section>

            <section className="legend-group">
              <h2>Stops &amp; Buses</h2>
              <div className="legend-swatches">
                {(['hub', 'office', 'market', 'residential'] as const).map(
                  (t) => (
                    <span key={t} className="swatch-item">
                      <i
                        className="swatch-dot"
                        style={{ backgroundColor: stopTypeColor(t) }}
                      />
                      {t}
                    </span>
                  ),
                )}
              </div>
              <div className="legend-swatches">
                {(['running', 'deadhead', 'reserve', 'broken'] as const).map(
                  (s) => (
                    <span key={s} className="swatch-item">
                      <i
                        className="swatch-pill"
                        style={{ backgroundColor: busStateColor(s) }}
                      />
                      {s}
                    </span>
                  ),
                )}
              </div>
            </section>
          </aside>
        </div>

        <section className="layout-panel-col" aria-label="Operations workspace">
          <nav className="primary-tabs" role="tablist" aria-label="Workspace views">
            {RIGHT_TABS.map((tab) => (
              <button
                key={tab.id}
                type="button"
                role="tab"
                aria-selected={activeTab === tab.id}
                className={`primary-tab-btn ${
                  activeTab === tab.id ? 'primary-tab-btn--active' : ''
                }`}
                onClick={() => setActiveTab(tab.id)}
              >
                {tab.label}
              </button>
            ))}
          </nav>

          <div className="panel-scroll-body">
            {activeTab === 'dashboard' && <DashboardPanel />}
            {activeTab === 'controls' && <ControlPanel />}
            {activeTab === 'demand' && <DemandPanel />}
            {activeTab === 'ds' && <DsView />}
            {activeTab === 'compare' && <CompareView />}
          </div>
        </section>
      </main>

      <footer className="app-footer" data-testid="app-footer">
        <span>Data attribution: OpenStreetMap contributors</span>
        {isLive && <span> • TomTom live traffic</span>}
        <span> • Simulated demand</span>
      </footer>
    </div>
  );
}

/** Root React application wrapped in AppStateProvider. */
export default function App() {
  return (
    <AppStateProvider>
      <DashboardShell />
    </AppStateProvider>
  );
}
