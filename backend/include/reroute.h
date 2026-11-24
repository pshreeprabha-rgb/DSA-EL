#ifndef REROUTE_H
#define REROUTE_H

#include <stddef.h>
#include "common.h"
#include "graph.h"
#include "pathfind.h"

/**
 * Thread-safety: The RouteManager module is NOT thread-safe.
 *
 * Error code contract:
 * - STATUS_ERR_NULL: Returned when any required pointer parameter (RouteManager*, etc.) is NULL.
 * - STATUS_ERR_INVALID: Returned when input arguments are out-of-range (e.g. invalid edge_id or node index).
 * - STATUS_ERR_DUPLICATE: Returned when attempting to track an already tracked route_id.
 * - STATUS_ERR_NOT_FOUND: Returned when route_id is not tracked or history index is out-of-range.
 * - STATUS_ERR_EMPTY: Returned when attempting to undo with an empty history stack.
 * - STATUS_ERR_NOMEM: Returned upon dynamic memory allocation failure.
 */

typedef struct RouteManager RouteManager;

typedef struct RerouteSummary {
    size_t routes_checked;
    size_t routes_rerouted;
    size_t routes_unreachable;
    int pushed_history;
} RerouteSummary;

#define RM_DEFAULT_THRESHOLD 1.8

/**
 * Creates a RouteManager associated with a Graph.
 *
 * @param g Graph pointer (manager does NOT own the graph).
 * @param reroute_threshold Ratio threshold (<= 1.0 means use RM_DEFAULT_THRESHOLD).
 * @return Newly allocated RouteManager, or NULL on error.
 * Ownership: Caller owns the RouteManager.
 * Complexity: O(1)
 */
RouteManager *rm_create(Graph *g, double reroute_threshold);

/**
 * Destroys the RouteManager, freeing all tracked routes and undo history.
 * Does NOT destroy the associated Graph.
 *
 * @param rm RouteManager pointer (NULL-safe).
 * Complexity: O(routes + history)
 */
void rm_destroy(RouteManager *rm);

/**
 * Starts tracking a route with the given ID between src and dst.
 * Plans initial path using A* with COST_TRAFFIC. Does NOT push history.
 *
 * @param rm RouteManager pointer.
 * @param route_id Unique route identifier.
 * @param src Dense source node index.
 * @param dst Dense destination node index.
 * @return STATUS_OK on success, STATUS_ERR_DUPLICATE if route_id exists,
 *         or other error status.
 * Complexity: O(E log V)
 */
Status rm_track_route(RouteManager *rm, int route_id, int src, int dst);

/**
 * Stops tracking a route and frees its stored path.
 *
 * @param rm RouteManager pointer.
 * @param route_id Route identifier.
 * @return STATUS_OK on success, STATUS_ERR_NOT_FOUND if not tracked.
 * Complexity: O(1) average
 */
Status rm_untrack_route(RouteManager *rm, int route_id);

/**
 * Retrieves the current path for a tracked route.
 *
 * @param rm RouteManager pointer.
 * @param route_id Route identifier.
 * @return Pointer to PathResult, or NULL if unknown.
 * Ownership: PathResult remains owned by RouteManager.
 * Complexity: O(1) average
 */
const PathResult *rm_get_path(const RouteManager *rm, int route_id);

/**
 * Blocks an edge and triggers rerouting for routes whose current path contains that edge.
 * Pushes atomic ChangeSet to history if edge wasn't already blocked.
 *
 * @param rm RouteManager pointer.
 * @param edge_id Edge ID.
 * @param out Optional output summary (may be NULL).
 * @return STATUS_OK on success, or error code.
 * Complexity: O(R * E log V) where R is affected routes.
 */
Status rm_block_edge(RouteManager *rm, int edge_id, RerouteSummary *out);

/**
 * Unblocks an edge, rerouting affected routes and retrying currently unreachable routes.
 * Pushes atomic ChangeSet to history if edge was blocked.
 *
 * @param rm RouteManager pointer.
 * @param edge_id Edge ID.
 * @param out Optional output summary (may be NULL).
 * @return STATUS_OK on success, or error code.
 * Complexity: O(R * E log V)
 */
Status rm_unblock_edge(RouteManager *rm, int edge_id, RerouteSummary *out);

/**
 * Sets traffic factor on an edge and reroutes routes containing that edge if factor >= threshold.
 * Pushes atomic ChangeSet to history if factor changed.
 *
 * @param rm RouteManager pointer.
 * @param edge_id Edge ID.
 * @param factor New traffic multiplier.
 * @param out Optional output summary (may be NULL).
 * @return STATUS_OK on success, or error code.
 * Complexity: O(R * E log V)
 */
Status rm_set_traffic_factor(RouteManager *rm, int edge_id, double factor, RerouteSummary *out);

/**
 * Scans all tracked routes and reroutes any route that has an edge with
 * traffic_factor >= threshold or is currently unreachable.
 * Pushes history ONLY if at least one route changed.
 *
 * @param rm RouteManager pointer.
 * @param out Optional output summary (may be NULL).
 * @return STATUS_OK on success, or error code.
 * Complexity: O(total_routes * E log V)
 */
Status rm_check_and_reroute(RouteManager *rm, RerouteSummary *out);

/**
 * Undoes the most recent action, atomically restoring both edge states
 * and affected route paths.
 *
 * @param rm RouteManager pointer.
 * @param description Optional buffer to receive human-readable description (may be NULL).
 * @param desc_size Size of description buffer.
 * @return STATUS_OK on success, STATUS_ERR_EMPTY if no history to undo.
 * Complexity: O(changed_routes + changed_edges)
 */
Status rm_undo(RouteManager *rm, char *description, size_t desc_size);

/**
 * Returns number of ChangeSets on the undo history stack.
 */
size_t rm_history_size(const RouteManager *rm);

/**
 * Retrieves human-readable description of a history entry (0 = most recent).
 *
 * @param rm RouteManager pointer.
 * @param index_from_top 0 for top of stack.
 * @param buf Output text buffer.
 * @param buf_size Capacity of buf.
 * @return STATUS_OK on success, or error code.
 * Complexity: O(1)
 */
Status rm_history_describe(const RouteManager *rm, size_t index_from_top, char *buf, size_t buf_size);

#endif /* REROUTE_H */

