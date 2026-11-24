#ifndef GRAPH_SCC_H
#define GRAPH_SCC_H

#include <stddef.h>
#include "common.h"
#include "graph.h"

/**
 * @file graph_scc.h
 * @brief Strongly Connected Components (SCC) analysis on directed road graphs.
 *
 * Provides Tarjan's SCC labeling algorithm using an explicit stack (strictly
 * iterative, safe for deep graphs such as long 5000+ node chains without
 * overflowing the C runtime stack), and extraction of the largest SCC.
 *
 * IMPORTANT: All edges are traversed regardless of the edge `blocked` flag.
 *
 * NOTE: Functions in this module are NOT thread-safe without external synchronization.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Labels each node in graph `g` with its strongly connected component index.
 *
 * @param g Const pointer to the graph.
 * @param labels Caller-allocated array of at least graph_node_count(g) ints.
 *               Upon success, labels[i] is in [0, *num_components).
 * @param num_components Pointer to size_t receiving the number of SCCs found.
 * @return STATUS_OK on success,
 *         STATUS_ERR_NULL if g, labels, or num_components is NULL,
 *         STATUS_ERR_INVALID if graph_node_count(g) == 0,
 *         STATUS_ERR_NOMEM if internal memory allocation fails.
 */
Status graph_scc_label(const Graph *g, int *labels, size_t *num_components);

/**
 * Finds and returns the ascending node indices of the largest SCC in graph `g`.
 *
 * If there is a tie in component sizes, the component containing the smallest
 * node index is chosen.
 *
 * @param g Const pointer to the graph.
 * @param nodes_out Output pointer receiving a malloc'ed array of node indices.
 *                  Caller is responsible for free()'ing this array.
 *                  Remains NULL on error.
 * @param count Output pointer receiving the number of nodes in the largest SCC.
 * @return STATUS_OK on success,
 *         STATUS_ERR_NULL if g, nodes_out, or count is NULL,
 *         STATUS_ERR_INVALID if graph_node_count(g) == 0,
 *         STATUS_ERR_NOMEM if memory allocation fails.
 */
Status graph_largest_scc(const Graph *g, int **nodes_out, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* GRAPH_SCC_H */
