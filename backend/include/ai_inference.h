#ifndef AI_INFERENCE_H
#define AI_INFERENCE_H

#include <stddef.h>
#include "common.h"
#include "demand_model.h"

/**
 * @file ai_inference.h
 * @brief C inference engine for scikit-learn RandomForestRegressor exported models.
 *
 * NOTE: Functions in this module are NOT thread-safe without external synchronization.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ForestModel ForestModel;

/**
 * Loads and validates a Random Forest model from an exported text file.
 *
 * @param out Output pointer receiving the allocated ForestModel.
 *            Remains NULL on any error with zero memory leaked.
 * @param path Path to model file.
 * @return STATUS_OK on success,
 *         STATUS_ERR_NULL if out or path is NULL,
 *         STATUS_ERR_NOT_FOUND if the file cannot be opened,
 *         STATUS_ERR_INVALID if the file is malformed or invalid,
 *         STATUS_ERR_NOMEM on memory allocation failure.
 */
Status forest_load(ForestModel **out, const char *path);

/**
 * Destroys a ForestModel and frees all associated memory. NULL-safe.
 */
void forest_destroy(ForestModel *m);

/**
 * Evaluates the random forest on a feature vector of length DEMAND_NUM_FEATURES.
 *
 * Conforms to scikit-learn's float32 conversion rule for internal split comparisons.
 *
 * @param m Pointer to ForestModel.
 * @param features Array of DEMAND_NUM_FEATURES finite doubles.
 * @return Predicted value, or NAN if m or features is NULL or any feature is not finite.
 */
double forest_predict(const ForestModel *m, const double features[DEMAND_NUM_FEATURES]);

/**
 * Returns the number of trees in the forest.
 */
size_t forest_tree_count(const ForestModel *m);

/**
 * Returns the total number of nodes across all trees in the forest.
 */
size_t forest_node_count(const ForestModel *m);

#ifdef __cplusplus
}
#endif

#endif /* AI_INFERENCE_H */
