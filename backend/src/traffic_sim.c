#include "traffic_sim.h"
#include <math.h>

static double bump(double t, double mu, double sigma) {
    double x = (t - mu) / sigma;
    return exp(-0.5 * x * x);
}

double traffic_sim_factor(double minutes_since_midnight, RoadClass cls, int rain) {
    /* Wrap into [0, 24) hours */
    double t = minutes_since_midnight / 60.0;
    while (t < 0.0)  t += 24.0;
    while (t >= 24.0) t -= 24.0;

    double intensity = bump(t, 9.0, 1.0);
    double b2 = bump(t, 18.5, 1.25);
    double b3 = 0.2 * bump(t, 13.0, 3.5);

    if (b2 > intensity) intensity = b2;
    if (b3 > intensity) intensity = b3;

    double peak;
    switch (cls) {
        case ROAD_PRIMARY:   peak = 2.6; break;
        case ROAD_SECONDARY: peak = 2.0; break;
        case ROAD_TERTIARY:  peak = 1.5; break;
        default:             peak = 1.3; break;
    }

    double factor = (1.0 + (peak - 1.0) * intensity) * (rain ? 1.2 : 1.0);
    if (factor < 1.0) factor = 1.0;
    if (factor > 5.0) factor = 5.0;
    return factor;
}

Status traffic_sim_apply(Graph *g, double minutes_since_midnight, int rain) {
    if (!g) return STATUS_ERR_NULL;
    size_t n = graph_edge_count(g);
    for (size_t i = 0; i < n; i++) {
        const EdgeMeta *meta = graph_edge_meta(g, (int)i);
        if (!meta) continue;
        double f = traffic_sim_factor(minutes_since_midnight, meta->road_class, rain);
        graph_set_traffic_factor(g, (int)i, f);
    }
    return STATUS_OK;
}
