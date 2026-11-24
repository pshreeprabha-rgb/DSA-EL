#ifndef AI_DEMAND_H
#define AI_DEMAND_H

#include <stddef.h>
#include "common.h"
#include "types.h"
#include "demand_model.h"
#include "ai_inference.h"

/**
 * @file ai_demand.h
 * @brief Demand predictors (Moving Average baseline, OLS per time band, Random Forest)
 *        and unified evaluation framework.
 *
 * NOTE: Functions in this module are NOT thread-safe without external synchronization.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DemandModelKind {
    DEMAND_MODEL_MOVING_AVG = 0,
    DEMAND_MODEL_OLS_BAND,
    DEMAND_MODEL_FOREST
} DemandModelKind;

typedef struct DemandPredictor DemandPredictor;

typedef struct DemandEval {
    size_t n;
    double mae_noisy;
    double rmse_noisy;
    double wape_noisy;
    double mae_lambda;
} DemandEval;

/**
 * Trains a moving average predictor using historical demand rows.
 * Falls back to (type, hour, day_type), then hour, then 0.0 for unseen stops.
 */
DemandPredictor *dp_train_moving_avg(const DemandRow *rows, size_t n);

/**
 * Trains an Ordinary Least Squares linear regression per time band with ridge regularisation.
 */
DemandPredictor *dp_train_ols_band(const DemandRow *rows, size_t n);

/**
 * Creates a DemandPredictor backed by an exported RandomForest model.
 * Takes ownership of fm (destroyed when predictor is destroyed).
 * Returns NULL if fm is NULL.
 */
DemandPredictor *dp_from_forest(ForestModel *fm);

/**
 * Destroys a DemandPredictor and frees its internal resources. NULL-safe.
 */
void dp_destroy(DemandPredictor *p);

/**
 * Predicts expected passenger arrivals per hour for a given stop and context.
 * Returns -1.0 on invalid arguments (hour outside 0..23, scale <= 0, etc.).
 */
double dp_predict(const DemandPredictor *p, int stop_node, StopType type, double scale, int hour, DayType day_type, int rain, int event);

/**
 * Returns the predictor kind.
 */
DemandModelKind dp_kind(const DemandPredictor *p);

/**
 * Returns the predictor display name ("moving_average", "ols_per_band", "random_forest").
 */
const char *dp_name(const DemandPredictor *p);

/**
 * Evaluates a predictor against a test dataset, returning overall and per-category metrics.
 * by_category may be NULL.
 */
Status dp_evaluate(const DemandPredictor *p, const DemandRow *rows, size_t n, DemandEval *overall, DemandEval by_category[DEMAND_NUM_CATEGORIES]);

#ifdef __cplusplus
}
#endif

#endif /* AI_DEMAND_H */
