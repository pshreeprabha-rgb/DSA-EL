import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const MOCK_DIR = path.resolve(__dirname, '../../docs/mock');
const PORT = Number(process.env.MOCK_PORT || 8080);
const DWELL_S = 20;

function readMockJson(filename) {
  const fullPath = path.join(MOCK_DIR, filename);
  return JSON.parse(fs.readFileSync(fullPath, 'utf8'));
}

const mockConfig = readMockJson('config.json');
const mockGraph = readMockJson('graph.json');
const mockStops = readMockJson('stops.json');
const mockRoutes = readMockJson('routes.json');
const mockGeometry = readMockJson('geometry.json');
const mockBaseState = readMockJson('state.json');
const mockSchedule = readMockJson('schedule.json');
const mockTree = readMockJson('tree.json');
const mockForecastStop3 = readMockJson('forecast_stop3.json');
const mockMetrics = readMockJson('metrics.json');
const mockAlerts = readMockJson('alerts.json');
const mockHistory = readMockJson('history.json');
const mockEval = readMockJson('eval.json');
const mockOptimize = readMockJson('optimize.json');

const legsByRoute = new Map();
for (const leg of mockGeometry.legs) {
  const list = legsByRoute.get(leg.route) ?? [];
  list.push(leg);
  legsByRoute.set(leg.route, list);
}
for (const [routeId, list] of legsByRoute.entries()) {
  list.sort((a, b) => a.leg - b.leg);
  legsByRoute.set(routeId, list);
}

const busInitialOffsetS = new Map();
for (const bus of mockBaseState.buses) {
  const legs = legsByRoute.get(bus.route) ?? [];
  let offset = 0;
  for (const leg of legs) {
    if (leg.from_stop === bus.stop_idx) {
      break;
    }
    offset += Math.max(1, leg.time_s) + DWELL_S;
  }
  busInitialOffsetS.set(bus.id, offset);
}

const sim = {
  anchorClockMin: mockBaseState.clock_min,
  anchorRealMs: Date.now(),
  running: true,
  finished: false,
  speed: mockBaseState.speed || 60,
  startMin: mockConfig.start_min ?? 360,
  endMin: mockConfig.end_min ?? 1320,
};

function formatClock(minutes) {
  const total = Math.max(0, Math.floor(minutes));
  const hh = Math.floor(total / 60);
  const mm = total % 60;
  return `${String(hh).padStart(2, '0')}:${String(mm).padStart(2, '0')}`;
}

function advanceClock(nowMs = Date.now()) {
  if (sim.running && !sim.finished) {
    const dtMin = Math.max(0, (nowMs - sim.anchorRealMs) / 60000) * sim.speed;
    const nextMin = sim.anchorClockMin + dtMin;
    if (nextMin >= sim.endMin) {
      sim.anchorClockMin = sim.endMin;
      sim.finished = true;
      sim.running = false;
    } else {
      sim.anchorClockMin = nextMin;
    }
  }
  sim.anchorRealMs = nowMs;
  return sim.anchorClockMin;
}

function computeAnimatedBus(baseBus, clockS) {
  if (baseBus.state !== 'running') {
    return baseBus;
  }
  const legs = legsByRoute.get(baseBus.route);
  if (!legs || legs.length === 0) {
    return baseBus;
  }

  let cycleS = 0;
  for (const leg of legs) {
    cycleS += Math.max(1, leg.time_s) + DWELL_S;
  }
  if (cycleS <= 0) {
    return baseBus;
  }

  const startS = sim.startMin * 60;
  const phaseOffsetS = busInitialOffsetS.get(baseBus.id) ?? 0;
  const elapsedS = Math.max(0, clockS - startS) + phaseOffsetS;
  const cyclePosS = ((elapsedS % cycleS) + cycleS) % cycleS;
  const cycleStartAbsS = clockS - cyclePosS;

  let acc = 0;
  for (const leg of legs) {
    const moveS = Math.max(1, leg.time_s);
    const slotS = moveS + DWELL_S;
    if (cyclePosS < acc + slotS) {
      const slotStartAbsS = cycleStartAbsS + acc;
      const posInSlot = cyclePosS - acc;
      if (posInSlot < moveS) {
        return {
          ...baseBus,
          stop_idx: leg.from_stop,
          motion: {
            moving: true,
            leg_route: leg.route,
            leg_index: leg.leg,
            from_stop: leg.from_stop,
            to_stop: leg.to_stop,
            depart_s: slotStartAbsS,
            arrive_s: slotStartAbsS + moveS,
          },
        };
      }
      return {
        ...baseBus,
        stop_idx: leg.to_stop,
        motion: {
          moving: false,
          leg_route: leg.route,
          leg_index: -1,
          from_stop: leg.to_stop,
          to_stop: leg.to_stop,
          depart_s: slotStartAbsS + moveS,
          arrive_s: slotStartAbsS + slotS,
        },
      };
    }
    acc += slotS;
  }

  return baseBus;
}

function buildLiveState(nowMs = Date.now()) {
  const clockMin = advanceClock(nowMs);
  const clockS = clockMin * 60;
  return {
    ...mockBaseState,
    clock_min: clockMin,
    clock: formatClock(clockMin),
    running: sim.running,
    finished: sim.finished,
    speed: sim.speed,
    buses: mockBaseState.buses.map((b) => computeAnimatedBus(b, clockS)),
  };
}

function sendJson(res, status, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Cache-Control': 'no-store',
  });
  res.end(body);
}

function sendError(res, status, code, message) {
  sendJson(res, status, { error: { code, message } });
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > 65536) {
        reject(Object.assign(new Error('Payload too large'), { status: 413, code: 'payload_too_large' }));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => {
      const raw = Buffer.concat(chunks).toString('utf8').trim();
      if (!raw) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(raw));
      } catch {
        reject(Object.assign(new Error('Malformed JSON body'), { status: 400, code: 'invalid_argument' }));
      }
    });
    req.on('error', (err) => {
      reject(Object.assign(err, { status: 400, code: 'invalid_argument' }));
    });
  });
}

const server = http.createServer(async (req, res) => {
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type',
    });
    res.end();
    return;
  }

  const parsedUrl = new URL(req.url || '/', `http://127.0.0.1:${PORT}`);
  const pathname = parsedUrl.pathname;

  if (req.method === 'GET') {
    if (pathname === '/api/health') {
      advanceClock();
      sendJson(res, 200, { ok: true, sim_running: sim.running });
      return;
    }
    if (pathname === '/api/config') {
      sendJson(res, 200, mockConfig);
      return;
    }
    if (pathname === '/api/graph') {
      sendJson(res, 200, mockGraph);
      return;
    }
    if (pathname === '/api/stops') {
      sendJson(res, 200, mockStops);
      return;
    }
    if (pathname === '/api/routes') {
      sendJson(res, 200, mockRoutes);
      return;
    }
    if (pathname === '/api/geometry') {
      sendJson(res, 200, mockGeometry);
      return;
    }
    if (pathname === '/api/state') {
      sendJson(res, 200, buildLiveState());
      return;
    }
    if (pathname.startsWith('/api/bus/')) {
      const idStr = pathname.slice('/api/bus/'.length);
      if (!/^\d+$/.test(idStr)) {
        sendError(res, 400, 'invalid_argument', 'Invalid bus ID');
        return;
      }
      const id = Number(idStr);
      const live = buildLiveState();
      const bus = live.buses.find((b) => b.id === id);
      if (!bus) {
        sendError(res, 404, 'not_found', `Bus ${id} not found`);
        return;
      }
      sendJson(res, 200, {
        id: bus.id,
        state: bus.state,
        route: bus.route,
        stop_idx: bus.stop_idx,
        load: bus.load,
        capacity: 60,
        depot: 0,
        journeys: [],
      });
      return;
    }
    if (pathname === '/api/schedule') {
      sendJson(res, 200, mockSchedule);
      return;
    }
    if (pathname === '/api/tree') {
      sendJson(res, 200, mockTree);
      return;
    }
    if (pathname === '/api/demand/forecast') {
      const stopParam = parsedUrl.searchParams.get('stop');
      if (stopParam !== null && !/^\d+$/.test(stopParam)) {
        sendError(res, 400, 'invalid_argument', 'Invalid stop query parameter');
        return;
      }
      const stopIdx = stopParam === null ? 3 : Number(stopParam);
      const stopInfo = mockStops.stops.find((s) => s.idx === stopIdx);
      if (!stopInfo) {
        sendError(res, 404, 'not_found', `Stop ${stopIdx} not found`);
        return;
      }
      sendJson(res, 200, {
        ...mockForecastStop3,
        stop: stopInfo.idx,
        name: stopInfo.name,
        type: stopInfo.type,
      });
      return;
    }
    if (pathname === '/api/metrics') {
      sendJson(res, 200, mockMetrics);
      return;
    }
    if (pathname === '/api/alerts') {
      sendJson(res, 200, mockAlerts);
      return;
    }
    if (pathname === '/api/history') {
      sendJson(res, 200, mockHistory);
      return;
    }
    if (pathname === '/api/eval') {
      sendJson(res, 200, mockEval);
      return;
    }
    sendError(res, 404, 'not_found', `Unknown endpoint: ${pathname}`);
    return;
  }

  if (req.method === 'POST') {
    let body;
    try {
      body = await readJsonBody(req);
    } catch (err) {
      const status = typeof err?.status === 'number' ? err.status : 400;
      const code = typeof err?.code === 'string' ? err.code : 'invalid_argument';
      sendError(res, status, code, err instanceof Error ? err.message : 'Invalid request');
      return;
    }

    if (pathname === '/api/sim/control') {
      const action = body?.action;
      const hasSpeed = body?.speed !== undefined;

      if (hasSpeed) {
        if (typeof body.speed !== 'number' || !Number.isFinite(body.speed) || body.speed < 1 || body.speed > 3600) {
          sendError(res, 400, 'invalid_argument', 'speed must be a finite number in [1, 3600]');
          return;
        }
      }

      if (action === undefined && !hasSpeed) {
        sendError(res, 400, 'invalid_argument', 'action or speed is required');
        return;
      }

      const allowedActions = new Set(['start', 'pause', 'resume', 'speed', 'reset', 'seek']);
      if (action !== undefined && (typeof action !== 'string' || !allowedActions.has(action))) {
        sendError(res, 400, 'invalid_argument', `Unknown action: ${String(action)}`);
        return;
      }

      advanceClock();

      if (hasSpeed) {
        sim.speed = body.speed;
      }
      if (action === 'start' || action === 'resume') {
        if (!sim.finished) {
          sim.running = true;
        }
      } else if (action === 'pause') {
        sim.running = false;
      } else if (action === 'reset') {
        sim.anchorClockMin = mockBaseState.clock_min;
        sim.running = false;
        sim.finished = false;
      } else if (action === 'seek') {
        const tm = body?.time_min;
        if (typeof tm !== 'number' || !Number.isFinite(tm) || tm < sim.startMin || tm > sim.endMin) {
          sendError(res, 400, 'invalid_argument', 'time_min out of window');
          return;
        }
        sim.anchorClockMin = tm;
        sim.running = false;
        sim.finished = tm >= sim.endMin;
      }

      sendJson(res, 200, {
        ok: true,
        clock_min: sim.anchorClockMin,
        running: sim.running,
        speed: sim.speed,
      });
      return;
    }

    if (pathname === '/api/route/optimize') {
      sendJson(res, 200, mockOptimize);
      return;
    }

    sendError(res, 404, 'not_found', `Unknown endpoint: ${pathname}`);
    return;
  }

  sendError(res, 405, 'method_not_allowed', `Method ${req.method} not allowed`);
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Mock API server listening on http://127.0.0.1:${PORT} (mock dir: ${MOCK_DIR})`);
});

