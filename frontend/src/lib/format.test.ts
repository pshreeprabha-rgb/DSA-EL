import { describe, it, expect } from 'vitest';
import {
  boardDisplayOrder,
  busStateColor,
  ciLabel,
  ciSignificance,
  clockHHMM,
  minutesLabel,
  parseClockHHMM,
  ROUTE_PALETTE,
  routeColor,
  stackTopFirst,
  stopTypeColor,
  topQueuesByStop,
} from './format';

describe('format helpers', () => {
  it('formats clockHHMM for standard boundary and fractional minutes', () => {
    expect(clockHHMM(360)).toBe('06:00');
    expect(clockHHMM(1319.5)).toBe('21:59');
    expect(clockHHMM(0)).toBe('00:00');
    expect(clockHHMM(1440)).toBe('24:00');
  });

  it('formats minutesLabel from seconds', () => {
    expect(minutesLabel(750)).toBe('12.5 min');
    expect(minutesLabel(0)).toBe('0.0 min');
    expect(minutesLabel(-1)).toBe('N/A');
  });

  it('formats confidence intervals with ciLabel', () => {
    expect(ciLabel(12.34, 15.66)).toBe('[12.3, 15.7]');
  });

  it('returns distinct colors for all 6 routes and all stop/bus states', () => {
    const colors = new Set(Array.from({ length: 6 }, (_, i) => routeColor(i)));
    expect(colors.size).toBe(6);
    expect(ROUTE_PALETTE.length).toBe(6);
    expect(routeColor(-1)).toBe('#64748b');

    expect(stopTypeColor('hub')).not.toBe(stopTypeColor('office'));
    expect(stopTypeColor('market')).not.toBe(stopTypeColor('residential'));
    expect(busStateColor('running')).not.toBe(busStateColor('broken'));
  });

  it('preserves server heap order in boardDisplayOrder', () => {
    const serverBoard = [
      { route_id: 3, priority: 21.1 },
      { route_id: 1, priority: 20.8 },
      { route_id: 2, priority: 15.3 },
    ];
    expect(boardDisplayOrder(serverBoard)).toEqual(serverBoard);
  });

  it('derives significance from paired CI bounds', () => {
    expect(ciSignificance(-80.5, -32.2)).toBe('significant');
    expect(ciSignificance(5.1, 14.9)).toBe('significant');
    expect(ciSignificance(-47.5, 2.9)).toBe('inconclusive');
    expect(ciSignificance(0, 0)).toBe('inconclusive');
  });

  it('sorts top queues descending capped at limit with stop names', () => {
    const queues = Array.from({ length: 20 }, (_, i) => ({
      stop: i,
      waiting: i * 3,
    }));
    const stops = queues.map((q) => ({
      idx: q.stop,
      node: q.stop,
      name: `S${q.stop}`,
      type: 'market',
      scale: 1,
      lat: 12.9,
      lng: 77.5,
      route: 0,
      pos: 0,
      event_active: false,
      first_flag_min: -1,
    }));
    const top = topQueuesByStop(queues, stops, 15);
    expect(top).toHaveLength(15);
    expect(top[0]).toEqual({ stop: 19, name: 'S19', waiting: 57 });
    expect(top[14]).toEqual({ stop: 5, name: 'S5', waiting: 15 });
  });

  it('marks index 0 as stack top in stackTopFirst', () => {
    expect(stackTopFirst(['newest', 'older'])).toEqual([
      { text: 'newest', isTop: true },
      { text: 'older', isTop: false },
    ]);
  });

  it('parses valid HH:MM clock times and rejects malformed input', () => {
    expect(parseClockHHMM('08:00')).toBe(480);
    expect(parseClockHHMM('24:00')).toBe(1440);
    expect(parseClockHHMM('25:00')).toBeNull();
    expect(parseClockHHMM('08:99')).toBeNull();
  });
});
