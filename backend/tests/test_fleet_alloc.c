#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "test_util.h"
#include "fleet_alloc.h"
#include "network.h"
#include "types.h"
#include "demand_model.h"

#define ASSERT_TRUE(c) TEST_ASSERT(c)
#define ASSERT_EQ(a,b) TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p) TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_APPROX(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))
#define TEST_START(name) (void)(name)
#define TEST_PASS() do {} while(0)

/* Helper to build a line graph network with 8 stops and 2 routes */
static Network *create_line_network(void) {
    DemandStop stops[8];
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

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = 2;
    cfg.dwell_s_per_stop = 20.0;
    cfg.build_matrix = 1;

    Network *net = NULL;
    Status st = network_build(g, stops, 8, &cfg, &net);
    if (st != STATUS_OK) {
        graph_destroy(g);
        return NULL;
    }
    graph_destroy(g);
    return net;
}

static void test_fleet_equal(void) {
    TEST_START("Fleet Targets Equal");

    int targets[4];

    /* 4 routes, 10 buses -> [3, 3, 2, 2] */
    Status st = fleet_targets_equal(4, 10, targets);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(targets[0], 3);
    ASSERT_EQ(targets[1], 3);
    ASSERT_EQ(targets[2], 2);
    ASSERT_EQ(targets[3], 2);

    /* 4 routes, 8 buses -> [2, 2, 2, 2] */
    st = fleet_targets_equal(4, 8, targets);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(targets[0], 2);
    ASSERT_EQ(targets[1], 2);
    ASSERT_EQ(targets[2], 2);
    ASSERT_EQ(targets[3], 2);

    /* 3 routes, 1 bus -> [1, 0, 0] */
    st = fleet_targets_equal(3, 1, targets);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(targets[0], 1);
    ASSERT_EQ(targets[1], 0);
    ASSERT_EQ(targets[2], 0);

    /* Error cases */
    ASSERT_EQ(fleet_targets_equal(0, 10, targets), STATUS_ERR_INVALID);
    ASSERT_EQ(fleet_targets_equal(4, 10, NULL), STATUS_ERR_NULL);

    TEST_PASS();
}

static void test_fleet_proportional(void) {
    TEST_START("Fleet Targets Proportional (Hamilton)");

    int targets[3];

    /* weights = [10.0, 20.0, 30.0], 6 buses, min_per_route = 1 */
    /* remaining = 3, sum = 60. quotas: 0.5, 1.0, 1.5. ints: 0, 1, 1. fracs: 0.5, 0.0, 0.5. */
    /* leftover = 1. tie break smaller route ID -> route 0 gets +1 -> [2, 2, 2] */
    double w1[3] = {10.0, 20.0, 30.0};
    Status st = fleet_targets_proportional(w1, 3, 6, 1, targets);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(targets[0], 2);
    ASSERT_EQ(targets[1], 2);
    ASSERT_EQ(targets[2], 2);

    /* weights = [10.0, 20.0, 30.0], 7 buses, min_per_route = 1 */
    /* remaining = 4, quotas: 0.6667, 1.3333, 2.0. ints: 0, 1, 2. fracs: 0.6667, 0.3333, 0.0 */
    /* leftover = 1 -> route 0 gets +1 -> [2, 2, 3] */
    st = fleet_targets_proportional(w1, 3, 7, 1, targets);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(targets[0], 2);
    ASSERT_EQ(targets[1], 2);
    ASSERT_EQ(targets[2], 3);

    /* All weights 0.0 -> behaves like equal on remainder */
    double w_zero[3] = {0.0, 0.0, 0.0};
    st = fleet_targets_proportional(w_zero, 3, 5, 1, targets);
    ASSERT_EQ(st, STATUS_OK);
    /* min gives [1, 1, 1], remaining 2 distributed equally to lowest IDs -> [2, 2, 1] */
    ASSERT_EQ(targets[0], 2);
    ASSERT_EQ(targets[1], 2);
    ASSERT_EQ(targets[2], 1);

    /* Insufficient buses for min_per_route */
    st = fleet_targets_proportional(w1, 3, 2, 1, targets);
    ASSERT_EQ(st, STATUS_ERR_INVALID);

    /* NULL and bounds */
    ASSERT_EQ(fleet_targets_proportional(NULL, 3, 6, 1, targets), STATUS_ERR_NULL);
    ASSERT_EQ(fleet_targets_proportional(w1, 3, 6, 1, NULL), STATUS_ERR_NULL);
    ASSERT_EQ(fleet_targets_proportional(w1, 0, 6, 1, targets), STATUS_ERR_INVALID);

    TEST_PASS();
}

static void test_fleet_greedy_marginal_vs_demand_per_bus(void) {
    TEST_START("Fleet Greedy Marginal Wait vs Demand Per Bus");

    /*
     * Counterexample demonstrating that MARGINAL_WAIT achieves strictly lower
     * waiting time proxy than DEMAND_PER_BUS when cycle times differ:
     *
     * Route 0: demand = 120.0, cycle = 600.0 s.
     * Route 1: demand = 100.0, cycle = 3600.0 s.
     * n_buses = 3, min_per_route = 1 (1 extra bus to assign).
     */
    double demand[2] = {120.0, 100.0};
    double cycle_s[2] = {600.0, 3600.0};
    int targets_dpb[2];
    int targets_mw[2];

    Status st1 = fleet_targets_greedy(demand, cycle_s, 2, 3, 1, FLEET_PRIO_DEMAND_PER_BUS, targets_dpb);
    ASSERT_EQ(st1, STATUS_OK);
    /* Demand per bus: R0 priority = 120 / 2 = 60; R1 priority = 100 / 2 = 50 -> assigns to R0 */
    ASSERT_EQ(targets_dpb[0], 2);
    ASSERT_EQ(targets_dpb[1], 1);

    Status st2 = fleet_targets_greedy(demand, cycle_s, 2, 3, 1, FLEET_PRIO_MARGINAL_WAIT, targets_mw);
    ASSERT_EQ(st2, STATUS_OK);
    /* Marginal wait: R0 = 120*600/2 = 36000; R1 = 100*3600/2 = 180000 -> assigns to R1 */
    ASSERT_EQ(targets_mw[0], 1);
    ASSERT_EQ(targets_mw[1], 2);

    double wait_dpb = fleet_wait_proxy_s(demand, cycle_s, targets_dpb, 2);
    double wait_mw = fleet_wait_proxy_s(demand, cycle_s, targets_mw, 2);

    /* wait_dpb = (120*150 + 100*1800) / 220 = 900.0 s */
    /* wait_mw = (120*300 + 100*900) / 220 = 572.727 s */
    ASSERT_APPROX(wait_dpb, 900.0, 1e-4);
    ASSERT_APPROX(wait_mw, 572.72727, 1e-4);
    ASSERT_TRUE(wait_mw < wait_dpb - 300.0);

    /* Cross-check priority formula with types.h route_compute_priority */
    Route *r_test = route_create(0, "TestRoute", 120.0);
    r_test->assigned_buses = 1;
    double type_prio = route_compute_priority(r_test);
    ASSERT_APPROX(type_prio, 60.0, 1e-9); /* 120 / (1 + 1) = 60 */
    route_destroy(r_test);

    /*
     * Specification hand case:
     * Demand [30, 10, 5], cycles [600, 1200, 600], N = 8 buses, min_per_route = 1:
     * - Marginal wait targets: [3, 3, 2], proxy = 127.77777777777777 s
     * - Demand per bus targets: [5, 2, 1], proxy = 140.0 s
     * - Proportional targets: [4, 2, 2], proxy = 133.33333333333334 s
     */
    double d3[3] = {30.0, 10.0, 5.0};
    double c3[3] = {600.0, 1200.0, 600.0};
    int t_mw[3];
    int t_dpb[3];
    int t_prop[3];

    ASSERT_EQ(fleet_targets_greedy(d3, c3, 3, 8, 1, FLEET_PRIO_MARGINAL_WAIT, t_mw), STATUS_OK);
    ASSERT_EQ(t_mw[0], 3);
    ASSERT_EQ(t_mw[1], 3);
    ASSERT_EQ(t_mw[2], 2);

    ASSERT_EQ(fleet_targets_greedy(d3, c3, 3, 8, 1, FLEET_PRIO_DEMAND_PER_BUS, t_dpb), STATUS_OK);
    ASSERT_EQ(t_dpb[0], 5);
    ASSERT_EQ(t_dpb[1], 2);
    ASSERT_EQ(t_dpb[2], 1);

    ASSERT_EQ(fleet_targets_proportional(d3, 3, 8, 1, t_prop), STATUS_OK);
    ASSERT_EQ(t_prop[0], 4);
    ASSERT_EQ(t_prop[1], 2);
    ASSERT_EQ(t_prop[2], 2);

    double p_mw = fleet_wait_proxy_s(d3, c3, t_mw, 3);
    double p_dpb = fleet_wait_proxy_s(d3, c3, t_dpb, 3);
    double p_prop = fleet_wait_proxy_s(d3, c3, t_prop, 3);

    ASSERT_APPROX(p_mw, 127.77777777777777, 1e-6);
    ASSERT_APPROX(p_dpb, 140.0, 1e-6);
    ASSERT_APPROX(p_prop, 133.33333333333334, 1e-6);

    TEST_PASS();
}

static void test_fleet_wait_proxy_by_hand(void) {
    TEST_START("Fleet Wait Proxy Hand Calculation");

    double demand[2] = {100.0, 200.0};
    double cycle_s[2] = {1200.0, 2400.0};
    int counts[2] = {2, 4};

    /* wait_0 = 1200 / (2 * 2) = 300 s; wait_1 = 2400 / (2 * 4) = 300 s */
    /* proxy = (100 * 300 + 200 * 300) / 300 = 300.0 s */
    double p = fleet_wait_proxy_s(demand, cycle_s, counts, 2);
    ASSERT_APPROX(p, 300.0, 1e-9);

    /* Zero buses on route 0 with demand -> wait_0 = 3600.0 s */
    int counts_zero[2] = {0, 2};
    /* wait_0 = 3600, wait_1 = 2400 / (2 * 2) = 600 */
    /* proxy = (100 * 3600 + 200 * 600) / 300 = (360000 + 120000) / 300 = 1600.0 s */
    double p_zero = fleet_wait_proxy_s(demand, cycle_s, counts_zero, 2);
    ASSERT_APPROX(p_zero, 1600.0, 1e-9);

    /* Total demand 0 -> 0.0 */
    double demand_zero[2] = {0.0, 0.0};
    ASSERT_APPROX(fleet_wait_proxy_s(demand_zero, cycle_s, counts, 2), 0.0, 1e-9);

    /* Negative count or NaN -> NAN */
    int counts_bad[2] = {-1, 2};
    ASSERT_TRUE(isnan(fleet_wait_proxy_s(demand, cycle_s, counts_bad, 2)));
    ASSERT_TRUE(isnan(fleet_wait_proxy_s(NULL, cycle_s, counts, 2)));

    TEST_PASS();
}

static void test_fleet_greedy_optimality_oracle(void) {
    TEST_START("Fleet Greedy Optimality Oracle (200 instances vs Brute Force)");

    /*
     * For R=3 routes, B=7 buses, min_per_route=1:
     * 4 extra buses distributed among 3 routes: (c0, c1, c2) with c0+c1+c2=7, ci >= 1.
     * Number of combinations is (4 + 3 - 1) choose 2 = 15.
     * We brute-force all 15 valid allocations and check that greedy MARGINAL_WAIT
     * always achieves the minimum wait proxy within 1e-9.
     */
    uint64_t state = 42;
    for (int iter = 0; iter < 200; iter++) {
        double demand[3];
        double cycle[3];
        for (int r = 0; r < 3; r++) {
            /* LCG pseudo-random */
            state = state * 6364136223846793005ULL + 1;
            demand[r] = 10.0 + (double)(state % 500);
            state = state * 6364136223846793005ULL + 1;
            cycle[r] = 300.0 + (double)(state % 3000);
        }

        /* Brute-force search */
        double best_proxy = 1e30;
        int best_alloc[3] = {0, 0, 0};
        for (int c0 = 1; c0 <= 5; c0++) {
            for (int c1 = 1; c1 <= 7 - c0 - 1; c1++) {
                int c2 = 7 - c0 - c1;
                if (c2 < 1) continue;
                int trial[3] = {c0, c1, c2};
                double px = fleet_wait_proxy_s(demand, cycle, trial, 3);
                if (px < best_proxy) {
                    best_proxy = px;
                    best_alloc[0] = c0;
                    best_alloc[1] = c1;
                    best_alloc[2] = c2;
                }
            }
        }

        /* Greedy allocation */
        int greedy_alloc[3];
        Status st = fleet_targets_greedy(demand, cycle, 3, 7, 1, FLEET_PRIO_MARGINAL_WAIT, greedy_alloc);
        ASSERT_EQ(st, STATUS_OK);
        double greedy_proxy = fleet_wait_proxy_s(demand, cycle, greedy_alloc, 3);

        ASSERT_APPROX(greedy_proxy, best_proxy, 1e-9);
        (void)best_alloc;
    }

    TEST_PASS();
}

static void test_fleet_plan_moves_all_cases(void) {
    TEST_START("Fleet Plan Moves (Balanced, Surplus->Deficit, Reserve, Depot, Load)");

    Network *net = create_line_network();
    ASSERT_NOT_NULL(net);

    /* Case a: Balanced (current == targets) -> 0 moves */
    FleetBus buses_a[4] = {
        {1, 0, 0, 0},
        {2, 0, 1, 0},
        {3, 1, 4, 0},
        {4, 1, 5, 0}
    };
    int targets_a[2] = {2, 2};
    FleetMove moves[8];
    size_t n_moves = 0;
    Status st = fleet_plan_moves(net, buses_a, 4, targets_a, 2, moves, 8, &n_moves);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_moves, 0);

    /* Case b: Surplus on R0, Deficit on R1 -> bus moves from R0 to R1 */
    FleetBus buses_b[4] = {
        {10, 0, 0, 5},
        {11, 0, 1, 0},  /* lowest load on R0 */
        {12, 0, 2, 10},
        {20, 1, 5, 0}
    };
    int targets_b[2] = {2, 2};
    st = fleet_plan_moves(net, buses_b, 4, targets_b, 2, moves, 8, &n_moves);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_moves, 1);
    ASSERT_EQ(moves[0].bus_id, 11);
    ASSERT_EQ(moves[0].from_route, 0);
    ASSERT_EQ(moves[0].to_route, 1);
    ASSERT_TRUE(moves[0].deadhead_s >= 0.0);
    ASSERT_TRUE(moves[0].entry_stop >= 4 && moves[0].entry_stop <= 7);

    /* Case c: Reserve bus (route == -1) fills deficit on R0 */
    FleetBus buses_c[4] = {
        {31, 0, 0, 0},
        {32, 1, 4, 0},
        {33, 1, 5, 0},
        {99, -1, 1, 0} /* reserve at stop 1 */
    };
    int targets_c[2] = {2, 2};
    st = fleet_plan_moves(net, buses_c, 4, targets_c, 2, moves, 8, &n_moves);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_moves, 1);
    ASSERT_EQ(moves[0].bus_id, 99);
    ASSERT_EQ(moves[0].from_route, -1);
    ASSERT_EQ(moves[0].to_route, 0);

    /* Case d: Surplus bus moved to depot/reserve (to_route = -1) when sum(targets) < sum(current) */
    FleetBus buses_d[4] = {
        {41, 0, 0, 2},
        {42, 0, 1, 0}, /* lowest load on R0 */
        {43, 1, 4, 0},
        {44, 1, 5, 0}
    };
    int targets_d[2] = {1, 2}; /* Total target 3 vs 4 buses */
    st = fleet_plan_moves(net, buses_d, 4, targets_d, 2, moves, 8, &n_moves);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_moves, 1);
    ASSERT_EQ(moves[0].bus_id, 42);
    ASSERT_EQ(moves[0].from_route, 0);
    ASSERT_EQ(moves[0].to_route, -1);
    ASSERT_EQ(moves[0].entry_stop, -1);
    ASSERT_APPROX(moves[0].deadhead_s, 0.0, 1e-9);

    /* Case e: Load ordering check (buses with lower load released first) */
    FleetBus buses_e[4] = {
        {51, 0, 0, 20},
        {52, 0, 1, 5},
        {53, 0, 2, 10},
        {54, 1, 4, 0}
    };
    int targets_e[2] = {1, 3}; /* R0 surplus of 2 buses; should release 52 (load 5) and 53 (load 10) */
    st = fleet_plan_moves(net, buses_e, 4, targets_e, 2, moves, 8, &n_moves);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_moves, 2);
    ASSERT_EQ(moves[0].bus_id, 52);
    ASSERT_EQ(moves[1].bus_id, 53);

    network_destroy(net);
    TEST_PASS();
}

static void test_fleet_pick_nearest(void) {
    TEST_START("Fleet Pick Nearest");

    Network *net = create_line_network();
    ASSERT_NOT_NULL(net);

    FleetBus cands[3] = {
        {1, 0, 0, 0}, /* stop 0 */
        {2, 0, 2, 0}, /* stop 2 */
        {3, 1, 6, 0}  /* stop 6: already on route 1 */
    };

    size_t idx = 999;
    double dh = -1.0;
    int entry = -1;

    Status st = fleet_pick_nearest(net, cands, 3, 1, &idx, &dh, &entry);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(idx, 2); /* Bus 3 at stop 6 is nearest */
    ASSERT_APPROX(dh, 0.0, 1e-9);
    ASSERT_EQ(entry, 6);

    network_destroy(net);
    TEST_PASS();
}

static void test_fleet_edge_cases(void) {
    TEST_START("Fleet Edge Cases and Validation");

    int targets[2];
    double demand[2] = {100.0, 200.0};
    double cycle[2] = {1000.0, 2000.0};

    ASSERT_EQ(fleet_targets_greedy(NULL, cycle, 2, 4, 1, FLEET_PRIO_DEMAND_PER_BUS, targets), STATUS_ERR_NULL);
    ASSERT_EQ(fleet_targets_greedy(demand, cycle, 2, 4, 1, FLEET_PRIO_DEMAND_PER_BUS, NULL), STATUS_ERR_NULL);
    ASSERT_EQ(fleet_targets_greedy(demand, NULL, 2, 4, 1, FLEET_PRIO_MARGINAL_WAIT, targets), STATUS_ERR_NULL);
    ASSERT_EQ(fleet_targets_greedy(demand, cycle, 2, 1, 1, FLEET_PRIO_DEMAND_PER_BUS, targets), STATUS_ERR_INVALID);
    ASSERT_EQ(fleet_targets_greedy(demand, cycle, 2, 4, 0, FLEET_PRIO_MARGINAL_WAIT, targets), STATUS_ERR_INVALID);

    TEST_PASS();
}

static void test_fleet_greedy_optimality_oracle_spec(void) {
    TEST_START("Fleet Greedy Optimality Oracle Spec (200 instances, R=4, N in 4..12)");

    uint64_t seed = 42;
    for (int iter = 0; iter < 200; iter++) {
        uint64_t h_n = demand_hash(seed, (uint64_t)iter, 0, 0);
        int n_buses = 4 + (int)(demand_unit(h_n) * 9.0);
        if (n_buses > 12) n_buses = 12;

        double demand[4];
        double cycle[4];
        for (int r = 0; r < 4; r++) {
            uint64_t hd = demand_hash(seed, (uint64_t)iter, (uint64_t)r, 1);
            demand[r] = demand_unit(hd) * 100.0;
            uint64_t hc = demand_hash(seed, (uint64_t)iter, (uint64_t)r, 2);
            cycle[r] = 300.0 + demand_unit(hc) * 2100.0;
        }

        /* Brute-force search over every composition of N into 4 parts >= 1 */
        double best_proxy = 1e30;
        for (int c0 = 1; c0 <= n_buses - 3; c0++) {
            for (int c1 = 1; c1 <= n_buses - c0 - 2; c1++) {
                for (int c2 = 1; c2 <= n_buses - c0 - c1 - 1; c2++) {
                    int c3 = n_buses - c0 - c1 - c2;
                    if (c3 < 1) continue;
                    int trial[4] = {c0, c1, c2, c3};
                    double px = fleet_wait_proxy_s(demand, cycle, trial, 4);
                    if (px < best_proxy) {
                        best_proxy = px;
                    }
                }
            }
        }

        int greedy_alloc[4] = {0, 0, 0, 0};
        Status st = fleet_targets_greedy(demand, cycle, 4, (size_t)n_buses, 1,
                                         FLEET_PRIO_MARGINAL_WAIT, greedy_alloc);
        ASSERT_EQ(st, STATUS_OK);

        int sum = 0;
        for (int r = 0; r < 4; r++) {
            ASSERT_TRUE(greedy_alloc[r] >= 1);
            sum += greedy_alloc[r];
        }
        ASSERT_EQ(sum, n_buses);

        double greedy_proxy = fleet_wait_proxy_s(demand, cycle, greedy_alloc, 4);
        double max_proxy = (best_proxy > 1.0) ? best_proxy : 1.0;
        ASSERT_TRUE(fabs(greedy_proxy - best_proxy) <= 1e-9 * max_proxy);
    }

    TEST_PASS();
}

int main(void) {
    RUN_TEST(test_fleet_equal);
    RUN_TEST(test_fleet_proportional);
    RUN_TEST(test_fleet_greedy_marginal_vs_demand_per_bus);
    RUN_TEST(test_fleet_wait_proxy_by_hand);
    RUN_TEST(test_fleet_greedy_optimality_oracle);
    RUN_TEST(test_fleet_plan_moves_all_cases);
    RUN_TEST(test_fleet_pick_nearest);
    RUN_TEST(test_fleet_edge_cases);
    RUN_TEST(test_fleet_greedy_optimality_oracle_spec);

    return test_summary();
}
