import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { memo, Profiler } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { AppStateProvider, useAppState } from '../state/AppState';
import { MapView } from './MapView';

describe('MapView render stability under 1 Hz state polling', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('does not re-render MapView when /api/state ticks repeatedly without geometry changes', async () => {
    let clockMin = 360;
    const fetchMock = vi.fn().mockImplementation((url: string) => {
      if (url === '/api/config') {
        return Promise.resolve(
          new Response(
            JSON.stringify({
              stops: 2, routes: 1, buses: 1, reserve: 0, seed: 42,
              start_min: 360, end_min: 1320, slice_min: 15,
              policy: 'fixed_equal', predictor: 'ma', spacing: 'time', reassign: 'drain',
              models: { ma: true, rf: true }, offline: true,
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
                { idx: 0, node: 1, name: 'S0', type: 'hub', scale: 1, lat: 12.94, lng: 77.54, route: 0, pos: 0, event_active: false, first_flag_min: -1 },
                { idx: 1, node: 2, name: 'S1', type: 'office', scale: 1, lat: 12.95, lng: 77.55, route: 0, pos: 1, event_active: false, first_flag_min: -1 },
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
              legs: [{ route: 0, leg: 0, from_stop: 0, to_stop: 1, time_s: 120, path: [[12.94, 77.54], [12.95, 77.55]] }],
              blocked: [],
            }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/routes') {
        return Promise.resolve(
          new Response(
            JSON.stringify({ hour: 6, basis: 'forecast', routes: [], board: [] }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/metrics') {
        return Promise.resolve(
          new Response(
            JSON.stringify({
              sim: {
                arrived: 10, boarded: 5, still_waiting: 5, wait_sum_s: 600, wait_max_s: 200,
                mean_wait_boarded_s: 120, mean_wait_incl_censored_s: 130, p50_s: 110, p95_s: 200,
                full_bus_events: 0, forced_dumps: 0, stranded_by_breakdown: 0, bus_arrivals: 2,
                moves_applied: 0, deadhead_s_total: 0, anomaly_flags: 0, emergency_dispatches: 0,
                stalled_s: 0, mean_load_factor: 0.1, decisions: 0, decisions_applied: 0,
              },
              routes: [], ml: null, last_optimize: null,
              traffic: { source: 'simulated', calls: 0, live_corridors: 0, last_error: '' },
            }),
            { status: 200 },
          ),
        );
      }
      if (url === '/api/state') {
        clockMin += 1;
        return Promise.resolve(
          new Response(
            JSON.stringify({
              clock_min: clockMin,
              clock: `06:${String(clockMin - 360).padStart(2, '0')}`,
              running: true,
              finished: false,
              speed: 300,
              day_type: 'weekday',
              rain: false,
              policy: 'fixed_equal',
              traffic_source: 'simulated',
              geometry_version: 1,
              history_size: 0,
              buses: [
                {
                  id: 0, state: 'running', route: 0, stop_idx: 0, load: 5, pending_route: -2,
                  motion: { moving: true, leg_route: 0, leg_index: 0, from_stop: 0, to_stop: 1, depart_s: 21600, arrive_s: 21720 },
                },
              ],
              queues: [{ stop: 0, waiting: clockMin % 10 }, { stop: 1, waiting: 1 }],
              summary: { arrived: 10, boarded: 5, still_waiting: 5, mean_wait_boarded_s: 120, mean_wait_incl_censored_s: 130, p95_s: 200 },
            }),
            { status: 200 },
          ),
        );
      }
      return Promise.resolve(new Response('{}', { status: 200 }));
    });
    vi.stubGlobal('fetch', fetchMock);

    let mapRenders = 0;

    function StateTicker() {
      const { state, pollStateNow } = useAppState();
      return (
        <div>
          <span data-testid="tick-clock">{state?.clock_min ?? 0}</span>
          <button type="button" onClick={() => void pollStateNow()}>
            Tick State
          </button>
        </div>
      );
    }

    const ProfiledMap = memo(function ProfiledMap() {
      return (
        <Profiler
          id="map-view"
          onRender={() => {
            mapRenders += 1;
          }}
        >
          <MapView />
        </Profiler>
      );
    });

    render(
      <AppStateProvider pollIntervalMs={60_000}>
        <StateTicker />
        <ProfiledMap />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByTestId('tick-clock')).toHaveTextContent('361');
      expect(
        fetchMock.mock.calls.some((call) => call[0] === '/api/geometry'),
      ).toBe(true);
    });

    // Trigger one settle tick so initial config/stops/geometry effects have committed.
    fireEvent.click(screen.getByRole('button', { name: 'Tick State' }));
    await waitFor(() => {
      expect(screen.getByTestId('tick-clock')).toHaveTextContent('362');
    });

    const rendersAfterBoot = mapRenders;

    for (let tick = 3; tick <= 12; tick += 1) {
      fireEvent.click(screen.getByRole('button', { name: 'Tick State' }));
      await waitFor(() => {
        expect(screen.getByTestId('tick-clock')).toHaveTextContent(String(360 + tick));
      });
    }

    expect(mapRenders).toBe(rendersAfterBoot);
  });
});
