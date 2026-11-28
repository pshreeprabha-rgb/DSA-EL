export type ValidationResult =
  | { ok: true }
  | { ok: false; field: string; reason: string };

type JsonRecord = Record<string, unknown>;

const ok = (): ValidationResult => ({ ok: true });
const fail = (field: string, reason: string): ValidationResult => ({
  ok: false,
  field,
  reason,
});

function isRecord(v: unknown): v is JsonRecord {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function isFiniteNum(v: unknown): v is number {
  return typeof v === 'number' && Number.isFinite(v);
}

function isBool(v: unknown): v is boolean {
  return typeof v === 'boolean';
}

function isStr(v: unknown): v is string {
  return typeof v === 'string';
}

function checkNums(
  obj: JsonRecord,
  prefix: string,
  fields: readonly string[],
): ValidationResult | null {
  for (const key of fields) {
    if (!isFiniteNum(obj[key])) {
      return fail(prefix ? `${prefix}.${key}` : key, 'expected finite number');
    }
  }
  return null;
}

function checkBools(
  obj: JsonRecord,
  prefix: string,
  fields: readonly string[],
): ValidationResult | null {
  for (const key of fields) {
    if (!isBool(obj[key])) {
      return fail(prefix ? `${prefix}.${key}` : key, 'expected boolean');
    }
  }
  return null;
}

function checkStrs(
  obj: JsonRecord,
  prefix: string,
  fields: readonly string[],
): ValidationResult | null {
  for (const key of fields) {
    if (!isStr(obj[key])) {
      return fail(prefix ? `${prefix}.${key}` : key, 'expected string');
    }
  }
  return null;
}

function isLatLngTuple(v: unknown): boolean {
  return (
    Array.isArray(v) &&
    v.length === 2 &&
    isFiniteNum(v[0]) &&
    isFiniteNum(v[1])
  );
}

export function validateHealthResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  return checkBools(data, '', ['ok', 'sim_running']) ?? ok();
}

export function validateConfigResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  const nums = checkNums(data, '', [
    'stops',
    'routes',
    'buses',
    'reserve',
    'seed',
    'start_min',
    'end_min',
    'slice_min',
  ]);
  if (nums) return nums;
  const strs = checkStrs(data, '', ['policy', 'predictor', 'spacing', 'reassign']);
  if (strs) return strs;
  if (!isBool(data.offline)) return fail('offline', 'expected boolean');
  if (!isRecord(data.models)) return fail('models', 'expected object');
  return checkBools(data.models, 'models', ['ma', 'rf']) ?? ok();
}

export function validateGraphResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!Array.isArray(data.nodes)) return fail('nodes', 'expected array');
  for (let i = 0; i < data.nodes.length; i += 1) {
    const n = data.nodes[i];
    if (
      !Array.isArray(n) ||
      n.length !== 3 ||
      !isFiniteNum(n[0]) ||
      !isFiniteNum(n[1]) ||
      !isFiniteNum(n[2])
    ) {
      return fail(`nodes[${i}]`, 'expected [lat, lng, is_stop]');
    }
  }
  if (!Array.isArray(data.edges)) return fail('edges', 'expected array');
  for (let i = 0; i < data.edges.length; i += 1) {
    const e = data.edges[i];
    if (
      !Array.isArray(e) ||
      e.length !== 3 ||
      !isFiniteNum(e[0]) ||
      !isFiniteNum(e[1]) ||
      !isFiniteNum(e[2])
    ) {
      return fail(`edges[${i}]`, 'expected [from, to, road_class]');
    }
  }
  return ok();
}

export function validateStopsResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!Array.isArray(data.stops)) return fail('stops', 'expected array');
  for (let i = 0; i < data.stops.length; i += 1) {
    const s = data.stops[i];
    if (!isRecord(s)) return fail(`stops[${i}]`, 'expected object');
    const err =
      checkNums(s, `stops[${i}]`, [
        'idx',
        'node',
        'scale',
        'lat',
        'lng',
        'route',
        'pos',
        'first_flag_min',
      ]) ??
      checkStrs(s, `stops[${i}]`, ['name', 'type']) ??
      checkBools(s, `stops[${i}]`, ['event_active']);
    if (err) return err;
  }
  return ok();
}

export function validateRoutesResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!isFiniteNum(data.hour)) return fail('hour', 'expected number');
  if (!isStr(data.basis)) return fail('basis', 'expected string');
  if (!Array.isArray(data.routes)) return fail('routes', 'expected array');
  for (let i = 0; i < data.routes.length; i += 1) {
    const r = data.routes[i];
    if (!isRecord(r)) return fail(`routes[${i}]`, 'expected object');
    const err =
      checkNums(r, `routes[${i}]`, [
        'id',
        'loop_min',
        'cycle_min',
        'buses',
        'demand',
        'priority',
      ]) ?? checkStrs(r, `routes[${i}]`, ['name']);
    if (err) return err;
    if (!Array.isArray(r.stops) || !r.stops.every(isFiniteNum)) {
      return fail(`routes[${i}].stops`, 'expected number array');
    }
  }
  if (!Array.isArray(data.board)) return fail('board', 'expected array');
  for (let i = 0; i < data.board.length; i += 1) {
    const b = data.board[i];
    if (!isRecord(b)) return fail(`board[${i}]`, 'expected object');
    const err = checkNums(b, `board[${i}]`, [
      'route_id',
      'priority',
      'demand',
      'buses',
    ]);
    if (err) return err;
  }
  return ok();
}

export function validateGeometryResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!isFiniteNum(data.version)) return fail('version', 'expected number');
  if (!Array.isArray(data.legs)) return fail('legs', 'expected array');
  for (let i = 0; i < data.legs.length; i += 1) {
    const leg = data.legs[i];
    if (!isRecord(leg)) return fail(`legs[${i}]`, 'expected object');
    const err = checkNums(leg, `legs[${i}]`, [
      'route',
      'leg',
      'from_stop',
      'to_stop',
      'time_s',
    ]);
    if (err) return err;
    if (!Array.isArray(leg.path) || !leg.path.every(isLatLngTuple)) {
      return fail(`legs[${i}].path`, 'expected [lat, lng][]');
    }
  }
  if (!Array.isArray(data.blocked)) return fail('blocked', 'expected array');
  for (let i = 0; i < data.blocked.length; i += 1) {
    const seg = data.blocked[i];
    if (
      !Array.isArray(seg) ||
      seg.length !== 2 ||
      !isLatLngTuple(seg[0]) ||
      !isLatLngTuple(seg[1])
    ) {
      return fail(`blocked[${i}]`, 'expected [[lat, lng], [lat, lng]]');
    }
  }
  return ok();
}

export function validateStateResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  const err =
    checkNums(data, '', [
      'clock_min',
      'speed',
      'geometry_version',
      'history_size',
    ]) ??
    checkStrs(data, '', ['clock', 'day_type', 'policy', 'traffic_source']) ??
    checkBools(data, '', ['running', 'finished', 'rain']);
  if (err) return err;

  if (!Array.isArray(data.buses)) return fail('buses', 'expected array');
  for (let i = 0; i < data.buses.length; i += 1) {
    const b = data.buses[i];
    if (!isRecord(b)) return fail(`buses[${i}]`, 'expected object');
    const bErr =
      checkNums(b, `buses[${i}]`, [
        'id',
        'route',
        'stop_idx',
        'load',
        'pending_route',
      ]) ?? checkStrs(b, `buses[${i}]`, ['state']);
    if (bErr) return bErr;
    if (!isRecord(b.motion)) return fail(`buses[${i}].motion`, 'expected object');
    const mErr =
      checkBools(b.motion, `buses[${i}].motion`, ['moving']) ??
      checkNums(b.motion, `buses[${i}].motion`, [
        'leg_route',
        'leg_index',
        'from_stop',
        'to_stop',
        'depart_s',
        'arrive_s',
      ]);
    if (mErr) return mErr;
  }

  if (!Array.isArray(data.queues)) return fail('queues', 'expected array');
  for (let i = 0; i < data.queues.length; i += 1) {
    const q = data.queues[i];
    if (!isRecord(q)) return fail(`queues[${i}]`, 'expected object');
    const qErr = checkNums(q, `queues[${i}]`, ['stop', 'waiting']);
    if (qErr) return qErr;
  }

  if (!isRecord(data.summary)) return fail('summary', 'expected object');
  return (
    checkNums(data.summary, 'summary', [
      'arrived',
      'boarded',
      'still_waiting',
      'mean_wait_boarded_s',
      'mean_wait_incl_censored_s',
      'p95_s',
    ]) ?? ok()
  );
}

export function validateBusDetailResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  const err =
    checkNums(data, '', ['id', 'route', 'stop_idx', 'load', 'capacity', 'depot']) ??
    checkStrs(data, '', ['state']);
  if (err) return err;
  if (!Array.isArray(data.journeys)) return fail('journeys', 'expected array');
  for (let i = 0; i < data.journeys.length; i += 1) {
    const j = data.journeys[i];
    if (!isRecord(j)) return fail(`journeys[${i}]`, 'expected object');
    const jErr = checkNums(j, `journeys[${i}]`, [
      'trip_id',
      'route_id',
      'start_min',
      'end_min',
      'passengers',
    ]);
    if (jErr) return jErr;
  }
  return ok();
}

export function validateScheduleResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  const err = checkNums(data, '', ['from', 'to', 'count']);
  if (err) return err;
  if (!isRecord(data.avl)) return fail('avl', 'expected object');
  const avlErr =
    checkNums(data.avl, 'avl', ['size', 'height']) ??
    checkBools(data.avl, 'avl', ['balanced']);
  if (avlErr) return avlErr;
  if (!Array.isArray(data.trips)) return fail('trips', 'expected array');
  for (let i = 0; i < data.trips.length; i += 1) {
    const t = data.trips[i];
    if (!isRecord(t)) return fail(`trips[${i}]`, 'expected object');
    const tErr =
      checkNums(t, `trips[${i}]`, [
        'trip_id',
        'route_id',
        'bus_id',
        'departure_min',
      ]) ?? checkStrs(t, `trips[${i}]`, ['departure']);
    if (tErr) return tErr;
  }
  return ok();
}

export function validateTreeResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  const err = checkNums(data, '', ['count', 'height']);
  if (err) return err;
  if (!Array.isArray(data.nodes)) return fail('nodes', 'expected array');
  for (let i = 0; i < data.nodes.length; i += 1) {
    const n = data.nodes[i];
    if (!isRecord(n)) return fail(`nodes[${i}]`, 'expected object');
    const nErr =
      checkNums(n, `nodes[${i}]`, ['row', 'parent', 'depth', 'kind', 'id']) ??
      checkStrs(n, `nodes[${i}]`, ['name']);
    if (nErr) return nErr;
  }
  return ok();
}

export function validateDemandForecastResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!isFiniteNum(data.stop)) return fail('stop', 'expected number');
  const err =
    checkStrs(data, '', ['name', 'type', 'day_type']) ??
    checkBools(data, '', ['rain', 'event_active']);
  if (err) return err;
  if (!Array.isArray(data.hours)) return fail('hours', 'expected array');
  for (let i = 0; i < data.hours.length; i += 1) {
    const h = data.hours[i];
    if (!isRecord(h)) return fail(`hours[${i}]`, 'expected object');
    const hErr = checkNums(h, `hours[${i}]`, ['hour', 'oracle', 'observed']);
    if (hErr) return hErr;
    if (h.ma !== null && !isFiniteNum(h.ma)) {
      return fail(`hours[${i}].ma`, 'expected number or null');
    }
    if (h.rf !== null && !isFiniteNum(h.rf)) {
      return fail(`hours[${i}].rf`, 'expected number or null');
    }
  }
  return ok();
}

function validateOptimizeAlgo(v: unknown, prefix: string): ValidationResult {
  if (!isRecord(v)) return fail(prefix, 'expected object');
  const err =
    checkBools(v, prefix, ['found']) ??
    checkNums(v, prefix, [
      'time_s',
      'length_m',
      'nodes_explored',
      'time_static_s',
      'time_traffic_s',
    ]);
  if (err) return err;
  if (!Array.isArray(v.path) || !v.path.every(isLatLngTuple)) {
    return fail(`${prefix}.path`, 'expected [lat, lng][]');
  }
  return ok();
}

export function validateOptimizeResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!isStr(data.mode)) return fail('mode', 'expected string');
  if (data.astar !== undefined) {
    const err = validateOptimizeAlgo(data.astar, 'astar');
    if (!err.ok) return err;
  }
  if (data.dijkstra !== undefined) {
    const err = validateOptimizeAlgo(data.dijkstra, 'dijkstra');
    if (!err.ok) return err;
  }
  if (data.astar !== undefined && data.dijkstra !== undefined && !isFiniteNum(data.nodes_ratio)) {
    return fail('nodes_ratio', 'expected number when both algos returned');
  }
  return ok();
}

export function validateMetricsResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!isRecord(data.sim)) return fail('sim', 'expected object');
  const simErr = checkNums(data.sim, 'sim', [
    'arrived',
    'boarded',
    'still_waiting',
    'wait_sum_s',
    'wait_max_s',
    'mean_wait_boarded_s',
    'mean_wait_incl_censored_s',
    'p50_s',
    'p95_s',
    'full_bus_events',
    'forced_dumps',
    'stranded_by_breakdown',
    'bus_arrivals',
    'moves_applied',
    'deadhead_s_total',
    'anomaly_flags',
    'emergency_dispatches',
    'stalled_s',
    'mean_load_factor',
    'decisions',
    'decisions_applied',
  ]);
  if (simErr) return simErr;
  if (!Array.isArray(data.routes)) return fail('routes', 'expected array');
  for (let i = 0; i < data.routes.length; i += 1) {
    const r = data.routes[i];
    if (!isRecord(r)) return fail(`routes[${i}]`, 'expected object');
    const rErr = checkNums(r, `routes[${i}]`, [
      'route',
      'boarded',
      'still_waiting',
      'mean_wait_s',
      'max_wait_s',
    ]);
    if (rErr) return rErr;
  }
  if (data.ml !== null && !isRecord(data.ml)) {
    return fail('ml', 'expected object or null');
  }
  if (data.last_optimize !== null) {
    const optErr = validateOptimizeResponse(data.last_optimize);
    if (!optErr.ok) {
      return fail(`last_optimize.${optErr.field}`, optErr.reason);
    }
  }
  if (!isRecord(data.traffic)) return fail('traffic', 'expected object');
  return (
    checkStrs(data.traffic, 'traffic', ['source', 'last_error']) ??
    checkNums(data.traffic, 'traffic', ['calls', 'live_corridors']) ??
    ok()
  );
}

export function validateAlertsResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!Array.isArray(data.flags)) return fail('flags', 'expected array');
  for (let i = 0; i < data.flags.length; i += 1) {
    const f = data.flags[i];
    if (!isRecord(f)) return fail(`flags[${i}]`, 'expected object');
    const err =
      checkNums(f, `flags[${i}]`, ['stop', 'time_min', 'observed', 'expected', 'z']) ??
      checkStrs(f, `flags[${i}]`, ['name']);
    if (err) return err;
  }
  if (!Array.isArray(data.log) || !data.log.every(isStr)) {
    return fail('log', 'expected string array');
  }
  return ok();
}

export function validateHistoryResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!isFiniteNum(data.size)) return fail('size', 'expected number');
  if (!Array.isArray(data.entries) || !data.entries.every(isStr)) {
    return fail('entries', 'expected string array');
  }
  return ok();
}

export function validateEvalResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if (!isRecord(data.config)) return fail('config', 'expected object');
  const cfgErr = checkNums(data.config, 'config', [
    'seeds',
    'base_seed',
    'stops',
    'routes',
    'buses',
  ]);
  if (cfgErr) return cfgErr;
  if (!Array.isArray(data.scenarios)) return fail('scenarios', 'expected array');
  for (let i = 0; i < data.scenarios.length; i += 1) {
    const sc = data.scenarios[i];
    if (!isRecord(sc)) return fail(`scenarios[${i}]`, 'expected object');
    const scErr = checkStrs(sc, `scenarios[${i}]`, ['name', 'metric', 'baseline']);
    if (scErr) return scErr;
    if (!Array.isArray(sc.policies)) return fail(`scenarios[${i}].policies`, 'expected array');
    for (let j = 0; j < sc.policies.length; j += 1) {
      const p = sc.policies[j];
      if (!isRecord(p)) return fail(`scenarios[${i}].policies[${j}]`, 'expected object');
      const pErr =
        checkStrs(p, `scenarios[${i}].policies[${j}]`, ['name']) ??
        checkNums(p, `scenarios[${i}].policies[${j}]`, [
          'n',
          'mean',
          'sd',
          'diff_mean',
          'ci_lo',
          'ci_hi',
          'wins',
          'ties',
          'losses',
        ]) ??
        checkBools(p, `scenarios[${i}].policies[${j}]`, ['significant']);
      if (pErr) return pErr;
    }
  }
  return ok();
}

export function validateSimControlResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  const err =
    checkNums(data, '', ['clock_min', 'speed']) ?? checkBools(data, '', ['running']);
  if (err) return err;
  if (data.ok !== undefined && !isBool(data.ok)) {
    return fail('ok', 'expected boolean');
  }
  return ok();
}

export function validateScenarioResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  return (
    checkBools(data, '', ['scheduled']) ??
    checkNums(data, '', ['time_min']) ??
    checkStrs(data, '', ['kind']) ??
    ok()
  );
}

export function validateAllocateResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  if ('moved' in data) {
    if (!isRecord(data.moved)) return fail('moved', 'expected object');
    return checkNums(data.moved, 'moved', ['bus', 'to_route']) ?? ok();
  }
  if (!isStr(data.policy)) return fail('policy', 'expected string');
  if (!isRecord(data.decision)) return fail('decision', 'expected object');
  const decErr =
    checkBools(data.decision, 'decision', ['forced']) ??
    checkNums(data.decision, 'decision', ['moves_applied']);
  if (decErr) return decErr;
  if (!Array.isArray(data.counts) || !data.counts.every(isFiniteNum)) {
    return fail('counts', 'expected number array');
  }
  return ok();
}

export function validateInjectResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  return checkNums(data, '', ['stop', 'count', 'time_s']) ?? ok();
}

export function validateUndoResponse(data: unknown): ValidationResult {
  if (!isRecord(data)) return fail('root', 'expected object');
  return (
    checkStrs(data, '', ['undone']) ??
    checkNums(data, '', ['history_size', 'geometry_version']) ??
    ok()
  );
}

/** Validates an API response payload against the endpoint's contract schema. */
export function validateEndpointResponse(
  path: string,
  method: string,
  data: unknown,
): ValidationResult {
  const pathname = path.split('?')[0];
  if (method.toUpperCase() === 'GET') {
    if (pathname === '/api/health') return validateHealthResponse(data);
    if (pathname === '/api/config') return validateConfigResponse(data);
    if (pathname === '/api/graph') return validateGraphResponse(data);
    if (pathname === '/api/stops') return validateStopsResponse(data);
    if (pathname === '/api/routes') return validateRoutesResponse(data);
    if (pathname === '/api/geometry') return validateGeometryResponse(data);
    if (pathname === '/api/state') return validateStateResponse(data);
    if (pathname.startsWith('/api/bus/')) return validateBusDetailResponse(data);
    if (pathname === '/api/schedule') return validateScheduleResponse(data);
    if (pathname === '/api/tree') return validateTreeResponse(data);
    if (pathname === '/api/demand/forecast') return validateDemandForecastResponse(data);
    if (pathname === '/api/metrics') return validateMetricsResponse(data);
    if (pathname === '/api/alerts') return validateAlertsResponse(data);
    if (pathname === '/api/history') return validateHistoryResponse(data);
    if (pathname === '/api/eval') return validateEvalResponse(data);
    return ok();
  }
  if (method.toUpperCase() === 'POST') {
    if (pathname === '/api/sim/control') return validateSimControlResponse(data);
    if (pathname === '/api/scenario') return validateScenarioResponse(data);
    if (pathname === '/api/allocate') return validateAllocateResponse(data);
    if (pathname === '/api/inject') return validateInjectResponse(data);
    if (pathname === '/api/route/optimize') return validateOptimizeResponse(data);
    if (pathname === '/api/undo') return validateUndoResponse(data);
    return ok();
  }
  return ok();
}

