#ifndef OSM_LOADER_H
#define OSM_LOADER_H

#include <stddef.h>
#include "common.h"
#include "graph.h"

typedef struct OsmConfig {
    double south, west, north, east;
    const char *overpass_url;
    const char *data_dir;
    const char *user_agent;
    int timeout_s;
} OsmConfig;

typedef struct OsmBuildStats {
    size_t ways_total;
    size_t ways_used;
    size_t json_nodes;
    size_t junction_nodes;
    size_t edges_created;
    size_t stops_found;
    size_t stops_snapped;
    size_t stops_discarded;
    size_t nodes_dropped_by_component;
    size_t final_nodes;
    size_t final_edges;
} OsmBuildStats;

/**
 * Sets default OsmConfig values for central Bangalore.
 */
void osm_config_default(OsmConfig *c);

/**
 * Builds a Graph purely from JSON strings (no network, no files).
 * stops_json may be NULL.
 *
 * @param roads_json Raw Overpass JSON for roads.
 * @param stops_json Raw Overpass JSON for bus stops (may be NULL).
 * @param out Output Graph pointer.
 * @param stats Optional output statistics (may be NULL).
 * @return STATUS_OK on success, or error code.
 */
Status osm_build_graph_from_json(const char *roads_json, const char *stops_json, Graph **out, OsmBuildStats *stats);

/**
 * Fetches raw Overpass data for a query.
 * Retries on HTTP 429/504 or timeout (up to 2 retries).
 *
 * @param c OsmConfig.
 * @param query Overpass query string.
 * @param body_out malloc'd response body; caller frees.
 * @return STATUS_OK on success, or error code.
 */
Status osm_fetch_raw(const OsmConfig *c, const char *query, char **body_out);

/**
 * Loads or fetches the graph, handling the full lifecycle.
 * See specification for the load order.
 *
 * @param c OsmConfig.
 * @param refresh 1 to force re-fetch.
 * @param offline 1 to disallow network.
 * @param out Output Graph pointer.
 * @param stats Optional output stats.
 * @return STATUS_OK on success.
 */
Status osm_load_or_fetch(const OsmConfig *c, int refresh, int offline, Graph **out, OsmBuildStats *stats);

#endif /* OSM_LOADER_H */
