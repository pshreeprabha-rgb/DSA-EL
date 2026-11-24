#include "test_util.h"
#include "pathfind.h"
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

/*
 * Hand-built 6-node graph:
 * Nodes A..F = 0..5.
 * length_m = 10 * time_s (so all edges are 10 m/s).
 * Bidirectional edges:
 * A-B: 4 s  (40 m)
 * A-C: 2 s  (20 m)
 * B-C: 1 s  (10 m)
 * B-D: 5 s  (50 m)
 * C-D: 8 s  (80 m)
 * C-E: 10 s (100 m)
 * D-E: 2 s  (20 m)
 * D-F: 6 s  (60 m)
 * E-F: 3 s  (30 m)
 */
static Graph *build_6node_graph(void) {
    Graph *g = graph_create();
    /* Place coordinates along a line or grid */
    graph_add_node(g, 1, 12.00, 77.00, "A", 0);
    graph_add_node(g, 2, 12.01, 77.01, "B", 0);
    graph_add_node(g, 3, 12.02, 77.02, "C", 0);
    graph_add_node(g, 4, 12.03, 77.03, "D", 0);
    graph_add_node(g, 5, 12.04, 77.04, "E", 0);
    graph_add_node(g, 6, 12.05, 77.05, "F", 0);

    graph_add_bidirectional(g, 0, 1, 40.0, 4.0, "A-B", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 0, 2, 20.0, 2.0, "A-C", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 1, 2, 10.0, 1.0, "B-C", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 1, 3, 50.0, 5.0, "B-D", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 2, 3, 80.0, 8.0, "C-D", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 2, 4, 100.0, 10.0, "C-E", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 3, 4, 20.0, 2.0, "D-E", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 3, 5, 60.0, 6.0, "D-F", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 4, 5, 30.0, 3.0, "E-F", ROAD_PRIMARY, NULL, NULL);

    return g;
}

static void test_hand_worked_answers(void) {
    Graph *g = build_6node_graph();
    ASSERT_NOT_NULL(g);

    PathResult p;
    path_result_init(&p);

    /*
     * 1) A->F, COST_STATIC:
     * Candidate paths:
     * - A-B-D-F: 4+5+6 = 15
     * - A-B-D-E-F: 4+5+2+3 = 14
     * - A-C-B-D-F: 2+1+5+6 = 14
     * - A-C-B-D-E-F: 2+1+5+2+3 = 13  (Shortest!)
     * - A-C-E-F: 2+10+3 = 15
     */
    Status st = astar_find_path(g, 0, 5, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 13.0, 1e-9);
    ASSERT_EQ(p.node_count, 6);
    ASSERT_EQ(p.nodes[0], 0); /* A */
    ASSERT_EQ(p.nodes[1], 2); /* C */
    ASSERT_EQ(p.nodes[2], 1); /* B */
    ASSERT_EQ(p.nodes[3], 3); /* D */
    ASSERT_EQ(p.nodes[4], 4); /* E */
    ASSERT_EQ(p.nodes[5], 5); /* F */
    ASSERT_TRUE(path_is_valid(g, &p));

    /*
     * 2) A->D:
     * - A-B-D = 4+5 = 9
     * - A-C-B-D = 2+1+5 = 8 (Shortest!)
     */
    path_result_free(&p);
    st = astar_find_path(g, 0, 3, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 8.0, 1e-9);
    ASSERT_EQ(p.node_count, 4);
    ASSERT_EQ(p.nodes[0], 0);
    ASSERT_EQ(p.nodes[1], 2);
    ASSERT_EQ(p.nodes[2], 1);
    ASSERT_EQ(p.nodes[3], 3);

    /*
     * 3) A->E:
     * - A-C-B-D-E = 2+1+5+2 = 10 (Shortest!)
     * - A-C-E = 2+10 = 12
     */
    path_result_free(&p);
    st = astar_find_path(g, 0, 4, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 10.0, 1e-9);
    ASSERT_EQ(p.node_count, 5);
    ASSERT_EQ(p.nodes[0], 0);
    ASSERT_EQ(p.nodes[1], 2);
    ASSERT_EQ(p.nodes[2], 1);
    ASSERT_EQ(p.nodes[3], 3);
    ASSERT_EQ(p.nodes[4], 4);

    /*
     * 4) Block directed edge B->D only:
     * In g, find directed edge from 1 to 3:
     */
    int edge_bd = -1;
    for (size_t k = 0; k < graph_out_degree(g, 1); k++) {
        int eid = graph_out_edge(g, 1, k);
        if (graph_edge(g, eid)->to == 3) {
            edge_bd = eid;
            break;
        }
    }
    ASSERT_TRUE(edge_bd >= 0);
    graph_set_blocked(g, edge_bd, 1);

    /*
     * A->F with B->D blocked:
     * Paths avoiding directed B->D:
     * - A-C-D-E-F: 2+8+2+3 = 15
     * - A-C-E-F: 2+10+3 = 15
     * Cost is 15.
     */
    path_result_free(&p);
    st = astar_find_path(g, 0, 5, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 15.0, 1e-9);

    /* A->D with B->D blocked:
     * - A-C-D = 2+8 = 10
     */
    path_result_free(&p);
    st = astar_find_path(g, 0, 3, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 10.0, 1e-9);

    /* Unblock B->D */
    graph_set_blocked(g, edge_bd, 0);

    /*
     * 5) Traffic factor 3.0 on edge A->C only (COST_TRAFFIC):
     */
    int edge_ac = -1;
    for (size_t k = 0; k < graph_out_degree(g, 0); k++) {
        int eid = graph_out_edge(g, 0, k);
        if (graph_edge(g, eid)->to == 2) {
            edge_ac = eid;
            break;
        }
    }
    ASSERT_TRUE(edge_ac >= 0);
    graph_set_traffic_factor(g, edge_ac, 3.0);

    /*
     * Candidate paths under COST_TRAFFIC (A->C costs 2 * 3.0 = 6):
     * - A-B-D-E-F: 4 + 5 + 2 + 3 = 14 (Shortest!)
     * - A-C-B-D-E-F: 6 + 1 + 5 + 2 + 3 = 17
     * - A-C-E-F: 6 + 10 + 3 = 19
     */
    path_result_free(&p);
    st = astar_find_path(g, 0, 5, COST_TRAFFIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 14.0, 1e-9);
    ASSERT_EQ(p.node_count, 5);
    ASSERT_EQ(p.nodes[0], 0); /* A */
    ASSERT_EQ(p.nodes[1], 1); /* B */
    ASSERT_EQ(p.nodes[2], 3); /* D */
    ASSERT_EQ(p.nodes[3], 4); /* E */
    ASSERT_EQ(p.nodes[4], 5); /* F */

    /* The COST_STATIC path A,C,B,D,E,F evaluated with path_time_s(COST_TRAFFIC) is 17 */
    PathResult static_p;
    path_result_init(&static_p);
    astar_find_path(g, 0, 5, COST_STATIC, &static_p);
    ASSERT_DOUBLE_EQ(static_p.total_time_s, 13.0, 1e-9);
    double evaluated_time = path_time_s(g, &static_p, COST_TRAFFIC);
    ASSERT_DOUBLE_EQ(evaluated_time, 17.0, 1e-9);
    path_result_free(&static_p);

    /*
     * 6) A->A: found, time 0, 1 node, 0 edges, nodes_explored 1
     */
    path_result_free(&p);
    st = astar_find_path(g, 0, 0, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 0.0, 1e-9);
    ASSERT_EQ(p.node_count, 1);
    ASSERT_EQ(p.edge_count, 0);
    ASSERT_EQ(p.nodes[0], 0);
    ASSERT_EQ(p.nodes_explored, 1);
    ASSERT_TRUE(path_is_valid(g, &p));

    path_result_free(&p);
    graph_destroy(g);
}

static void test_test_graph_queries(void) {
    Graph *g = NULL;
    Status st = graph_load(&g, "data/test_graph.txt");
    if (st != STATUS_OK) {
        st = graph_load(&g, "../../data/test_graph.txt");
    }
    if (st != STATUS_OK) {
        st = graph_load(&g, "../data/test_graph.txt");
    }
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(g);

    PathResult p;
    path_result_init(&p);

    /*
     * Query 1: Majestic (0) -> Vidhana Soudha (8)
     * Candidates:
     * - Direct edge 0 -> 8: 160 s
     * - 0 -> 6 (Cubbon Park) 140 s + 6 -> 8 (Ambedkar Veedhi) 80 s = 220 s
     * - 0 -> 19 (Race Course Rd) 100 s + 19 -> 8 (Palace Rd) 90 s = 190 s
     * Shortest: 0 -> 8 directly with time = 160 s.
     */
    st = astar_find_path(g, 0, 8, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 160.0, 1e-9);
    ASSERT_EQ(p.node_count, 2);
    ASSERT_EQ(p.nodes[0], 0);
    ASSERT_EQ(p.nodes[1], 8);

    /*
     * Query 2: Majestic (0) -> MG Road (2)
     * Candidates:
     * - 0 -> 6 (140) + 6 -> 2 (90) = 230 s
     * - 0 -> 6 (140) + 6 -> 1 (120) + 1 -> 2 (100) = 360 s
     * - 0 -> 15 (130) + 15 -> 5 (150) + 5 -> 2 (100) = 380 s
     * Shortest: 0 -> 6 -> 2 with time = 230 s.
     */
    path_result_free(&p);
    st = astar_find_path(g, 0, 2, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 230.0, 1e-9);
    ASSERT_EQ(p.node_count, 3);
    ASSERT_EQ(p.nodes[0], 0);
    ASSERT_EQ(p.nodes[1], 6);
    ASSERT_EQ(p.nodes[2], 2);

    /*
     * Query 3: Vidhana Soudha (8) -> Shivajinagar (1)
     * Candidates:
     * - 8 -> 1 (Infantry Rd): 100 s (Direct!)
     * - 8 -> 6 (80) + 6 -> 1 (120) = 200 s
     * Shortest: 8 -> 1 with time = 100 s.
     */
    path_result_free(&p);
    st = astar_find_path(g, 8, 1, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 100.0, 1e-9);
    ASSERT_EQ(p.node_count, 2);
    ASSERT_EQ(p.nodes[0], 8);
    ASSERT_EQ(p.nodes[1], 1);

    /*
     * Query 4: Seshadripuram (14) -> Malleshwaram (13)
     * Candidates:
     * - 14 -> 13 (Malleshwaram Main Rd): 160 s (Direct!)
     * - 14 -> 0 (150) + ...
     * Shortest: 14 -> 13 with time = 160 s.
     */
    path_result_free(&p);
    st = astar_find_path(g, 14, 13, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 160.0, 1e-9);
    ASSERT_EQ(p.node_count, 2);
    ASSERT_EQ(p.nodes[0], 14);
    ASSERT_EQ(p.nodes[1], 13);

    /*
     * Query 5: Richmond Circle (5) -> Indiranagar (3)
     * Candidates:
     * - 5 -> 2 (Residency Rd, 100) + 2 -> 9 (MG Rd, 120) + 9 -> 3 (Old Airport Rd, 250) = 470 s
     * - 5 -> 2 (100) + 2 -> 9 (120) + 9 -> 11 (Ulsoor Rd, 110) + 11 -> 3 (CMH Rd, 200) = 530 s
     * - 5 -> 6 (130) + 6 -> 2 (90) + ... = 590 s
     * Shortest: 5 -> 2 -> 9 -> 3 with time = 470 s.
     */
    path_result_free(&p);
    st = astar_find_path(g, 5, 3, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 470.0, 1e-9);
    ASSERT_EQ(p.node_count, 4);
    ASSERT_EQ(p.nodes[0], 5);
    ASSERT_EQ(p.nodes[1], 2);
    ASSERT_EQ(p.nodes[2], 9);
    ASSERT_EQ(p.nodes[3], 3);

    path_result_free(&p);
    graph_destroy(g);
}

/* Independent Bellman-Ford Oracle */
static int bellman_ford(const Graph *g, int src, int dst, CostMode mode, double *out_dist) {
    size_t n = graph_node_count(g);
    size_t m = graph_edge_count(g);
    double *dist = (double *)malloc(n * sizeof(double));
    if (!dist) return 0;
    for (size_t i = 0; i < n; i++) dist[i] = 1e30;
    dist[src] = 0.0;

    for (size_t i = 1; i < n; i++) {
        int updated = 0;
        for (size_t e = 0; e < m; e++) {
            const Edge *edge = graph_edge(g, (int)e);
            if (!edge || edge->blocked) continue;
            int u = edge->from;
            int v = edge->to;
            if (dist[u] < 1e29) {
                double w = graph_edge_cost_s(g, (int)e, mode);
                if (dist[u] + w < dist[v] - 1e-12) {
                    dist[v] = dist[u] + w;
                    updated = 1;
                }
            }
        }
        if (!updated) break;
    }

    int found = (dist[dst] < 1e29);
    if (found && out_dist) *out_dist = dist[dst];
    free(dist);
    return found;
}

static void test_oracle_and_exploration_count(void) {
    /* Test on 5 random synthetic graphs (~200 nodes each: 14x14 = 196 nodes) */
    size_t total_astar_explored = 0;
    size_t total_dijkstra_explored = 0;

    for (int g_idx = 0; g_idx < 5; g_idx++) {
        Graph *g = NULL;
        uint64_t seed = (uint64_t)(1000 + g_idx * 777);
        Status st = graph_build_synthetic_grid(&g, 14, 14, 12.97, 77.59, 100.0, seed);
        ASSERT_EQ(st, STATUS_OK);
        ASSERT_NOT_NULL(g);

        /* Apply random traffic and a few blocked edges */
        size_t m = graph_edge_count(g);
        for (size_t e = 0; e < m; e++) {
            if (e % 13 == 0) {
                graph_set_blocked(g, (int)e, 1);
            } else if (e % 3 == 0) {
                graph_set_traffic_factor(g, (int)e, 1.5 + (double)(e % 5) * 0.5);
            }
        }

        size_t n = graph_node_count(g);
        for (size_t q = 0; q < 300; q++) { /* 5 graphs * 300 queries = 1500 queries total */
            int u = (int)((q * 37 + (size_t)g_idx * 13) % n);
            int v = (int)((q * 89 + 17) % n);

            PathResult p_astar, p_dijk;
            path_result_init(&p_astar);
            path_result_init(&p_dijk);

            Status st_a = astar_find_path(g, u, v, COST_TRAFFIC, &p_astar);
            Status st_d = dijkstra_find_path(g, u, v, COST_TRAFFIC, &p_dijk);
            ASSERT_EQ(st_a, STATUS_OK);
            ASSERT_EQ(st_d, STATUS_OK);

            double bf_cost = 0.0;
            int bf_found = bellman_ford(g, u, v, COST_TRAFFIC, &bf_cost);

            ASSERT_EQ(p_astar.found, bf_found);
            ASSERT_EQ(p_dijk.found, bf_found);

            if (bf_found) {
                double rel_diff_a = fabs(p_astar.total_time_s - bf_cost) / (bf_cost > 1e-6 ? bf_cost : 1.0);
                double rel_diff_d = fabs(p_dijk.total_time_s - bf_cost) / (bf_cost > 1e-6 ? bf_cost : 1.0);
                ASSERT_TRUE(rel_diff_a < 1e-6);
                ASSERT_TRUE(rel_diff_d < 1e-6);
            }

            total_astar_explored += p_astar.nodes_explored;
            total_dijkstra_explored += p_dijk.nodes_explored;

            path_result_free(&p_astar);
            path_result_free(&p_dijk);
        }

        graph_destroy(g);
    }

    /* Assert that total A* nodes explored <= total Dijkstra nodes explored */
    ASSERT_TRUE(total_astar_explored <= total_dijkstra_explored);

    /* 40x40 grid, 100 queries: assert A* total explored is at least 20% lower than Dijkstra */
    Graph *grid40 = NULL;
    Status st = graph_build_synthetic_grid(&grid40, 40, 40, 12.97, 77.59, 100.0, 4242ULL);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(grid40);

    size_t a40_explored = 0;
    size_t d40_explored = 0;
    size_t n40 = graph_node_count(grid40);

    for (size_t q = 0; q < 100; q++) {
        int u = (int)((q * 101) % n40);
        int v = (int)((q * 307 + 555) % n40);

        PathResult pa, pd;
        path_result_init(&pa);
        path_result_init(&pd);

        astar_find_path(grid40, u, v, COST_STATIC, &pa);
        dijkstra_find_path(grid40, u, v, COST_STATIC, &pd);

        if (pa.found && pd.found) {
            ASSERT_DOUBLE_EQ(pa.total_time_s, pd.total_time_s, 1e-6);
        }

        a40_explored += pa.nodes_explored;
        d40_explored += pd.nodes_explored;

        path_result_free(&pa);
        path_result_free(&pd);
    }

    /* A* should be at least 20% lower (a40_explored <= d40_explored * 0.80) */
    ASSERT_TRUE((double)a40_explored <= (double)d40_explored * 0.80);

    graph_destroy(grid40);
}

static void test_pathfind_edge_cases(void) {
    Graph *g = build_6node_graph();
    ASSERT_NOT_NULL(g);

    PathResult p;
    path_result_init(&p);

    /* 1. Invalid arguments */
    ASSERT_EQ(find_path(NULL, 0, 1, SEARCH_ASTAR, COST_STATIC, &p), STATUS_ERR_NULL);
    ASSERT_EQ(find_path(g, -1, 1, SEARCH_ASTAR, COST_STATIC, &p), STATUS_ERR_INVALID);
    ASSERT_EQ(find_path(g, 0, 999, SEARCH_ASTAR, COST_STATIC, &p), STATUS_ERR_INVALID);
    ASSERT_EQ(find_path(g, 0, 1, SEARCH_ASTAR, COST_STATIC, NULL), STATUS_ERR_NULL);

    /* 2. Unreachable destination */
    /* Add an isolated node */
    int iso = graph_add_node(g, 99, 13.0, 78.0, "Isolated", 0);
    ASSERT_TRUE(iso >= 0);
    Status st = astar_find_path(g, 0, iso, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(p.found, 0);
    ASSERT_EQ(p.node_count, 0);

    /* 3. Blocked edges skipped in both cost modes */
    /* Block edge between 0 and 1, test COST_STATIC and COST_TRAFFIC */
    int edge_ab = -1;
    for (size_t k = 0; k < graph_out_degree(g, 0); k++) {
        int eid = graph_out_edge(g, 0, k);
        if (graph_edge(g, eid)->to == 1) { edge_ab = eid; break; }
    }
    ASSERT_TRUE(edge_ab >= 0);
    graph_set_blocked(g, edge_ab, 1);

    path_result_free(&p);
    st = astar_find_path(g, 0, 1, COST_STATIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    /* Should go A-C-B: 2 + 1 = 3 */
    ASSERT_DOUBLE_EQ(p.total_time_s, 3.0, 1e-9);

    path_result_free(&p);
    st = astar_find_path(g, 0, 1, COST_TRAFFIC, &p);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_TRUE(p.found);
    ASSERT_DOUBLE_EQ(p.total_time_s, 3.0, 1e-9);

    /* 4. path_result_copy independence */
    PathResult copy;
    path_result_init(&copy);
    ASSERT_EQ(path_result_copy(&copy, &p), STATUS_OK);
    ASSERT_EQ(copy.found, p.found);
    ASSERT_DOUBLE_EQ(copy.total_time_s, p.total_time_s, 1e-9);
    ASSERT_EQ(copy.node_count, p.node_count);
    ASSERT_EQ(copy.edge_count, p.edge_count);
    ASSERT_TRUE(copy.nodes != p.nodes);
    ASSERT_TRUE(copy.edges != p.edges);

    /* Modifying copy does not affect original */
    copy.nodes[0] = 999;
    ASSERT_TRUE(p.nodes[0] != 999);

    path_result_free(&copy);
    path_result_free(&p);
    graph_destroy(g);
}

int main(void) {
    RUN_TEST(test_hand_worked_answers);
    RUN_TEST(test_test_graph_queries);
    RUN_TEST(test_oracle_and_exploration_count);
    RUN_TEST(test_pathfind_edge_cases);

    return test_summary();
}