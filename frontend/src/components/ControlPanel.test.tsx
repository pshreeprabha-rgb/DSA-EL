import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { AppStateProvider, useAppState } from '../state/AppState';
import { ControlPanel } from './ControlPanel';

function buildFetchMock(options?: { rfLoaded?: boolean }) {
  const rfLoaded = options?.rfLoaded ?? true;
  return vi.fn().mockImplementation((url: string, init?: RequestInit) => {
    if (url === '/api/config') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            stops: 2,
            routes: 2,
            buses: 2,
            reserve: 0,
            seed: 42,
            start_min: 360,
            end_min: 1320,
            slice_min: 15,
            policy: 'fixed_equal',
            predictor: 'ma',
            spacing: 'time',
            reassign: 'drain',
            models: { ma: true, rf: rfLoaded },
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
              {
                idx: 0,
                node: 1,
                name: 'Majestic',
                type: 'hub',
                scale: 1,
                lat: 12.97,
                lng: 77.57,
                route: 0,
                pos: 0,
                event_active: false,
                first_flag_min: -1,
              },
              {
                idx: 1,
                node: 2,
                name: 'Indiranagar',
                type: 'office',
                scale: 1,
                lat: 12.97,
                lng: 77.64,
                route: 1,
                pos: 0,
                event_active: false,
                first_flag_min: -1,
              },
            ],
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/geometry') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            version: 1,
            legs: [
              {
                route: 0,
                leg: 0,
                from_stop: 0,
                to_stop: 1,
                time_s: 120,
                path: [
                  [12.97, 77.57],
                  [12.97, 77.64],
                ],
              },
            ],
            blocked: [],
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/routes') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
            hour: 6,
            basis: 'forecast',
            routes: [
              { id: 0, name: 'Route 1', stops: [0, 1], loop_min: 30, cycle_min: 32, buses: 1, demand: 40, priority: 10 },
              { id: 1, name: 'Route 2', stops: [1, 0], loop_min: 30, cycle_min: 32, buses: 1, demand: 30, priority: 8 },
            ],
            board: [
              { route_id: 0, priority: 10, demand: 40, buses: 1 },
              { route_id: 1, priority: 8, demand: 30, buses: 1 },
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
              arrived: 0, boarded: 0, still_waiting: 0, wait_sum_s: 0, wait_max_s: 0,
              mean_wait_boarded_s: 0, mean_wait_incl_censored_s: 0, p50_s: -1, p95_s: -1,
              full_bus_events: 0, forced_dumps: 0, stranded_by_breakdown: 0, bus_arrivals: 0,
              moves_applied: 0, deadhead_s_total: 0, anomaly_flags: 0, emergency_dispatches: 0,
              stalled_s: 0, mean_load_factor: 0, decisions: 0, decisions_applied: 0,
            },
            routes: [],
            ml: null,
            last_optimize: null,
            traffic: { source: 'simulated', calls: 0, live_corridors: 0, last_error: '' },
          }),
          { status: 200 },
        ),
      );
    }
    if (url === '/api/history') {
      return Promise.resolve(
        new Response(JSON.stringify({ size: 1, entries: ['block route 0 leg 0'] }), { status: 200 }),
      );
    }
    if (url === '/api/state') {
      return Promise.resolve(
        new Response(
          JSON.stringify({
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
            history_size: 1,
            buses: [
              {
                id: 0,
                state: 'running',
                route: 0,
                stop_idx: 0,
                load: 0,
                pending_route: -2,
                motion: { moving: false, leg_route: 0, leg_index: -1, from_stop: 0, to_stop: 0, depart_s: 21600, arrive_s: 21600 },
              },
            ],
            queues: [{ stop: 0, waiting: 2 }],
            summary: { arrived: 2, boarded: 0, still_waiting: 2, mean_wait_boarded_s: 0, mean_wait_incl_censored_s: 0, p95_s: -1 },
          }),
          { status: 200 },
        ),
      );
    }

    if (init?.method === 'POST') {
      if (url === '/api/sim/control') {
        return Promise.resolve(
          new Response(JSON.stringify({ ok: true, clock_min: 480, running: true, speed: 300 }), { status: 200 }),
        );
      }
      if (url === '/api/scenario') {
        return Promise.resolve(
          new Response(JSON.stringify({ scheduled: true, time_min: 375, kind: 'ok' }), { status: 200 }),
        );
      }
      if (url === '/api/allocate') {
        return Promise.resolve(
          new Response(
            JSON.stringify({ policy: 'greedy_marginal', decision: { forced: true, moves_applied: 1 }, counts: [1, 1] }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/inject') {
        return Promise.resolve(
          new Response(JSON.stringify({ stop: 0, count: 150, time_s: 21600 }), { status: 200 }),
        );
      }
      if (url === '/api/undo') {
        return Promise.resolve(
          new Response(JSON.stringify({ undone: 'block route 0 leg 0', history_size: 0, geometry_version: 2 }), { status: 200 }),
        );
      }
    }

    return Promise.resolve(new Response('{}', { status: 200 }));
  });
}

function getLastPostCall(fetchMock: ReturnType<typeof vi.fn>, url: string) {
  const calls = fetchMock.mock.calls.filter(
    (c) => c[0] === url && (c[1] as RequestInit | undefined)?.method === 'POST',
  );
  const last = calls[calls.length - 1];
  return {
    url: last[0] as string,
    init: last[1] as RequestInit,
    body: JSON.parse(String((last[1] as RequestInit).body)),
  };
}

describe('ControlPanel', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  const renderPanel = async (rfLoaded = true) => {
    const fetchMock = buildFetchMock({ rfLoaded });
    vi.stubGlobal('fetch', fetchMock);
    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <ControlPanel />
      </AppStateProvider>,
    );
    await waitFor(() => {
      expect(screen.getByTestId('control-history-stack')).toBeInTheDocument();
    });
    return fetchMock;
  };

  it('sends exact POST payload for Start action', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: 'Start' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/sim/control').body).toEqual({ action: 'start' });
    });
  });

  it('sends exact POST payload for Pause action', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: 'Pause' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/sim/control').body).toEqual({ action: 'pause' });
    });
  });

  it('sends exact POST payload for Resume action', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: 'Resume' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/sim/control').body).toEqual({ action: 'resume' });
    });
  });

  it('sends exact POST payload for Speed preset', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: '300x' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/sim/control').body).toEqual({ action: 'speed', speed: 300 });
    });
  });

  it('sends exact POST payload for Seek HH:MM', async () => {
    const fetchMock = await renderPanel();
    fireEvent.change(screen.getByLabelText('Seek time HH:MM'), { target: { value: '09:15' } });
    fireEvent.click(screen.getByRole('button', { name: 'Seek' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/sim/control').body).toEqual({ action: 'seek', time_min: 555 });
    });
  });

  it('asks for confirmation before Reset and does not post when cancelled', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: 'Reset' }));
    expect(screen.getByRole('dialog', { name: 'Confirm simulation reset' })).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(
      fetchMock.mock.calls.filter(
        (c) => c[0] === '/api/sim/control' && (c[1] as RequestInit | undefined)?.method === 'POST',
      ),
    ).toHaveLength(0);

    fireEvent.click(screen.getByRole('button', { name: 'Reset' }));
    fireEvent.click(screen.getByRole('button', { name: 'Confirm Reset' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/sim/control').body).toEqual({ action: 'reset' });
    });
  });

  it('sends exact POST payload for Rain On and Rain Off', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: 'Rain On' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/scenario').body).toEqual({ kind: 'rain_on' });
    });

    fireEvent.click(screen.getByRole('button', { name: 'Rain Off' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/scenario').body).toEqual({ kind: 'rain_off' });
    });
  });

  it('sends exact POST payload for Event On and Event Off', async () => {
    const fetchMock = await renderPanel();
    fireEvent.change(screen.getByLabelText('Event stop'), { target: { value: '1' } });
    fireEvent.click(screen.getByRole('button', { name: 'Event On' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/scenario').body).toEqual({ kind: 'event_on', stop: 1 });
    });

    fireEvent.click(screen.getByRole('button', { name: 'Event Off' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/scenario').body).toEqual({ kind: 'event_off', stop: 1 });
    });
  });

  it('sends exact POST payload for Block Road and Unblock Road', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: 'Block Road' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/scenario').body).toEqual({
        kind: 'block_edge',
        route: 0,
        leg: 0,
      });
    });

    fireEvent.click(screen.getByRole('button', { name: 'Unblock Road' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/scenario').body).toEqual({
        kind: 'unblock_edge',
        route: 0,
        leg: 0,
      });
    });
  });

  it('sends exact POST payload for Block Road, shows time_min toast, and pans once new block appears at next slice', async () => {
    let geometryVersion = 1;
    let blockedSegs: Array<[[number, number], [number, number]]> = [];
    const baseMock = buildFetchMock();
    const baseImpl = baseMock.getMockImplementation()!;

    const fetchMock = vi.fn().mockImplementation((url: string, init?: RequestInit) => {
      if (url === '/api/state') {
        return Promise.resolve(
          new Response(
            JSON.stringify({
              clock_min: 360,
              clock: '06:00',
              running: true,
              finished: false,
              speed: 60,
              day_type: 'weekday',
              rain: false,
              policy: 'fixed_equal',
              traffic_source: 'simulated',
              geometry_version: geometryVersion,
              history_size: 1,
              buses: [
                {
                  id: 0,
                  state: 'running',
                  route: 0,
                  stop_idx: 0,
                  load: 0,
                  pending_route: -2,
                  motion: { moving: false, leg_route: 0, leg_index: -1, from_stop: 0, to_stop: 0, depart_s: 21600, arrive_s: 21600 },
                },
              ],
              queues: [{ stop: 0, waiting: 2 }],
              summary: { arrived: 2, boarded: 0, still_waiting: 2, mean_wait_boarded_s: 0, mean_wait_incl_censored_s: 0, p95_s: -1 },
            }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/geometry') {
        return Promise.resolve(
          new Response(
            JSON.stringify({
              version: geometryVersion,
              legs: [
                {
                  route: 0,
                  leg: 0,
                  from_stop: 0,
                  to_stop: 1,
                  time_s: 120,
                  path: [
                    [12.97, 77.57],
                    [12.97, 77.64],
                  ],
                },
              ],
              blocked: blockedSegs,
            }),
            { status: 200 },
          ),
        );
      }
      return baseImpl(url, init);
    });
    vi.stubGlobal('fetch', fetchMock);

    function Harness() {
      const { panTarget, pollStateNow } = useAppState();
      return (
        <>
          <div data-testid="pan-target">
            {panTarget ? `${panTarget.lat.toFixed(5)},${panTarget.lng.toFixed(5)}` : 'none'}
          </div>
          <button type="button" onClick={() => void pollStateNow()}>
            Trigger Poll
          </button>
          <ControlPanel />
        </>
      );
    }

    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <Harness />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('control-history-stack')).toBeInTheDocument();
    });

    fireEvent.click(screen.getByRole('button', { name: 'Block Road' }));
    await waitFor(() => {
      expect(screen.getByTestId('control-toast')).toHaveTextContent('06:15');
      expect(screen.getByTestId('control-toast')).toHaveTextContent('375 min');
    });
    expect(screen.getByTestId('pan-target')).toHaveTextContent('none');

    // Simulate next 15-minute slice applying the scheduled block
    geometryVersion = 2;
    blockedSegs = [[[12.971, 77.571], [12.9712, 77.5714]]];
    fireEvent.click(screen.getByRole('button', { name: 'Trigger Poll' }));

    await waitFor(() => {
      expect(screen.getByTestId('pan-target')).toHaveTextContent('12.97110,77.57120');
    });
  });

  it('sends exact POST payload for Breakdown and Repair', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: 'Breakdown' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/scenario').body).toEqual({
        kind: 'breakdown',
        bus: 0,
      });
    });

    fireEvent.click(screen.getByRole('button', { name: 'Repair' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/scenario').body).toEqual({
        kind: 'repair',
        bus: 0,
      });
    });
  });

  it('sends exact POST payload for Inject Burst', async () => {
    const fetchMock = await renderPanel();
    fireEvent.change(screen.getByLabelText('Inject passenger count'), { target: { value: '250' } });
    fireEvent.click(screen.getByRole('button', { name: 'Inject Burst' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/inject').body).toEqual({
        stop: 0,
        count: 250,
      });
    });
  });

  it('sends exact POST payload for Re-allocate now', async () => {
    const fetchMock = await renderPanel();
    fireEvent.change(screen.getByLabelText('Allocation policy'), { target: { value: 'greedy_marginal' } });
    fireEvent.change(screen.getByLabelText('Demand predictor'), { target: { value: 'rf' } });
    fireEvent.click(screen.getByRole('button', { name: 'Re-allocate now' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/allocate').body).toEqual({
        policy: 'greedy_marginal',
        predictor: 'rf',
        force: true,
      });
    });
  });

  it('sends exact POST payload for manual bus move', async () => {
    const fetchMock = await renderPanel();
    fireEvent.change(screen.getByLabelText('Move target route'), { target: { value: '1' } });
    fireEvent.click(screen.getByRole('button', { name: 'Move Bus' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/allocate').body).toEqual({
        manual_move: { bus: 0, to_route: 1 },
      });
    });
  });

  it('sends exact POST payload for Undo', async () => {
    const fetchMock = await renderPanel();
    fireEvent.click(screen.getByRole('button', { name: 'Undo' }));
    await waitFor(() => {
      expect(getLastPostCall(fetchMock, '/api/undo').body).toEqual({});
    });
  });

  it('disables rf option when config.models.rf is false', async () => {
    await renderPanel(false);
    const rfOption = screen.getByRole('option', { name: /rf/i }) as HTMLOptionElement;
    expect(rfOption.disabled).toBe(true);
  });

  it('disables button while pending, re-enables after error, and displays error code toast', async () => {
    const fetchMock = buildFetchMock();
    let rejectPost!: (err: unknown) => void;
    const baseImpl = fetchMock.getMockImplementation()!;

    fetchMock.mockImplementation((url: string, init?: RequestInit) => {
      if (url === '/api/undo' && init?.method === 'POST') {
        return new Promise((_resolve, reject) => {
          rejectPost = reject;
        });
      }
      return baseImpl(url, init);
    });
    vi.stubGlobal('fetch', fetchMock);

    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <ControlPanel />
      </AppStateProvider>,
    );
    await waitFor(() => {
      expect(screen.getByRole('button', { name: 'Undo' })).toBeEnabled();
    });

    const undoBtn = screen.getByRole('button', { name: 'Undo' });
    fireEvent.click(undoBtn);
    expect(undoBtn).toBeDisabled();

    fetchMock.mockImplementation((url: string, init?: RequestInit) => {
      if (url === '/api/undo' && init?.method === 'POST') {
        return Promise.resolve(
          new Response(
            JSON.stringify({ error: { code: 'conflict', message: 'History empty' } }),
            { status: 409 },
          ),
        );
      }
      return baseImpl(url, init);
    });

    // Reject first inflight attempt via server 409 instead
    rejectPost(new TypeError('network down'));
    await waitFor(() => {
      expect(screen.getByRole('button', { name: 'Undo' })).toBeEnabled();
    });

    fireEvent.click(screen.getByRole('button', { name: 'Undo' }));
    await waitFor(() => {
      expect(screen.getByTestId('control-toast')).toHaveTextContent('conflict');
    });
  });
});
