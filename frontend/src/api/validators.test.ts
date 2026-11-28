import fs from 'node:fs';
import path from 'node:path';
import { describe, expect, it, vi, afterEach } from 'vitest';
import { getJson } from './client';
import { validateEndpointResponse as validateTs } from './validators';
import { validateEndpointResponse as validateNode } from '../../scripts/contract_check.mjs';

const MOCK_DIR = path.resolve(__dirname, '../../../docs/mock');

function readFixture(name: string): unknown {
  return JSON.parse(fs.readFileSync(path.join(MOCK_DIR, name), 'utf8'));
}

describe('runtime API contract validators & script parity', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('validates all mock GET fixtures in both TypeScript and contract_check.mjs', () => {
    const cases: Array<[string, unknown]> = [
      ['/api/config', readFixture('config.json')],
      ['/api/graph', readFixture('graph.json')],
      ['/api/stops', readFixture('stops.json')],
      ['/api/routes', readFixture('routes.json')],
      ['/api/geometry', readFixture('geometry.json')],
      ['/api/state', readFixture('state.json')],
      ['/api/schedule', readFixture('schedule.json')],
      ['/api/tree', readFixture('tree.json')],
      ['/api/demand/forecast?stop=3', readFixture('forecast_stop3.json')],
      ['/api/metrics', readFixture('metrics.json')],
      ['/api/alerts', readFixture('alerts.json')],
      ['/api/history', readFixture('history.json')],
      ['/api/eval', readFixture('eval.json')],
    ];

    for (const [endpoint, fixture] of cases) {
      expect(validateTs(endpoint, 'GET', fixture)).toEqual({ ok: true });
      expect(validateNode(endpoint, 'GET', fixture)).toEqual({ ok: true });
    }
  });

  it('validates POST fixtures and detects field-level contract violations identically in TS and Node', () => {
    const validOpt = readFixture('optimize.json');
    expect(validateTs('/api/route/optimize', 'POST', validOpt)).toEqual({ ok: true });
    expect(validateNode('/api/route/optimize', 'POST', validOpt)).toEqual({ ok: true });

    const invalidCases: Array<[string, string, unknown]> = [
      ['/api/health', 'GET', { ok: 'yes', sim_running: false }],
      ['/api/state', 'GET', { clock_min: 360 }],
      ['/api/sim/control', 'POST', { clock_min: 480, running: 'no', speed: 60 }],
      ['/api/undo', 'POST', { undone: 123, history_size: 0, geometry_version: 2 }],
    ];

    for (const [endpoint, method, payload] of invalidCases) {
      const resTs = validateTs(endpoint, method, payload);
      const resNode = validateNode(endpoint, method, payload);
      expect(resTs.ok).toBe(false);
      expect(resNode.ok).toBe(false);
    }
  });

  it('logs console.warn in DEV mode when an endpoint response fails validation', async () => {
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        new Response(JSON.stringify({ ok: true, sim_running: 'invalid' }), {
          status: 200,
          headers: { 'Content-Type': 'application/json' },
        }),
      ),
    );

    await getJson('/api/health');
    expect(warnSpy).toHaveBeenCalledTimes(1);
    expect(warnSpy.mock.calls[0][0]).toContain('/api/health');
    expect(warnSpy.mock.calls[0][0]).toContain('sim_running');
  });
});

