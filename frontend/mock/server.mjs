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

function formatClock(minutes) {
  const total = Math.max(0, Math.floor(minutes));
  const hh = Math.floor(total / 60);
  const mm = total % 60;
  return `${String(hh).padStart(2, '0')}:${String(mm).padStart(2, '0')}`;
}

function createInitialRuntimeState() {
  return {
    anchorClockMin: mockBaseState.clock_min,
    anchorRealMs: Date.now(),
    running: true,
    finished: false,
    speed: mockBaseState.speed || 60,
    startMin: mockConfig.start_min ?? 360,
    endMin: mockConfig.end_min ?? 1320,
    sliceMin: mockConfig.slice_min ?? 15,
    rain: Boolean(mockBaseState.rain),
    policy: mockBaseState.policy || 'fixed_equal',
    predictor: mockConfig.predictor || 'ma',
    geometryVersion: mockGeometry.version ?? 1,
    busStateOverrides: new Map(
      mockBaseState.buses.map((b) => [
        b.id,
        { state: b.state, route: b.route, stop_idx: b.stop_idx },
      ]),
    ),
    stopEvents: new Set(
      mockStops.stops.filter((s) => s.event_active).map((s) => s.idx),
    ),
    extraQueues: new Map(),
    blockedLegs: new Map(),
    history: [...(mockHistory.entries || [])],
    flags: [...(mockAlerts.flags || [])],
    log: [...(mockAlerts.log || [])],
    lastOptimize: mockMetrics.last_optimize ?? null,
    movesApplied: 0,
    decisions: 0,
    decisionsApplied: 0,
  };
}

let sim = createInitialRuntimeState();

function appendLog(entry) {
  sim.log.push(`[${formatClock(sim.anchorClockMin)}:00] ${entry}`);
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
  const override = sim.busStateOverrides.get(baseBus.id);
  const curState = override?.state ?? baseBus.state;
  const curRoute = override?.route ?? baseBus.route;

  if (curState !== 'running') {
    return {
      ...baseBus,
      state: curState,
      route: curRoute,
      motion: {
        ...baseBus.motion,
        moving: false,
      },
    };
  }

  const legs = legsByRoute.get(curRoute);
  if (!legs || legs.length === 0) {
    return {
      ...baseBus,
      state: curState,
      route: curRoute,
    };
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
          state: curState,
          route: curRoute,
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
        state: curState,
        route: curRoute,
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
  const elapsedMin = Math.max(0, clockMin - sim.startMin);

  const buses = mockBaseState.buses.map((b) => computeAnimatedBus(b, clockS));
  const queues = mockBaseState.queues.map((q) => {
    const extra = sim.extraQueues.get(q.stop) ?? 0;
    const baseFlow = sim.rain ? Math.floor(elapsedMin * 0.15) : Math.floor(elapsedMin * 0.05);
    const stopBoost = sim.stopEvents.has(q.stop) ? 8 : 0;
    return {
      stop: q.stop,
      waiting: q.waiting + extra + ((q.stop % 4 === 0 ? baseFlow : 0) + stopBoost),
    };
  });

  const extraWaiting = Array.from(sim.extraQueues.values()).reduce((sum, n) => sum + n, 0);
  const dynamicBoarded = Math.floor(elapsedMin * 4);
  const totalWaiting = queues.reduce((sum, q) => sum + q.waiting, 0);
  const dynamicWaitS = elapsedMin > 0 ? Math.min(900, 180 + elapsedMin * 2.5 + (sim.rain ? 60 : 0)) : 0;

  return {
    ...mockBaseState,
    clock_min: clockMin,
    clock: formatClock(clockMin),
    running: sim.running,
    finished: sim.finished,
    speed: sim.speed,
    rain: sim.rain,
    policy: sim.policy,
    geometry_version: sim.geometryVersion,
    history_size: sim.history.length,
    buses,
    queues,
    summary: {
      arrived: mockBaseState.summary.arrived + dynamicBoarded + totalWaiting + extraWaiting,
      boarded: mockBaseState.summary.boarded + dynamicBoarded,
      still_waiting: totalWaiting,
      mean_wait_boarded_s: dynamicWaitS,
      mean_wait_incl_censored_s: dynamicWaitS > 0 ? dynamicWaitS + 35 : 0,
      p95_s: dynamicWaitS > 0 ? dynamicWaitS * 2.1 : -1,
    },
  };
}

function routeBusCounts(liveBuses) {
  const counts = new Array(mockRoutes.routes.length).fill(0);
  for (const b of liveBuses) {
    if (b.state !== 'reserve' && b.route >= 0 && b.route < counts.length) {
      counts[b.route] += 1;
    }
  }
  return counts;
}

function buildRoutesPayload() {
  const live = buildLiveState();
  const counts = routeBusCounts(live.buses);
  const routes = mockRoutes.routes.map((r) => ({
    ...r,
    buses: counts[r.id] ?? r.buses,
  }));
  const board = [...routes]
    .sort((a, b) => b.priority - a.priority)
    .map((r) => ({
      route_id: r.id,
      priority: r.priority,
      demand: r.demand,
      buses: r.buses,
    }));
  return {
    hour: Math.floor(live.clock_min / 60) % 24,
    basis: sim.predictor === 'oracle' ? 'oracle' : 'forecast',
    routes,
    board,
  };
}

function buildGeometryPayload() {
  const blocked = [...(mockGeometry.blocked || [])];
  for (const seg of sim.blockedLegs.values()) {
    blocked.push(seg);
  }
  return {
    ...mockGeometry,
    version: sim.geometryVersion,
    blocked,
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
        reject(
          Object.assign(new Error('Payload too large'), {
            status: 413,
            code: 'payload_too_large',
          }),
        );
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
        reject(
          Object.assign(new Error('Malformed JSON body'), {
            status: 400,
            code: 'invalid_argument',
          }),
        );
      }
    });
    req.on('error', (err) => {
      reject(Object.assign(err, { status: 400, code: 'invalid_argument' }));
    });
  });
}

function nextSliceMin(clockMin, sliceMin) {
  return Math.ceil(clockMin / sliceMin) * sliceMin;
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
      sendJson(res, 200, {
        ...mockConfig,
        policy: sim.policy,
        predictor: sim.predictor,
      });
      return;
    }
    if (pathname === '/api/graph') {
      sendJson(res, 200, mockGraph);
      return;
    }
    if (pathname === '/api/stops') {
      sendJson(res, 200, {
        stops: mockStops.stops.map((s) => ({
          ...s,
          event_active: sim.stopEvents.has(s.idx),
        })),
      });
      return;
    }
    if (pathname === '/api/routes') {
      sendJson(res, 200, buildRoutesPayload());
      return;
    }
    if (pathname === '/api/geometry') {
      sendJson(res, 200, buildGeometryPayload());
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
      const busTrips = mockSchedule.trips
        .filter((t) => t.bus_id === bus.id)
        .slice(0, 5)
        .map((t, idx) => ({
          trip_id: t.trip_id,
          route_id: t.route_id,
          start_min: t.departure_min,
          end_min: Math.min(sim.endMin, t.departure_min + 45),
          passengers: 18 + ((bus.id + idx * 7) % 24),
        }));
      sendJson(res, 200, {
        id: bus.id,
        state: bus.state,
        route: bus.route,
        stop_idx: bus.stop_idx,
        load: bus.load,
        capacity: 60,
        depot: 0,
        journeys: busTrips,
      });
      return;
    }
    if (pathname === '/api/schedule') {
      const live = buildLiveState();
      const fromParam = parsedUrl.searchParams.get('from');
      const toParam = parsedUrl.searchParams.get('to');
      const routeParam = parsedUrl.searchParams.get('route');

      const fromMin = fromParam !== null && /^\d+$/.test(fromParam) ? Number(fromParam) : Math.floor(live.clock_min);
      const toMin = toParam !== null && /^\d+$/.test(toParam) ? Number(toParam) : Math.min(fromMin + 120, 1440);
      const routeFilter = routeParam !== null && /^-?\d+$/.test(routeParam) ? Number(routeParam) : -1;

      if (fromMin < 0 || fromMin >= 1440 || toMin <= fromMin || toMin > 1440 || routeFilter < -1 || routeFilter >= mockRoutes.routes.length) {
        sendError(res, 400, 'invalid_argument', 'Invalid schedule query parameters');
        return;
      }

      const filtered = mockSchedule.trips.filter(
        (t) =>
          t.departure_min >= fromMin &&
          t.departure_min <= toMin &&
          (routeFilter === -1 || t.route_id === routeFilter),
      );
      sendJson(res, 200, {
        from: fromMin,
        to: toMin,
        count: filtered.length,
        avl: mockSchedule.avl,
        trips: filtered.slice(0, 500),
      });
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
        rain: sim.rain,
        event_active: sim.stopEvents.has(stopInfo.idx),
      });
      return;
    }
    if (pathname === '/api/metrics') {
      const live = buildLiveState();
      sendJson(res, 200, {
        ...mockMetrics,
        sim: {
          ...mockMetrics.sim,
          arrived: live.summary.arrived,
          boarded: live.summary.boarded,
          still_waiting: live.summary.still_waiting,
          mean_wait_boarded_s: live.summary.mean_wait_boarded_s,
          mean_wait_incl_censored_s: live.summary.mean_wait_incl_censored_s,
          p95_s: live.summary.p95_s,
          moves_applied: sim.movesApplied,
          decisions: sim.decisions,
          decisions_applied: sim.decisionsApplied,
          anomaly_flags: sim.flags.length,
        },
        last_optimize: sim.lastOptimize,
      });
      return;
    }
    if (pathname === '/api/alerts') {
      sendJson(res, 200, {
        flags: sim.flags,
        log: sim.log,
      });
      return;
    }
    if (pathname === '/api/history') {
      sendJson(res, 200, {
        size: sim.history.length,
        entries: sim.history,
      });
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
        if (
          typeof body.speed !== 'number' ||
          !Number.isFinite(body.speed) ||
          body.speed < 1 ||
          body.speed > 3600
        ) {
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
        const nextGeo = sim.geometryVersion + 1;
        sim = createInitialRuntimeState();
        sim.running = false;
        sim.geometryVersion = nextGeo;
      } else if (action === 'seek') {
        const tm = body?.time_min;
        if (
          typeof tm !== 'number' ||
          !Number.isFinite(tm) ||
          tm < sim.startMin ||
          tm > sim.endMin ||
          tm % sim.sliceMin !== 0
        ) {
          sendError(res, 400, 'invalid_argument', 'time_min must be a multiple of slice_min within [start_min, end_min]');
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

    if (pathname === '/api/scenario') {
      const kind = body?.kind;
      const sliceT = nextSliceMin(advanceClock(), sim.sliceMin);
      if (sliceT >= sim.endMin) {
        sendError(res, 409, 'conflict', 'Next slice is past simulation window');
        return;
      }

      if (kind === 'rain_on' || kind === 'rain_off') {
        sim.rain = kind === 'rain_on';
        appendLog(`scenario: rain ${sim.rain ? 'ON' : 'OFF'}`);
        sendJson(res, 200, { scheduled: true, time_min: sliceT, kind });
        return;
      }

      if (kind === 'event_on' || kind === 'event_off') {
        const stop = body?.stop;
        if (typeof stop !== 'number' || !Number.isInteger(stop) || stop < 0 || stop >= mockStops.stops.length) {
          sendError(res, 400, 'invalid_argument', 'Invalid stop index');
          return;
        }
        const stopInfo = mockStops.stops[stop];
        if (kind === 'event_on') {
          sim.stopEvents.add(stop);
          sim.flags.push({
            stop,
            name: stopInfo?.name ?? `Stop ${stop}`,
            time_min: Math.floor(sim.anchorClockMin),
            observed: 28,
            expected: 3.5,
            z: 6.4,
          });
        } else {
          sim.stopEvents.delete(stop);
        }
        appendLog(`scenario: event ${kind === 'event_on' ? 'ON' : 'OFF'} at stop ${stop}`);
        sendJson(res, 200, { scheduled: true, time_min: sliceT, kind });
        return;
      }

      if (kind === 'breakdown' || kind === 'repair') {
        const bus = body?.bus;
        if (typeof bus !== 'number' || !Number.isInteger(bus) || !sim.busStateOverrides.has(bus)) {
          sendError(res, 400, 'invalid_argument', 'Invalid bus ID');
          return;
        }
        const prev = sim.busStateOverrides.get(bus);
        sim.busStateOverrides.set(bus, {
          ...prev,
          state: kind === 'breakdown' ? 'broken' : 'running',
        });
        appendLog(`scenario: bus ${bus} ${kind}`);
        sendJson(res, 200, { scheduled: true, time_min: sliceT, kind });
        return;
      }

      if (kind === 'block_edge' || kind === 'unblock_edge') {
        const hasEdge = typeof body?.edge === 'number' && Number.isInteger(body.edge);
        const hasRouteLeg =
          typeof body?.route === 'number' &&
          Number.isInteger(body.route) &&
          typeof body?.leg === 'number' &&
          Number.isInteger(body.leg);

        if (!hasEdge && !hasRouteLeg) {
          sendError(res, 400, 'invalid_argument', 'Provide edge or route+leg');
          return;
        }

        const key = hasEdge ? `edge:${body.edge}` : `leg:${body.route}:${body.leg}`;
        const label = hasEdge ? `block edge ${body.edge}` : `block route ${body.route} leg ${body.leg}`;

        if (kind === 'block_edge') {
          let seg;
          if (hasRouteLeg) {
            const legObj = mockGeometry.legs.find(
              (l) => l.route === body.route && l.leg === body.leg,
            );
            if (!legObj || legObj.path.length < 2) {
              sendError(res, 409, 'conflict', 'Target leg has no path');
              return;
            }
            const mid = Math.floor(legObj.path.length / 2);
            seg = [legObj.path[Math.max(0, mid - 1)], legObj.path[mid]];
          } else {
            const firstLeg = mockGeometry.legs[0];
            seg = [firstLeg.path[0], firstLeg.path[1]];
          }
          sim.blockedLegs.set(key, seg);
          sim.history.unshift(label);
        } else {
          sim.blockedLegs.delete(key);
          sim.history.unshift(hasEdge ? `unblock edge ${body.edge}` : `unblock route ${body.route} leg ${body.leg}`);
        }
        sim.geometryVersion += 1;
        appendLog(`scenario: ${label} (${kind})`);
        sendJson(res, 200, { scheduled: true, time_min: sliceT, kind });
        return;
      }

      sendError(res, 400, 'invalid_argument', `Unknown scenario kind: ${String(kind)}`);
      return;
    }

    if (pathname === '/api/allocate') {
      if (body?.manual_move) {
        const busId = body.manual_move.bus;
        const toRoute = body.manual_move.to_route;
        if (
          typeof busId !== 'number' ||
          !Number.isInteger(busId) ||
          !sim.busStateOverrides.has(busId) ||
          typeof toRoute !== 'number' ||
          !Number.isInteger(toRoute) ||
          toRoute < 0 ||
          toRoute >= mockRoutes.routes.length
        ) {
          sendError(res, 400, 'invalid_argument', 'Invalid bus or target route');
          return;
        }
        const prev = sim.busStateOverrides.get(busId);
        if (prev.state !== 'running') {
          sendError(res, 400, 'invalid_argument', 'Bus must be in running state for manual move');
          return;
        }
        sim.busStateOverrides.set(busId, { ...prev, route: toRoute });
        sim.movesApplied += 1;
        appendLog(`manual move: bus ${busId} -> route ${toRoute}`);
        sendJson(res, 200, { moved: { bus: busId, to_route: toRoute } });
        return;
      }

      const policyInput = body?.policy;
      const policyMap = {
        equal: 'fixed_equal',
        proportional: 'fixed_proportional',
        greedy_spec: 'greedy_spec',
        greedy_marginal: 'greedy_marginal',
      };
      if (typeof policyInput !== 'string' || !(policyInput in policyMap)) {
        sendError(res, 400, 'invalid_argument', `Unknown policy: ${String(policyInput)}`);
        return;
      }
      const predictor = body?.predictor;
      if (predictor !== undefined) {
        if (!['oracle', 'ma', 'rf'].includes(predictor)) {
          sendError(res, 400, 'invalid_argument', `Unknown predictor: ${String(predictor)}`);
          return;
        }
        if ((predictor === 'ma' && !mockConfig.models.ma) || (predictor === 'rf' && !mockConfig.models.rf)) {
          sendError(res, 400, 'invalid_argument', `Predictor ${predictor} is not loaded`);
          return;
        }
        sim.predictor = predictor;
      }

      if (body?.force && (policyInput === 'equal' || policyInput === 'proportional')) {
        sendError(res, 400, 'invalid_argument', 'Cannot force fixed allocation policy');
        return;
      }

      sim.policy = policyMap[policyInput];
      sim.decisions += 1;
      sim.decisionsApplied += 1;

      const targetCounts =
        policyInput === 'equal'
          ? [6, 6, 6, 5, 5, 5]
          : [7, 7, 6, 5, 4, 4];

      let busPtr = 0;
      for (let r = 0; r < targetCounts.length; r += 1) {
        for (let k = 0; k < targetCounts[r]; k += 1) {
          const entry = sim.busStateOverrides.get(busPtr);
          if (entry && entry.state === 'running') {
            sim.busStateOverrides.set(busPtr, { ...entry, route: r });
          }
          busPtr += 1;
        }
      }
      sim.movesApplied += 1;
      appendLog(`allocate: policy=${sim.policy}`);
      sendJson(res, 200, {
        policy: sim.policy,
        decision: { forced: Boolean(body?.force), moves_applied: 1 },
        counts: targetCounts,
      });
      return;
    }

    if (pathname === '/api/inject') {
      const stop = body?.stop;
      const count = body?.count;
      if (
        typeof stop !== 'number' ||
        !Number.isInteger(stop) ||
        stop < 0 ||
        stop >= mockStops.stops.length ||
        typeof count !== 'number' ||
        !Number.isInteger(count) ||
        count < 1 ||
        count > 500
      ) {
        sendError(res, 400, 'invalid_argument', 'Valid stop index and count (1..500) required');
        return;
      }
      const prev = sim.extraQueues.get(stop) ?? 0;
      sim.extraQueues.set(stop, prev + count);
      const timeS = Math.floor(advanceClock()) * 60;
      const stopInfo = mockStops.stops[stop];
      sim.flags.push({
        stop,
        name: stopInfo?.name ?? `Stop ${stop}`,
        time_min: Math.floor(sim.anchorClockMin),
        observed: count,
        expected: 3.0,
        z: Number((count / 2.5).toFixed(2)),
      });
      appendLog(`inject: +${count} passengers at stop ${stop}`);
      sendJson(res, 200, { stop, count, time_s: timeS });
      return;
    }

    if (pathname === '/api/route/optimize') {
      const fromStop = body?.from_stop;
      const toStop = body?.to_stop;
      const algo = body?.algo ?? 'both';
      const mode = body?.mode ?? 'traffic';

      if (
        typeof fromStop !== 'number' ||
        !Number.isInteger(fromStop) ||
        fromStop < 0 ||
        fromStop >= mockStops.stops.length ||
        typeof toStop !== 'number' ||
        !Number.isInteger(toStop) ||
        toStop < 0 ||
        toStop >= mockStops.stops.length ||
        !['astar', 'dijkstra', 'both'].includes(algo) ||
        !['traffic', 'static'].includes(mode)
      ) {
        sendError(res, 400, 'invalid_argument', 'Invalid optimize parameters');
        return;
      }

      let result;
      if (fromStop === 0 && toStop === 5) {
        result = {
          ...mockOptimize,
          mode,
        };
      } else {
        const sFrom = mockStops.stops[fromStop];
        const sTo = mockStops.stops[toStop];
        const midLat = (sFrom.lat + sTo.lat) / 2 + 0.004;
        const midLng = (sFrom.lng + sTo.lng) / 2 - 0.004;
        const basePath = [
          [sFrom.lat, sFrom.lng],
          [midLat, midLng],
          [sTo.lat, sTo.lng],
        ];
        const dLat = (sTo.lat - sFrom.lat) * 111000;
        const dLng = (sTo.lng - sFrom.lng) * 108000;
        const lengthM = Math.max(400, Math.round(Math.hypot(dLat, dLng) * 1.25));
        const staticS = Number((lengthM / 7.8).toFixed(2));
        const trafficFactor = sim.rain || sim.blockedLegs.size > 0 ? 1.28 : 1.08;
        const trafficS = Number((staticS * trafficFactor).toFixed(2));
        const staticPenaltyS = Number((trafficS * 1.12).toFixed(2));
        const astarNodes = 240 + ((fromStop * 17 + toStop * 31) % 300);
        const dijkstraNodes = astarNodes * 3 + 45;

        const astarRes = {
          found: true,
          time_s: mode === 'traffic' ? trafficS : staticS,
          length_m: lengthM,
          nodes_explored: astarNodes,
          time_static_s: staticS,
          time_traffic_s: mode === 'traffic' ? trafficS : staticPenaltyS,
          path: basePath,
        };
        const dijkstraRes = {
          found: true,
          time_s: mode === 'traffic' ? trafficS : staticS,
          length_m: lengthM,
          nodes_explored: dijkstraNodes,
          time_static_s: staticS,
          time_traffic_s: mode === 'traffic' ? trafficS : staticPenaltyS,
          path: basePath,
        };

        result = {
          mode,
          ...(algo === 'astar' || algo === 'both' ? { astar: astarRes } : {}),
          ...(algo === 'dijkstra' || algo === 'both' ? { dijkstra: dijkstraRes } : {}),
          ...(algo === 'both'
            ? { nodes_ratio: Number((astarNodes / dijkstraNodes).toFixed(4)) }
            : {}),
        };
      }

      sim.lastOptimize = result;
      sendJson(res, 200, result);
      return;
    }

    if (pathname === '/api/undo') {
      if (sim.history.length === 0) {
        sendError(res, 409, 'conflict', 'Undo stack is empty');
        return;
      }
      const undone = sim.history.shift();
      const firstKey = sim.blockedLegs.keys().next().value;
      if (firstKey !== undefined) {
        sim.blockedLegs.delete(firstKey);
      }
      sim.geometryVersion += 1;
      appendLog(`undo: ${undone}`);
      sendJson(res, 200, {
        undone,
        history_size: sim.history.length,
        geometry_version: sim.geometryVersion,
      });
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
