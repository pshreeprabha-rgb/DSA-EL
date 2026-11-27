/**
 * @file eval.c
 * @brief Statistical evaluation, paired simulation runner, and anomaly validation.
 */

#include "eval.h"
#include "anomaly.h"
#include "demand_model.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

double eval_t_crit_975(size_t df) {
    static const double t_1_to_30[30] = {
        12.706, 4.303, 3.182, 2.776, 2.571,
        2.447, 2.365, 2.306, 2.262, 2.228,
        2.201, 2.179, 2.160, 2.145, 2.131,
        2.120, 2.110, 2.101, 2.093, 2.086,
        2.080, 2.074, 2.069, 2.064, 2.060,
        2.056, 2.052, 2.048, 2.045, 2.042
    };
    if (df == 0) {
        return NAN;
    }
    if (df <= 30) {
        return t_1_to_30[df - 1];
    }
    if (df < 40) {
        return 2.042;
    }
    if (df < 60) {
        return 2.021;
    }
    if (df < 120) {
        return 2.000;
    }
    if (df == 120) {
        return 1.980;
    }
    return 1.960;
}

Status eval_mean_sd(const double *x, size_t n, double *mean, double *sd) {
    if (!x || !mean || !sd) {
        return STATUS_ERR_NULL;
    }
    if (n < 2) {
        return STATUS_ERR_INVALID;
    }

    double sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        if (!isfinite(x[i])) {
            return STATUS_ERR_INVALID;
        }
        sum += x[i];
    }
    double m = sum / (double)n;

    double sq = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = x[i] - m;
        sq += d * d;
    }
    double var = sq / (double)(n - 1);
    if (var < 0.0) {
        var = 0.0;
    }

    *mean = m;
    *sd = sqrt(var);
    return STATUS_OK;
}

Status eval_paired_stats(const double *base, const double *other, size_t n, EvalStats *out) {
    if (!base || !other || !out) {
        return STATUS_ERR_NULL;
    }
    if (n < 2) {
        return STATUS_ERR_INVALID;
    }

    double sum = 0.0;
    size_t wins = 0;
    size_t ties = 0;
    size_t losses = 0;

    for (size_t i = 0; i < n; i++) {
        if (!isfinite(base[i]) || !isfinite(other[i])) {
            return STATUS_ERR_INVALID;
        }
        double d = other[i] - base[i];
        sum += d;
        if (d < -1e-9) {
            wins++;
        } else if (d > 1e-9) {
            losses++;
        } else {
            ties++;
        }
    }

    double mean = sum / (double)n;
    double sq = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = (other[i] - base[i]) - mean;
        sq += d * d;
    }
    double var = sq / (double)(n - 1);
    if (var < 0.0) {
        var = 0.0;
    }
    double sd = sqrt(var);
    double se = sd / sqrt((double)n);
    double t = eval_t_crit_975(n - 1);

    out->n = n;
    out->mean = mean;
    out->sd = sd;
    out->se = se;
    out->ci_lo = mean - t * se;
    out->ci_hi = mean + t * se;
    out->wins = wins;
    out->ties = ties;
    out->losses = losses;
    return STATUS_OK;
}

double eval_poisson_tail_ge(double mu, int k) {
    if (isnan(mu)) {
        return NAN;
    }
    if (k <= 0) {
        return 1.0;
    }
    if (mu <= 0.0) {
        return 0.0;
    }

    double p = exp(-mu);
    double sum = p;
    for (int j = 1; j < k; j++) {
        p *= mu / (double)j;
        sum += p;
    }
    double tail = 1.0 - sum;
    if (tail < 0.0) {
        tail = 0.0;
    }
    if (tail > 1.0) {
        tail = 1.0;
    }
    return tail;
}

static int dbl_cmp(const void *pa, const void *pb) {
    double a = *(const double *)pa;
    double b = *(const double *)pb;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

double eval_median(double *x, size_t n) {
    if (!x || n == 0) {
        return NAN;
    }
    for (size_t i = 0; i < n; i++) {
        if (isnan(x[i])) {
            return NAN;
        }
    }
    qsort(x, n, sizeof(double), dbl_cmp);
    if (n % 2 == 1) {
        return x[n / 2];
    }
    return 0.5 * (x[n / 2 - 1] + x[n / 2]);
}

const char *eval_scenario_name(EvalScenario scen) {
    switch (scen) {
        case EVAL_SCEN_NORMAL:     return "normal";
        case EVAL_SCEN_RAIN_EVENT: return "rain-event";
        case EVAL_SCEN_BREAKDOWN:  return "breakdown";
        default:                   return "unknown";
    }
}

const char *eval_policy_name(EvalPolicyId pol) {
    switch (pol) {
        case EVAL_POL_FIXED_EQUAL:            return "fixed_equal";
        case EVAL_POL_FIXED_PROPORTIONAL:     return "fixed_proportional";
        case EVAL_POL_GREEDY_SPEC_MA:         return "greedy_spec_ma";
        case EVAL_POL_GREEDY_MARGINAL_MA:     return "greedy_marginal_ma";
        case EVAL_POL_GREEDY_MARGINAL_RF:     return "greedy_marginal_rf";
        case EVAL_POL_GREEDY_MARGINAL_ORACLE: return "greedy_marginal_oracle";
        default:                              return "unknown";
    }
}

int eval_pick_event_stop(const NetworkModel *net) {
    if (!net) {
        return -1;
    }
    size_t n_stops = network_stop_count(net);
    if (n_stops == 0) {
        return -1;
    }

    int best_idx = -1;
    double best_scale = -1.0;
    for (size_t i = 0; i < n_stops; i++) {
        const DemandStop *ds = network_stop(net, i);
        if (ds && ds->type == STOP_HUB) {
            if (best_idx < 0 || ds->scale > best_scale) {
                best_idx = (int)i;
                best_scale = ds->scale;
            }
        }
    }
    return (best_idx >= 0) ? best_idx : 0;
}

int eval_graph_has_blocked_edge(const Graph *g) {
    if (!g) {
        return -1;
    }
    size_t n_edges = graph_edge_count(g);
    for (size_t e = 0; e < n_edges; e++) {
        const Edge *edge = graph_edge(g, (int)e);
        if (edge && edge->blocked != 0) {
            return 1;
        }
    }
    return 0;
}

Status eval_compute_prop_weights(const NetworkModel *net, const DemandPredictor *dp_ma, double *out_weights) {
    if (!net || !dp_ma || !out_weights) {
        return STATUS_ERR_NULL;
    }
    size_t n_routes = network_route_count(net);
    if (n_routes == 0) {
        return STATUS_ERR_INVALID;
    }

    double *tmp = (double *)calloc(n_routes, sizeof(double));
    if (!tmp) {
        return STATUS_ERR_NOMEM;
    }
    for (size_t r = 0; r < n_routes; r++) {
        out_weights[r] = 0.0;
    }

    for (int h = 0; h < 24; h++) {
        Status st = network_route_demands(net, dp_ma, h, DAY_WEEKDAY, 0, NULL, 0, tmp);
        if (st != STATUS_OK) {
            free(tmp);
            return st;
        }
        for (size_t r = 0; r < n_routes; r++) {
            out_weights[r] += tmp[r];
        }
    }
    for (size_t r = 0; r < n_routes; r++) {
        out_weights[r] /= 24.0;
    }

    free(tmp);
    return STATUS_OK;
}

Status eval_run_single(NetworkModel *net, EvalScenario scen, EvalPolicyId pol, uint64_t seed,
                       const DemandPredictor *dp_ma, const DemandPredictor *dp_rf,
                       const double *prop_weights, int n_buses,
                       double realloc_interval_min, double realloc_hysteresis,
                       int bus_capacity, int apply_traffic, SimStats *out_stats) {
    if (!net || !out_stats) {
        return STATUS_ERR_NULL;
    }
    if ((int)scen < 0 || scen >= EVAL_SCEN_COUNT || (int)pol < 0 || pol >= EVAL_POL_COUNT) {
        return STATUS_ERR_INVALID;
    }

    Graph *g = network_graph(net);
    if (!g || eval_graph_has_blocked_edge(g) != 0) {
        return STATUS_ERR_INVALID;
    }
    graph_reset_traffic(g);

    if ((pol == EVAL_POL_FIXED_PROPORTIONAL && !prop_weights && !dp_ma) ||
        (pol == EVAL_POL_GREEDY_SPEC_MA && !dp_ma) ||
        (pol == EVAL_POL_GREEDY_MARGINAL_MA && !dp_ma) ||
        (pol == EVAL_POL_GREEDY_MARGINAL_RF && !dp_rf)) {
        return STATUS_ERR_NULL;
    }

    SimConfig scfg;
    sim_config_default(&scfg);
    scfg.seed = seed;
    scfg.day = 0;
    scfg.day_type = DAY_WEEKDAY;
    scfg.rain = 0;
    scfg.start_min = 360.0;
    scfg.end_min = 1320.0;
    scfg.warmup_min = 30.0;
    scfg.slice_min = 15.0;
    scfg.arrivals_enabled = 1;
    scfg.dwell_s = 20.0;
    scfg.per_pax_s = 1.0;
    scfg.bus_capacity = bus_capacity;
    scfg.travel_noise = 0.10;
    scfg.n_buses = n_buses;
    scfg.n_reserve = (scen == EVAL_SCEN_RAIN_EVENT) ? 3 : 0;
    scfg.depot_stop = 0;
    scfg.initial_spacing = SIM_SPACING_BY_TIME;
    scfg.reassign_mode = SIM_REASSIGN_DRAIN;
    scfg.realloc_interval_min = realloc_interval_min;
    scfg.realloc_hysteresis = realloc_hysteresis;
    scfg.apply_traffic = apply_traffic;

    if (scen == EVAL_SCEN_RAIN_EVENT) {
        scfg.anomaly_detect = 1;
        scfg.anomaly_dispatch = 1;
        scfg.anomaly = anomaly_config_default();
    }

    double *owned_pw = NULL;
    switch (pol) {
        case EVAL_POL_FIXED_EQUAL:
            scfg.policy = SIM_POLICY_FIXED_EQUAL;
            scfg.predictor = NULL;
            break;
        case EVAL_POL_FIXED_PROPORTIONAL:
            scfg.policy = SIM_POLICY_FIXED_PROPORTIONAL;
            scfg.predictor = NULL;
            if (prop_weights) {
                scfg.prop_weights = prop_weights;
            } else {
                size_t n_routes = network_route_count(net);
                owned_pw = (double *)calloc(n_routes, sizeof(double));
                if (!owned_pw) {
                    return STATUS_ERR_NOMEM;
                }
                Status wst = eval_compute_prop_weights(net, dp_ma, owned_pw);
                if (wst != STATUS_OK) {
                    free(owned_pw);
                    return wst;
                }
                scfg.prop_weights = owned_pw;
            }
            break;
        case EVAL_POL_GREEDY_SPEC_MA:
            scfg.policy = SIM_POLICY_GREEDY_SPEC;
            scfg.predictor = dp_ma;
            break;
        case EVAL_POL_GREEDY_MARGINAL_MA:
            scfg.policy = SIM_POLICY_GREEDY_MARGINAL;
            scfg.predictor = dp_ma;
            break;
        case EVAL_POL_GREEDY_MARGINAL_RF:
            scfg.policy = SIM_POLICY_GREEDY_MARGINAL;
            scfg.predictor = dp_rf;
            break;
        case EVAL_POL_GREEDY_MARGINAL_ORACLE:
            scfg.policy = SIM_POLICY_GREEDY_MARGINAL;
            scfg.predictor = NULL;
            break;
        default:
            free(owned_pw);
            return STATUS_ERR_INVALID;
    }

    Sim *sim = NULL;
    Status st = sim_create(net, &scfg, &sim);
    free(owned_pw);
    if (st != STATUS_OK || !sim) {
        return (st != STATUS_OK) ? st : STATUS_ERR_INVALID;
    }

    if (scen == EVAL_SCEN_RAIN_EVENT) {
        int ev_stop = eval_pick_event_stop(net);
        SimScenario sc1 = {480.0, SIM_SCEN_RAIN_ON, 0};
        SimScenario sc2 = {1020.0, SIM_SCEN_EVENT_ON, ev_stop};
        SimScenario sc3 = {1140.0, SIM_SCEN_EVENT_OFF, ev_stop};
        if (sim_schedule(sim, &sc1) != STATUS_OK ||
            sim_schedule(sim, &sc2) != STATUS_OK ||
            sim_schedule(sim, &sc3) != STATUS_OK) {
            sim_destroy(sim);
            return STATUS_ERR_INVALID;
        }
    } else if (scen == EVAL_SCEN_BREAKDOWN) {
        SimScenario sc1 = {540.0, SIM_SCEN_BREAKDOWN, 0};
        SimScenario sc2 = {720.0, SIM_SCEN_REPAIR, 0};
        if (sim_schedule(sim, &sc1) != STATUS_OK ||
            sim_schedule(sim, &sc2) != STATUS_OK) {
            sim_destroy(sim);
            return STATUS_ERR_INVALID;
        }
    }

    st = sim_run_to_end(sim);
    if (st == STATUS_OK) {
        st = sim_stats(sim, out_stats);
    }
    sim_destroy(sim);
    return st;
}

double eval_anomaly_expected_flags(const NetworkModel *net, double start_min, double end_min,
                                   double slice_min, DayType day_type, int rain,
                                   double z_threshold, double variance_floor, int min_observed) {
    if (!net || slice_min <= 0.0 || end_min <= start_min || variance_floor <= 0.0 ||
        isnan(z_threshold) || isnan(variance_floor) || min_observed < 0) {
        return NAN;
    }
    size_t n_stops = network_stop_count(net);
    double total = 0.0;

    for (double t_min = start_min + slice_min; t_min < end_min - 1e-6; t_min += slice_min) {
        double finished_slice_min = t_min - slice_min;
        int hour = (int)floor(finished_slice_min / 60.0);
        if (hour < 0 || hour > 23) {
            continue;
        }
        for (size_t i = 0; i < n_stops; i++) {
            const DemandStop *ds = network_stop(net, i);
            StopType type = ds ? ds->type : STOP_RESIDENTIAL;
            double scale = ds ? ds->scale : 1.0;
            double rate = demand_expected(type, scale, hour, day_type, rain, 0);
            if (rate < 0.0) {
                return NAN;
            }
            double lambda_w = rate * slice_min / 60.0;

            int k = min_observed;
            while (1) {
                double z = anomaly_zscore(k, lambda_w, variance_floor);
                if (!isnan(z) && z > z_threshold) {
                    break;
                }
                k++;
            }
            total += eval_poisson_tail_ge(lambda_w, k);
        }
    }
    return total;
}

