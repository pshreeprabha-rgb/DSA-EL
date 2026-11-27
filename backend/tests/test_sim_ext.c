/**
 * @file test_sim_ext.c
 * @brief Unit and integration tests for Part 3C-1 simulation extensions
 *        (DRAIN reassignment, runtime control APIs, motion, hourly arrivals,
 *         and loop observer).
 */

#include "anomaly.h"
#include "fleet_alloc.h"
#include "graph.h"
#include "network.h"
#include "sim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_assertions_run = 0;
static int g_assertions_failed = 0;

#define ASSERT_TRUE(cond)                                                      \
  do {                                                                         \
    g_assertions_run++;                                                        \
    if (!(cond)) {                                                             \
      printf("FAILED assertion at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      g_assertions_failed++;                                                   \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    g_assertions_run++;                                                        \
    if ((a) != (b)) {                                                          \
      printf("FAILED assertion at %s:%d: %s == %s (%lld != %lld)\n", __FILE__, \
             __LINE__, #a, #b, (long long)(a), (long long)(b));                \
      g_assertions_failed++;                                                   \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_APPROX(a, b, eps)                                               \
  do {                                                                         \
    g_assertions_run++;                                                        \
    if (fabs((double)(a) - (double)(b)) > (double)(eps)) {                     \
      printf("FAILED assertion at %s:%d: %s ~= %s (|%f - %f| > %f)\n",         \
             __FILE__, __LINE__, #a, #b, (double)(a), (double)(b),             \
             (double)(eps));                                                   \
      g_assertions_failed++;                                                   \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_NOT_NULL(ptr)                                                   \
  do {                                                                         \
    g_assertions_run++;                                                        \
    if ((ptr) == NULL) {                                                       \
      printf("FAILED assertion at %s:%d: %s != NULL\n", __FILE__, __LINE__,    \
             #ptr);                                                            \
      g_assertions_failed++;                                                   \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_NULL(ptr)                                                       \
  do {                                                                         \
    g_assertions_run++;                                                        \
    if ((ptr) != NULL) {                                                       \
      printf("FAILED assertion at %s:%d: %s == NULL\n", __FILE__, __LINE__,    \
             #ptr);                                                            \
      g_assertions_failed++;                                                   \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define TEST_START(name)                                                       \
  do {                                                                         \
    g_tests_run++;                                                             \
    printf("[RUN ] %s\n", name);                                               \
  } while (0)

#define TEST_PASS()                                                            \
  do {                                                                         \
    g_tests_passed++;                                                          \
    printf("[PASS] %s\n", __func__);                                           \
  } while (0)

#define RUN_TEST(fn)                                                           \
  do {                                                                         \
    int failed_before = g_assertions_failed;                                   \
    fn();                                                                      \
    if (g_assertions_failed > failed_before) {                                 \
      g_tests_failed++;                                                        \
      printf("[FAIL] %s\n", #fn);                                              \
    }                                                                          \
  } while (0)

static int test_summary(void) {
  printf("----------------------------------------\n");
  printf("Tests run: %d | Passed: %d | Failed: %d\n", g_tests_run,
         g_tests_passed, g_tests_failed);
  printf("Assertions run: %d | Failed: %d\n", g_assertions_run,
         g_assertions_failed);
  printf("----------------------------------------\n");
  return (g_tests_failed == 0 && g_assertions_failed == 0) ? 0 : 1;
}

/* --- Fixture matching tests/test_sim.c --- */

static Graph *build_line_graph_fixture(DemandStop stops_out[8]) {
  Graph *g = graph_create();
  if (!g) return NULL;

  double lat = 12.9716;
  double lng0 = 77.5946;

  for (int i = 0; i < 8; i++) {
    char name[32];
    snprintf(name, sizeof(name), "Stop %d", i);
    graph_add_node(g, (int64_t)(1000 + i), lat, lng0 + (double)i * 0.001, name,
                   1);
  }

  for (int i = 0; i < 7; i++) {
    double d = graph_haversine_m(lat, lng0 + (double)i * 0.001, lat,
                                 lng0 + (double)(i + 1) * 0.001);
    char e_fwd[16], e_rev[16];
    snprintf(e_fwd, sizeof(e_fwd), "e%d_%d", i, i + 1);
    snprintf(e_rev, sizeof(e_rev), "e%d_%d", i + 1, i);
    graph_add_edge(g, i, i + 1, d, 100.0, e_fwd, ROAD_PRIMARY);
    graph_add_edge(g, i + 1, i, d, 100.0, e_rev, ROAD_PRIMARY);
  }

  if (stops_out) {
    for (int i = 0; i < 8; i++) {
      stops_out[i].node = (size_t)i;
      stops_out[i].type = (i < 4) ? STOP_OFFICE : STOP_RESIDENTIAL;
      stops_out[i].scale = 1.0;
      snprintf(stops_out[i].name, sizeof(stops_out[i].name), "Stop %d", i);
      stops_out[i].lat = lat;
      stops_out[i].lng = lng0 + (double)i * 0.001;
    }
  }
  return g;
}

static NetworkModel *build_line_network_opt(Graph *g, DemandStop *stops,
                                            size_t n_routes, int build_matrix) {
  NetworkConfig ncfg;
  network_config_default(&ncfg);
  ncfg.n_routes = n_routes;
  ncfg.dwell_s_per_stop = 20.0;
  ncfg.build_matrix = build_matrix;
  NetworkModel *net = NULL;
  Status st = network_build(g, stops, 8, &ncfg, &net);
  if (st != STATUS_OK) return NULL;
  return net;
}

static void get_scripted_config(SimConfig *c) {
  sim_config_default(c);
  c->arrivals_enabled = 0;
  c->dwell_s = 20.0;
  c->per_pax_s = 0.0;
  c->travel_noise = 0.0;
  c->bus_capacity = 100;
  c->n_buses = 2;
  c->n_reserve = 0;
  c->apply_traffic = 0;
  c->warmup_min = 0.0;
  c->slice_min = 15.0;
  c->start_min = 360.0;
  c->end_min = 1320.0;
  c->policy = SIM_POLICY_FIXED_EQUAL;
  c->initial_spacing = SIM_SPACING_BY_STOP;
}

/* 1. Reassign mode defaults and validation */
static void test_ext_reassign_config_validation(void) {
  TEST_START("test_ext_reassign_config_validation");

  SimConfig def;
  sim_config_default(&def);
  /* Hand derivation: default reassign_mode must remain SIM_REASSIGN_DUMP (0). */
  ASSERT_EQ(def.reassign_mode, SIM_REASSIGN_DUMP);

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);

  SimConfig c = def;
  c.reassign_mode = 2;
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);
  ASSERT_NULL(s);

  c.reassign_mode = -1;
  s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);
  ASSERT_NULL(s);

  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 2. Worked example: DUMP vs DRAIN reassignment */
static void test_ext_worked_dump_vs_drain(void) {
  TEST_START("test_ext_worked_dump_vs_drain");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);

  /*
   * Hand derivation (DUMP mode):
   * n_buses = 3, FIXED_EQUAL -> targets [2, 1]:
   *   bus 0: route 0, pos 0 (stop 0)
   *   bus 1: route 0, pos 2 (stop 2)
   *   bus 2: route 1, pos 0 (stop 4)
   * t0 = 360 * 60 = 21600 s.
   * Inject {stop 2, 21600 s, 1 pax, ride 3}:
   *   bus 1 arrives stop 2 at 21600, boards pax (dest pos (2+3)%4 = 1 -> stop 1),
   *   departs at 21620, arrives stop 3 at 21720 (362.0 min).
   * After run_until(360.0), sim_request_move(s, 1, 1) sets pending_route = 1.
   * At 21720 s (stop 3): load 1 is dumped (forced_dumps = 1), entry to route 1
   * is min over 4..7 of 100*|3-s| = 100 s (stop 4), deadhead 21720 -> 21820 s.
   * At 363.7 min (21822 s): RUNNING on route 1 at stop 4.
   * At 364.0 min (21840 s): RUNNING on route 1, moves_applied = 1, deadhead = 100.
   */
  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 3;
  c.reassign_mode = SIM_REASSIGN_DUMP;

  Sim *s_dump = NULL;
  ASSERT_EQ(sim_create(net, &c, &s_dump), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s_dump, 2, 21600.0, 1, 3), STATUS_OK);
  ASSERT_EQ(sim_run_until(s_dump, 360.0), STATUS_OK);
  ASSERT_EQ(sim_request_move(s_dump, 1, 1), STATUS_OK);

  ASSERT_EQ(sim_run_until(s_dump, 362.0), STATUS_OK);
  SimBusInfo bi;
  ASSERT_EQ(sim_bus_info(s_dump, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_DEADHEAD);
  SimStats st;
  ASSERT_EQ(sim_stats(s_dump, &st), STATUS_OK);
  ASSERT_EQ(st.forced_dumps, 1);
  ASSERT_EQ(st.moves_applied, 1);
  ASSERT_APPROX(st.deadhead_s_total, 100.0, 1e-9);

  ASSERT_EQ(sim_run_until(s_dump, 363.7), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s_dump, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_RUNNING);
  ASSERT_EQ(bi.route, 1);
  ASSERT_EQ(bi.stop_idx, 4);

  ASSERT_EQ(sim_run_until(s_dump, 364.0), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s_dump, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_RUNNING);
  ASSERT_EQ(bi.route, 1);
  ASSERT_EQ(sim_stats(s_dump, &st), STATUS_OK);
  ASSERT_EQ(st.moves_applied, 1);
  ASSERT_APPROX(st.deadhead_s_total, 100.0, 1e-9);
  sim_destroy(s_dump);

  /*
   * Hand derivation (DRAIN mode):
   * Bus 1 arrives stop 3 at 21720 (load 1, no alight, no board), departs 21740.
   * Arrives stop 0 at 21740 + 300 = 22040, departs 22060.
   * At 366.0 min (21960 s) and 369.0 min (22140 s): RUNNING on route 0, load 1,
   * forced_dumps 0.
   * Arrives stop 1 at 22060 + 100 = 22160 s: pax alights -> load 0, move executes!
   * Entry from stop 1 to route 1 = stop 4, deadhead = 100 * |1 - 4| = 300 s,
   * deadhead 22160 -> 22460 s (374.33 min).
   * At 370.0 min (22200 s): DEADHEAD.
   * At 374.0 min (22440 s): still DEADHEAD.
   * At 374.5 min (22470 s): RUNNING on route 1 at stop 4.
   * At 375.0 min (22500 s): RUNNING on route 1; moves_applied 1, deadhead 300, forced_dumps 0.
   */
  c.reassign_mode = SIM_REASSIGN_DRAIN;
  Sim *s_drain = NULL;
  ASSERT_EQ(sim_create(net, &c, &s_drain), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s_drain, 2, 21600.0, 1, 3), STATUS_OK);
  ASSERT_EQ(sim_run_until(s_drain, 360.0), STATUS_OK);
  ASSERT_EQ(sim_request_move(s_drain, 1, 1), STATUS_OK);

  ASSERT_EQ(sim_run_until(s_drain, 366.0), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s_drain, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_RUNNING);
  ASSERT_EQ(bi.route, 0);
  ASSERT_EQ(bi.load, 1);
  ASSERT_EQ(sim_stats(s_drain, &st), STATUS_OK);
  ASSERT_EQ(st.forced_dumps, 0);

  ASSERT_EQ(sim_run_until(s_drain, 369.0), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s_drain, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_RUNNING);
  ASSERT_EQ(bi.route, 0);
  ASSERT_EQ(bi.load, 1);
  ASSERT_EQ(sim_stats(s_drain, &st), STATUS_OK);
  ASSERT_EQ(st.forced_dumps, 0);

  ASSERT_EQ(sim_run_until(s_drain, 370.0), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s_drain, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_DEADHEAD);
  ASSERT_EQ(sim_stats(s_drain, &st), STATUS_OK);
  ASSERT_EQ(st.forced_dumps, 0);

  ASSERT_EQ(sim_run_until(s_drain, 374.0), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s_drain, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_DEADHEAD);

  ASSERT_EQ(sim_run_until(s_drain, 374.5), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s_drain, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_RUNNING);
  ASSERT_EQ(bi.route, 1);
  ASSERT_EQ(bi.stop_idx, 4);

  ASSERT_EQ(sim_run_until(s_drain, 375.0), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s_drain, 1, &bi), STATUS_OK);
  ASSERT_EQ(bi.state, SIM_BUS_RUNNING);
  ASSERT_EQ(bi.route, 1);
  ASSERT_EQ(sim_stats(s_drain, &st), STATUS_OK);
  ASSERT_EQ(st.moves_applied, 1);
  ASSERT_APPROX(st.deadhead_s_total, 300.0, 1e-9);
  ASSERT_EQ(st.forced_dumps, 0);

  sim_destroy(s_drain);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 3. Error validation for sim_request_move */
static void test_ext_request_move_errors(void) {
  TEST_START("test_ext_request_move_errors");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);
  NetworkModel *net_nomat = build_line_network_opt(g, stops, 2, 0);
  ASSERT_NOT_NULL(net_nomat);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 3;
  c.n_reserve = 1; /* bus 0 on route 0, bus 1 on route 1, bus 2 reserve */

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  ASSERT_EQ(sim_request_move(NULL, 0, 1), STATUS_ERR_NULL);
  ASSERT_EQ(sim_request_move(s, -1, 1), STATUS_ERR_INVALID);
  ASSERT_EQ(sim_request_move(s, 3, 1), STATUS_ERR_INVALID);
  ASSERT_EQ(sim_request_move(s, 2, 0), STATUS_ERR_INVALID);  /* reserve */
  ASSERT_EQ(sim_request_move(s, 0, -2), STATUS_ERR_INVALID); /* out of range */
  ASSERT_EQ(sim_request_move(s, 0, 2), STATUS_ERR_INVALID);  /* out of range */
  ASSERT_EQ(sim_request_move(s, 0, 0), STATUS_ERR_INVALID);  /* same route */

  /* Breakdown bus 1 and try to move it */
  SimScenario sc = {375.0, SIM_SCEN_BREAKDOWN, 1};
  ASSERT_EQ(sim_schedule(s, &sc), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);
  ASSERT_EQ(sim_request_move(s, 1, 0), STATUS_ERR_INVALID);  /* broken */

  /* First request succeeds, second while pending fails */
  ASSERT_EQ(sim_request_move(s, 0, 1), STATUS_OK);
  ASSERT_EQ(sim_request_move(s, 0, -1), STATUS_ERR_INVALID);
  sim_destroy(s);

  /* Without distance matrix: to_route >= 0 rejected, -1 allowed */
  c.n_buses = 2;
  c.n_reserve = 0;
  ASSERT_EQ(sim_create(net_nomat, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_request_move(s, 0, 1), STATUS_ERR_INVALID);
  ASSERT_EQ(sim_request_move(s, 0, -1), STATUS_OK);
  sim_destroy(s);

  network_destroy(net_nomat);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 4. sim_set_policy and sim_force_decision */
static void test_ext_set_policy_and_force_decision(void) {
  TEST_START("test_ext_set_policy_and_force_decision");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  for (int i = 0; i < 4; i++) {
    stops[i].type = STOP_OFFICE;
    stops[i].scale = 1.8;
  }
  for (int i = 4; i < 8; i++) {
    stops[i].type = STOP_RESIDENTIAL;
    stops[i].scale = 0.6;
  }
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);
  NetworkModel *net_nomat = build_line_network_opt(g, stops, 2, 0);
  ASSERT_NOT_NULL(net_nomat);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 4;
  c.start_min = 480.0;
  c.end_min = 600.0;
  c.policy = SIM_POLICY_FIXED_EQUAL;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_policy(s), SIM_POLICY_FIXED_EQUAL);

  size_t moves = 99;
  ASSERT_EQ(sim_force_decision(s, &moves), STATUS_ERR_INVALID);
  ASSERT_EQ(sim_set_policy(NULL, SIM_POLICY_GREEDY_MARGINAL, NULL), STATUS_ERR_NULL);
  ASSERT_EQ(sim_set_policy(s, (SimPolicy)99, NULL), STATUS_ERR_INVALID);

  ASSERT_EQ(sim_set_policy(s, SIM_POLICY_GREEDY_MARGINAL, NULL), STATUS_OK);
  ASSERT_EQ(sim_policy(s), SIM_POLICY_GREEDY_MARGINAL);

  /* Derive expected targets [3, 1] by brute force over demand_expected at hour 8 */
  double d0 = 0.0, d1 = 0.0;
  for (int i = 0; i < 4; i++) {
    d0 += demand_expected(stops[i].type, stops[i].scale, 8, DAY_WEEKDAY, 0, 0);
  }
  for (int i = 4; i < 8; i++) {
    d1 += demand_expected(stops[i].type, stops[i].scale, 8, DAY_WEEKDAY, 0, 0);
  }
  double best_obj = 1e30;
  int exp_c0 = 0, exp_c1 = 0;
  for (int c0 = 1; c0 <= 3; c0++) {
    int c1 = 4 - c0;
    double obj =
        (d0 * 680.0) / (2.0 * (double)c0) + (d1 * 680.0) / (2.0 * (double)c1);
    if (obj < best_obj) {
      best_obj = obj;
      exp_c0 = c0;
      exp_c1 = c1;
    }
  }
  ASSERT_EQ(exp_c0, 3);
  ASSERT_EQ(exp_c1, 1);

  ASSERT_EQ(sim_force_decision(s, &moves), STATUS_OK);
  ASSERT_EQ(moves, 1);
  ASSERT_EQ(sim_force_decision(s, NULL), STATUS_OK);

  /* Advance past first arrival + deadhead so moved bus reaches route 0 */
  ASSERT_EQ(sim_run_until(s, 495.0), STATUS_OK);
  ASSERT_EQ(sim_route_bus_count(s, 0), (size_t)exp_c0);
  ASSERT_EQ(sim_route_bus_count(s, 1), (size_t)exp_c1);

  /* Switching back to FIXED_EQUAL stops controller */
  ASSERT_EQ(sim_set_policy(s, SIM_POLICY_FIXED_EQUAL, NULL), STATUS_OK);
  ASSERT_EQ(sim_policy(s), SIM_POLICY_FIXED_EQUAL);
  ASSERT_EQ(sim_force_decision(s, &moves), STATUS_ERR_INVALID);
  sim_destroy(s);

  /* Network without matrix rejects GREEDY_* in sim_set_policy */
  c.n_buses = 2;
  ASSERT_EQ(sim_create(net_nomat, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_set_policy(s, SIM_POLICY_GREEDY_MARGINAL, NULL), STATUS_ERR_INVALID);
  ASSERT_EQ(sim_set_policy(s, SIM_POLICY_GREEDY_SPEC, NULL), STATUS_ERR_INVALID);
  sim_destroy(s);

  network_destroy(net_nomat);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 5. Rain and event getters */
static void test_ext_rain_and_event_getters(void) {
  TEST_START("test_ext_rain_and_event_getters");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  ASSERT_EQ(sim_rain_active(NULL), 0);
  ASSERT_EQ(sim_event_active(NULL, 0), 0);
  ASSERT_EQ(sim_event_active(s, 99), 0);

  SimScenario sc_rain = {375.0, SIM_SCEN_RAIN_ON, 0};
  SimScenario sc_ev_on = {375.0, SIM_SCEN_EVENT_ON, 2};
  SimScenario sc_ev_off = {390.0, SIM_SCEN_EVENT_OFF, 2};
  ASSERT_EQ(sim_schedule(s, &sc_rain), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc_ev_on), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc_ev_off), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, 374.0), STATUS_OK);
  ASSERT_EQ(sim_rain_active(s), 0);
  ASSERT_EQ(sim_event_active(s, 2), 0);
  ASSERT_EQ(sim_event_active(s, 3), 0);

  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);
  ASSERT_EQ(sim_rain_active(s), 1);
  ASSERT_EQ(sim_event_active(s, 2), 1);
  ASSERT_EQ(sim_event_active(s, 3), 0);

  ASSERT_EQ(sim_run_until(s, 390.0), STATUS_OK);
  ASSERT_EQ(sim_event_active(s, 2), 0);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 6. sim_stop_hour_arrivals */
static void test_ext_stop_hour_arrivals(void) {
  TEST_START("test_ext_stop_hour_arrivals");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  ASSERT_EQ(sim_stop_hour_arrivals(NULL, 0, 6), 0);
  ASSERT_EQ(sim_stop_hour_arrivals(s, 99, 6), 0);
  ASSERT_EQ(sim_stop_hour_arrivals(s, 0, -1), 0);
  ASSERT_EQ(sim_stop_hour_arrivals(s, 0, 24), 0);

  /* Inject 5 pax at stop 1, t0 + 10 = 21610 s -> floor(21610/3600) = hour 6 */
  ASSERT_EQ(sim_inject_arrivals(s, 1, 21610.0, 5, 1), STATUS_OK);
  ASSERT_EQ(sim_stop_hour_arrivals(s, 1, 6), 5);
  ASSERT_EQ(sim_stop_hour_arrivals(s, 1, 7), 0);
  ASSERT_EQ(sim_stop_hour_arrivals(s, 2, 6), 0);
  sim_destroy(s);

  /* Random arrivals on, warmup 0: sum over stops and hours == SimStats.arrived */
  c.arrivals_enabled = 1;
  c.warmup_min = 0.0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_run_to_end(s), STATUS_OK);

  SimStats st;
  ASSERT_EQ(sim_stats(s, &st), STATUS_OK);
  size_t total_gen = 0;
  for (size_t stop = 0; stop < 8; stop++) {
    for (int h = 0; h < 24; h++) {
      total_gen += sim_stop_hour_arrivals(s, stop, h);
    }
  }
  ASSERT_TRUE(total_gen > 0);
  ASSERT_EQ(total_gen, st.arrived);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 7. sim_bus_motion across dwelling, running leg, deadhead, reserve, broken */
static void test_ext_bus_motion(void) {
  TEST_START("test_ext_bus_motion");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 4;
  c.n_reserve = 1; /* buses 0,1 on route 0; bus 2 on route 1; bus 3 reserve */
  c.depot_stop = 5;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  SimBusMotion m;
  ASSERT_EQ(sim_bus_motion(NULL, 0, &m), STATUS_ERR_NULL);
  ASSERT_EQ(sim_bus_motion(s, 0, NULL), STATUS_ERR_NULL);
  ASSERT_EQ(sim_bus_motion(s, 4, &m), STATUS_ERR_INVALID);

  /* Reserve bus 3 at depot_stop 5 */
  ASSERT_EQ(sim_bus_motion(s, 3, &m), STATUS_OK);
  ASSERT_EQ(m.moving, 0);
  ASSERT_EQ(m.from_stop, 5);
  ASSERT_EQ(m.to_stop, 5);

  /*
   * Hand derivation for Bus 0 (route 0, pos 0 = stop 0):
   * 21610 s (360 + 10/60 min): dwelling at stop 0 until 21620 s.
   * 21660 s (361.0 min): moving on leg 0 (0 -> 1), depart 21620, arrive 21720.
   * 21720 s (362.0 min): arrived at stop 1, dwelling until 21740 s.
   */
  ASSERT_EQ(sim_run_until(s, 360.0 + 10.0 / 60.0), STATUS_OK);
  ASSERT_EQ(sim_bus_motion(s, 0, &m), STATUS_OK);
  ASSERT_EQ(m.moving, 0);
  ASSERT_EQ(m.route, 0);
  ASSERT_EQ(m.leg_index, -1);
  ASSERT_EQ(m.from_stop, 0);
  ASSERT_EQ(m.to_stop, 0);
  ASSERT_APPROX(m.depart_s, 21620.0, 1e-9);
  ASSERT_APPROX(m.arrive_s, 21620.0, 1e-9);

  ASSERT_EQ(sim_run_until(s, 361.0), STATUS_OK);
  ASSERT_EQ(sim_bus_motion(s, 0, &m), STATUS_OK);
  ASSERT_EQ(m.moving, 1);
  ASSERT_EQ(m.route, 0);
  ASSERT_EQ(m.leg_index, 0);
  ASSERT_EQ(m.from_stop, 0);
  ASSERT_EQ(m.to_stop, 1);
  ASSERT_APPROX(m.depart_s, 21620.0, 1e-9);
  ASSERT_APPROX(m.arrive_s, 21720.0, 1e-9);

  ASSERT_EQ(sim_run_until(s, 362.0), STATUS_OK);
  ASSERT_EQ(sim_bus_motion(s, 0, &m), STATUS_OK);
  ASSERT_EQ(m.moving, 0);
  ASSERT_EQ(m.from_stop, 1);
  ASSERT_EQ(m.to_stop, 1);
  ASSERT_APPROX(m.depart_s, 21740.0, 1e-9);
  ASSERT_APPROX(m.arrive_s, 21740.0, 1e-9);

  /* Broken bus 2 */
  SimScenario sc = {375.0, SIM_SCEN_BREAKDOWN, 2};
  ASSERT_EQ(sim_schedule(s, &sc), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);
  ASSERT_EQ(sim_bus_motion(s, 2, &m), STATUS_OK);
  ASSERT_EQ(m.moving, 0);
  sim_destroy(s);

  /* DEADHEAD bus (Test 2 DUMP at 21740 s = 362.0 + 20/60 min) */
  get_scripted_config(&c);
  c.n_buses = 3;
  c.reassign_mode = SIM_REASSIGN_DUMP;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 360.0), STATUS_OK);
  ASSERT_EQ(sim_request_move(s, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 362.0 + 20.0 / 60.0), STATUS_OK);
  ASSERT_EQ(sim_bus_motion(s, 1, &m), STATUS_OK);
  ASSERT_EQ(m.moving, 1);
  ASSERT_EQ(m.route, 1);
  ASSERT_EQ(m.leg_index, -1);
  ASSERT_EQ(m.from_stop, 3);
  ASSERT_EQ(m.to_stop, 4);
  ASSERT_APPROX(m.depart_s, 21720.0, 1e-9);
  ASSERT_APPROX(m.arrive_s, 21820.0, 1e-9);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

typedef struct {
  int count;
  int last_bus;
  int last_route;
  double last_start_s;
  double last_end_s;
  int last_pax;
  void *expected_self;
  int ctx_matched;
} LoopRec;

static void on_loop_cb(void *ctx, int bus, int route, double start_s,
                       double end_s, int pax_boarded) {
  LoopRec *r = (LoopRec *)ctx;
  if (bus != 0) return; /* filter for bus 0 in Test 8 */
  r->count++;
  r->last_bus = bus;
  r->last_route = route;
  r->last_start_s = start_s;
  r->last_end_s = end_s;
  r->last_pax = pax_boarded;
  if (ctx == r->expected_self) {
    r->ctx_matched++;
  }
}

/* 8. Loop observer callbacks, breakdown discard, and removal */
static void test_ext_loop_observer(void) {
  TEST_START("test_ext_loop_observer");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  LoopRec rec;
  memset(&rec, 0, sizeof(rec));
  rec.expected_self = &rec;

  ASSERT_EQ(sim_set_loop_observer(NULL, on_loop_cb, &rec), STATUS_ERR_NULL);
  ASSERT_EQ(sim_set_loop_observer(s, on_loop_cb, &rec), STATUS_OK);

  /*
   * Hand derivation:
   * Bus 0 on route 0 (m = 4, cycle = 680 s):
   *   Arrives stop 0 at 21600 (arr 1), stop 1 at 21720 (arr 2, boards 2 pax),
   *   stop 2 at 21840 (arr 3), stop 3 at 21960 (arr 4),
   *   stop 0 at 22280 s (arr 5 = m+1) -> callback(0, 0, 21600, 22280, 2).
   *   Second loop closes at 22280 + 680 = 22960 s -> callback(0, 0, 22280, 22960, 0).
   */
  ASSERT_EQ(sim_inject_arrivals(s, 1, 21610.0, 2, 1), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, 371.0), STATUS_OK); /* 22260 s */
  ASSERT_EQ(rec.count, 0);

  ASSERT_EQ(sim_run_until(s, 372.0), STATUS_OK); /* 22320 s */
  ASSERT_EQ(rec.count, 1);
  ASSERT_EQ(rec.ctx_matched, 1);
  ASSERT_EQ(rec.last_bus, 0);
  ASSERT_EQ(rec.last_route, 0);
  ASSERT_APPROX(rec.last_start_s, 21600.0, 1e-9);
  ASSERT_APPROX(rec.last_end_s, 22280.0, 1e-9);
  ASSERT_EQ(rec.last_pax, 2);

  ASSERT_EQ(sim_run_until(s, 383.0), STATUS_OK); /* 22980 s */
  ASSERT_EQ(rec.count, 2);
  ASSERT_APPROX(rec.last_start_s, 22280.0, 1e-9);
  ASSERT_APPROX(rec.last_end_s, 22960.0, 1e-9);
  ASSERT_EQ(rec.last_pax, 0);

  /* Breakdown mid-loop at 390.0 min discards partial loop (no callback by 405.0) */
  SimScenario sc = {390.0, SIM_SCEN_BREAKDOWN, 0};
  ASSERT_EQ(sim_schedule(s, &sc), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 405.0), STATUS_OK);
  ASSERT_EQ(rec.count, 2);

  /* Remove observer with fn = NULL */
  ASSERT_EQ(sim_set_loop_observer(s, NULL, NULL), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 430.0), STATUS_OK);
  ASSERT_EQ(rec.count, 2);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 9. DRAIN mode with the controller (matching test_sim_dynamic_reassignment) */
static void test_ext_controller_drain(void) {
  TEST_START("test_ext_controller_drain");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  for (int i = 0; i < 8; i++) {
    stops[i].type = STOP_RESIDENTIAL;
    stops[i].scale = 1.0;
  }
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 6;
  c.policy = SIM_POLICY_GREEDY_MARGINAL;
  c.start_min = 480.0;
  c.end_min = 600.0;
  c.realloc_interval_min = 30.0;
  c.reassign_mode = SIM_REASSIGN_DRAIN;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  ASSERT_EQ(sim_inject_arrivals(s, 4, 29470.0, 5, 3), STATUS_OK);
  SimScenario sc1 = {495.0, SIM_SCEN_BREAKDOWN, 3};
  SimScenario sc2 = {495.0, SIM_SCEN_BREAKDOWN, 4};
  SimScenario sc3 = {495.0, SIM_SCEN_BREAKDOWN, 5};
  ASSERT_EQ(sim_schedule(s, &sc1), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc2), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc3), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, 495.0), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 3, 30250.0, 4, 2), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 3, 30350.0, 4, 2), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 3, 30500.0, 4, 2), STATUS_OK);

  /*
   * Hand derivation:
   * At 510 min (30600 s), buses 0, 1, 2 each boarded 4 pax at stop 3 (ride 2,
   * dest pos (3+2)%4 = 1 -> stop 1). With equal load 4, fleet_plan_moves picks
   * smallest bus_id (bus 0) to move from route 0 to route 1.
   * Bus 0 passes stop 0 at 30840 s (load 4), then arrives stop 1 at 30960 s
   * (516 min), where all 4 pax alight (load 0) and the move executes at stop 1.
   * Deadhead from stop 1 to route 1 = network_entry_time_s(net, 1, 1, &entry) = 300.0 s.
   */
  ASSERT_EQ(sim_run_until(s, 540.0), STATUS_OK);
  SimStats st;
  ASSERT_EQ(sim_stats(s, &st), STATUS_OK);
  ASSERT_EQ(st.forced_dumps, 0);
  ASSERT_EQ(st.moves_applied, 1);

  int exp_entry = -1;
  double exp_dh = network_entry_time_s(net, 1, 1, &exp_entry);
  ASSERT_EQ(exp_entry, 4);
  ASSERT_APPROX(st.deadhead_s_total, exp_dh, 1e-9);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 10. Memory cleanup mid-run with pending DRAIN move and observer set */
static void test_ext_memory_mid_run(void) {
  TEST_START("test_ext_memory_mid_run");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network_opt(g, stops, 2, 1);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 3;
  c.reassign_mode = SIM_REASSIGN_DRAIN;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  LoopRec rec;
  memset(&rec, 0, sizeof(rec));
  ASSERT_EQ(sim_set_loop_observer(s, on_loop_cb, &rec), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 2, 21600.0, 2, 3), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 360.0), STATUS_OK);
  ASSERT_EQ(sim_request_move(s, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 366.0), STATUS_OK);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

int main(void) {
  RUN_TEST(test_ext_reassign_config_validation);
  RUN_TEST(test_ext_worked_dump_vs_drain);
  RUN_TEST(test_ext_request_move_errors);
  RUN_TEST(test_ext_set_policy_and_force_decision);
  RUN_TEST(test_ext_rain_and_event_getters);
  RUN_TEST(test_ext_stop_hour_arrivals);
  RUN_TEST(test_ext_bus_motion);
  RUN_TEST(test_ext_loop_observer);
  RUN_TEST(test_ext_controller_drain);
  RUN_TEST(test_ext_memory_mid_run);

  return test_summary();
}

