#ifndef TRANSPORT_API_APP_H
#define TRANSPORT_API_APP_H

/**
 * @file api_app.h
 * @brief Application context, lifecycle management, simulation thread, and traffic poller thread.
 */

#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "graph.h"
#include "demand_model.h"
#include "ai_demand.h"
#include "network.h"
#include "sim.h"
#include "registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AppCtx AppCtx;

typedef struct AppOptions {
    int port;
    const char *bind_addr;
    const char *data_dir;
    uint64_t seed;
    size_t n_stops;
    size_t n_routes;
    size_t n_buses;
    size_t n_reserve;
    SimPolicy policy;
    const char *predictor;   /* "oracle" | "ma" | "rf" */
    double speed;            /* simulated seconds per wall second (1..3600) */
    int paused;              /* 1 = start paused, 0 = start running */
    const char *cors_origin; /* CORS Access-Control-Allow-Origin */
    int offline;             /* 1 = disable live TomTom API calls */
    int no_poll;             /* 1 = disable live polling */
} AppOptions;

void app_options_default(AppOptions *opt);
Status app_create(const AppOptions *opt, AppCtx **out);
Status app_create_from_parts(Graph *g, NetworkModel *net, const DemandStop *stops, size_t n,
                             const SimConfig *cfg, AppCtx **out);
void app_destroy(AppCtx *ctx);

Status app_lock(AppCtx *ctx);
Status app_unlock(AppCtx *ctx);
Status app_start_thread(AppCtx *ctx);
Status app_stop_thread(AppCtx *ctx);

Status app_sim_control(AppCtx *ctx, const char *action, int has_speed, double speed);
Status app_sim_control_ext(AppCtx *ctx, const char *action, int has_speed, double speed,
                           int has_time_min, double time_min);

/**
 * @brief Synchronizes Registry state and checks RouteManager history size to bump geometry_version.
 *        Caller holds app_lock(ctx).
 * @param ctx Application context (NULL-safe).
 * @note Complexity: O(B * children).
 */
void app_sync_after_step(AppCtx *ctx);

/**
 * @brief Schedules a scenario at the next slice boundary (caller holds app_lock).
 * @return STATUS_OK, STATUS_ERR_NULL, STATUS_ERR_INVALID, or STATUS_ERR_EMPTY (past end_min).
 * @note Complexity: O(log E).
 */
Status app_schedule_scenario(AppCtx *ctx, SimScenarioKind kind, int arg, double *time_min_out);

/**
 * @brief Updates allocation policy/predictor and optionally forces a reallocation decision (caller holds lock).
 * @return STATUS_OK on success, STATUS_ERR_INVALID if unknown/unloaded predictor or invalid force.
 * @note Complexity: O(B + R log R + B^2).
 */
Status app_set_allocation(AppCtx *ctx, SimPolicy policy, const char *pred_name,
                          int force, size_t *moves_applied_out);

/**
 * @brief Requests a manual bus reassignment (caller holds lock).
 * @return STATUS_OK on success, STATUS_ERR_INVALID if request cannot be queued.
 * @note Complexity: O(B).
 */
Status app_manual_move(AppCtx *ctx, int bus_id, int to_route);

/**
 * @brief Injects passenger arrivals at current sim time (caller holds lock).
 * @return STATUS_OK on success, STATUS_ERR_INVALID on invalid stop/count/ride.
 * @note Complexity: O(count).
 */
Status app_inject(AppCtx *ctx, size_t stop_idx, int count, int ride_stops, double *time_s_out);

/**
 * @brief Undoes the most recent RouteManager ChangeSet (caller holds lock).
 * @return STATUS_OK on success, STATUS_ERR_EMPTY when history is empty.
 * @note Complexity: O(changed_routes + changed_edges).
 */
Status app_undo(AppCtx *ctx, char *desc_buf, size_t desc_cap, size_t *history_size_out);

/**
 * @brief Stores a copy of the latest /api/route/optimize response JSON (caller holds lock).
 * @note Complexity: O(strlen(json_str)).
 */
Status app_set_last_optimize(AppCtx *ctx, const char *json_str);

/**
 * @brief Overrides the eval summary file path for unit tests (caller owns setup, not exposed over HTTP).
 * @note Complexity: O(strlen(path)).
 */
Status app_set_eval_path_for_test(AppCtx *ctx, const char *path);

void app_bump_geometry_version(AppCtx *ctx);

/* Lock-protected accessors */
const AppOptions *app_options(const AppCtx *ctx);
Graph *app_graph_mut(AppCtx *ctx);
const Graph *app_graph(const AppCtx *ctx);
const NetworkModel *app_network(const AppCtx *ctx);
const DemandStop *app_stops(const AppCtx *ctx, size_t *n_out);
Sim *app_sim(AppCtx *ctx);
const Sim *app_sim_const(const AppCtx *ctx);
Registry *app_registry(AppCtx *ctx);
const Registry *app_registry_const(const AppCtx *ctx);
const SimConfig *app_sim_config(const AppCtx *ctx);
const DemandPredictor *app_predictor_ma(const AppCtx *ctx);
const DemandPredictor *app_predictor_rf(const AppCtx *ctx);
const DemandPredictor *app_active_predictor(const AppCtx *ctx);
const char *app_active_predictor_name(const AppCtx *ctx);
const char *app_ml_metrics_json(const AppCtx *ctx);
const char *app_last_optimize_json(const AppCtx *ctx);
const char *app_eval_path(const AppCtx *ctx);
const char *app_traffic_source_str(const AppCtx *ctx);
int app_traffic_calls(const AppCtx *ctx);
size_t app_traffic_live_corridors(const AppCtx *ctx);
const char *app_traffic_last_error(const AppCtx *ctx);
int app_is_running(const AppCtx *ctx);
double app_speed(const AppCtx *ctx);
uint64_t app_geometry_version(const AppCtx *ctx);
uint64_t app_generation(const AppCtx *ctx);
const char *app_cors_origin(const AppCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_API_APP_H */
