#ifndef FLEET_ALLOC_H
#define FLEET_ALLOC_H

#include <stddef.h>
#include "common.h"
#include "network.h"

#define FLEET_NO_SERVICE_WAIT_S 3600.0

typedef enum {
    FLEET_PRIO_DEMAND_PER_BUS = 0,
    FLEET_PRIO_MARGINAL_WAIT = 1
} FleetPriority;

typedef struct {
    int bus_id;
    int route;      /* -1 if unassigned / depot */
    int stop_idx;   /* current stop (node index) */
    int load;       /* current passengers on board */
} FleetBus;

typedef struct {
    int bus_id;
    int from_route;
    int to_route;
    int entry_stop; /* node index where bus joins new route (-1 if going to depot/unassigned) */
    double deadhead_s;
} FleetMove;

/**
 * Distributes buses equally across routes: floor(n/R) each, with the remainder
 * (n % R) distributed +1 to route indices 0, 1, ..., (n % R) - 1.
 */
Status fleet_targets_equal(size_t n_routes, size_t n_buses, int *out);

/**
 * Distributes buses proportionally using Hamilton's largest-remainder method.
 * Each route receives at least min_per_route buses. Remaining buses are distributed
 * according to weights[r]. Leftover fractional buses go to routes with largest
 * fractional parts, ties broken by smaller route index.
 * If all weights are 0, remaining buses are distributed equally.
 */
Status fleet_targets_proportional(const double *weights, size_t n_routes, size_t n_buses,
                                  int min_per_route, int *out);

/**
 * Distributes buses greedily using a Part 1 Heap.
 * Each route starts with min_per_route buses.
 * Routes are prioritised by:
 *   - FLEET_PRIO_DEMAND_PER_BUS: demand[r] / (count + 1)
 *   - FLEET_PRIO_MARGINAL_WAIT:  (demand[r] * cycle_s[r]) / (count * (count + 1))
 * Ties are broken by higher priority, then smaller bus count, then smaller route ID.
 * MARGINAL_WAIT requires min_per_route >= 1 and cycle_s != NULL.
 */
Status fleet_targets_greedy(const double *demand, const double *cycle_s,
                            size_t n_routes, size_t n_buses, int min_per_route,
                            FleetPriority prio, int *out);

/**
 * Computes demand-weighted average waiting time proxy across all routes.
 * If counts[r] > 0: wait_r = cycle_s[r] / (2 * counts[r]).
 * If counts[r] == 0 and demand[r] > 0: wait_r = FLEET_NO_SERVICE_WAIT_S (3600.0).
 * If counts[r] == 0 and demand[r] == 0: wait_r = 0.0.
 * Returns NAN if inputs are invalid/NULL. Returns 0.0 if total demand is 0.
 */
double fleet_wait_proxy_s(const double *demand, const double *cycle_s,
                          const int *counts, size_t n_routes);

/**
 * Finds the candidate bus closest to entering to_route based on network entry travel time.
 * Ties broken by smaller bus_id.
 */
Status fleet_pick_nearest(const Network *net, const FleetBus *cands, size_t n,
                          size_t to_route, size_t *index_out, double *deadhead_s,
                          int *entry_stop);

/**
 * Plans moves to transition current fleet distribution to target distribution.
 * Surplus routes release lowest-load buses (ties broken by bus_id).
 * Movers (released + unassigned buses) are matched greedily to deficit routes
 * by minimum deadhead travel time (ties: smaller bus_id, then smaller route ID).
 * Unmatched released buses move to depot (route = -1, entry_stop = -1, deadhead = 0.0).
 * Output moves are sorted by bus_id ascending.
 */
Status fleet_plan_moves(const Network *net, const FleetBus *buses, size_t n_buses,
                        const int *targets, size_t n_routes,
                        FleetMove *moves_out, size_t max_moves, size_t *n_moves);

#endif /* FLEET_ALLOC_H */
