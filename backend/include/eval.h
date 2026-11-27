#ifndef TRANSPORT_EVAL_H
#define TRANSPORT_EVAL_H

/**
 * @file eval.h
 * @brief Statistical evaluation, paired simulation runner, and anomaly validation helpers.
 *
 * NOTE: Not thread-safe without external synchronization.
 */

#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "graph.h"
#include "network.h"
#include "sim.h"
#include "ai_demand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Summary statistics and win/tie/loss counts for paired evaluation.
 */
typedef struct EvalStats {
    size_t n;
    double mean;
    double sd;
    double se;
    double ci_lo;
    double ci_hi;
    size_t wins;
    size_t ties;
    size_t losses;
} EvalStats;

/**
 * @brief Evaluation scenario identifiers.
 */
typedef enum EvalScenario {
    EVAL_SCEN_NORMAL = 0,
    EVAL_SCEN_RAIN_EVENT,
    EVAL_SCEN_BREAKDOWN,
    EVAL_SCEN_COUNT
} EvalScenario;

/**
 * @brief Evaluation policy identifiers.
 */
typedef enum EvalPolicyId {
    EVAL_POL_FIXED_EQUAL = 0,
    EVAL_POL_FIXED_PROPORTIONAL,
    EVAL_POL_GREEDY_SPEC_MA,
    EVAL_POL_GREEDY_MARGINAL_MA,
    EVAL_POL_GREEDY_MARGINAL_RF,
    EVAL_POL_GREEDY_MARGINAL_ORACLE,
    EVAL_POL_COUNT
} EvalPolicyId;

/**
 * @brief Returns the two-sided 95% Student t critical value (t_{0.975, df}).
 *
 * Uses the conservative next-lower tabulated df for values between entries.
 *
 * @param df Degrees of freedom (>= 1).
 * @return Critical value, or NAN if df == 0.
 * @note Complexity: O(1).
 */
double eval_t_crit_975(size_t df);

/**
 * @brief Computes sample mean and sample standard deviation (n - 1 denominator).
 *
 * @param x Input array of length n (non-NULL, all finite).
 * @param n Sample count (>= 2).
 * @param mean Output mean pointer (non-NULL).
 * @param sd Output sample standard deviation pointer (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg, STATUS_ERR_INVALID if n < 2 or non-finite.
 * @note Ownership: none. Complexity: O(n).
 */
Status eval_mean_sd(const double *x, size_t n, double *mean, double *sd);

/**
 * @brief Computes paired statistics on diff_i = other_i - base_i (lower is better).
 *
 * Wins count diff_i < -1e-9; losses count diff_i > 1e-9; ties otherwise.
 *
 * @param base Baseline measurements of length n (non-NULL, all finite).
 * @param other Comparison measurements of length n (non-NULL, all finite).
 * @param n Number of paired observations (>= 2).
 * @param out Output EvalStats pointer (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg, STATUS_ERR_INVALID if n < 2 or non-finite.
 * @note Ownership: none. Complexity: O(n).
 */
Status eval_paired_stats(const double *base, const double *other, size_t n, EvalStats *out);

/**
 * @brief Computes upper Poisson tail P(X >= k) for X ~ Poisson(mu).
 *
 * Uses 1 - sum_{j=0}^{k-1} exp(-mu) * mu^j / j!.
 *
 * @param mu Poisson mean parameter.
 * @param k Integer threshold.
 * @return Tail probability in [0, 1], or NAN if mu is NaN.
 * @note Complexity: O(k).
 */
double eval_poisson_tail_ge(double mu, int k);

/**
 * @brief Computes the median of an array by sorting it in place.
 *
 * @param x Array of n doubles (modified in place).
 * @param n Number of elements.
 * @return Median value, or NAN if x is NULL, n == 0, or any element is NaN.
 * @note Ownership: mutates caller array x. Complexity: O(n log n).
 */
double eval_median(double *x, size_t n);

/**
 * @brief Returns canonical string name of an EvalScenario.
 * @param scen Scenario enum.
 * @return Static string literal, or "unknown" if invalid.
 * @note Complexity: O(1).
 */
const char *eval_scenario_name(EvalScenario scen);

/**
 * @brief Returns canonical string name of an EvalPolicyId.
 * @param pol Policy enum.
 * @return Static string literal, or "unknown" if invalid.
 * @note Complexity: O(1).
 */
const char *eval_policy_name(EvalPolicyId pol);

/**
 * @brief Selects the event stop E for rain-event and anomaly-eval scenarios.
 *
 * Selects the STOP_HUB stop with the largest scale (ties broken by smaller index),
 * or stop 0 if no STOP_HUB exists in the network.
 *
 * @param net NetworkModel pointer (non-NULL).
 * @return Stop index in [0, n_stops), or -1 if net is NULL or empty.
 * @note Complexity: O(n_stops).
 */
int eval_pick_event_stop(const NetworkModel *net);

/**
 * @brief Checks whether any edge in the graph currently has its blocked flag set.
 *
 * @param g Graph pointer (non-NULL).
 * @return 1 if at least one edge is blocked, 0 if none, or -1 if g is NULL.
 * @note Complexity: O(E).
 */
int eval_graph_has_blocked_edge(const Graph *g);

/**
 * @brief Computes fixed_proportional route weights as the 24-hour mean of MA predictions
 *        for a dry weekday.
 *
 * @param net NetworkModel pointer (non-NULL).
 * @param dp_ma Moving-average DemandPredictor pointer (non-NULL).
 * @param out_weights Caller-allocated array of length network_route_count(net).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg.
 * @note Ownership: writes caller-owned out_weights. Complexity: O(24 * S).
 */
Status eval_compute_prop_weights(const NetworkModel *net, const DemandPredictor *dp_ma, double *out_weights);

/**
 * @brief Executes one simulation run for a given scenario, policy, and seed.
 *
 * Resets graph traffic factors and verifies no edge is blocked before creating the simulation.
 * Uses spacing BY_TIME, reassign DRAIN, weekday 06:00-22:00, warmup 30 min, slice 15 min,
 * travel noise 0.10, dwell 20 s, per_pax_s 1 s.
 *
 * @param net NetworkModel pointer (non-NULL).
 * @param scen Scenario identifier.
 * @param pol Policy identifier.
 * @param seed PRNG seed.
 * @param dp_ma Moving-average predictor (required for proportional/MA policies).
 * @param dp_rf Random forest predictor (required for EVAL_POL_GREEDY_MARGINAL_RF).
 * @param prop_weights Precomputed proportional weights (optional; computed if NULL for fixed_proportional).
 * @param n_buses Total fleet size (e.g. 36).
 * @param realloc_interval_min Reallocation interval in minutes (e.g. 30.0).
 * @param realloc_hysteresis Reallocation hysteresis threshold (e.g. 0.02).
 * @param bus_capacity Bus capacity in passengers (e.g. 60).
 * @param apply_traffic Whether diurnal traffic factors are applied (1 or 0).
 * @param out_stats Output SimStats pointer (non-NULL).
 * @return STATUS_OK on success, or error status on invalid input/state.
 * @note Ownership: none. Complexity: depends on simulation horizon.
 */
Status eval_run_single(NetworkModel *net, EvalScenario scen, EvalPolicyId pol, uint64_t seed,
                       const DemandPredictor *dp_ma, const DemandPredictor *dp_rf,
                       const double *prop_weights, int n_buses,
                       double realloc_interval_min, double realloc_hysteresis,
                       int bus_capacity, int apply_traffic, SimStats *out_stats);

/**
 * @brief Computes the analytic expected number of anomaly flags per dry weekday run
 *        across all stops and all closed windows.
 *
 * Matches sim.c window closure semantics: windows closed at slice boundaries strictly
 * between start_min and end_min (the last slice window is not closed).
 *
 * @param net NetworkModel pointer (non-NULL).
 * @param start_min Simulation start in minutes (e.g. 360.0).
 * @param end_min Simulation end in minutes (e.g. 1320.0).
 * @param slice_min Slice duration in minutes (e.g. 15.0).
 * @param day_type Day type (e.g. DAY_WEEKDAY).
 * @param rain Rain indicator (0 or 1).
 * @param z_threshold Anomaly z-score threshold (e.g. 3.0).
 * @param variance_floor Anomaly variance floor (e.g. 1.0).
 * @param min_observed Minimum observed arrivals required to flag (e.g. 5).
 * @return Analytic expected flag count, or NAN on invalid arguments.
 * @note Complexity: O(windows * stops * k_max).
 */
double eval_anomaly_expected_flags(const NetworkModel *net, double start_min, double end_min,
                                   double slice_min, DayType day_type, int rain,
                                   double z_threshold, double variance_floor, int min_observed);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_EVAL_H */

