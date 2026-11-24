#ifndef DEMAND_MODEL_H
#define DEMAND_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "types.h"
#include "graph.h"

/**
 * @file demand_model.h
 * @brief Passenger demand modelling, deterministic simulation, and dataset generation.
 *
 * NOTE: Functions in this module are NOT thread-safe without external synchronization.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define DEMAND_NUM_FEATURES 10

/* Feature order (EXACT, used across C and Python models):
 * 0: hour
 * 1: weekend
 * 2: holiday
 * 3: rain
 * 4: event
 * 5: type_hub
 * 6: type_office
 * 7: type_market
 * 8: type_residential
 * 9: stop_scale
 */

typedef struct DemandStop {
    int node;
    int64_t osm_id;
    StopType type;
    double scale;
    double lat;
    double lng;
    char name[64];
} DemandStop;

typedef struct DemandRow {
    int day;
    int stop_node;
    StopType stop_type;
    double stop_scale;
    int hour;
    DayType day_type;
    int rain;
    int event;
    double passengers;
    double lambda;
} DemandRow;

typedef enum DemandCategory {
    CAT_WEEKDAY = 0,
    CAT_WEEKEND,
    CAT_HOLIDAY,
    CAT_RAIN,
    CAT_EVENT,
    DEMAND_NUM_CATEGORIES
} DemandCategory;

/* Deterministic 64-bit mixing and hashing functions */
static inline uint64_t mix64(uint64_t x) {
    uint64_t z = x + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline uint64_t demand_hash(uint64_t seed, uint64_t a, uint64_t b, uint64_t c) {
    return mix64(mix64(mix64(mix64(seed) ^ a) ^ b) ^ c);
}

static inline double demand_unit(uint64_t h) {
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

/**
 * Calculates theoretical expected demand (lambda in passengers/hour).
 * Returns -1.0 on invalid arguments.
 */
double demand_expected(StopType type, double scale, int hour, DayType day_type, int rain, int event);

/**
 * Fills out[10] with the one-hot and numerical feature representation.
 */
void demand_features(StopType type, double scale, int hour, DayType day_type, int rain, int event, double out[DEMAND_NUM_FEATURES]);

/**
 * Deterministic calendar generation for a single day index.
 */
void demand_calendar(uint64_t seed, int day, DayType *day_type, int *rain, int *event_day);

/**
 * Selects up to 3 distinct stop indices for an event on a given day.
 */
size_t demand_event_stops(uint64_t seed, int day, size_t n_stops, int out[3]);

/**
 * Generates a quantised stop scale in [0.6, 1.8] (step 1/64).
 */
double demand_stop_scale(uint64_t seed, int64_t osm_id);

/**
 * Extracts and classifies bus stops from the graph's largest SCC using uniform stride.
 */
Status demand_stops_from_graph(const Graph *g, size_t max_stops, uint64_t seed, DemandStop **out, size_t *count);

typedef struct AnchorCentre {
    const char *name;
    double lat;
    double lng;
    double radius_m;
    size_t max_anchors;
    StopType type;
} AnchorCentre;

#define DEMAND_NUM_ANCHOR_CENTRES 5
extern const AnchorCentre g_anchor_centres[DEMAND_NUM_ANCHOR_CENTRES];

/**
 * Extracts bus stops from the graph's largest SCC with anchored selection
 * for central Bangalore hubs and office corridors, filling remaining slots by stride.
 */
Status demand_stops_from_graph_anchored(const Graph *g, size_t max_stops, uint64_t seed, DemandStop **out, size_t *count);

/**
 * Samples from Poisson(lambda) deterministically using stream_hash.
 */
double demand_poisson_sample(double lambda, uint64_t stream_hash);

/**
 * Generates a full dataset of DemandRows.
 */
Status demand_generate(const DemandStop *stops, size_t n_stops, uint64_t seed, int days, DemandRow **rows_out, size_t *n_rows);

/**
 * Writes DemandRows to CSV atomically.
 */
Status demand_rows_write_csv(const DemandRow *rows, size_t n, const char *path);

/**
 * Loads DemandRows from CSV with strict format and range checks.
 */
Status demand_rows_load_csv(const char *path, DemandRow **rows, size_t *n);

/**
 * Classifies a demand row into a single reporting category (priority order).
 */
DemandCategory demand_row_category(const DemandRow *r);

/**
 * Returns string name of a demand category.
 */
const char *demand_category_name(DemandCategory c);

#ifdef __cplusplus
}
#endif

#endif /* DEMAND_MODEL_H */
