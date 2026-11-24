#include "anomaly.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct AnomalyDetector {
    size_t max_stops;
    AnomalyConfig cfg;
    int *counts;
    double *last_flag_time;
};

AnomalyConfig anomaly_config_default(void) {
    AnomalyConfig cfg;
    cfg.z_threshold = 3.0;
    cfg.variance_floor = 1.0;
    cfg.min_observed = 5;
    cfg.cooldown_min = 60.0;
    return cfg;
}

AnomalyDetector *anomaly_create(size_t max_stops, const AnomalyConfig *cfg) {
    if (max_stops == 0) {
        return NULL;
    }
    AnomalyDetector *d = (AnomalyDetector *)calloc(1, sizeof(AnomalyDetector));
    if (!d) return NULL;

    d->max_stops = max_stops;
    d->cfg = cfg ? *cfg : anomaly_config_default();

    d->counts = (int *)calloc(max_stops, sizeof(int));
    if (!d->counts) {
        free(d);
        return NULL;
    }

    d->last_flag_time = (double *)malloc(max_stops * sizeof(double));
    if (!d->last_flag_time) {
        free(d->counts);
        free(d);
        return NULL;
    }

    for (size_t i = 0; i < max_stops; i++) {
        d->last_flag_time[i] = -1e9; /* Ensures cooldown is satisfied initially */
    }

    return d;
}

void anomaly_destroy(AnomalyDetector *d) {
    if (!d) return;
    free(d->counts);
    free(d->last_flag_time);
    free(d);
}

double anomaly_zscore(int observed, double expected, double variance_floor) {
    if (isnan(expected) || isinf(expected) || expected < 0.0 ||
        isnan(variance_floor) || isinf(variance_floor) || variance_floor <= 0.0) {
        return NAN;
    }
    double v = (expected > variance_floor) ? expected : variance_floor;
    return ((double)observed - expected) / sqrt(v);
}

Status anomaly_record_arrival(AnomalyDetector *d, size_t stop) {
    if (!d || stop >= d->max_stops) {
        return (d == NULL) ? STATUS_ERR_NULL : STATUS_ERR_INVALID;
    }
    d->counts[stop]++;
    return STATUS_OK;
}

int anomaly_observed(const AnomalyDetector *d, size_t stop) {
    if (!d || stop >= d->max_stops) {
        return -1;
    }
    return d->counts[stop];
}

Status anomaly_close_window(AnomalyDetector *d, double now_min, const double *expected,
                           AnomalyFlag *out, size_t max_out, size_t *n_out) {
    if (!d || !expected || !n_out) {
        return STATUS_ERR_NULL;
    }

    *n_out = 0;
    size_t flag_count = 0;

    for (size_t s = 0; s < d->max_stops; s++) {
        int obs = d->counts[s];
        double exp = expected[s];
        double z = anomaly_zscore(obs, exp, d->cfg.variance_floor);

        if (!isnan(z) && z > d->cfg.z_threshold && obs >= d->cfg.min_observed) {
            if ((now_min - d->last_flag_time[s]) >= d->cfg.cooldown_min) {
                if (out && flag_count < max_out) {
                    out[flag_count].stop = s;
                    out[flag_count].observed = obs;
                    out[flag_count].expected = exp;
                    out[flag_count].z_score = z;
                }
                flag_count++;
                d->last_flag_time[s] = now_min;
            }
        }
    }

    /* Reset all window counts */
    for (size_t s = 0; s < d->max_stops; s++) {
        d->counts[s] = 0;
    }

    *n_out = flag_count;

    if (out != NULL && flag_count > max_out) {
        return STATUS_ERR_NOMEM;
    }

    return STATUS_OK;
}
