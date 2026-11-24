#ifndef NETWORK_H
#define NETWORK_H

/**
 * @file network.h
 * @brief Bus network model: stop partitioning into routes, tour loops,
 *        RouteManager integration, and static travel-time matrix.
 *
 * NOTE: Functions in this module are NOT thread-safe without external synchronization.
 */

#include <stddef.h>
#include "common.h"
#include "types.h"
#include "graph.h"
#include "pathfind.h"
#include "reroute.h"
#include "demand_model.h"
#include "ai_demand.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_DEFAULT_DWELL_S 20.0

typedef struct NetworkModel NetworkModel;
typedef struct NetworkModel Network;

#define network_travel_time_s network_stop_time_s

typedef struct NetworkConfig {
    size_t n_routes;
    double dwell_s_per_stop;
    int build_matrix;
} NetworkConfig;

/**
 * Initializes default network configuration:
 * n_routes = 6, dwell_s_per_stop = 20.0, build_matrix = 1.
 */
void network_config_default(NetworkConfig *c);

/**
 * Builds a bus network model from stops and graph.
 *
 * Side effect: calls graph_reset_traffic(g) (legs are planned in free flow;
 * blocked flags are not touched).
 *
 * @param g Graph pointer (NOT owned, must outlive model).
 * @param stops Array of DemandStop (copied).
 * @param n_stops Number of stops (>= 2).
 * @param cfg Configuration pointer (NULL uses defaults).
 * @param out Receives newly allocated NetworkModel.
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL pointer,
 *         STATUS_ERR_INVALID on invalid parameters or unreachable leg/pair,
 *         STATUS_ERR_NOMEM on memory allocation failure.
 */
Status network_build(Graph *g, const DemandStop *stops, size_t n_stops, const NetworkConfig *cfg, NetworkModel **out);

/**
 * Destroys the NetworkModel and its internal RouteManager.
 * Does NOT destroy the underlying Graph. NULL-safe.
 */
void network_destroy(NetworkModel *n);

/* Accessors */
Graph *network_graph(const NetworkModel *n);
RouteManager *network_route_manager(const NetworkModel *n);
size_t network_route_count(const NetworkModel *n);
size_t network_stop_count(const NetworkModel *n);
const DemandStop *network_stop(const NetworkModel *n, size_t stop_idx);
size_t network_route_stop_count(const NetworkModel *n, size_t r);
int network_route_stop(const NetworkModel *n, size_t r, size_t k);
int network_stop_route(const NetworkModel *n, size_t stop_idx);
int network_stop_pos(const NetworkModel *n, size_t stop_idx);
const char *network_route_name(const NetworkModel *n, size_t r);
int network_leg_route_id(const NetworkModel *n, size_t r, size_t k);
const PathResult *network_leg_path(const NetworkModel *n, size_t r, size_t k);
double network_leg_time_s(const NetworkModel *n, size_t r, size_t k, CostMode mode);
double network_route_loop_time_s(const NetworkModel *n, size_t r, CostMode mode);
double network_route_cycle_s(const NetworkModel *n, size_t r);
Status network_route_cycles(const NetworkModel *n, double *out);
double network_stop_time_s(const NetworkModel *n, size_t a, size_t b);
double network_entry_time_s(const NetworkModel *n, size_t from_stop, size_t to_route, int *entry_stop);
Status network_route_demands(const NetworkModel *n, const DemandPredictor *dp, int hour, DayType day_type, int rain, const int *event_stops, size_t n_event_stops, double *out);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H */
