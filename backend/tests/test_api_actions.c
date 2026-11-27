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

static Graph *build_line_graph_fixture(DemandStop stops_out[8]) {
  Graph *g = graph_create();
  if (!g) return NULL;

  double lat = 12.9716;
  double lng0 = 77.5946;

  for (int i = 0; i < 8; i++) {
    char name[32];
    snprintf(name, sizeof(name), "Stop %d", i);
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
      snprintf(stops_out[i].name, sizeof(stops_out[i].name), "Stop %d", i);
      stops_out[i].lat = lat;
      stops_out[i].lng = lng0 + (double)i * 0.001;
    }
  }
  return g;
}

static AppCtx *create_fixture_app(int n_buses) {
  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
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
  scfg.n_buses = n_buses;
  scfg.n_reserve = 0;
  scfg.apply_traffic = 0;
  scfg.anomaly_detect = 1;
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

/* 1. Scenario rain_on, block_edge by route & leg, geometry_version bump, undo & second undo 409 */
static void test_actions_scenario_block_and_undo(void) {
  TEST_START("test_actions_scenario_block_and_undo");

  AppCtx *ctx = create_fixture_app(4);
  ASSERT_NOT_NULL(ctx);

  uint64_t v0 = app_geometry_version(ctx);

  /* rain_on at now = 360 schedules at 360 */
  char *resp = NULL;
  ASSERT_EQ(api_json_handle_scenario(ctx, "{\"kind\":\"rain_on\"}", &resp), STATUS_OK);
  ASSERT_NOT_NULL(resp);
  cJSON *r1 = cJSON_Parse(resp);
  ASSERT_NOT_NULL(r1);
  ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r1, "scheduled")));
  ASSERT_NEAR(cJSON_GetObjectItemCaseSensitive(r1, "time_min")->valuedouble, 360.0, 1e-6);
  ASSERT_STR_EQ(cJSON_GetObjectItemCaseSensitive(r1, "kind")->valuestring, "rain_on");
  cJSON_Delete(r1);
  free(resp);
  resp = NULL;

  /* Advance one slice (375): rain is now visible */
  ASSERT_EQ(sim_run_until(app_sim(ctx), 375.0), STATUS_OK);
  ASSERT_EQ(sim_rain_active(app_sim_const(ctx)), 1);

  /* block_edge by route=0 leg=0 at 375: after running to 376, path becomes unreachable & version bumps */
  ASSERT_EQ(api_json_handle_scenario(ctx, "{\"kind\":\"block_edge\",\"route\":0,\"leg\":0}", &resp), STATUS_OK);
  cJSON *r2 = cJSON_Parse(resp);
  ASSERT_NEAR(cJSON_GetObjectItemCaseSensitive(r2, "time_min")->valuedouble, 375.0, 1e-6);
  cJSON_Delete(r2);
  free(resp);
  resp = NULL;

  ASSERT_EQ(sim_run_until(app_sim(ctx), 376.0), STATUS_OK);
  app_sync_after_step(ctx);
  uint64_t v1 = app_geometry_version(ctx);
  ASSERT_TRUE(v1 > v0);
  const PathResult *p_blocked = network_leg_path(app_network(ctx), 0, 0);
  ASSERT_NOT_NULL(p_blocked);
  ASSERT_EQ(p_blocked->found, 0);

  /* Undo restores leg path and bumps geometry_version again */
  ASSERT_EQ(api_json_handle_undo(ctx, "{}", &resp), STATUS_OK);
  cJSON *u1 = cJSON_Parse(resp);
  ASSERT_NOT_NULL(u1);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(u1, "history_size")->valueint, 0);
  uint64_t v2 = (uint64_t)cJSON_GetObjectItemCaseSensitive(u1, "geometry_version")->valuedouble;
  ASSERT_TRUE(v2 > v1);
  cJSON_Delete(u1);
  free(resp);
  resp = NULL;

  const PathResult *p_restored = network_leg_path(app_network(ctx), 0, 0);
  ASSERT_NOT_NULL(p_restored);
  ASSERT_EQ(p_restored->found, 1);

  /* Second undo -> STATUS_ERR_EMPTY (HTTP 409) */
  Status st_u2 = api_json_handle_undo(ctx, NULL, &resp);
  ASSERT_EQ(st_u2, STATUS_ERR_EMPTY);
  ASSERT_EQ(api_http_status_from_error(st_u2, NULL), 409);

  /* Scenario at end_min (1320) -> STATUS_ERR_EMPTY (HTTP 409) */
  ASSERT_EQ(app_sim_control_ext(ctx, "seek", 0, 0.0, 1, 1320.0), STATUS_OK);
  Status st_end = api_json_handle_scenario(ctx, "{\"kind\":\"rain_on\"}", &resp);
  ASSERT_EQ(st_end, STATUS_ERR_EMPTY);
  ASSERT_EQ(api_http_status_from_error(st_end, NULL), 409);

  app_destroy(ctx);
  TEST_END("test_actions_scenario_block_and_undo");
}

/* 2. Allocate (policy + force, unloaded predictor 400) & manual_move + error cases */
static void test_actions_allocate_and_manual_move(void) {
  TEST_START("test_actions_allocate_and_manual_move");

  AppCtx *ctx = create_fixture_app(4);
  ASSERT_NOT_NULL(ctx);

  /* Seek to 09:00 (540): STOP_OFFICE stops 0..3 dominate STOP_RESIDENTIAL stops 4..7 */
  ASSERT_EQ(app_sim_control_ext(ctx, "seek", 0, 0.0, 1, 540.0), STATUS_OK);

  char *resp = NULL;
  ASSERT_EQ(api_json_handle_allocate(ctx,
      "{\"policy\":\"greedy_spec\",\"predictor\":\"oracle\",\"force\":true}", &resp), STATUS_OK);
  ASSERT_NOT_NULL(resp);
  cJSON *ar = cJSON_Parse(resp);
  ASSERT_NOT_NULL(ar);
  ASSERT_STR_EQ(cJSON_GetObjectItemCaseSensitive(ar, "policy")->valuestring, "greedy_spec");
  cJSON *dec = cJSON_GetObjectItemCaseSensitive(ar, "decision");
  ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dec, "forced")));
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(dec, "moves_applied")->valueint, 1);
  cJSON_Delete(ar);
  free(resp);
  resp = NULL;

  /* Unloaded model ('ma' or 'rf') or unknown predictor -> 400 (STATUS_ERR_INVALID) */
  ASSERT_EQ(api_json_handle_allocate(ctx,
      "{\"policy\":\"equal\",\"predictor\":\"rf\",\"force\":false}", &resp), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_allocate(ctx,
      "{\"policy\":\"equal\",\"predictor\":\"ma\",\"force\":false}", &resp), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_allocate(ctx,
      "{\"policy\":\"equal\",\"predictor\":\"unknown_model\",\"force\":false}", &resp), STATUS_ERR_INVALID);

  /* Reset back to clean [2, 2], then request manual move bus 0 -> route 1 */
  ASSERT_EQ(app_sim_control_ext(ctx, "reset", 0, 0.0, 0, 0.0), STATUS_OK);
  ASSERT_EQ(api_json_handle_allocate(ctx, "{\"manual_move\":{\"bus\":0,\"to_route\":1}}", &resp), STATUS_OK);
  cJSON *mr = cJSON_Parse(resp);
  ASSERT_NOT_NULL(mr);
  cJSON *mv = cJSON_GetObjectItemCaseSensitive(mr, "moved");
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(mv, "bus")->valueint, 0);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(mv, "to_route")->valueint, 1);
  cJSON_Delete(mr);
  free(resp);
  resp = NULL;

  /* Manual move errors: already pending, same route, bad bus, bad route */
  ASSERT_EQ(api_json_handle_allocate(ctx, "{\"manual_move\":{\"bus\":0,\"to_route\":1}}", &resp), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_allocate(ctx, "{\"manual_move\":{\"bus\":1,\"to_route\":0}}", &resp), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_allocate(ctx, "{\"manual_move\":{\"bus\":99,\"to_route\":1}}", &resp), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_allocate(ctx, "{\"manual_move\":{\"bus\":1,\"to_route\":5}}", &resp), STATUS_ERR_INVALID);

  app_destroy(ctx);
  TEST_END("test_actions_allocate_and_manual_move");
}

/* 3. Inject passenger queue & route/optimize (astar vs dijkstra nodes & equal cost) */
static void test_actions_inject_and_route_optimize(void) {
  TEST_START("test_actions_inject_and_route_optimize");

  AppCtx *ctx = create_fixture_app(4);
  ASSERT_NOT_NULL(ctx);

  ASSERT_EQ(sim_queue_length(app_sim(ctx), 2), 0);

  char *resp = NULL;
  ASSERT_EQ(api_json_handle_inject(ctx, "{\"stop\":2,\"count\":25,\"ride\":1}", &resp), STATUS_OK);
  cJSON *ir = cJSON_Parse(resp);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(ir, "stop")->valueint, 2);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(ir, "count")->valueint, 25);
  cJSON_Delete(ir);
  free(resp);
  resp = NULL;

  ASSERT_EQ(sim_queue_length(app_sim(ctx), 2), 25);

  /* Inject invalid counts */
  ASSERT_EQ(api_json_handle_inject(ctx, "{\"stop\":2,\"count\":0}", &resp), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_inject(ctx, "{\"stop\":2,\"count\":501}", &resp), STATUS_ERR_INVALID);

  /* Route optimize: stop 0 to stop 7 along 8-node line = 7 edges * 100 s = 700 s */
  ASSERT_EQ(api_json_handle_route_optimize(ctx,
      "{\"from_stop\":0,\"to_stop\":7,\"algo\":\"both\",\"mode\":\"traffic\"}", &resp), STATUS_OK);
  cJSON *oroot = cJSON_Parse(resp);
  ASSERT_NOT_NULL(oroot);
  ASSERT_STR_EQ(cJSON_GetObjectItemCaseSensitive(oroot, "mode")->valuestring, "traffic");
  cJSON *oa = cJSON_GetObjectItemCaseSensitive(oroot, "astar");
  cJSON *od = cJSON_GetObjectItemCaseSensitive(oroot, "dijkstra");
  ASSERT_NOT_NULL(oa);
  ASSERT_NOT_NULL(od);
  ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(oa, "found")));
  ASSERT_NEAR(cJSON_GetObjectItemCaseSensitive(oa, "time_s")->valuedouble, 700.0, 1e-6);
  ASSERT_NEAR(cJSON_GetObjectItemCaseSensitive(od, "time_s")->valuedouble, 700.0, 1e-6);
  ASSERT_TRUE(cJSON_GetObjectItemCaseSensitive(oa, "nodes_explored")->valueint <=
              cJSON_GetObjectItemCaseSensitive(od, "nodes_explored")->valueint);
  ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(oroot, "nodes_ratio"));
  cJSON_Delete(oroot);
  free(resp);
  resp = NULL;

  /* Verify /api/metrics populates last_optimize */
  ASSERT_EQ(api_json_metrics(ctx, &resp), STATUS_OK);
  cJSON *m = cJSON_Parse(resp);
  ASSERT_TRUE(!cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(m, "last_optimize")));
  cJSON_Delete(m);
  free(resp);
  resp = NULL;

  /* algo = "astar" only omits dijkstra & nodes_ratio */
  ASSERT_EQ(api_json_handle_route_optimize(ctx,
      "{\"from_stop\":0,\"to_stop\":3,\"algo\":\"astar\",\"mode\":\"static\"}", &resp), STATUS_OK);
  cJSON *ao = cJSON_Parse(resp);
  ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(ao, "astar"));
  ASSERT_TRUE(cJSON_GetObjectItemCaseSensitive(ao, "dijkstra") == NULL);
  ASSERT_TRUE(cJSON_GetObjectItemCaseSensitive(ao, "nodes_ratio") == NULL);
  cJSON_Delete(ao);
  free(resp);

  app_destroy(ctx);
  TEST_END("test_actions_inject_and_route_optimize");
}

/* 4. Seek 480 & reset restores initial state identically */
static void test_actions_reset_and_seek(void) {
  TEST_START("test_actions_reset_and_seek");

  AppCtx *ctx = create_fixture_app(4);
  ASSERT_NOT_NULL(ctx);

  char *init_state_str = NULL;
  ASSERT_EQ(api_json_state(ctx, &init_state_str), STATUS_OK);
  cJSON *init_st = cJSON_Parse(init_state_str);
  ASSERT_NOT_NULL(init_st);

  /* Seek 480 */
  ASSERT_EQ(app_sim_control_ext(ctx, "seek", 0, 0.0, 1, 480.0), STATUS_OK);
  ASSERT_NEAR(sim_now_min(app_sim_const(ctx)), 480.0, 1e-6);

  /* Mutate state */
  char *tmp = NULL;
  ASSERT_EQ(api_json_handle_inject(ctx, "{\"stop\":1,\"count\":15}", &tmp), STATUS_OK);
  free(tmp); tmp = NULL;
  ASSERT_EQ(api_json_handle_route_optimize(ctx,
      "{\"from_stop\":0,\"to_stop\":4,\"algo\":\"astar\",\"mode\":\"static\"}", &tmp), STATUS_OK);
  free(tmp); tmp = NULL;

  /* Invalid seek values rejected */
  ASSERT_EQ(app_sim_control_ext(ctx, "seek", 0, 0.0, 1, 481.0), STATUS_ERR_INVALID);
  ASSERT_EQ(app_sim_control_ext(ctx, "seek", 0, 0.0, 1, 1440.0), STATUS_ERR_INVALID);

  /* Reset returns to initial state */
  ASSERT_EQ(app_sim_control_ext(ctx, "reset", 0, 0.0, 0, 0.0), STATUS_OK);

  char *after_reset_str = NULL;
  ASSERT_EQ(api_json_state(ctx, &after_reset_str), STATUS_OK);
  cJSON *after_st = cJSON_Parse(after_reset_str);
  ASSERT_NOT_NULL(after_st);

  ASSERT_NEAR(cJSON_GetObjectItemCaseSensitive(after_st, "clock_min")->valuedouble,
              cJSON_GetObjectItemCaseSensitive(init_st, "clock_min")->valuedouble, 1e-6);
  ASSERT_TRUE(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(after_st, "running")));
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(after_st, "history_size")->valueint, 0);
  ASSERT_TRUE(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(init_st, "buses"),
                            cJSON_GetObjectItemCaseSensitive(after_st, "buses"), 1));
  ASSERT_TRUE(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(init_st, "queues"),
                            cJSON_GetObjectItemCaseSensitive(after_st, "queues"), 1));
  ASSERT_TRUE(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(init_st, "summary"),
                            cJSON_GetObjectItemCaseSensitive(after_st, "summary"), 1));

  cJSON_Delete(init_st);
  cJSON_Delete(after_st);
  free(init_state_str);
  free(after_reset_str);
  app_destroy(ctx);
  TEST_END("test_actions_reset_and_seek");
}

/* 5. Eval endpoint (404 missing, 500 broken JSON, 200 valid) & hardening invalid inputs */
static void test_actions_eval_and_hardening(void) {
  TEST_START("test_actions_eval_and_hardening");

  AppCtx *ctx = create_fixture_app(4);
  ASSERT_NOT_NULL(ctx);

  /* Missing file -> 404 */
  ASSERT_EQ(app_set_eval_path_for_test(ctx, "/tmp/nonexistent_transport_eval_404.json"), STATUS_OK);
  char *out = NULL;
  Status st_miss = api_json_eval(ctx, &out);
  ASSERT_EQ(st_miss, STATUS_ERR_NOT_FOUND);
  ASSERT_EQ(api_http_status_from_error(st_miss, NULL), 404);

  /* Broken JSON temp file -> 500 */
  const char *bad_path = "/tmp/test_api_actions_bad_eval.json";
  FILE *fp = fopen(bad_path, "wb");
  ASSERT_NOT_NULL(fp);
  fputs("{broken json", fp);
  fclose(fp);
  ASSERT_EQ(app_set_eval_path_for_test(ctx, bad_path), STATUS_OK);
  Status st_bad = api_json_eval(ctx, &out);
  ASSERT_EQ(api_http_status_from_error(st_bad, NULL), 500);
  remove(bad_path);

  /* Valid JSON temp file -> 200 */
  const char *ok_path = "/tmp/test_api_actions_ok_eval.json";
  fp = fopen(ok_path, "wb");
  ASSERT_NOT_NULL(fp);
  fputs("{\"seeds\":30,\"status\":\"ok\"}\n", fp);
  fclose(fp);
  ASSERT_EQ(app_set_eval_path_for_test(ctx, ok_path), STATUS_OK);
  ASSERT_EQ(api_json_eval(ctx, &out), STATUS_OK);
  cJSON *eo = cJSON_Parse(out);
  ASSERT_EQ(cJSON_GetObjectItemCaseSensitive(eo, "seeds")->valueint, 30);
  cJSON_Delete(eo);
  free(out);
  out = NULL;
  remove(ok_path);

  /* Hardening: invalid JSON, trailing garbage, unknown kind, negative IDs, huge nums, NaN-like strings -> 400 */
  ASSERT_EQ(api_json_handle_scenario(ctx, "not json", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_scenario(ctx, "{\"kind\":\"rain_on\"} trailing", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_scenario(ctx, "{\"kind\":\"alien_invasion\"}", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_scenario(ctx, "{\"kind\":\"event_on\",\"stop\":-1}", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_scenario(ctx, "{\"kind\":\"breakdown\",\"bus\":99999999999999}", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_scenario(ctx, "{\"kind\":\"event_on\",\"stop\":1.5}", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_inject(ctx, "{\"stop\":0,\"count\":\"NaN\"}", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_inject(ctx, "{\"stop\":-2,\"count\":10}", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_route_optimize(ctx, "{\"from_stop\":0,\"to_stop\":1e20}", &out), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_handle_undo(ctx, "{not json", &out), STATUS_ERR_INVALID);

  char act[32]; int ha = 0, hs = 0, ht = 0; double spd = 0.0, tmin = 0.0;
  ASSERT_EQ(api_json_sim_control_parse_ext("{\"action\":\"seek\",\"time_min\":\"NaN\"}",
                                           act, sizeof(act), &ha, &hs, &spd, &ht, &tmin), STATUS_ERR_INVALID);
  ASSERT_EQ(api_json_sim_control_parse_ext("{\"action\":\"seek\",\"time_min\":999999}",
                                           act, sizeof(act), &ha, &hs, &spd, &ht, &tmin), STATUS_ERR_INVALID);

  app_destroy(ctx);
  TEST_END("test_actions_eval_and_hardening");
}

int main(void) {
  printf("========================================\n");
  printf("Running API Action & Control Unit Tests\n");
  printf("========================================\n");

  RUN_TEST(test_actions_scenario_block_and_undo);
  RUN_TEST(test_actions_allocate_and_manual_move);
  RUN_TEST(test_actions_inject_and_route_optimize);
  RUN_TEST(test_actions_reset_and_seek);
  RUN_TEST(test_actions_eval_and_hardening);

  return test_summary();
}

