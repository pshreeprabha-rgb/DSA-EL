#ifndef TRAFFIC_SIM_H
#define TRAFFIC_SIM_H

#include "graph.h"
#include "common.h"

/**
 * Computes a deterministic traffic factor for a road class at a given time of day.
 *
 * Uses a Gaussian bump model for rush-hour peaks (09:00, 18:30) and a mid-day
 * shoulder (13:00). Factor is clamped to [1.0, 5.0].
 *
 * @param minutes_since_midnight Minutes since midnight (wrapped into [0, 1440)).
 * @param cls Road class (determines peak intensity).
 * @param rain 1 to apply 1.2x rain multiplier (before clamping), 0 otherwise.
 * @return Traffic factor in [1.0, 5.0].
 * Complexity: O(1)
 */
double traffic_sim_factor(double minutes_since_midnight, RoadClass cls, int rain);

/**
 * Applies simulated traffic factors to every edge in the graph based on each
 * edge's road class and the given time. Does not modify blocked flags.
 *
 * @param g Graph pointer.
 * @param minutes_since_midnight Minutes since midnight.
 * @param rain 1 to apply rain multiplier.
 * @return STATUS_OK on success, STATUS_ERR_NULL if g is NULL.
 * Complexity: O(E)
 */
Status traffic_sim_apply(Graph *g, double minutes_since_midnight, int rain);

#endif /* TRAFFIC_SIM_H */
