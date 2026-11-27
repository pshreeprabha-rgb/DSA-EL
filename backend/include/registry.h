#ifndef TRANSPORT_REGISTRY_H
#define TRANSPORT_REGISTRY_H

/**
 * @file registry.h
 * @brief Entity registry backed by Part 1 Tree, HashTable, AVL, LinkedList, and Heap.
 *
 * The Registry does NOT own the NetworkModel or Sim; it owns its internal Tree,
 * HashTables, AVL timetable, and Bus, Route, and Stop records.
 *
 * NOTE: Not thread-safe without external synchronization. When registry_sim_observer
 * is attached to a Sim, it runs synchronously inside sim_run_until() under the
 * caller's lock (e.g., server mutex).
 */

#include <stddef.h>
#include "common.h"
#include "types.h"
#include "network.h"
#include "sim.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REG_KIND_CITY  0
#define REG_KIND_DEPOT 1
#define REG_KIND_ROUTE 2
#define REG_KIND_BUS   3

#define REGISTRY_MAX_JOURNEYS 200

typedef struct Registry Registry;

/**
 * @brief Depot specification for registry construction.
 */
typedef struct RegistryDepotSpec {
    const char *name;
    size_t stop_idx;
} RegistryDepotSpec;

/**
 * @brief Row of the route priority board ordered by descending priority, then route_id.
 */
typedef struct RouteBoardRow {
    int route_id;
    double demand;
    int buses;
    double priority;
} RouteBoardRow;

/**
 * @brief Flattened preorder row of the hierarchy tree.
 */
typedef struct RegTreeRow {
    int depth;
    int kind;
    int id;
    int parent_row;
    char name[64];
} RegTreeRow;

/**
 * @brief Creates a Registry mirroring the network topology and initial simulation fleet.
 *
 * @param net Borrowed NetworkModel pointer (non-NULL, must outlive registry).
 * @param depots Array of n_depots depot specifications (non-NULL, n_depots >= 1).
 * @param n_depots Number of depots.
 * @param sim Borrowed Sim pointer for initial bus placement (non-NULL).
 * @param out Output pointer receiving allocated Registry (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg, STATUS_ERR_INVALID on bad depot/config,
 *         STATUS_ERR_NOMEM on allocation failure.
 * @note Ownership: Caller owns *out and must destroy with registry_destroy(). Complexity: O(B + R*D + S).
 */
Status registry_create(const NetworkModel *net, const RegistryDepotSpec *depots, size_t n_depots,
                       const Sim *sim, Registry **out);

/**
 * @brief Destroys the Registry and all owned structures (NULL-safe).
 * @param r Registry handle.
 * @note Ownership: frees all owned Tree, HashTables, AVL, Bus/Route/Stop records. Complexity: O(n).
 */
void registry_destroy(Registry *r);

/**
 * @brief Synchronizes registry bus states, loads, coordinates, and tree hierarchy from sim.
 *
 * @param r Registry handle (non-NULL).
 * @param sim Sim handle (non-NULL, matching bus count).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg, STATUS_ERR_INVALID on mismatch,
 *         STATUS_ERR_NOMEM on allocation failure.
 * @note Complexity: O(B * children).
 */
Status registry_sync(Registry *r, const Sim *sim);

/**
 * @brief Hash-table lookup for a Bus by bus_id.
 * @param r Registry handle.
 * @param bus_id Bus identifier.
 * @return Borrowed const Bus pointer, or NULL if r is NULL or unknown.
 * @note Complexity: O(1) average.
 */
const Bus *registry_bus(const Registry *r, int bus_id);

/**
 * @brief Hash-table lookup for a Route by route_id.
 * @param r Registry handle.
 * @param route_id Route identifier.
 * @return Borrowed const Route pointer, or NULL if r is NULL or unknown.
 * @note Complexity: O(1) average.
 */
const Route *registry_route(const Registry *r, int route_id);

/**
 * @brief Hash-table lookup for a Stop by stop_idx.
 * @param r Registry handle.
 * @param stop_idx Stop index.
 * @return Borrowed const Stop pointer, or NULL if r is NULL or unknown.
 * @note Complexity: O(1) average.
 */
const Stop *registry_stop(const Registry *r, int stop_idx);

/**
 * @brief Returns number of registered buses (0 if r is NULL).
 * @note Complexity: O(1).
 */
size_t registry_bus_count(const Registry *r);

/**
 * @brief Returns total node count in the hierarchy tree (0 if r is NULL).
 * @note Complexity: O(1).
 */
size_t registry_tree_count(const Registry *r);

/**
 * @brief Returns height of the hierarchy tree (root alone = 0; -1 if r is NULL).
 * @note Complexity: O(n).
 */
int registry_tree_height(const Registry *r);

/**
 * @brief Exports the hierarchy tree in preorder traversal.
 *
 * @param r Registry handle (non-NULL).
 * @param out Output buffer of size at least max (non-NULL).
 * @param max Capacity of out.
 * @param n Receives number of rows written (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg.
 * @note Complexity: O(n).
 */
Status registry_tree_rows(const Registry *r, RegTreeRow *out, size_t max, size_t *n);

/**
 * @brief Rebuilds the AVL timetable from scratch for [from_min, to_min) and resets trip ID counter to 1.
 *
 * @param r Registry handle (non-NULL).
 * @param buses_per_route Array of length n_routes with bus counts >= 0 (non-NULL).
 * @param n_routes Must equal network_route_count.
 * @param from_min Start minute in [0, 1440).
 * @param to_min End minute in (from_min, 1440].
 * @return STATUS_OK on success, STATUS_ERR_NULL/INVALID/NOMEM on error.
 * @note Complexity: O(T log T).
 */
Status registry_build_timetable(Registry *r, const int *buses_per_route, size_t n_routes,
                                int from_min, int to_min);

/**
 * @brief Deletes trips with departure >= now_min and generates new trips for [now_min, to_min).
 *
 * Trip IDs continue incrementing from the existing counter.
 *
 * @param r Registry handle (non-NULL).
 * @param now_min Replan start minute in [0, 1440).
 * @param buses_per_route Array of length n_routes with bus counts >= 0 (non-NULL).
 * @param n_routes Must equal network_route_count.
 * @param to_min End minute in (now_min, 1440].
 * @return STATUS_OK on success, STATUS_ERR_NULL/INVALID/NOMEM on error.
 * @note Complexity: O(T log T).
 */
Status registry_replan_from(Registry *r, int now_min, const int *buses_per_route, size_t n_routes,
                            int to_min);

/**
 * @brief Finds the earliest trip on route_id with departure_time >= after_min.
 *
 * @param r Registry handle.
 * @param route_id Route index.
 * @param after_min Earliest departure minute.
 * @return Borrowed const Trip pointer, or NULL if none or invalid args.
 * @note Complexity: O(k log T) where k is trips checked at >= after_min.
 */
const Trip *registry_next_departure(const Registry *r, int route_id, int after_min);

/**
 * @brief Lists trips in [from_min, to_min) in ascending AVL order.
 *
 * @param r Registry handle (non-NULL).
 * @param from_min Start minute (inclusive).
 * @param to_min End minute (exclusive).
 * @param route_id Route filter, or -1 for all routes.
 * @param out Output Trip array (non-NULL).
 * @param max Capacity of out.
 * @param n Receives number of trips written (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL/INVALID on error.
 * @note Complexity: O(T).
 */
Status registry_trips(const Registry *r, int from_min, int to_min, int route_id,
                      Trip *out, size_t max, size_t *n);

/**
 * @brief Returns total number of trips in the AVL timetable (0 if r is NULL).
 * @note Complexity: O(1).
 */
size_t registry_trip_count(const Registry *r);

/**
 * @brief Returns AVL timetable height (0 if empty; -1 if r is NULL).
 * @note Complexity: O(1).
 */
int registry_timetable_height(const Registry *r);

/**
 * @brief Checks AVL timetable balance invariant (1 if balanced, 0 if not, -1 if r is NULL).
 * @note Complexity: O(T).
 */
int registry_timetable_balanced(const Registry *r);

/**
 * @brief SimLoopFn callback appending completed bus loops to the bus's journey LinkedList.
 *
 * Runs synchronously inside sim_run_until() under the caller's external lock.
 * Unknown bus IDs are ignored. Caps retained history per bus at REGISTRY_MAX_JOURNEYS.
 *
 * @param ctx Registry* pointer.
 * @param bus Bus ID.
 * @param route Route ID.
 * @param start_s Loop start time in seconds.
 * @param end_s Loop end time in seconds.
 * @param pax Passengers boarded during the loop.
 * @note Complexity: O(1).
 */
void registry_sim_observer(void *ctx, int bus, int route, double start_s, double end_s, int pax);

/**
 * @brief Retrieves recorded journey history for bus_id (oldest first).
 *
 * @param r Registry handle (non-NULL).
 * @param bus_id Bus ID.
 * @param out Output JourneyRecord array (non-NULL).
 * @param max Capacity of out.
 * @param n Receives count of records written (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg, STATUS_ERR_NOT_FOUND if bus unknown.
 * @note Complexity: O(J).
 */
Status registry_journeys(const Registry *r, int bus_id, JourneyRecord *out, size_t max, size_t *n);

/**
 * @brief Ranks routes on a priority board using a Part 1 max-heap.
 *
 * Priority = demand[r] / (counts[r] + 1); ties broken by smaller route_id.
 *
 * @param r Registry handle (non-NULL).
 * @param demand Array of route demands (length n, finite >= 0).
 * @param counts Array of assigned bus counts (length n, >= 0).
 * @param out Output RouteBoardRow array (length n, non-NULL).
 * @param n Must equal network_route_count(net).
 * @return STATUS_OK on success, STATUS_ERR_NULL/INVALID/NOMEM on error.
 * @note Complexity: O(R log R).
 */
Status registry_route_board(const Registry *r, const double *demand, const int *counts,
                            RouteBoardRow *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_REGISTRY_H */

