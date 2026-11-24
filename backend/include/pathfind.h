#ifndef PATHFIND_H
#define PATHFIND_H

#include <stddef.h>
#include "common.h"
#include "graph.h"

/**
 * Thread-safety: The Pathfinding module is NOT thread-safe.
 *
 * Error code contract:
 * - STATUS_ERR_NULL: Returned when any required pointer parameter (Graph*, out PathResult*, etc.) is NULL.
 * - STATUS_ERR_INVALID: Returned when input arguments are out-of-range (e.g. src or dst < 0 or >= node_count,
 *   invalid search algorithm, or invalid cost mode).
 * - STATUS_ERR_NOMEM: Returned upon dynamic memory allocation failure.
 */

typedef enum SearchAlgo {
    SEARCH_ASTAR = 0,
    SEARCH_DIJKSTRA = 1
} SearchAlgo;

typedef struct PathResult {
    int found;                  /* 1 if path found, 0 if unreachable/error */
    double total_time_s;        /* Total travel time along path */
    double total_length_m;      /* Total geometric distance in metres */
    size_t nodes_explored;      /* Count of nodes popped/closed (including goal) */
    size_t nodes_generated;     /* Count of distinct nodes pushed to open list */
    size_t node_count;          /* Number of nodes in path */
    int *nodes;                 /* Array of dense node indices */
    size_t edge_count;          /* Number of edges in path */
    int *edges;                 /* Array of edge IDs */
} PathResult;

/**
 * Initialises a PathResult struct to empty state (zeroes/NULLs).
 *
 * @param p Pointer to PathResult (NULL-safe).
 * Complexity: O(1)
 */
void path_result_init(PathResult *p);

/**
 * Frees dynamically allocated memory in PathResult and re-initialises it.
 *
 * @param p Pointer to PathResult (NULL-safe).
 * Complexity: O(1)
 */
void path_result_free(PathResult *p);

/**
 * Performs a deep copy of a PathResult.
 * Caller must ensure dst is either initialised or freed before copying.
 *
 * @param dst Destination PathResult.
 * @param src Source PathResult.
 * @return STATUS_OK on success, or error code on failure.
 * Complexity: O(node_count + edge_count)
 */
Status path_result_copy(PathResult *dst, const PathResult *src);

/**
 * Finds shortest path between src and dst nodes using specified algorithm and cost mode.
 * Blocked edges are skipped in both cost modes.
 *
 * @param g Graph pointer.
 * @param src Source dense node index.
 * @param dst Destination dense node index.
 * @param algo SEARCH_ASTAR or SEARCH_DIJKSTRA.
 * @param mode COST_STATIC or COST_TRAFFIC.
 * @param out Output PathResult.
 * @return STATUS_OK on success (even if unreachable; out->found = 0),
 *         STATUS_ERR_INVALID on invalid parameters, STATUS_ERR_NOMEM on allocation failure.
 * Complexity: O(E log V) worst case.
 */
Status find_path(const Graph *g, int src, int dst, SearchAlgo algo, CostMode mode, PathResult *out);

/**
 * A* search convenience wrapper.
 */
Status astar_find_path(const Graph *g, int src, int dst, CostMode mode, PathResult *out);

/**
 * Dijkstra search convenience wrapper.
 */
Status dijkstra_find_path(const Graph *g, int src, int dst, CostMode mode, PathResult *out);

/**
 * Recomputes travel time of the stored edge list under the given mode.
 *
 * @param g Graph pointer.
 * @param p PathResult pointer.
 * @param mode Cost mode (STATIC or TRAFFIC).
 * @return Total time in seconds, or -1.0 if path not found, invalid, or contains a blocked edge.
 * Complexity: O(edge_count)
 */
double path_time_s(const Graph *g, const PathResult *p, CostMode mode);

/**
 * Checks whether an edge ID is present in the path.
 *
 * @param p PathResult pointer.
 * @param edge_id Edge ID.
 * @return 1 if found, 0 otherwise.
 * Complexity: O(edge_count)
 */
int path_contains_edge(const PathResult *p, int edge_id);

/**
 * Validates that consecutive edges connect properly, match nodes[], and none are blocked.
 *
 * @param g Graph pointer.
 * @param p PathResult pointer.
 * @return 1 if path is valid, 0 otherwise.
 * Complexity: O(edge_count)
 */
int path_is_valid(const Graph *g, const PathResult *p);

#endif /* PATHFIND_H */

