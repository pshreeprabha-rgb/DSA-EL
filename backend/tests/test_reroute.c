#include "test_util.h"
#include "reroute.h"
#include "graph.h"
#include "pathfind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ASSERT_TRUE(cond) TEST_ASSERT(cond)
#define ASSERT_EQ(a,b)    TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p)    TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_DOUBLE_EQ(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))

/*
 * 6-node graph: A=0 B=1 C=2 D=3 E=4 F=5
 * Bidirectional: A-B:4, A-C:2, B-C:1, B-D:5, C-D:8, C-E:10, D-E:2, D-F:6, E-F:3
 */
static Graph *build_6node(int *edge_bd_out, int *edge_ac_out,
                           int *edge_df_out, int *edge_ef_out) {
    Graph *g = graph_create();
    graph_add_node(g, 1, 12.00, 77.00, "A", 0);
    graph_add_node(g, 2, 12.01, 77.01, "B", 0);
    graph_add_node(g, 3, 12.02, 77.02, "C", 0);
    graph_add_node(g, 4, 12.03, 77.03, "D", 0);
    graph_add_node(g, 5, 12.04, 77.04, "E", 0);
    graph_add_node(g, 6, 12.05, 77.05, "F", 0);

    graph_add_bidirectional(g, 0, 1, 40.0, 4.0, "A-B", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 0, 2, 20.0, 2.0, "A-C", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 1, 2, 10.0, 1.0, "B-C", ROAD_PRIMARY, NULL, NULL);
    int e_bd = -1;
    graph_add_bidirectional(g, 1, 3, 50.0, 5.0, "B-D", ROAD_PRIMARY, &e_bd, NULL);
    graph_add_bidirectional(g, 2, 3, 80.0, 8.0, "C-D", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 2, 4, 100.0, 10.0, "C-E", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 3, 4, 20.0, 2.0, "D-E", ROAD_PRIMARY, NULL, NULL);
    int e_df = -1;
    graph_add_bidirectional(g, 3, 5, 60.0, 6.0, "D-F", ROAD_PRIMARY, &e_df, NULL);
    int e_ef = -1;
    graph_add_bidirectional(g, 4, 5, 30.0, 3.0, "E-F", ROAD_PRIMARY, &e_ef, NULL);

    /* Find A->C edge */
    int e_ac = -1;
    for (size_t k = 0; k < graph_out_degree(g, 0); k++) {
        int eid = graph_out_edge(g, 0, k);
        if (graph_edge(g, eid)->to == 2) { e_ac = eid; break; }
    }

    if (edge_bd_out) *edge_bd_out = e_bd;
    if (edge_ac_out) *edge_ac_out = e_ac;
    if (edge_df_out) *edge_df_out = e_df;
    if (edge_ef_out) *edge_ef_out = e_ef;
    return g;
}

static void test_basic_track_and_path(void) {
    int edge_bd, edge_ac;
    Graph *g = build_6node(&edge_bd, &edge_ac, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);
    ASSERT_NOT_NULL(rm);

    Status st = rm_track_route(rm, 10, 0, 5);
    ASSERT_EQ(st, STATUS_OK);

    const PathResult *p = rm_get_path(rm, 10);
    ASSERT_NOT_NULL(p);
    ASSERT_TRUE(p->found);
    ASSERT_DOUBLE_EQ(p->total_time_s, 13.0, 1e-9);
    ASSERT_EQ(p->node_count, 6);

    /* Duplicate track */
    ASSERT_EQ(rm_track_route(rm, 10, 0, 5), STATUS_ERR_DUPLICATE);

    ASSERT_EQ(rm_history_size(rm), 0);

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_block_reroute_undo(void) {
    int edge_bd, edge_ac;
    Graph *g = build_6node(&edge_bd, &edge_ac, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);

    rm_track_route(rm, 1, 0, 5);

    /* Store original nodes */
    const PathResult *p_orig = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p_orig);
    ASSERT_DOUBLE_EQ(p_orig->total_time_s, 13.0, 1e-9);
    int orig_nodes[6];
    size_t orig_nc = p_orig->node_count;
    for (size_t i = 0; i < orig_nc; i++) orig_nodes[i] = p_orig->nodes[i];
    int orig_edges[5];
    for (size_t i = 0; i < p_orig->edge_count; i++) orig_edges[i] = p_orig->edges[i];

    RerouteSummary sum;
    Status st = rm_block_edge(rm, edge_bd, &sum);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(sum.pushed_history, 1);
    ASSERT_EQ(rm_history_size(rm), 1);

    const PathResult *p2 = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p2);
    ASSERT_TRUE(p2->found);
    ASSERT_DOUBLE_EQ(p2->total_time_s, 15.0, 1e-9);

    /* Edge B->D should be blocked */
    ASSERT_EQ(graph_edge(g, edge_bd)->blocked, 1);

    /* Undo */
    char desc[128];
    st = rm_undo(rm, desc, sizeof(desc));
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(rm_history_size(rm), 0);

    /* Edge should be unblocked */
    ASSERT_EQ(graph_edge(g, edge_bd)->blocked, 0);

    /* Path should be restored exactly */
    const PathResult *p_restored = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p_restored);
    ASSERT_TRUE(p_restored->found);
    ASSERT_DOUBLE_EQ(p_restored->total_time_s, 13.0, 1e-9);
    ASSERT_EQ(p_restored->node_count, orig_nc);
    for (size_t i = 0; i < orig_nc; i++) {
        ASSERT_EQ(p_restored->nodes[i], orig_nodes[i]);
    }
    for (size_t i = 0; i < p_restored->edge_count; i++) {
        ASSERT_EQ(p_restored->edges[i], orig_edges[i]);
    }

    /* Undo on empty */
    ASSERT_EQ(rm_undo(rm, NULL, 0), STATUS_ERR_EMPTY);

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_block_edge_no_route_uses_it(void) {
    int edge_bd, edge_ac;
    Graph *g = build_6node(&edge_bd, &edge_ac, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);

    /* Track A->B: shortest = direct A->B = 4 */
    rm_track_route(rm, 1, 0, 1);

    /* Block D->F which route A->B doesn't use */
    int edge_df = -1;
    for (size_t k = 0; k < graph_out_degree(g, 3); k++) {
        int eid = graph_out_edge(g, 3, k);
        if (graph_edge(g, eid)->to == 5) { edge_df = eid; break; }
    }
    ASSERT_TRUE(edge_df >= 0);

    RerouteSummary sum;
    Status st = rm_block_edge(rm, edge_df, &sum);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(sum.pushed_history, 1);
    ASSERT_EQ(sum.routes_rerouted, 0);
    ASSERT_EQ(rm_history_size(rm), 1);

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_block_already_blocked_noop(void) {
    int edge_bd;
    Graph *g = build_6node(&edge_bd, NULL, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);
    rm_track_route(rm, 1, 0, 5);

    rm_block_edge(rm, edge_bd, NULL);
    ASSERT_EQ(rm_history_size(rm), 1);

    /* Block again - no-op */
    RerouteSummary sum;
    rm_block_edge(rm, edge_bd, &sum);
    ASSERT_EQ(sum.pushed_history, 0);
    ASSERT_EQ(rm_history_size(rm), 1);

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_traffic_factor_threshold(void) {
    int edge_bd, edge_ac;
    Graph *g = build_6node(&edge_bd, &edge_ac, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);
    rm_track_route(rm, 1, 0, 5);

    /* Factor 1.5 on A->C (below threshold 1.8): NO reroute */
    RerouteSummary sum;
    Status st = rm_set_traffic_factor(rm, edge_ac, 1.5, &sum);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(sum.pushed_history, 1);
    ASSERT_EQ(sum.routes_rerouted, 0);
    ASSERT_EQ(rm_history_size(rm), 1);

    /* Undo */
    rm_undo(rm, NULL, 0);
    ASSERT_EQ(rm_history_size(rm), 0);
    ASSERT_DOUBLE_EQ(graph_edge(g, edge_ac)->traffic_factor, 1.0, 1e-9);

    /* Factor 3.0 on A->C (>= threshold 1.8): triggers reroute */
    st = rm_set_traffic_factor(rm, edge_ac, 3.0, &sum);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(sum.pushed_history, 1);
    ASSERT_EQ(sum.routes_rerouted, 1);
    ASSERT_EQ(rm_history_size(rm), 1);

    const PathResult *p = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p);
    ASSERT_TRUE(p->found);
    ASSERT_DOUBLE_EQ(p->total_time_s, 14.0, 1e-9);
    /* Path should be A-B-D-E-F */
    ASSERT_EQ(p->node_count, 5);
    ASSERT_EQ(p->nodes[0], 0);
    ASSERT_EQ(p->nodes[1], 1);
    ASSERT_EQ(p->nodes[2], 3);
    ASSERT_EQ(p->nodes[3], 4);
    ASSERT_EQ(p->nodes[4], 5);

    /* Undo: restores original path and factor 1.0 */
    rm_undo(rm, NULL, 0);
    ASSERT_DOUBLE_EQ(graph_edge(g, edge_ac)->traffic_factor, 1.0, 1e-9);
    const PathResult *pr = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(pr);
    ASSERT_DOUBLE_EQ(pr->total_time_s, 13.0, 1e-9);

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_unreachable_then_recover(void) {
    int edge_bd, edge_ac, edge_df, edge_ef;
    Graph *g = build_6node(&edge_bd, &edge_ac, &edge_df, &edge_ef);
    RouteManager *rm = rm_create(g, 1.8);
    rm_track_route(rm, 1, 0, 5);

    /* Block both edges into F (D->F and E->F) */
    rm_block_edge(rm, edge_df, NULL);
    int edge_ef_b = -1;
    for (size_t k = 0; k < graph_out_degree(g, 4); k++) {
        int eid = graph_out_edge(g, 4, k);
        if (graph_edge(g, eid)->to == 5) { edge_ef_b = eid; break; }
    }
    ASSERT_TRUE(edge_ef_b >= 0);
    rm_block_edge(rm, edge_ef_b, NULL);

    const PathResult *p = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->found, 0);  /* unreachable */
    ASSERT_EQ(rm_history_size(rm), 2);

    /* Unblock E->F: route should become reachable */
    RerouteSummary sum;
    rm_unblock_edge(rm, edge_ef_b, &sum);
    ASSERT_EQ(sum.pushed_history, 1);
    p = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p);
    ASSERT_TRUE(p->found);
    ASSERT_EQ(rm_history_size(rm), 3);

    /* Undo 3 times restores each step */
    rm_undo(rm, NULL, 0);
    ASSERT_EQ(rm_history_size(rm), 2);
    rm_undo(rm, NULL, 0);
    ASSERT_EQ(rm_history_size(rm), 1);
    rm_undo(rm, NULL, 0);
    ASSERT_EQ(rm_history_size(rm), 0);

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_two_routes_sorted_order(void) {
    int edge_bd;
    Graph *g = build_6node(&edge_bd, NULL, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);

    rm_track_route(rm, 20, 0, 5);
    rm_track_route(rm, 10, 0, 3);

    /* Block B->D affects both (route 10: A->D, route 20: A->F) */
    RerouteSummary sum;
    rm_block_edge(rm, edge_bd, &sum);
    ASSERT_EQ(sum.pushed_history, 1);
    ASSERT_TRUE(sum.routes_rerouted >= 1);

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_check_and_reroute(void) {
    int edge_bd, edge_ac;
    Graph *g = build_6node(&edge_bd, &edge_ac, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);
    rm_track_route(rm, 1, 0, 5);

    /* Set factor >= threshold directly on graph (no history from set_traffic_factor via rm) */
    graph_set_traffic_factor(g, edge_ac, 2.5);

    RerouteSummary sum;
    Status st = rm_check_and_reroute(rm, &sum);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(sum.pushed_history, 1);
    ASSERT_TRUE(sum.routes_rerouted >= 1);

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_history_describe(void) {
    int edge_bd;
    Graph *g = build_6node(&edge_bd, NULL, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);
    rm_track_route(rm, 1, 0, 5);

    rm_block_edge(rm, edge_bd, NULL);
    ASSERT_EQ(rm_history_size(rm), 1);

    char buf[128] = {0};
    Status st = rm_history_describe(rm, 0, buf, sizeof(buf));
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(buf[0] != '\0');

    rm_destroy(rm);
    graph_destroy(g);
}

static void test_destroy_with_history(void) {
    int edge_bd;
    Graph *g = build_6node(&edge_bd, NULL, NULL, NULL);
    RouteManager *rm = rm_create(g, 1.8);
    rm_track_route(rm, 1, 0, 5);
    rm_block_edge(rm, edge_bd, NULL);
    ASSERT_EQ(rm_history_size(rm), 1);
    /* Should free everything without crash */
    rm_destroy(rm);
    graph_destroy(g);
}

static void test_spec_reroute_undo_cases(void) {
    int edge_bd, edge_ac, edge_df, edge_ef;
    Graph *g = build_6node(&edge_bd, &edge_ac, &edge_df, &edge_ef);
    RouteManager *rm = rm_create(g, 1.8);
    ASSERT_NOT_NULL(rm);

    /* 2a) rm_undo on empty history returns STATUS_ERR_EMPTY */
    char desc_empty[64];
    ASSERT_EQ(rm_undo(rm, desc_empty, sizeof(desc_empty)), STATUS_ERR_EMPTY);

    /* Find C->E directed edge */
    int edge_ce = -1;
    for (size_t k = 0; k < graph_out_degree(g, 2); k++) {
        int eid = graph_out_edge(g, 2, k);
        if (graph_edge(g, eid)->to == 4) { edge_ce = eid; break; }
    }
    ASSERT_TRUE(edge_ce >= 0);

    /* Track route 1: A->F */
    rm_track_route(rm, 1, 0, 5);
    const PathResult *p_initial = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p_initial);
    ASSERT_TRUE(p_initial->found);
    ASSERT_DOUBLE_EQ(p_initial->total_time_s, 13.0, 1e-9);

    /* 2c) traffic factor 1.5 on A->C (below 1.8 threshold): routes_rerouted == 0 and route path is unchanged */
    RerouteSummary sum_sub;
    Status st_sub = rm_set_traffic_factor(rm, edge_ac, 1.5, &sum_sub);
    ASSERT_EQ(st_sub, STATUS_OK);
    ASSERT_EQ(sum_sub.routes_rerouted, 0);
    const PathResult *p_after_15 = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p_after_15);
    ASSERT_DOUBLE_EQ(p_after_15->total_time_s, 13.0, 1e-9);

    /* 2d) After undoing a factor-3.0 change, the factor is exactly 1.0 again */
    /* Undo the 1.5 change first */
    rm_undo(rm, NULL, 0);
    ASSERT_DOUBLE_EQ(graph_edge(g, edge_ac)->traffic_factor, 1.0, 1e-9);

    /* Set factor 3.0 */
    RerouteSummary sum_30;
    rm_set_traffic_factor(rm, edge_ac, 3.0, &sum_30);
    ASSERT_EQ(sum_30.routes_rerouted, 1);
    const PathResult *p_after_30 = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p_after_30);
    ASSERT_DOUBLE_EQ(p_after_30->total_time_s, 14.0, 1e-9);
    ASSERT_DOUBLE_EQ(graph_edge(g, edge_ac)->traffic_factor, 3.0, 1e-9);

    /* Undo factor 3.0 change -> factor must be exactly 1.0 again and path restored to 13.0 */
    rm_undo(rm, NULL, 0);
    ASSERT_DOUBLE_EQ(graph_edge(g, edge_ac)->traffic_factor, 1.0, 1e-9);
    const PathResult *p_after_undo30 = rm_get_path(rm, 1);
    ASSERT_NOT_NULL(p_after_undo30);
    ASSERT_DOUBLE_EQ(p_after_undo30->total_time_s, 13.0, 1e-9);

    /* 2b) Two stacked actions (block B->D, then block C->E) undone in LIFO order */
    /* Action 1: block B->D -> A->F becomes A-C-E-F (15s) */
    RerouteSummary sum1;
    rm_block_edge(rm, edge_bd, &sum1);
    ASSERT_EQ(sum1.routes_rerouted, 1);
    ASSERT_EQ(graph_edge(g, edge_bd)->blocked, 1);
    ASSERT_EQ(graph_edge(g, edge_ce)->blocked, 0);
    const PathResult *p_step1 = rm_get_path(rm, 1);
    ASSERT_DOUBLE_EQ(p_step1->total_time_s, 15.0, 1e-9);

    /* Action 2: block C->E -> A->F becomes A-C-D-E-F not possible because B->D blocked; C-D-E-F: 2+8+2+3 = 15s */
    RerouteSummary sum2;
    rm_block_edge(rm, edge_ce, &sum2);
    ASSERT_EQ(graph_edge(g, edge_bd)->blocked, 1);
    ASSERT_EQ(graph_edge(g, edge_ce)->blocked, 1);
    const PathResult *p_step2 = rm_get_path(rm, 1);
    ASSERT_DOUBLE_EQ(p_step2->total_time_s, 15.0, 1e-9);
    ASSERT_EQ(rm_history_size(rm), 2);

    /* Undo Action 2: C->E is unblocked, B->D is still blocked */
    char desc_undo2[128];
    ASSERT_EQ(rm_undo(rm, desc_undo2, sizeof(desc_undo2)), STATUS_OK);
    ASSERT_EQ(graph_edge(g, edge_bd)->blocked, 1);
    ASSERT_EQ(graph_edge(g, edge_ce)->blocked, 0);
    const PathResult *p_after_undo2 = rm_get_path(rm, 1);
    ASSERT_DOUBLE_EQ(p_after_undo2->total_time_s, 15.0, 1e-9);
    ASSERT_EQ(rm_history_size(rm), 1);

    /* Undo Action 1: B->D is unblocked, both are open, path restored to 13s */
    char desc_undo1[128];
    ASSERT_EQ(rm_undo(rm, desc_undo1, sizeof(desc_undo1)), STATUS_OK);
    ASSERT_EQ(graph_edge(g, edge_bd)->blocked, 0);
    ASSERT_EQ(graph_edge(g, edge_ce)->blocked, 0);
    const PathResult *p_after_undo1 = rm_get_path(rm, 1);
    ASSERT_DOUBLE_EQ(p_after_undo1->total_time_s, 13.0, 1e-9);
    ASSERT_EQ(rm_history_size(rm), 0);

    rm_destroy(rm);
    graph_destroy(g);
}

int main(void) {
    RUN_TEST(test_basic_track_and_path);
    RUN_TEST(test_block_reroute_undo);
    RUN_TEST(test_block_edge_no_route_uses_it);
    RUN_TEST(test_block_already_blocked_noop);
    RUN_TEST(test_traffic_factor_threshold);
    RUN_TEST(test_unreachable_then_recover);
    RUN_TEST(test_two_routes_sorted_order);
    RUN_TEST(test_check_and_reroute);
    RUN_TEST(test_history_describe);
    RUN_TEST(test_destroy_with_history);
    RUN_TEST(test_spec_reroute_undo_cases);
    return test_summary();
}
