import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { AppStateProvider, useAppState } from '../state/AppState';
import { MapView } from './MapView';

describe('MapView blocked edge overlays', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('draws blocked line above routes with fixed-size Road blocked midpoint marker and removes it on undo/unblock', async () => {
    let geometryVersion = 1;
    let blockedSegs: Array<[[number, number], [number, number]]> = [
      [
        [12.94, 77.54],
        [12.9403, 77.5403],
      ],
    ];

    const fetchMock = vi.fn().mockImplementation((url: string) => {
      if (url === '/api/config') {
        return Promise.resolve(
          new Response(
            JSON.stringify({
              stops: 2,
              routes: 1,
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
          new Response(
            JSON.stringify({
              stops: [
                { idx: 0, node: 1, name: 'Stop A', type: 'hub', scale: 1, lat: 12.94, lng: 77.54, route: 0, pos: 0, event_active: false, first_flag_min: -1 },
                { idx: 1, node: 2, name: 'Stop B', type: 'office', scale: 1, lat: 12.95, lng: 77.55, route: 0, pos: 1, event_active: false, first_flag_min: -1 },
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
              version: geometryVersion,
              legs: [
                {
                  route: 0,
                  leg: 0,
                  from_stop: 0,
                  to_stop: 1,
                  time_s: 120,
                  path: [
                    [12.94, 77.54],
                    [12.95, 77.55],
                  ],
                },
              ],
              blocked: blockedSegs,
            }),
            { status: 200 },
          ),
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
              geometry_version: geometryVersion,
              history_size: blockedSegs.length,
              buses: [],
              queues: [
                { stop: 0, waiting: 0 },
                { stop: 1, waiting: 0 },
              ],
              summary: {
                arrived: 0,
                boarded: 0,
                still_waiting: 0,
                mean_wait_boarded_s: 0,
                mean_wait_incl_censored_s: 0,
                p95_s: -1,
              },
            }),
            { status: 200 },
          ),
        );
      }
      return Promise.resolve(new Response('{}', { status: 200 }));
    });
    vi.stubGlobal('fetch', fetchMock);

    function Harness() {
      const { pollStateNow } = useAppState();
      return (
        <>
          <button type="button" onClick={() => void pollStateNow()}>
            Refresh
          </button>
          <MapView />
        </>
      );
    }

    const { container } = render(
      <AppStateProvider pollIntervalMs={60_000}>
        <Harness />
      </AppStateProvider>,
    );

    await waitFor(() => {
      expect(screen.getByText('Road blocked')).toBeInTheDocument();
    });

    const blockedRoadsPane = container.querySelector('.leaflet-blocked-roads-pane') as HTMLElement | null;
    const blockedMarkersPane = container.querySelector('.leaflet-blocked-markers-pane') as HTMLElement | null;
    expect(blockedRoadsPane).not.toBeNull();
    expect(blockedMarkersPane).not.toBeNull();
    expect(Number(blockedRoadsPane!.style.zIndex)).toBeGreaterThan(400);
    expect(Number(blockedMarkersPane!.style.zIndex)).toBeGreaterThan(600);

    // Undo / unblock clears blocked array and removes marker
    geometryVersion = 2;
    blockedSegs = [];
    fireEvent.click(screen.getByRole('button', { name: 'Refresh' }));

    await waitFor(() => {
      expect(screen.queryByText('Road blocked')).toBeNull();
    });
  });
});

