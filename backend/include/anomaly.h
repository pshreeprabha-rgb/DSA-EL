#ifndef ANOMALY_H
#define ANOMALY_H

#include <stddef.h>
#include "common.h"

typedef struct {
    double z_threshold;     /* default 3.0 */
    double variance_floor;  /* default 1.0 */
    int min_observed;       /* default 5 */
    double cooldown_min;    /* default 60.0 */
} AnomalyConfig;

typedef struct AnomalyDetector AnomalyDetector;

typedef struct {
    size_t stop;
    int observed;
    double expected;
    double z_score;
} AnomalyFlag;

/**
 * Returns the default anomaly detection configuration:
 * z_threshold = 3.0, variance_floor = 1.0, min_observed = 5, cooldown_min = 60.0.
 */
AnomalyConfig anomaly_config_default(void);

/**
 * Creates an anomaly detector tracking up to max_stops stops.
 * If cfg is NULL, default configuration is used.
 */
AnomalyDetector *anomaly_create(size_t max_stops, const AnomalyConfig *cfg);

/**
 * Destroys an anomaly detector and frees its memory.
 */
void anomaly_destroy(AnomalyDetector *d);

/**
 * Computes the Poisson z-score:
 * z = (observed - expected) / sqrt(max(expected, variance_floor)).
 * Returns NAN if expected < 0.0 or variance_floor <= 0.0 or inputs are NaN.
 */
double anomaly_zscore(int observed, double expected, double variance_floor);

/**
 * Records a passenger arrival event at the given stop in the current open window.
 */
Status anomaly_record_arrival(AnomalyDetector *d, size_t stop);

/**
 * Returns the count of arrivals recorded at the stop in the current open window.
 * Returns -1 if d is NULL or stop is out of bounds.
 */
int anomaly_observed(const AnomalyDetector *d, size_t stop);

/**
 * Closes the current open window at time now_min, checks for demand anomalies
 * against expected demand, populates out flags up to max_out, and resets
 * window counts to 0. Flags are emitted in ascending stop index order.
 */
Status anomaly_close_window(AnomalyDetector *d, double now_min, const double *expected,
                           AnomalyFlag *out, size_t max_out, size_t *n_out);

#endif /* ANOMALY_H */
