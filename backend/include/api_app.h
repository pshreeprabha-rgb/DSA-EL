#ifndef TRANSPORT_API_APP_H
#define TRANSPORT_API_APP_H

/**
 * @file api_app.h
 * @brief Application context, lifecycle management, and simulation background thread.
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
} AppOptions;

/**
 * @brief Populates AppOptions with server defaults (--port 8080, --seed 42, etc.).
 * @param opt Destination AppOptions pointer (NULL-safe).
 * @note Complexity: O(1).
 */
void app_options_default(AppOptions *opt);

/**
 * @brief Builds a full AppCtx from cached OSM graph, optional predictors, network, sim, and registry.
 * @param opt Configuration options (NULL uses defaults).
 * @param out Output pointer receiving allocated AppCtx (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL/INVALID/NOT_FOUND/NOMEM on error.
 * @note Ownership: Caller owns *out and must destroy with app_destroy(). Complexity: O(S^2 log V + B).
 */
Status app_create(const AppOptions *opt, AppCtx **out);

/**
 * @brief Builds an AppCtx from caller-supplied graph, network, stops, and SimConfig (for unit tests).
 *
 * @param g Graph pointer (owned by AppCtx on STATUS_OK; borrowed/untouched on error).
 * @param net NetworkModel pointer with matrix built (owned by AppCtx on STATUS_OK; untouched on error).
 * @param stops Array of n DemandStops (copied internally; caller keeps ownership).
 * @param n Number of stops (>= 2).
 * @param cfg SimConfig (NULL uses server defaults).
 * @param out Output pointer receiving allocated AppCtx (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL/INVALID/NOMEM on error.
 * @note Ownership: Takes ownership of g and net ONLY when returning STATUS_OK. Complexity: O(S + B + T log T).
 */
Status app_create_from_parts(Graph *g, NetworkModel *net, const DemandStop *stops, size_t n,
                             const SimConfig *cfg, AppCtx **out);

/**
 * @brief Stops any running simulation thread and frees all resources owned by AppCtx (NULL-safe).
 * @param ctx Application context.
 * @note Complexity: O(n).
 */
void app_destroy(AppCtx *ctx);

/**
 * @brief Acquires the application mutex protecting sim, network, graph, and registry.
 * @param ctx Application context (non-NULL).
 * @return STATUS_OK on success, or error code.
 * @note Complexity: O(1).
 */
Status app_lock(AppCtx *ctx);

/**
 * @brief Releases the application mutex.
 * @param ctx Application context (non-NULL).
 * @return STATUS_OK on success, or error code.
 * @note Complexity: O(1).
 */
Status app_unlock(AppCtx *ctx);

/**
 * @brief Starts the background simulation loop thread (idempotent if already started).
 * @param ctx Application context (non-NULL).
 * @return STATUS_OK on success, or error code.
 * @note Complexity: O(1).
 */
Status app_start_thread(AppCtx *ctx);

/**
 * @brief Signals the simulation loop thread to stop and joins it (idempotent).
 * @param ctx Application context (NULL-safe).
 * @return STATUS_OK on success, or error code.
 * @note Complexity: O(1) wait up to one sleep interval.
 */
Status app_stop_thread(AppCtx *ctx);

/**
 * @brief Applies a simulation control action ("start", "pause", "resume", "speed") and/or speed update.
 *        Caller should hold app_lock(ctx) or call before starting HTTP workers.
 *
 * @param ctx Application context (non-NULL).
 * @param action Optional action string ("start", "pause", "resume", "speed"), or NULL if only speed set.
 * @param has_speed Non-zero if speed argument is provided.
 * @param speed New speed in simulated seconds per wall second (must be in [1, 3600] if has_speed).
 * @return STATUS_OK on success, STATUS_ERR_NULL/INVALID on bad arguments.
 * @note Complexity: O(1).
 */
Status app_sim_control(AppCtx *ctx, const char *action, int has_speed, double speed);

/**
 * @brief Increments geometry_version and generation counters (caller holds lock).
 * @param ctx Application context (NULL-safe).
 * @note Complexity: O(1).
 */
void app_bump_geometry_version(AppCtx *ctx);

/* Read/lock-protected accessors */
const AppOptions *app_options(const AppCtx *ctx);
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
int app_is_running(const AppCtx *ctx);
double app_speed(const AppCtx *ctx);
uint64_t app_geometry_version(const AppCtx *ctx);
uint64_t app_generation(const AppCtx *ctx);
const char *app_cors_origin(const AppCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_API_APP_H */

