/**
 * @file sim.h
 * @brief Discrete-event simulation engine for the Bangalore public transit network.
 *
 * NOTE: Not thread-safe. Synchronization is the responsibility of callers.
 */

#ifndef TRANSPORT_SIM_H
#define TRANSPORT_SIM_H

#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "network.h"
#include "demand_model.h"
#include "ai_demand.h"
#include "anomaly.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sim Sim;

typedef enum SimPolicy {
    SIM_POLICY_FIXED_EQUAL = 0,
    SIM_POLICY_FIXED_PROPORTIONAL,
    SIM_POLICY_GREEDY_SPEC,
    SIM_POLICY_GREEDY_MARGINAL
} SimPolicy;

typedef enum SimBusState {
    SIM_BUS_RESERVE = 0,
    SIM_BUS_RUNNING,
    SIM_BUS_DEADHEAD,
    SIM_BUS_BROKEN
} SimBusState;

typedef enum SimScenarioKind {
    SIM_SCEN_RAIN_ON = 0,
    SIM_SCEN_RAIN_OFF,
    SIM_SCEN_EVENT_ON,
    SIM_SCEN_EVENT_OFF,
    SIM_SCEN_BLOCK_EDGE,
    SIM_SCEN_UNBLOCK_EDGE,
    SIM_SCEN_BREAKDOWN,
    SIM_SCEN_REPAIR
} SimScenarioKind;

typedef struct SimScenario {
    double time_min;
    SimScenarioKind kind;
    int arg;
} SimScenario;

#define SIM_SPACING_BY_STOP 0
#define SIM_SPACING_BY_TIME 1

#define SIM_REASSIGN_DUMP 0
#define SIM_REASSIGN_DRAIN 1

typedef struct SimConfig {
    uint64_t seed;
    int day;
    DayType day_type;
    int rain;
    double start_min;
    double end_min;
    double warmup_min;
    double slice_min;
    int arrivals_enabled;
    double dwell_s;
    double per_pax_s;
    int bus_capacity;
    double travel_noise;
    int n_buses;
    int n_reserve;
    int depot_stop;
    SimPolicy policy;
    const double *prop_weights;
    const DemandPredictor *predictor;
    double realloc_interval_min;
    double realloc_hysteresis;
    int apply_traffic;
    int anomaly_detect;
    int anomaly_dispatch;
    AnomalyConfig anomaly;
    int initial_spacing;
    int reassign_mode;
} SimConfig;

typedef struct SimBusInfo {
    int id;
    SimBusState state;
    int route;
    int stop_idx;
    int load;
    int pending_route;
} SimBusInfo;

typedef struct SimStats {
    size_t arrived;
    size_t boarded;
    size_t still_waiting;
    double wait_sum_s;
    double wait_max_s;
    double mean_wait_boarded_s;
    double mean_wait_incl_censored_s;
    double p50_s;
    double p95_s;
    size_t full_bus_events;
    size_t forced_dumps;
    size_t stranded_by_breakdown;
    size_t bus_arrivals;
    size_t moves_applied;
    double deadhead_s_total;
    size_t anomaly_flags;
    size_t emergency_dispatches;
    double stalled_s;
    double mean_load_factor;
    size_t decisions;
    size_t decisions_applied;
} SimStats;

typedef struct SimRouteStats {
    size_t boarded;
    size_t still_waiting;
    double wait_sum_s;
    double wait_max_s;
    double mean_wait_s;
} SimRouteStats;

void sim_config_default(SimConfig *c);
Status sim_create(NetworkModel *net, const SimConfig *cfg, Sim **out);
void sim_destroy(Sim *s);
Status sim_schedule(Sim *s, const SimScenario *sc);
Status sim_inject_arrivals(Sim *s, size_t stop, double time_s, int count, int ride_stops);
Status sim_run_until(Sim *s, double t_min);
Status sim_run_to_end(Sim *s);
double sim_now_min(const Sim *s);
int sim_finished(const Sim *s);
size_t sim_bus_count(const Sim *s);
Status sim_bus_info(const Sim *s, size_t bus, SimBusInfo *out);
size_t sim_route_bus_count(const Sim *s, size_t r);
long sim_queue_length(Sim *s, size_t stop);
Status sim_stats(Sim *s, SimStats *out);
Status sim_route_stats(Sim *s, size_t r, SimRouteStats *out);
size_t sim_flag_count(const Sim *s);
Status sim_flag(const Sim *s, size_t i, AnomalyFlag *out);
double sim_first_flag_min(const Sim *s, size_t stop);
double sim_event_start_min(const Sim *s, size_t stop);
size_t sim_log_count(const Sim *s);
const char *sim_log_line(const Sim *s, size_t i);

/**
 * @brief Instantaneous motion state of a simulated bus.
 */
typedef struct SimBusMotion {
    int moving;
    int route;
    int leg_index;
    int from_stop;
    int to_stop;
    double depart_s;
    double arrive_s;
} SimBusMotion;

/**
 * @brief Loop completion callback invoked when a RUNNING bus completes m legs.
 *
 * Runs synchronously inside sim_run_until() on the caller's thread; the caller's
 * external synchronization/lock covers the callback invocation.
 *
 * @param ctx Caller context pointer.
 * @param bus Bus index/ID.
 * @param route Route index.
 * @param start_s Loop start timestamp (seconds).
 * @param end_s Loop completion timestamp (seconds).
 * @param pax_boarded Total passengers boarded during the completed loop.
 */
typedef void (*SimLoopFn)(void *ctx, int bus, int route, double start_s, double end_s, int pax_boarded);

/**
 * @brief Requests an administrative reassignment of a RUNNING bus to another route or reserve.
 *
 * @param s Simulation handle (non-NULL).
 * @param bus_id Bus index in [0, n_buses); must be RUNNING with no pending move.
 * @param to_route Target route index in [0, R), or -1 for reserve; must differ from current route.
 *                 Non-negative targets require a precomputed distance matrix.
 * @return STATUS_OK on success, STATUS_ERR_NULL if s is NULL, STATUS_ERR_INVALID otherwise.
 * @note Ownership: none. Complexity: O(1).
 */
Status sim_request_move(Sim *s, int bus_id, int to_route);

/**
 * @brief Updates the active fleet allocation policy and borrowed demand predictor.
 *
 * Sets the last decision time to now (no immediate decision). GREEDY_* policies
 * require a precomputed distance matrix; FIXED_* policies stop the controller.
 *
 * @param s Simulation handle (non-NULL).
 * @param policy New fleet policy.
 * @param predictor Borrowed DemandPredictor pointer (may be NULL for oracle).
 * @return STATUS_OK on success, STATUS_ERR_NULL if s is NULL, STATUS_ERR_INVALID on invalid policy/missing matrix.
 * @note Ownership: borrows predictor (caller keeps ownership). Complexity: O(1).
 */
Status sim_set_policy(Sim *s, SimPolicy policy, const DemandPredictor *predictor);

/**
 * @brief Forces a single reallocation decision immediately via the controller path.
 *
 * Requires an active GREEDY_* policy. Updates last decision time to now.
 *
 * @param s Simulation handle (non-NULL).
 * @param moves_applied Optional output receiving the number of bus moves planned/set pending (may be NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL if s is NULL, STATUS_ERR_INVALID if not a GREEDY_* policy.
 * @note Ownership: none. Complexity: O(B + R log R + B^2).
 */
Status sim_force_decision(Sim *s, size_t *moves_applied);

/**
 * @brief Queries whether rain is currently active in the simulation.
 * @param s Simulation handle.
 * @return 1 if rain is active, 0 otherwise (or if s is NULL).
 * @note Complexity: O(1).
 */
int sim_rain_active(const Sim *s);

/**
 * @brief Queries whether an event surge is currently active at a given stop.
 * @param s Simulation handle.
 * @param stop Stop index in [0, n_stops).
 * @return 1 if event is active at stop, 0 otherwise (or on invalid args).
 * @note Complexity: O(1).
 */
int sim_event_active(const Sim *s, size_t stop);

/**
 * @brief Returns the active fleet policy of the simulation.
 * @param s Simulation handle.
 * @return Active SimPolicy (returns SIM_POLICY_FIXED_EQUAL if s is NULL).
 * @note Complexity: O(1).
 */
SimPolicy sim_policy(const Sim *s);

/**
 * @brief Returns total passengers generated (random plus injected) for a stop and hour since simulation start.
 * @param s Simulation handle.
 * @param stop Stop index in [0, n_stops).
 * @param hour Hour of day in [0, 23].
 * @return Passenger count, or 0 on invalid arguments.
 * @note Complexity: O(1).
 */
size_t sim_stop_hour_arrivals(const Sim *s, size_t stop, int hour);

/**
 * @brief Retrieves instantaneous motion state for a bus.
 * @param s Simulation handle (non-NULL).
 * @param bus Bus index in [0, n_buses).
 * @param out Output SimBusMotion pointer (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg, STATUS_ERR_INVALID on bad bus index.
 * @note Ownership: writes caller-owned struct. Complexity: O(1).
 */
Status sim_bus_motion(const Sim *s, size_t bus, SimBusMotion *out);

/**
 * @brief Registers or clears the per-bus loop completion observer callback.
 *
 * The callback runs inside sim_run_until() on the caller's thread; the caller's
 * lock covers callback execution. Passing fn == NULL removes the observer.
 *
 * @param s Simulation handle (non-NULL).
 * @param fn Callback function pointer, or NULL to clear.
 * @param ctx Opaque caller context pointer passed to fn.
 * @return STATUS_OK on success, STATUS_ERR_NULL if s is NULL.
 * @note Ownership: caller owns ctx. Complexity: O(1).
 */
Status sim_set_loop_observer(Sim *s, SimLoopFn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_SIM_H */
