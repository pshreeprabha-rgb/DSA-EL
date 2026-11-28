import { describe, it, expect } from 'vitest';
import type { BusState, GeometryResponse, StopInfo } from '../api/types';
import {
  blockedSegmentKey,
  busPosition,
  haversineMeters,
  legProgress,
  pointAlong,
  segmentMidpoint,
  simNowS,
} from './interpolate';

describe('interpolate helpers', () => {
  it('computes pointAlong midpoint on a uniform 2-point equator segment', () => {
    const p = pointAlong(
      [
        [0, 0],
        [0, 2],
      ],
      0.5,
    );
    expect(p[0]).toBeCloseTo(0, 6);
    expect(p[1]).toBeCloseTo(1, 3);
  });

  it('weights pointAlong by Haversine length on uneven multi-point path', () => {
    // Equator degrees: [0,0]->[0,1] is 1 deg, [0,1]->[0,3] is 2 deg; total 3, half is 1.5 deg
    const p = pointAlong(
      [
        [0, 0],
        [0, 1],
        [0, 3],
      ],
      0.5,
    );
    expect(p[0]).toBeCloseTo(0, 6);
    expect(p[1]).toBeCloseTo(1.5, 3);
  });

  it('returns endpoints at fraction 0 and 1 and clamps out-of-range fractions', () => {
    const pts = [
      [12.9, 77.5],
      [12.95, 77.55],
    ] as const;
    expect(pointAlong(pts, 0)).toEqual([12.9, 77.5]);
    expect(pointAlong(pts, 1)).toEqual([12.95, 77.55]);
    expect(pointAlong(pts, -1)).toEqual([12.9, 77.5]);
    expect(pointAlong(pts, 2)).toEqual([12.95, 77.55]);
  });

  it('handles single-point and zero-length paths safely', () => {
    expect(pointAlong([[12.97, 77.59]], 0.5)).toEqual([12.97, 77.59]);
    expect(
      pointAlong(
        [
          [12.97, 77.59],
          [12.97, 77.59],
        ],
        0.5,
      ),
    ).toEqual([12.97, 77.59]);
    expect(haversineMeters([12.97, 77.59], [12.97, 77.59])).toBe(0);
  });

  it('computes legProgress with clamping and degenerate arrive <= depart', () => {
    expect(legProgress(100, 200, 150)).toBeCloseTo(0.5, 6);
    expect(legProgress(100, 200, 50)).toBe(0);
    expect(legProgress(100, 200, 250)).toBe(1);
    expect(legProgress(200, 200, 150)).toBe(1);
    expect(legProgress(250, 200, 225)).toBe(1);
  });

  it('advances simNowS when running, freezes when paused/finished, and caps at window end', () => {
    expect(
      simNowS(
        { clock_min: 360, running: true, finished: false, speed: 60 },
        10_000,
        11_000,
      ),
    ).toBeCloseTo(21_660, 6);

    expect(
      simNowS(
        { clock_min: 360, running: false, finished: false, speed: 60 },
        10_000,
        11_000,
      ),
    ).toBeCloseTo(21_600, 6);

    expect(
      simNowS(
        { clock_min: 1319.9, running: true, finished: false, speed: 60 },
        10_000,
        20_000,
      ),
    ).toBeCloseTo(79_200, 6);
  });

  it('computes busPosition for dwelling, moving along leg, and deadheading buses', () => {
    const stops: StopInfo[] = [
      {
        idx: 0,
        node: 10,
        name: 'Stop A',
        type: 'hub',
        scale: 1,
        lat: 0,
        lng: 0,
        route: 0,
        pos: 0,
        event_active: false,
        first_flag_min: -1,
      },
      {
        idx: 1,
        node: 20,
        name: 'Stop B',
        type: 'office',
        scale: 1,
        lat: 0,
        lng: 2,
        route: 0,
        pos: 1,
        event_active: false,
        first_flag_min: -1,
      },
    ];

    const geometry: GeometryResponse = {
      version: 1,
      legs: [
        {
          route: 0,
          leg: 0,
          from_stop: 0,
          to_stop: 1,
          time_s: 100,
          path: [
            [0, 0],
            [0, 2],
          ],
        },
      ],
      blocked: [],
    };

    const dwellingBus: BusState = {
      id: 0,
      state: 'running',
      route: 0,
      stop_idx: 1,
      load: 5,
      pending_route: -2,
      motion: {
        moving: false,
        leg_route: 0,
        leg_index: -1,
        from_stop: 1,
        to_stop: 1,
        depart_s: 21600,
        arrive_s: 21600,
      },
    };
    expect(busPosition(dwellingBus, geometry, stops, 21600)).toEqual([0, 2]);

    const movingBus: BusState = {
      ...dwellingBus,
      id: 1,
      stop_idx: 0,
      motion: {
        moving: true,
        leg_route: 0,
        leg_index: 0,
        from_stop: 0,
        to_stop: 1,
        depart_s: 100,
        arrive_s: 200,
      },
    };
    const mid = busPosition(movingBus, geometry, stops, 150);
    expect(mid).not.toBeNull();
    expect(mid![0]).toBeCloseTo(0, 6);
    expect(mid![1]).toBeCloseTo(1, 3);

    const deadheadBus: BusState = {
      id: 2,
      state: 'deadhead',
      route: 0,
      stop_idx: 0,
      load: 0,
      pending_route: -2,
      motion: {
        moving: true,
        leg_route: 0,
        leg_index: -1,
        from_stop: 0,
        to_stop: 1,
        depart_s: 100,
        arrive_s: 200,
      },
    };
    const deadMid = busPosition(deadheadBus, geometry, stops, 150);
    expect(deadMid).not.toBeNull();
    expect(deadMid![0]).toBeCloseTo(0, 6);
    expect(deadMid![1]).toBeCloseTo(1, 3);
  });

  it('computes segmentMidpoint and blockedSegmentKey for 30 m blocked edges', () => {
    const seg = [
      [12.94, 77.54],
      [12.9402, 77.5404],
    ] as const;
    const mid = segmentMidpoint(seg);
    expect(mid[0]).toBeCloseTo(12.9401, 6);
    expect(mid[1]).toBeCloseTo(77.5402, 6);
    expect(blockedSegmentKey(seg)).toBe('12.9400000,77.5400000|12.9402000,77.5404000');
  });
});

