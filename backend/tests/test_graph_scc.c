#include "test_util.h"
#include "graph_scc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(c) TEST_ASSERT(c)
#define ASSERT_EQ(a,b) TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p) TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)

/* 1. NULL arguments and empty graph */
static void test_scc_null_and_empty(void) {
    int labels[5];
    size_t num_comps = 0;
    int *nodes = NULL;
    size_t count = 0;

    /* NULL arguments */
    ASSERT_EQ(graph_scc_label(NULL, labels, &num_comps), STATUS_ERR_NULL);
    Graph *g = graph_create();
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(graph_scc_label(g, NULL, &num_comps), STATUS_ERR_NULL);
    ASSERT_EQ(graph_scc_label(g, labels, NULL), STATUS_ERR_NULL);

    ASSERT_EQ(graph_largest_scc(NULL, &nodes, &count), STATUS_ERR_NULL);
    ASSERT_EQ(graph_largest_scc(g, NULL, &count), STATUS_ERR_NULL);
    ASSERT_EQ(graph_largest_scc(g, &nodes, NULL), STATUS_ERR_NULL);

    /* Empty graph (0 nodes) */
    ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_ERR_INVALID);
    ASSERT_EQ(graph_largest_scc(g, &nodes, &count), STATUS_ERR_INVALID);
    ASSERT_NULL(nodes);

    graph_destroy(g);
}

/* 2. Single node graph */
static void test_scc_single_node(void) {
    Graph *g = graph_create();
    graph_add_node(g, 101, 12.0, 77.0, "Node0", 0);

    int labels[1];
    size_t num_comps = 0;
    ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_OK);
    ASSERT_EQ(num_comps, 1);
    ASSERT_EQ(labels[0], 0);

    int *nodes = NULL;
    size_t count = 0;
    ASSERT_EQ(graph_largest_scc(g, &nodes, &count), STATUS_OK);
    ASSERT_NOT_NULL(nodes);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(nodes[0], 0);

    free(nodes);
    graph_destroy(g);
}

/* 3. 3-cycle = 1 component */
static void test_scc_3_cycle(void) {
    Graph *g = graph_create();
    graph_add_node(g, 1, 12.0, 77.0, "N0", 0);
    graph_add_node(g, 2, 12.1, 77.1, "N1", 0);
    graph_add_node(g, 3, 12.2, 77.2, "N2", 0);

    graph_add_edge(g, 0, 1, 100.0, 10.0, "e01", ROAD_PRIMARY);
    graph_add_edge(g, 1, 2, 100.0, 10.0, "e12", ROAD_PRIMARY);
    graph_add_edge(g, 2, 0, 100.0, 10.0, "e20", ROAD_PRIMARY);

    int labels[3];
    size_t num_comps = 0;
    ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_OK);
    ASSERT_EQ(num_comps, 1);
    ASSERT_EQ(labels[0], labels[1]);
    ASSERT_EQ(labels[1], labels[2]);

    int *nodes = NULL;
    size_t count = 0;
    ASSERT_EQ(graph_largest_scc(g, &nodes, &count), STATUS_OK);
    ASSERT_EQ(count, 3);
    /* Ascending indices verified */
    ASSERT_EQ(nodes[0], 0);
    ASSERT_EQ(nodes[1], 1);
    ASSERT_EQ(nodes[2], 2);

    free(nodes);
    graph_destroy(g);
}

/* 4. Chain 0->1->2 = 3 components */
static void test_scc_chain(void) {
    Graph *g = graph_create();
    graph_add_node(g, 1, 12.0, 77.0, "N0", 0);
    graph_add_node(g, 2, 12.1, 77.1, "N1", 0);
    graph_add_node(g, 3, 12.2, 77.2, "N2", 0);

    graph_add_edge(g, 0, 1, 100.0, 10.0, "e01", ROAD_PRIMARY);
    graph_add_edge(g, 1, 2, 100.0, 10.0, "e12", ROAD_PRIMARY);

    int labels[3];
    size_t num_comps = 0;
    ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_OK);
    ASSERT_EQ(num_comps, 3);
    ASSERT_TRUE(labels[0] != labels[1]);
    ASSERT_TRUE(labels[1] != labels[2]);
    ASSERT_TRUE(labels[0] != labels[2]);

    int *nodes = NULL;
    size_t count = 0;
    ASSERT_EQ(graph_largest_scc(g, &nodes, &count), STATUS_OK);
    /* Tie rule: all components size 1, choose one containing smallest node index (node 0) */
    ASSERT_EQ(count, 1);
    ASSERT_EQ(nodes[0], 0);

    free(nodes);
    graph_destroy(g);
}

/* 5. Two 2-cycles joined by ONE directed edge = 2 components with tie rule */
static void test_scc_two_cycles_tie_rule(void) {
    Graph *g = graph_create();
    /* Cycle A: nodes 0, 1 (2 nodes) */
    graph_add_node(g, 10, 12.0, 77.0, "A0", 0);
    graph_add_node(g, 11, 12.0, 77.1, "A1", 0);
    graph_add_edge(g, 0, 1, 50.0, 5.0, "A01", ROAD_PRIMARY);
    graph_add_edge(g, 1, 0, 50.0, 5.0, "A10", ROAD_PRIMARY);

    /* Cycle B: nodes 2, 3 (2 nodes) */
    graph_add_node(g, 20, 12.1, 77.0, "B0", 0);
    graph_add_node(g, 21, 12.1, 77.1, "B1", 0);
    graph_add_edge(g, 2, 3, 50.0, 5.0, "B01", ROAD_PRIMARY);
    graph_add_edge(g, 3, 2, 50.0, 5.0, "B10", ROAD_PRIMARY);

    /* One directed bridge from Cycle A to Cycle B */
    graph_add_edge(g, 1, 2, 100.0, 10.0, "Bridge", ROAD_PRIMARY);

    int labels[4];
    size_t num_comps = 0;
    ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_OK);
    ASSERT_EQ(num_comps, 2);
    ASSERT_EQ(labels[0], labels[1]);
    ASSERT_EQ(labels[2], labels[3]);
    ASSERT_TRUE(labels[0] != labels[2]);

    /* Both components have size 2. Tie rule must choose Cycle A (smallest node index 0 < 2) */
    int *nodes = NULL;
    size_t count = 0;
    ASSERT_EQ(graph_largest_scc(g, &nodes, &count), STATUS_OK);
    ASSERT_EQ(count, 2);
    ASSERT_EQ(nodes[0], 0);
    ASSERT_EQ(nodes[1], 1);

    free(nodes);
    graph_destroy(g);
}

/* 6. Bidirectional 10x10 synthetic grid = 1 component of 100 */
static void test_scc_synthetic_grid(void) {
    Graph *g = NULL;
    Status st = graph_build_synthetic_grid(&g, 10, 10, 12.9, 77.5, 100.0, 42);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(graph_node_count(g), 100);

    int labels[100];
    size_t num_comps = 0;
    ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_OK);
    ASSERT_EQ(num_comps, 1);

    int *nodes = NULL;
    size_t count = 0;
    ASSERT_EQ(graph_largest_scc(g, &nodes, &count), STATUS_OK);
    ASSERT_EQ(count, 100);
    for (size_t i = 0; i < 100; i++) {
        ASSERT_EQ(nodes[i], (int)i);
    }

    free(nodes);
    graph_destroy(g);
}

/* 7. Blocked edges are ignored (traversed regardless of blocked flag) */
static void test_scc_blocked_edges_ignored(void) {
    Graph *g = graph_create();
    graph_add_node(g, 1, 12.0, 77.0, "N0", 0);
    graph_add_node(g, 2, 12.1, 77.1, "N1", 0);

    int e01 = graph_add_edge(g, 0, 1, 100.0, 10.0, "e01", ROAD_PRIMARY);
    int e10 = graph_add_edge(g, 1, 0, 100.0, 10.0, "e10", ROAD_PRIMARY);

    /* Block both edges */
    graph_set_blocked(g, e01, 1);
    graph_set_blocked(g, e10, 1);

    int labels[2];
    size_t num_comps = 0;
    ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_OK);
    /* Since blocked flag is ignored by SCC, this remains 1 component */
    ASSERT_EQ(num_comps, 1);
    ASSERT_EQ(labels[0], labels[1]);

    int *nodes = NULL;
    size_t count = 0;
    ASSERT_EQ(graph_largest_scc(g, &nodes, &count), STATUS_OK);
    ASSERT_EQ(count, 2);
    ASSERT_EQ(nodes[0], 0);
    ASSERT_EQ(nodes[1], 1);

    free(nodes);
    graph_destroy(g);
}

/* 8. 5000-node one-way chain does not crash (iterative Tarjan) */
static void test_scc_deep_chain_iterative(void) {
    const size_t CHAIN_LEN = 5000;
    Graph *g = graph_create();
    ASSERT_NOT_NULL(g);

    for (size_t i = 0; i < CHAIN_LEN; i++) {
        graph_add_node(g, (int64_t)(1000 + i), 12.0 + (double)i * 0.0001, 77.0, "", 0);
        if (i > 0) {
            graph_add_edge(g, (int)(i - 1), (int)i, 10.0, 1.0, "c", ROAD_PRIMARY);
        }
    }

    int *labels = (int *)malloc(CHAIN_LEN * sizeof(int));
    ASSERT_NOT_NULL(labels);
    size_t num_comps = 0;
    ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_OK);
    ASSERT_EQ(num_comps, CHAIN_LEN);

    int *nodes = NULL;
    size_t count = 0;
    ASSERT_EQ(graph_largest_scc(g, &nodes, &count), STATUS_OK);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(nodes[0], 0); /* Smallest node index among components of size 1 */

    free(nodes);
    free(labels);
    graph_destroy(g);
}

/* 9. ORACLE: 10 seeded random digraphs (60 nodes, ~150 edges) vs brute-force reachability */
static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void test_scc_oracle_reachability(void) {
    const int N_NODES = 60;
    const int N_EDGES = 150;
    const int N_RUNS = 10;
    uint32_t rng_state = 123456789;

    for (int run = 0; run < N_RUNS; run++) {
        Graph *g = graph_create();
        for (int i = 0; i < N_NODES; i++) {
            graph_add_node(g, (int64_t)(i + 1), 12.0 + (double)i * 0.01, 77.0, "", 0);
        }

        /* Build adjacency matrix for transitive closure reachability check */
        int reach[60][60];
        memset(reach, 0, sizeof(reach));
        for (int i = 0; i < N_NODES; i++) {
            reach[i][i] = 1;
        }

        for (int e = 0; e < N_EDGES; e++) {
            int u = (int)(xorshift32(&rng_state) % (uint32_t)N_NODES);
            int v = (int)(xorshift32(&rng_state) % (uint32_t)N_NODES);
            if (u != v) {
                graph_add_edge(g, u, v, 100.0, 10.0, "rnd", ROAD_PRIMARY);
                reach[u][v] = 1;
            }
        }

        /* Floyd-Warshall reachability */
        for (int k = 0; k < N_NODES; k++) {
            for (int i = 0; i < N_NODES; i++) {
                for (int j = 0; j < N_NODES; j++) {
                    if (reach[i][k] && reach[k][j]) {
                        reach[i][j] = 1;
                    }
                }
            }
        }

        int labels[60];
        size_t num_comps = 0;
        ASSERT_EQ(graph_scc_label(g, labels, &num_comps), STATUS_OK);

        /* Oracle check: u and v share label iff reach[u][v] && reach[v][u] */
        for (int u = 0; u < N_NODES; u++) {
            for (int v = 0; v < N_NODES; v++) {
                int same_scc = (labels[u] == labels[v]);
                int mutually_reachable = (reach[u][v] && reach[v][u]);
                ASSERT_EQ(same_scc, mutually_reachable);
            }
        }

        int *largest_nodes = NULL;
        size_t largest_count = 0;
        ASSERT_EQ(graph_largest_scc(g, &largest_nodes, &largest_count), STATUS_OK);
        ASSERT_TRUE(largest_count >= 1);
        ASSERT_TRUE(largest_count <= (size_t)N_NODES);

        /* Verify ascending order of largest_nodes */
        for (size_t i = 1; i < largest_count; i++) {
            ASSERT_TRUE(largest_nodes[i - 1] < largest_nodes[i]);
        }

        free(largest_nodes);
        graph_destroy(g);
    }
}

int main(void) {
    RUN_TEST(test_scc_null_and_empty);
    RUN_TEST(test_scc_single_node);
    RUN_TEST(test_scc_3_cycle);
    RUN_TEST(test_scc_chain);
    RUN_TEST(test_scc_two_cycles_tie_rule);
    RUN_TEST(test_scc_synthetic_grid);
    RUN_TEST(test_scc_blocked_edges_ignored);
    RUN_TEST(test_scc_deep_chain_iterative);
    RUN_TEST(test_scc_oracle_reachability);

    return test_summary();
}
