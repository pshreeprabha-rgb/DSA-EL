export type LatLngTuple = [number, number];

export type ServerErrorCode =
  | 'invalid_argument'
  | 'not_found'
  | 'method_not_allowed'
  | 'conflict'
  | 'payload_too_large'
  | 'internal';

export interface ApiErrorBody {
  error: {
    code: ServerErrorCode;
    message: string;
  };
}

/** GET /api/health */
export interface HealthResponse {
  ok: boolean;
  sim_running: boolean;
}

/** GET /api/config */
export interface ConfigResponse {
  stops: number;
  routes: number;
  buses: number;
  reserve: number;
  seed: number;
  start_min: number;
  end_min: number;
  slice_min: number;
  policy: string;
  predictor: string;
  spacing: string;
  reassign: string;
  models: {
    ma: boolean;
    rf: boolean;
  };
  offline: boolean;
}

/** GET /api/graph */
export type GraphNodeTuple = [number, number, number];
export type GraphEdgeTuple = [number, number, number];

export interface GraphResponse {
  nodes: GraphNodeTuple[];
  edges: GraphEdgeTuple[];
}

export type StopType = 'hub' | 'office' | 'market' | 'residential';

export interface StopInfo {
  idx: number;
  node: number;
  name: string;
  type: StopType | string;
  scale: number;
  lat: number;
  lng: number;
  route: number;
  pos: number;
  event_active: boolean;
  first_flag_min: number;
}

/** GET /api/stops */
export interface StopsResponse {
  stops: StopInfo[];
}

export interface RouteSummary {
  id: number;
  name: string;
  stops: number[];
  loop_min: number;
  cycle_min: number;
  buses: number;
  demand: number;
  priority: number;
}

export interface RouteBoardEntry {
  route_id: number;
  priority: number;
  demand: number;
  buses: number;
}

/** GET /api/routes */
export interface RoutesResponse {
  hour: number;
  basis: 'oracle' | 'forecast' | string;
  routes: RouteSummary[];
  board: RouteBoardEntry[];
}

export interface GeometryLeg {
  route: number;
  leg: number;
  from_stop: number;
  to_stop: number;
  time_s: number;
  path: LatLngTuple[];
}

export type BlockedEdgeSegment = [LatLngTuple, LatLngTuple];

/** GET /api/geometry */
export interface GeometryResponse {
  version: number;
  legs: GeometryLeg[];
  blocked: BlockedEdgeSegment[];
}

export type BusStateName = 'running' | 'deadhead' | 'reserve' | 'broken';

export interface BusMotion {
  moving: boolean;
  leg_route: number;
  leg_index: number;
  from_stop: number;
  to_stop: number;
  depart_s: number;
  arrive_s: number;
}

export interface BusState {
  id: number;
  state: BusStateName;
  route: number;
  stop_idx: number;
  load: number;
  pending_route: number;
  motion: BusMotion;
}

export interface StopQueueState {
  stop: number;
  waiting: number;
}

export interface StateSummary {
  arrived: number;
  boarded: number;
  still_waiting: number;
  mean_wait_boarded_s: number;
  mean_wait_incl_censored_s: number;
  p95_s: number;
}

export type TrafficSource = 'simulated' | 'live';

/** GET /api/state */
export interface SimStateResponse {
  clock_min: number;
  clock: string;
  running: boolean;
  finished: boolean;
  speed: number;
  day_type: string;
  rain: boolean;
  policy: string;
  traffic_source: TrafficSource | string;
  geometry_version: number;
  history_size: number;
  buses: BusState[];
  queues: StopQueueState[];
  summary: StateSummary;
}

export interface BusJourney {
  trip_id: number;
  route_id: number;
  start_min: number;
  end_min: number;
  passengers: number;
}

/** GET /api/bus/:id */
export interface BusDetailResponse {
  id: number;
  state: BusStateName;
  route: number;
  stop_idx: number;
  load: number;
  capacity: number;
  depot: number;
  journeys: BusJourney[];
}

export interface ScheduledTrip {
  trip_id: number;
  route_id: number;
  bus_id: number;
  departure_min: number;
  departure: string;
}

/** GET /api/schedule */
export interface ScheduleResponse {
  from: number;
  to: number;
  count: number;
  avl: {
    size: number;
    height: number;
    balanced: boolean;
  };
  trips: ScheduledTrip[];
}

export interface FleetTreeNode {
  row: number;
  parent: number;
  depth: number;
  kind: number;
  id: number;
  name: string;
}

/** GET /api/tree */
export interface FleetTreeResponse {
  count: number;
  height: number;
  nodes: FleetTreeNode[];
}

export interface DemandHourEntry {
  hour: number;
  oracle: number;
  ma: number | null;
  rf: number | null;
  observed: number;
}

/** GET /api/demand/forecast */
export interface DemandForecastResponse {
  stop: number;
  name: string;
  type: StopType | string;
  day_type: string;
  rain: boolean;
  event_active: boolean;
  hours: DemandHourEntry[];
}

export interface SimStats {
  arrived: number;
  boarded: number;
  still_waiting: number;
  wait_sum_s: number;
  wait_max_s: number;
  mean_wait_boarded_s: number;
  mean_wait_incl_censored_s: number;
  p50_s: number;
  p95_s: number;
  full_bus_events: number;
  forced_dumps: number;
  stranded_by_breakdown: number;
  bus_arrivals: number;
  moves_applied: number;
  deadhead_s_total: number;
  anomaly_flags: number;
  emergency_dispatches: number;
  stalled_s: number;
  mean_load_factor: number;
  decisions: number;
  decisions_applied: number;
}

export interface RouteMetricsEntry {
  route: number;
  boarded: number;
  still_waiting: number;
  mean_wait_s: number;
  max_wait_s: number;
}

export interface OptimizeAlgoResult {
  found: boolean;
  time_s: number;
  length_m: number;
  nodes_explored: number;
  time_static_s: number;
  time_traffic_s: number;
  path: LatLngTuple[];
}

/** POST /api/route/optimize */
export interface OptimizeResponse {
  mode: 'traffic' | 'static' | string;
  astar?: OptimizeAlgoResult;
  dijkstra?: OptimizeAlgoResult;
  nodes_ratio?: number;
}

export interface MlMetricsSummary {
  train_rows?: number;
  test_rows?: number;
  train_days?: number;
  test_days?: number;
  trees?: number;
  depth?: number;
  leaf?: number;
  rf_mae?: number;
  rf_mae_noisy?: number;
  rf_rmse?: number;
  rf_wape?: number;
  rf_mae_lambda?: number;
  ma_mae?: number;
}

export interface TrafficMetricsSummary {
  source: TrafficSource | string;
  calls: number;
  live_corridors: number;
  last_error: string;
}

/** GET /api/metrics */
export interface MetricsResponse {
  sim: SimStats;
  routes: RouteMetricsEntry[];
  ml: MlMetricsSummary | null;
  last_optimize: OptimizeResponse | null;
  traffic: TrafficMetricsSummary;
}

export interface AnomalyFlag {
  stop: number;
  name: string;
  time_min: number;
  observed: number;
  expected: number;
  z: number;
}

/** GET /api/alerts */
export interface AlertsResponse {
  flags: AnomalyFlag[];
  log: string[];
}

/** GET /api/history */
export interface HistoryResponse {
  size: number;
  entries: string[];
}

export interface EvalPolicySummary {
  name: string;
  n: number;
  mean: number;
  sd: number;
  diff_mean: number;
  ci_lo: number;
  ci_hi: number;
  wins: number;
  ties: number;
  losses: number;
  significant: boolean;
}

export interface EvalScenarioSummary {
  name: string;
  metric: string;
  baseline: string;
  policies: EvalPolicySummary[];
}

export interface EvalColdStartModel {
  model: string;
  mae_noisy: number;
  rmse_noisy: number;
  mae_lambda: number;
}

export interface EvalColdStartPaired {
  n_days: number;
  diff_mean_rf_minus_ma: number;
  ci_lo: number;
  ci_hi: number;
  wins: number;
  ties: number;
  losses: number;
}

export interface EvalColdStartSummary {
  train_rows: number;
  holdout_stops: number;
  seen_rows: number;
  unseen_rows: number;
  seen: EvalColdStartModel[];
  unseen: EvalColdStartModel[];
  paired_unseen_mae_lambda: EvalColdStartPaired;
}

export interface EvalAnomalyThreshold {
  threshold: number;
  false_alarms_mean: number;
  false_alarms_sd: number;
  detection_rate: number;
  median_delay_min: number;
  precision: number;
}

export interface EvalAnomalyValidation {
  analytic_expected: number;
  observed_mean: number;
  standard_error: number;
  pass: boolean;
}

export interface EvalAnomalySummary {
  seeds: number;
  base_seed: number;
  event_stop: number;
  thresholds: EvalAnomalyThreshold[];
  analytic_validation: EvalAnomalyValidation;
}

/** GET /api/eval */
export interface EvalResponse {
  config: {
    seeds: number;
    base_seed: number;
    stops: number;
    routes: number;
    buses: number;
    spacing: string;
    reassign: string;
  };
  scenarios: EvalScenarioSummary[];
  coldstart?: EvalColdStartSummary;
  anomaly?: EvalAnomalySummary;
}

export interface SimControlRequest {
  action?: 'start' | 'pause' | 'resume' | 'speed' | 'reset' | 'seek';
  speed?: number;
  time_min?: number;
}

/** POST /api/sim/control */
export interface SimControlResponse {
  ok?: boolean;
  clock_min: number;
  running: boolean;
  speed: number;
}

export type ScenarioRequest =
  | { kind: 'rain_on' | 'rain_off' }
  | { kind: 'event_on' | 'event_off'; stop: number }
  | { kind: 'breakdown' | 'repair'; bus: number }
  | { kind: 'block_edge' | 'unblock_edge'; edge: number }
  | { kind: 'block_edge' | 'unblock_edge'; route: number; leg: number };

/** POST /api/scenario */
export interface ScenarioResponse {
  scheduled: boolean;
  time_min: number;
  kind: string;
}

export type AllocateRequest =
  | {
      policy: 'equal' | 'proportional' | 'greedy_spec' | 'greedy_marginal';
      predictor?: 'oracle' | 'ma' | 'rf';
      force?: boolean;
    }
  | {
      manual_move: {
        bus: number;
        to_route: number;
      };
    };

/** POST /api/allocate */
export type AllocateResponse =
  | {
      policy: string;
      decision: {
        forced: boolean;
        moves_applied: number;
      };
      counts: number[];
    }
  | {
      moved: {
        bus: number;
        to_route: number;
      };
    };

export interface InjectRequest {
  stop: number;
  count: number;
  ride?: number;
}

/** POST /api/inject */
export interface InjectResponse {
  stop: number;
  count: number;
  time_s: number;
}

export interface OptimizeRequest {
  from_stop: number;
  to_stop: number;
  algo: 'astar' | 'dijkstra' | 'both';
  mode: 'traffic' | 'static';
}

/** POST /api/undo */
export interface UndoResponse {
  undone: string;
  history_size: number;
  geometry_version: number;
}

