import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import { ApiError, getJson } from '../api/client';
import type { AlertsResponse, DemandForecastResponse } from '../api/types';
import { usePolling } from '../hooks/usePolling';
import { clockHHMM } from '../lib/format';
import { useAppState } from '../state/AppState';

/** Stop demand forecast chart over 24 hours with interactive anomaly alerts. */
export function DemandPanel() {
  const { state, stops, selectedStopIdx, setSelectedStopIdx, focusStop } =
    useAppState();

  const defaultStopIdx = useMemo(() => {
    const firstHub = stops.find((s) => s.type === 'hub');
    return firstHub?.idx ?? stops[0]?.idx ?? 0;
  }, [stops]);

  const activeStopIdx = selectedStopIdx ?? defaultStopIdx;

  const [forecast, setForecast] = useState<DemandForecastResponse | null>(null);
  const [forecastError, setForecastError] = useState<string | null>(null);
  const [forecastLoading, setForecastLoading] = useState<boolean>(false);

  const [alerts, setAlerts] = useState<AlertsResponse | null>(null);
  const [alertsError, setAlertsError] = useState<string | null>(null);

  const loadForecast = useCallback(async () => {
    setForecastLoading(true);
    try {
      const data = await getJson<DemandForecastResponse>(
        `/api/demand/forecast?stop=${activeStopIdx}`,
      );
      setForecast(data);
      setForecastError(null);
    } catch (err: unknown) {
      setForecastError(
        err instanceof ApiError
          ? `${err.code}: ${err.message}`
          : 'Failed to load demand forecast',
      );
    } finally {
      setForecastLoading(false);
    }
  }, [activeStopIdx]);

  const loadAlerts = useCallback(async () => {
    try {
      const res = await getJson<AlertsResponse>('/api/alerts');
      setAlerts(res);
      setAlertsError(null);
    } catch (err: unknown) {
      setAlertsError(
        err instanceof ApiError
          ? `${err.code}: ${err.message}`
          : 'Failed to load alerts',
      );
    }
  }, []);

  useEffect(() => {
    let active = true;
    Promise.resolve().then(() => {
      if (!active) return;
      void loadForecast();
      void loadAlerts();
    });
    return () => {
      active = false;
    };
  }, [loadForecast, loadAlerts]);

  usePolling(loadAlerts, 5000);

  const currentHour = Math.floor((state?.clock_min ?? 360) / 60) % 24;
  const hasMa = Boolean(forecast?.hours.some((h) => h.ma !== null));
  const hasRf = Boolean(forecast?.hours.some((h) => h.rf !== null));

  return (
    <div className="panel-stack" data-testid="demand-panel">
      <section className="panel-card">
        <div className="panel-header-row">
          <h2>Stop Demand Forecast (24h)</h2>
          <label className="field-group">
            <span>Stop</span>
            <select
              aria-label="Demand forecast stop"
              value={activeStopIdx}
              onChange={(e) => setSelectedStopIdx(Number(e.target.value))}
            >
              {stops.map((s) => (
                <option key={s.idx} value={s.idx}>
                  {s.idx}: {s.name} ({s.type})
                </option>
              ))}
              {stops.length === 0 && (
                <option value={activeStopIdx}>Stop {activeStopIdx}</option>
              )}
            </select>
          </label>
        </div>

        <p className="info-note" data-testid="ma-event-note">
          Note: The moving average predictor ignores events and predicts from
          historical slice averages alone.
        </p>

        {forecastLoading && !forecast && (
          <p className="muted-text">Loading stop demand forecast...</p>
        )}
        {forecastError && <p className="error-text">{forecastError}</p>}

        {forecast && (
          <div className="chart-box" data-testid="demand-chart">
            <div className="series-summary">
              <span>Now ({currentHour}h): {String(currentHour).padStart(2, '0')}:00</span>
              <span>
                Active series: Oracle, Observed
                {hasMa ? ', MA' : ''}
                {hasRf ? ', RF' : ''}
              </span>
            </div>
            <ResponsiveContainer
              width="100%"
              height={260}
              initialDimension={{ width: 500, height: 260 }}
            >
              <LineChart
                data={forecast.hours}
                margin={{ top: 10, right: 16, left: 0, bottom: 8 }}
              >
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="hour" />
                <YAxis />
                <Tooltip />
                <Legend />
                <ReferenceLine
                  x={currentHour}
                  stroke="#dc2626"
                  strokeDasharray="4 4"
                  label={`Now (${currentHour}h)`}
                />
                <Line
                  type="monotone"
                  dataKey="oracle"
                  name="Oracle"
                  stroke="#0f172a"
                  strokeWidth={2}
                  dot={false}
                  isAnimationActive={false}
                />
                {hasMa && (
                  <Line
                    type="monotone"
                    dataKey="ma"
                    name="Moving Average"
                    stroke="#E69F00"
                    strokeWidth={2}
                    dot={false}
                    isAnimationActive={false}
                  />
                )}
                {hasRf && (
                  <Line
                    type="monotone"
                    dataKey="rf"
                    name="Random Forest"
                    stroke="#009E73"
                    strokeWidth={2}
                    dot={false}
                    isAnimationActive={false}
                  />
                )}
                <Line
                  type="monotone"
                  dataKey="observed"
                  name="Observed"
                  stroke="#0072B2"
                  strokeWidth={2}
                  dot={{ r: 2 }}
                  isAnimationActive={false}
                />
              </LineChart>
            </ResponsiveContainer>
          </div>
        )}
      </section>

      <section className="panel-card">
        <h2>Anomaly Alerts ({alerts?.flags.length ?? 0})</h2>
        {alertsError && <p className="error-text">{alertsError}</p>}
        {!alerts && !alertsError && (
          <p className="muted-text">Loading anomaly alerts...</p>
        )}
        {alerts && alerts.flags.length === 0 ? (
          <p className="muted-text">No anomaly flags triggered yet.</p>
        ) : (
          <ul className="alert-list" data-testid="alerts-list">
            {(alerts?.flags ?? []).map((flag, idx) => (
              <li key={`${flag.stop}-${flag.time_min}-${idx}`}>
                <button
                  type="button"
                  className={`alert-item-btn ${
                    activeStopIdx === flag.stop ? 'alert-item-btn--selected' : ''
                  }`}
                  onClick={() => focusStop(flag.stop)}
                >
                  <strong>{flag.name}</strong>
                  <span>
                    {clockHHMM(flag.time_min)} | obs {flag.observed} vs exp{' '}
                    {flag.expected.toFixed(1)} | z={flag.z.toFixed(2)}
                  </span>
                </button>
              </li>
            ))}
          </ul>
        )}
      </section>
    </div>
  );
}
