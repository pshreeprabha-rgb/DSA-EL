#include "test_util.h"
#include "network.h"
#include "demand_model.h"
#include "ai_demand.h"
#include "graph.h"
#include "pathfind.h"
#include "reroute.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ASSERT_TRUE(c) TEST_ASSERT(c)
#define ASSERT_EQ(a,b) TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p) TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_DOUBLE_EQ(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))

/* Helper to build the line graph fixture */
static Graph *create_line_graph(DemandStop stops[8]) {
    Graph *g = graph_create();
    if (!g) return NULL;

    const double LAT = 12.9700;
    const double LNG0 = 77.5700;

    for (int i = 0; i < 8; i++) {
        double lng = LNG0 + (double)i * 0.001;
        char name[16];
        snprintf(name, sizeof(name), "S%d", i);
        graph_add_node(g, (int64_t)(1000 + i), LAT, lng, name, 1);

        stops[i].node = i;
        stops[i].osm_id = (int64_t)(1000 + i);
        stops[i].type = (StopType)(i % 4);
        stops[i].scale = 1.0;
        stops[i].lat = LAT;
        stops[i].lng = lng;
        snprintf(stops[i].name, sizeof(stops[i].name), "%s", name);
    }

    for (int i = 0; i < 7; i++) {
        double d = graph_haversine_m(LAT, LNG0 + (double)i * 0.001,
                                     LAT, LNG0 + (double)(i + 1) * 0.001);
        char e_fwd[16], e_rev[16];
        snprintf(e_fwd, sizeof(e_fwd), "e%d_%d", i, i + 1);
        snprintf(e_rev, sizeof(e_rev), "e%d_%d", i + 1, i);
        graph_add_edge(g, i, i + 1, d, 100.0, e_fwd, ROAD_PRIMARY);
        graph_add_edge(g, i + 1, i, d, 100.0, e_rev, ROAD_PRIMARY);
    }

    return g;
}

/* 1. Line graph hand-derived tour and cycles test */
static void test_network_line_graph(void) {
    DemandStop stops[8];
    Graph *g = create_line_graph(stops);
    ASSERT_NOT_NULL(g);

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = 2;
    cfg.dwell_s_per_stop = 20.0;
    cfg.build_matrix = 1;

    NetworkModel *net = NULL;
    Status st = network_build(g, stops, 8, &cfg, &net);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(net);

    /*
     * Hand derivation:
     * 8 stops sorted by (lng asc, lat asc, index asc) gives indices 0,1,2,3,4,5,6,7.
     * With n_routes = 2, group sizes are 8/2 = 4 for each route.
     * Route 0 gets {0, 1, 2, 3}. All have identical latitude, so smallest (lat, index) is stop 0.
     * Nearest-neighbour from 0 along the line: 0 -> 1 -> 2 -> 3.
     * Route 1 gets {4, 5, 6, 7}. Smallest (lat, index) is stop 4.
     * Nearest-neighbour from 4 along the line: 4 -> 5 -> 6 -> 7.
     */
    ASSERT_EQ(network_route_count(net), 2);
    ASSERT_EQ(network_stop_count(net), 8);

    ASSERT_EQ(network_route_stop_count(net, 0), 4);
    ASSERT_EQ(network_route_stop_count(net, 1), 4);

    for (size_t k = 0; k < 4; k++) {
        ASSERT_EQ(network_route_stop(net, 0, k), (int)k);
        ASSERT_EQ(network_stop_route(net, k), 0);
        ASSERT_EQ(network_stop_pos(net, k), (int)k);

        ASSERT_EQ(network_route_stop(net, 1, k), (int)(k + 4));
        ASSERT_EQ(network_stop_route(net, k + 4), 1);
        ASSERT_EQ(network_stop_pos(net, k + 4), (int)k);
    }

    /*
     * Leg times:
     * Route 0:
     *   Leg 0: 0 -> 1 = 100 s
     *   Leg 1: 1 -> 2 = 100 s
     *   Leg 2: 2 -> 3 = 100 s
     *   Leg 3: 3 -> 0 = 300 s (closing loop leg)
     *   Total loop time = 100 + 100 + 100 + 300 = 600 s.
     *   Cycle time = 600 + 4 * 20 = 680 s.
     * Route 1: identical = 600 s loop, 680 s cycle.
     */
    ASSERT_DOUBLE_EQ(network_leg_time_s(net, 0, 0, COST_STATIC), 100.0, 1e-9);
    ASSERT_DOUBLE_EQ(network_leg_time_s(net, 0, 1, COST_STATIC), 100.0, 1e-9);
    ASSERT_DOUBLE_EQ(network_leg_time_s(net, 0, 2, COST_STATIC), 100.0, 1e-9);
    ASSERT_DOUBLE_EQ(network_leg_time_s(net, 0, 3, COST_STATIC), 300.0, 1e-9);

    ASSERT_DOUBLE_EQ(network_route_loop_time_s(net, 0, COST_STATIC), 600.0, 1e-9);
    ASSERT_DOUBLE_EQ(network_route_loop_time_s(net, 1, COST_STATIC), 600.0, 1e-9);

    ASSERT_DOUBLE_EQ(network_route_cycle_s(net, 0), 680.0, 1e-9);
    ASSERT_DOUBLE_EQ(network_route_cycle_s(net, 1), 680.0, 1e-9);

    double cycles[2];
    ASSERT_EQ(network_route_cycles(net, cycles), STATUS_OK);
    ASSERT_DOUBLE_EQ(cycles[0], 680.0, 1e-9);
    ASSERT_DOUBLE_EQ(cycles[1], 680.0, 1e-9);

    /* Route names and leg IDs */
    const char *r0_name = network_route_name(net, 0);
    const char *r1_name = network_route_name(net, 1);
    ASSERT_NOT_NULL(r0_name);
    ASSERT_NOT_NULL(r1_name);
    ASSERT_TRUE(strlen(r0_name) > 0 && strlen(r0_name) <= 63);
    ASSERT_TRUE(strlen(r1_name) > 0 && strlen(r1_name) <= 63);

    for (size_t r = 0; r < 2; r++) {
        for (size_t k = 0; k < 4; k++) {
            ASSERT_EQ(network_leg_route_id(net, r, k), (int)(r * 1000 + k));
            ASSERT_NOT_NULL(network_leg_path(net, r, k));
        }
    }

    network_destroy(net);
    graph_destroy(g);
}

/* 2. Matrix on the line graph */
static void test_network_matrix_line_graph(void) {
    DemandStop stops[8];
    Graph *g = create_line_graph(stops);
    ASSERT_NOT_NULL(g);

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = 2;
    cfg.build_matrix = 1;

    NetworkModel *net = NULL;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_OK);

    /* network_stop_time_s(a, b) = 100 * |a - b| for ALL pairs */
    for (int a = 0; a < 8; a++) {
        for (int b = 0; b < 8; b++) {
            double expected = 100.0 * (double)abs(a - b);
            ASSERT_DOUBLE_EQ(network_stop_time_s(net, (size_t)a, (size_t)b), expected, 1e-9);
        }
    }

    /*
     * Entry time assertions from specification:
     * - network_entry_time_s(1, 1, &e) = 300 with e = 4 (stops of route 1 are {4,5,6,7}; nearest from 1 is stop 4, dist 300)
     * - network_entry_time_s(6, 0, &e) = 300 with e = 3 (stops of route 0 are {0,1,2,3}; nearest from 6 is stop 3, dist 300)
     * - network_entry_time_s(5, 1, &e) = 0 with e = 5 (stop 5 is on route 1; distance to itself is 0)
     */
    int e = -1;
    double t1 = network_entry_time_s(net, 1, 1, &e);
    ASSERT_DOUBLE_EQ(t1, 300.0, 1e-9);
    ASSERT_EQ(e, 4);

    e = -1;
    double t2 = network_entry_time_s(net, 6, 0, &e);
    ASSERT_DOUBLE_EQ(t2, 300.0, 1e-9);
    ASSERT_EQ(e, 3);

    e = -1;
    double t3 = network_entry_time_s(net, 5, 1, &e);
    ASSERT_DOUBLE_EQ(t3, 0.0, 1e-9);
    ASSERT_EQ(e, 5);

    network_destroy(net);

    /* With build_matrix = 0: network_stop_time_s and network_entry_time_s return -1.0 */
    cfg.build_matrix = 0;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_OK);
    ASSERT_DOUBLE_EQ(network_stop_time_s(net, 1, 2), -1.0, 1e-9);
    e = 99;
    ASSERT_DOUBLE_EQ(network_entry_time_s(net, 1, 1, &e), -1.0, 1e-9);
    ASSERT_EQ(e, -1);

    network_destroy(net);
    graph_destroy(g);
}

/* 3. Grid fixture 12x12 with 24 stops and 4 routes */
static void test_network_grid_fixture(void) {
    Graph *g = NULL;
    ASSERT_EQ(graph_build_synthetic_grid(&g, 12, 12, 12.9, 77.5, 100.0, 7), STATUS_OK);
    ASSERT_NOT_NULL(g);

    /* Choose 24 deterministic stops on the 144-node grid */
    DemandStop stops[24];
    for (int i = 0; i < 24; i++) {
        int node = i * 6 + (i % 3);
        const GraphNode *gn = graph_node(g, node);
        ASSERT_NOT_NULL(gn);

        stops[i].node = node;
        stops[i].osm_id = (int64_t)(50000 + node);
        stops[i].type = (StopType)(i % 4);
        stops[i].scale = 1.0;
        stops[i].lat = gn->lat;
        stops[i].lng = gn->lng;
        snprintf(stops[i].name, sizeof(stops[i].name), "GridStop_%d", node);
    }

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = 4;
    cfg.build_matrix = 1;

    NetworkModel *net = NULL;
    ASSERT_EQ(network_build(g, stops, 24, &cfg, &net), STATUS_OK);
    ASSERT_NOT_NULL(net);

    ASSERT_EQ(network_route_count(net), 4);
    ASSERT_EQ(network_stop_count(net), 24);

    /* Every route size is 6 (24 / 4 = 6) */
    char stop_assigned[24] = {0};
    for (size_t r = 0; r < 4; r++) {
        ASSERT_EQ(network_route_stop_count(net, r), 6);
        for (size_t k = 0; k < 6; k++) {
            int s = network_route_stop(net, r, k);
            ASSERT_TRUE(s >= 0 && s < 24);
            ASSERT_EQ(stop_assigned[s], 0);
            stop_assigned[s] = 1;
            ASSERT_EQ(network_stop_route(net, (size_t)s), (int)r);
            ASSERT_EQ(network_stop_pos(net, (size_t)s), (int)k);

            /* Path validity and endpoints */
            const PathResult *path = network_leg_path(net, r, k);
            ASSERT_NOT_NULL(path);
            ASSERT_TRUE(path_is_valid(g, path));

            int next_s = network_route_stop(net, r, (k + 1) % 6);
            ASSERT_EQ(path->nodes[0], stops[s].node);
            ASSERT_EQ(path->nodes[path->node_count - 1], stops[next_s].node);
        }

        /* Loop time equals sum of leg times */
        double loop_t = network_route_loop_time_s(net, r, COST_STATIC);
        double sum_legs = 0.0;
        for (size_t k = 0; k < 6; k++) {
            sum_legs += network_leg_time_s(net, r, k, COST_STATIC);
        }
        ASSERT_DOUBLE_EQ(loop_t, sum_legs, 1e-9);
    }

    /* All 24 stops assigned */
    for (int i = 0; i < 24; i++) {
        ASSERT_EQ(stop_assigned[i], 1);
    }

    /* Building twice gives identical routes and times (determinism) */
    NetworkModel *net2 = NULL;
    ASSERT_EQ(network_build(g, stops, 24, &cfg, &net2), STATUS_OK);
    for (size_t r = 0; r < 4; r++) {
        ASSERT_DOUBLE_EQ(network_route_cycle_s(net, r), network_route_cycle_s(net2, r), 1e-9);
        for (size_t k = 0; k < 6; k++) {
            ASSERT_EQ(network_route_stop(net, r, k), network_route_stop(net2, r, k));
        }
    }

    network_destroy(net2);
    network_destroy(net);
    graph_destroy(g);
}

/* 4. Integration with Part 2: RouteManager edge blocking and undo */
static void test_network_reroute_integration(void) {
    Graph *g = NULL;
    ASSERT_EQ(graph_build_synthetic_grid(&g, 10, 10, 12.9, 77.5, 100.0, 42), STATUS_OK);

    DemandStop stops[8];
    for (int i = 0; i < 8; i++) {
        int node = i * 10;
        const GraphNode *gn = graph_node(g, node);
        stops[i].node = node;
        stops[i].osm_id = (int64_t)(1000 + i);
        stops[i].type = STOP_HUB;
        stops[i].scale = 1.0;
        stops[i].lat = gn->lat;
        stops[i].lng = gn->lng;
        snprintf(stops[i].name, sizeof(stops[i].name), "Stop_%d", i);
    }

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = 2;
    cfg.build_matrix = 0;

    NetworkModel *net = NULL;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_OK);

    RouteManager *rm = network_route_manager(net);
    ASSERT_NOT_NULL(rm);

    /* Get original path of leg 0 of route 0 */
    const PathResult *orig_path = network_leg_path(net, 0, 0);
    ASSERT_NOT_NULL(orig_path);
    ASSERT_TRUE(orig_path->edge_count > 0);

    int orig_edge = orig_path->edges[0];
    size_t orig_nodes_count = orig_path->node_count;
    int *saved_nodes = (int *)malloc(orig_nodes_count * sizeof(int));
    memcpy(saved_nodes, orig_path->nodes, orig_nodes_count * sizeof(int));

    /* Block the first edge */
    RerouteSummary summary;
    ASSERT_EQ(rm_block_edge(rm, orig_edge, &summary), STATUS_OK);

    const PathResult *new_path = network_leg_path(net, 0, 0);
    ASSERT_NOT_NULL(new_path);
    ASSERT_TRUE(path_is_valid(g, new_path));
    /* New path does not contain the blocked edge */
    for (size_t e = 0; e < new_path->edge_count; e++) {
        ASSERT_TRUE(new_path->edges[e] != orig_edge);
    }

    /* Undo restores original path and unblocks the edge */
    ASSERT_EQ(rm_undo(rm, NULL, 0), STATUS_OK);
    const PathResult *restored_path = network_leg_path(net, 0, 0);
    ASSERT_NOT_NULL(restored_path);
    ASSERT_EQ(restored_path->node_count, orig_nodes_count);
    for (size_t n = 0; n < orig_nodes_count; n++) {
        ASSERT_EQ(restored_path->nodes[n], saved_nodes[n]);
    }
    ASSERT_EQ(graph_edge(g, orig_edge)->blocked, 0);

    free(saved_nodes);
    network_destroy(net);
    graph_destroy(g);
}

/* 5. Matrix versus A* search check on 30 pairs */
static void test_network_matrix_vs_astar(void) {
    Graph *g = NULL;
    ASSERT_EQ(graph_build_synthetic_grid(&g, 10, 10, 12.9, 77.5, 100.0, 99), STATUS_OK);

    DemandStop stops[10];
    for (int i = 0; i < 10; i++) {
        int node = i * 9;
        const GraphNode *gn = graph_node(g, node);
        stops[i].node = node;
        stops[i].osm_id = (int64_t)(2000 + i);
        stops[i].type = STOP_OFFICE;
        stops[i].scale = 1.0;
        stops[i].lat = gn->lat;
        stops[i].lng = gn->lng;
        snprintf(stops[i].name, sizeof(stops[i].name), "Stop_%d", i);
    }

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = 2;
    cfg.build_matrix = 1;

    NetworkModel *net = NULL;
    ASSERT_EQ(network_build(g, stops, 10, &cfg, &net), STATUS_OK);

    /* 30 deterministic pairs */
    int pair_count = 0;
    for (int a = 0; a < 10 && pair_count < 30; a++) {
        for (int b = 0; b < 10 && pair_count < 30; b++) {
            double mat_t = network_stop_time_s(net, (size_t)a, (size_t)b);
            if (a == b) {
                ASSERT_DOUBLE_EQ(mat_t, 0.0, 1e-9);
            } else {
                PathResult res;
                path_result_init(&res);
                ASSERT_EQ(find_path(g, stops[a].node, stops[b].node, SEARCH_ASTAR, COST_STATIC, &res), STATUS_OK);
                ASSERT_TRUE(res.found);
                ASSERT_DOUBLE_EQ(mat_t, res.total_time_s, 1e-9);
                path_result_free(&res);
            }
            pair_count++;
        }
    }

    network_destroy(net);
    graph_destroy(g);
}

/* 6. network_route_demands test: oracle vs predictor vs event impact */
static void test_network_route_demands(void) {
    DemandStop stops[8];
    Graph *g = create_line_graph(stops);

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = 2;
    cfg.build_matrix = 0;

    NetworkModel *net = NULL;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_OK);

    /* dp == NULL: oracle demands */
    double demands_base[2];
    ASSERT_EQ(network_route_demands(net, NULL, 8, DAY_WEEKDAY, 0, NULL, 0, demands_base), STATUS_OK);

    /* Independent sum calculation for verification */
    for (size_t r = 0; r < 2; r++) {
        double expected_sum = 0.0;
        for (size_t k = 0; k < 4; k++) {
            int s = network_route_stop(net, r, k);
            expected_sum += demand_expected(stops[s].type, stops[s].scale, 8, DAY_WEEKDAY, 0, 0);
        }
        ASSERT_DOUBLE_EQ(demands_base[r], expected_sum, 1e-9);
    }

    /* Event on stop 1 (which belongs to route 0) */
    int ev_stops[1] = { 1 };
    double demands_ev[2];
    ASSERT_EQ(network_route_demands(net, NULL, 8, DAY_WEEKDAY, 0, ev_stops, 1, demands_ev), STATUS_OK);

    double diff_r0 = demands_ev[0] - demands_base[0];
    double expected_diff = demand_expected(stops[1].type, stops[1].scale, 8, DAY_WEEKDAY, 0, 1) -
                           demand_expected(stops[1].type, stops[1].scale, 8, DAY_WEEKDAY, 0, 0);
    ASSERT_DOUBLE_EQ(diff_r0, expected_diff, 1e-9);
    ASSERT_DOUBLE_EQ(demands_ev[1], demands_base[1], 1e-9); /* Route 1 unaffected */

    /* Moving-average predictor trained on synthetic history */
    DemandRow *rows = NULL;
    size_t n_rows = 0;
    ASSERT_EQ(demand_generate(stops, 8, 42, 3, &rows, &n_rows), STATUS_OK);
    DemandPredictor *dp = dp_train_moving_avg(rows, n_rows);
    ASSERT_NOT_NULL(dp);

    double demands_pred[2];
    ASSERT_EQ(network_route_demands(net, dp, 8, DAY_WEEKDAY, 0, ev_stops, 1, demands_pred), STATUS_OK);

    for (size_t r = 0; r < 2; r++) {
        double exp_pred_sum = 0.0;
        for (size_t k = 0; k < 4; k++) {
            int s = network_route_stop(net, r, k);
            exp_pred_sum += dp_predict(dp, stops[s].node, stops[s].type, stops[s].scale, 8, DAY_WEEKDAY, 0, 0);
        }
        ASSERT_DOUBLE_EQ(demands_pred[r], exp_pred_sum, 1e-9);
    }

    /* Invalid hours */
    ASSERT_EQ(network_route_demands(net, NULL, 24, DAY_WEEKDAY, 0, NULL, 0, demands_base), STATUS_ERR_INVALID);
    ASSERT_EQ(network_route_demands(net, NULL, -1, DAY_WEEKDAY, 0, NULL, 0, demands_base), STATUS_ERR_INVALID);

    dp_destroy(dp);
    free(rows);
    network_destroy(net);
    graph_destroy(g);
}

/* 7. Error handling tests */
static void test_network_errors(void) {
    DemandStop stops[8];
    Graph *g = create_line_graph(stops);

    NetworkConfig cfg;
    network_config_default(&cfg);
    NetworkModel *net = NULL;

    /* NULL arguments */
    ASSERT_EQ(network_build(NULL, stops, 8, &cfg, &net), STATUS_ERR_NULL);
    ASSERT_EQ(network_build(g, NULL, 8, &cfg, &net), STATUS_ERR_NULL);
    ASSERT_EQ(network_build(g, stops, 8, &cfg, NULL), STATUS_ERR_NULL);
    ASSERT_NULL(net);

    /* n_stops < 2 */
    ASSERT_EQ(network_build(g, stops, 1, &cfg, &net), STATUS_ERR_INVALID);
    ASSERT_NULL(net);

    /* n_routes == 0 */
    cfg.n_routes = 0;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_ERR_INVALID);
    ASSERT_NULL(net);

    /* n_routes > n_stops / 2 (8 stops / 2 = 4, try 5) */
    cfg.n_routes = 5;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_ERR_INVALID);
    ASSERT_NULL(net);

    /* Duplicate stop node */
    cfg.n_routes = 2;
    stops[1].node = stops[0].node;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_ERR_INVALID);
    ASSERT_NULL(net);
    stops[1].node = 1;

    /* Node outside graph */
    stops[1].node = 9999;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_ERR_INVALID);
    ASSERT_NULL(net);
    stops[1].node = 1;

    /* Negative dwell */
    cfg.dwell_s_per_stop = -5.0;
    ASSERT_EQ(network_build(g, stops, 8, &cfg, &net), STATUS_ERR_INVALID);
    ASSERT_NULL(net);
    cfg.dwell_s_per_stop = 20.0;

    /* Accessors with invalid indices on NULL model */
    ASSERT_NULL(network_graph(NULL));
    ASSERT_NULL(network_route_manager(NULL));
    ASSERT_EQ(network_route_count(NULL), 0);
    ASSERT_EQ(network_stop_count(NULL), 0);
    ASSERT_NULL(network_stop(NULL, 0));
    ASSERT_EQ(network_route_stop_count(NULL, 0), 0);
    ASSERT_EQ(network_route_stop(NULL, 0, 0), -1);
    ASSERT_EQ(network_stop_route(NULL, 0), -1);
    ASSERT_EQ(network_stop_pos(NULL, 0), -1);
    ASSERT_NULL(network_route_name(NULL, 0));
    ASSERT_EQ(network_leg_route_id(NULL, 0, 0), -1);
    ASSERT_NULL(network_leg_path(NULL, 0, 0));
    ASSERT_DOUBLE_EQ(network_leg_time_s(NULL, 0, 0, COST_STATIC), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(network_route_loop_time_s(NULL, 0, COST_STATIC), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(network_route_cycle_s(NULL, 0), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(network_stop_time_s(NULL, 0, 1), -1.0, 1e-9);

    /* destroy(NULL) is safe */
    network_destroy(NULL);

    graph_destroy(g);
}

int main(void) {
    RUN_TEST(test_network_line_graph);
    RUN_TEST(test_network_matrix_line_graph);
    RUN_TEST(test_network_grid_fixture);
    RUN_TEST(test_network_reroute_integration);
    RUN_TEST(test_network_matrix_vs_astar);
    RUN_TEST(test_network_route_demands);
    RUN_TEST(test_network_errors);

    return test_summary();
}
