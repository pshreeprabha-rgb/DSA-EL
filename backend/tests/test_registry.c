/**
 * @file test_registry.c
 * @brief Unit and integration tests for the Part 3D Registry module.
 */

#include "graph.h"
#include "network.h"
#include "registry.h"
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
      stops_out[i].osm_id = (int64_t)(1000 + i);
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
  if (network_build(g, stops, 8, &ncfg, &net) != STATUS_OK) return NULL;
  return net;
}

static void get_scripted_config(SimConfig *c) {
  sim_config_default(c);
  c->arrivals_enabled = 0;
  c->dwell_s = 20.0;
  c->per_pax_s = 0.0;
  c->travel_noise = 0.0;
  c->bus_capacity = 100;
  c->n_buses = 4;
  c->n_reserve = 0;
  c->apply_traffic = 0;
  c->warmup_min = 0.0;
  c->slice_min = 15.0;
  c->start_min = 360.0;
  c->end_min = 1320.0;
  c->policy = SIM_POLICY_FIXED_EQUAL;
  c->initial_spacing = SIM_SPACING_BY_STOP;
}

/* 1. Tree structure, preorder rows, height, count */
static void test_reg_tree_structure(void) {
  TEST_START("test_reg_tree_structure");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  Sim *sim = NULL;
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);

  /*
   * Hand derivation:
   * Depots: West (stop 0), East (stop 7).
   * Route 0 starts at stop 0 -> 0 steps from West -> child of West.
   * Route 1 starts at stop 4 -> 3 steps from East vs 4 steps from West -> child of East.
   * Buses: [0,1] on Route 0, [2,3] on Route 1.
   * Preorder: City(0), West(1), Route 0(2), Bus 0(3), Bus 1(3), East(1), Route 1(2), Bus 2(3), Bus 3(3).
   * Total nodes = 9, tree height = 3.
   */
  const RegistryDepotSpec depots[2] = {{"West", 0}, {"East", 7}};
  Registry *reg = NULL;
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);
  ASSERT_EQ(registry_tree_count(reg), 9);
  ASSERT_EQ(registry_tree_height(reg), 3);

  RegTreeRow rows[16];
  size_t n = 0;
  ASSERT_EQ(registry_tree_rows(reg, rows, 16, &n), STATUS_OK);
  ASSERT_EQ(n, 9);

  const int exp_kinds[9] = {REG_KIND_CITY, REG_KIND_DEPOT, REG_KIND_ROUTE,
                            REG_KIND_BUS,  REG_KIND_BUS,   REG_KIND_DEPOT,
                            REG_KIND_ROUTE, REG_KIND_BUS,  REG_KIND_BUS};
  const int exp_ids[9] = {0, 0, 0, 0, 1, 1, 1, 2, 3};
  const int exp_parents[9] = {-1, 0, 1, 2, 2, 0, 5, 6, 6};
  const int exp_depths[9] = {0, 1, 2, 3, 3, 1, 2, 3, 3};

  for (size_t i = 0; i < 9; i++) {
    ASSERT_EQ(rows[i].kind, exp_kinds[i]);
    ASSERT_EQ(rows[i].id, exp_ids[i]);
    ASSERT_EQ(rows[i].parent_row, exp_parents[i]);
    ASSERT_EQ(rows[i].depth, exp_depths[i]);
    ASSERT_TRUE(rows[i].name[0] != '\0');
  }
  ASSERT_EQ(strcmp(rows[0].name, "Bengaluru"), 0);
  ASSERT_EQ(strcmp(rows[1].name, "West"), 0);
  ASSERT_EQ(strcmp(rows[5].name, "East"), 0);

  registry_destroy(reg);
  sim_destroy(sim);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 2. Hash lookups for buses, routes, and stops */
static void test_reg_hash_lookups(void) {
  TEST_START("test_reg_hash_lookups");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  Sim *sim = NULL;
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);

  const RegistryDepotSpec depots[2] = {{"West", 0}, {"East", 7}};
  Registry *reg = NULL;
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);
  ASSERT_EQ(registry_bus_count(reg), 4);

  for (int i = 0; i < 4; i++) {
    const Bus *b = registry_bus(reg, i);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(b->id, i);
  }
  ASSERT_NULL(registry_bus(reg, 4));
  const Bus *b1 = registry_bus(reg, 1);
  ASSERT_NOT_NULL(b1);
  ASSERT_TRUE(b1->capacity > 0);
  ASSERT_EQ(b1->current_route_id, 0);

  for (int r = 0; r < 2; r++) {
    const Route *rt = registry_route(reg, r);
    ASSERT_NOT_NULL(rt);
    ASSERT_EQ(rt->id, r);
    ASSERT_EQ(ll_size(rt->stop_ids), 4);
  }
  ASSERT_NULL(registry_route(reg, 2));

  for (int s = 0; s < 8; s++) {
    const Stop *st = registry_stop(reg, s);
    ASSERT_NOT_NULL(st);
    ASSERT_EQ(st->id, s);
  }
  ASSERT_NULL(registry_stop(reg, 8));

  registry_destroy(reg);
  sim_destroy(sim);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 3. Timetable worked example */
static void test_reg_timetable_worked_example(void) {
  TEST_START("test_reg_timetable_worked_example");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  Sim *sim = NULL;
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);

  const RegistryDepotSpec depots[2] = {{"West", 0}, {"East", 7}};
  Registry *reg = NULL;
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);

  /*
   * Hand derivation:
   * cycle = 680 s -> cycle_min = floor(680/60 + 0.5) = 11.
   * counts [2, 2], from 360, to 390:
   * headway = 5.5 -> offsets 0, 6.
   * Route 0: bus 0 -> 360(1), 371(2), 382(3); bus 1 -> 366(4), 377(5), 388(6).
   * Route 1: bus 2 -> 360(7), 371(8), 382(9); bus 3 -> 366(10), 377(11), 388(12).
   */
  const int counts[2] = {2, 2};
  ASSERT_EQ(registry_build_timetable(reg, counts, 2, 360, 390), STATUS_OK);
  ASSERT_EQ(registry_trip_count(reg), 12);
  ASSERT_EQ(registry_timetable_balanced(reg), 1);

  Trip all[16];
  size_t nt = 0;
  ASSERT_EQ(registry_trips(reg, 360, 390, -1, all, 16, &nt), STATUS_OK);
  ASSERT_EQ(nt, 12);

  const int exp_dep[12] = {360, 360, 366, 366, 371, 371, 377, 377, 382, 382, 388, 388};
  const int exp_id[12] = {1, 7, 4, 10, 2, 8, 5, 11, 3, 9, 6, 12};
  for (size_t i = 0; i < 12; i++) {
    ASSERT_EQ(all[i].departure_time, exp_dep[i]);
    ASSERT_EQ(all[i].trip_id, exp_id[i]);
  }

  const Trip *nd = registry_next_departure(reg, 0, 361);
  ASSERT_NOT_NULL(nd);
  ASSERT_EQ(nd->trip_id, 4);
  ASSERT_EQ(nd->departure_time, 366);

  nd = registry_next_departure(reg, 1, 388);
  ASSERT_NOT_NULL(nd);
  ASSERT_EQ(nd->trip_id, 12);

  ASSERT_NULL(registry_next_departure(reg, 1, 389));

  Trip sub[16];
  ASSERT_EQ(registry_trips(reg, 375, 390, -1, sub, 16, &nt), STATUS_OK);
  ASSERT_EQ(nt, 6);
  const int exp_sub_ids[6] = {5, 11, 3, 9, 6, 12};
  for (size_t i = 0; i < 6; i++) {
    ASSERT_EQ(sub[i].trip_id, exp_sub_ids[i]);
  }

  registry_destroy(reg);
  sim_destroy(sim);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 4. Replan from 375 with counts [3, 1] */
static void test_reg_replan_from(void) {
  TEST_START("test_reg_replan_from");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  Sim *sim = NULL;
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);

  const RegistryDepotSpec depots[2] = {{"West", 0}, {"East", 7}};
  Registry *reg = NULL;
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);

  const int counts1[2] = {2, 2};
  ASSERT_EQ(registry_build_timetable(reg, counts1, 2, 360, 390), STATUS_OK);

  /*
   * Hand derivation:
   * replan_from(375, [3, 1], 390):
   * Trips >= 375 (5,11,3,9,6,12) are deleted -> 6 remain.
   * New trips start at id 13:
   * Route 0 (c=3, headway=11/3, offsets 0, 4, 7; buses 0, 1, -1):
   *   375(13, bus 0), 386(14, bus 0), 379(15, bus 1), 382(16, bus -1).
   * Route 1 (c=1, headway=11, offset 0; bus 2):
   *   375(17, bus 2), 386(18, bus 2).
   * Total = 12 trips.
   * [375, 390) in order: (375,13),(375,17),(379,15),(382,16),(386,14),(386,18).
   */
  const int counts2[2] = {3, 1};
  ASSERT_EQ(registry_replan_from(reg, 375, counts2, 2, 390), STATUS_OK);
  ASSERT_EQ(registry_trip_count(reg), 12);
  ASSERT_EQ(registry_timetable_balanced(reg), 1);

  Trip sub[16];
  size_t nt = 0;
  ASSERT_EQ(registry_trips(reg, 375, 390, -1, sub, 16, &nt), STATUS_OK);
  ASSERT_EQ(nt, 6);

  const int exp_dep[6] = {375, 375, 379, 382, 386, 386};
  const int exp_id[6] = {13, 17, 15, 16, 14, 18};
  const int exp_bus[6] = {0, 2, 1, -1, 0, 2};
  for (size_t i = 0; i < 6; i++) {
    ASSERT_EQ(sub[i].departure_time, exp_dep[i]);
    ASSERT_EQ(sub[i].trip_id, exp_id[i]);
    ASSERT_EQ(sub[i].bus_id, exp_bus[i]);
  }

  registry_destroy(reg);
  sim_destroy(sim);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 5. Sync with DEADHEAD reassignment, breakdown, and reserve bus */
static void test_reg_sync_move_breakdown_reserve(void) {
  TEST_START("test_reg_sync_move_breakdown_reserve");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  /* Part A: T1 worked example (n_buses 3, bus 1 moves to route 1 in DUMP mode) */
  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 3;
  c.reassign_mode = SIM_REASSIGN_DUMP;
  Sim *sim = NULL;
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);

  const RegistryDepotSpec depots[2] = {{"West", 0}, {"East", 7}};
  Registry *reg = NULL;
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);

  ASSERT_EQ(sim_run_until(sim, 360.0), STATUS_OK);
  ASSERT_EQ(sim_request_move(sim, 1, 1), STATUS_OK);
  ASSERT_EQ(sim_run_until(sim, 362.0), STATUS_OK);

  ASSERT_EQ(registry_sync(reg, sim), STATUS_OK);
  const Bus *b1 = registry_bus(reg, 1);
  ASSERT_NOT_NULL(b1);
  ASSERT_EQ(b1->current_route_id, 1);
  ASSERT_EQ(b1->status, BUS_RUNNING);

  /* Route 0 has 1 child bus (bus 0), Route 1 has 2 child buses (bus 2, bus 1) */
  const Route *r0 = registry_route(reg, 0);
  const Route *r1 = registry_route(reg, 1);
  ASSERT_EQ(r0->assigned_buses, 1);
  ASSERT_EQ(r1->assigned_buses, 2);

  RegTreeRow rows[16];
  size_t n = 0;
  ASSERT_EQ(registry_tree_rows(reg, rows, 16, &n), STATUS_OK);
  ASSERT_EQ(n, 8);
  int r0_row = -1, r1_row = -1, r0_buses = 0, r1_buses = 0;
  for (size_t i = 0; i < n; i++) {
    if (rows[i].kind == REG_KIND_ROUTE && rows[i].id == 0) r0_row = (int)i;
    if (rows[i].kind == REG_KIND_ROUTE && rows[i].id == 1) r1_row = (int)i;
  }
  for (size_t i = 0; i < n; i++) {
    if (rows[i].kind == REG_KIND_BUS && rows[i].parent_row == r0_row) r0_buses++;
    if (rows[i].kind == REG_KIND_BUS && rows[i].parent_row == r1_row) r1_buses++;
  }
  ASSERT_EQ(r0_buses, 1);
  ASSERT_EQ(r1_buses, 2);

  /* Breakdown of bus 0 -> BUS_BROKEN */
  SimScenario brk = {375.0, SIM_SCEN_BREAKDOWN, 0};
  ASSERT_EQ(sim_schedule(sim, &brk), STATUS_OK);
  ASSERT_EQ(sim_run_until(sim, 375.0), STATUS_OK);
  ASSERT_EQ(registry_sync(reg, sim), STATUS_OK);
  const Bus *b0 = registry_bus(reg, 0);
  ASSERT_NOT_NULL(b0);
  ASSERT_EQ(b0->status, BUS_BROKEN);

  registry_destroy(reg);
  sim_destroy(sim);

  /* Part B: RESERVE bus is a direct child of its nearest depot */
  c.n_buses = 3;
  c.n_reserve = 1;
  c.depot_stop = 6; /* nearer to East (stop 7) than West (stop 0) */
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);

  ASSERT_EQ(registry_tree_rows(reg, rows, 16, &n), STATUS_OK);
  int east_row = -1, bus2_parent = -1, bus2_depth = -1;
  for (size_t i = 0; i < n; i++) {
    if (rows[i].kind == REG_KIND_DEPOT && rows[i].id == 1) east_row = (int)i;
    if (rows[i].kind == REG_KIND_BUS && rows[i].id == 2) {
      bus2_parent = rows[i].parent_row;
      bus2_depth = rows[i].depth;
    }
  }
  ASSERT_EQ(bus2_parent, east_row);
  ASSERT_EQ(bus2_depth, 2);
  ASSERT_EQ(registry_bus(reg, 2)->status, BUS_IDLE);

  registry_destroy(reg);
  sim_destroy(sim);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 6. Journeys with loop observer and 200-record ring cap */
static void test_reg_journeys_observer_and_cap(void) {
  TEST_START("test_reg_journeys_observer_and_cap");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 2;
  Sim *sim = NULL;
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);

  const RegistryDepotSpec depots[2] = {{"West", 0}, {"East", 7}};
  Registry *reg = NULL;
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);

  ASSERT_EQ(sim_set_loop_observer(sim, registry_sim_observer, reg), STATUS_OK);
  ASSERT_EQ(sim_inject_arrivals(sim, 1, 21610.0, 2, 1), STATUS_OK);
  ASSERT_EQ(sim_run_until(sim, 371.4), STATUS_OK);

  JourneyRecord jbuf[REGISTRY_MAX_JOURNEYS];
  size_t nj = 0;
  ASSERT_EQ(registry_journeys(reg, 0, jbuf, REGISTRY_MAX_JOURNEYS, &nj), STATUS_OK);
  ASSERT_EQ(nj, 1);
  ASSERT_EQ(jbuf[0].trip_id, 1);
  ASSERT_EQ(jbuf[0].route_id, 0);
  ASSERT_APPROX(jbuf[0].start_time, 360.0, 1e-9);
  ASSERT_APPROX(jbuf[0].end_time, 22280.0 / 60.0, 1e-6);
  ASSERT_EQ(jbuf[0].passengers_carried, 2);

  /* Use a fresh Registry to feed 205 records directly to bus 1 -> capped at 200 (trip_id 6..205) */
  registry_destroy(reg);
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);
  for (int i = 1; i <= 205; i++) {
    registry_sim_observer(reg, 1, 1, (double)i * 60.0, (double)(i + 10) * 60.0, i);
  }
  ASSERT_EQ(registry_journeys(reg, 1, jbuf, REGISTRY_MAX_JOURNEYS, &nj), STATUS_OK);
  ASSERT_EQ(nj, REGISTRY_MAX_JOURNEYS);
  ASSERT_EQ(jbuf[0].trip_id, 6);
  ASSERT_EQ(jbuf[REGISTRY_MAX_JOURNEYS - 1].trip_id, 205);

  /* Unknown bus ignored */
  registry_sim_observer(reg, 999, 0, 0.0, 60.0, 1);
  registry_sim_observer(NULL, 0, 0, 0.0, 60.0, 1);

  registry_destroy(reg);
  sim_destroy(sim);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 7. Priority board via max-heap */
static void test_reg_route_board(void) {
  TEST_START("test_reg_route_board");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 3);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  c.n_buses = 3;
  Sim *sim = NULL;
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);

  const RegistryDepotSpec depots[2] = {{"West", 0}, {"East", 7}};
  Registry *reg = NULL;
  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);

  const double demand[3] = {30.0, 10.0, 5.0};
  RouteBoardRow out[3];

  /* Case 1: counts [1, 1, 1] -> priorities 15, 5, 2.5 -> order 0, 1, 2 */
  const int c1[3] = {1, 1, 1};
  ASSERT_EQ(registry_route_board(reg, demand, c1, out, 3), STATUS_OK);
  ASSERT_EQ(out[0].route_id, 0); ASSERT_APPROX(out[0].priority, 15.0, 1e-9);
  ASSERT_EQ(out[1].route_id, 1); ASSERT_APPROX(out[1].priority, 5.0, 1e-9);
  ASSERT_EQ(out[2].route_id, 2); ASSERT_APPROX(out[2].priority, 2.5, 1e-9);

  /* Case 2: counts [5, 1, 1] -> priorities 5, 5, 2.5 -> tie by route_id -> 0, 1, 2 */
  const int c2[3] = {5, 1, 1};
  ASSERT_EQ(registry_route_board(reg, demand, c2, out, 3), STATUS_OK);
  ASSERT_EQ(out[0].route_id, 0); ASSERT_APPROX(out[0].priority, 5.0, 1e-9);
  ASSERT_EQ(out[1].route_id, 1); ASSERT_APPROX(out[1].priority, 5.0, 1e-9);
  ASSERT_EQ(out[2].route_id, 2); ASSERT_APPROX(out[2].priority, 2.5, 1e-9);

  /* Case 3: counts [1, 10, 1] -> priorities 15, 10/11=0.90909, 2.5 -> order 0, 2, 1 */
  const int c3[3] = {1, 10, 1};
  ASSERT_EQ(registry_route_board(reg, demand, c3, out, 3), STATUS_OK);
  ASSERT_EQ(out[0].route_id, 0); ASSERT_APPROX(out[0].priority, 15.0, 1e-9);
  ASSERT_EQ(out[1].route_id, 2); ASSERT_APPROX(out[1].priority, 2.5, 1e-9);
  ASSERT_EQ(out[2].route_id, 1); ASSERT_APPROX(out[2].priority, 10.0 / 11.0, 1e-6);

  registry_destroy(reg);
  sim_destroy(sim);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

/* 8. Errors and memory */
static void test_reg_errors_and_memory(void) {
  TEST_START("test_reg_errors_and_memory");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  SimConfig c;
  get_scripted_config(&c);
  Sim *sim = NULL;
  ASSERT_EQ(sim_create(net, &c, &sim), STATUS_OK);

  const RegistryDepotSpec depots[2] = {{"West", 0}, {"East", 7}};
  const RegistryDepotSpec bad_depot[1] = {{"Bad", 99}};
  Registry *reg = NULL;

  ASSERT_EQ(registry_create(NULL, depots, 2, sim, &reg), STATUS_ERR_NULL);
  ASSERT_EQ(registry_create(net, NULL, 2, sim, &reg), STATUS_ERR_NULL);
  ASSERT_EQ(registry_create(net, depots, 2, NULL, &reg), STATUS_ERR_NULL);
  ASSERT_EQ(registry_create(net, depots, 2, sim, NULL), STATUS_ERR_NULL);
  ASSERT_EQ(registry_create(net, depots, 0, sim, &reg), STATUS_ERR_INVALID);
  ASSERT_EQ(registry_create(net, bad_depot, 1, sim, &reg), STATUS_ERR_INVALID);
  ASSERT_NULL(reg);

  registry_destroy(NULL);
  ASSERT_EQ(registry_bus_count(NULL), 0);
  ASSERT_EQ(registry_tree_count(NULL), 0);
  ASSERT_EQ(registry_tree_height(NULL), -1);
  ASSERT_EQ(registry_trip_count(NULL), 0);
  ASSERT_EQ(registry_timetable_height(NULL), -1);
  ASSERT_EQ(registry_timetable_balanced(NULL), -1);
  ASSERT_NULL(registry_bus(NULL, 0));
  ASSERT_NULL(registry_route(NULL, 0));
  ASSERT_NULL(registry_stop(NULL, 0));

  ASSERT_EQ(registry_create(net, depots, 2, sim, &reg), STATUS_OK);

  const int counts[2] = {2, 2};
  ASSERT_EQ(registry_build_timetable(reg, counts, 3, 360, 390), STATUS_ERR_INVALID);
  ASSERT_EQ(registry_build_timetable(reg, counts, 2, -1, 390), STATUS_ERR_INVALID);
  ASSERT_EQ(registry_build_timetable(reg, counts, 2, 390, 390), STATUS_ERR_INVALID);
  ASSERT_EQ(registry_build_timetable(reg, counts, 2, 360, 1441), STATUS_ERR_INVALID);
  ASSERT_EQ(registry_build_timetable(NULL, counts, 2, 360, 390), STATUS_ERR_NULL);
  ASSERT_EQ(registry_build_timetable(reg, NULL, 2, 360, 390), STATUS_ERR_NULL);

  Trip tbuf[4];
  size_t n = 0;
  ASSERT_EQ(registry_trips(reg, 360, 390, 5, tbuf, 4, &n), STATUS_ERR_INVALID);
  ASSERT_EQ(registry_trips(reg, 390, 360, -1, tbuf, 4, &n), STATUS_ERR_INVALID);
  ASSERT_EQ(registry_trips(NULL, 360, 390, -1, tbuf, 4, &n), STATUS_ERR_NULL);

  JourneyRecord jbuf[4];
  ASSERT_EQ(registry_journeys(reg, 99, jbuf, 4, &n), STATUS_ERR_NOT_FOUND);
  ASSERT_EQ(registry_journeys(NULL, 0, jbuf, 4, &n), STATUS_ERR_NULL);

  RouteBoardRow rbuf[2];
  const double d[2] = {10.0, 20.0};
  ASSERT_EQ(registry_route_board(reg, d, counts, rbuf, 3), STATUS_ERR_INVALID);
  ASSERT_EQ(registry_route_board(NULL, d, counts, rbuf, 2), STATUS_ERR_NULL);

  /* Populate timetable and journey lists before destroy to verify zero leaks */
  ASSERT_EQ(registry_build_timetable(reg, counts, 2, 360, 480), STATUS_OK);
  registry_sim_observer(reg, 0, 0, 21600.0, 22280.0, 5);
  registry_sim_observer(reg, 1, 0, 21600.0, 22280.0, 3);

  registry_destroy(reg);
  sim_destroy(sim);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

int main(void) {
  RUN_TEST(test_reg_tree_structure);
  RUN_TEST(test_reg_hash_lookups);
  RUN_TEST(test_reg_timetable_worked_example);
  RUN_TEST(test_reg_replan_from);
  RUN_TEST(test_reg_sync_move_breakdown_reserve);
  RUN_TEST(test_reg_journeys_observer_and_cap);
  RUN_TEST(test_reg_route_board);
  RUN_TEST(test_reg_errors_and_memory);

  return test_summary();
}
