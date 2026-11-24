#ifndef TRAFFIC_CLIENT_H
#define TRAFFIC_CLIENT_H

#include <stddef.h>
#include "common.h"
#include "graph.h"

/**
 * Thread-safety: The TrafficUpdater is NOT thread-safe.
 * Part 4 will wrap calls in a mutex.
 *
 * Error code contract:
 * - STATUS_ERR_NULL: Returned when any required pointer parameter (TrafficUpdater*, buf, path, etc.) is NULL.
 * - STATUS_ERR_INVALID: Returned when arguments are invalid (e.g. empty API key, buffer truncation, malformed JSON,
 *   malformed corridor line format, or over-long .env line).
 * - STATUS_ERR_NOT_FOUND: Returned when an external resource or target file is missing.
 * - STATUS_ERR_NOMEM: Returned upon dynamic memory allocation failure.
 */

/* ---- Types ---- */

typedef struct TomTomFlow {
    double current_speed;    /* km/h */
    double free_flow_speed;  /* km/h */
    double confidence;       /* 0.0 .. 1.0 */
    int road_closure;        /* 1 if closed */
} TomTomFlow;

typedef struct TrafficCorridor {
    char name[48];
    double lat;
    double lng;
    double radius_m;
} TrafficCorridor;

typedef enum TrafficSource {
    TRAFFIC_SIMULATED = 0,
    TRAFFIC_LIVE = 1
} TrafficSource;

/**
 * HTTP fetch callback. body_out is malloc'd by callee; caller frees.
 * http_status receives the HTTP response code.
 */
typedef Status (*HttpFetchFn)(const char *url, char **body_out, long *http_status, void *ctx);

typedef struct TrafficConfig {
    const char *api_key;    /* NULL: read env TOMTOM_API_KEY */
    int max_calls;          /* <= 0: use 500 */
    int cache_seconds;      /* <= 0: use 300 */
    int offline;            /* 1: skip all live fetches */
    HttpFetchFn fetch;      /* NULL: default libcurl */
    void *fetch_ctx;        /* user context for custom fetch */
} TrafficConfig;

typedef struct TrafficUpdater TrafficUpdater;

/* ---- .env loader ---- */

/**
 * Reads a KEY=VALUE file and calls setenv() for each key (non-overwrite).
 * Accepts export prefix, quoted values, blank lines, # comments, CRLF.
 * Missing file -> STATUS_ERR_NOT_FOUND (not a failure for callers).
 *
 * @param path File path.
 * @return STATUS_OK, STATUS_ERR_NOT_FOUND, STATUS_ERR_INVALID.
 */
Status env_load_file(const char *path);

/**
 * Tries ".env", "../.env", "../../.env" (first existing wins).
 * Returns STATUS_OK if one was loaded, STATUS_ERR_NOT_FOUND if none exists.
 */
Status env_load_default(void);

/* ---- Pure functions (no network) ---- */

/**
 * Builds a TomTom Traffic Flow URL into buf.
 *
 * @param buf Output buffer.
 * @param n Buffer size.
 * @param api_key TomTom API key (must be non-empty).
 * @param lat Latitude.
 * @param lng Longitude.
 * @return STATUS_OK on success, STATUS_ERR_INVALID on truncation or empty key.
 */
Status tomtom_build_url(char *buf, size_t n, const char *api_key, double lat, double lng);

/**
 * Copies url into buf with the value after "key=" replaced by "***".
 * Always NUL-terminates buf.
 */
void tomtom_redact_url(const char *url, char *buf, size_t n);

/**
 * Parses a TomTom flowSegmentData JSON response.
 * Tolerant of missing optional fields (confidence defaults to 1.0,
 * roadClosure defaults to 0). Missing currentSpeed/freeFlowSpeed or
 * an error object -> STATUS_ERR_INVALID.
 *
 * @param json Null-terminated JSON string.
 * @param out Output TomTomFlow struct.
 * @return STATUS_OK or STATUS_ERR_INVALID.
 */
Status tomtom_parse_flow_json(const char *json, TomTomFlow *out);

/**
 * Computes traffic factor from a TomTomFlow.
 * road_closure or current_speed <= 0 -> GRAPH_MAX_TRAFFIC_FACTOR.
 * Otherwise: clamp(free_flow_speed / current_speed, 1.0, GRAPH_MAX_TRAFFIC_FACTOR).
 * Confidence < 0.4 is "unusable" (the updater handles this, not this function).
 */
double tomtom_traffic_factor(const TomTomFlow *f);

/* ---- TrafficUpdater ---- */

/**
 * Creates a TrafficUpdater. Copies corridors array and the api_key string.
 *
 * @param g Graph (not owned).
 * @param corridors Array of corridors.
 * @param n Corridor count.
 * @param cfg Configuration (NULL is valid, uses all defaults).
 * @return New TrafficUpdater, or NULL on error.
 */
TrafficUpdater *tu_create(Graph *g, const TrafficCorridor *corridors, size_t n, const TrafficConfig *cfg);

/**
 * Destroys the updater. NULL-safe.
 */
void tu_destroy(TrafficUpdater *tu);

/**
 * Updates traffic on the graph.
 * 1. Always calls traffic_sim_apply (base layer).
 * 2. For each corridor with an expired cache, fetches live data.
 * 3. On any failure keeps the simulated value for that corridor.
 *
 * @param tu TrafficUpdater.
 * @param now_s Caller-supplied monotonic time in seconds (tests control this).
 * @param sim_minutes Minutes since midnight for simulation base.
 * @param rain Rain flag for simulation.
 * @return STATUS_OK on success (fetch failures are non-fatal).
 */
Status tu_update(TrafficUpdater *tu, double now_s, double sim_minutes, int rain);

/**
 * Returns TRAFFIC_LIVE if at least one corridor has a usable live cache value.
 */
TrafficSource tu_source(const TrafficUpdater *tu);

/**
 * Returns the number of live API calls made.
 */
int tu_call_count(const TrafficUpdater *tu);

/**
 * Returns the last error string, or empty string if none.
 */
const char *tu_last_error(const TrafficUpdater *tu);

/**
 * Returns the number of corridors currently with a usable live cached value.
 */
size_t tu_live_corridor_count(const TrafficUpdater *tu);

/**
 * Loads corridors from a tab-separated file.
 * Format: name<TAB>lat<TAB>lng<TAB>radius_m (one per line, # comments, blank lines ignored).
 *
 * @param path File path.
 * @param out Output array.
 * @param max Maximum capacity.
 * @param count Output count.
 * @return STATUS_OK, STATUS_ERR_NOT_FOUND, STATUS_ERR_INVALID.
 */
Status tu_load_corridors(const char *path, TrafficCorridor *out, size_t max, size_t *count);

#endif /* TRAFFIC_CLIENT_H */
