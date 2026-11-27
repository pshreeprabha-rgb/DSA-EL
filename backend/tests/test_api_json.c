#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __has_include
#  if __has_include(<cjson/cJSON.h>)
#    include <cjson/cJSON.h>
#  else
#    include <cJSON.h>
#  endif
#else
#  include <cjson/cJSON.h>
#endif

#include "api_app.h"
#include "api_json.h"

/* Minimal test harness matching other backend/tests suites */
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_assertions_run = 0;
static int g_assertions_failed = 0;
static int g_cur_failed = 0;

#define TEST_START(name)                                                       \
  do {                                                                         \
    printf("[RUN ] %s\n", (name));                                             \
    g_tests_run++;                                                             \
    g_cur_failed = 0;                                                          \
  } while (0)

#define TEST_END(name)                                                         \
  do {                                                                         \
    if (g_cur_failed == 0) {                                                   \
      printf("[PASS] %s\n", (name));                                           \
      g_tests_passed++;                                                        \
    } else {                                                                   \
      printf("[FAIL] %s\n", (name));                                           \
      g_tests_failed++;                                                        \
    }                                                                          \
  } while (0)

#define ASSERT_TRUE(cond)                                                      \
  do {                                                                         \
    g_assertions_run++;                                                        \
    if (!(cond)) {                                                             \
      fprintf(stderr, "  ASSERTION FAILED (%s:%d): %s\n", __FILE__, __LINE__,  \
              #cond);                                                          \
      g_assertions_failed++;                                                   \
      g_cur_failed = 1;                                                        \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    long long _va = (long long)(a);                                            \
    long long _vb = (long long)(b);                                            \
    g_assertions_run++;                                                        \
    if (_va != _vb) {                                                          \
      fprintf(stderr, "  ASSERTION FAILED (%s:%d): %s (%lld) != %s (%lld)\n",  \
              __FILE__, __LINE__, #a, _va, #b, _vb);                           \
      g_assertions_failed++;                                                   \
      g_cur_failed = 1;                                                        \
    }                                                                          \
  } while (0)

#define ASSERT_NEAR(a, b, eps)                                                 \
  do {                                                                         \
    double _da = (double)(a);                                                  \
    double _db = (double)(b);                                                  \
    g_assertions_run++;                                                        \
    if (isnan(_da) || isnan(_db) || fabs(_da - _db) > (eps)) {                 \
      fprintf(stderr, "  ASSERTION FAILED (%s:%d): %.6f != %.6f (tol %.6f)\n", \
              __FILE__, __LINE__, _da, _db, (double)(eps));                    \
      g_assertions_failed++;                                                   \
      g_cur_failed = 1;                                                        \
    }                                                                          \
  } while (0)

#define ASSERT_STR_EQ(a, b)                                                    \
  do {                                                                         \
    const char *_sa = (a);                                                     \
    const char *_sb = (b);                                                     \
    g_assertions_run++;                                                        \
    if (!_sa || !_sb || strcmp(_sa, _sb) != 0) {                               \
      fprintf(stderr, "  ASSERTION FAILED (%s:%d): \"%s\" != \"%s\"\n",        \
              __FILE__, __LINE__, _sa ? _sa : "(null)", _sb ? _sb : "(null)"); \
      g_assertions_failed++;                                                   \
      g_cur_failed = 1;                                                        \
    }                                                                          \
  } while (0)

#define ASSERT_NOT_NULL(p)                                                     \
  do {                                                                         \
    g_assertions_run++;                                                        \
    if ((p) == NULL) {                                                         \
      fprintf(stderr, "  ASSERTION FAILED (%s:%d): %s is NULL\n", __FILE__,    \
              __LINE__, #p);                                                   \
      g_assertions_failed++;                                                   \
      g_cur_failed = 1;                                                        \
    }                                                                          \
  } while (0)

#define RUN_TEST(fn)                                                           \
  do {                                                                         \
    fn();                                                                      \
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

static Graph *build_line_graph_fixture(DemandStop stops_out[8], int quote_stop3) {
  Graph *g = graph_create();
  if (!g) return NULL;

  double lat = 12.9716;
  double lng0 = 77.5946;

  for (int i = 0; i < 8; i++) {
    char name[64];
    if (quote_stop3 && i == 3) {
      snprintf(name, sizeof(name), "Stop \"Q\" \\ 3");
    } else {
      snprintf(name, sizeof(name), "Stop %d", i);
    }
    graph_add_node(g, (int64_t)(1000 + i), lat, lng0 + (double)i * 0.001, name, 1);
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
      stops_out[i].node = (int)i;
      stops_out[i].osm_id = (int64_t)(1000 + i);
      stops_out[i].type = (i < 4) ? STOP_OFFICE : STOP_RESIDENTIAL;
      stops_out[i].scale = 1.0;
      if (quote_stop3 && i == 3) {
        snprintf(stops_out[i].name, sizeof(stops_out[i].name), "Stop \"Q\" \\ 3");
      } else {
        snprintf(stops_out[i].name, sizeof(stops_out[i].name), "Stop %d", i);
      }
      stops_out[i].lat = lat;
      stops_out[i].lng = lng0 + (double)i * 0.001;
    }
  }
  return g;
}

static AppCtx *create_fixture_app(int quote_stop3) {
  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops, quote_stop3);
  if (!g) return NULL;

  NetworkConfig ncfg;
  network_config_default(&ncfg);
  ncfg.n_routes = 2;
  ncfg.dwell_s_per_stop = 20.0;
  ncfg.build_matrix = 1;
  NetworkModel *net = NULL;
  if (network_build(g, stops, 8, &ncfg, &net) != STATUS_OK) {
    graph_destroy(g);
    return NULL;
  }

  SimConfig scfg;
  sim_config_default(&scfg);
  scfg.arrivals_enabled = 0;
  scfg.dwell_s = 20.0;
  scfg.per_pax_s = 0.0;
  scfg.travel_noise = 0.0;
  scfg.bus_capacity = 100;
  scfg.n_buses = 4;
  scfg.n_reserve = 0;
  scfg.apply_traffic = 0;
  scfg.warmup_min = 0.0;
  scfg.slice_min = 15.0;
  scfg.start_min = 360.0;
  scfg.end_min = 1320.0;
  scfg.policy = SIM_POLICY_FIXED_EQUAL;
  scfg.initial_spacing = SIM_SPACING_BY_STOP;

  AppCtx *ctx = NULL;
  if (app_create_from_parts(g, net, stops, 8, &scfg, &ctx) != STATUS_OK) {
    network_destroy(net);
    graph_destroy(g);
    return NULL;
  }
  return ctx;
}

/* 1. State JSON: 4 running buses, bus 0 motion at stop 0, queues length 8, summary arrived 0 */
static void test_api_json_state_and_bus(void) {
  TEST_START("test_api_json_state_and_bus");

  AppCtx *ctx = create_fixture_app(0);
  ASSERT_NOT_NULL(ctx);

  char *json = NULL;
  ASSERT_EQ(api_json_state(ctx, &json), STATUS_OK);
  ASSERT_NOT_NULL(json);

  cJSON *root = cJSON_Parse(json);
  ASSERT_NOT_NULL(root);
  ASSERT_NEAR(cJSON_GetObjectItemCaseSensitive(root, "clock_min")->valuedouble, 360.0, 1e-6);
  ASSERT_STR_EQ(cJSON_GetObjectItemCaseSensitive(root, "clock")->valuestring, "06:00");
  ASSERT_TRUE(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "running")));

  cJSON *buses = cJSON_GetObjectItemCaseSensitive(root, "buses");
  ASSERT_EQ(cJSON_GetArraySize(buses), 4);
  for (int i = 0; i < 4; i++) {
    cJSON *b = cJSON_GetArrayItem(buses, i);
    ASSERT_STR_EQ(cJSON_GetObjectItemCaseSensitive(b, "state")->valuestring, "running");
  }

  /* Bus 0 starts stationary at stop 0 matching sim_bus_motion */
  SimBusMotion bm0;
  ASSERT_EQ(sim_bus_motion(app_sim_const(ctx), 0, &bm0), STATUS_OK);
  cJSON *b0 = cJSON_GetArrayItem(buses, 0);
  cJSON *m0 = cJSON_GetObjectItemCaseSensitive(b0, "motion");
  ASSERT_NOT_NULL(m0);
  ASSERT_TRUE(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m0, "moving")));
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(m0, "from_stop")->valueint, bm0.from_stop);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(m0, "from_stop")->valueint, 0);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(m0, "to_stop")->valueint, bm0.to_stop);

  cJSON *queues = cJSON_GetObjectItemCaseSensitive(root, "queues");
  ASSERT_EQ(cJSON_GetArraySize(queues), 8);

  cJSON *summary = cJSON_GetObjectItemCaseSensitive(root, "summary");
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(summary, "arrived")->valueint, 0);

  cJSON_Delete(root);
  free(json);

  /* Known bus vs unknown bus -> 404 */
  char *bjson = NULL;
  ASSERT_EQ(api_json_bus(ctx, 0, &bjson), STATUS_OK);
  cJSON *broot = cJSON_Parse(bjson);
  ASSERT_NOT_NULL(broot);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(broot, "id")->valueint, 0);
  ASSERT_STR_EQ(cJSON_GetObjectItemCaseSensitive(broot, "state")->valuestring, "running");
  cJSON_Delete(broot);
  free(bjson);

  char *unknown_b = NULL;
  ASSERT_EQ(api_json_bus(ctx, 99, &unknown_b), STATUS_ERR_NOT_FOUND);
  ASSERT_EQ(api_json_bus(ctx, -1, &unknown_b), STATUS_ERR_NOT_FOUND);

  app_destroy(ctx);
  TEST_END("test_api_json_state_and_bus");
}

/* 2. Routes JSON: 2 routes, board priority order, cycle_min 11.33 (680 s / 60) */
static void test_api_json_routes(void) {
  TEST_START("test_api_json_routes");

  AppCtx *ctx = create_fixture_app(0);
  ASSERT_NOT_NULL(ctx);

  char *json = NULL;
  ASSERT_EQ(api_json_routes(ctx, &json), STATUS_OK);
  ASSERT_NOT_NULL(json);

  cJSON *root = cJSON_Parse(json);
  ASSERT_NOT_NULL(root);
  ASSERT_STR_EQ(cJSON_GetObjectItemCaseSensitive(root, "basis")->valuestring, "oracle");

  cJSON *routes = cJSON_GetObjectItemCaseSensitive(root, "routes");
  ASSERT_EQ(cJSON_GetArraySize(routes), 2);
  for (int i = 0; i < 2; i++) {
    cJSON *r = cJSON_GetArrayItem(routes, i);
    /* Line graph cycle: 6 edges * 100 s + 4 stops * 20 s = 680 s = 11.3333 min */
    ASSERT_NEAR(cJSON_GetObjectItemCaseSensitive(r, "cycle_min")->valuedouble, 11.3333, 0.01);
  }

  cJSON *board = cJSON_GetObjectItemCaseSensitive(root, "board");
  ASSERT_EQ(cJSON_GetArraySize(board), 2);
  double p0 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(board, 0), "priority")->valuedouble;
  double p1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(board, 1), "priority")->valuedouble;
  ASSERT_TRUE(p0 >= p1);

  cJSON_Delete(root);
  free(json);
  app_destroy(ctx);
  TEST_END("test_api_json_routes");
}

/* 3. Schedule JSON: 360..390 worked example returns exact 12 trips in order */
static void test_api_json_schedule_worked_example(void) {
  TEST_START("test_api_json_schedule_worked_example");

  AppCtx *ctx = create_fixture_app(0);
  ASSERT_NOT_NULL(ctx);

  int counts[2] = {2, 2};
  ASSERT_EQ(registry_build_timetable(app_registry(ctx), counts, 2, 360, 390), STATUS_OK);

  char *json = NULL;
  ASSERT_EQ(api_json_schedule(ctx, 1, 360, 1, 390, 1, -1, &json), STATUS_OK);
  ASSERT_NOT_NULL(json);

  cJSON *root = cJSON_Parse(json);
  ASSERT_NOT_NULL(root);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(root, "count")->valueint, 12);

  cJSON *avl = cJSON_GetObjectItemCaseSensitive(root, "avl");
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(avl, "size")->valueint, 12);
  ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(avl, "balanced")));

  static const int exp_dep[12] = {360, 360, 366, 366, 371, 371, 377, 377, 382, 382, 388, 388};
  static const int exp_id[12] = {1, 7, 4, 10, 2, 8, 5, 11, 3, 9, 6, 12};

  cJSON *trips = cJSON_GetObjectItemCaseSensitive(root, "trips");
  ASSERT_EQ(cJSON_GetArraySize(trips), 12);
  for (int i = 0; i < 12; i++) {
    cJSON *t = cJSON_GetArrayItem(trips, i);
    ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(t, "departure_min")->valueint, exp_dep[i]);
    ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(t, "trip_id")->valueint, exp_id[i]);
  }

  cJSON_Delete(root);
  free(json);
  app_destroy(ctx);
  TEST_END("test_api_json_schedule_worked_example");
}

/* 4. Tree, stops (with JSON escaping), graph, geometry, history, alerts, metrics */
static void test_api_json_tree_stops_and_escaping(void) {
  TEST_START("test_api_json_tree_stops_and_escaping");

  AppCtx *ctx = create_fixture_app(1); /* Stop 3 contains quotes and backslashes */
  ASSERT_NOT_NULL(ctx);

  /* Tree: 1 city + 2 depots + 2 routes + 4 buses = 9 nodes, height 3 */
  char *tjson = NULL;
  ASSERT_EQ(api_json_tree(ctx, &tjson), STATUS_OK);
  cJSON *troot = cJSON_Parse(tjson);
  ASSERT_NOT_NULL(troot);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(troot, "count")->valueint, 9);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(troot, "height")->valueint, 3);
  cJSON *tnodes = cJSON_GetObjectItemCaseSensitive(troot, "nodes");
  ASSERT_EQ(cJSON_GetArraySize(tnodes), 9);
  cJSON_Delete(troot);
  free(tjson);

  /* Stops: 8 entries with route, pos, and unescaped roundtrip on stop 3 */
  char *sjson = NULL;
  ASSERT_EQ(api_json_stops(ctx, &sjson), STATUS_OK);
  ASSERT_TRUE(strstr(sjson, "\\\"Q\\\"") != NULL);
  ASSERT_TRUE(strstr(sjson, "\\\\") != NULL);
  cJSON *sroot = cJSON_Parse(sjson);
  ASSERT_NOT_NULL(sroot);
  cJSON *stops = cJSON_GetObjectItemCaseSensitive(sroot, "stops");
  ASSERT_EQ(cJSON_GetArraySize(stops), 8);
  for (int i = 0; i < 8; i++) {
    cJSON *st = cJSON_GetArrayItem(stops, i);
    ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(st, "route")->valueint, (i < 4) ? 0 : 1);
    ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(st, "pos")->valueint, i % 4);
  }
  cJSON *st3 = cJSON_GetArrayItem(stops, 3);
  ASSERT_STR_EQ(cJSON_GetObjectItemCaseSensitive(st3, "name")->valuestring, "Stop \"Q\" \\ 3");
  cJSON_Delete(sroot);
  free(sjson);

  /* Graph & geometry & metrics & alerts & history sanity */
  char *gjson = NULL, *geom = NULL, *met = NULL, *al = NULL, *hist = NULL;
  ASSERT_EQ(api_json_graph(ctx, &gjson), STATUS_OK);
  ASSERT_EQ(api_json_geometry(ctx, &geom), STATUS_OK);
  ASSERT_EQ(api_json_metrics(ctx, &met), STATUS_OK);
  ASSERT_EQ(api_json_alerts(ctx, &al), STATUS_OK);
  ASSERT_EQ(api_json_history(ctx, &hist), STATUS_OK);

  cJSON *gr = cJSON_Parse(gjson);
  ASSERT_EQ(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(gr, "nodes")), 8);
  ASSERT_EQ(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(gr, "edges")), 14);
  cJSON_Delete(gr);

  cJSON *geo = cJSON_Parse(geom);
  ASSERT_EQ(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(geo, "legs")), 8);
  cJSON_Delete(geo);

  cJSON *mr = cJSON_Parse(met);
  ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(mr, "sim"));
  ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(mr, "ml")));
  cJSON_Delete(mr);

  cJSON *ar = cJSON_Parse(al);
  ASSERT_EQ(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(ar, "flags")), 0);
  cJSON_Delete(ar);

  cJSON *hr = cJSON_Parse(hist);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(hr, "size")->valueint, 0);
  cJSON_Delete(hr);

  free(gjson); free(geom); free(met); free(al); free(hist);
  app_destroy(ctx);
  TEST_END("test_api_json_tree_stops_and_escaping");
}

/* 5. Forecast: 24 hours, oracle == demand_expected, ma/rf null when absent */
static void test_api_json_forecast(void) {
  TEST_START("test_api_json_forecast");

  AppCtx *ctx = create_fixture_app(0);
  ASSERT_NOT_NULL(ctx);

  char *json = NULL;
  ASSERT_EQ(api_json_forecast(ctx, 2, &json), STATUS_OK);
  cJSON *root = cJSON_Parse(json);
  ASSERT_NOT_NULL(root);
  cJSON *hours = cJSON_GetObjectItemCaseSensitive(root, "hours");
  ASSERT_EQ(cJSON_GetArraySize(hours), 24);

  for (int h = 0; h < 24; h++) {
    cJSON *item = cJSON_GetArrayItem(hours, h);
    ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(item, "hour")->valueint, h);
    double expected_oracle = demand_expected(STOP_OFFICE, 1.0, h, DAY_WEEKDAY, 0, 0);
    ASSERT_NEAR(cJSON_GetObjectItemCaseSensitive(item, "oracle")->valuedouble,
                expected_oracle, 1e-6);
    ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(item, "ma")));
    ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(item, "rf")));
  }

  cJSON_Delete(root);
  free(json);
  app_destroy(ctx);
  TEST_END("test_api_json_forecast");
}

/* 6. Strict parsing, sim_control validation, and HTTP error mapping */
static void test_api_strict_parsing_and_error_mapping(void) {
  TEST_START("test_api_strict_parsing_and_error_mapping");

  int v = -1;
  ASSERT_EQ(api_parse_int_strict("42", 0, 100, &v), STATUS_OK);
  ASSERT_EQ(v, 42);
  ASSERT_EQ(api_parse_int_strict("-1", -1, 10, &v), STATUS_OK);
  ASSERT_EQ(v, -1);

  /* Reject non-numeric, negative out-of-range, overflow, trailing garbage, whitespace */
  ASSERT_EQ(api_parse_int_strict("abc", 0, 100, &v), STATUS_ERR_INVALID);
  ASSERT_EQ(api_parse_int_strict("-5", 0, 100, &v), STATUS_ERR_INVALID);
  ASSERT_EQ(api_parse_int_strict("999999999999999999999", 0, 100, &v), STATUS_ERR_INVALID);
  ASSERT_EQ(api_parse_int_strict("12abc", 0, 100, &v), STATUS_ERR_INVALID);
  ASSERT_EQ(api_parse_int_strict(" 12", 0, 100, &v), STATUS_ERR_INVALID);
  ASSERT_EQ(api_parse_int_strict("", 0, 100, &v), STATUS_ERR_INVALID);
  ASSERT_EQ(api_parse_int_strict(NULL, 0, 100, &v), STATUS_ERR_NULL);

  double d = 0.0;
  ASSERT_EQ(api_parse_double_strict("60.5", 1.0, 3600.0, &d), STATUS_OK);
  ASSERT_NEAR(d, 60.5, 1e-9);
  ASSERT_EQ(api_parse_double_strict("0.5", 1.0, 3600.0, &d), STATUS_ERR_INVALID);
  ASSERT_EQ(api_parse_double_strict("12x", 1.0, 3600.0, &d), STATUS_ERR_INVALID);
  ASSERT_EQ(api_parse_double_strict("NaN", 1.0, 3600.0, &d), STATUS_ERR_INVALID);

  /* HTTP error mapping table */
  const char *code = NULL;
  ASSERT_EQ(api_http_status_from_error(STATUS_ERR_INVALID, &code), 400);
  ASSERT_STR_EQ(code, "invalid_argument");
  ASSERT_EQ(api_http_status_from_error(STATUS_ERR_NOT_FOUND, &code), 404);
  ASSERT_STR_EQ(code, "not_found");
  ASSERT_EQ(api_http_status_from_error(STATUS_ERR_EMPTY, &code), 409);
  ASSERT_STR_EQ(code, "conflict");
  ASSERT_EQ(api_http_status_from_error(STATUS_ERR_DUPLICATE, &code), 409);
  ASSERT_STR_EQ(code, "conflict");
  ASSERT_EQ(api_http_status_from_error(STATUS_ERR_NULL, &code), 500);
  ASSERT_STR_EQ(code, "internal");
  ASSERT_EQ(api_http_status_from_error(STATUS_ERR_NOMEM, &code), 500);
  ASSERT_STR_EQ(code, "internal");

  /* Control body parser */
  char act[32];
  int ha = 0, hs = 0;
  double spd = 0.0;
  ASSERT_EQ(api_json_sim_control_parse("{\"action\":\"start\",\"speed\":120}",
                                       act, sizeof(act), &ha, &hs, &spd), STATUS_OK);
  ASSERT_EQ(ha, 1);
  ASSERT_STR_EQ(act, "start");
  ASSERT_EQ(hs, 1);
  ASSERT_NEAR(spd, 120.0, 1e-6);

  ASSERT_EQ(api_json_sim_control_parse("{\"action\":\"bogus\"}",
                                       act, sizeof(act), &ha, &hs, &spd), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_sim_control_parse("{\"action\":\"start\",\"speed\":5000}",
                                       act, sizeof(act), &ha, &hs, &spd), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_sim_control_parse("not json",
                                       act, sizeof(act), &ha, &hs, &spd), STATUS_ERR_INVALID);

  app_destroy(NULL);
  TEST_END("test_api_strict_parsing_and_error_mapping");
}

int main(void) {
  printf("========================================\n");
  printf("Running API JSON & App Unit Tests\n");
  printf("========================================\n");

  RUN_TEST(test_api_json_state_and_bus);
  RUN_TEST(test_api_json_routes);
  RUN_TEST(test_api_json_schedule_worked_example);
  RUN_TEST(test_api_json_tree_stops_and_escaping);
  RUN_TEST(test_api_json_forecast);
  RUN_TEST(test_api_strict_parsing_and_error_mapping);

  return test_summary();
}

