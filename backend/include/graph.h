#ifndef GRAPH_H
#define GRAPH_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "types.h"

/**
 * Thread-safety: The Graph module and all Part 2 modules are NOT thread-safe.
 * Concurrent access must be synchronised by the caller (e.g. via mutexes).
 *
 * Error code contract:
 * - STATUS_ERR_NULL: Returned when any required pointer parameter (Graph*, out*, path, etc.) is NULL.
 * - STATUS_ERR_INVALID: Returned when input arguments are out-of-range (e.g. invalid node/edge index,
 *   length <= 0, base_time <= 0, factor is NaN, malformed cache file syntax, or invalid grid dimensions).
 * - STATUS_ERR_NOT_FOUND: Returned when an external resource or target file is missing.
 * - STATUS_ERR_NOMEM: Returned upon dynamic memory allocation failure.
 */

typedef struct Graph Graph;

typedef enum CostMode {
    COST_STATIC = 0,   /* base_time_s */
    COST_TRAFFIC = 1   /* base_time_s * traffic_factor */
} CostMode;

typedef enum RoadClass {
    ROAD_PRIMARY = 0,
    ROAD_SECONDARY,
    ROAD_TERTIARY,
    ROAD_OTHER
} RoadClass;

typedef struct GraphNode {
    int64_t osm_id;
    double lat;
    double lng;
    char name[64];
    int is_stop;
} GraphNode;

typedef struct EdgeMeta {
    char road_name[48];
    RoadClass road_class;
} EdgeMeta;

#define GRAPH_MAX_TRAFFIC_FACTOR 10.0

/**
 * Creates an empty graph.
 *
 * @return Pointer to newly allocated Graph, or NULL on allocation failure.
 * Ownership: Caller owns the returned Graph.
 * Complexity: O(1)
 */
Graph *graph_create(void);

/**
 * Destroys a graph and frees all allocated memory.
 *
 * @param g Graph to destroy (NULL-safe).
 * Ownership: Graph and all internal memory are freed.
 * Complexity: O(V + E)
 */
void graph_destroy(Graph *g);

/**
 * Adds a node to the graph or updates it if osm_id already exists.
 * If osm_id exists, ORs is_stop with existing, and if existing name is empty,
 * copies the new name.
 *
 * @param g Graph pointer.
 * @param osm_id OpenStreetMap node identifier.
 * @param lat Latitude in degrees.
 * @param lng Longitude in degrees.
 * @param name Optional node name (may be NULL or empty).
 * @param is_stop 1 if bus stop, 0 otherwise.
 * @return Dense node index (0..N-1), or -1 on error.
 * Ownership: Graph copies the name.
 * Complexity: O(1) average.
 */
int graph_add_node(Graph *g, int64_t osm_id, double lat, double lng, const char *name, int is_stop);

/**
 * Adds a directed edge between two dense node indices.
 * Rejects out-of-range indices, from == to, length_m <= 0, or base_time_s <= 0.
 *
 * NOTE: Pointers returned by graph_edge and graph_edge_mut are invalidated
 * by subsequent calls to graph_add_edge.
 *
 * @param g Graph pointer.
 * @param from Source dense node index.
 * @param to Destination dense node index.
 * @param length_m Edge length in metres (> 0).
 * @param base_time_s Free-flow traversal time in seconds (> 0).
 * @param road_name Road name (may be NULL).
 * @param cls Road classification.
 * @return Edge ID (0..E-1), or -1 on error.
 * Ownership: Graph stores metadata.
 * Complexity: O(1) amortised.
 */
int graph_add_edge(Graph *g, int from, int to, double length_m, double base_time_s, const char *road_name, RoadClass cls);

/**
 * Adds two directed edges (a -> b and b -> a).
 *
 * @param g Graph pointer.
 * @param a First node index.
 * @param b Second node index.
 * @param length_m Edge length in metres.
 * @param base_time_s Edge base time in seconds.
 * @param road_name Road name.
 * @param cls Road class.
 * @param edge_ab Optional output pointer for forward edge ID (may be NULL).
 * @param edge_ba Optional output pointer for backward edge ID (may be NULL).
 * @return STATUS_OK on success, or error code on failure.
 * Complexity: O(1) amortised.
 */
Status graph_add_bidirectional(Graph *g, int a, int b, double length_m, double base_time_s,
                               const char *road_name, RoadClass cls, int *edge_ab, int *edge_ba);

/**
 * Returns the number of nodes in the graph.
 */
size_t graph_node_count(const Graph *g);

/**
 * Returns the number of edges in the graph.
 */
size_t graph_edge_count(const Graph *g);

/**
 * Returns a pointer to the GraphNode at the given dense index.
 *
 * @param g Graph pointer.
 * @param index Node index (0..N-1).
 * @return Pointer to node, or NULL if out of range.
 * Ownership: Node remains owned by graph.
 * Complexity: O(1)
 */
const GraphNode *graph_node(const Graph *g, int index);

/**
 * Finds the dense node index corresponding to an osm_id.
 *
 * @param g Graph pointer.
 * @param osm_id OpenStreetMap node ID.
 * @return Dense index (0..N-1), or -1 if not found.
 * Complexity: O(1) average.
 */
int graph_find_node_by_osm_id(const Graph *g, int64_t osm_id);

/**
 * Returns a const pointer to Edge by edge ID.
 *
 * @param g Graph pointer.
 * @param edge_id Edge ID (0..E-1).
 * @return Pointer to Edge, or NULL if out of range.
 * Ownership: Remains owned by graph; invalidated by graph_add_edge.
 * Complexity: O(1)
 */
const Edge *graph_edge(const Graph *g, int edge_id);

/**
 * Returns a mutable pointer to Edge by edge ID.
 *
 * @param g Graph pointer.
 * @param edge_id Edge ID (0..E-1).
 * @return Pointer to Edge, or NULL if out of range.
 * Ownership: Remains owned by graph; invalidated by graph_add_edge.
 * Complexity: O(1)
 */
Edge *graph_edge_mut(Graph *g, int edge_id);

/**
 * Returns metadata for the given edge ID.
 *
 * @param g Graph pointer.
 * @param edge_id Edge ID (0..E-1).
 * @return Pointer to EdgeMeta, or NULL if out of range.
 * Ownership: Remains owned by graph; invalidated by graph_add_edge.
 * Complexity: O(1)
 */
const EdgeMeta *graph_edge_meta(const Graph *g, int edge_id);

/**
 * Returns the out-degree (number of outgoing edges) of a node.
 *
 * @param g Graph pointer.
 * @param node Node index.
 * @return Number of outgoing edges, or 0 if invalid node.
 * Complexity: O(1)
 */
size_t graph_out_degree(const Graph *g, int node);

/**
 * Returns the k-th outgoing edge ID for the given node.
 *
 * @param g Graph pointer.
 * @param node Node index.
 * @param k 0-based index among outgoing edges (0..out_degree-1).
 * @return Edge ID, or -1 if out of range.
 * Complexity: O(1)
 */
int graph_out_edge(const Graph *g, int node, size_t k);

/**
 * Sets the traffic factor for an edge. Clamped to [1.0, GRAPH_MAX_TRAFFIC_FACTOR].
 * Returns STATUS_ERR_INVALID if factor is NaN.
 *
 * @param g Graph pointer.
 * @param edge_id Edge ID.
 * @param factor Traffic multiplier (>= 1.0).
 * @return STATUS_OK on success, or error code.
 * Complexity: O(1)
 */
Status graph_set_traffic_factor(Graph *g, int edge_id, double factor);

/**
 * Sets the blocked flag for an edge (normalised to 0 or 1).
 *
 * @param g Graph pointer.
 * @param edge_id Edge ID.
 * @param blocked 0 for open, non-zero for blocked.
 * @return STATUS_OK on success, or error code.
 * Complexity: O(1)
 */
Status graph_set_blocked(Graph *g, int edge_id, int blocked);

/**
 * Resets traffic factors of all edges to 1.0. Does not modify blocked flags.
 *
 * @param g Graph pointer.
 * Complexity: O(E)
 */
void graph_reset_traffic(Graph *g);

/**
 * Computes edge traversal cost in seconds for a given cost mode.
 *
 * @param g Graph pointer.
 * @param edge_id Edge ID.
 * @param mode COST_STATIC (base_time_s) or COST_TRAFFIC (base_time_s * traffic_factor).
 * @return Cost in seconds, or -1.0 if invalid.
 * Complexity: O(1)
 */
double graph_edge_cost_s(const Graph *g, int edge_id, CostMode mode);

/**
 * Computes great-circle distance in metres between two coordinate pairs using Haversine formula.
 * Earth radius = 6,371,000 m.
 *
 * @param lat1 Latitude of point 1 in degrees.
 * @param lng1 Longitude of point 1 in degrees.
 * @param lat2 Latitude of point 2 in degrees.
 * @param lng2 Longitude of point 2 in degrees.
 * @return Distance in metres.
 * Complexity: O(1)
 */
double graph_haversine_m(double lat1, double lng1, double lat2, double lng2);

/**
 * Returns the maximum speed (in m/s) across all edges in the graph.
 * Defined as MAX over all edges of ( haversine(from, to) / base_time_s ).
 * Maintained incrementally; never below 1e-3.
 *
 * @param g Graph pointer.
 * @return Maximum speed in metres per second.
 * Complexity: O(1)
 */
double graph_max_speed_mps(const Graph *g);

/**
 * Finds all edges whose midpoint lies within radius_m of (lat, lng).
 * Results are sorted by ascending edge ID.
 *
 * @param g Graph pointer.
 * @param lat Target latitude.
 * @param lng Target longitude.
 * @param radius_m Search radius in metres.
 * @param out_ids Array to store matching edge IDs.
 * @param max_out Maximum capacity of out_ids array.
 * @return Number of matching edge IDs written.
 * Complexity: O(E)
 */
size_t graph_find_edges_near(const Graph *g, double lat, double lng, double radius_m, int *out_ids, size_t max_out);

/**
 * Finds the nearest node to the given coordinates.
 *
 * @param g Graph pointer.
 * @param lat Target latitude.
 * @param lng Target longitude.
 * @param dist_m Optional pointer to receive distance in metres (may be NULL).
 * @return Node index, or -1 if graph is empty or invalid.
 * Complexity: O(V)
 */
int graph_nearest_node(const Graph *g, double lat, double lng, double *dist_m);

/**
 * Extracts a new Graph containing only the largest weakly connected component.
 * Nodes are re-indexed in ascending original order, edges remapped, metadata kept.
 * Traffic factors are reset to 1.0. Ties broken by choosing component containing
 * the smallest node index.
 *
 * @param g Graph pointer.
 * @return New Graph pointer, or NULL on error / empty graph.
 * Ownership: Caller owns the returned Graph.
 * Complexity: O(V + E)
 */
Graph *graph_extract_largest_component(const Graph *g);

/**
 * Saves the graph to a text cache file atomically (writes to <path>.tmp then renames).
 *
 * @param g Graph pointer.
 * @param path Destination file path.
 * @return STATUS_OK on success, or error code.
 * Complexity: O(V + E)
 */
Status graph_save(const Graph *g, const char *path);

/**
 * Loads a graph from a text cache file.
 * Returns STATUS_ERR_INVALID on any malformed line, count mismatch, or out-of-range edge.
 * Returns STATUS_ERR_NOT_FOUND if file is missing. On error, *out is set to NULL without leaks.
 *
 * @param out Output pointer to receive newly created Graph.
 * @param path Source file path.
 * @return STATUS_OK on success, or error code.
 * Ownership: Caller owns *out on success.
 * Complexity: O(V + E)
 */
Status graph_load(Graph **out, const char *path);

/**
 * Generates a deterministic synthetic grid graph.
 * Rows and cols must be in [2, 400].
 *
 * @param out Output pointer to receive newly created Graph.
 * @param rows Number of grid rows.
 * @param cols Number of grid columns.
 * @param lat0 Top-left latitude.
 * @param lng0 Top-left longitude.
 * @param spacing_m Grid node spacing in metres.
 * @param seed Seed for deterministic splitmix64 PRNG.
 * @return STATUS_OK on success, or error code.
 * Ownership: Caller owns *out on success.
 * Complexity: O(rows * cols)
 */
Status graph_build_synthetic_grid(Graph **out, int rows, int cols, double lat0, double lng0, double spacing_m, uint64_t seed);

#endif /* GRAPH_H */

