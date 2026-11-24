/**
 * @file test_sim.c
 * @brief Unit and integration tests for the discrete-event bus simulation
 * engine.
 */

#include "anomaly.h"
#include "fleet_alloc.h"
#include "graph.h"
#include "network.h"
#include "reroute.h"
#include "sim.h"
#include "traffic_sim.h"
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

/* --- Fixtures --- */

static Graph *build_line_graph_fixture(DemandStop stops_out[8]) {
  Graph *g = graph_create();
  if (!g)
    return NULL;

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

static NetworkModel *build_line_network(Graph *g, DemandStop *stops,
                                        size_t n_routes) {
  NetworkConfig ncfg;
  network_config_default(&ncfg);
  ncfg.n_routes = n_routes;
  ncfg.dwell_s_per_stop = 20.0;
  ncfg.build_matrix = 1;
  NetworkModel *net = NULL;
  Status st = network_build(g, stops, 8, &ncfg, &net);
  if (st != STATUS_OK)
    return NULL;
  return net;
}

/*
 * Hand-derived timeline for Bus 0 on Route 0 (stops 0, 1, 2, 3):
 * Leg times: 0->1: 100s, 1->2: 100s, 2->3: 100s, 3->0: 300s.
 * Dwell time: 20s.
 * Cycle time: (100+100+100+300) + 4 * 20 = 600 + 80 = 680s.
 *
 * t0 = 360 * 60 = 21600s:
 * ARRIVE stop 0 at 21600s
 * DEPART stop 0 at 21620s
 * ARRIVE stop 1 at 21720s
 * DEPART stop 1 at 21740s
 * ARRIVE stop 2 at 21840s (364 min)
 * DEPART stop 2 at 21860s
 * ARRIVE stop 3 at 21960s (366 min)
 * DEPART stop 3 at 21980s
 * ARRIVE stop 0 at 22280s (second loop)
 * ARRIVE stop 1 at 22400s
 * ARRIVE stop 2 at 22520s (375.333 min)
 */
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
}

/* 1. Config and errors */
static void test_sim_config_and_errors(void) {
  TEST_START("test_sim_config_and_errors");

  SimConfig def;
  sim_config_default(&def);
  ASSERT_EQ(def.seed, 42ULL);
  ASSERT_APPROX(def.start_min, 360.0, 1e-9);
  ASSERT_APPROX(def.end_min, 1320.0, 1e-9);
  ASSERT_APPROX(def.slice_min, 15.0, 1e-9);
  ASSERT_EQ(def.n_buses, 36);
  ASSERT_EQ(def.policy, SIM_POLICY_FIXED_EQUAL);

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  Sim *s = NULL;
  /* NULL args */
  ASSERT_EQ(sim_create(NULL, &def, &s), STATUS_ERR_NULL);
  ASSERT_EQ(sim_create(net, &def, NULL), STATUS_ERR_NULL);

  /* Bad slice values */
  SimConfig c = def;
  c.slice_min = 7.0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);
  ASSERT_NULL(s);

  /* Start not multiple of slice */
  c = def;
  c.start_min = 362.0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* End <= start */
  c = def;
  c.end_min = 300.0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Warmup too large */
  c = def;
  c.warmup_min = 1000.0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Negative dwell */
  c = def;
  c.dwell_s = -5.0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Capacity 0 */
  c = def;
  c.bus_capacity = 0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Noise >= 0.5 */
  c = def;
  c.travel_noise = 0.5;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Insufficient buses */
  c = def;
  c.n_buses = 2;
  c.n_reserve = 1; /* 2-1 = 1 < 2 routes */
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Bad depot stop */
  c = def;
  c.depot_stop = 999;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Proportional without weights */
  c = def;
  c.policy = SIM_POLICY_FIXED_PROPORTIONAL;
  c.prop_weights = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Hysteresis >= 1.0 */
  c = def;
  c.realloc_hysteresis = 1.0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Anomaly dispatch without detect */
  c = def;
  c.anomaly_detect = 0;
  c.anomaly_dispatch = 1;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);

  /* Network built without matrix for dynamic policy */
  NetworkConfig ncfg_nomat;
  network_config_default(&ncfg_nomat);
  ncfg_nomat.n_routes = 2;
  ncfg_nomat.dwell_s_per_stop = 20.0;
  ncfg_nomat.build_matrix = 0;
  NetworkModel *net_nomat = NULL;
  ASSERT_EQ(network_build(g, stops, 8, &ncfg_nomat, &net_nomat), STATUS_OK);
  c = def;
  c.policy = SIM_POLICY_GREEDY_MARGINAL;
  ASSERT_EQ(sim_create(net_nomat, &c, &s), STATUS_ERR_INVALID);
  network_destroy(net_nomat);

  /* Destroy NULL is safe */
  sim_destroy(NULL);

  /* Create valid sim and test method errors */
  get_scripted_config(&c);
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_NOT_NULL(s);

  /* Schedule errors */
  SimScenario sc;
  sc.time_min = 350.0; /* in the past */
  sc.kind = SIM_SCEN_RAIN_ON;
  sc.arg = 0;
  ASSERT_EQ(sim_schedule(s, &sc), STATUS_ERR_INVALID);
  sc.time_min = 362.0; /* not multiple of slice */
  ASSERT_EQ(sim_schedule(s, &sc), STATUS_ERR_INVALID);
  sc.time_min = 1400.0; /* >= end */
  ASSERT_EQ(sim_schedule(s, &sc), STATUS_ERR_INVALID);
  sc.time_min = 375.0;
  sc.kind = SIM_SCEN_EVENT_ON;
  sc.arg = 999; /* bad stop */
  ASSERT_EQ(sim_schedule(s, &sc), STATUS_ERR_INVALID);

  /* Inject arrivals errors */
  ASSERT_EQ(sim_inject_arrivals(s, 0, 21500.0, 1, 1),
            STATUS_ERR_INVALID); /* before now */
  ASSERT_EQ(sim_inject_arrivals(s, 0, 22600.0, 1, 1),
            STATUS_ERR_INVALID); /* after current slice (slice ends at 22500) */
  ASSERT_EQ(sim_inject_arrivals(s, 0, 21610.0, 0, 1),
            STATUS_ERR_INVALID); /* count 0 */
  ASSERT_EQ(sim_inject_arrivals(s, 0, 21610.0, 1, 99),
            STATUS_ERR_INVALID); /* ride out of range */
  ASSERT_EQ(sim_inject_arrivals(s, 999, 21610.0, 1, 1),
            STATUS_ERR_INVALID); /* bad stop */

  /* Run until errors */
  ASSERT_EQ(sim_run_until(s, 350.0), STATUS_ERR_INVALID);  /* backwards */
  ASSERT_EQ(sim_run_until(s, 1400.0), STATUS_ERR_INVALID); /* beyond end */

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 2. Scripted timeline */
static void test_sim_scripted_timeline(void) {
  TEST_START("test_sim_scripted_timeline");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /* Injections */
  ASSERT_EQ(sim_inject_arrivals(s, 2, 21610.0, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 2, 21850.0, 1, 1), STATUS_OK);

  /* Run to 363 min (21780 s): bus not reached stop 2 yet */
  ASSERT_EQ(sim_run_until(s, 363.0), STATUS_OK);
  SimStats st;
  sim_stats(s, &st);
  ASSERT_EQ(st.boarded, 0);
  SimBusInfo binfo;
  ASSERT_EQ(sim_bus_info(s, 0, &binfo), STATUS_OK);
  ASSERT_EQ(binfo.state, SIM_BUS_RUNNING);
  ASSERT_EQ(binfo.load, 0);

  /* Run to 364 min (21840 s): first arrival at stop 2, boarded 1 */
  ASSERT_EQ(sim_run_until(s, 364.0), STATUS_OK);
  sim_stats(s, &st);
  ASSERT_EQ(st.boarded, 1);
  ASSERT_APPROX(st.wait_sum_s, 230.0, 1e-9); /* 21840 - 21610 = 230 */
  sim_bus_info(s, 0, &binfo);
  ASSERT_EQ(binfo.load, 1);

  /* Run to 365 min (21900 s): in transit to stop 3, load 1 */
  ASSERT_EQ(sim_run_until(s, 365.0), STATUS_OK);
  sim_bus_info(s, 0, &binfo);
  ASSERT_EQ(binfo.load, 1);

  /* Run to 366 min (21960 s): reached stop 3, alights, load 0 */
  ASSERT_EQ(sim_run_until(s, 366.0), STATUS_OK);
  sim_bus_info(s, 0, &binfo);
  ASSERT_EQ(binfo.load, 0);

  /* Run to 376 min (22560 s): second arrival at stop 2 was at 22520 s */
  ASSERT_EQ(sim_run_until(s, 376.0), STATUS_OK);
  sim_stats(s, &st);
  ASSERT_EQ(st.boarded, 2);
  /* second passenger wait: 22520 - 21850 = 670 s */
  ASSERT_APPROX(st.wait_sum_s, 900.0, 1e-9);
  ASSERT_APPROX(st.wait_max_s, 670.0, 1e-9);
  ASSERT_APPROX(st.mean_wait_boarded_s, 450.0, 1e-9);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 3. Capacity */
static void test_sim_capacity(void) {
  TEST_START("test_sim_capacity");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.bus_capacity = 2;
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /* 5 pax at stop 1 at 21601 s */
  ASSERT_EQ(sim_inject_arrivals(s, 1, 21601.0, 5, 1), STATUS_OK);

  /* Run to 385 min (23100 s):
   * Loop 1 at stop 1: 21720 s -> boards 2 pax (wait 21720 - 21601 = 119 each).
   * Remaining 3. full_bus = 1. Loop 2 at stop 1: 21720 + 680 = 22400 s ->
   * boards 2 pax (wait 22400 - 21601 = 799 each). Remaining 1. full_bus = 2.
   * Loop 3 at stop 1: 22400 + 680 = 23080 s -> boards 1 pax (wait 23080 - 21601
   * = 1479). Remaining 0. Total wait = 2*119 + 2*799 + 1479 = 238 + 1598 + 1479
   * = 3315.
   */
  ASSERT_EQ(sim_run_until(s, 385.0), STATUS_OK);
  SimStats st;
  sim_stats(s, &st);
  ASSERT_EQ(st.boarded, 5);
  ASSERT_APPROX(st.wait_sum_s, 3315.0, 1e-9);
  ASSERT_EQ(st.full_bus_events, 2);
  ASSERT_EQ(st.still_waiting, 0);
  ASSERT_EQ(st.forced_dumps, 0);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 4. Warm-up */
static void test_sim_warmup(void) {
  TEST_START("test_sim_warmup");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.warmup_min = 5.0; /* warmup_end = (360 + 5)*60 = 21900 s */
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  ASSERT_EQ(sim_inject_arrivals(s, 2, 21610.0, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 2, 21901.0, 1, 1), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, 376.0), STATUS_OK);
  SimStats st;
  sim_stats(s, &st);
  /* Only the second passenger is counted: wait = 22520 - 21901 = 619 s */
  ASSERT_EQ(st.boarded, 1);
  ASSERT_APPROX(st.wait_sum_s, 619.0, 1e-9);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 5. Censoring */
static void test_sim_censoring(void) {
  TEST_START("test_sim_censoring");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.start_min = 360.0;
  c.end_min = 370.0;
  c.slice_min = 10.0;
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  ASSERT_EQ(sim_inject_arrivals(s, 2, 21610.0, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 0, 21601.0, 1, 1), STATUS_OK);

  ASSERT_EQ(sim_run_to_end(s), STATUS_OK);
  ASSERT_TRUE(sim_finished(s));

  SimStats st;
  sim_stats(s, &st);
  ASSERT_EQ(st.boarded, 1);
  ASSERT_APPROX(st.wait_sum_s, 230.0, 1e-9);
  ASSERT_EQ(st.still_waiting, 1);
  ASSERT_APPROX(st.mean_wait_boarded_s, 230.0, 1e-9);
  /* Stop 0 passenger censored wait = 22200 - 21601 = 599.0 s */
  ASSERT_APPROX(st.mean_wait_incl_censored_s, 414.5,
                1e-9); /* (230 + 599)/2 = 414.5 */

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 6. Percentiles */
static void test_sim_percentiles(void) {
  TEST_START("test_sim_percentiles");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  SimStats st0;
  sim_stats(s, &st0);
  ASSERT_APPROX(st0.p50_s, -1.0, 1e-9);
  ASSERT_APPROX(st0.p95_s, -1.0, 1e-9);

  /*
   * We want 5 waits: 30, 30, 90, 200, 400 s.
   * Bus arrives at stop 2 at 21840 s.
   * arrival_s = 21840 - wait:
   * 21810 -> wait 30
   * 21810 -> wait 30
   * 21750 -> wait 90
   * 21640 -> wait 200
   * 21440 is before start, so inject at stop 1 (arrival at 22400):
   * 22400 - 400 = 22000 s (in slice [21600, 22500)).
   */
  ASSERT_EQ(sim_inject_arrivals(s, 2, 21810.0, 2, 1), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 2, 21750.0, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 2, 21640.0, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 1, 22000.0, 1, 1), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);
  SimStats st;
  sim_stats(s, &st);
  ASSERT_EQ(st.boarded, 5);

  /*
   * Sorted waits: 30, 30, 90, 200, 400.
   * Buckets (30s width):
   * 30 in [30, 60) -> upper edge 60.
   * 90 in [90, 120) -> upper edge 120.
   * 200 in [180, 210) -> upper edge 210.
   * 400 in [390, 420) -> upper edge 420.
   * p50 (ceil(0.50 * 5) = 3rd element = 90) -> upper edge 120.
   * p95 (ceil(0.95 * 5) = 5th element = 400) -> upper edge 420.
   */
  ASSERT_APPROX(st.p50_s, 120.0, 1e-9);
  ASSERT_APPROX(st.p95_s, 420.0, 1e-9);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 7. Route stats */
static void test_sim_route_stats(void) {
  TEST_START("test_sim_route_stats");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /* Inject passenger at stop 6 (Route 1) */
  ASSERT_EQ(sim_inject_arrivals(s, 6, 21610.0, 1, 1), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);

  SimRouteStats r0, r1;
  ASSERT_EQ(sim_route_stats(s, 0, &r0), STATUS_OK);
  ASSERT_EQ(sim_route_stats(s, 1, &r1), STATUS_OK);

  ASSERT_EQ(r0.boarded, 0);
  ASSERT_EQ(r1.boarded, 1);
  ASSERT_TRUE(r1.wait_sum_s > 0.0);

  SimStats st;
  sim_stats(s, &st);
  ASSERT_EQ(st.boarded, r0.boarded + r1.boarded);
  ASSERT_APPROX(st.wait_sum_s, r0.wait_sum_s + r1.wait_sum_s, 1e-9);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 8. Dwell with passengers */
static void test_sim_dwell_with_passengers(void) {
  TEST_START("test_sim_dwell_with_passengers");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.dwell_s = 20.0;
  c.per_pax_s = 2.0;
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /*
   * Bus 0 at stop 0: boards 1 with ride 1 at 21600.
   * Leaves stop 0 at 21600 + 20 + 2*1 = 21622 s.
   * Arrives at stop 1 at 21622 + 100 = 21722 s.
   * At stop 1: 1 alights, 3 board -> dwell = 20 + 2 * (1 + 3) = 28 s.
   * Departs stop 1 at 21722 + 28 = 21750 s.
   * Arrives at stop 2 at 21750 + 100 = 21850 s.
   */
  ASSERT_EQ(sim_inject_arrivals(s, 0, 21600.0, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 1, 21605.0, 3, 1), STATUS_OK);

  /* Run to 21749 s (362.483 min) -> still at stop 1 */
  ASSERT_EQ(sim_run_until(s, 21749.0 / 60.0), STATUS_OK);
  SimBusInfo bi;
  sim_bus_info(s, 0, &bi);
  ASSERT_EQ(bi.stop_idx, 1);
  ASSERT_EQ(bi.load, 3);

  /* Run to 21850 s (364.1666 min) -> arrives at stop 2 */
  ASSERT_EQ(sim_run_until(s, 21850.0 / 60.0), STATUS_OK);
  sim_bus_info(s, 0, &bi);
  ASSERT_EQ(bi.stop_idx, 2);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 9. Anomaly detection and dispatch */
static void test_sim_anomaly_detection_and_dispatch(void) {
  TEST_START("test_sim_anomaly_detection_and_dispatch");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  for (int i = 0; i < 8; i++) {
    stops[i].type = STOP_RESIDENTIAL;
    stops[i].scale = 1.0;
  }
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 3;
  c.n_reserve = 1;
  c.depot_stop = 5;
  c.anomaly_detect = 1;
  c.anomaly_dispatch = 1;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /* Inject 40 pax at stop 1 at 21660 s */
  ASSERT_EQ(sim_inject_arrivals(s, 1, 21660.0, 40, 1), STATUS_OK);

  /* Run to 375 min (22500 s, end of slice 1) */
  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);

  ASSERT_EQ(sim_flag_count(s), 1);
  AnomalyFlag fl;
  ASSERT_EQ(sim_flag(s, 0, &fl), STATUS_OK);
  ASSERT_EQ(fl.stop, 1);
  ASSERT_EQ(fl.observed, 40);
  ASSERT_TRUE(fl.z_score > 3.0);

  SimStats st;
  sim_stats(s, &st);
  ASSERT_EQ(st.anomaly_flags, 1);
  ASSERT_EQ(st.emergency_dispatches, 1);

  /* Bus 2 was reserve at depot_stop 5; route 0 closest stop to 5 is stop 3
   * (deadhead = 200s) */
  SimBusInfo bi2;
  sim_bus_info(s, 2, &bi2);
  ASSERT_EQ(bi2.state, SIM_BUS_DEADHEAD);
  ASSERT_EQ(bi2.route, 0);
  ASSERT_EQ(bi2.stop_idx, 3);

  /* Inject second burst within 60 min cooldown at 22560 s before advancing past
   * it */
  ASSERT_EQ(sim_inject_arrivals(s, 1, 22560.0, 40, 1), STATUS_OK);

  /* Run to 378.5 min (22710 s): bus 2 has arrived at entry stop 3, is RUNNING
   * at stop 3 */
  ASSERT_EQ(sim_run_until(s, 378.5), STATUS_OK);
  sim_bus_info(s, 2, &bi2);
  ASSERT_EQ(bi2.state, SIM_BUS_RUNNING);
  ASSERT_EQ(bi2.route, 0);
  ASSERT_EQ(bi2.stop_idx, 3);

  /*
   * Run to 380 min: bus 2 departed stop 3 at 22720 s and is RUNNING on route 0.
   * On leg 3->0 (travel time 300 s), it is heading to stop 0.
   */
  ASSERT_EQ(sim_run_until(s, 380.0), STATUS_OK);
  sim_bus_info(s, 2, &bi2);
  ASSERT_EQ(bi2.state, SIM_BUS_RUNNING);
  ASSERT_EQ(bi2.route, 0);
  ASSERT_APPROX(sim_first_flag_min(s, 1), 375.0, 1e-9);

  /* Run to 390 min: second burst at 22560 s is within 60 min cooldown -> NOT
   * flagged again */
  ASSERT_EQ(sim_run_until(s, 390.0), STATUS_OK);
  ASSERT_EQ(sim_flag_count(s), 1);

  sim_destroy(s);

  /* Variant with anomaly_dispatch = 0 */
  c.anomaly_dispatch = 0;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 1, 21660.0, 40, 1), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);
  ASSERT_EQ(sim_flag_count(s), 1);
  sim_stats(s, &st);
  ASSERT_EQ(st.emergency_dispatches, 0);
  sim_bus_info(s, 2, &bi2);
  ASSERT_EQ(bi2.state, SIM_BUS_RESERVE);
  sim_destroy(s);

  /* Variant with no reserve buses */
  c.n_buses = 2;
  c.n_reserve = 0;
  c.anomaly_dispatch = 1;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 1, 21660.0, 40, 1), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);
  ASSERT_EQ(sim_flag_count(s), 1);
  int found_log = 0;
  for (size_t i = 0; i < sim_log_count(s); i++) {
    if (strstr(sim_log_line(s, i), "no reserve bus")) {
      found_log = 1;
      break;
    }
  }
  ASSERT_TRUE(found_log);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 10. Reallocation to the forecast */
static void test_sim_reallocation_to_forecast(void) {
  TEST_START("test_sim_reallocation_to_forecast");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  for (int i = 0; i < 4; i++) {
    stops[i].type = STOP_OFFICE;
    stops[i].scale = 1.8;
  }
  for (int i = 4; i < 8; i++) {
    stops[i].type = STOP_RESIDENTIAL;
    stops[i].scale = 0.6;
  }
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 6;
  c.policy = SIM_POLICY_GREEDY_MARGINAL;
  c.start_min = 480.0;
  c.end_min = 600.0;
  c.realloc_interval_min = 30.0;

  /* Compute expected initial counts via brute force in test */
  double d0 = 0.0, d1 = 0.0;
  for (int i = 0; i < 4; i++) {
    d0 += demand_expected(stops[i].type, stops[i].scale, 8, DAY_WEEKDAY, 0, 0);
  }
  for (int i = 4; i < 8; i++) {
    d1 += demand_expected(stops[i].type, stops[i].scale, 8, DAY_WEEKDAY, 0, 0);
  }
  double best_obj = 1e30;
  int exp_c0 = 0, exp_c1 = 0;
  for (int c0 = 1; c0 <= 5; c0++) {
    int c1 = 6 - c0;
    double obj =
        (d0 * 680.0) / (2.0 * (double)c0) + (d1 * 680.0) / (2.0 * (double)c1);
    if (obj < best_obj) {
      best_obj = obj;
      exp_c0 = c0;
      exp_c1 = c1;
    }
  }

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_route_bus_count(s, 0), (size_t)exp_c0);
  ASSERT_EQ(sim_route_bus_count(s, 1), (size_t)exp_c1);
  sim_destroy(s);

  /* Policy GREEDY_SPEC: priority D_r / (c + 1) */
  c.policy = SIM_POLICY_GREEDY_SPEC;
  int spec_c0 = 1, spec_c1 = 1;
  for (int bus = 2; bus < 6; bus++) {
    double p0 = d0 / (double)(spec_c0 + 1);
    double p1 = d1 / (double)(spec_c1 + 1);
    if (p0 >= p1)
      spec_c0++;
    else
      spec_c1++;
  }
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_route_bus_count(s, 0), (size_t)spec_c0);
  ASSERT_EQ(sim_route_bus_count(s, 1), (size_t)spec_c1);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 11. Hysteresis and symmetric demand */
static void test_sim_hysteresis_and_symmetric_demand(void) {
  TEST_START("test_sim_hysteresis_and_symmetric_demand");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  for (int i = 0; i < 8; i++) {
    stops[i].type = STOP_RESIDENTIAL;
    stops[i].scale = 1.0;
  }
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 6;
  c.policy = SIM_POLICY_GREEDY_MARGINAL;
  c.start_min = 480.0;
  c.end_min = 600.0;
  c.realloc_interval_min = 30.0;
  c.realloc_hysteresis = 0.05;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_route_bus_count(s, 0), 3);
  ASSERT_EQ(sim_route_bus_count(s, 1), 3);

  /* Run through 3 decisions (480 -> 510 -> 540 -> 570) */
  ASSERT_EQ(sim_run_until(s, 580.0), STATUS_OK);

  SimStats st;
  sim_stats(s, &st);
  ASSERT_TRUE(st.decisions >= 2);
  ASSERT_EQ(st.moves_applied, 0);
  ASSERT_EQ(st.decisions_applied, 0);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 12. Dynamic reassignment */
static void test_sim_dynamic_reassignment(void) {
  TEST_START("test_sim_dynamic_reassignment");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  for (int i = 0; i < 8; i++) {
    stops[i].type = STOP_RESIDENTIAL;
    stops[i].scale = 1.0;
  }
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 6;
  c.policy = SIM_POLICY_GREEDY_MARGINAL;
  c.start_min = 480.0;
  c.end_min = 600.0;
  c.realloc_interval_min = 30.0;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /* Route 1 has buses 3, 4, 5 */
  /*
   * Bus 3 arrives at stop 4 on second loop at 29480 s.
   * Inject 5 pax with ride 3 at stop 4 at 29470 s so bus 3 boards them.
   * At 495 min (29700 s), bus 3 is in transit between stop 5 and 6 with 5 pax
   * on board.
   */
  ASSERT_EQ(sim_inject_arrivals(s, 4, 29470.0, 5, 3), STATUS_OK);
  /* Schedule breakdown of buses 3, 4, 5 at consecutive slice boundaries */
  SimScenario sc1 = {495.0, SIM_SCEN_BREAKDOWN, 3};
  SimScenario sc2 = {495.0, SIM_SCEN_BREAKDOWN, 4};
  SimScenario sc3 = {495.0, SIM_SCEN_BREAKDOWN, 5};
  ASSERT_EQ(sim_schedule(s, &sc1), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc2), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc3), STATUS_OK);

  /* Advance to 495 min (slice boundary) so current slice becomes [495, 510 min)
   */
  ASSERT_EQ(sim_run_until(s, 495.0), STATUS_OK);

  /*
   * Inject passengers at stop 3 so that all active buses on Route 0 (buses 0,
   * 1, 2) board passengers with ride 2 (dest stop 1) before the 510 min
   * decision:
   * - at 30250 s: Bus 2 boards them at 30280 s
   * - at 30350 s: Bus 1 boards them at 30400 s
   * - at 30500 s: Bus 0 boards them at 30520 s
   * All 3 buses have load = 4 at 510 min (30600 s).
   * Whichever bus is picked to move to Route 1 cannot alight before stop 0,
   * so it forced-dumps its passengers on arrival.
   */
  ASSERT_EQ(sim_inject_arrivals(s, 3, 30250.0, 4, 2), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 3, 30350.0, 4, 2), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(s, 3, 30500.0, 4, 2), STATUS_OK);

  /* Run past decision at 510 min */
  ASSERT_EQ(sim_run_until(s, 515.0), STATUS_OK);

  /* Route 1 now has 0 working buses, so decision at 510 min planned moves to
   * route 1 */
  SimStats st;
  sim_stats(s, &st);
  ASSERT_TRUE(st.stranded_by_breakdown > 0);

  /* Check that at least one bus was reassigned or is deadheading/running on
   * route 1 */
  ASSERT_EQ(sim_run_until(s, 540.0), STATUS_OK);
  sim_stats(s, &st);
  ASSERT_TRUE(st.moves_applied > 0);
  ASSERT_TRUE(st.forced_dumps > 0);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 13. Blocked edge */
static void test_sim_blocked_edge(void) {
  TEST_START("test_sim_blocked_edge");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /* Block edge between 0 and 1 in both directions at 375 min */
  SimScenario sc_b1 = {375.0, SIM_SCEN_BLOCK_EDGE, 0};
  SimScenario sc_b2 = {375.0, SIM_SCEN_BLOCK_EDGE, 1};
  ASSERT_EQ(sim_schedule(s, &sc_b1), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc_b2), STATUS_OK);

  /* Run to 400 min: leg 0->1 is unreachable, bus will stall */
  ASSERT_EQ(sim_run_until(s, 400.0), STATUS_OK);
  SimStats st;
  sim_stats(s, &st);
  ASSERT_TRUE(st.stalled_s > 0.0);

  /* Unblock edge at 405 min */
  SimScenario sc_u1 = {405.0, SIM_SCEN_UNBLOCK_EDGE, 0};
  SimScenario sc_u2 = {405.0, SIM_SCEN_UNBLOCK_EDGE, 1};
  ASSERT_EQ(sim_schedule(s, &sc_u1), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc_u2), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, 430.0), STATUS_OK);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 14. Traffic coupling */
static void test_sim_traffic_coupling(void) {
  TEST_START("test_sim_traffic_coupling");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.apply_traffic = 1;
  c.start_min = 480.0; /* 08:00 */
  c.end_min = 600.0;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /* Run to 09:00 (540 min) */
  ASSERT_EQ(sim_run_until(s, 540.0), STATUS_OK);
  SimStats st;
  sim_stats(s, &st);
  ASSERT_TRUE(st.bus_arrivals > 0);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 15. Random arrivals, statistics */
static void test_sim_random_arrivals_statistics(void) {
  TEST_START("test_sim_random_arrivals_statistics");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  sim_config_default(&c);
  c.arrivals_enabled = 1;
  c.n_buses = 8;
  c.travel_noise = 0.10;
  c.warmup_min = 0.0;
  c.start_min = 360.0;
  c.end_min = 1320.0;
  c.seed = 42;

  Sim *s1 = NULL;
  ASSERT_EQ(sim_create(net, &c, &s1), STATUS_OK);
  ASSERT_EQ(sim_run_to_end(s1), STATUS_OK);

  SimStats st1;
  sim_stats(s1, &st1);

  /* Compute independent expectation over slices and stops */
  double expected_total = 0.0;
  for (double t = c.start_min; t < c.end_min; t += c.slice_min) {
    int hour = (int)floor(t / 60.0);
    for (int i = 0; i < 8; i++) {
      double rate = demand_expected(stops[i].type, stops[i].scale, hour,
                                    c.day_type, c.rain, 0);
      expected_total += rate * (c.slice_min / 60.0);
    }
  }

  double diff = fabs((double)st1.arrived - expected_total);
  double sigma = sqrt(expected_total);
  ASSERT_TRUE(diff <= 5.0 * sigma);
  ASSERT_EQ(st1.arrived, st1.boarded + st1.still_waiting);

  /* Per route stats add up */
  SimRouteStats r0, r1;
  sim_route_stats(s1, 0, &r0);
  sim_route_stats(s1, 1, &r1);
  ASSERT_EQ(st1.boarded, r0.boarded + r1.boarded);
  ASSERT_EQ(st1.still_waiting, r0.still_waiting + r1.still_waiting);

  /* Determinism check with fresh run and same seed */
  Sim *s2 = NULL;
  ASSERT_EQ(sim_create(net, &c, &s2), STATUS_OK);
  ASSERT_EQ(sim_run_to_end(s2), STATUS_OK);
  SimStats st2;
  sim_stats(s2, &st2);

  ASSERT_EQ(st1.arrived, st2.arrived);
  ASSERT_EQ(st1.boarded, st2.boarded);
  ASSERT_EQ(st1.still_waiting, st2.still_waiting);
  ASSERT_EQ(st1.wait_sum_s, st2.wait_sum_s);
  ASSERT_EQ(st1.bus_arrivals, st2.bus_arrivals);

  /* Different seed check */
  c.seed = 999;
  Sim *s3 = NULL;
  ASSERT_EQ(sim_create(net, &c, &s3), STATUS_OK);
  ASSERT_EQ(sim_run_to_end(s3), STATUS_OK);
  SimStats st3;
  sim_stats(s3, &st3);
  ASSERT_TRUE(st1.arrived != st3.arrived || st1.wait_sum_s != st3.wait_sum_s);

  sim_destroy(s1);
  sim_destroy(s2);
  sim_destroy(s3);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 16. Scenario semantics */
static void test_sim_scenario_semantics(void) {
  TEST_START("test_sim_scenario_semantics");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  c.arrivals_enabled = 1;
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  SimScenario sc_rain = {405.0, SIM_SCEN_RAIN_ON, 0};
  SimScenario sc_ev = {420.0, SIM_SCEN_EVENT_ON, 2};
  ASSERT_EQ(sim_schedule(s, &sc_rain), STATUS_OK);
  ASSERT_EQ(sim_schedule(s, &sc_ev), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, 435.0), STATUS_OK);
  ASSERT_APPROX(sim_event_start_min(s, 2), 420.0, 1e-9);

  SimScenario sc_ev_off = {450.0, SIM_SCEN_EVENT_OFF, 2};
  ASSERT_EQ(sim_schedule(s, &sc_ev_off), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 465.0), STATUS_OK);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 17. Breakdown and repair */
static void test_sim_breakdown_and_repair(void) {
  TEST_START("test_sim_breakdown_and_repair");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  get_scripted_config(&c);
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /*
   * Bus 0 arrives at stop 2 on loop 1 at 21840 s (364 min).
   * Inject 1 pax with ride 2 at stop 2 at 21810 s.
   * Bus 0 boards pax at 21840 s.
   * Passenger drop-off is at stop 0 at 22280 s.
   * At 375 min (22500 s), bus 0 completed loop 1 and is on loop 2.
   * Better: inject at stop 1 at 22390 s with ride 2.
   * Bus 0 boards at 22400 s (373.33 min).
   * Breakdown scheduled at 375 min (22500 s) occurs while bus is between stop 1
   * and 2 with load 1.
   */
  ASSERT_EQ(sim_inject_arrivals(s, 1, 22390.0, 1, 2), STATUS_OK);
  SimScenario sc_bd = {375.0, SIM_SCEN_BREAKDOWN, 0};
  ASSERT_EQ(sim_schedule(s, &sc_bd), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 375.0), STATUS_OK);

  SimBusInfo bi;
  sim_bus_info(s, 0, &bi);
  ASSERT_EQ(bi.state, SIM_BUS_BROKEN);
  ASSERT_EQ(bi.load, 0);

  SimStats st;
  sim_stats(s, &st);
  ASSERT_EQ(st.stranded_by_breakdown, 1);

  /* Repeat breakdown on already broken bus is ignored */
  SimScenario sc_bd2 = {390.0, SIM_SCEN_BREAKDOWN, 0};
  ASSERT_EQ(sim_schedule(s, &sc_bd2), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 390.0), STATUS_OK);

  /* Repair bus 0 */
  SimScenario sc_rp = {405.0, SIM_SCEN_REPAIR, 0};
  ASSERT_EQ(sim_schedule(s, &sc_rp), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 405.0), STATUS_OK);
  sim_bus_info(s, 0, &bi);
  ASSERT_EQ(bi.state, SIM_BUS_RESERVE);

  /* Repair of non-broken bus returns STATUS_ERR_INVALID */
  SimScenario sc_rp_bad = {420.0, SIM_SCEN_REPAIR, 0};
  ASSERT_EQ(sim_schedule(s, &sc_rp_bad), STATUS_OK);
  ASSERT_EQ(sim_run_until(s, 420.0), STATUS_OK);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 18. Sanity band */
static void test_sim_sanity_band(void) {
  TEST_START("test_sim_sanity_band");

  /* 1 route, 4 stops, cycle 680 s, 3 buses */
  DemandStop stops[4];
  Graph *g = graph_create();
  for (int i = 0; i < 4; i++) {
    char name[32];
    snprintf(name, sizeof(name), "Stop %d", i);
    graph_add_node(g, (int64_t)(1000 + i), 12.9716, 77.5946 + (double)i * 0.001,
                   name, 1);
    stops[i].node = (size_t)i;
    stops[i].type = STOP_OFFICE;
    stops[i].scale = 1.0;
    snprintf(stops[i].name, sizeof(stops[i].name), "Stop %d", i);
    stops[i].lat = 12.9716;
    stops[i].lng = 77.5946 + (double)i * 0.001;
  }
  for (int i = 0; i < 3; i++) {
    char e_fwd[16], e_rev[16];
    snprintf(e_fwd, sizeof(e_fwd), "e%d_%d", i, i + 1);
    snprintf(e_rev, sizeof(e_rev), "e%d_%d", i + 1, i);
    graph_add_edge(g, i, i + 1, 100.0, 100.0, e_fwd, ROAD_PRIMARY);
    graph_add_edge(g, i + 1, i, 100.0, 100.0, e_rev, ROAD_PRIMARY);
  }
  graph_add_edge(g, 3, 0, 300.0, 300.0, "e3_0", ROAD_PRIMARY);
  graph_add_edge(g, 0, 3, 300.0, 300.0, "e0_3", ROAD_PRIMARY);

  NetworkConfig ncfg;
  network_config_default(&ncfg);
  ncfg.n_routes = 1;
  ncfg.dwell_s_per_stop = 20.0;
  ncfg.build_matrix = 1;
  NetworkModel *net = NULL;
  ASSERT_EQ(network_build(g, stops, 4, &ncfg, &net), STATUS_OK);

  SimConfig c;
  sim_config_default(&c);
  c.arrivals_enabled = 1;
  c.travel_noise = 0.0;
  c.apply_traffic = 0;
  c.n_buses = 3;
  c.warmup_min = 30.0;
  c.start_min = 600.0; /* 10:00 to 14:00 */
  c.end_min = 840.0;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_EQ(sim_run_to_end(s), STATUS_OK);

  SimStats st;
  sim_stats(s, &st);
  /* Analytic proxy = 680 / (2 * 3) = 113.333 s */
  double proxy = 680.0 / 6.0;
  ASSERT_TRUE(st.mean_wait_boarded_s >= 0.5 * proxy);
  ASSERT_TRUE(st.mean_wait_boarded_s <= 2.5 * proxy);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 19. Memory */
static void test_sim_memory(void) {
  TEST_START("test_sim_memory");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  NetworkModel *net = build_line_network(g, stops, 2);

  SimConfig c;
  sim_config_default(&c);
  c.arrivals_enabled = 1;
  c.n_buses = 6;
  c.policy = SIM_POLICY_GREEDY_MARGINAL;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);

  /* Generate lots of scenarios to exceed 2000 log lines */
  for (int i = 0; i < 2500; i++) {
    SimScenario sc = {360.0 + (double)(i % 10) * 15.0, SIM_SCEN_RAIN_ON, 0};
    sim_schedule(s, &sc);
  }

  ASSERT_EQ(sim_run_until(s, 510.0), STATUS_OK);

  /* Check log count */
  ASSERT_TRUE(sim_log_count(s) > 2000);
  /* But kept lines never exceed 2000 */
  ASSERT_NOT_NULL(sim_log_line(s, 1999));
  ASSERT_NULL(sim_log_line(s, 2000));

  /* Destroy mid-run with events pending */
  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/*
 * Test 20: BY_TIME N=4 Initial Stops
 * Route 0 stops: 0, 1, 2, 3 (legs: 100, 100, 100, 300, dwell 20) -> T = [0, 120, 240, 360, 680]
 * Route 1 stops: 4, 5, 6, 7 (same leg geometry)
 * N = 4 buses (2 on Route 0, 2 on Route 1)
 * Under BY_TIME:
 * Route 0:
 *   Bus 0: psi_0 = 0 -> k* = 0 -> pos = 0 (Stop 0)
 *   Bus 1: psi_1 = 340 -> T_3 = 360 >= 340 -> k* = 3 -> pos = 3 (Stop 3)
 * Route 1:
 *   Bus 2: psi_0 = 0 -> k* = 0 -> pos = 0 (Stop 4)
 *   Bus 3: psi_1 = 340 -> k* = 3 -> pos = 3 (Stop 7)
 */
static void test_sim_spacing_by_time_positions(void) {
  TEST_START("test_sim_spacing_by_time_positions");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 4;
  c.initial_spacing = SIM_SPACING_BY_TIME;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_NOT_NULL(s);

  SimBusInfo b0, b1, b2, b3;
  ASSERT_EQ(sim_bus_info(s, 0, &b0), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s, 1, &b1), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s, 2, &b2), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s, 3, &b3), STATUS_OK);

  ASSERT_EQ(b0.route, 0);
  ASSERT_EQ(b0.stop_idx, 0);

  ASSERT_EQ(b1.route, 0);
  ASSERT_EQ(b1.stop_idx, 3);

  ASSERT_EQ(b2.route, 1);
  ASSERT_EQ(b2.stop_idx, 4);

  ASSERT_EQ(b3.route, 1);
  ASSERT_EQ(b3.stop_idx, 7);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/*
 * Test 21: BY_TIME N=4 Wait Sum 330s vs Legacy 430s
 * Injection of 1 passenger at stop 0 at t_0 + 10s.
 * Bus 0 arrives at stop 0 at t_0 and departs at t_0 + 20s. The passenger
 * arrives at t_0 + 10s during dwell, after Bus 0 has finished boarding,
 * and therefore must wait for the next bus.
 *
 * Under Legacy BY_STOP:
 *   Bus 1 starts at stop 2 at t_0, travels 2->3 (120s dwell+leg), arrives 3 at t_0+120,
 *   departs 3 at t_0+140, travels 3->0 (300s leg), arrives at stop 0 at t_0 + 440s.
 *   Waiting time = (t_0 + 440) - (t_0 + 10) = 430s.
 *
 * Under BY_TIME:
 *   Bus 1 starts at stop 3 with arrival delay T_3 - 340 = 360 - 340 = 20s.
 *   Bus 1 arrives at stop 3 at t_0 + 20, dwells 20s (departs t_0 + 40),
 *   travels 3->0 (300s leg), arrives at stop 0 at t_0 + 40 + 300 = t_0 + 340s.
 *   Waiting time = (t_0 + 340) - (t_0 + 10) = 330s.
 */
static void test_sim_spacing_by_time_timeline_vs_legacy(void) {
  TEST_START("test_sim_spacing_by_time_timeline_vs_legacy");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  /* 1. Legacy Spacing (BY_STOP) */
  {
    SimConfig c;
    get_scripted_config(&c);
    c.n_buses = 4;
    c.initial_spacing = SIM_SPACING_BY_STOP;

    Sim *s = NULL;
    ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
    ASSERT_NOT_NULL(s);

    double t0 = c.start_min * 60.0;
    ASSERT_EQ(sim_inject_arrivals(s, 0, t0 + 10.0, 1, 1), STATUS_OK);

    ASSERT_EQ(sim_run_until(s, c.start_min + 15.0), STATUS_OK);

    SimStats st;
    ASSERT_EQ(sim_stats(s, &st), STATUS_OK);
    ASSERT_EQ(st.boarded, 1ULL);
    ASSERT_APPROX(st.wait_sum_s, 430.0, 1e-6);

    sim_destroy(s);
  }

  /* 2. Time Spacing (BY_TIME) */
  {
    SimConfig c;
    get_scripted_config(&c);
    c.n_buses = 4;
    c.initial_spacing = SIM_SPACING_BY_TIME;

    Sim *s = NULL;
    ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
    ASSERT_NOT_NULL(s);

    double t0 = c.start_min * 60.0;
    ASSERT_EQ(sim_inject_arrivals(s, 0, t0 + 10.0, 1, 1), STATUS_OK);

    ASSERT_EQ(sim_run_until(s, c.start_min + 15.0), STATUS_OK);

    SimStats st;
    ASSERT_EQ(sim_stats(s, &st), STATUS_OK);
    ASSERT_EQ(st.boarded, 1ULL);
    ASSERT_APPROX(st.wait_sum_s, 330.0, 1e-6);

    sim_destroy(s);
  }

  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/*
 * Test 22: BY_TIME N=6 Initial Stops and Wait Sum = 680/3 - 10 = 216.6667
 * 6 buses total -> 3 buses on Route 0, 3 buses on Route 1.
 * Route cycle = 680s.
 * Route 0:
 *   Bus 0: psi_0 = 0.0 -> k* = 0 -> pos = 0 (Stop 0), delay = 0
 *   Bus 1: psi_1 = 680/3 = 226.6667 -> T_2 = 240 >= 226.6667 -> k* = 2 -> pos = 2 (Stop 2), delay = 13.3333s
 *   Bus 2: psi_2 = 2*680/3 = 453.3333 -> T_4 = 680 >= 453.3333 -> k* = 4 -> pos = 0 (Stop 0), delay = 226.6667s
 * Route 1:
 *   Bus 3: pos = 0 (Stop 4)
 *   Bus 4: pos = 2 (Stop 6)
 *   Bus 5: pos = 0 (Stop 4)
 *
 * Inject 1 passenger at stop 0 at t_0 + 10s.
 * Bus 0 arrives at t_0, boards nobody, departs at t_0 + 20s.
 * Next arrival at stop 0 is Bus 2 scheduled at t_0 + delay = t_0 + 226.666667s.
 * Passenger boards Bus 2 at t_0 + 226.666667s.
 * Waiting time = 226.666667 - 10.0 = 216.666667s (exact formula: 680.0 / 3.0 - 10.0).
 */
static void test_sim_spacing_by_time_three_buses(void) {
  TEST_START("test_sim_spacing_by_time_three_buses");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 6;
  c.initial_spacing = SIM_SPACING_BY_TIME;

  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_OK);
  ASSERT_NOT_NULL(s);

  SimBusInfo b0, b1, b2, b3, b4, b5;
  ASSERT_EQ(sim_bus_info(s, 0, &b0), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s, 1, &b1), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s, 2, &b2), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s, 3, &b3), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s, 4, &b4), STATUS_OK);
  ASSERT_EQ(sim_bus_info(s, 5, &b5), STATUS_OK);

  ASSERT_EQ(b0.stop_idx, 0);
  ASSERT_EQ(b1.stop_idx, 2);
  ASSERT_EQ(b2.stop_idx, 0);

  ASSERT_EQ(b3.stop_idx, 4);
  ASSERT_EQ(b4.stop_idx, 6);
  ASSERT_EQ(b5.stop_idx, 4);

  double t0 = c.start_min * 60.0;
  ASSERT_EQ(sim_inject_arrivals(s, 0, t0 + 10.0, 1, 1), STATUS_OK);

  ASSERT_EQ(sim_run_until(s, c.start_min + 15.0), STATUS_OK);

  SimStats st;
  ASSERT_EQ(sim_stats(s, &st), STATUS_OK);
  ASSERT_EQ(st.boarded, 1ULL);
  ASSERT_APPROX(st.wait_sum_s, 680.0 / 3.0 - 10.0, 1e-4);

  sim_destroy(s);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/*
 * Test 23: Config Validation for Spacing Mode
 * Default is SIM_SPACING_BY_STOP (0).
 * Values 2 and -1 must return STATUS_ERR_INVALID and *out == NULL.
 */
static void test_sim_spacing_config_validation(void) {
  TEST_START("test_sim_spacing_config_validation");

  SimConfig def;
  sim_config_default(&def);
  ASSERT_EQ(def.initial_spacing, SIM_SPACING_BY_STOP);

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c = def;
  c.initial_spacing = 2;
  Sim *s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);
  ASSERT_NULL(s);

  c.initial_spacing = -1;
  s = NULL;
  ASSERT_EQ(sim_create(net, &c, &s), STATUS_ERR_INVALID);
  ASSERT_NULL(s);

  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

int main(void) {
  RUN_TEST(test_sim_config_and_errors);
  RUN_TEST(test_sim_scripted_timeline);
  RUN_TEST(test_sim_capacity);
  RUN_TEST(test_sim_warmup);
  RUN_TEST(test_sim_censoring);
  RUN_TEST(test_sim_percentiles);
  RUN_TEST(test_sim_route_stats);
  RUN_TEST(test_sim_dwell_with_passengers);
  RUN_TEST(test_sim_anomaly_detection_and_dispatch);
  RUN_TEST(test_sim_reallocation_to_forecast);
  RUN_TEST(test_sim_hysteresis_and_symmetric_demand);
  RUN_TEST(test_sim_dynamic_reassignment);
  RUN_TEST(test_sim_blocked_edge);
  RUN_TEST(test_sim_traffic_coupling);
  RUN_TEST(test_sim_random_arrivals_statistics);
  RUN_TEST(test_sim_scenario_semantics);
  RUN_TEST(test_sim_breakdown_and_repair);
  RUN_TEST(test_sim_sanity_band);
  RUN_TEST(test_sim_memory);
  RUN_TEST(test_sim_spacing_by_time_positions);
  RUN_TEST(test_sim_spacing_by_time_timeline_vs_legacy);
  RUN_TEST(test_sim_spacing_by_time_three_buses);
  RUN_TEST(test_sim_spacing_config_validation);

  return test_summary();
}
