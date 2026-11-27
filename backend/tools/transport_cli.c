#define _POSIX_C_SOURCE 200809L

#include "graph.h"
#include "pathfind.h"
#include "reroute.h"
#include "traffic_sim.h"
#include "traffic_client.h"
#include "osm_loader.h"
#include "graph_scc.h"
#include "demand_model.h"
#include "ai_inference.h"
#include "ai_demand.h"
#include "network.h"
#include "fleet_alloc.h"
#include "anomaly.h"
#include "sim.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

/* Global or configured data directory */
static const char *g_data_dir = "data";

static void resolve_data_dir(const char *override_dir) {
    if (override_dir && override_dir[0]) {
        g_data_dir = override_dir;
        return;
    }
    struct stat st;
    if (stat("data", &st) == 0 && S_ISDIR(st.st_mode)) {
        g_data_dir = "data";
    } else if (stat("../data", &st) == 0 && S_ISDIR(st.st_mode)) {
        g_data_dir = "../data";
    } else {
        g_data_dir = "data";
    }
}

/* ---- Helpers ---- */

static double now_monotonic_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void print_path(const Graph *g, const PathResult *p, const char *label) {
    (void)g;
    if (!p || !p->found) {
        printf("  %s: NOT FOUND\n", label);
        return;
    }
    printf("  %s: %.1f s, %.0f m, %zu nodes, %zu edges explored=%zu\n",
           label, p->total_time_s, p->total_length_m,
           p->node_count, p->edge_count, p->nodes_explored);
}

static void print_path_nodes(const Graph *g, const PathResult *p) {
    if (!p || !p->found || !p->nodes) return;
    printf("  Path nodes: ");
    for (size_t i = 0; i < p->node_count; i++) {
        const GraphNode *n = graph_node(g, p->nodes[i]);
        if (n && n->name[0])
            printf("%s", n->name);
        else
            printf("%d", p->nodes[i]);
        if (i + 1 < p->node_count) printf(" -> ");
    }
    printf("\n");
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <command> [options]\n\n"
        "Global Options:\n"
        "  --data-dir <path>          Path to data directory (default: data or ../data)\n\n"
        "Commands:\n"
        "  demo                       6-node walkthrough\n"
        "  info [--synthetic]         Graph statistics\n"
        "  route <src> <dst> [opts]   Find route between two nodes\n"
        "    --synthetic              Use synthetic grid graph\n"
        "    --hour H                 Hour of day (0-23, default 9)\n"
        "    --rain                   Apply rain multiplier\n"
        "    --algo astar|dijkstra    Algorithm (default: astar)\n"
        "    --static                 Use static cost (no traffic)\n"
        "  bench [opts]               Benchmark pathfinding\n"
        "    --synthetic              Use synthetic graph\n"
        "    --pairs N                Number of random pairs (default: 200)\n"
        "    --seed S                 PRNG seed (default: 42)\n"
        "    --hour H                 Hour for traffic sim\n"
        "    --rain                   Apply rain multiplier\n"
        "  osm [--refresh] [--offline] Load/fetch OSM graph\n"
        "  traffic [--offline]        Fetch live traffic (falls back to sim)\n"
        "  stops-info [opts]          Bangalore stops & SCC breakdown\n"
        "    --cache <path>           Graph cache path\n"
        "    --stops/--max-stops N    Max stops to sample (default: 60)\n"
        "    --seed S                 PRNG seed (default: 42)\n"
        "    --stride-only            Use uniform stride selection instead of anchored\n"
        "  gen-demand [opts]          Generate synthetic demand dataset\n"
        "    --cache <path>           Graph cache path\n"
        "    --out <path>             Output CSV path (default: data/demand_history.csv)\n"
        "    --days D                 Days of history (default: 120)\n"
        "    --stops/--max-stops N    Number of stops (default: 60)\n"
        "    --seed S                 PRNG seed (default: 42)\n"
        "    --synthetic-stops        Generate synthetic stops without graph cache\n"
        "    --stride-only            Use uniform stride selection instead of anchored\n",
        prog);
    fprintf(stderr,
        "  demand-eval [opts]         Evaluate demand predictors\n"
        "    --train <path>           Train CSV path\n"
        "    --test <path>            Test CSV path\n"
        "    --model <path>           Random Forest model text file\n"
        "    --expect-forest-mae V    Validate forest MAE is within tol\n"
        "    --tol T                  Tolerance for expected MAE (default: 0.5)\n"
        "  golden-check [opts]        Verify C inference against golden predictions\n"
        "    --model <path>           Random Forest model text file\n"
        "    --golden <path>          Golden CSV path\n"
        "    --tol T                  Max error tolerance (default: 1e-6)\n"
        "  network-info [opts]        Bus network topology and route info\n"
        "    --cache <path>           Graph cache path\n"
        "    --stops/--max-stops N    Number of stops (default: 60)\n"
        "    --routes R               Number of routes (default: 4)\n"
        "    --seed S                 PRNG seed (default: 42)\n"
        "    --no-matrix              Skip all-pairs travel time matrix\n"
        "  alloc-demo [opts]          Fleet allocation strategies and move planning\n"
        "    --cache <path>           Graph cache path\n"
        "    --stops N                Number of stops (default: 60)\n"
        "    --routes R               Number of routes (default: 4)\n"
        "    --buses B                Total fleet size (default: 40)\n"
        "    --min-per-route M        Minimum buses per route (default: 2)\n"
        "    --day wkday|wkend|hol    Day type (default: weekday)\n"
        "    --hour H                 Hour of day (0-23, default: 9)\n"
        "    --rain                   Apply rain factor\n"
        "    --model-rf <path>        Path to random forest model\n"
        "    --train <path>           Path to train CSV for OLS predictor\n"
        "  sim-run [opts]             Discrete-event public transport bus simulation\n"
        "    --stops N                Number of stops (default: 60)\n"
        "    --routes R               Number of routes (default: 6)\n"
        "    --buses B                Total fleet size (default: 36)\n"
        "    --capacity C             Bus passenger capacity (default: 60)\n"
        "    --reserve K              Reserve depot buses (default: 0)\n"
        "    --policy P               fixed-equal|fixed-prop|greedy-spec|greedy-marginal\n"
        "    --predictor PR           ma|rf|oracle (default: ma)\n"
        "    --start/--end HH:MM      Time window (default: 06:00 to 22:00)\n"
        "    --warmup W               Warmup time in min (default: 30)\n"
        "    --slice S                Slice interval in min (default: 15)\n"
        "    --noise N                Travel time noise fraction (default: 0.10)\n"
        "    --realloc-interval R     Reallocation interval in min (default: 30)\n"
        "    --realloc-hysteresis H   Hysteresis threshold (default: 0.02)\n"
        "    --anomaly none|detect|dispatch Anomaly detection mode (default: none)\n"
        "    --spacing stop|time        Initial bus spacing (default: time)\n"
        "    --reassign dump|drain      Reassignment mode (default: drain)\n"
        "    --log                    Print detailed event log\n"
        "  sim-compare [opts]         Multi-policy simulation comparison\n"
        "    (accepts same scenario and network flags as sim-run)\n\n");
}

/* ---- 6-node demo graph ---- */

/*
 * Exactly as in Part 2 specification:
 * Nodes A..F = 0..5.
 * All edges bidirectional:
 *   A-B: 4 s  (40 m)
 *   A-C: 2 s  (20 m)
 *   B-C: 1 s  (10 m)
 *   B-D: 5 s  (50 m)
 *   C-D: 8 s  (80 m)
 *   C-E: 10 s (100 m)
 *   D-E: 2 s  (20 m)
 *   D-F: 6 s  (60 m)
 *   E-F: 3 s  (30 m)
 * length_m = 10 * time_s
 */
static Graph *build_spec_6node_graph(int *edge_bd_out, int *edge_ac_out) {
    Graph *g = graph_create();
    if (!g) return NULL;

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
    graph_add_bidirectional(g, 3, 5, 60.0, 6.0, "D-F", ROAD_PRIMARY, NULL, NULL);
    graph_add_bidirectional(g, 4, 5, 30.0, 3.0, "E-F", ROAD_PRIMARY, NULL, NULL);

    int e_ac = -1;
    for (size_t k = 0; k < graph_out_degree(g, 0); k++) {
        int eid = graph_out_edge(g, 0, k);
        if (graph_edge(g, eid)->to == 2) { e_ac = eid; break; }
    }

    if (edge_bd_out) *edge_bd_out = e_bd;
    if (edge_ac_out) *edge_ac_out = e_ac;
    return g;
}

/* ---- command: demo ---- */

static int cmd_demo(void) {
    printf("=== Demo: Spec 6-node Bangalore reference graph ===\n\n");
    printf("Nodes: A=0, B=1, C=2, D=3, E=4, F=5\n");
    printf("Edges (all bidirectional, length_m = 10 * time_s):\n");
    printf("  A-B(4s), A-C(2s), B-C(1s), B-D(5s), C-D(8s), C-E(10s), D-E(2s), D-F(6s), E-F(3s)\n\n");

    int edge_bd = -1, edge_ac = -1;
    Graph *g = build_spec_6node_graph(&edge_bd, &edge_ac);
    if (!g) { fprintf(stderr, "demo: graph_create failed\n"); return 1; }

    printf("--- Step 1: Static routes (no traffic) ---\n");
    PathResult p_af, p_ad, p_ae;
    path_result_init(&p_af);
    path_result_init(&p_ad);
    path_result_init(&p_ae);

    find_path(g, 0, 5, SEARCH_ASTAR, COST_STATIC, &p_af);
    find_path(g, 0, 3, SEARCH_ASTAR, COST_STATIC, &p_ad);
    find_path(g, 0, 4, SEARCH_ASTAR, COST_STATIC, &p_ae);

    printf("A->F: "); print_path(g, &p_af, "A* static");
    print_path_nodes(g, &p_af);
    printf("A->D: "); print_path(g, &p_ad, "A* static");
    print_path_nodes(g, &p_ad);
    printf("A->E: "); print_path(g, &p_ae, "A* static");
    print_path_nodes(g, &p_ae);

    path_result_free(&p_af);
    path_result_free(&p_ad);
    path_result_free(&p_ae);

    printf("\n--- Step 2: Block directed edge B->D (edge id %d), reroute ---\n", edge_bd);
    RouteManager *rm = rm_create(g, 0.0);
    if (!rm) { graph_destroy(g); return 1; }

    rm_track_route(rm, 1, 0, 5); /* A->F */
    rm_track_route(rm, 2, 0, 3); /* A->D */

    const PathResult *before_af = rm_get_path(rm, 1);
    const PathResult *before_ad = rm_get_path(rm, 2);
    printf("Before block:\n");
    print_path(g, before_af, "route 1 A->F");
    print_path_nodes(g, before_af);
    print_path(g, before_ad, "route 2 A->D");
    print_path_nodes(g, before_ad);

    RerouteSummary sum;
    rm_block_edge(rm, edge_bd, &sum);
    printf("\nBlock summary: checked=%zu rerouted=%zu unreachable=%zu\n",
           sum.routes_checked, sum.routes_rerouted, sum.routes_unreachable);

    const PathResult *after_af = rm_get_path(rm, 1);
    const PathResult *after_ad = rm_get_path(rm, 2);
    printf("After block:\n");
    print_path(g, after_af, "route 1 A->F");
    print_path_nodes(g, after_af);
    print_path(g, after_ad, "route 2 A->D");
    print_path_nodes(g, after_ad);

    printf("\nHistory size before undo: %zu\n", rm_history_size(rm));

    printf("\n--- Step 3: Undo block ---\n");
    char desc[128];
    rm_undo(rm, desc, sizeof(desc));
    printf("Undid: \"%s\"\n", desc);
    printf("History size after undo: %zu\n", rm_history_size(rm));

    const PathResult *restored_af = rm_get_path(rm, 1);
    const PathResult *restored_ad = rm_get_path(rm, 2);
    printf("After undo:\n");
    print_path(g, restored_af, "route 1 A->F");
    print_path_nodes(g, restored_af);
    print_path(g, restored_ad, "route 2 A->D");
    print_path_nodes(g, restored_ad);

    const Edge *e_bd = graph_edge(g, edge_bd);
    printf("Edge B->D blocked after undo: %s\n",
           (e_bd && e_bd->blocked) ? "YES (BUG)" : "NO (restored)");

    printf("\n--- Step 4: Traffic factor 3.0 on directed edge A->C ---\n");
    /* Set traffic factor 3.0 on A->C */
    graph_set_traffic_factor(g, edge_ac, 3.0);

    PathResult p_static_eval, p_traffic_opt;
    path_result_init(&p_static_eval);
    path_result_init(&p_traffic_opt);

    find_path(g, 0, 5, SEARCH_ASTAR, COST_STATIC, &p_static_eval);
    find_path(g, 0, 5, SEARCH_ASTAR, COST_TRAFFIC, &p_traffic_opt);

    double static_eval_cost = path_time_s(g, &p_static_eval, COST_TRAFFIC);
    printf("Static path evaluated under traffic: %.1f s (path A,C,B,D,E,F)\n", static_eval_cost);
    printf("Traffic-optimal A* path: ");
    print_path(g, &p_traffic_opt, "A->F optimal traffic A*");
    print_path_nodes(g, &p_traffic_opt);

    path_result_free(&p_static_eval);
    path_result_free(&p_traffic_opt);

    rm_destroy(rm);
    graph_destroy(g);

    printf("\n=== Demo complete ===\n");
    return 0;
}

/* ---- Splitmix64 PRNG ---- */

static uint64_t sm64_state;

static uint64_t sm64_next(void) {
    uint64_t z = (sm64_state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

/* ---- command: bench ---- */

static int cmd_bench(int argc, char **argv) {
    int synthetic = 0;
    int pairs = 200;
    uint64_t seed = 42;
    double hour = 9.0;
    int rain = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--synthetic") == 0) synthetic = 1;
        else if (strcmp(argv[i], "--pairs") == 0 && i+1 < argc) pairs = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed")  == 0 && i+1 < argc) seed  = (uint64_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--hour")  == 0 && i+1 < argc) hour  = atof(argv[++i]);
        else if (strcmp(argv[i], "--rain")  == 0) rain = 1;
    }

    Graph *g = NULL;

    if (synthetic) {
        printf("[bench] Building 40x40 synthetic grid ...\n");
        Status st = graph_build_synthetic_grid(&g, 40, 40, 12.90, 77.54, 500.0, seed);
        if (st != STATUS_OK || !g) {
            fprintf(stderr, "bench: graph_build_synthetic_grid failed: %s\n", status_to_string(st));
            return 1;
        }
    } else {
        OsmConfig cfg;
        osm_config_default(&cfg);
        cfg.data_dir = g_data_dir;
        OsmBuildStats stats;
        printf("[bench] Loading OSM graph (%s/graph_cache.txt or %s/osm_*.json) ...\n", g_data_dir, g_data_dir);
        Status st = osm_load_or_fetch(&cfg, 0, 1, &g, &stats);
        if (st != STATUS_OK || !g) {
            fprintf(stderr, "bench: OSM graph not available (%s). Use --synthetic.\n", status_to_string(st));
            return 1;
        }
        printf("[bench] OSM loaded: %zu nodes, %zu edges\n", graph_node_count(g), graph_edge_count(g));
    }

    /* Apply traffic simulation */
    traffic_sim_apply(g, hour * 60.0, rain);

    size_t N = graph_node_count(g);
    if (N < 2) {
        fprintf(stderr, "bench: graph too small\n");
        graph_destroy(g);
        return 1;
    }

    printf("[bench] %zu nodes, %zu edges, %d pairs, seed=%llu, hour=%.0f, rain=%d\n",
           N, graph_edge_count(g), pairs, (unsigned long long)seed, hour, rain);

    sm64_state = seed;

    long long astar_explored = 0;
    long long dijk_explored  = 0;
    int    matched = 0;
    int    found_count = 0;
    double astar_us = 0.0, dijk_us = 0.0;

    double sum_static_eval_time = 0.0;
    double sum_traffic_opt_time = 0.0;
    int    cmp_pairs_count = 0;

    for (int q = 0; q < pairs; q++) {
        int src = (int)(sm64_next() % N);
        int dst = (int)(sm64_next() % N);
        if (src == dst) { dst = (dst + 1) % (int)N; }

        PathResult pa, pd, p_static;
        path_result_init(&pa);
        path_result_init(&pd);
        path_result_init(&p_static);

        double t0 = now_monotonic_s();
        find_path(g, src, dst, SEARCH_ASTAR,    COST_TRAFFIC, &pa);
        double t1 = now_monotonic_s();
        find_path(g, src, dst, SEARCH_DIJKSTRA, COST_TRAFFIC, &pd);
        double t2 = now_monotonic_s();

        astar_us += (t1 - t0) * 1e6;
        dijk_us  += (t2 - t1) * 1e6;

        astar_explored += (long long)pa.nodes_explored;
        dijk_explored  += (long long)pd.nodes_explored;

        if (pa.found && pd.found) {
            found_count++;
            double diff = fabs(pa.total_time_s - pd.total_time_s);
            double denom = pd.total_time_s > 1e-9 ? pd.total_time_s : 1e-9;
            if (diff / denom > 1e-6) {
                fprintf(stderr,
                    "[bench] COST MISMATCH src=%d dst=%d astar=%.6f dijk=%.6f\n",
                    src, dst, pa.total_time_s, pd.total_time_s);
                path_result_free(&pa);
                path_result_free(&pd);
                path_result_free(&p_static);
                graph_destroy(g);
                return 1;
            }
            matched++;

            /* Static vs Traffic-optimal comparison */
            find_path(g, src, dst, SEARCH_ASTAR, COST_STATIC, &p_static);
            if (p_static.found) {
                double static_eval = path_time_s(g, &p_static, COST_TRAFFIC);
                if (static_eval >= 0.0) {
                    sum_static_eval_time += static_eval;
                    sum_traffic_opt_time += pa.total_time_s;
                    cmp_pairs_count++;
                }
            }
        }

        path_result_free(&pa);
        path_result_free(&pd);
        path_result_free(&p_static);
    }

    printf("[bench] Results (%d pairs):\n", pairs);
    printf("  Found paths:            %d / %d pairs\n", found_count, pairs);
    printf("  Cost matched:           %d / %d (A* == Dijkstra within 1e-6)\n", matched, found_count);
    printf("  A* explored:            %lld nodes total (avg %.1f/query)\n",
           astar_explored, (double)astar_explored / pairs);
    printf("  Dijk explored:          %lld nodes total (avg %.1f/query)\n",
           dijk_explored, (double)dijk_explored / pairs);
    if (dijk_explored > 0) {
        double ratio = (double)astar_explored / (double)dijk_explored;
        double savings = (1.0 - ratio) * 100.0;
        printf("  Nodes-explored ratio:   %.3f (A* / Dijkstra)\n", ratio);
        printf("  A* savings:             %.1f%% fewer nodes explored\n", savings);
    }
    printf("  A* avg runtime:         %.1f us/query\n", astar_us / pairs);
    printf("  Dijk avg runtime:       %.1f us/query\n", dijk_us  / pairs);

    if (cmp_pairs_count > 0) {
        double avg_static = sum_static_eval_time / cmp_pairs_count;
        double avg_traffic = sum_traffic_opt_time / cmp_pairs_count;
        double pct_imp = ((avg_static - avg_traffic) / (avg_static > 1e-6 ? avg_static : 1.0)) * 100.0;
        printf("\n[bench] Static vs Traffic Routing Comparison (%d valid pairs):\n", cmp_pairs_count);
        printf("  Static path under traffic avg time:    %.2f s\n", avg_static);
        printf("  Traffic-optimal path avg time:         %.2f s\n", avg_traffic);
        printf("  Traffic-aware routing improvement:     %.2f%%\n", pct_imp);
    }

    graph_destroy(g);
    return 0;
}

/* ---- command: info ---- */

static int cmd_info(int argc, char **argv) {
    int synthetic = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--synthetic") == 0) synthetic = 1;
    }

    Graph *g = NULL;

    if (synthetic) {
        printf("[info] Building 40x40 synthetic grid ...\n");
        Status st = graph_build_synthetic_grid(&g, 40, 40, 12.90, 77.54, 500.0, 42);
        if (st != STATUS_OK || !g) {
            fprintf(stderr, "info: graph_build_synthetic_grid failed\n");
            return 1;
        }
    } else {
        OsmConfig cfg;
        osm_config_default(&cfg);
        cfg.data_dir = g_data_dir;
        OsmBuildStats stats;
        printf("[info] Loading OSM graph from %s ...\n", g_data_dir);
        Status st = osm_load_or_fetch(&cfg, 0, 1, &g, &stats);
        if (st != STATUS_OK || !g) {
            fprintf(stderr, "info: OSM graph not available (%s). Use --synthetic.\n", status_to_string(st));
            return 1;
        }
        printf("[info] OSM build stats: ways_total=%zu ways_used=%zu json_nodes=%zu\n",
               stats.ways_total, stats.ways_used, stats.json_nodes);
        printf("[info] junction_nodes=%zu edges_created=%zu\n",
               stats.junction_nodes, stats.edges_created);
        printf("[info] stops found=%zu snapped=%zu discarded=%zu\n",
               stats.stops_found, stats.stops_snapped, stats.stops_discarded);
        printf("[info] LCC dropped=%zu\n", stats.nodes_dropped_by_component);
    }

    printf("[info] Graph: %zu nodes, %zu edges\n",
           graph_node_count(g), graph_edge_count(g));
    printf("[info] Max speed: %.2f m/s\n", graph_max_speed_mps(g));

    size_t stop_count = 0;
    for (size_t i = 0; i < graph_node_count(g); i++) {
        const GraphNode *n = graph_node(g, (int)i);
        if (n && n->is_stop) stop_count++;
    }
    printf("[info] Bus stops snapped: %zu\n", stop_count);

    graph_destroy(g);
    return 0;
}

/* ---- command: route ---- */

static int cmd_route(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "route: requires <src> <dst>\n");
        return 1;
    }

    int src_idx = atoi(argv[0]);
    int dst_idx = atoi(argv[1]);
    int synthetic = 0;
    double hour = 9.0;
    int rain = 0;
    SearchAlgo algo = SEARCH_ASTAR;
    CostMode  mode  = COST_TRAFFIC;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--synthetic") == 0) synthetic = 1;
        else if (strcmp(argv[i], "--rain")   == 0) rain = 1;
        else if (strcmp(argv[i], "--static") == 0) mode = COST_STATIC;
        else if (strcmp(argv[i], "--hour")   == 0 && i+1 < argc) hour = atof(argv[++i]);
        else if (strcmp(argv[i], "--algo")   == 0 && i+1 < argc) {
            i++;
            if (strcmp(argv[i], "dijkstra") == 0) algo = SEARCH_DIJKSTRA;
            else algo = SEARCH_ASTAR;
        }
    }

    Graph *g = NULL;

    if (synthetic) {
        Status st = graph_build_synthetic_grid(&g, 40, 40, 12.90, 77.54, 500.0, 42);
        if (st != STATUS_OK || !g) {
            fprintf(stderr, "route: graph_build_synthetic_grid failed\n");
            return 1;
        }
    } else {
        OsmConfig cfg;
        osm_config_default(&cfg);
        cfg.data_dir = g_data_dir;
        OsmBuildStats stats;
        Status st = osm_load_or_fetch(&cfg, 0, 1, &g, &stats);
        if (st != STATUS_OK || !g) {
            fprintf(stderr, "route: OSM graph not available. Use --synthetic.\n");
            return 1;
        }
    }

    if (mode == COST_TRAFFIC) {
        traffic_sim_apply(g, hour * 60.0, rain);
    }

    size_t N = graph_node_count(g);
    if (src_idx < 0 || (size_t)src_idx >= N || dst_idx < 0 || (size_t)dst_idx >= N) {
        fprintf(stderr, "route: node indices out of range [0, %zu)\n", N);
        graph_destroy(g);
        return 1;
    }

    PathResult p;
    path_result_init(&p);
    const char *algo_name = (algo == SEARCH_ASTAR) ? "A*" : "Dijkstra";
    const char *mode_name = (mode == COST_TRAFFIC) ? "traffic" : "static";

    double t0 = now_monotonic_s();
    find_path(g, src_idx, dst_idx, algo, mode, &p);
    double elapsed_ms = (now_monotonic_s() - t0) * 1000.0;

    printf("[route] %s -> %s: %s (%s), hour=%.0f, rain=%d\n",
           graph_node(g, src_idx) ? graph_node(g, src_idx)->name : "?",
           graph_node(g, dst_idx) ? graph_node(g, dst_idx)->name : "?",
           algo_name, mode_name, hour, rain);

    if (p.found) {
        printf("  Total time:    %.2f s\n", p.total_time_s);
        printf("  Total length:  %.0f m\n", p.total_length_m);
        printf("  Node count:    %zu\n", p.node_count);
        printf("  Edge count:    %zu\n", p.edge_count);
        printf("  Explored:      %zu nodes\n", p.nodes_explored);
        printf("  Generated:     %zu nodes\n", p.nodes_generated);
        printf("  Query time:    %.3f ms\n", elapsed_ms);
        if (p.node_count <= 20) print_path_nodes(g, &p);
    } else {
        printf("  NOT REACHABLE\n");
    }

    path_result_free(&p);
    graph_destroy(g);
    return 0;
}

/* ---- command: osm ---- */

static int cmd_osm(int argc, char **argv) {
    int refresh = 0, offline = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--refresh") == 0) refresh = 1;
        if (strcmp(argv[i], "--offline") == 0) offline = 1;
    }

    OsmConfig cfg;
    osm_config_default(&cfg);
    cfg.data_dir = g_data_dir;
    OsmBuildStats stats;

    printf("[osm] Loading graph (dir=%s, refresh=%d, offline=%d) ...\n", g_data_dir, refresh, offline);
    double t0 = now_monotonic_s();
    Graph *g = NULL;
    Status st = osm_load_or_fetch(&cfg, refresh, offline, &g, &stats);
    double elapsed = now_monotonic_s() - t0;

    if (st != STATUS_OK) {
        fprintf(stderr, "[osm] Failed: %s\n", status_to_string(st));
        return 1;
    }

    printf("[osm] Done in %.2f s\n", elapsed);
    printf("[osm] Stats: ways_total=%zu ways_used=%zu json_nodes=%zu\n",
           stats.ways_total, stats.ways_used, stats.json_nodes);
    printf("[osm] junction_nodes=%zu edges_created=%zu\n",
           stats.junction_nodes, stats.edges_created);
    printf("[osm] stops found=%zu snapped=%zu discarded=%zu\n",
           stats.stops_found, stats.stops_snapped, stats.stops_discarded);
    printf("[osm] LCC dropped=%zu\n", stats.nodes_dropped_by_component);
    printf("[osm] Final: %zu nodes, %zu edges\n",
           graph_node_count(g), graph_edge_count(g));

    graph_destroy(g);
    return 0;
}

/* ---- command: traffic ---- */

static int cmd_traffic(int argc, char **argv) {
    int offline = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--offline") == 0) offline = 1;
    }

    const char *env_key = getenv("TOMTOM_API_KEY");
    int has_key = (env_key && env_key[0] != '\0');
    printf("[traffic] API key: %s\n", has_key ? "found" : "not found (simulated mode)");

    /* Use OSM graph if available, else synthetic */
    Graph *g = NULL;
    OsmConfig ocfg;
    osm_config_default(&ocfg);
    ocfg.data_dir = g_data_dir;
    OsmBuildStats ost;
    Status st = osm_load_or_fetch(&ocfg, 0, 1, &g, &ost);
    if (st != STATUS_OK || !g) {
        printf("[traffic] OSM graph not found in %s, using synthetic 10x10 grid\n", g_data_dir);
        st = graph_build_synthetic_grid(&g, 10, 10, 12.90, 77.54, 500.0, 42);
        if (st != STATUS_OK || !g) {
            fprintf(stderr, "[traffic] Failed to build test graph\n");
            return 1;
        }
    } else {
        printf("[traffic] Using loaded OSM graph: %zu nodes, %zu edges\n", graph_node_count(g), graph_edge_count(g));
    }

    /* Load corridors */
    char corr_path[512];
    snprintf(corr_path, sizeof(corr_path), "%s/corridors.txt", g_data_dir);
    TrafficCorridor cors[32];
    size_t ncors = 0;
    Status cst = tu_load_corridors(corr_path, cors, 32, &ncors);
    if (cst != STATUS_OK || ncors == 0) {
        printf("[traffic] No corridors loaded from %s, using empty corridor list\n", corr_path);
        ncors = 0;
    } else {
        printf("[traffic] Loaded %zu corridors from %s\n", ncors, corr_path);
    }

    TrafficConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.offline       = offline ? 1 : (has_key ? 0 : 1);
    cfg.max_calls     = 8;
    cfg.cache_seconds = 300;

    TrafficUpdater *tu = tu_create(g, cors, ncors, &cfg);
    if (!tu) {
        fprintf(stderr, "[traffic] tu_create failed\n");
        graph_destroy(g);
        return 1;
    }

    /* Record simulated factors before live update */
    size_t ne = graph_edge_count(g);
    double *base_sim_factors = (double *)malloc(ne * sizeof(double));
    if (!base_sim_factors) {
        tu_destroy(tu);
        graph_destroy(g);
        return 1;
    }

    double now_s = now_monotonic_s();
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long wall_s = ts.tv_sec % 86400;
    double sim_min = (double)(wall_s / 60);

    /* Apply sim once to record base factors */
    traffic_sim_apply(g, sim_min, 0);
    for (size_t i = 0; i < ne; i++) {
        const Edge *e = graph_edge(g, (int)i);
        base_sim_factors[i] = e ? e->traffic_factor : 1.0;
    }

    st = tu_update(tu, now_s, sim_min, 0);
    if (st != STATUS_OK) {
        fprintf(stderr, "[traffic] tu_update failed: %s\n", status_to_string(st));
        free(base_sim_factors);
        tu_destroy(tu);
        graph_destroy(g);
        return 1;
    }

    TrafficSource src_enum = tu_source(tu);
    printf("[traffic] Source: %s\n", (src_enum == TRAFFIC_LIVE) ? "LIVE" : "SIMULATED");
    printf("[traffic] Call count: %d\n", tu_call_count(tu));
    printf("[traffic] Live corridors: %zu\n", tu_live_corridor_count(tu));
    const char *err = tu_last_error(tu);
    if (err && err[0]) printf("[traffic] Last error: %s\n", err);

    /* Track edges overwritten by live data */
    size_t live_edges = 0;
    double live_min = 1e9, live_max = 0.0, live_sum = 0.0;
    double all_min = 1e9, all_max = 0.0, all_sum = 0.0;

    for (size_t i = 0; i < ne; i++) {
        const Edge *e = graph_edge(g, (int)i);
        if (!e) continue;
        double f = e->traffic_factor;

        if (f < all_min) all_min = f;
        if (f > all_max) all_max = f;
        all_sum += f;

        if (fabs(f - base_sim_factors[i]) > 1e-4) {
            live_edges++;
            if (f < live_min) live_min = f;
            if (f > live_max) live_max = f;
            live_sum += f;
        }
    }
    free(base_sim_factors);

    printf("[traffic] Edges overwritten by live data: %zu of %zu\n", live_edges, ne);
    if (live_edges > 0) {
        printf("[traffic] Overwritten edges factor: min=%.2f avg=%.2f max=%.2f\n",
               live_min, live_sum / (double)live_edges, live_max);
    } else {
        printf("[traffic] Overwritten edges factor: N/A (no edges overwritten)\n");
    }

    if (ne > 0) {
        printf("[traffic] All edges traffic factors: min=%.2f avg=%.2f max=%.2f\n",
               all_min, all_sum / (double)ne, all_max);
    }

    tu_destroy(tu);
    graph_destroy(g);
    return 0;
}

/* ---- Part 3A CLI Commands ---- */

static int cmd_stops_info(int argc, char **argv) {
    const char *cache_path = NULL;
    size_t max_stops = 60;
    uint64_t seed = 42;
    int stride_only = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
            cache_path = argv[++i];
        } else if ((strcmp(argv[i], "--stops") == 0 || strcmp(argv[i], "--max-stops") == 0) && i + 1 < argc) {
            max_stops = (size_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--stride-only") == 0) {
            stride_only = 1;
        }
    }

    char resolved_cache[512];
    if (!cache_path) {
        snprintf(resolved_cache, sizeof(resolved_cache), "%s/graph_cache.txt", g_data_dir);
        cache_path = resolved_cache;
    }

    Graph *g = NULL;
    Status st = graph_load(&g, cache_path);
    if (st != STATUS_OK || !g) {
        fprintf(stderr, "Error: failed to load graph cache from %s\n", cache_path);
        return 1;
    }

    size_t total_nodes = graph_node_count(g);
    size_t total_edges = graph_edge_count(g);

    int *scc_map = (int *)malloc(total_nodes * sizeof(int));
    if (!scc_map) {
        graph_destroy(g);
        return 1;
    }
    size_t n_scc = 0;
    st = graph_scc_label(g, scc_map, &n_scc);
    if (st != STATUS_OK) {
        fprintf(stderr, "Error: graph_scc_label failed\n");
        free(scc_map);
        graph_destroy(g);
        return 1;
    }

    int *largest_scc = NULL;
    size_t n_largest = 0;
    st = graph_largest_scc(g, &largest_scc, &n_largest);
    if (st != STATUS_OK) {
        fprintf(stderr, "Error: graph_largest_scc failed\n");
        free(scc_map);
        graph_destroy(g);
        return 1;
    }

    size_t stops_total = 0;
    for (size_t i = 0; i < total_nodes; i++) {
        const GraphNode *gn = graph_node(g, (int)i);
        if (gn && gn->is_stop) stops_total++;
    }

    size_t stops_in_scc = 0;
    for (size_t i = 0; i < n_largest; i++) {
        const GraphNode *gn = graph_node(g, largest_scc[i]);
        if (gn && gn->is_stop) stops_in_scc++;
    }

    DemandStop *stops = NULL;
    size_t n_stops = 0;

    if (!stride_only) {
        printf("\nAnchors Breakdown:\n");
        char *taken = (char *)calloc(n_largest, sizeof(char));
        typedef struct { int node; double dist; } CDist;
        CDist *cbuf = (CDist *)malloc(n_largest * sizeof(CDist));

        for (size_t c = 0; c < DEMAND_NUM_ANCHOR_CENTRES; c++) {
            const AnchorCentre *ac = &g_anchor_centres[c];
            printf("Centre: %s (%s, radius %.0f m, limit %zu)\n",
                   ac->name, (ac->type == STOP_HUB ? "Hub" : "Office"), ac->radius_m, ac->max_anchors);
            size_t n_match = 0;
            for (size_t i = 0; i < n_largest; i++) {
                if (taken[i]) continue;
                int u = largest_scc[i];
                const GraphNode *gn = graph_node(g, u);
                if (gn && gn->is_stop) {
                    double d = graph_haversine_m(gn->lat, gn->lng, ac->lat, ac->lng);
                    if (d <= ac->radius_m) {
                        cbuf[n_match].node = u;
                        cbuf[n_match].dist = d;
                        n_match++;
                    }
                }
            }

            if (n_match == 0) {
                printf("NO STOP WITHIN RADIUS\n");
            } else {
                for (size_t i = 0; i < n_match; i++) {
                    for (size_t j = i + 1; j < n_match; j++) {
                        if (cbuf[j].dist < cbuf[i].dist ||
                            (cbuf[j].dist == cbuf[i].dist && cbuf[j].node < cbuf[i].node)) {
                            CDist tmp = cbuf[i]; cbuf[i] = cbuf[j]; cbuf[j] = tmp;
                        }
                    }
                }
                size_t take = (n_match < ac->max_anchors) ? n_match : ac->max_anchors;
                for (size_t j = 0; j < take; j++) {
                    int u = cbuf[j].node;
                    const GraphNode *gn = graph_node(g, u);
                    const char *sname = (gn && gn->name[0]) ? gn->name : "<unnamed>";
                    printf("  Anchor: centre \"%s\", stop \"%s\", distance %.1f m\n",
                           ac->name, sname, cbuf[j].dist);
                    for (size_t i = 0; i < n_largest; i++) {
                        if (largest_scc[i] == u) {
                            taken[i] = 1;
                            break;
                        }
                    }
                }
            }
        }
        free(taken);
        free(cbuf);
        printf("\n");
        st = demand_stops_from_graph_anchored(g, max_stops, seed, &stops, &n_stops);
    } else {
        st = demand_stops_from_graph(g, max_stops, seed, &stops, &n_stops);
    }
    if (st != STATUS_OK) {
        fprintf(stderr, "Error: demand_stops_from_graph%s failed\n", stride_only ? "" : "_anchored");
        free(largest_scc);
        free(scc_map);
        graph_destroy(g);
        return 1;
    }

    size_t n_hub = 0, n_office = 0, n_market = 0, n_res = 0;
    double min_scale = 1e9, max_scale = -1e9, sum_scale = 0.0;
    for (size_t i = 0; i < n_stops; i++) {
        if (stops[i].type == STOP_HUB) n_hub++;
        else if (stops[i].type == STOP_OFFICE) n_office++;
        else if (stops[i].type == STOP_MARKET) n_market++;
        else if (stops[i].type == STOP_RESIDENTIAL) n_res++;

        if (stops[i].scale < min_scale) min_scale = stops[i].scale;
        if (stops[i].scale > max_scale) max_scale = stops[i].scale;
        sum_scale += stops[i].scale;
    }
    double mean_scale = (n_stops > 0) ? (sum_scale / (double)n_stops) : 0.0;

    printf("=== Bangalore Graph & Bus Stops Summary ===\n");
    printf("Graph nodes:            %zu\n", total_nodes);
    printf("Graph edges:            %zu\n", total_edges);
    printf("Strongly Connected Components (SCCs): %zu\n", n_scc);
    printf("Largest SCC size:       %zu nodes (%.2f%% of graph)\n",
           n_largest, total_nodes > 0 ? (100.0 * (double)n_largest / (double)total_nodes) : 0.0);
    printf("Total stops in graph:   %zu\n", stops_total);
    printf("Stops in largest SCC:   %zu (%.2f%% of stops)\n",
           stops_in_scc, stops_total > 0 ? (100.0 * (double)stops_in_scc / (double)stops_total) : 0.0);
    printf("Selected stops:         %zu (limit: %zu)\n", n_stops, max_stops);
    printf("\nSelected Stops Breakdown by Category:\n");
    printf("  Hub (Majestic/Shivajinagar): %zu (%.1f%%)\n", n_hub, n_stops > 0 ? (100.0 * (double)n_hub / (double)n_stops) : 0.0);
    printf("  Office (CBD/Tech corridors): %zu (%.1f%%)\n", n_office, n_stops > 0 ? (100.0 * (double)n_office / (double)n_stops) : 0.0);
    printf("  Market (Commercial zones):   %zu (%.1f%%)\n", n_market, n_stops > 0 ? (100.0 * (double)n_market / (double)n_stops) : 0.0);
    printf("  Residential (Neighborhoods): %zu (%.1f%%)\n", n_res, n_stops > 0 ? (100.0 * (double)n_res / (double)n_stops) : 0.0);
    printf("Scale range:            min %.4f, max %.4f, mean %.4f\n",
           (n_stops > 0 ? min_scale : 0.0), (n_stops > 0 ? max_scale : 0.0), mean_scale);

    free(stops);
    free(largest_scc);
    free(scc_map);
    graph_destroy(g);
    return 0;
}

static int cmd_gen_demand(int argc, char **argv) {
    const char *cache_path = NULL;
    const char *out_path = NULL;
    size_t max_stops = 60;
    int days = 120;
    uint64_t seed = 42;
    int synthetic_stops = 0;
    int stride_only = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
            cache_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if ((strcmp(argv[i], "--stops") == 0 || strcmp(argv[i], "--max-stops") == 0) && i + 1 < argc) {
            max_stops = (size_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--days") == 0 && i + 1 < argc) {
            days = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--synthetic-stops") == 0) {
            synthetic_stops = 1;
        } else if (strcmp(argv[i], "--stride-only") == 0) {
            stride_only = 1;
        }
    }

    char resolved_cache[512];
    char resolved_out[512];
    if (!cache_path) {
        snprintf(resolved_cache, sizeof(resolved_cache), "%s/graph_cache.txt", g_data_dir);
        cache_path = resolved_cache;
    }
    if (!out_path) {
        snprintf(resolved_out, sizeof(resolved_out), "%s/demand_history.csv", g_data_dir);
        out_path = resolved_out;
    }

    DemandStop *stops = NULL;
    size_t n_stops = 0;

    if (synthetic_stops) {
        n_stops = max_stops;
        stops = (DemandStop *)calloc(n_stops, sizeof(DemandStop));
        if (!stops) {
            fprintf(stderr, "Error: out of memory for synthetic stops\n");
            return 1;
        }
        for (size_t i = 0; i < n_stops; i++) {
            stops[i].node = (int)i;
            stops[i].osm_id = (int64_t)(100000 + i);
            stops[i].type = (StopType)(i % 4);
            stops[i].scale = demand_stop_scale(seed, stops[i].osm_id);
            snprintf(stops[i].name, sizeof(stops[i].name), "SynthStop_%zu", i);
        }
    } else {
        Graph *g = NULL;
        Status st = graph_load(&g, cache_path);
        if (st != STATUS_OK || !g) {
            fprintf(stderr, "Error: failed to load graph cache from %s\n", cache_path);
            return 1;
        }
        if (stride_only) {
            st = demand_stops_from_graph(g, max_stops, seed, &stops, &n_stops);
        } else {
            st = demand_stops_from_graph_anchored(g, max_stops, seed, &stops, &n_stops);
        }
        graph_destroy(g);
        if (st != STATUS_OK || !stops || n_stops == 0) {
            fprintf(stderr, "Error: failed to extract stops from graph\n");
            free(stops);
            return 1;
        }
    }

    DemandRow *rows = NULL;
    size_t n_rows = 0;
    Status st = demand_generate(stops, n_stops, seed, days, &rows, &n_rows);
    if (st != STATUS_OK) {
        fprintf(stderr, "Error: demand_generate failed\n");
        free(stops);
        return 1;
    }

    st = demand_rows_write_csv(rows, n_rows, out_path);
    if (st != STATUS_OK) {
        fprintf(stderr, "Error: demand_rows_write_csv failed writing to %s\n", out_path);
        free(rows);
        free(stops);
        return 1;
    }

    /* Compute category breakdown */
    size_t cat_cnt[DEMAND_NUM_CATEGORIES] = {0};
    double cat_pass[DEMAND_NUM_CATEGORIES] = {0.0};
    double total_pass = 0.0;

    for (size_t i = 0; i < n_rows; i++) {
        DemandCategory cat = demand_row_category(&rows[i]);
        cat_cnt[cat]++;
        cat_pass[cat] += rows[i].passengers;
        total_pass += rows[i].passengers;
    }

    printf("Generated %zu demand rows (%d days, %zu stops) -> %s\n", n_rows, days, n_stops, out_path);
    printf("Category Breakdown:\n");
    for (int c = 0; c < DEMAND_NUM_CATEGORIES; c++) {
        double mean_pax = (cat_cnt[c] > 0) ? (cat_pass[c] / (double)cat_cnt[c]) : 0.0;
        printf("  %-16s: %7zu rows, mean %6.2f passengers/hr\n",
               demand_category_name((DemandCategory)c), cat_cnt[c], mean_pax);
    }
    printf("Overall mean passengers: %.2f passengers/hr\n", (n_rows > 0) ? (total_pass / (double)n_rows) : 0.0);

    free(rows);
    free(stops);
    return 0;
}

static int cmd_demand_eval(int argc, char **argv) {
    const char *train_path = NULL;
    const char *test_path = NULL;
    const char *model_path = NULL;
    double expect_forest_mae = -1.0;
    double tol = 0.5;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--train") == 0 && i + 1 < argc) {
            train_path = argv[++i];
        } else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            test_path = argv[++i];
        } else if ((strcmp(argv[i], "--model") == 0 || strcmp(argv[i], "--model-rf") == 0) && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--expect-forest-mae") == 0 && i + 1 < argc) {
            expect_forest_mae = atof(argv[++i]);
        } else if (strcmp(argv[i], "--tol") == 0 && i + 1 < argc) {
            tol = atof(argv[++i]);
        }
    }

    char resolved_train[512];
    char resolved_test[512];
    if (!train_path) {
        snprintf(resolved_train, sizeof(resolved_train), "%s/demand_train.csv", g_data_dir);
        train_path = resolved_train;
    }
    if (!test_path) {
        snprintf(resolved_test, sizeof(resolved_test), "%s/demand_test.csv", g_data_dir);
        test_path = resolved_test;
    }

    DemandRow *train_rows = NULL;
    size_t n_train = 0;
    Status st = demand_rows_load_csv(train_path, &train_rows, &n_train);
    if (st != STATUS_OK || !train_rows) {
        fprintf(stderr, "Error: failed to load train rows from %s (status %d)\n", train_path, (int)st);
        return 1;
    }

    DemandRow *test_rows = NULL;
    size_t n_test = 0;
    st = demand_rows_load_csv(test_path, &test_rows, &n_test);
    if (st != STATUS_OK || !test_rows) {
        fprintf(stderr, "Error: failed to load test rows from %s (status %d)\n", test_path, (int)st);
        free(train_rows);
        return 1;
    }

    DemandPredictor *p_ma = dp_train_moving_avg(train_rows, n_train);
    DemandPredictor *p_ols = dp_train_ols_band(train_rows, n_train);
    DemandPredictor *p_rf = NULL;

    if (model_path) {
        ForestModel *fm = NULL;
        st = forest_load(&fm, model_path);
        if (st == STATUS_OK && fm) {
            p_rf = dp_from_forest(fm);
        } else {
            fprintf(stderr, "Warning: failed to load model from %s\n", model_path);
        }
    }

    DemandPredictor *models[3];
    int n_models = 0;
    if (p_ma) models[n_models++] = p_ma;
    if (p_ols) models[n_models++] = p_ols;
    if (p_rf) models[n_models++] = p_rf;

    size_t cat_counts[DEMAND_NUM_CATEGORIES] = {0};
    for (size_t i = 0; i < n_test; i++) {
        DemandCategory cat = demand_row_category(&test_rows[i]);
        if ((int)cat >= 0 && (int)cat < DEMAND_NUM_CATEGORIES) {
            cat_counts[cat]++;
        }
    }

    printf("\n=== Demand Predictor Evaluation (Test Set: %zu rows) ===\n", n_test);
    for (int c = 0; c < DEMAND_NUM_CATEGORIES; c++) {
        if (cat_counts[c] == 0) {
            printf("Notice: Category '%s' has 0 rows (empty).\n", demand_category_name((DemandCategory)c));
        }
    }
    printf("%-18s | %8s %8s %8s %11s | %8s %8s %8s %8s %8s\n",
           "Model", "MAE", "RMSE", "WAPE", "MAE(lambda)", "Wkday", "Wkend", "Hol", "Rain", "Event");
    printf("-------------------+---------------------------------------------+---------------------------------------------\n");
    printf("%-18s | %8s %8s %8s %11s | %8zu %8zu %8zu %8zu %8zu\n",
           "row_counts", "-", "-", "-", "-",
           cat_counts[CAT_WEEKDAY],
           cat_counts[CAT_WEEKEND],
           cat_counts[CAT_HOLIDAY],
           cat_counts[CAT_RAIN],
           cat_counts[CAT_EVENT]);
    printf("-------------------+---------------------------------------------+---------------------------------------------\n");

    double rf_actual_mae = -1.0;

    for (int m = 0; m < n_models; m++) {
        DemandPredictor *p = models[m];
        DemandEval overall;
        DemandEval by_cat[DEMAND_NUM_CATEGORIES];
        memset(&overall, 0, sizeof(overall));
        memset(by_cat, 0, sizeof(by_cat));

        dp_evaluate(p, test_rows, n_test, &overall, by_cat);

        if (dp_kind(p) == DEMAND_MODEL_FOREST) {
            rf_actual_mae = overall.mae_noisy;
        }

        printf("%-18s | %8.3f %8.3f %7.2f%% %11.3f | %8.3f %8.3f %8.3f %8.3f %8.3f\n",
               dp_name(p),
               overall.mae_noisy,
               overall.rmse_noisy,
               overall.wape_noisy * 100.0,
               overall.mae_lambda,
               by_cat[CAT_WEEKDAY].mae_noisy,
               by_cat[CAT_WEEKEND].mae_noisy,
               by_cat[CAT_HOLIDAY].mae_noisy,
               by_cat[CAT_RAIN].mae_noisy,
               by_cat[CAT_EVENT].mae_noisy);
    }
    printf("-------------------+---------------------------------------------+---------------------------------------------\n\n");
    if (rf_actual_mae >= 0.0) {
        printf("Forest MAE (noisy): %.6f\n", rf_actual_mae);
    }

    int exit_code = 0;
    if (expect_forest_mae >= 0.0) {
        if (rf_actual_mae < 0.0) {
            fprintf(stderr, "Error: --expect-forest-mae specified but forest was not evaluated\n");
            exit_code = 1;
        } else {
            double diff = fabs(rf_actual_mae - expect_forest_mae);
            printf("Checking expected Forest MAE: expected %.4f, actual %.4f, diff %.4f (tol %.4f) -> %s\n",
                   expect_forest_mae, rf_actual_mae, diff, tol, (diff <= tol) ? "PASS" : "FAIL");
            if (diff > tol) exit_code = 1;
        }
    }

    if (p_ma) dp_destroy(p_ma);
    if (p_ols) dp_destroy(p_ols);
    if (p_rf) dp_destroy(p_rf);
    free(train_rows);
    free(test_rows);
    return exit_code;
}

static int cmd_golden_check(int argc, char **argv) {
    const char *model_path = NULL;
    const char *golden_path = NULL;
    double tol = 1e-6;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--golden") == 0 && i + 1 < argc) {
            golden_path = argv[++i];
        } else if (strcmp(argv[i], "--tol") == 0 && i + 1 < argc) {
            tol = atof(argv[++i]);
        }
    }

    char resolved_model[512];
    char resolved_golden[512];
    if (!model_path) {
        snprintf(resolved_model, sizeof(resolved_model), "%s/demand_rf.txt", g_data_dir);
        model_path = resolved_model;
    }
    if (!golden_path) {
        snprintf(resolved_golden, sizeof(resolved_golden), "%s/golden_predictions.csv", g_data_dir);
        golden_path = resolved_golden;
    }

    ForestModel *fm = NULL;
    Status st = forest_load(&fm, model_path);
    if (st != STATUS_OK || !fm) {
        fprintf(stderr, "Error: failed to load forest model from %s\n", model_path);
        return 1;
    }

    FILE *f = fopen(golden_path, "rb");
    if (!f) {
        fprintf(stderr, "Error: failed to open golden predictions CSV %s\n", golden_path);
        forest_destroy(fm);
        return 1;
    }

    char line[514];
    if (!fgets(line, sizeof(line), f)) {
        fprintf(stderr, "Error: empty golden file %s\n", golden_path);
        fclose(f);
        forest_destroy(fm);
        return 1;
    }

    int format_mode = 0; /* 0 = feature columns + expected, 1 = stop_node,stop_type,... */
    if (strstr(line, "stop_node") != NULL) {
        format_mode = 1;
    }

    size_t rows_checked = 0;
    double max_diff = 0.0;
    double sum_diff = 0.0;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        double feats[DEMAND_NUM_FEATURES];
        double expected = 0.0;

        if (format_mode == 0) {
            /* Features then expected */
            char *cursor = line;
            for (int i = 0; i < DEMAND_NUM_FEATURES; i++) {
                char *p = strchr(cursor, ',');
                if (!p) break;
                *p = '\0';
                feats[i] = atof(cursor);
                cursor = p + 1;
            }
            expected = atof(cursor);
        } else {
            /* stop_node,stop_type,scale,hour,day_type,rain,event,passengers,expected,category */
            char *p = line;
            char *tokens[11];
            int n_tokens = 0;
            while (p && n_tokens < 11) {
                tokens[n_tokens++] = p;
                char *comma = strchr(p, ',');
                if (!comma) break;
                *comma = '\0';
                p = comma + 1;
            }
            if (n_tokens < 9) continue;

            StopType st_type = (StopType)atoi(tokens[1]);
            double scale = atof(tokens[2]);
            int hour = atoi(tokens[3]);
            DayType dt = (DayType)atoi(tokens[4]);
            int rain = atoi(tokens[5]);
            int event = atoi(tokens[6]);
            expected = atof(tokens[8]);

            demand_features(st_type, scale, hour, dt, rain, event, feats);
        }

        double pred = forest_predict(fm, feats);
        double diff = fabs(pred - expected);
        if (diff > max_diff) {
            max_diff = diff;
        }
        sum_diff += diff;
        rows_checked++;
    }

    fclose(f);
    forest_destroy(fm);

    double golden_mae = (rows_checked > 0) ? (sum_diff / (double)rows_checked) : 0.0;
    int pass = (rows_checked > 0 && max_diff <= tol);
    printf("=== Golden Predictions Check ===\n");
    printf("rows checked: %zu\n", rows_checked);
    printf("max diff:     %.6e\n", max_diff);
    printf("golden MAE:   %.6f\n", golden_mae);
    printf("tolerance:    %.6e\n", tol);
    printf("status:       %s\n", pass ? "PASS" : "FAIL");

    return pass ? 0 : 1;
}

static int cmd_network_info(int argc, char **argv) {
    const char *cache_path = NULL;
    size_t stops_requested = 60;
    size_t n_routes = 6;
    uint64_t seed = 42;
    int build_matrix = 1;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
            cache_path = argv[++i];
        } else if ((strcmp(argv[i], "--stops") == 0 || strcmp(argv[i], "--max-stops") == 0) && i + 1 < argc) {
            stops_requested = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--routes") == 0 && i + 1 < argc) {
            n_routes = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--no-matrix") == 0) {
            build_matrix = 0;
        }
    }

    char resolved_cache[512];
    if (!cache_path) {
        snprintf(resolved_cache, sizeof(resolved_cache), "%s/graph_cache.txt", g_data_dir);
        cache_path = resolved_cache;
    }

    Graph *g = NULL;
    Status st = graph_load(&g, cache_path);
    if (st != STATUS_OK || !g) {
        fprintf(stderr, "Error: failed to load graph cache from %s\n", cache_path);
        return 1;
    }

    DemandStop *stops = NULL;
    size_t n_stops = 0;
    st = demand_stops_from_graph_anchored(g, stops_requested, seed, &stops, &n_stops);
    if (st != STATUS_OK || !stops) {
        fprintf(stderr, "Error: failed to select anchored stops (status %d)\n", (int)st);
        graph_destroy(g);
        return 1;
    }

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = n_routes;
    cfg.build_matrix = build_matrix;

    NetworkModel *net = NULL;
    st = network_build(g, stops, n_stops, &cfg, &net);
    if (st != STATUS_OK || !net) {
        fprintf(stderr, "Error: failed to build network (status %d)\n", (int)st);
        free(stops);
        graph_destroy(g);
        return 1;
    }

    printf("\n=== Bus Network Information ===\n");
    printf("Total Routes:     %zu\n", network_route_count(net));
    printf("Total Stops:      %zu (requested %zu)\n", n_stops, stops_requested);
    printf("Dwell Time:       %.1f s\n", cfg.dwell_s_per_stop);
    printf("Matrix Built:     %s\n\n", build_matrix ? "YES" : "NO");

    printf("%-8s | %-32s | %-6s | %-12s | %-12s | %s\n",
           "Route", "Name", "Stops", "Loop (min)", "Cycle (min)", "Terminals");
    printf("---------+----------------------------------+--------+--------------+--------------+----------------------------------\n");

    for (size_t r = 0; r < network_route_count(net); r++) {
        double loop_s = network_route_loop_time_s(net, r, COST_STATIC);
        double cycle_s = network_route_cycle_s(net, r);
        const char *name = network_route_name(net, r);
        size_t scnt = network_route_stop_count(net, r);

        char terminals[160] = "-";
        if (scnt > 0) {
            int first_stop_idx = network_route_stop(net, r, 0);
            int last_stop_idx = network_route_stop(net, r, scnt - 1);
            const DemandStop *ds1 = (first_stop_idx >= 0) ? network_stop(net, (size_t)first_stop_idx) : NULL;
            const DemandStop *ds2 = (last_stop_idx >= 0) ? network_stop(net, (size_t)last_stop_idx) : NULL;
            const char *n1 = (ds1 && ds1->name[0]) ? ds1->name : "Stop";
            const char *n2 = (ds2 && ds2->name[0]) ? ds2->name : "Stop";
            snprintf(terminals, sizeof(terminals), "%.70s -> %.70s", n1, n2);
        }

        printf("R%-7zu | %-32.32s | %-6zu | %-12.2f | %-12.2f | %s\n",
               r + 1, name ? name : "Route", scnt, loop_s / 60.0, cycle_s / 60.0, terminals);
    }

    if (build_matrix) {
        double min_t = 1e30, max_t = 0.0, sum_t = 0.0;
        size_t valid_pairs = 0;
        for (size_t i = 0; i < n_stops; i++) {
            for (size_t j = 0; j < n_stops; j++) {
                if (i == j) continue;
                double t = network_travel_time_s(net, i, j);
                if (t >= 0.0) {
                    if (t < min_t) min_t = t;
                    if (t > max_t) max_t = t;
                    sum_t += t;
                    valid_pairs++;
                }
            }
        }
        printf("\nTravel Time Matrix Statistics (%zu stops, %zu pairs):\n", n_stops, n_stops * n_stops);
        if (valid_pairs > 0) {
            printf("  Valid Directed Pairs: %zu (%.1f%%)\n", valid_pairs, 100.0 * (double)valid_pairs / (double)(n_stops * (n_stops - 1)));
            printf("  Min Travel Time:      %.1f s (%.2f min)\n", min_t, min_t / 60.0);
            printf("  Max Travel Time:      %.1f s (%.2f min)\n", max_t, max_t / 60.0);
            printf("  Mean Travel Time:     %.1f s (%.2f min)\n", sum_t / (double)valid_pairs, (sum_t / (double)valid_pairs) / 60.0);
        }
    }

    network_destroy(net);
    free(stops);
    graph_destroy(g);
    return 0;
}

static int cmd_alloc_demo(int argc, char **argv) {
    const char *cache_path = NULL;
    const char *train_path = NULL;
    const char *model_path = NULL;
    size_t stops_requested = 60;
    size_t n_routes = 6;
    size_t n_buses = 36;
    int min_per_route = 1;
    int hour = 9;
    DayType day_type = DAY_WEEKDAY;
    int rain = 0;
    uint64_t seed = 42;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
            cache_path = argv[++i];
        } else if (strcmp(argv[i], "--train") == 0 && i + 1 < argc) {
            train_path = argv[++i];
        } else if ((strcmp(argv[i], "--model") == 0 || strcmp(argv[i], "--model-rf") == 0) && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--stops") == 0 && i + 1 < argc) {
            stops_requested = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--routes") == 0 && i + 1 < argc) {
            n_routes = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--buses") == 0 && i + 1 < argc) {
            n_buses = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--min-per-route") == 0 && i + 1 < argc) {
            min_per_route = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hour") == 0 && i + 1 < argc) {
            hour = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rain") == 0) {
            rain = 1;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--day") == 0 && i + 1 < argc) {
            const char *dstr = argv[++i];
            if (strcmp(dstr, "weekend") == 0) day_type = DAY_WEEKEND;
            else if (strcmp(dstr, "holiday") == 0) day_type = DAY_HOLIDAY;
            else day_type = DAY_WEEKDAY;
        }
    }

    char resolved_cache[512];
    if (!cache_path) {
        snprintf(resolved_cache, sizeof(resolved_cache), "%s/graph_cache.txt", g_data_dir);
        cache_path = resolved_cache;
    }

    char resolved_model[512];
    if (!model_path) {
        snprintf(resolved_model, sizeof(resolved_model), "%s/demand_rf.txt", g_data_dir);
        struct stat st_m;
        if (stat(resolved_model, &st_m) == 0) {
            model_path = resolved_model;
        }
    }

    Graph *g = NULL;
    Status st = graph_load(&g, cache_path);
    if (st != STATUS_OK || !g) {
        fprintf(stderr, "Error: failed to load graph cache from %s\n", cache_path);
        return 1;
    }

    DemandStop *stops = NULL;
    size_t n_stops = 0;
    st = demand_stops_from_graph_anchored(g, stops_requested, seed, &stops, &n_stops);
    if (st != STATUS_OK || !stops) {
        fprintf(stderr, "Error: failed to extract anchored stops\n");
        graph_destroy(g);
        return 1;
    }

    DemandPredictor *dp = NULL;
    if (model_path) {
        ForestModel *fm = NULL;
        st = forest_load(&fm, model_path);
        if (st == STATUS_OK && fm) {
            dp = dp_from_forest(fm);
        }
    }
    if (!dp && train_path) {
        DemandRow *t_rows = NULL;
        size_t n_tr = 0;
        if (demand_rows_load_csv(train_path, &t_rows, &n_tr) == STATUS_OK && t_rows) {
            dp = dp_train_ols_band(t_rows, n_tr);
            free(t_rows);
        }
    }

    NetworkConfig cfg;
    network_config_default(&cfg);
    cfg.n_routes = n_routes;
    cfg.build_matrix = 1;

    NetworkModel *net = NULL;
    st = network_build(g, stops, n_stops, &cfg, &net);
    if (st != STATUS_OK || !net) {
        fprintf(stderr, "Error: failed to build network (status %d)\n", (int)st);
        if (dp) dp_destroy(dp);
        free(stops);
        graph_destroy(g);
        return 1;
    }

    double *demands = (double *)calloc(n_routes, sizeof(double));
    double *cycles = (double *)calloc(n_routes, sizeof(double));
    int *alloc_eq = (int *)calloc(n_routes, sizeof(int));
    int *alloc_prop = (int *)calloc(n_routes, sizeof(int));
    int *alloc_dpb = (int *)calloc(n_routes, sizeof(int));
    int *alloc_mw = (int *)calloc(n_routes, sizeof(int));

    network_route_demands(net, dp, hour, day_type, rain, NULL, 0, demands);
    for (size_t r = 0; r < n_routes; r++) {
        cycles[r] = network_route_cycle_s(net, r);
    }

    fleet_targets_equal(n_routes, n_buses, alloc_eq);
    fleet_targets_proportional(demands, n_routes, n_buses, min_per_route, alloc_prop);
    fleet_targets_greedy(demands, cycles, n_routes, n_buses, min_per_route, FLEET_PRIO_DEMAND_PER_BUS, alloc_dpb);
    fleet_targets_greedy(demands, cycles, n_routes, n_buses, min_per_route, FLEET_PRIO_MARGINAL_WAIT, alloc_mw);

    double wait_eq = fleet_wait_proxy_s(demands, cycles, alloc_eq, n_routes);
    double wait_prop = fleet_wait_proxy_s(demands, cycles, alloc_prop, n_routes);
    double wait_dpb = fleet_wait_proxy_s(demands, cycles, alloc_dpb, n_routes);
    double wait_mw = fleet_wait_proxy_s(demands, cycles, alloc_mw, n_routes);

    const char *day_names[] = {"Weekday", "Weekend", "Holiday"};
    printf("\n=== Fleet Allocation Demonstration ===\n");
    printf("Scenario:         Hour %02d:00, %s, Rain=%s (Total Buses: %zu, Min/Route: %d)\n",
           hour, day_names[(int)day_type], rain ? "YES" : "NO", n_buses, min_per_route);
    printf("Demand Predictor: %s\n\n", dp ? (model_path ? "Random Forest" : "OLS Band") : "Ground-Truth Oracle");

    printf("%-8s | %-12s | %-12s | %-8s | %-8s | %-11s | %-13s\n",
           "Route", "Demand (p/h)", "Cycle (min)", "Equal", "Prop", "Demand/Bus", "Marginal Wait");
    printf("---------+--------------+--------------+----------+----------+-------------+--------------\n");

    double total_dem = 0.0;
    for (size_t r = 0; r < n_routes; r++) {
        total_dem += demands[r];
        printf("R%-7zu | %-12.1f | %-12.2f | %-8d | %-8d | %-11d | %-13d\n",
               r + 1, demands[r], cycles[r] / 60.0,
               alloc_eq[r], alloc_prop[r], alloc_dpb[r], alloc_mw[r]);
    }
    printf("---------+--------------+--------------+----------+----------+-------------+--------------\n");
    printf("%-8s | %-12.1f | %-12s | %-8zu | %-8zu | %-11zu | %-13zu\n\n",
           "Total", total_dem, "-", n_buses, n_buses, n_buses, n_buses);

    printf("Waiting Time Proxy (Avg Passenger Wait):\n");
    printf("  Equal:         %7.1f s (%5.2f min)\n", wait_eq, wait_eq / 60.0);
    printf("  Proportional:  %7.1f s (%5.2f min) [vs Equal: %+5.1f%%]\n",
           wait_prop, wait_prop / 60.0, (wait_prop - wait_eq) / wait_eq * 100.0);
    printf("  Demand/Bus:    %7.1f s (%5.2f min) [vs Equal: %+5.1f%%]\n",
           wait_dpb, wait_dpb / 60.0, (wait_dpb - wait_eq) / wait_eq * 100.0);
    printf("  Marginal Wait: %7.1f s (%5.2f min) [vs Equal: %+5.1f%%, vs Demand/Bus: %+5.1f%%]\n\n",
           wait_mw, wait_mw / 60.0, (wait_mw - wait_eq) / wait_eq * 100.0,
           (wait_mw - wait_dpb) / wait_dpb * 100.0);

    /* Move Planning Demonstration */
    printf("=== Dynamic Fleet Move Plan (Transition from Equal -> Marginal Wait) ===\n");
    FleetBus *buses = (FleetBus *)calloc(n_buses, sizeof(FleetBus));
    size_t b_idx = 0;
    for (size_t r = 0; r < n_routes; r++) {
        size_t r_stops = network_route_stop_count(net, r);
        for (int k = 0; k < alloc_eq[r]; k++) {
            buses[b_idx].bus_id = (int)b_idx + 1;
            buses[b_idx].route = (int)r;
            size_t s_idx = ((size_t)k * 2) % (r_stops > 0 ? r_stops : 1);
            int s_idx_val = network_route_stop(net, r, s_idx);
            buses[b_idx].stop_idx = s_idx_val;
            buses[b_idx].load = (k % 3) * 5; /* varying passenger load */
            b_idx++;
        }
    }

    FleetMove *moves = (FleetMove *)malloc(n_buses * sizeof(FleetMove));
    size_t n_moves = 0;
    st = fleet_plan_moves(net, buses, n_buses, alloc_mw, n_routes, moves, n_buses, &n_moves);

    if (st == STATUS_OK) {
        printf("Planned Fleet Moves: %zu\n", n_moves);
        if (n_moves > 0) {
            printf("%-8s | %-10s | %-10s | %-20s | %-12s | %-12s\n",
                   "Bus ID", "From Route", "To Route", "Entry Stop", "Deadhead (s)", "Deadhead (min)");
            printf("---------+------------+------------+----------------------+--------------+--------------\n");
            double total_dh = 0.0;
            for (size_t m = 0; m < n_moves; m++) {
                total_dh += moves[m].deadhead_s;
                int entry_stop = moves[m].entry_stop;
                const DemandStop *ds = (entry_stop >= 0) ? network_stop(net, (size_t)entry_stop) : NULL;
                char entry_buf[80];
                if (entry_stop < 0) {
                    snprintf(entry_buf, sizeof(entry_buf), "Depot / None");
                } else if (ds && ds->name[0]) {
                    snprintf(entry_buf, sizeof(entry_buf), "%.70s", ds->name);
                } else {
                    snprintf(entry_buf, sizeof(entry_buf), "Stop %d", entry_stop);
                }

                printf("Bus %-4d | R%-9d | R%-9d | %-20.20s | %-12.1f | %-12.2f\n",
                       moves[m].bus_id,
                       moves[m].from_route + 1,
                       moves[m].to_route + 1,
                       entry_buf,
                       moves[m].deadhead_s,
                       moves[m].deadhead_s / 60.0);
            }
            printf("---------+------------+------------+------------+--------------+--------------\n");
            printf("Total Deadhead Travel Time: %.1f s (%.2f min)\n", total_dh, total_dh / 60.0);
        } else {
            printf("No fleet moves required (fleet already optimal).\n");
        }
    } else {
        printf("Error planning fleet moves: status %d\n", (int)st);
    }

    free(moves);
    free(buses);
    free(alloc_mw);
    free(alloc_dpb);
    free(alloc_prop);
    free(alloc_eq);
    free(cycles);
    free(demands);
    network_destroy(net);
    if (dp) dp_destroy(dp);
    free(stops);
    graph_destroy(g);
    return 0;
}

/* ---- sim-run and sim-compare ---- */

static double cli_parse_time_min(const char *str) {
    if (!str) return 0.0;
    int h = 0, m = 0;
    if (sscanf(str, "%d:%d", &h, &m) == 2) {
        return (double)(h * 60 + m);
    }
    return atof(str);
}

typedef struct {
    double time_min;
    SimScenarioKind kind;
    int arg;
} CliScenario;

static DemandPredictor *cli_get_predictor(const char *pred_name, const char *train_path, const char *model_path) {
    if (!pred_name || strcmp(pred_name, "none") == 0 || strcmp(pred_name, "oracle") == 0) {
        return NULL;
    }
    if (strcmp(pred_name, "rf") == 0) {
        if (model_path) {
            ForestModel *fm = NULL;
            if (forest_load(&fm, model_path) == STATUS_OK && fm) {
                return dp_from_forest(fm);
            }
        }
        return NULL;
    }
    if (strcmp(pred_name, "ols") == 0) {
        if (train_path) {
            DemandRow *rows = NULL;
            size_t n_rows = 0;
            if (demand_rows_load_csv(train_path, &rows, &n_rows) == STATUS_OK && rows) {
                DemandPredictor *dp = dp_train_ols_band(rows, n_rows);
                free(rows);
                return dp;
            }
        }
        return NULL;
    }
    /* "ma" */
    if (strcmp(pred_name, "ma") == 0) {
        if (train_path) {
            DemandRow *rows = NULL;
            size_t n_rows = 0;
            if (demand_rows_load_csv(train_path, &rows, &n_rows) == STATUS_OK && rows) {
                DemandPredictor *dp = dp_train_moving_avg(rows, n_rows);
                free(rows);
                return dp;
            }
        }
        return NULL;
    }
    return NULL;
}

static int cmd_sim_run(int argc, char **argv) {
    const char *cache_path = NULL;
    const char *train_path = NULL;
    const char *model_path = NULL;
    size_t stops_requested = 60;
    size_t n_routes = 6;
    size_t n_buses = 36;
    size_t n_reserve = 0;
    uint64_t seed = 42;
    int day = 1;
    int rain = 0;
    double start_min = 360.0;
    double end_min = 1320.0;
    double warmup_min = 30.0;
    double noise = 0.10;
    double slice_min = 15.0;
    const char *policy_str = "fixed-equal";
    const char *pred_str = "ma";
    double realloc_interval = 30.0;
    double hysteresis = 0.02;
    int capacity = 60;
    int depot_stop = 0;
    int anomaly_mode = 0; /* 0=none, 1=detect, 2=dispatch */
    int spacing = SIM_SPACING_BY_TIME;
    int reassign_mode = SIM_REASSIGN_DRAIN;
    int print_log = 0;

    #define MAX_CLI_SCENARIOS 64
    CliScenario scenarios[MAX_CLI_SCENARIOS];
    size_t n_scenarios = 0;
    int last_event_on_idx = -1;
    int last_event_off_idx = -1;
    int last_block_idx = -1;
    int last_bus_scen_idx = -1;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
            cache_path = argv[++i];
        } else if (strcmp(argv[i], "--train") == 0 && i + 1 < argc) {
            train_path = argv[++i];
        } else if ((strcmp(argv[i], "--model") == 0 || strcmp(argv[i], "--model-rf") == 0) && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--stops") == 0 && i + 1 < argc) {
            stops_requested = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--routes") == 0 && i + 1 < argc) {
            n_routes = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--buses") == 0 && i + 1 < argc) {
            n_buses = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--reserve") == 0 && i + 1 < argc) {
            n_reserve = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--day") == 0 && i + 1 < argc) {
            day = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rain") == 0 && i + 1 < argc) {
            rain = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            start_min = cli_parse_time_min(argv[++i]);
        } else if (strcmp(argv[i], "--end") == 0 && i + 1 < argc) {
            end_min = cli_parse_time_min(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup_min = atof(argv[++i]);
        } else if (strcmp(argv[i], "--noise") == 0 && i + 1 < argc) {
            noise = atof(argv[++i]);
        } else if (strcmp(argv[i], "--slice") == 0 && i + 1 < argc) {
            slice_min = atof(argv[++i]);
        } else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            policy_str = argv[++i];
        } else if (strcmp(argv[i], "--predictor") == 0 && i + 1 < argc) {
            pred_str = argv[++i];
        } else if (strcmp(argv[i], "--realloc-interval") == 0 && i + 1 < argc) {
            realloc_interval = atof(argv[++i]);
        } else if (strcmp(argv[i], "--hysteresis") == 0 && i + 1 < argc) {
            hysteresis = atof(argv[++i]);
        } else if (strcmp(argv[i], "--capacity") == 0 && i + 1 < argc) {
            capacity = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--depot-stop") == 0 && i + 1 < argc) {
            depot_stop = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--anomaly") == 0 && i + 1 < argc) {
            const char *av = argv[++i];
            if (strcmp(av, "detect") == 0) anomaly_mode = 1;
            else if (strcmp(av, "dispatch") == 0) anomaly_mode = 2;
            else anomaly_mode = 0;
        } else if (strcmp(argv[i], "--spacing") == 0 && i + 1 < argc) {
            const char *sp = argv[++i];
            if (strcmp(sp, "stop") == 0) spacing = SIM_SPACING_BY_STOP;
            else if (strcmp(sp, "time") == 0) spacing = SIM_SPACING_BY_TIME;
        } else if (strcmp(argv[i], "--reassign") == 0 && i + 1 < argc) {
            const char *rm_s = argv[++i];
            if (strcmp(rm_s, "dump") == 0) reassign_mode = SIM_REASSIGN_DUMP;
            else if (strcmp(rm_s, "drain") == 0) reassign_mode = SIM_REASSIGN_DRAIN;
        } else if (strcmp(argv[i], "--log") == 0) {
            print_log = 1;
        } else if (strcmp(argv[i], "--rain-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios + 2 <= MAX_CLI_SCENARIOS) {
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_RAIN_ON, 0};
                scenarios[n_scenarios++] = (CliScenario){t + 60.0, SIM_SCEN_RAIN_OFF, 0};
            }
        } else if (strcmp(argv[i], "--rain-on") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_RAIN_ON, 0};
            }
        } else if (strcmp(argv[i], "--rain-off") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_RAIN_OFF, 0};
            }
        } else if (strcmp(argv[i], "--event-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios + 2 <= MAX_CLI_SCENARIOS) {
                last_event_on_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_EVENT_ON, 0};
                last_event_off_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t + 60.0, SIM_SCEN_EVENT_OFF, 0};
            }
        } else if (strcmp(argv[i], "--event-stop") == 0 && i + 1 < argc) {
            int st_idx = atoi(argv[++i]);
            if (last_event_on_idx >= 0) scenarios[last_event_on_idx].arg = st_idx;
            if (last_event_off_idx >= 0) scenarios[last_event_off_idx].arg = st_idx;
        } else if (strcmp(argv[i], "--event-len") == 0 && i + 1 < argc) {
            double len = atof(argv[++i]);
            if (last_event_on_idx >= 0 && last_event_off_idx >= 0) {
                scenarios[last_event_off_idx].time_min = scenarios[last_event_on_idx].time_min + len;
            }
        } else if (strcmp(argv[i], "--breakdown-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                last_bus_scen_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_BREAKDOWN, 0};
            }
        } else if (strcmp(argv[i], "--repair-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                last_bus_scen_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_REPAIR, 0};
            }
        } else if (strcmp(argv[i], "--bus") == 0 && i + 1 < argc) {
            int b_id = atoi(argv[++i]);
            if (last_bus_scen_idx >= 0) scenarios[last_bus_scen_idx].arg = b_id;
        } else if (strcmp(argv[i], "--block-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                last_block_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_BLOCK_EDGE, 0};
            }
        } else if (strcmp(argv[i], "--unblock-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                last_block_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_UNBLOCK_EDGE, 0};
            }
        } else if (strcmp(argv[i], "--edge") == 0 && i + 1 < argc) {
            int e_id = atoi(argv[++i]);
            if (last_block_idx >= 0) scenarios[last_block_idx].arg = e_id;
        }
    }

    char resolved_cache[512], resolved_train[512], resolved_model[512];
    if (!cache_path) {
        snprintf(resolved_cache, sizeof(resolved_cache), "%s/graph_cache.txt", g_data_dir);
        struct stat st_c;
        if (stat(resolved_cache, &st_c) != 0) {
            snprintf(resolved_cache, sizeof(resolved_cache), "%s/cache_central_blr.json", g_data_dir);
        }
        cache_path = resolved_cache;
    }
    if (!train_path) {
        snprintf(resolved_train, sizeof(resolved_train), "%s/demand_train.csv", g_data_dir);
        train_path = resolved_train;
    }
    if (!model_path) {
        snprintf(resolved_model, sizeof(resolved_model), "%s/demand_rf.txt", g_data_dir);
        model_path = resolved_model;
    }

    SimPolicy policy = SIM_POLICY_FIXED_EQUAL;
    if (strcmp(policy_str, "fixed-prop") == 0 || strcmp(policy_str, "fixed_proportional") == 0 || strcmp(policy_str, "fixed-proportional") == 0) {
        policy = SIM_POLICY_FIXED_PROPORTIONAL;
    } else if (strcmp(policy_str, "greedy-spec") == 0 || strcmp(policy_str, "greedy_spec") == 0) {
        policy = SIM_POLICY_GREEDY_SPEC;
    } else if (strcmp(policy_str, "greedy-marginal") == 0 || strcmp(policy_str, "greedy_marginal") == 0) {
        policy = SIM_POLICY_GREEDY_MARGINAL;
    }

    if (!pred_str) {
        if (policy == SIM_POLICY_GREEDY_SPEC || policy == SIM_POLICY_GREEDY_MARGINAL) {
            pred_str = "ma";
        } else {
            pred_str = "none";
        }
    }

    DemandPredictor *dp_ma = cli_get_predictor("ma", train_path, model_path);
    DemandPredictor *dp_ols = cli_get_predictor("ols", train_path, model_path);
    DemandPredictor *dp_rf = cli_get_predictor("rf", train_path, model_path);

    printf("[predictor] oracle: loaded (built-in)\n");
    if (dp_ma) {
        printf("[predictor] ma: loaded (%s)\n", train_path);
    } else {
        printf("[predictor] ma: not loaded (%s)\n", train_path);
    }
    if (dp_ols) {
        printf("[predictor] ols: loaded (%s)\n", train_path);
    } else {
        printf("[predictor] ols: not loaded (%s)\n", train_path);
    }
    if (dp_rf) {
        printf("[predictor] rf: loaded (%s)\n", model_path);
    } else {
        printf("[predictor] rf: not loaded (%s)\n", model_path);
    }

    DemandPredictor *dp = NULL;
    if (strcmp(pred_str, "rf") == 0) {
        if (!dp_rf) {
            fprintf(stderr, "Error: requested predictor 'rf' could not be built from %s\n", model_path);
            if (dp_ma) dp_destroy(dp_ma);
            if (dp_ols) dp_destroy(dp_ols);
            return 1;
        }
        dp = dp_rf;
        dp_rf = NULL;
    } else if (strcmp(pred_str, "ma") == 0) {
        if (!dp_ma) {
            fprintf(stderr, "Error: requested predictor 'ma' could not be built from %s\n", train_path);
            if (dp_ols) dp_destroy(dp_ols);
            if (dp_rf) dp_destroy(dp_rf);
            return 1;
        }
        dp = dp_ma;
        dp_ma = NULL;
    } else if (strcmp(pred_str, "ols") == 0) {
        if (!dp_ols) {
            fprintf(stderr, "Error: requested predictor 'ols' could not be built from %s\n", train_path);
            if (dp_ma) dp_destroy(dp_ma);
            if (dp_rf) dp_destroy(dp_rf);
            return 1;
        }
        dp = dp_ols;
        dp_ols = NULL;
    } else if (strcmp(pred_str, "oracle") != 0 && strcmp(pred_str, "none") != 0) {
        fprintf(stderr, "Error: unknown predictor '%s'\n", pred_str);
        if (dp_ma) dp_destroy(dp_ma);
        if (dp_ols) dp_destroy(dp_ols);
        if (dp_rf) dp_destroy(dp_rf);
        return 1;
    }
    if (dp_ma) { dp_destroy(dp_ma); dp_ma = NULL; }
    if (dp_ols) { dp_destroy(dp_ols); dp_ols = NULL; }
    if (dp_rf) { dp_destroy(dp_rf); dp_rf = NULL; }

    Graph *g = NULL;
    Status st = graph_load(&g, cache_path);
    if (st != STATUS_OK || !g) {
        fprintf(stderr, "Error: failed to load graph cache from %s\n", cache_path);
        if (dp) dp_destroy(dp);
        return 1;
    }

    DemandStop *stops = NULL;
    size_t n_stops = 0;
    st = demand_stops_from_graph_anchored(g, stops_requested, seed, &stops, &n_stops);
    if (st != STATUS_OK || !stops) {
        fprintf(stderr, "Error: failed to extract anchored stops\n");
        if (dp) dp_destroy(dp);
        graph_destroy(g);
        return 1;
    }

    NetworkConfig ncfg;
    network_config_default(&ncfg);
    ncfg.n_routes = n_routes;
    ncfg.build_matrix = 1;

    NetworkModel *net = NULL;
    st = network_build(g, stops, n_stops, &ncfg, &net);
    if (st != STATUS_OK || !net) {
        fprintf(stderr, "Error: failed to build network\n");
        if (dp) dp_destroy(dp);
        free(stops);
        graph_destroy(g);
        return 1;
    }

    SimConfig scfg;
    sim_config_default(&scfg);
    scfg.seed = seed;
    scfg.day = day;
    scfg.day_type = (day % 7 == 5 || day % 7 == 6) ? DAY_WEEKEND : DAY_WEEKDAY;
    scfg.rain = rain;
    scfg.start_min = start_min;
    scfg.end_min = end_min;
    scfg.warmup_min = warmup_min;
    scfg.travel_noise = noise;
    scfg.slice_min = slice_min;
    scfg.n_buses = (int)n_buses;
    scfg.n_reserve = (int)n_reserve;
    scfg.bus_capacity = capacity;
    scfg.depot_stop = depot_stop;
    scfg.policy = policy;
    scfg.predictor = dp;
    scfg.realloc_interval_min = realloc_interval;
    scfg.realloc_hysteresis = hysteresis;
    scfg.anomaly_detect = (anomaly_mode >= 1) ? 1 : 0;
    scfg.anomaly_dispatch = (anomaly_mode >= 2) ? 1 : 0;
    scfg.initial_spacing = spacing;
    scfg.reassign_mode = reassign_mode;

    double *prop_weights = NULL;
    if (policy == SIM_POLICY_FIXED_PROPORTIONAL) {
        prop_weights = (double *)calloc(n_routes, sizeof(double));
        if (prop_weights) {
            network_route_demands(net, dp, (int)(start_min / 60.0), scfg.day_type, rain, NULL, 0, prop_weights);
            scfg.prop_weights = prop_weights;
        }
    }

    Sim *sim = NULL;
    st = sim_create(net, &scfg, &sim);
    if (st != STATUS_OK || !sim) {
        fprintf(stderr, "Error: failed to create simulation (status %d)\n", (int)st);
        free(prop_weights);
        if (dp) dp_destroy(dp);
        network_destroy(net);
        free(stops);
        graph_destroy(g);
        return 1;
    }

    for (size_t i = 0; i < n_scenarios; i++) {
        SimScenario sc = {scenarios[i].time_min, scenarios[i].kind, scenarios[i].arg};
        sim_schedule(sim, &sc);
    }

    printf("\n=== SIMULATION CONFIGURATION ===\n");
    printf("Stops:               %zu\n", n_stops);
    printf("Routes:              %zu\n", n_routes);
    printf("Buses:               %zu (Reserve: %zu)\n", n_buses, n_reserve);
    printf("Capacity:            %d pax\n", capacity);
    printf("Seed:                %llu\n", (unsigned long long)seed);
    printf("Day:                 %d (%s)\n", day, scfg.day_type == DAY_WEEKEND ? "Weekend" : "Weekday");
    printf("Time Window:         %02d:%02d -> %02d:%02d (Warmup: %.1f min, Slice: %.1f min)\n",
           (int)(start_min / 60.0), (int)fmod(start_min, 60.0),
           (int)(end_min / 60.0), (int)fmod(end_min, 60.0),
           warmup_min, slice_min);
    printf("Travel Noise:        %.2f\n", noise);
    printf("Policy:              %s\n", policy_str);
    printf("Predictor:           %s\n", pred_str);
    printf("Realloc Interval:    %.1f min (Hysteresis: %.2f)\n", realloc_interval, hysteresis);
    printf("Spacing:             %s\n", (spacing == SIM_SPACING_BY_TIME) ? "by-time" : "by-stop");
    printf("Reassign Mode:       %s\n", (reassign_mode == SIM_REASSIGN_DRAIN) ? "drain" : "dump");
    printf("Anomaly Mode:        %s\n", (anomaly_mode == 2) ? "detect+dispatch" : (anomaly_mode == 1 ? "detect" : "none"));
    printf("Scenarios Scheduled: %zu\n", n_scenarios);

    st = sim_run_until(sim, scfg.end_min);
    if (st != STATUS_OK) {
        fprintf(stderr, "Error: simulation execution failed (status %d)\n", (int)st);
    }

    printf("\n=== PER-ROUTE RESULTS ===\n");
    printf("%-8s | %-10s | %-10s | %-12s | %-8s | %-13s | %-12s | %-11s\n",
           "Route", "Tour len", "Cycle(s)", "Buses", "Boarded", "Still waiting", "Mean wait(s)", "Max wait(s)");
    printf("---------+------------+------------+--------------+----------+---------------+--------------+------------\n");
    for (size_t r = 0; r < network_route_count(net); r++) {
        SimRouteStats rs;
        sim_route_stats(sim, r, &rs);
        size_t tour_len = network_route_stop_count(net, r);
        double cycle_s = network_route_cycle_s(net, r);
        size_t b_cnt = sim_route_bus_count(sim, r);
        printf("R%-7zu | %-10zu | %-10.1f | %-12zu | %-8zu | %-13zu | %-12.2f | %-11.2f\n",
               r + 1, tour_len, cycle_s, b_cnt,
               rs.boarded, rs.still_waiting,
               rs.mean_wait_s, rs.wait_max_s);
    }

    SimStats gst;
    sim_stats(sim, &gst);
    printf("\n=== GLOBAL SUMMARY ===\n");
    printf("Total arrivals:             %zu\n", gst.arrived);
    printf("Boarded passengers:         %zu\n", gst.boarded);
    printf("Still waiting:              %zu\n", gst.still_waiting);
    printf("Mean waiting time (boarded): %.2f s\n", gst.mean_wait_boarded_s);
    printf("Mean waiting time (all):    %.2f s\n", gst.mean_wait_incl_censored_s);
    printf("P50 waiting time:           %.2f s\n", gst.p50_s);
    printf("P95 waiting time:           %.2f s\n", gst.p95_s);
    printf("Max waiting time:           %.2f s\n", gst.wait_max_s);
    printf("Full bus events:            %zu\n", gst.full_bus_events);
    printf("Forced dumps:               %zu\n", gst.forced_dumps);
    printf("Stranded by breakdown:      %zu\n", gst.stranded_by_breakdown);
    printf("Bus arrivals:               %zu\n", gst.bus_arrivals);
    printf("Moves applied:              %zu\n", gst.moves_applied);
    printf("Total deadhead time:        %.1f s\n", gst.deadhead_s_total);
    printf("Decisions evaluated:        %zu\n", gst.decisions);
    printf("Decisions applied:          %zu\n", gst.decisions_applied);
    printf("Anomaly flags:              %zu\n", gst.anomaly_flags);
    printf("Emergency dispatches:       %zu\n", gst.emergency_dispatches);
    printf("Mean load factor:           %.4f\n", gst.mean_load_factor);

    if (gst.anomaly_flags > 0) {
        printf("\n=== ANOMALY DETECTION REPORT ===\n");
        printf("%-8s | %-15s | %-17s | %s\n", "Stop", "Flag Time (min)", "First Event (min)", "Response Delay (min)");
        printf("---------+-----------------+-------------------+---------------------\n");
        size_t n_fl = sim_flag_count(sim);
        for (size_t i = 0; i < n_fl; i++) {
            AnomalyFlag fl;
            if (sim_flag(sim, i, &fl) == STATUS_OK) {
                double flag_t = sim_first_flag_min(sim, fl.stop);
                double ev_t = sim_event_start_min(sim, fl.stop);
                double delay = (ev_t >= 0.0 && flag_t >= ev_t) ? (flag_t - ev_t) : 0.0;
                printf("%-8zu | %-15.1f | %-17.1f | %.1f\n", fl.stop, flag_t, ev_t, delay);
            }
        }
    }

    if (print_log) {
        printf("\n=== SIMULATION LOG ===\n");
        size_t lcnt = sim_log_count(sim);
        for (size_t i = 0; i < lcnt; i++) {
            const char *line = sim_log_line(sim, i);
            if (line) printf("%s\n", line);
        }
    }

    sim_destroy(sim);
    free(prop_weights);
    if (dp) dp_destroy(dp);
    network_destroy(net);
    free(stops);
    graph_destroy(g);
    return 0;
}

static int cmd_sim_compare(int argc, char **argv) {
    const char *cache_path = NULL;
    const char *train_path = NULL;
    const char *model_path = NULL;
    size_t stops_requested = 60;
    size_t n_routes = 6;
    size_t n_buses = 36;
    size_t n_reserve = 0;
    uint64_t seed = 42;
    int day = 1;
    int rain = 0;
    double start_min = 360.0;
    double end_min = 1320.0;
    double warmup_min = 30.0;
    double noise = 0.10;
    double slice_min = 15.0;
    double realloc_interval = 30.0;
    double hysteresis = 0.02;
    int capacity = 60;
    int depot_stop = 0;
    int anomaly_mode = 0;
    int spacing = SIM_SPACING_BY_TIME;
    int reassign_mode = SIM_REASSIGN_DRAIN;

    CliScenario scenarios[MAX_CLI_SCENARIOS];
    size_t n_scenarios = 0;
    int last_event_on_idx = -1;
    int last_event_off_idx = -1;
    int last_block_idx = -1;
    int last_bus_scen_idx = -1;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
            cache_path = argv[++i];
        } else if (strcmp(argv[i], "--train") == 0 && i + 1 < argc) {
            train_path = argv[++i];
        } else if ((strcmp(argv[i], "--model") == 0 || strcmp(argv[i], "--model-rf") == 0) && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--stops") == 0 && i + 1 < argc) {
            stops_requested = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--routes") == 0 && i + 1 < argc) {
            n_routes = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--buses") == 0 && i + 1 < argc) {
            n_buses = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--reserve") == 0 && i + 1 < argc) {
            n_reserve = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--day") == 0 && i + 1 < argc) {
            day = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rain") == 0 && i + 1 < argc) {
            rain = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            start_min = cli_parse_time_min(argv[++i]);
        } else if (strcmp(argv[i], "--end") == 0 && i + 1 < argc) {
            end_min = cli_parse_time_min(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup_min = atof(argv[++i]);
        } else if (strcmp(argv[i], "--noise") == 0 && i + 1 < argc) {
            noise = atof(argv[++i]);
        } else if (strcmp(argv[i], "--slice") == 0 && i + 1 < argc) {
            slice_min = atof(argv[++i]);
        } else if (strcmp(argv[i], "--realloc-interval") == 0 && i + 1 < argc) {
            realloc_interval = atof(argv[++i]);
        } else if (strcmp(argv[i], "--hysteresis") == 0 && i + 1 < argc) {
            hysteresis = atof(argv[++i]);
        } else if (strcmp(argv[i], "--capacity") == 0 && i + 1 < argc) {
            capacity = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--depot-stop") == 0 && i + 1 < argc) {
            depot_stop = (size_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--anomaly") == 0 && i + 1 < argc) {
            const char *av = argv[++i];
            if (strcmp(av, "detect") == 0) anomaly_mode = 1;
            else if (strcmp(av, "dispatch") == 0) anomaly_mode = 2;
            else anomaly_mode = 0;
        } else if (strcmp(argv[i], "--spacing") == 0 && i + 1 < argc) {
            const char *sp = argv[++i];
            if (strcmp(sp, "stop") == 0) spacing = SIM_SPACING_BY_STOP;
            else if (strcmp(sp, "time") == 0) spacing = SIM_SPACING_BY_TIME;
        } else if (strcmp(argv[i], "--reassign") == 0 && i + 1 < argc) {
            const char *rm_s = argv[++i];
            if (strcmp(rm_s, "dump") == 0) reassign_mode = SIM_REASSIGN_DUMP;
            else if (strcmp(rm_s, "drain") == 0) reassign_mode = SIM_REASSIGN_DRAIN;
        } else if (strcmp(argv[i], "--rain-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios + 2 <= MAX_CLI_SCENARIOS) {
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_RAIN_ON, 0};
                scenarios[n_scenarios++] = (CliScenario){t + 60.0, SIM_SCEN_RAIN_OFF, 0};
            }
        } else if (strcmp(argv[i], "--rain-on") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_RAIN_ON, 0};
            }
        } else if (strcmp(argv[i], "--rain-off") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_RAIN_OFF, 0};
            }
        } else if (strcmp(argv[i], "--event-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios + 2 <= MAX_CLI_SCENARIOS) {
                last_event_on_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_EVENT_ON, 0};
                last_event_off_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t + 60.0, SIM_SCEN_EVENT_OFF, 0};
            }
        } else if (strcmp(argv[i], "--event-stop") == 0 && i + 1 < argc) {
            int st_idx = atoi(argv[++i]);
            if (last_event_on_idx >= 0) scenarios[last_event_on_idx].arg = st_idx;
            if (last_event_off_idx >= 0) scenarios[last_event_off_idx].arg = st_idx;
        } else if (strcmp(argv[i], "--event-len") == 0 && i + 1 < argc) {
            double len = atof(argv[++i]);
            if (last_event_on_idx >= 0 && last_event_off_idx >= 0) {
                scenarios[last_event_off_idx].time_min = scenarios[last_event_on_idx].time_min + len;
            }
        } else if (strcmp(argv[i], "--breakdown-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                last_bus_scen_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_BREAKDOWN, 0};
            }
        } else if (strcmp(argv[i], "--repair-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                last_bus_scen_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_REPAIR, 0};
            }
        } else if (strcmp(argv[i], "--bus") == 0 && i + 1 < argc) {
            int b_id = atoi(argv[++i]);
            if (last_bus_scen_idx >= 0) scenarios[last_bus_scen_idx].arg = b_id;
        } else if (strcmp(argv[i], "--block-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                last_block_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_BLOCK_EDGE, 0};
            }
        } else if (strcmp(argv[i], "--unblock-at") == 0 && i + 1 < argc) {
            double t = cli_parse_time_min(argv[++i]);
            if (n_scenarios < MAX_CLI_SCENARIOS) {
                last_block_idx = (int)n_scenarios;
                scenarios[n_scenarios++] = (CliScenario){t, SIM_SCEN_UNBLOCK_EDGE, 0};
            }
        } else if (strcmp(argv[i], "--edge") == 0 && i + 1 < argc) {
            int e_id = atoi(argv[++i]);
            if (last_block_idx >= 0) scenarios[last_block_idx].arg = e_id;
        }
    }

    char resolved_cache[512], resolved_train[512], resolved_model[512];
    if (!cache_path) {
        snprintf(resolved_cache, sizeof(resolved_cache), "%s/graph_cache.txt", g_data_dir);
        struct stat st_c;
        if (stat(resolved_cache, &st_c) != 0) {
            snprintf(resolved_cache, sizeof(resolved_cache), "%s/cache_central_blr.json", g_data_dir);
        }
        cache_path = resolved_cache;
    }
    if (!train_path) {
        snprintf(resolved_train, sizeof(resolved_train), "%s/demand_train.csv", g_data_dir);
        train_path = resolved_train;
    }
    if (!model_path) {
        snprintf(resolved_model, sizeof(resolved_model), "%s/demand_rf.txt", g_data_dir);
        model_path = resolved_model;
    }

    DemandPredictor *dp_ma = cli_get_predictor("ma", train_path, model_path);
    DemandPredictor *dp_ols = cli_get_predictor("ols", train_path, model_path);
    DemandPredictor *dp_rf = cli_get_predictor("rf", train_path, model_path);

    printf("[predictor] oracle: loaded (built-in)\n");
    if (dp_ma) {
        printf("[predictor] ma: loaded (%s)\n", train_path);
    } else {
        printf("[predictor] ma: not loaded (%s)\n", train_path);
    }
    if (dp_ols) {
        printf("[predictor] ols: loaded (%s)\n", train_path);
    } else {
        printf("[predictor] ols: not loaded (%s)\n", train_path);
    }
    if (dp_rf) {
        printf("[predictor] rf: loaded (%s)\n", model_path);
    } else {
        printf("[predictor] rf: not loaded (%s)\n", model_path);
    }

    Graph *g = NULL;
    Status st = graph_load(&g, cache_path);
    if (st != STATUS_OK || !g) {
        fprintf(stderr, "Error: failed to load graph cache from %s\n", cache_path);
        if (dp_ma) dp_destroy(dp_ma);
        if (dp_ols) dp_destroy(dp_ols);
        if (dp_rf) dp_destroy(dp_rf);
        return 1;
    }

    DemandStop *stops = NULL;
    size_t n_stops = 0;
    st = demand_stops_from_graph_anchored(g, stops_requested, seed, &stops, &n_stops);
    if (st != STATUS_OK || !stops) {
        fprintf(stderr, "Error: failed to extract anchored stops\n");
        if (dp_ma) dp_destroy(dp_ma);
        if (dp_ols) dp_destroy(dp_ols);
        if (dp_rf) dp_destroy(dp_rf);
        graph_destroy(g);
        return 1;
    }

    NetworkConfig ncfg;
    network_config_default(&ncfg);
    ncfg.n_routes = n_routes;
    ncfg.build_matrix = 1;

    NetworkModel *net = NULL;
    st = network_build(g, stops, n_stops, &ncfg, &net);
    if (st != STATUS_OK || !net) {
        fprintf(stderr, "Error: failed to build network\n");
        if (dp_ma) dp_destroy(dp_ma);
        if (dp_ols) dp_destroy(dp_ols);
        if (dp_rf) dp_destroy(dp_rf);
        free(stops);
        graph_destroy(g);
        return 1;
    }

    typedef struct {
        const char *name;
        SimPolicy policy;
        DemandPredictor *dp;
        int is_prop;
        int needs_train;
        int needs_model;
    } CompPolicy;

    CompPolicy pols[6] = {
        {"fixed_equal",            SIM_POLICY_FIXED_EQUAL,        NULL,   0, 0, 0},
        {"fixed_proportional",     SIM_POLICY_FIXED_PROPORTIONAL, dp_ma,  1, 1, 0},
        {"greedy_spec_ma",         SIM_POLICY_GREEDY_SPEC,        dp_ma,  0, 1, 0},
        {"greedy_marginal_ma",     SIM_POLICY_GREEDY_MARGINAL,    dp_ma,  0, 1, 0},
        {"greedy_marginal_rf",     SIM_POLICY_GREEDY_MARGINAL,    dp_rf,  0, 0, 1},
        {"greedy_marginal_oracle", SIM_POLICY_GREEDY_MARGINAL,    NULL,   0, 0, 0}
    };

    SimStats stats[6];
    memset(stats, 0, sizeof(stats));

    for (int p = 0; p < 6; p++) {
        if (pols[p].needs_train && !dp_ma) {
            printf("skipped: training data not loaded: %s\n", train_path);
            continue;
        }
        if (pols[p].needs_model && !dp_rf) {
            printf("skipped: model not loaded: %s\n", model_path);
            continue;
        }

        SimConfig scfg;
        sim_config_default(&scfg);
        scfg.seed = seed;
        scfg.day = day;
        scfg.day_type = (day % 7 == 5 || day % 7 == 6) ? DAY_WEEKEND : DAY_WEEKDAY;
        scfg.rain = rain;
        scfg.start_min = start_min;
        scfg.end_min = end_min;
        scfg.warmup_min = warmup_min;
        scfg.travel_noise = noise;
        scfg.slice_min = slice_min;
        scfg.n_buses = (int)n_buses;
        scfg.n_reserve = (int)n_reserve;
        scfg.bus_capacity = capacity;
        scfg.depot_stop = depot_stop;
        scfg.policy = pols[p].policy;
        scfg.predictor = pols[p].dp;
        scfg.realloc_interval_min = realloc_interval;
        scfg.realloc_hysteresis = hysteresis;
        scfg.anomaly_detect = (anomaly_mode >= 1) ? 1 : 0;
        scfg.anomaly_dispatch = (anomaly_mode >= 2) ? 1 : 0;
        scfg.initial_spacing = spacing;
        scfg.reassign_mode = reassign_mode;

        double *pw = NULL;
        if (pols[p].is_prop) {
            pw = (double *)calloc(n_routes, sizeof(double));
            if (pw) {
                network_route_demands(net, pols[p].dp, (int)(start_min / 60.0), scfg.day_type, rain, NULL, 0, pw);
                scfg.prop_weights = pw;
            }
        }

        Sim *sim = NULL;
        st = sim_create(net, &scfg, &sim);
        if (st == STATUS_OK && sim) {
            for (size_t i = 0; i < n_scenarios; i++) {
                SimScenario sc = {scenarios[i].time_min, scenarios[i].kind, scenarios[i].arg};
                sim_schedule(sim, &sc);
            }
            sim_run_until(sim, scfg.end_min);
            sim_stats(sim, &stats[p]);
            sim_destroy(sim);
        }
        free(pw);
    }

    printf("\n=== POLICY COMPARISON SUMMARY ===\n");
    printf("Spacing:                  %s\n", (spacing == SIM_SPACING_BY_TIME) ? "by-time" : "by-stop");
    printf("Reassign Mode:            %s\n", (reassign_mode == SIM_REASSIGN_DRAIN) ? "drain" : "dump");
    printf("%-24s | %-8s | %-13s | %-13s | %-12s | %-12s | %-8s | %-12s | %-5s | %-5s | %-10s\n",
           "Policy", "Boarded", "Still Waiting", "Mean Wait (s)", "P95 Wait (s)", "Max Wait (s)",
           "Full Bus", "Forced Dumps", "Moves", "Flags", "Dispatches");
    printf("-------------------------+----------+---------------+---------------+--------------+--------------+----------+--------------+-------+-------+-----------\n");

    for (int p = 0; p < 6; p++) {
        if (pols[p].needs_train && !dp_ma) {
            printf("%-24s | (skipped: training data not loaded: %s)\n", pols[p].name, train_path);
            continue;
        }
        if (pols[p].needs_model && !dp_rf) {
            printf("%-24s | (skipped: model not loaded: %s)\n", pols[p].name, model_path);
            continue;
        }
        printf("%-24s | %-8zu | %-13zu | %-13.2f | %-12.2f | %-12.2f | %-8zu | %-12zu | %-5zu | %-5zu | %-10zu\n",
               pols[p].name,
               stats[p].boarded,
               stats[p].still_waiting,
               stats[p].mean_wait_boarded_s,
               stats[p].p95_s,
               stats[p].wait_max_s,
               stats[p].full_bus_events,
               stats[p].forced_dumps,
               stats[p].moves_applied,
               stats[p].anomaly_flags,
               stats[p].emergency_dispatches);
    }
    printf("\n");

    if (dp_ma) dp_destroy(dp_ma);
    if (dp_ols) dp_destroy(dp_ols);
    if (dp_rf) dp_destroy(dp_rf);
    network_destroy(net);
    free(stops);
    graph_destroy(g);
    return 0;
}

/* ---- main ---- */

int main(int argc, char **argv) {
    env_load_default();

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* Extract --data-dir if present */
    const char *override_dir = NULL;
    int first_cmd_idx = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            override_dir = argv[i + 1];
            /* Shift argv past --data-dir */
            for (int j = i; j + 2 < argc; j++) {
                argv[j] = argv[j + 2];
            }
            argc -= 2;
            i--;
        }
    }
    resolve_data_dir(override_dir);

    if (first_cmd_idx >= argc) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[first_cmd_idx];
    int sub_argc = argc - (first_cmd_idx + 1);
    char **sub_argv = argv + (first_cmd_idx + 1);

    if (strcmp(cmd, "demo") == 0) {
        return cmd_demo();
    } else if (strcmp(cmd, "info") == 0) {
        return cmd_info(sub_argc, sub_argv);
    } else if (strcmp(cmd, "route") == 0) {
        return cmd_route(sub_argc, sub_argv);
    } else if (strcmp(cmd, "bench") == 0) {
        return cmd_bench(sub_argc, sub_argv);
    } else if (strcmp(cmd, "osm") == 0) {
        return cmd_osm(sub_argc, sub_argv);
    } else if (strcmp(cmd, "traffic") == 0) {
        return cmd_traffic(sub_argc, sub_argv);
    } else if (strcmp(cmd, "stops-info") == 0) {
        return cmd_stops_info(sub_argc, sub_argv);
    } else if (strcmp(cmd, "gen-demand") == 0) {
        return cmd_gen_demand(sub_argc, sub_argv);
    } else if (strcmp(cmd, "demand-eval") == 0) {
        return cmd_demand_eval(sub_argc, sub_argv);
    } else if (strcmp(cmd, "golden-check") == 0) {
        return cmd_golden_check(sub_argc, sub_argv);
    } else if (strcmp(cmd, "network-info") == 0) {
        return cmd_network_info(sub_argc, sub_argv);
    } else if (strcmp(cmd, "alloc-demo") == 0) {
        return cmd_alloc_demo(sub_argc, sub_argv);
    } else if (strcmp(cmd, "sim-run") == 0) {
        return cmd_sim_run(sub_argc, sub_argv);
    } else if (strcmp(cmd, "sim-compare") == 0) {
        return cmd_sim_compare(sub_argc, sub_argv);
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        usage(argv[0]);
        return 1;
    }
}
