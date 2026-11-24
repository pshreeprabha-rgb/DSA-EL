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

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_SIM_H */
