#include "test_util.h"
#include "traffic_sim.h"
#include "graph.h"
#include <math.h>

#define ASSERT_TRUE(cond) TEST_ASSERT(cond)
#define ASSERT_EQ(a,b)    TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_DOUBLE_EQ(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))

static void test_factor_range(void) {
    /* Factor always in [1.0, 5.0] for all times and classes */
    double times[] = {0, 60, 180, 540, 720, 1110, 1440, 1500};
    RoadClass classes[] = {ROAD_PRIMARY, ROAD_SECONDARY, ROAD_TERTIARY, ROAD_OTHER};
    for (size_t t = 0; t < 8; t++) {
        for (size_t c = 0; c < 4; c++) {
            double f = traffic_sim_factor(times[t], classes[c], 0);
            ASSERT_TRUE(f >= 1.0);
            ASSERT_TRUE(f <= 5.0);
            double fr = traffic_sim_factor(times[t], classes[c], 1);
            ASSERT_TRUE(fr >= 1.0);
            ASSERT_TRUE(fr <= 5.0);
        }
    }
}

static void test_near_one_at_night(void) {
    /* At 03:00 (180 min) factor should be near 1 (below 1.01) */
    double f = traffic_sim_factor(180.0, ROAD_PRIMARY, 0);
    ASSERT_TRUE(f < 1.01);
}

static void test_peak_ordering(void) {
    /* 09:00 > 13:00 > 03:00 for all classes */
    for (int c = 0; c <= 3; c++) {
        double f9  = traffic_sim_factor(9.0 * 60.0, (RoadClass)c, 0);
        double f13 = traffic_sim_factor(13.0 * 60.0, (RoadClass)c, 0);
        double f3  = traffic_sim_factor(3.0 * 60.0, (RoadClass)c, 0);
        ASSERT_TRUE(f9 > f13);
        ASSERT_TRUE(f13 > f3);
    }
    /* 18:30 is also a peak (comparable to 09:00) */
    double f9  = traffic_sim_factor(9.0 * 60.0, ROAD_PRIMARY, 0);
    double f18 = traffic_sim_factor(18.5 * 60.0, ROAD_PRIMARY, 0);
    ASSERT_TRUE(f18 > 1.5);
    ASSERT_TRUE(f9 > 1.5);
}

static void test_primary_vs_tertiary(void) {
    /* Primary peak > tertiary peak at peak times */
    double fp = traffic_sim_factor(9.0 * 60.0, ROAD_PRIMARY, 0);
    double ft = traffic_sim_factor(9.0 * 60.0, ROAD_TERTIARY, 0);
    ASSERT_TRUE(fp > ft);
}

static void test_rain_multiplier(void) {
    /* Rain raises by exactly 1.2x before clamping. Use an unclamped time. */
    /* At 03:00, factor is near 1 (dry), so 1.2x won't clamp */
    double f_dry  = traffic_sim_factor(3.0 * 60.0, ROAD_TERTIARY, 0);
    double f_rain = traffic_sim_factor(3.0 * 60.0, ROAD_TERTIARY, 1);
    /* Both should be below 5.0 at off-peak hours */
    ASSERT_TRUE(f_rain < 5.0);
    ASSERT_DOUBLE_EQ(f_rain, f_dry * 1.2, 1e-9);
}

static void test_determinism(void) {
    double f1 = traffic_sim_factor(540.0, ROAD_PRIMARY, 0);
    double f2 = traffic_sim_factor(540.0, ROAD_PRIMARY, 0);
    ASSERT_DOUBLE_EQ(f1, f2, 1e-15);
}

static void test_apply_sets_all_edges_not_blocked(void) {
    Graph *g = graph_create();
    graph_add_node(g, 1, 12.0, 77.0, "A", 0);
    graph_add_node(g, 2, 12.1, 77.1, "B", 0);
    graph_add_node(g, 3, 12.2, 77.2, "C", 0);
    graph_add_edge(g, 0, 1, 1000.0, 100.0, "P", ROAD_PRIMARY);
    graph_add_edge(g, 1, 2, 500.0, 50.0, "T", ROAD_TERTIARY);
    graph_set_blocked(g, 0, 1);

    Status st = traffic_sim_apply(g, 9.0 * 60.0, 0);
    ASSERT_EQ(st, STATUS_OK);

    /* Blocked flag should be unchanged */
    ASSERT_EQ(graph_edge(g, 0)->blocked, 1);

    /* Factors should match traffic_sim_factor */
    double expected0 = traffic_sim_factor(9.0 * 60.0, ROAD_PRIMARY, 0);
    double expected1 = traffic_sim_factor(9.0 * 60.0, ROAD_TERTIARY, 0);
    ASSERT_DOUBLE_EQ(graph_edge(g, 0)->traffic_factor, expected0, 1e-9);
    ASSERT_DOUBLE_EQ(graph_edge(g, 1)->traffic_factor, expected1, 1e-9);

    graph_destroy(g);
}

int main(void) {
    RUN_TEST(test_factor_range);
    RUN_TEST(test_near_one_at_night);
    RUN_TEST(test_peak_ordering);
    RUN_TEST(test_primary_vs_tertiary);
    RUN_TEST(test_rain_multiplier);
    RUN_TEST(test_determinism);
    RUN_TEST(test_apply_sets_all_edges_not_blocked);
    return test_summary();
}
