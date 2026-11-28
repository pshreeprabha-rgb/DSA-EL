import { describe, it, expect } from 'vitest';
import {
  busStateColor,
  ciLabel,
  clockHHMM,
  minutesLabel,
  ROUTE_PALETTE,
  routeColor,
  stopTypeColor,
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
});

