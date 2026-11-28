import { useCallback, useEffect, useState } from 'react';
import {
  Bar,
  BarChart,
  CartesianGrid,
  ErrorBar,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import { ApiError, getJson } from '../api/client';
import type { EvalResponse } from '../api/types';
import { ciLabel, ciSignificance, minutesLabel } from '../lib/format';
import { useAppState } from '../state/AppState';

/** Displays paired evaluation results, cold-start comparison, anomaly thresholds, and ML summary. */
export function CompareView() {
  const { metrics } = useAppState();
  const [evalData, setEvalData] = useState<EvalResponse | null>(null);
  const [missingEval, setMissingEval] = useState<boolean>(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [loading, setLoading] = useState<boolean>(true);

  const loadEval = useCallback(async () => {
    setLoading(true);
    try {
      const res = await getJson<EvalResponse>('/api/eval');
      setEvalData(res);
      setMissingEval(false);
      setErrorMsg(null);
    } catch (err: unknown) {
      if (err instanceof ApiError && err.status === 404) {
        setMissingEval(true);
        setErrorMsg(null);
      } else {
        setMissingEval(false);
        setErrorMsg(
          err instanceof ApiError
            ? `${err.code}: ${err.message}`
            : 'Failed to load evaluation',
        );
      }
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    let active = true;
    Promise.resolve().then(() => {
      if (active) {
        void loadEval();
      }
    });
    return () => {
      active = false;
    };
  }, [loadEval]);

  return (
    <div className="panel-stack" data-testid="compare-view">
      <section className="disclaimer-panel" data-testid="eval-disclaimer">
        <strong>Simulation Assumptions &amp; Scope</strong>
        <p>
          Demand and arrivals are simulated. Results hold under the
          simulation&apos;s assumptions and are not real-world gains. Differences
          are paired over N seeds with 95 percent confidence intervals. Live
          TomTom data is a real-time overlay on a simulated clock.
        </p>
      </section>

      {loading && !evalData && !missingEval && !errorMsg && (
        <div className="panel-card">Loading benchmark evaluation...</div>
      )}

      {missingEval && (
        <div className="panel-card" role="status" data-testid="eval-missing">
          Run make eval in the backend to generate the evaluation
        </div>
      )}

      {errorMsg && (
        <div className="panel-card error-text" role="alert">
          {errorMsg}
        </div>
      )}

      {evalData?.scenarios.map((scenario) => (
        <section
          key={scenario.name}
          className="panel-card"
          data-testid={`scenario-${scenario.name}`}
        >
          <h2>
            Scenario: {scenario.name} ({scenario.metric})
          </h2>

          <div className="chart-box">
            <ResponsiveContainer
              width="100%"
              height={230}
              initialDimension={{ width: 500, height: 230 }}
            >
              <BarChart
                data={scenario.policies}
                margin={{ top: 10, right: 16, left: 0, bottom: 40 }}
              >
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis
                  dataKey="name"
                  interval={0}
                  angle={-25}
                  textAnchor="end"
                  tick={{ fontSize: 10 }}
                />
                <YAxis />
                <Tooltip />
                <Bar
                  dataKey="mean"
                  name="Mean wait (s)"
                  fill="#0072B2"
                  isAnimationActive={false}
                >
                  <ErrorBar
                    dataKey="sd"
                    width={4}
                    strokeWidth={2}
                    stroke="#0f172a"
                  />
                </Bar>
              </BarChart>
            </ResponsiveContainer>
          </div>

          <table className="data-table">
            <thead>
              <tr>
                <th>Policy</th>
                <th>Mean +- SD</th>
                <th>Diff vs {scenario.baseline}</th>
                <th>95% CI (s)</th>
                <th>W / T / L</th>
                <th>Verdict</th>
              </tr>
            </thead>
            <tbody>
              {scenario.policies.map((p) => {
                const verdict = ciSignificance(p.ci_lo, p.ci_hi);
                return (
                  <tr key={p.name}>
                    <td>{p.name}</td>
                    <td>
                      {minutesLabel(p.mean)} (+- {p.sd.toFixed(1)}s)
                    </td>
                    <td>{p.diff_mean.toFixed(1)} s</td>
                    <td>{ciLabel(p.ci_lo, p.ci_hi)}</td>
                    <td>
                      {p.wins}/{p.ties}/{p.losses}
                    </td>
                    <td>
                      <span
                        className={`pill-tag ${
                          verdict === 'significant'
                            ? 'pill-tag--ok'
                            : 'pill-tag--muted'
                        }`}
                      >
                        {verdict}
                      </span>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </section>
      ))}

      {evalData?.coldstart && (
        <section className="panel-card" data-testid="coldstart-section">
          <h2>Cold-Start Holdout Comparison</h2>
          <p className="muted-text">
            Paired unseen lambda MAE diff (RF - MA):{' '}
            {evalData.coldstart.paired_unseen_mae_lambda.diff_mean_rf_minus_ma.toFixed(3)}{' '}
            with 95% CI{' '}
            {ciLabel(
              evalData.coldstart.paired_unseen_mae_lambda.ci_lo,
              evalData.coldstart.paired_unseen_mae_lambda.ci_hi,
            )}{' '}
            (W/T/L {evalData.coldstart.paired_unseen_mae_lambda.wins}/
            {evalData.coldstart.paired_unseen_mae_lambda.ties}/
            {evalData.coldstart.paired_unseen_mae_lambda.losses})
          </p>
          <table className="data-table">
            <thead>
              <tr>
                <th>Split</th>
                <th>Model</th>
                <th>MAE (noisy)</th>
                <th>RMSE (noisy)</th>
                <th>MAE (lambda)</th>
              </tr>
            </thead>
            <tbody>
              {evalData.coldstart.seen.map((m) => (
                <tr key={`seen-${m.model}`}>
                  <td>Seen</td>
                  <td>{m.model}</td>
                  <td>{m.mae_noisy.toFixed(3)}</td>
                  <td>{m.rmse_noisy.toFixed(3)}</td>
                  <td>{m.mae_lambda.toFixed(3)}</td>
                </tr>
              ))}
              {evalData.coldstart.unseen.map((m) => (
                <tr key={`unseen-${m.model}`}>
                  <td>Unseen</td>
                  <td>{m.model}</td>
                  <td>{m.mae_noisy.toFixed(3)}</td>
                  <td>{m.rmse_noisy.toFixed(3)}</td>
                  <td>{m.mae_lambda.toFixed(3)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </section>
      )}

      {evalData?.anomaly && (
        <section className="panel-card" data-testid="anomaly-section">
          <h2>Anomaly Detector Thresholds</h2>
          <table className="data-table">
            <thead>
              <tr>
                <th>Threshold (z)</th>
                <th>False Alarms / Day</th>
                <th>Detection Rate</th>
                <th>Median Delay</th>
                <th>Precision</th>
              </tr>
            </thead>
            <tbody>
              {evalData.anomaly.thresholds.map((t) => (
                <tr key={t.threshold}>
                  <td>{t.threshold}</td>
                  <td>
                    {t.false_alarms_mean.toFixed(2)} (+- {t.false_alarms_sd.toFixed(2)})
                  </td>
                  <td>{t.detection_rate.toFixed(2)}</td>
                  <td>{t.median_delay_min} min</td>
                  <td>{t.precision.toFixed(3)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </section>
      )}

      {metrics?.ml && (
        <section className="panel-card" data-testid="ml-metrics-section">
          <h2>Random Forest &amp; scikit-learn Reference MAE</h2>
          <table className="data-table">
            <tbody>
              <tr>
                <th>Forest MAE (noisy test)</th>
                <td>
                  {metrics.ml.rf_mae !== undefined
                    ? metrics.ml.rf_mae.toFixed(4)
                    : 'N/A'}
                </td>
              </tr>
              <tr>
                <th>Forest RMSE (noisy test)</th>
                <td>
                  {metrics.ml.rf_rmse !== undefined
                    ? metrics.ml.rf_rmse.toFixed(4)
                    : 'N/A'}
                </td>
              </tr>
              <tr>
                <th>Forest MAE vs lambda</th>
                <td>
                  {metrics.ml.rf_mae_lambda !== undefined
                    ? metrics.ml.rf_mae_lambda.toFixed(4)
                    : 'N/A'}
                </td>
              </tr>
              <tr>
                <th>Model configuration</th>
                <td>
                  {metrics.ml.trees ?? '?'} trees, depth {metrics.ml.depth ?? '?'},
                  leaf {metrics.ml.leaf ?? '?'} (exported from scikit-learn
                  RandomForestRegressor)
                </td>
              </tr>
            </tbody>
          </table>
        </section>
      )}
    </div>
  );
}

