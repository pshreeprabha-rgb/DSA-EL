import { useMemo } from 'react';
import {
  Bar,
  BarChart,
  CartesianGrid,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import {
  boardDisplayOrder,
  minutesLabel,
  routeColor,
  topQueuesByStop,
} from '../lib/format';
import { useAppState } from '../state/AppState';

/** Live operational dashboard displaying KPIs, route priority heap order, allocation, and queues. */
export function DashboardPanel() {
  const { state, stops, routes, routesError, metrics, metricsError } =
    useAppState();

  const boardRows = useMemo(() => {
    if (!routes) return [];
    return boardDisplayOrder(routes.board);
  }, [routes]);

  const routeMap = useMemo(() => {
    const map = new Map<number, { name: string; buses: number; demand: number }>();
    if (routes) {
      for (const r of routes.routes) {
        map.set(r.id, { name: r.name, buses: r.buses, demand: r.demand });
      }
    }
    return map;
  }, [routes]);

  const maxPriority = useMemo(() => {
    let maxP = 1;
    for (const row of boardRows) {
      if (row.priority > maxP) {
        maxP = row.priority;
      }
    }
    return maxP;
  }, [boardRows]);

  const topQueues = useMemo(() => {
    if (!state) return [];
    return topQueuesByStop(state.queues, stops, 15);
  }, [state, stops]);

  const fleetCounts = useMemo(() => {
    const perRoute = new Map<number, number>();
    let reserve = 0;
    let broken = 0;
    if (state) {
      for (const b of state.buses) {
        if (b.state === 'reserve') {
          reserve += 1;
        } else if (b.state === 'broken') {
          broken += 1;
        } else if (b.route >= 0) {
          perRoute.set(b.route, (perRoute.get(b.route) ?? 0) + 1);
        }
      }
    }
    return { perRoute, reserve, broken };
  }, [state]);

  if (!state && !routes && !metrics) {
    return (
      <div className="panel-card" data-testid="dashboard-loading">
        Loading dashboard telemetry...
      </div>
    );
  }

  return (
    <div className="panel-stack" data-testid="dashboard-panel">
      {(routesError || metricsError) && (
        <div role="alert" className="panel-error">
          {routesError || metricsError}
        </div>
      )}

      <section className="kpi-grid" data-testid="kpi-cards">
        <div className="kpi-card">
          <span className="kpi-label">Mean wait (incl. censored)</span>
          <strong className="kpi-value">
            {state
              ? minutesLabel(state.summary.mean_wait_incl_censored_s)
              : 'N/A'}
          </strong>
        </div>
        <div className="kpi-card">
          <span className="kpi-label">P95 wait</span>
          <strong className="kpi-value">
            {state ? minutesLabel(state.summary.p95_s) : 'N/A'}
          </strong>
        </div>
        <div className="kpi-card">
          <span className="kpi-label">Boarded</span>
          <strong className="kpi-value">{state?.summary.boarded ?? 0}</strong>
        </div>
        <div className="kpi-card">
          <span className="kpi-label">Still waiting</span>
          <strong className="kpi-value">
            {state?.summary.still_waiting ?? 0}
          </strong>
        </div>
        <div className="kpi-card">
          <span className="kpi-label">Full-bus events</span>
          <strong className="kpi-value">
            {metrics?.sim.full_bus_events ?? 0}
          </strong>
        </div>
      </section>

      <section className="panel-card">
        <h2>Route Priority Board (Heap Order)</h2>
        {boardRows.length === 0 ? (
          <p className="muted-text">No route board entries loaded.</p>
        ) : (
          <table className="data-table" data-testid="priority-board-table">
            <thead>
              <tr>
                <th>Rank</th>
                <th>Route</th>
                <th>Demand</th>
                <th>Buses</th>
                <th>Priority</th>
              </tr>
            </thead>
            <tbody>
              {boardRows.map((entry, idx) => {
                const info = routeMap.get(entry.route_id);
                const pct = Math.max(4, Math.min(100, (entry.priority / maxPriority) * 100));
                return (
                  <tr key={entry.route_id}>
                    <td>{idx + 1}</td>
                    <td>
                      <span
                        className="color-dot"
                        style={{ backgroundColor: routeColor(entry.route_id) }}
                        aria-hidden="true"
                      />
                      {info?.name ?? `R${entry.route_id + 1}`}
                    </td>
                    <td>{entry.demand.toFixed(1)}</td>
                    <td>{entry.buses}</td>
                    <td>
                      <div className="priority-bar-cell">
                        <div className="priority-bar-track">
                          <div
                            className="priority-bar-fill"
                            style={{
                              width: `${pct}%`,
                              backgroundColor: routeColor(entry.route_id),
                            }}
                          />
                        </div>
                        <span>{entry.priority.toFixed(1)}</span>
                      </div>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </section>

      <section className="panel-card">
        <h2>Fleet Allocation</h2>
        <table className="data-table" data-testid="allocation-table">
          <thead>
            <tr>
              <th>Assignment</th>
              <th>Buses</th>
            </tr>
          </thead>
          <tbody>
            {(routes?.routes ?? []).map((r) => (
              <tr key={r.id}>
                <td>
                  <span
                    className="color-dot"
                    style={{ backgroundColor: routeColor(r.id) }}
                    aria-hidden="true"
                  />
                  {r.name}
                </td>
                <td>{fleetCounts.perRoute.get(r.id) ?? r.buses}</td>
              </tr>
            ))}
            <tr>
              <td>Reserve pool</td>
              <td>{fleetCounts.reserve}</td>
            </tr>
            <tr>
              <td>Broken / out of service</td>
              <td>{fleetCounts.broken}</td>
            </tr>
          </tbody>
        </table>
      </section>

      <section className="panel-card">
        <h2>Top 15 Longest Stop Queues</h2>
        {topQueues.length === 0 ? (
          <p className="muted-text">No queue data available.</p>
        ) : (
          <div className="chart-box" data-testid="queue-chart">
            <ResponsiveContainer
              width="100%"
              height={260}
              initialDimension={{ width: 480, height: 260 }}
            >
              <BarChart
                data={topQueues}
                margin={{ top: 8, right: 12, left: 0, bottom: 52 }}
              >
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis
                  dataKey="name"
                  interval={0}
                  angle={-35}
                  textAnchor="end"
                  tick={{ fontSize: 10 }}
                />
                <YAxis allowDecimals={false} />
                <Tooltip />
                <Bar
                  dataKey="waiting"
                  name="Waiting passengers"
                  fill="#0072B2"
                  isAnimationActive={false}
                />
              </BarChart>
            </ResponsiveContainer>
          </div>
        )}
      </section>
    </div>
  );
}

