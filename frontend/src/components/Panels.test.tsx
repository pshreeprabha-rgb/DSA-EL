import { fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import App from '../App';
import { AppStateProvider, useAppState } from '../state/AppState';
import { CompareView } from './CompareView';
import { DashboardPanel } from './DashboardPanel';
import { DemandPanel } from './DemandPanel';
import { DsView } from './DsView';

function createDashboardFetchMock(opts?: {
  evalStatus?: number;
  nullRf?: boolean;
  trafficSource?: 'simulated' | 'live';
}) {
  const evalStatus = opts?.evalStatus ?? 200;
  const nullRf = opts?.nullRf ?? false;
  const trafficSource = opts?.trafficSource ?? 'simulated';

  return vi.fn().mockImplementation((url: string, init?: RequestInit) => {
    if (url === '/api/config') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            stops: 3,
            routes: 2,
            buses: 3,
            reserve: 1,
            seed: 42,
            start_min: 360,
            end_min: 1320,
            slice_min: 15,
            policy: 'fixed_equal',
            predictor: 'ma',
            spacing: 'time',
            reassign: 'drain',
            models: { ma: true, rf: !nullRf },
            offline: true,
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/stops') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            stops: [
              { idx: 0, node: 10, name: 'Market Zero', type: 'market', scale: 1, lat: 12.95, lng: 77.55, route: 0, pos: 0, event_active: false, first_flag_min: -1 },
              { idx: 3, node: 20, name: 'Majestic Hub', type: 'hub', scale: 1.5, lat: 12.97, lng: 77.57, route: 0, pos: 1, event_active: true, first_flag_min: 495 },
              { idx: 5, node: 30, name: 'Canara Bank', type: 'office', scale: 1, lat: 12.98, lng: 77.59, route: 1, pos: 0, event_active: false, first_flag_min: 450 },
            ],
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/geometry') {
      return Promise.resolve(
        new Response(JSON.stringify({ version: 1, legs: [], blocked: [] }), { status: 200 }),
      );
    }
    if (url === '/api/state') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            clock_min: 480,
            clock: '08:00',
            running: true,
            finished: false,
            speed: 60,
            day_type: 'weekday',
            rain: false,
            policy: 'fixed_equal',
            traffic_source: trafficSource,
            geometry_version: 1,
            history_size: 2,
            buses: [
              { id: 0, state: 'running', route: 0, stop_idx: 3, load: 12, pending_route: -2, motion: { moving: false, leg_route: 0, leg_index: -1, from_stop: 3, to_stop: 3, depart_s: 28800, arrive_s: 28800 } },
              { id: 1, state: 'running', route: 1, stop_idx: 5, load: 8, pending_route: -2, motion: { moving: false, leg_route: 1, leg_index: -1, from_stop: 5, to_stop: 5, depart_s: 28800, arrive_s: 28800 } },
              { id: 2, state: 'reserve', route: -1, stop_idx: 3, load: 0, pending_route: -2, motion: { moving: false, leg_route: -1, leg_index: -1, from_stop: 3, to_stop: 3, depart_s: 28800, arrive_s: 28800 } },
            ],
            queues: [
              { stop: 0, waiting: 4 },
              { stop: 3, waiting: 18 },
              { stop: 5, waiting: 11 },
            ],
            summary: { arrived: 500, boarded: 420, still_waiting: 80, mean_wait_boarded_s: 360, mean_wait_incl_censored_s: 450, p95_s: 900 },
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/routes') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            hour: 8,
            basis: 'forecast',
            routes: [
              { id: 0, name: 'R1: South-Central', stops: [0, 3], loop_min: 40, cycle_min: 42, buses: 1, demand: 88.5, priority: 12.6 },
              { id: 1, name: 'R2: East-West', stops: [5], loop_min: 35, cycle_min: 37, buses: 1, demand: 146.2, priority: 20.9 },
            ],
            board: [
              { route_id: 1, priority: 20.9, demand: 146.2, buses: 1 },
              { route_id: 0, priority: 12.6, demand: 88.5, buses: 1 },
            ],
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/metrics') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            sim: {
              arrived: 500, boarded: 420, still_waiting: 80, wait_sum_s: 151200, wait_max_s: 900,
              mean_wait_boarded_s: 360, mean_wait_incl_censored_s: 450, p50_s: 350, p95_s: 900,
              full_bus_events: 4, forced_dumps: 0, stranded_by_breakdown: 0, bus_arrivals: 120,
              moves_applied: 1, deadhead_s_total: 0, anomaly_flags: 1, emergency_dispatches: 0,
              stalled_s: 0, mean_load_factor: 0.4, decisions: 1, decisions_applied: 1,
            },
            routes: [],
            ml: { train_rows: 120960, test_rows: 51840, trees: 30, depth: 9, leaf: 30, rf_mae: 2.4617, rf_rmse: 4.0777, rf_mae_lambda: 1.2408 },
            last_optimize: null,
            traffic: { source: 'simulated', calls: 0, live_corridors: 0, last_error: '' },
          }),
          { status: 200 },
        ),
      );
    }
    if (url.startsWith('/api/demand/forecast')) {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            stop: 3,
            name: 'Majestic Hub',
            type: 'hub',
            day_type: 'weekday',
            rain: false,
            event_active: true,
            hours: [
              { hour: 7, oracle: 3.1, ma: 2.9, rf: nullRf ? null : 3.2, observed: 3 },
              { hour: 8, oracle: 5.4, ma: 4.8, rf: nullRf ? null : 5.1, observed: 6 },
            ],
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/alerts') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            flags: [
              { stop: 5, name: 'Canara Bank', time_min: 450, observed: 9, expected: 3.4, z: 3.07 },
            ],
            log: [],
          }),
          { status: 200 },
        ),
      );
    }
    if (url.startsWith('/api/schedule')) {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            from: 480,
            to: 600,
            count: 2,
            avl: { size: 506, height: 10, balanced: true },
            trips: [
              { trip_id: 14, route_id: 1, bus_id: 1, departure_min: 482, departure: '08:02' },
              { trip_id: 11, route_id: 0, bus_id: 0, departure_min: 490, departure: '08:10' },
            ],
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/history') {
      return Promise.resolve(
        new Response(JSON.stringify({ size: 2, entries: ['block edge 3614', 'block edge 900'] }), { status: 200 }),
      );
    }
    if (url.startsWith('/api/bus/')) {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            id: 0,
            state: 'running',
            route: 0,
            stop_idx: 3,
            load: 12,
            capacity: 60,
            depot: 0,
            journeys: [{ trip_id: 101, route_id: 0, start_min: 360, end_min: 405, passengers: 31 }],
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/tree') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            count: 3,
            height: 2,
            nodes: [
              { row: 0, parent: -1, depth: 0, kind: 0, id: 0, name: 'Bengaluru' },
              { row: 1, parent: 0, depth: 1, kind: 1, id: 0, name: 'Majestic Depot' },
              { row: 2, parent: 1, depth: 3, kind: 3, id: 0, name: 'Bus 0' },
            ],
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/route/optimize' && init?.method === 'POST') {
      const reqBody = JSON.parse(String(init.body || '{}')) as { mode?: string };
      const isStatic = reqBody.mode === 'static';
      return Promise.resolve(
        new Response(
          JSON.stringify({
            mode: reqBody.mode ?? 'traffic',
            astar: { found: true, time_s: isStatic ? 540 : 600, length_m: 5000, nodes_explored: 525, time_static_s: 540, time_traffic_s: isStatic ? 720 : 600, path: [[12.95, 77.55], [12.98, 77.59]] },
            dijkstra: { found: true, time_s: isStatic ? 540 : 600, length_m: 5000, nodes_explored: 1419, time_static_s: 540, time_traffic_s: isStatic ? 720 : 600, path: [[12.95, 77.55], [12.98, 77.59]] },
            nodes_ratio: 0.37,
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/eval') {
      if (evalStatus === 404) {
        return Promise.resolve(
          new Response(JSON.stringify({ error: { code: 'not_found', message: 'missing summary.json' } }), { status: 404 }),
        );
      }
      return Promise.resolve(
        new Response(
          JSON.stringify({
            config: { seeds: 30, base_seed: 1000, stops: 60, routes: 6, buses: 36, spacing: 'time', reassign: 'drain' },
            scenarios: [
              {
                name: 'normal',
                metric: 'mean_wait_incl_censored_s',
                baseline: 'fixed_equal',
                policies: [
                  { name: 'fixed_equal', n: 30, mean: 844.5, sd: 42.5, diff_mean: 0, ci_lo: 0, ci_hi: 0, wins: 0, ties: 30, losses: 0, significant: false },
                  { name: 'greedy_spec_ma', n: 30, mean: 822.2, sd: 60.1, diff_mean: -22.3, ci_lo: -47.5, ci_hi: 2.9, wins: 21, ties: 0, losses: 9, significant: false },
                  { name: 'greedy_marginal_rf', n: 30, mean: 781.8, sd: 33.8, diff_mean: -62.7, ci_lo: -84.7, ci_hi: -40.7, wins: 26, ties: 0, losses: 4, significant: true },
                ],
              },
            ],
            coldstart: {
              train_rows: 90720,
              holdout_stops: 15,
              seen_rows: 38880,
              unseen_rows: 12960,
              seen: [{ model: 'random_forest', mae_noisy: 2.67, rmse_noisy: 4.57, mae_lambda: 1.44 }],
              unseen: [{ model: 'random_forest', mae_noisy: 2.91, rmse_noisy: 5.01, mae_lambda: 1.92 }],
              paired_unseen_mae_lambda: { n_days: 36, diff_mean_rf_minus_ma: -0.858, ci_lo: -1.09, ci_hi: -0.63, wins: 35, ties: 0, losses: 1 },
            },
            anomaly: {
              seeds: 30,
              base_seed: 1000,
              event_stop: 13,
              thresholds: [{ threshold: 4, false_alarms_mean: 3.2, false_alarms_sd: 2.2, detection_rate: 1, median_delay_min: 15, precision: 0.385 }],
              analytic_validation: { analytic_expected: 16.46, observed_mean: 17.43, standard_error: 0.82, pass: true },
            },
          }),
          { status: 200 },
        ),
      );
    }

    return Promise.resolve(new Response('{}', { status: 200 }));
  });
}

describe('Dashboard, Demand, DsView, and CompareView', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('DashboardPanel displays KPIs, exact server heap order, and reserve count', async () => {
    vi.stubGlobal('fetch', createDashboardFetchMock());
    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <DashboardPanel />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('priority-board-table')).toBeInTheDocument();
    });

    const kpis = screen.getByTestId('kpi-cards');
    expect(kpis).toHaveTextContent('7.5 min');
    expect(kpis).toHaveTextContent('15.0 min');
    expect(kpis).toHaveTextContent('420');
    expect(kpis).toHaveTextContent('80');

    const rows = within(screen.getByTestId('priority-board-table')).getAllByRole('row');
    expect(rows[1]).toHaveTextContent('R2: East-West');
    expect(rows[2]).toHaveTextContent('R1: South-Central');

    expect(screen.getByTestId('allocation-table')).toHaveTextContent('Reserve pool1');
  });

  it('DemandPanel defaults to first hub stop, omits null RF series, shows Now reference line, and selects alert stop', async () => {
    vi.stubGlobal('fetch', createDashboardFetchMock({ nullRf: true }));

    function Harness() {
      const { selectedStopIdx } = useAppState();
      return (
        <>
          <div data-testid="selected-stop">{String(selectedStopIdx)}</div>
          <DemandPanel />
        </>
      );
    }

    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <Harness />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('demand-chart')).toBeInTheDocument();
      expect(screen.getByTestId('alerts-list')).toBeInTheDocument();
    });

    const select = screen.getByLabelText('Demand forecast stop') as HTMLSelectElement;
    expect(select.value).toBe('3');
    expect(screen.getByTestId('ma-event-note')).toHaveTextContent('ignores events');
    expect(screen.getByTestId('demand-chart')).toHaveTextContent('Oracle, Observed, MA');
    expect(screen.getByTestId('demand-chart')).not.toHaveTextContent('RF');
    expect(screen.getByTestId('demand-chart')).toHaveTextContent('Now (8h)');

    fireEvent.click(screen.getByRole('button', { name: /Canara Bank/i }));
    expect(screen.getByTestId('selected-stop')).toHaveTextContent('5');
  });

  it('DsView renders AVL schedule order & badge, top stack entry, journeys, and collapsible tree', async () => {
    vi.stubGlobal('fetch', createDashboardFetchMock());
    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <DsView />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('avl-badges')).toBeInTheDocument();
    });

    expect(screen.getByTestId('avl-badges')).toHaveTextContent('Balanced');
    const schedRows = within(screen.getByTestId('ds-schedule')).getAllByRole('row');
    expect(schedRows[1]).toHaveTextContent('#14');
    expect(schedRows[2]).toHaveTextContent('#11');

    fireEvent.click(screen.getByRole('tab', { name: 'History (Stack)' }));
    await waitFor(() => {
      expect(screen.getByTestId('ds-history')).toHaveTextContent('block edge 3614');
    });
    expect(screen.getByTestId('ds-history')).toHaveTextContent('TOP');

    fireEvent.click(screen.getByRole('tab', { name: 'Bus journeys' }));
    await waitFor(() => {
      expect(screen.getByTestId('ds-journeys')).toHaveTextContent('#101');
    });
    expect(screen.getByTestId('ds-journeys')).toHaveTextContent('06:00');
    expect(screen.getByTestId('ds-journeys')).toHaveTextContent('31');

    fireEvent.click(screen.getByRole('tab', { name: 'Fleet tree' }));
    await waitFor(() => {
      expect(screen.getByText('Bus 0')).toBeInTheDocument();
    });
    fireEvent.click(screen.getByRole('button', { name: /Majestic Depot/i }));
    expect(screen.queryByText('Bus 0')).toBeNull();
  });

  it('DsView Route Optimizer submits algo both and renders node counts and ratio', async () => {
    const fetchMock = createDashboardFetchMock();
    vi.stubGlobal('fetch', fetchMock);
    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <DsView />
      </AppStateProvider>,
    );

    fireEvent.click(screen.getByRole('tab', { name: 'Route optimizer' }));
    fireEvent.click(screen.getByRole('button', { name: 'Optimize Path' }));

    await waitFor(() => {
      expect(screen.getByTestId('optimizer-results')).toBeInTheDocument();
    });

    const resBox = screen.getByTestId('optimizer-results');
    expect(resBox).toHaveTextContent('525');
    expect(resBox).toHaveTextContent('1419');
    expect(resBox).toHaveTextContent('0.370');
  });

  it('CompareView renders policy verdicts, disclaimer, cold-start, anomaly, and ML summary', async () => {
    vi.stubGlobal('fetch', createDashboardFetchMock());
    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <CompareView />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('scenario-normal')).toBeInTheDocument();
    });

    expect(screen.getByTestId('eval-disclaimer')).toHaveTextContent(
      'Demand and arrivals are simulated.',
    );
    const rows = within(screen.getByTestId('scenario-normal')).getAllByRole('row');
    expect(rows[1]).toHaveTextContent('baseline');
    expect(rows[2]).toHaveTextContent('inconclusive');
    expect(rows[3]).toHaveTextContent('significant');
    expect(screen.getByTestId('coldstart-section')).toHaveTextContent('random_forest');
    expect(screen.getByTestId('anomaly-section')).toHaveTextContent('0.385');
    expect(screen.getByTestId('ml-metrics-section')).toHaveTextContent('2.4617');
  });

  it('CompareView shows instructions when /api/eval returns 404', async () => {
    vi.stubGlobal('fetch', createDashboardFetchMock({ evalStatus: 404 }));
    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <CompareView />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('eval-missing')).toHaveTextContent(
        'Run make eval in the backend to generate the evaluation',
      );
    });
  });

  it('DemandPanel includes RF series when present and refetches on stop change', async () => {
    const fetchMock = createDashboardFetchMock({ nullRf: false });
    vi.stubGlobal('fetch', fetchMock);
    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <DemandPanel />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('demand-chart')).toHaveTextContent('Oracle, Observed, MA, RF');
    });

    fireEvent.change(screen.getByLabelText('Demand forecast stop'), { target: { value: '5' } });
    await waitFor(() => {
      expect(
        fetchMock.mock.calls.some((c) => c[0] === '/api/demand/forecast?stop=5'),
      ).toBe(true);
    });
  });

  it('DsView filters Schedule by route and supports Compare Static vs Traffic', async () => {
    const fetchMock = createDashboardFetchMock();
    vi.stubGlobal('fetch', fetchMock);
    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <DsView />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('ds-schedule')).toBeInTheDocument();
    });

    fireEvent.change(screen.getByLabelText('Schedule route filter'), { target: { value: '1' } });
    await waitFor(() => {
      expect(
        fetchMock.mock.calls.some((c) => String(c[0]).includes('route=1')),
      ).toBe(true);
    });

    fireEvent.click(screen.getByRole('tab', { name: 'Route optimizer' }));
    fireEvent.click(screen.getByRole('button', { name: 'Compare Static vs Traffic' }));
    await waitFor(() => {
      expect(screen.getByTestId('mode-comparison')).toHaveTextContent('12.0 min');
      expect(screen.getByTestId('mode-comparison')).toHaveTextContent('10.0 min');
    });
  });

  it('App renders workspace tabs and footer with TomTom attribution only when live', async () => {
    vi.stubGlobal('fetch', createDashboardFetchMock({ trafficSource: 'live' }));
    render(<App />);

    await waitFor(() => {
      expect(screen.getByTestId('app-footer')).toHaveTextContent('TomTom live traffic');
    });

    const footer = screen.getByTestId('app-footer');
    expect(footer).toHaveTextContent('OpenStreetMap contributors');
    expect(footer).toHaveTextContent('Simulated demand');
    expect(screen.getByRole('tab', { name: 'Dashboard' })).toBeInTheDocument();
    expect(screen.getByRole('tab', { name: 'Controls' })).toBeInTheDocument();
    expect(screen.getByRole('tab', { name: 'Demand' })).toBeInTheDocument();
    expect(screen.getByRole('tab', { name: 'Data structures' })).toBeInTheDocument();
    expect(screen.getByRole('tab', { name: 'Comparison' })).toBeInTheDocument();
  });

  it('App omits TomTom footer attribution in simulated mode', async () => {
    vi.stubGlobal('fetch', createDashboardFetchMock({ trafficSource: 'simulated' }));
    render(<App />);

    await waitFor(() => {
      expect(screen.getByTestId('traffic-badge')).toHaveTextContent('Simulated');
    });

    const footer = screen.getByTestId('app-footer');
    expect(footer).toHaveTextContent('OpenStreetMap contributors');
    expect(footer).not.toHaveTextContent('TomTom');
  });
});
