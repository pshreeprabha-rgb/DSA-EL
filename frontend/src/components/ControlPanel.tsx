import { useCallback, useEffect, useState } from 'react';
import { ApiError, getJson, postJson } from '../api/client';
import type {
  AllocateRequest,
  AllocateResponse,
  HistoryResponse,
  InjectResponse,
  ScenarioRequest,
  ScenarioResponse,
  SimControlResponse,
  UndoResponse,
} from '../api/types';
import { clockHHMM, parseClockHHMM, stackTopFirst } from '../lib/format';
import { useAppState } from '../state/AppState';

const SPEED_CHOICES = [1, 10, 60, 300, 600] as const;

type PolicyOption = 'equal' | 'proportional' | 'greedy_spec' | 'greedy_marginal';
type PredictorOption = 'oracle' | 'ma' | 'rf';

interface ToastState {
  kind: 'success' | 'error';
  text: string;
}

/** Control panel triggering simulation, scenario, allocation, injection, and undo actions. */
export function ControlPanel() {
  const {
    state,
    config,
    stops,
    routes,
    geometry,
    expectNewBlockPan,
    pollStateNow,
    refreshDashboardNow,
  } = useAppState();

  const [pendingKey, setPendingKey] = useState<string | null>(null);
  const [toast, setToast] = useState<ToastState | null>(null);
  const [seekInput, setSeekInput] = useState('08:00');
  const [resetConfirmOpen, setResetConfirmOpen] = useState(false);

  const [scenarioStop, setScenarioStop] = useState<number>(0);
  const [blockRoute, setBlockRoute] = useState<number>(0);
  const [blockLeg, setBlockLeg] = useState<number>(0);
  const [scenarioBus, setScenarioBus] = useState<number>(0);
  const [injectStop, setInjectStop] = useState<number>(0);
  const [injectCount, setInjectCount] = useState<number>(150);

  const [policy, setPolicy] = useState<PolicyOption>('equal');
  const [predictor, setPredictor] = useState<PredictorOption>('ma');
  const [moveBus, setMoveBus] = useState<number>(0);
  const [moveRoute, setMoveRoute] = useState<number>(0);

  const [history, setHistory] = useState<HistoryResponse | null>(null);
  const [historyError, setHistoryError] = useState<string | null>(null);

  const loadHistory = useCallback(async () => {
    try {
      const h = await getJson<HistoryResponse>('/api/history');
      setHistory(h);
      setHistoryError(null);
    } catch (err: unknown) {
      setHistoryError(
        err instanceof ApiError ? `${err.code}: ${err.message}` : 'Failed to load history',
      );
    }
  }, []);

  useEffect(() => {
    let active = true;
    Promise.resolve().then(() => {
      if (active) {
        void loadHistory();
      }
    });
    return () => {
      active = false;
    };
  }, [loadHistory, state?.history_size]);

  const maEnabled = config?.models?.ma ?? true;
  const rfEnabled = config?.models?.rf ?? true;

  const effectivePredictor: PredictorOption =
    predictor === 'rf' && !rfEnabled
      ? 'oracle'
      : predictor === 'ma' && !maEnabled
        ? 'oracle'
        : predictor;

  const routeOptions = routes?.routes ?? [];
  const legsForRoute =
    geometry?.legs.filter((l) => l.route === blockRoute) ?? [];
  const busOptions = state?.buses ?? [];

  async function runAction<T>(
    key: string,
    fn: () => Promise<T>,
    formatSuccess: (res: T) => string,
  ): Promise<void> {
    setPendingKey(key);
    try {
      const res = await fn();
      setToast({ kind: 'success', text: formatSuccess(res) });
      await Promise.all([pollStateNow(), refreshDashboardNow(), loadHistory()]);
    } catch (err: unknown) {
      if (err instanceof ApiError) {
        setToast({ kind: 'error', text: `${err.code}: ${err.message}` });
      } else {
        setToast({
          kind: 'error',
          text: err instanceof Error ? err.message : 'Request failed',
        });
      }
    } finally {
      setPendingKey(null);
    }
  }

  const handleSeek = () => {
    const mins = parseClockHHMM(seekInput);
    if (mins === null) {
      setToast({
        kind: 'error',
        text: 'invalid_argument: Enter a valid HH:MM time (00:00..24:00)',
      });
      return;
    }
    void runAction<SimControlResponse>(
      'seek',
      () => postJson('/api/sim/control', { action: 'seek', time_min: mins }),
      (r) => `Seeked to ${Math.floor(r.clock_min)} min`,
    );
  };

  const handleConfirmReset = () => {
    setResetConfirmOpen(false);
    void runAction<SimControlResponse>(
      'reset',
      () => postJson('/api/sim/control', { action: 'reset' }),
      (r) => `Simulation reset (clock=${r.clock_min})`,
    );
  };

  const handleScenario = (key: string, payload: ScenarioRequest) => {
    void runAction<ScenarioResponse>(
      key,
      async () => {
        const res = await postJson<ScenarioResponse>('/api/scenario', payload);
        if (payload.kind === 'block_edge') {
          expectNewBlockPan();
        }
        return res;
      },
      (r) =>
        r.kind === 'block_edge'
          ? `Road block scheduled — applies at ${clockHHMM(r.time_min)} (${r.time_min} min)`
          : `Scheduled ${r.kind} — applies at ${clockHHMM(r.time_min)} (${r.time_min} min)`,
    );
  };

  const handleReallocate = () => {
    const force = policy === 'greedy_spec' || policy === 'greedy_marginal';
    const payload: AllocateRequest = {
      policy,
      predictor: effectivePredictor,
      force,
    };
    void runAction<AllocateResponse>(
      'reallocate',
      () => postJson('/api/allocate', payload),
      (r) =>
        'policy' in r
          ? `Allocated (${r.policy}, moves=${r.decision.moves_applied})`
          : `Moved bus ${r.moved.bus}`,
    );
  };

  const handleManualMove = () => {
    void runAction<AllocateResponse>(
      'manual_move',
      () =>
        postJson('/api/allocate', {
          manual_move: { bus: moveBus, to_route: moveRoute },
        }),
      (r) =>
        'moved' in r
          ? `Moved bus ${r.moved.bus} to R${r.moved.to_route + 1}`
          : 'Move applied',
    );
  };

  const handleInject = () => {
    void runAction<InjectResponse>(
      'inject',
      () => postJson('/api/inject', { stop: injectStop, count: injectCount }),
      (r) => `Injected ${r.count} passengers at stop ${r.stop}`,
    );
  };

  const handleUndo = () => {
    void runAction<UndoResponse>(
      'undo',
      () => postJson('/api/undo', {}),
      (r) => `Undone: ${r.undone} (history=${r.history_size})`,
    );
  };

  const stackRows = stackTopFirst(history?.entries ?? []);

  return (
    <div className="panel-stack" data-testid="control-panel">
      {toast && (
        <div
          role="status"
          data-testid="control-toast"
          className={`toast toast--${toast.kind}`}
        >
          <span>{toast.text}</span>
          <button
            type="button"
            className="toast__close"
            onClick={() => setToast(null)}
            aria-label="Dismiss notification"
          >
            x
          </button>
        </div>
      )}

      <section className="panel-card">
        <h2>Simulation Playback</h2>
        <div className="btn-row">
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'start'}
            onClick={() =>
              void runAction<SimControlResponse>(
                'start',
                () => postJson('/api/sim/control', { action: 'start' }),
                () => 'Simulation started',
              )
            }
          >
            Start
          </button>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'pause'}
            onClick={() =>
              void runAction<SimControlResponse>(
                'pause',
                () => postJson('/api/sim/control', { action: 'pause' }),
                () => 'Simulation paused',
              )
            }
          >
            Pause
          </button>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'resume'}
            onClick={() =>
              void runAction<SimControlResponse>(
                'resume',
                () => postJson('/api/sim/control', { action: 'resume' }),
                () => 'Simulation resumed',
              )
            }
          >
            Resume
          </button>
          <button
            type="button"
            className="action-btn action-btn--danger"
            disabled={pendingKey === 'reset'}
            onClick={() => setResetConfirmOpen(true)}
          >
            Reset
          </button>
        </div>

        <div className="btn-row" role="group" aria-label="Simulation speed presets">
          <span className="field-label">Speed:</span>
          {SPEED_CHOICES.map((spd) => (
            <button
              key={spd}
              type="button"
              className={`chip-btn ${state?.speed === spd ? 'chip-btn--active' : ''}`}
              disabled={pendingKey === `speed-${spd}`}
              onClick={() =>
                void runAction<SimControlResponse>(
                  `speed-${spd}`,
                  () =>
                    postJson('/api/sim/control', {
                      action: 'speed',
                      speed: spd,
                    }),
                  (r) => `Speed set to ${r.speed}x`,
                )
              }
            >
              {spd}x
            </button>
          ))}
        </div>

        <div className="form-row">
          <label className="field-group">
            <span>Seek (HH:MM)</span>
            <input
              type="text"
              aria-label="Seek time HH:MM"
              value={seekInput}
              onChange={(e) => setSeekInput(e.target.value)}
              placeholder="08:00"
            />
          </label>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'seek'}
            onClick={handleSeek}
          >
            Seek
          </button>
        </div>

        {resetConfirmOpen && (
          <div
            role="dialog"
            aria-modal="true"
            aria-label="Confirm simulation reset"
            className="confirm-box"
          >
            <p>Reset simulation state, undo stack, and blocked roads?</p>
            <div className="btn-row">
              <button
                type="button"
                className="action-btn action-btn--danger"
                onClick={handleConfirmReset}
              >
                Confirm Reset
              </button>
              <button
                type="button"
                className="action-btn"
                onClick={() => setResetConfirmOpen(false)}
              >
                Cancel
              </button>
            </div>
          </div>
        )}
      </section>

      <section className="panel-card">
        <h2>Scenarios</h2>
        <div className="btn-row">
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'rain_on'}
            onClick={() => handleScenario('rain_on', { kind: 'rain_on' })}
          >
            Rain On
          </button>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'rain_off'}
            onClick={() => handleScenario('rain_off', { kind: 'rain_off' })}
          >
            Rain Off
          </button>
        </div>

        <div className="form-row">
          <label className="field-group">
            <span>Event Stop</span>
            <select
              aria-label="Event stop"
              value={scenarioStop}
              onChange={(e) => setScenarioStop(Number(e.target.value))}
            >
              {stops.map((s) => (
                <option key={s.idx} value={s.idx}>
                  {s.idx}: {s.name}
                </option>
              ))}
              {stops.length === 0 && <option value={0}>Stop 0</option>}
            </select>
          </label>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'event_on'}
            onClick={() =>
              handleScenario('event_on', { kind: 'event_on', stop: scenarioStop })
            }
          >
            Event On
          </button>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'event_off'}
            onClick={() =>
              handleScenario('event_off', { kind: 'event_off', stop: scenarioStop })
            }
          >
            Event Off
          </button>
        </div>

        <div className="form-row">
          <label className="field-group">
            <span>Block Route</span>
            <select
              aria-label="Block road route"
              value={blockRoute}
              onChange={(e) => {
                setBlockRoute(Number(e.target.value));
                setBlockLeg(0);
              }}
            >
              {(routeOptions.length > 0
                ? routeOptions
                : [0, 1, 2, 3, 4, 5].map((id) => ({ id, name: `R${id + 1}` }))
              ).map((r) => (
                <option key={r.id} value={r.id}>
                  R{r.id + 1}: {r.name}
                </option>
              ))}
            </select>
          </label>
          <label className="field-group">
            <span>Leg</span>
            <select
              aria-label="Block road leg"
              value={blockLeg}
              onChange={(e) => setBlockLeg(Number(e.target.value))}
            >
              {(legsForRoute.length > 0
                ? legsForRoute
                : [{ leg: 0, from_stop: 0, to_stop: 1 }]
              ).map((l) => (
                <option key={l.leg} value={l.leg}>
                  Leg {l.leg} ({l.from_stop} -&gt; {l.to_stop})
                </option>
              ))}
            </select>
          </label>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'block_edge'}
            onClick={() =>
              handleScenario('block_edge', {
                kind: 'block_edge',
                route: blockRoute,
                leg: blockLeg,
              })
            }
          >
            Block Road
          </button>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'unblock_edge'}
            onClick={() =>
              handleScenario('unblock_edge', {
                kind: 'unblock_edge',
                route: blockRoute,
                leg: blockLeg,
              })
            }
          >
            Unblock Road
          </button>
        </div>

        <div className="form-row">
          <label className="field-group">
            <span>Bus</span>
            <select
              aria-label="Scenario bus"
              value={scenarioBus}
              onChange={(e) => setScenarioBus(Number(e.target.value))}
            >
              {(busOptions.length > 0 ? busOptions : [{ id: 0, state: 'running' }]).map(
                (b) => (
                  <option key={b.id} value={b.id}>
                    Bus {b.id} ({b.state})
                  </option>
                ),
              )}
            </select>
          </label>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'breakdown'}
            onClick={() =>
              handleScenario('breakdown', { kind: 'breakdown', bus: scenarioBus })
            }
          >
            Breakdown
          </button>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'repair'}
            onClick={() =>
              handleScenario('repair', { kind: 'repair', bus: scenarioBus })
            }
          >
            Repair
          </button>
        </div>

        <div className="form-row">
          <label className="field-group">
            <span>Inject Stop</span>
            <select
              aria-label="Inject stop"
              value={injectStop}
              onChange={(e) => setInjectStop(Number(e.target.value))}
            >
              {stops.map((s) => (
                <option key={s.idx} value={s.idx}>
                  {s.idx}: {s.name}
                </option>
              ))}
              {stops.length === 0 && <option value={0}>Stop 0</option>}
            </select>
          </label>
          <label className="field-group">
            <span>Count</span>
            <input
              type="number"
              aria-label="Inject passenger count"
              min={1}
              max={500}
              value={injectCount}
              onChange={(e) => setInjectCount(Number(e.target.value))}
            />
          </label>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'inject'}
            onClick={handleInject}
          >
            Inject Burst
          </button>
        </div>
      </section>

      <section className="panel-card">
        <h2>Fleet Allocation &amp; Manual Move</h2>
        <div className="form-row">
          <label className="field-group">
            <span>Policy</span>
            <select
              aria-label="Allocation policy"
              value={policy}
              onChange={(e) => setPolicy(e.target.value as PolicyOption)}
            >
              <option value="equal">equal</option>
              <option value="proportional">proportional</option>
              <option value="greedy_spec">greedy_spec</option>
              <option value="greedy_marginal">greedy_marginal</option>
            </select>
          </label>
          <label className="field-group">
            <span>Predictor</span>
            <select
              aria-label="Demand predictor"
              value={effectivePredictor}
              onChange={(e) => setPredictor(e.target.value as PredictorOption)}
            >
              <option value="oracle">oracle</option>
              <option value="ma" disabled={!maEnabled}>
                ma{maEnabled ? '' : ' (unloaded)'}
              </option>
              <option value="rf" disabled={!rfEnabled}>
                rf{rfEnabled ? '' : ' (unloaded)'}
              </option>
            </select>
          </label>
          <button
            type="button"
            className="action-btn action-btn--primary"
            disabled={pendingKey === 'reallocate'}
            onClick={handleReallocate}
          >
            Re-allocate now
          </button>
        </div>

        <div className="form-row">
          <label className="field-group">
            <span>Move Bus</span>
            <select
              aria-label="Move bus selector"
              value={moveBus}
              onChange={(e) => setMoveBus(Number(e.target.value))}
            >
              {(busOptions.length > 0 ? busOptions : [{ id: 0, route: 0 }]).map((b) => (
                <option key={b.id} value={b.id}>
                  Bus {b.id} (R{b.route + 1})
                </option>
              ))}
            </select>
          </label>
          <label className="field-group">
            <span>To Route</span>
            <select
              aria-label="Move target route"
              value={moveRoute}
              onChange={(e) => setMoveRoute(Number(e.target.value))}
            >
              {(routeOptions.length > 0
                ? routeOptions
                : [0, 1, 2, 3, 4, 5].map((id) => ({ id, name: `R${id + 1}` }))
              ).map((r) => (
                <option key={r.id} value={r.id}>
                  R{r.id + 1}: {r.name}
                </option>
              ))}
            </select>
          </label>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'manual_move'}
            onClick={handleManualMove}
          >
            Move Bus
          </button>
        </div>
      </section>

      <section className="panel-card">
        <div className="panel-header-row">
          <h2>Undo Stack ({history?.size ?? 0})</h2>
          <button
            type="button"
            className="action-btn"
            disabled={pendingKey === 'undo'}
            onClick={handleUndo}
          >
            Undo
          </button>
        </div>
        {historyError && <p className="error-text">{historyError}</p>}
        {stackRows.length === 0 ? (
          <p className="muted-text">History stack is empty.</p>
        ) : (
          <ul className="stack-list" data-testid="control-history-stack">
            {stackRows.map((entry, idx) => (
              <li
                key={`${idx}-${entry.text}`}
                className={entry.isTop ? 'stack-item stack-item--top' : 'stack-item'}
              >
                {entry.isTop && <span className="pill-tag">TOP</span>}
                <span>{entry.text}</span>
              </li>
            ))}
          </ul>
        )}
      </section>
    </div>
  );
}

