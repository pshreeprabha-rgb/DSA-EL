import { useCallback, useEffect, useMemo, useState } from 'react';
import { ApiError, getJson, postJson } from '../api/client';
import type {
  BusDetailResponse,
  FleetTreeNode,
  FleetTreeResponse,
  HistoryResponse,
  OptimizeResponse,
  ScheduleResponse,
} from '../api/types';
import { clockHHMM, minutesLabel, stackTopFirst } from '../lib/format';
import { useAppState } from '../state/AppState';

type DsSubTab = 'schedule' | 'history' | 'journeys' | 'tree' | 'optimizer';

interface TreeEntry {
  node: FleetTreeNode;
  children: TreeEntry[];
}

function buildTreeHierarchy(nodes: readonly FleetTreeNode[]): TreeEntry[] {
  const byRow = new Map<number, TreeEntry>();
  const roots: TreeEntry[] = [];
  for (const n of nodes) {
    byRow.set(n.row, { node: n, children: [] });
  }
  for (const n of nodes) {
    const entry = byRow.get(n.row)!;
    if (n.parent < 0) {
      roots.push(entry);
    } else {
      const parent = byRow.get(n.parent);
      if (parent) {
        parent.children.push(entry);
      } else {
        roots.push(entry);
      }
    }
  }
  return roots;
}

function kindLabel(kind: number): string {
  switch (kind) {
    case 0:
      return 'CITY';
    case 1:
      return 'DEPOT';
    case 2:
      return 'ROUTE';
    case 3:
      return 'BUS';
    default:
      return 'NODE';
  }
}

/** Data structures explorer covering AVL schedule, undo stack, bus linked list, fleet tree, and route optimizer. */
export function DsView() {
  const { state, stops, routes, optimizedPath, setOptimizedPath } = useAppState();
  const [subTab, setSubTab] = useState<DsSubTab>('schedule');

  // Schedule (AVL)
  const [routeFilter, setRouteFilter] = useState<number>(-1);
  const [schedule, setSchedule] = useState<ScheduleResponse | null>(null);
  const [scheduleError, setScheduleError] = useState<string | null>(null);
  const [scheduleLoading, setScheduleLoading] = useState<boolean>(false);

  // History (Stack)
  const [history, setHistory] = useState<HistoryResponse | null>(null);
  const [historyError, setHistoryError] = useState<string | null>(null);
  const [historyLoading, setHistoryLoading] = useState<boolean>(false);

  // Bus journeys (Linked list)
  const [selectedBusId, setSelectedBusId] = useState<number>(0);
  const [busDetail, setBusDetail] = useState<BusDetailResponse | null>(null);
  const [busError, setBusError] = useState<string | null>(null);
  const [busLoading, setBusLoading] = useState<boolean>(false);

  // Tree
  const [treeData, setTreeData] = useState<FleetTreeResponse | null>(null);
  const [treeError, setTreeError] = useState<string | null>(null);
  const [treeLoading, setTreeLoading] = useState<boolean>(false);
  const [collapsedRows, setCollapsedRows] = useState<Set<number>>(() => new Set());

  // Optimizer
  const [fromStop, setFromStop] = useState<number>(0);
  const [toStop, setToStop] = useState<number>(5);
  const [optMode, setOptMode] = useState<'traffic' | 'static'>('traffic');
  const [optPending, setOptPending] = useState<boolean>(false);
  const [optError, setOptError] = useState<string | null>(null);
  const [modeComparison, setModeComparison] = useState<{
    staticRes: OptimizeResponse;
    trafficRes: OptimizeResponse;
  } | null>(null);

  const fromMin = Math.min(1439, Math.max(0, Math.floor(state?.clock_min ?? 360)));
  const toMin = Math.min(1440, fromMin + 120);

  const loadSchedule = useCallback(async () => {
    setScheduleLoading(true);
    try {
      const qs = `from=${fromMin}&to=${toMin}&route=${routeFilter}`;
      const res = await getJson<ScheduleResponse>(`/api/schedule?${qs}`);
      setSchedule(res);
      setScheduleError(null);
    } catch (err: unknown) {
      setScheduleError(
        err instanceof ApiError ? `${err.code}: ${err.message}` : 'Failed to load schedule',
      );
    } finally {
      setScheduleLoading(false);
    }
  }, [fromMin, toMin, routeFilter]);

  const loadHistory = useCallback(async () => {
    setHistoryLoading(true);
    try {
      const res = await getJson<HistoryResponse>('/api/history');
      setHistory(res);
      setHistoryError(null);
    } catch (err: unknown) {
      setHistoryError(
        err instanceof ApiError ? `${err.code}: ${err.message}` : 'Failed to load history',
      );
    } finally {
      setHistoryLoading(false);
    }
  }, []);

  const loadBusDetail = useCallback(async () => {
    setBusLoading(true);
    try {
      const res = await getJson<BusDetailResponse>(`/api/bus/${selectedBusId}`);
      setBusDetail(res);
      setBusError(null);
    } catch (err: unknown) {
      setBusError(
        err instanceof ApiError ? `${err.code}: ${err.message}` : 'Failed to load bus detail',
      );
    } finally {
      setBusLoading(false);
    }
  }, [selectedBusId]);

  const loadTree = useCallback(async () => {
    setTreeLoading(true);
    try {
      const res = await getJson<FleetTreeResponse>('/api/tree');
      setTreeData(res);
      setTreeError(null);
    } catch (err: unknown) {
      setTreeError(
        err instanceof ApiError ? `${err.code}: ${err.message}` : 'Failed to load fleet tree',
      );
    } finally {
      setTreeLoading(false);
    }
  }, []);

  useEffect(() => {
    let active = true;
    Promise.resolve().then(() => {
      if (!active) return;
      if (subTab === 'schedule') void loadSchedule();
      if (subTab === 'history') void loadHistory();
      if (subTab === 'journeys') void loadBusDetail();
      if (subTab === 'tree') void loadTree();
    });
    return () => {
      active = false;
    };
  }, [subTab, loadSchedule, loadHistory, loadBusDetail, loadTree]);

  const treeRoots = useMemo(
    () => buildTreeHierarchy(treeData?.nodes ?? []),
    [treeData],
  );

  const toggleCollapse = (row: number) => {
    setCollapsedRows((prev) => {
      const next = new Set(prev);
      if (next.has(row)) {
        next.delete(row);
      } else {
        next.add(row);
      }
      return next;
    });
  };

  const renderTreeNodes = (entries: readonly TreeEntry[]) => {
    return (
      <ul className="tree-list">
        {entries.map(({ node, children }) => {
          const isCollapsed = collapsedRows.has(node.row);
          const hasChildren = children.length > 0;
          return (
            <li key={node.row} className="tree-node">
              <div className="tree-row">
                {hasChildren ? (
                  <button
                    type="button"
                    className="tree-toggle"
                    aria-expanded={!isCollapsed}
                    onClick={() => toggleCollapse(node.row)}
                  >
                    {isCollapsed ? '+' : '-'} {node.name}
                  </button>
                ) : (
                  <span className="tree-leaf">{node.name}</span>
                )}
                <span className="pill-tag">{kindLabel(node.kind)}</span>
              </div>
              {hasChildren && !isCollapsed && renderTreeNodes(children)}
            </li>
          );
        })}
      </ul>
    );
  };

  const handleOptimize = async () => {
    setOptPending(true);
    setOptError(null);
    try {
      const res = await postJson<OptimizeResponse>('/api/route/optimize', {
        from_stop: fromStop,
        to_stop: toStop,
        algo: 'both',
        mode: optMode,
      });
      setOptimizedPath(res);
    } catch (err: unknown) {
      setOptError(
        err instanceof ApiError ? `${err.code}: ${err.message}` : 'Optimization failed',
      );
    } finally {
      setOptPending(false);
    }
  };

  const handleCompareModes = async () => {
    setOptPending(true);
    setOptError(null);
    try {
      const [staticRes, trafficRes] = await Promise.all([
        postJson<OptimizeResponse>('/api/route/optimize', {
          from_stop: fromStop,
          to_stop: toStop,
          algo: 'both',
          mode: 'static',
        }),
        postJson<OptimizeResponse>('/api/route/optimize', {
          from_stop: fromStop,
          to_stop: toStop,
          algo: 'both',
          mode: 'traffic',
        }),
      ]);
      setModeComparison({ staticRes, trafficRes });
      setOptimizedPath(trafficRes);
    } catch (err: unknown) {
      setOptError(
        err instanceof ApiError ? `${err.code}: ${err.message}` : 'Comparison failed',
      );
    } finally {
      setOptPending(false);
    }
  };

  const stackRows = stackTopFirst(history?.entries ?? []);

  return (
    <div className="panel-stack" data-testid="ds-view">
      <div className="subtab-bar" role="tablist" aria-label="Data structure views">
        {(
          [
            ['schedule', 'Schedule (AVL)'],
            ['history', 'History (Stack)'],
            ['journeys', 'Bus journeys'],
            ['tree', 'Fleet tree'],
            ['optimizer', 'Route optimizer'],
          ] as const
        ).map(([id, label]) => (
          <button
            key={id}
            type="button"
            role="tab"
            aria-selected={subTab === id}
            className={`subtab-btn ${subTab === id ? 'subtab-btn--active' : ''}`}
            onClick={() => setSubTab(id)}
          >
            {label}
          </button>
        ))}
      </div>

      {subTab === 'schedule' && (
        <section className="panel-card" data-testid="ds-schedule">
          <div className="panel-header-row">
            <h2>Upcoming Trips (Next 120 min)</h2>
            <label className="field-group">
              <span>Route Filter</span>
              <select
                aria-label="Schedule route filter"
                value={routeFilter}
                onChange={(e) => setRouteFilter(Number(e.target.value))}
              >
                <option value={-1}>All routes</option>
                {(routes?.routes ?? []).map((r) => (
                  <option key={r.id} value={r.id}>
                    R{r.id + 1}: {r.name}
                  </option>
                ))}
              </select>
            </label>
          </div>

          {schedule && (
            <div className="badge-strip" data-testid="avl-badges">
              <span className="pill-tag">Size: {schedule.avl.size}</span>
              <span className="pill-tag">Height: {schedule.avl.height}</span>
              <span
                className={`pill-tag ${
                  schedule.avl.balanced ? 'pill-tag--ok' : 'pill-tag--warn'
                }`}
              >
                {schedule.avl.balanced ? 'Balanced' : 'Unbalanced'}
              </span>
            </div>
          )}

          {scheduleLoading && !schedule && <p>Loading schedule...</p>}
          {scheduleError && <p className="error-text">{scheduleError}</p>}

          {schedule && schedule.trips.length === 0 ? (
            <p className="muted-text">No trips in window.</p>
          ) : (
            schedule && (
              <table className="data-table">
                <thead>
                  <tr>
                    <th>Trip</th>
                    <th>Route</th>
                    <th>Bus</th>
                    <th>Departure</th>
                  </tr>
                </thead>
                <tbody>
                  {schedule.trips.map((trip) => (
                    <tr key={trip.trip_id}>
                      <td>#{trip.trip_id}</td>
                      <td>R{trip.route_id + 1}</td>
                      <td>Bus {trip.bus_id}</td>
                      <td>{trip.departure}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )
          )}
        </section>
      )}

      {subTab === 'history' && (
        <section className="panel-card" data-testid="ds-history">
          <h2>ChangeSet History Stack ({history?.size ?? 0})</h2>
          {historyLoading && !history && <p>Loading history...</p>}
          {historyError && <p className="error-text">{historyError}</p>}
          {stackRows.length === 0 && !historyLoading ? (
            <p className="muted-text">History stack is empty.</p>
          ) : (
            <ul className="stack-list">
              {stackRows.map((entry, idx) => (
                <li
                  key={`${idx}-${entry.text}`}
                  className={
                    entry.isTop ? 'stack-item stack-item--top' : 'stack-item'
                  }
                >
                  {entry.isTop && <span className="pill-tag">TOP</span>}
                  <span>{entry.text}</span>
                </li>
              ))}
            </ul>
          )}
        </section>
      )}

      {subTab === 'journeys' && (
        <section className="panel-card" data-testid="ds-journeys">
          <div className="panel-header-row">
            <h2>Bus Journeys (Linked List)</h2>
            <label className="field-group">
              <span>Bus</span>
              <select
                aria-label="Bus journeys selector"
                value={selectedBusId}
                onChange={(e) => setSelectedBusId(Number(e.target.value))}
              >
                {(state?.buses ?? [{ id: 0, state: 'running' }]).map((b) => (
                  <option key={b.id} value={b.id}>
                    Bus {b.id} ({b.state})
                  </option>
                ))}
              </select>
            </label>
          </div>
          {busLoading && !busDetail && <p>Loading journeys...</p>}
          {busError && <p className="error-text">{busError}</p>}
          {busDetail && (
            <>
              <div className="badge-strip">
                <span className="pill-tag">State: {busDetail.state}</span>
                <span className="pill-tag">Load: {busDetail.load}/{busDetail.capacity}</span>
                <span className="pill-tag">Stop: {busDetail.stop_idx}</span>
              </div>
              {busDetail.journeys.length === 0 ? (
                <p className="muted-text">No completed or active journeys recorded.</p>
              ) : (
                <table className="data-table">
                  <thead>
                    <tr>
                      <th>Trip</th>
                      <th>Route</th>
                      <th>Start</th>
                      <th>End</th>
                      <th>Passengers</th>
                    </tr>
                  </thead>
                  <tbody>
                    {busDetail.journeys.map((j) => (
                      <tr key={j.trip_id}>
                        <td>#{j.trip_id}</td>
                        <td>R{j.route_id + 1}</td>
                        <td>{clockHHMM(j.start_min)}</td>
                        <td>{clockHHMM(j.end_min)}</td>
                        <td>{j.passengers}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              )}
            </>
          )}
        </section>
      )}

      {subTab === 'tree' && (
        <section className="panel-card" data-testid="ds-tree">
          <h2>Fleet Hierarchy Tree</h2>
          {treeData && (
            <div className="badge-strip">
              <span className="pill-tag">Nodes: {treeData.count}</span>
              <span className="pill-tag">Height: {treeData.height}</span>
            </div>
          )}
          {treeLoading && !treeData && <p>Loading fleet tree...</p>}
          {treeError && <p className="error-text">{treeError}</p>}
          {treeRoots.length > 0 && renderTreeNodes(treeRoots)}
        </section>
      )}

      {subTab === 'optimizer' && (
        <section className="panel-card" data-testid="ds-optimizer">
          <h2>Shortest-Path Route Optimizer</h2>
          <div className="form-row">
            <label className="field-group">
              <span>From Stop</span>
              <select
                aria-label="Optimizer from stop"
                value={fromStop}
                onChange={(e) => setFromStop(Number(e.target.value))}
              >
                {stops.map((s) => (
                  <option key={s.idx} value={s.idx}>
                    {s.idx}: {s.name}
                  </option>
                ))}
                {stops.length === 0 && <option value={0}>Stop 0</option>}
              </select>
            </label>
            <label className="field-group">
              <span>To Stop</span>
              <select
                aria-label="Optimizer to stop"
                value={toStop}
                onChange={(e) => setToStop(Number(e.target.value))}
              >
                {stops.map((s) => (
                  <option key={s.idx} value={s.idx}>
                    {s.idx}: {s.name}
                  </option>
                ))}
                {stops.length === 0 && <option value={5}>Stop 5</option>}
              </select>
            </label>
            <label className="field-group">
              <span>Mode</span>
              <select
                aria-label="Optimizer mode"
                value={optMode}
                onChange={(e) =>
                  setOptMode(e.target.value as 'traffic' | 'static')
                }
              >
                <option value="traffic">traffic</option>
                <option value="static">static</option>
              </select>
            </label>
          </div>
          <div className="btn-row">
            <button
              type="button"
              className="action-btn action-btn--primary"
              disabled={optPending}
              onClick={() => void handleOptimize()}
            >
              Optimize Path
            </button>
            <button
              type="button"
              className="action-btn"
              disabled={optPending}
              onClick={() => void handleCompareModes()}
            >
              Compare Static vs Traffic
            </button>
            {optimizedPath && (
              <button
                type="button"
                className="action-btn"
                onClick={() => setOptimizedPath(null)}
              >
                Clear Path
              </button>
            )}
          </div>

          {optError && <p className="error-text">{optError}</p>}

          {optimizedPath && (
            <div className="optimizer-results" data-testid="optimizer-results">
              {optimizedPath.nodes_ratio !== undefined && (
                <p>
                  <strong>Nodes ratio (A* / Dijkstra):</strong>{' '}
                  {optimizedPath.nodes_ratio.toFixed(3)}
                </p>
              )}
              <table className="data-table">
                <thead>
                  <tr>
                    <th>Algorithm</th>
                    <th>Time</th>
                    <th>Length (m)</th>
                    <th>Nodes Explored</th>
                    <th>Traffic Time</th>
                  </tr>
                </thead>
                <tbody>
                  {optimizedPath.astar && (
                    <tr>
                      <td>A*</td>
                      <td>{minutesLabel(optimizedPath.astar.time_s)}</td>
                      <td>{optimizedPath.astar.length_m.toFixed(1)} m</td>
                      <td>{optimizedPath.astar.nodes_explored}</td>
                      <td>{minutesLabel(optimizedPath.astar.time_traffic_s)}</td>
                    </tr>
                  )}
                  {optimizedPath.dijkstra && (
                    <tr>
                      <td>Dijkstra</td>
                      <td>{minutesLabel(optimizedPath.dijkstra.time_s)}</td>
                      <td>{optimizedPath.dijkstra.length_m.toFixed(1)} m</td>
                      <td>{optimizedPath.dijkstra.nodes_explored}</td>
                      <td>{minutesLabel(optimizedPath.dijkstra.time_traffic_s)}</td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>
          )}

          {modeComparison && (
            <div className="info-note" data-testid="mode-comparison">
              <strong>Static-planned vs Traffic-planned traffic traversal:</strong>{' '}
              Static path traffic time:{' '}
              {minutesLabel(modeComparison.staticRes.astar?.time_traffic_s ?? -1)}{' '}
              vs Traffic path traffic time:{' '}
              {minutesLabel(modeComparison.trafficRes.astar?.time_traffic_s ?? -1)}
            </div>
          )}
        </section>
      )}
    </div>
  );
}

