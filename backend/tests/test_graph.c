#include "test_util.h"
#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ASSERT_TRUE(cond) TEST_ASSERT(cond)
#define ASSERT_EQ(a, b) TEST_ASSERT_EQ_INT(a, b)
#define ASSERT_NULL(p) TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_DOUBLE_EQ(a, b, tol) TEST_ASSERT(fabs((a) - (b)) <= (tol))
#define ASSERT_STR_EQ(a, b) TEST_ASSERT(strcmp((a), (b)) == 0)

static void test_graph_node_operations(void) {
    Graph *g = graph_create();
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(graph_node_count(g), 0);

    int n0 = graph_add_node(g, 1001, 12.9716, 77.5946, "Majestic", 1);
    ASSERT_EQ(n0, 0);
    ASSERT_EQ(graph_node_count(g), 1);

    const GraphNode *node0 = graph_node(g, 0);
    ASSERT_NOT_NULL(node0);
    ASSERT_EQ(node0->osm_id, 1001);
    ASSERT_DOUBLE_EQ(node0->lat, 12.9716, 1e-6);
    ASSERT_DOUBLE_EQ(node0->lng, 77.5946, 1e-6);
    ASSERT_STR_EQ(node0->name, "Majestic");
    ASSERT_EQ(node0->is_stop, 1);

    /* Deduplication of osm_id */
    int n0_dup = graph_add_node(g, 1001, 12.0, 77.0, "New Name Ignored", 0);
    ASSERT_EQ(n0_dup, 0);
    ASSERT_EQ(graph_node_count(g), 1);
    ASSERT_STR_EQ(graph_node(g, 0)->name, "Majestic");
    ASSERT_EQ(graph_node(g, 0)->is_stop, 1);

    /* Node with empty initial name, then updated with name and stop */
    int n1 = graph_add_node(g, 1002, 12.9750, 77.6000, "", 0);
    ASSERT_EQ(n1, 1);
    ASSERT_EQ(graph_node(g, 1)->is_stop, 0);
    int n1_dup = graph_add_node(g, 1002, 12.9750, 77.6000, "Shivajinagar", 1);
    ASSERT_EQ(n1_dup, 1);
    ASSERT_STR_EQ(graph_node(g, 1)->name, "Shivajinagar");
    ASSERT_EQ(graph_node(g, 1)->is_stop, 1);

    /* Lookups */
    ASSERT_EQ(graph_find_node_by_osm_id(g, 1001), 0);
    ASSERT_EQ(graph_find_node_by_osm_id(g, 1002), 1);
    ASSERT_EQ(graph_find_node_by_osm_id(g, 9999), -1);

    /* Out of range */
    ASSERT_NULL(graph_node(g, -1));
    ASSERT_NULL(graph_node(g, 2));

    graph_destroy(g);
}

static void test_graph_edge_operations(void) {
    Graph *g = graph_create();
    ASSERT_NOT_NULL(g);

    graph_add_node(g, 1, 12.97, 77.59, "A", 0);
    graph_add_node(g, 2, 12.98, 77.60, "B", 0);
    graph_add_node(g, 3, 12.99, 77.61, "C", 0);

    /* Edge validation */
    ASSERT_EQ(graph_add_edge(g, -1, 1, 100.0, 10.0, "Road", ROAD_PRIMARY), -1);
    ASSERT_EQ(graph_add_edge(g, 0, 5, 100.0, 10.0, "Road", ROAD_PRIMARY), -1);
    ASSERT_EQ(graph_add_edge(g, 0, 0, 100.0, 10.0, "Self", ROAD_PRIMARY), -1);
    ASSERT_EQ(graph_add_edge(g, 0, 1, 0.0, 10.0, "ZeroLen", ROAD_PRIMARY), -1);
    ASSERT_EQ(graph_add_edge(g, 0, 1, 100.0, 0.0, "ZeroTime", ROAD_PRIMARY), -1);
    ASSERT_EQ(graph_add_edge(g, 0, 1, -10.0, 10.0, "NegLen", ROAD_PRIMARY), -1);

    /* Valid directed edge */
    int e0 = graph_add_edge(g, 0, 1, 500.0, 50.0, "MG Road", ROAD_PRIMARY);
    ASSERT_EQ(e0, 0);
    ASSERT_EQ(graph_edge_count(g), 1);

    const Edge *edge0 = graph_edge(g, 0);
    ASSERT_NOT_NULL(edge0);
    ASSERT_EQ(edge0->from, 0);
    ASSERT_EQ(edge0->to, 1);
    ASSERT_DOUBLE_EQ(edge0->length_m, 500.0, 1e-6);
    ASSERT_DOUBLE_EQ(edge0->base_time_s, 50.0, 1e-6);
    ASSERT_DOUBLE_EQ(edge0->traffic_factor, 1.0, 1e-6);
    ASSERT_EQ(edge0->blocked, 0);

    const EdgeMeta *meta0 = graph_edge_meta(g, 0);
    ASSERT_NOT_NULL(meta0);
    ASSERT_STR_EQ(meta0->road_name, "MG Road");
    ASSERT_EQ(meta0->road_class, ROAD_PRIMARY);

    /* Bidirectional edge */
    int e_ab = -1, e_ba = -1;
    Status st = graph_add_bidirectional(g, 1, 2, 800.0, 60.0, "Brigade Rd", ROAD_SECONDARY, &e_ab, &e_ba);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(e_ab, 1);
    ASSERT_EQ(e_ba, 2);
    ASSERT_EQ(graph_edge_count(g), 3);

    /* Adjacency checks and iteration */
    ASSERT_EQ(graph_out_degree(g, 0), 1);
    ASSERT_EQ(graph_out_edge(g, 0, 0), 0);
    ASSERT_EQ(graph_out_edge(g, 0, 1), -1);

    int visited_out_0 = 0;
    for (size_t deg_i = 0; deg_i < (size_t)graph_out_degree(g, 0); deg_i++) {
        int eid = graph_out_edge(g, 0, deg_i);
        ASSERT_EQ(eid, 0);
        visited_out_0++;
    }
    ASSERT_EQ(visited_out_0, 1);

    ASSERT_EQ(graph_out_degree(g, 1), 1);
    ASSERT_EQ(graph_out_edge(g, 1, 0), 1);

    ASSERT_EQ(graph_out_degree(g, 2), 1);
    ASSERT_EQ(graph_out_edge(g, 2, 0), 2);

    /* Cost calculation */
    ASSERT_DOUBLE_EQ(graph_edge_cost_s(g, 0, COST_STATIC), 50.0, 1e-6);
    ASSERT_DOUBLE_EQ(graph_edge_cost_s(g, 0, COST_TRAFFIC), 50.0, 1e-6);

    /* Traffic factor setting and clamping */
    ASSERT_EQ(graph_set_traffic_factor(g, 0, 0.5), STATUS_OK);
    ASSERT_DOUBLE_EQ(graph_edge(g, 0)->traffic_factor, 1.0, 1e-6); /* clamped to 1.0 */

    ASSERT_EQ(graph_set_traffic_factor(g, 0, 15.0), STATUS_OK);
    ASSERT_DOUBLE_EQ(graph_edge(g, 0)->traffic_factor, GRAPH_MAX_TRAFFIC_FACTOR, 1e-6); /* clamped to 10.0 */

    ASSERT_EQ(graph_set_traffic_factor(g, 0, 2.5), STATUS_OK);
    ASSERT_DOUBLE_EQ(graph_edge(g, 0)->traffic_factor, 2.5, 1e-6);
    ASSERT_DOUBLE_EQ(graph_edge_cost_s(g, 0, COST_TRAFFIC), 125.0, 1e-6);

    ASSERT_EQ(graph_set_traffic_factor(g, 0, NAN), STATUS_ERR_INVALID);
    ASSERT_EQ(graph_set_traffic_factor(g, 99, 1.5), STATUS_ERR_INVALID);

    /* Blocked normalisation */
    ASSERT_EQ(graph_set_blocked(g, 0, 42), STATUS_OK);
    ASSERT_EQ(graph_edge(g, 0)->blocked, 1);
    ASSERT_EQ(graph_set_blocked(g, 0, 0), STATUS_OK);
    ASSERT_EQ(graph_edge(g, 0)->blocked, 0);

    /* Reset traffic */
    graph_reset_traffic(g);
    ASSERT_DOUBLE_EQ(graph_edge(g, 0)->traffic_factor, 1.0, 1e-6);
    ASSERT_DOUBLE_EQ(graph_edge(g, 1)->traffic_factor, 1.0, 1e-6);

    graph_destroy(g);
}

static void test_graph_haversine_and_speed(void) {
    /* Same point -> 0 distance */
    double d0 = graph_haversine_m(12.9716, 77.5946, 12.9716, 77.5946);
    ASSERT_DOUBLE_EQ(d0, 0.0, 1e-6);

    /* Symmetric */
    double d1 = graph_haversine_m(12.9716, 77.5946, 13.0000, 77.6000);
    double d2 = graph_haversine_m(13.0000, 77.6000, 12.9716, 77.5946);
    ASSERT_DOUBLE_EQ(d1, d2, 1e-6);

    /* 1 degree latitude roughly 111195 m (+/- 100 m) */
    double d_lat = graph_haversine_m(0.0, 0.0, 1.0, 0.0);
    ASSERT_TRUE(fabs(d_lat - 111195.0) < 100.0);

    /* Max speed incremental maintenance */
    Graph *g = graph_create();
    ASSERT_NOT_NULL(g);
    ASSERT_DOUBLE_EQ(graph_max_speed_mps(g), 1e-3, 1e-6);

    graph_add_node(g, 1, 0.0, 0.0, "N1", 0);
    graph_add_node(g, 2, 1.0, 0.0, "N2", 0); /* ~111195 m apart */

    /* Add edge with 11119.5 seconds -> speed ~ 10.0 m/s */
    graph_add_edge(g, 0, 1, 120000.0, 11119.5, "R1", ROAD_PRIMARY);
    double sp1 = graph_max_speed_mps(g);
    ASSERT_TRUE(fabs(sp1 - 10.0) < 0.1);

    /* Add faster edge -> speed ~ 20.0 m/s */
    graph_add_node(g, 3, 2.0, 0.0, "N3", 0);
    graph_add_edge(g, 1, 2, 120000.0, 5559.75, "R2", ROAD_PRIMARY);
    double sp2 = graph_max_speed_mps(g);
    ASSERT_TRUE(fabs(sp2 - 20.0) < 0.1);

    graph_destroy(g);
}

static void test_graph_spatial_queries(void) {
    Graph *g = graph_create();
    graph_add_node(g, 1, 12.9700, 77.5900, "Node 0", 0);
    graph_add_node(g, 2, 12.9700, 77.5920, "Node 1", 0);
    graph_add_node(g, 3, 13.0000, 77.6000, "Far Node", 0);

    graph_add_edge(g, 0, 1, 200.0, 20.0, "Close Edge", ROAD_PRIMARY);
    graph_add_edge(g, 1, 2, 4000.0, 400.0, "Far Edge", ROAD_PRIMARY);

    double dist = 0.0;
    int near_node = graph_nearest_node(g, 12.9701, 77.5901, &dist);
    ASSERT_EQ(near_node, 0);
    ASSERT_TRUE(dist < 50.0);

    int out_ids[10];
    size_t found = graph_find_edges_near(g, 12.9700, 77.5910, 100.0, out_ids, 10);
    ASSERT_EQ(found, 1);
    ASSERT_EQ(out_ids[0], 0);

    graph_destroy(g);
}

static void test_graph_components(void) {
    Graph *g = graph_create();

    /* Island 1: nodes 0, 1, 2 (size 3) */
    graph_add_node(g, 1, 10.0, 10.0, "I1_0", 0);
    graph_add_node(g, 2, 10.0, 10.1, "I1_1", 0);
    graph_add_node(g, 3, 10.0, 10.2, "I1_2", 0);
    graph_add_edge(g, 0, 1, 100.0, 10.0, "E1", ROAD_PRIMARY);
    graph_add_edge(g, 1, 2, 100.0, 10.0, "E2", ROAD_PRIMARY);

    /* Island 2: nodes 3, 4 (size 2) */
    graph_add_node(g, 4, 20.0, 20.0, "I2_0", 0);
    graph_add_node(g, 5, 20.0, 20.1, "I2_1", 0);
    graph_add_edge(g, 3, 4, 100.0, 10.0, "E3", ROAD_PRIMARY);

    Graph *largest = graph_extract_largest_component(g);
    ASSERT_NOT_NULL(largest);
    ASSERT_EQ(graph_node_count(largest), 3);
    ASSERT_EQ(graph_edge_count(largest), 2);
    ASSERT_EQ(graph_node(largest, 0)->osm_id, 1);
    ASSERT_EQ(graph_node(largest, 1)->osm_id, 2);
    ASSERT_EQ(graph_node(largest, 2)->osm_id, 3);
    graph_destroy(largest);

    /* Tie breaker test: Island A (0,1) size 2, Island B (2,3) size 2 -> choose Island A */
    Graph *g_tie = graph_create();
    graph_add_node(g_tie, 10, 0.0, 0.0, "A0", 0);
    graph_add_node(g_tie, 11, 0.0, 0.1, "A1", 0);
    graph_add_edge(g_tie, 0, 1, 100.0, 10.0, "EA", ROAD_PRIMARY);

    graph_add_node(g_tie, 20, 1.0, 1.0, "B0", 0);
    graph_add_node(g_tie, 21, 1.0, 1.1, "B1", 0);
    graph_add_edge(g_tie, 2, 3, 100.0, 10.0, "EB", ROAD_PRIMARY);

    Graph *tie_comp = graph_extract_largest_component(g_tie);
    ASSERT_NOT_NULL(tie_comp);
    ASSERT_EQ(graph_node_count(tie_comp), 2);
    ASSERT_EQ(graph_node(tie_comp, 0)->osm_id, 10);
    ASSERT_EQ(graph_node(tie_comp, 1)->osm_id, 11);

    graph_destroy(tie_comp);
    graph_destroy(g_tie);
    graph_destroy(g);
}

static void test_graph_save_load(void) {
    Graph *g = graph_create();
    graph_add_node(g, 101, 12.9716, 77.5946, "Node One", 1);
    graph_add_node(g, 102, 12.9800, 77.6000, "Node Two", 0);
    graph_add_edge(g, 0, 1, 1234.5, 98.7, "Main St", ROAD_SECONDARY);

    const char *path = "test_graph_tmp.txt";
    Status st = graph_save(g, path);
    ASSERT_EQ(st, STATUS_OK);

    Graph *loaded = NULL;
    st = graph_load(&loaded, path);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(loaded);

    ASSERT_EQ(graph_node_count(loaded), 2);
    ASSERT_EQ(graph_edge_count(loaded), 1);
    ASSERT_EQ(graph_node(loaded, 0)->osm_id, 101);
    ASSERT_STR_EQ(graph_node(loaded, 0)->name, "Node One");
    ASSERT_EQ(graph_node(loaded, 0)->is_stop, 1);
    ASSERT_DOUBLE_EQ(graph_node(loaded, 0)->lat, 12.9716, 1e-6);

    const Edge *e = graph_edge(loaded, 0);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->from, 0);
    ASSERT_EQ(e->to, 1);
    ASSERT_DOUBLE_EQ(e->length_m, 1234.5, 1e-6);
    ASSERT_DOUBLE_EQ(e->base_time_s, 98.7, 1e-6);
    ASSERT_STR_EQ(graph_edge_meta(loaded, 0)->road_name, "Main St");
    ASSERT_EQ(graph_edge_meta(loaded, 0)->road_class, ROAD_SECONDARY);

    graph_destroy(g);
    graph_destroy(loaded);
    remove(path);

    /* Test error cases */
    Graph *err_g = NULL;
    ASSERT_EQ(graph_load(&err_g, "non_existent_file_xyz.txt"), STATUS_ERR_NOT_FOUND);
    ASSERT_NULL(err_g);

    /* a) "N 3" header but only 2 node lines */
    FILE *fa = fopen("bad_case_a.txt", "wb");
    fprintf(fa, "TRANSPORT_GRAPH v1\nN 3\n1\t12.0\t77.0\t0\tN1\n2\t12.1\t77.1\t0\tN2\nE 0\n");
    fclose(fa);
    ASSERT_EQ(graph_load(&err_g, "bad_case_a.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_case_a.txt");

    /* b) "E 2" header but only 1 edge line */
    FILE *fb = fopen("bad_case_b.txt", "wb");
    fprintf(fb, "TRANSPORT_GRAPH v1\nN 2\n1\t12.0\t77.0\t0\tN1\n2\t12.1\t77.1\t0\tN2\nE 2\n0\t1\t100.0\t10.0\t0\tR1\n");
    fclose(fb);
    ASSERT_EQ(graph_load(&err_g, "bad_case_b.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_case_b.txt");

    /* c) an edge line whose "to" index is >= node count */
    FILE *fc = fopen("bad_case_c.txt", "wb");
    fprintf(fc, "TRANSPORT_GRAPH v1\nN 2\n1\t12.0\t77.0\t0\tN1\n2\t12.1\t77.1\t0\tN2\nE 1\n0\t2\t100.0\t10.0\t0\tR1\n");
    fclose(fc);
    ASSERT_EQ(graph_load(&err_g, "bad_case_c.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_case_c.txt");

    /* d) an edge line with a negative "from" index */
    FILE *fd = fopen("bad_case_d.txt", "wb");
    fprintf(fd, "TRANSPORT_GRAPH v1\nN 2\n1\t12.0\t77.0\t0\tN1\n2\t12.1\t77.1\t0\tN2\nE 1\n-1\t1\t100.0\t10.0\t0\tR1\n");
    fclose(fd);
    ASSERT_EQ(graph_load(&err_g, "bad_case_d.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_case_d.txt");

    /* e) a garbage number in a node latitude ("abc") */
    FILE *fe = fopen("bad_case_e.txt", "wb");
    fprintf(fe, "TRANSPORT_GRAPH v1\nN 1\n1\tabc\t77.0\t0\tN1\nE 0\n");
    fclose(fe);
    ASSERT_EQ(graph_load(&err_g, "bad_case_e.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_case_e.txt");

    /* f) a garbage number in an edge length_m ("12x") */
    FILE *ff = fopen("bad_case_f.txt", "wb");
    fprintf(ff, "TRANSPORT_GRAPH v1\nN 2\n1\t12.0\t77.0\t0\tN1\n2\t12.1\t77.1\t0\tN2\nE 1\n0\t1\t12x\t10.0\t0\tR1\n");
    fclose(ff);
    ASSERT_EQ(graph_load(&err_g, "bad_case_f.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_case_f.txt");

    /* g) an empty file */
    FILE *fg = fopen("bad_case_g.txt", "wb");
    fclose(fg);
    ASSERT_EQ(graph_load(&err_g, "bad_case_g.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_case_g.txt");

    /* Malformed header */
    FILE *f = fopen("bad_header.txt", "wb");
    fprintf(f, "INVALID HEADER\nN 0\nE 0\n");
    fclose(f);
    ASSERT_EQ(graph_load(&err_g, "bad_header.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_header.txt");

    /* Truncated file */
    f = fopen("bad_trunc.txt", "wb");
    fprintf(f, "TRANSPORT_GRAPH v1\nN 2\n1\t12.0\t77.0\t0\tN1\n");
    fclose(f);
    ASSERT_EQ(graph_load(&err_g, "bad_trunc.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(err_g);
    remove("bad_trunc.txt");

    /* CRLF test */
    f = fopen("crlf_test.txt", "wb");
    fprintf(f, "TRANSPORT_GRAPH v1\r\nN 1\r\n10\t12.5\t77.5\t1\tNode\r\nE 0\r\n");
    fclose(f);
    ASSERT_EQ(graph_load(&err_g, "crlf_test.txt"), STATUS_OK);
    ASSERT_NOT_NULL(err_g);
    ASSERT_EQ(graph_node_count(err_g), 1);
    graph_destroy(err_g);
    remove("crlf_test.txt");
}

static void test_graph_synthetic_grid(void) {
    Graph *g1 = NULL;
    Graph *g2 = NULL;
    Graph *g3 = NULL;

    Status st = graph_build_synthetic_grid(&g1, 5, 5, 12.97, 77.59, 200.0, 12345ULL);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(g1);
    ASSERT_EQ(graph_node_count(g1), 25);
    /* 5x5 grid has 5*4 = 20 horizontal, 5*4 = 20 vertical -> 40 bidirectional edges = 80 directed edges */
    ASSERT_EQ(graph_edge_count(g1), 80);

    /* Determinism with same seed */
    st = graph_build_synthetic_grid(&g2, 5, 5, 12.97, 77.59, 200.0, 12345ULL);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_DOUBLE_EQ(graph_edge(g1, 0)->length_m, graph_edge(g2, 0)->length_m, 1e-9);
    ASSERT_DOUBLE_EQ(graph_edge(g1, 0)->base_time_s, graph_edge(g2, 0)->base_time_s, 1e-9);

    /* Different seed */
    st = graph_build_synthetic_grid(&g3, 5, 5, 12.97, 77.59, 200.0, 99999ULL);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(fabs(graph_edge(g1, 0)->base_time_s - graph_edge(g3, 0)->base_time_s) > 1e-6);

    graph_destroy(g1);
    graph_destroy(g2);
    graph_destroy(g3);
}

int main(void) {
    RUN_TEST(test_graph_node_operations);
    RUN_TEST(test_graph_edge_operations);
    RUN_TEST(test_graph_haversine_and_speed);
    RUN_TEST(test_graph_spatial_queries);
    RUN_TEST(test_graph_components);
    RUN_TEST(test_graph_save_load);
    RUN_TEST(test_graph_synthetic_grid);

    return test_summary();
}