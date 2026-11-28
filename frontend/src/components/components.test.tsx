import { render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { AppStateProvider, useAppState } from '../state/AppState';
import { Badge } from './Badge';
import { ClockDisplay } from './ClockDisplay';
import { ConnectionBanner } from './ConnectionBanner';

describe('UI components', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('Badge renders Live when traffic_source is live', () => {
    render(<Badge trafficSource="live" />);
    expect(screen.getByTestId('traffic-badge')).toHaveTextContent('Live');
  });

  it('Badge renders Simulated when traffic_source is simulated or missing', () => {
    const { rerender } = render(<Badge trafficSource="simulated" />);
    expect(screen.getByTestId('traffic-badge')).toHaveTextContent('Simulated');

    rerender(<Badge />);
    expect(screen.getByTestId('traffic-badge')).toHaveTextContent('Simulated');
  });

  it('ClockDisplay renders 06:00 for 360 and floors fractional minutes', () => {
    const { rerender } = render(<ClockDisplay clockMin={360} />);
    expect(screen.getByTestId('clock-display')).toHaveTextContent('06:00');

    rerender(<ClockDisplay clockMin={1319.5} />);
    expect(screen.getByTestId('clock-display')).toHaveTextContent('21:59');
  });

  it('ConnectionBanner shows after failure and hides when connected', () => {
    const { rerender } = render(
      <ConnectionBanner connected={false} message="Backend not reachable" />,
    );
    expect(screen.getByRole('alert')).toHaveTextContent(
      'Backend not reachable',
    );

    rerender(<ConnectionBanner connected={true} message={null} />);
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('AppStateProvider shows Backend not reachable after a failed poll and clears on recovery', async () => {
    let stateCalls = 0;
    const baseState = {
      clock_min: 360,
      clock: '06:00',
      running: true,
      finished: false,
      speed: 60,
      day_type: 'weekday',
      rain: false,
      policy: 'fixed_equal',
      traffic_source: 'simulated',
      geometry_version: 1,
      history_size: 0,
      buses: [],
      queues: [],
      summary: {
        arrived: 0,
        boarded: 0,
        still_waiting: 0,
        mean_wait_boarded_s: 0,
        mean_wait_incl_censored_s: 0,
        p95_s: -1,
      },
    };

    const fetchMock = vi.fn().mockImplementation((url: string) => {
      if (url === '/api/config') {
        return Promise.resolve(
          new Response(
            JSON.stringify({
              stops: 0,
              routes: 6,
              buses: 0,
              reserve: 0,
              seed: 42,
              start_min: 360,
              end_min: 1320,
              slice_min: 15,
              policy: 'fixed_equal',
              predictor: 'ma',
              spacing: 'time',
              reassign: 'drain',
              models: { ma: true, rf: true },
              offline: true,
            }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/stops') {
        return Promise.resolve(
          new Response(JSON.stringify({ stops: [] }), { status: 200 }),
        );
      }
      if (url === '/api/geometry') {
        return Promise.resolve(
          new Response(
            JSON.stringify({ version: 1, legs: [], blocked: [] }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/state') {
        stateCalls += 1;
        if (stateCalls === 1) {
          return Promise.reject(new TypeError('ECONNREFUSED'));
        }
        return Promise.resolve(
          new Response(JSON.stringify(baseState), { status: 200 }),
        );
      }
      return Promise.resolve(new Response('{}', { status: 200 }));
    });

    vi.stubGlobal('fetch', fetchMock);

    function Harness() {
      const { connectionStatus, connectionMessage } = useAppState();
      return (
        <ConnectionBanner
          connected={connectionStatus === 'connected'}
          message={connectionMessage}
        />
      );
    }

    render(
      <AppStateProvider pollIntervalMs={40}>
        <Harness />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByRole('alert')).toHaveTextContent(
        'Backend not reachable',
      );
    });

    await waitFor(() => {
      expect(screen.queryByRole('alert')).toBeNull();
    });
  });

  it('AppStateProvider refetches /api/geometry only when geometry_version changes', async () => {
    let statePollCount = 0;
    let geometryFetchCount = 0;

    const makeState = (geometryVersion: number) => ({
      clock_min: 360 + statePollCount,
      clock: '06:00',
      running: true,
      finished: false,
      speed: 60,
      day_type: 'weekday',
      rain: false,
      policy: 'fixed_equal',
      traffic_source: 'simulated',
      geometry_version: geometryVersion,
      history_size: 0,
      buses: [],
      queues: [],
      summary: {
        arrived: 0,
        boarded: 0,
        still_waiting: 0,
        mean_wait_boarded_s: 0,
        mean_wait_incl_censored_s: 0,
        p95_s: -1,
      },
    });

    const fetchMock = vi.fn().mockImplementation((url: string) => {
      if (url === '/api/config') {
        return Promise.resolve(
          new Response(
            JSON.stringify({
              stops: 0,
              routes: 6,
              buses: 0,
              reserve: 0,
              seed: 42,
              start_min: 360,
              end_min: 1320,
              slice_min: 15,
              policy: 'fixed_equal',
              predictor: 'ma',
              spacing: 'time',
              reassign: 'drain',
              models: { ma: true, rf: true },
              offline: true,
            }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/stops') {
        return Promise.resolve(
          new Response(JSON.stringify({ stops: [] }), { status: 200 }),
        );
      }
      if (url === '/api/geometry') {
        geometryFetchCount += 1;
        return Promise.resolve(
          new Response(
            JSON.stringify({
              version: geometryFetchCount,
              legs: [],
              blocked: [],
            }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/state') {
        statePollCount += 1;
        const version = statePollCount >= 3 ? 2 : 1;
        return Promise.resolve(
          new Response(JSON.stringify(makeState(version)), { status: 200 }),
        );
      }
      return Promise.resolve(new Response('{}', { status: 200 }));
    });

    vi.stubGlobal('fetch', fetchMock);

    function Observer() {
      const { state, geometry } = useAppState();
      return (
        <div data-testid="geo-ver">
          {state?.geometry_version ?? 0}:{geometry?.version ?? 0}
        </div>
      );
    }

    render(
      <AppStateProvider pollIntervalMs={30}>
        <Observer />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('geo-ver')).toHaveTextContent('2:2');
    });

    const configCalls = fetchMock.mock.calls.filter(
      (call) => call[0] === '/api/config',
    ).length;
    const stopsCalls = fetchMock.mock.calls.filter(
      (call) => call[0] === '/api/stops',
    ).length;

    expect(configCalls).toBe(1);
    expect(stopsCalls).toBe(1);
    expect(geometryFetchCount).toBe(2);
  });
});

