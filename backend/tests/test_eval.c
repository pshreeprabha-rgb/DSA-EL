/**
 * @file test_eval.c
 * @brief Unit and integration tests for the Part 3C-2 evaluation module.
 */

#include "anomaly.h"
#include "ai_demand.h"
#include "demand_model.h"
#include "eval.h"
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

/* 1. t_crit and eval_mean_sd hand values and errors */
static void test_eval_t_crit_and_mean_sd(void) {
  TEST_START("test_eval_t_crit_and_mean_sd");

  /* Hand check against table in specification */
  ASSERT_APPROX(eval_t_crit_975(1), 12.706, 1e-9);
  ASSERT_APPROX(eval_t_crit_975(4), 2.776, 1e-9);
  ASSERT_APPROX(eval_t_crit_975(29), 2.045, 1e-9);
  ASSERT_APPROX(eval_t_crit_975(35), 2.042, 1e-9);
  ASSERT_APPROX(eval_t_crit_975(60), 2.000, 1e-9);
  ASSERT_APPROX(eval_t_crit_975(90), 2.000, 1e-9);
  ASSERT_APPROX(eval_t_crit_975(120), 1.980, 1e-9);
  ASSERT_APPROX(eval_t_crit_975(1000), 1.960, 1e-9);
  ASSERT_TRUE(isnan(eval_t_crit_975(0)));

  /* Hand derivation: x = {2, 4, 4, 4, 5, 5, 7, 9}, sum = 40, mean = 5.0,
   * sq = 9+1+1+1+0+0+4+16 = 32, sample var = 32/7, sd = sqrt(32/7) = 2.1380899 */
  const double x[8] = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
  double mean = 0.0, sd = 0.0;
  ASSERT_EQ(eval_mean_sd(x, 8, &mean, &sd), STATUS_OK);
  ASSERT_APPROX(mean, 5.0, 1e-9);
  ASSERT_APPROX(sd, sqrt(32.0 / 7.0), 1e-9);

  ASSERT_EQ(eval_mean_sd(NULL, 8, &mean, &sd), STATUS_ERR_NULL);
  ASSERT_EQ(eval_mean_sd(x, 8, NULL, &sd), STATUS_ERR_NULL);
  ASSERT_EQ(eval_mean_sd(x, 8, &mean, NULL), STATUS_ERR_NULL);
  ASSERT_EQ(eval_mean_sd(x, 1, &mean, &sd), STATUS_ERR_INVALID);

  const double bad[2] = {1.0, NAN};
  ASSERT_EQ(eval_mean_sd(bad, 2, &mean, &sd), STATUS_ERR_INVALID);

  TEST_PASS();
}

/* 2. eval_paired_stats hand values and errors */
static void test_eval_paired_stats_hand_values(void) {
  TEST_START("test_eval_paired_stats_hand_values");

  /*
   * Hand derivation:
   * base  = {10, 10, 10, 10, 10}
   * other = { 9,  8, 12,  7,  9}
   * diffs = {-1, -2,  2, -3, -1}, sum = -5, mean = -1.0
   * deviations from -1: {0, -1, 3, -2, 0}, sq = 0+1+9+4+0 = 14
   * sd = sqrt(14/4) = 1.870828693, se = sd/sqrt(5) = 0.836660027
   * t_crit(4) = 2.776 -> half = 2.322568234
   * CI = [-3.322568, 1.322568], wins = 4, losses = 1, ties = 0.
   */
  const double base[5] = {10.0, 10.0, 10.0, 10.0, 10.0};
  const double other[5] = {9.0, 8.0, 12.0, 7.0, 9.0};
  EvalStats es;
  ASSERT_EQ(eval_paired_stats(base, other, 5, &es), STATUS_OK);
  ASSERT_EQ(es.n, 5);
  ASSERT_APPROX(es.mean, -1.0, 1e-9);
  ASSERT_APPROX(es.sd, 1.870829, 1e-5);
  ASSERT_APPROX(es.se, 0.836660, 1e-5);
  ASSERT_APPROX(es.ci_lo, -3.32257, 1e-4);
  ASSERT_APPROX(es.ci_hi, 1.32257, 1e-4);
  ASSERT_EQ(es.wins, 4);
  ASSERT_EQ(es.losses, 1);
  ASSERT_EQ(es.ties, 0);

  /* All diffs == -2 -> sd = 0, CI [-2, -2], wins 5 */
  const double eq_other[5] = {8.0, 8.0, 8.0, 8.0, 8.0};
  ASSERT_EQ(eval_paired_stats(base, eq_other, 5, &es), STATUS_OK);
  ASSERT_APPROX(es.sd, 0.0, 1e-12);
  ASSERT_APPROX(es.ci_lo, -2.0, 1e-12);
  ASSERT_APPROX(es.ci_hi, -2.0, 1e-12);
  ASSERT_EQ(es.wins, 5);
  ASSERT_EQ(es.ties, 0);
  ASSERT_EQ(es.losses, 0);

  /* Errors */
  ASSERT_EQ(eval_paired_stats(NULL, other, 5, &es), STATUS_ERR_NULL);
  ASSERT_EQ(eval_paired_stats(base, NULL, 5, &es), STATUS_ERR_NULL);
  ASSERT_EQ(eval_paired_stats(base, other, 5, NULL), STATUS_ERR_NULL);
  ASSERT_EQ(eval_paired_stats(base, other, 1, &es), STATUS_ERR_INVALID);

  const double nan_arr[2] = {1.0, NAN};
  ASSERT_EQ(eval_paired_stats(base, nan_arr, 2, &es), STATUS_ERR_INVALID);

  TEST_PASS();
}

/* 3. Poisson tail and median */
static void test_eval_poisson_tail_and_median(void) {
  TEST_START("test_eval_poisson_tail_and_median");

  /* P(X >= 1) for Poisson(2.0) = 1 - exp(-2) = 0.864664717 */
  ASSERT_APPROX(eval_poisson_tail_ge(2.0, 1), 1.0 - exp(-2.0), 1e-6);

  /* Independent pmf sum for Poisson(5.0), k = 12 */
  double sum_lt = 0.0;
  double fact = 1.0;
  for (int j = 0; j < 12; j++) {
    if (j > 0) fact *= (double)j;
    sum_lt += exp(-5.0) * pow(5.0, (double)j) / fact;
  }
  double indep_tail = 1.0 - sum_lt;
  ASSERT_APPROX(eval_poisson_tail_ge(5.0, 12), 0.00549, 1e-4);
  ASSERT_APPROX(eval_poisson_tail_ge(5.0, 12), indep_tail, 1e-9);

  ASSERT_APPROX(eval_poisson_tail_ge(3.0, 0), 1.0, 1e-12);
  ASSERT_APPROX(eval_poisson_tail_ge(0.0, 1), 0.0, 1e-12);
  ASSERT_APPROX(eval_poisson_tail_ge(0.0, 0), 1.0, 1e-12);
  ASSERT_TRUE(isnan(eval_poisson_tail_ge(NAN, 1)));

  /* Median */
  double odd[5] = {9.0, 1.0, 5.0, 3.0, 7.0};
  ASSERT_APPROX(eval_median(odd, 5), 5.0, 1e-12);
  double even[4] = {10.0, 2.0, 8.0, 4.0};
  ASSERT_APPROX(eval_median(even, 4), 6.0, 1e-12);
  ASSERT_TRUE(isnan(eval_median(even, 0)));
  ASSERT_TRUE(isnan(eval_median(NULL, 3)));

  TEST_PASS();
}

/* 4. Scenario helpers and error handling */
static void test_eval_scenario_helpers(void) {
  TEST_START("test_eval_scenario_helpers");

  /* No hubs -> stop 0 */
  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  NetworkModel *net_nohub = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net_nohub);
  ASSERT_EQ(eval_pick_event_stop(NULL), -1);
  ASSERT_EQ(eval_pick_event_stop(net_nohub), 0);
  network_destroy(net_nohub);

  /* Two hubs with tie (stops 2 and 5, both scale 1.5) -> smaller index 2 */
  stops[2].type = STOP_HUB;
  stops[2].scale = 1.5;
  stops[5].type = STOP_HUB;
  stops[5].scale = 1.5;
  NetworkModel *net_tie = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net_tie);
  ASSERT_EQ(eval_pick_event_stop(net_tie), 2);
  network_destroy(net_tie);

  /* Hub 5 has strictly larger scale (1.8 > 1.5) -> stop 5 */
  stops[5].scale = 1.8;
  NetworkModel *net_hub5 = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net_hub5);
  ASSERT_EQ(eval_pick_event_stop(net_hub5), 5);

  /* Blocked edge detection and reset of traffic */
  ASSERT_EQ(eval_graph_has_blocked_edge(NULL), -1);
  ASSERT_EQ(eval_graph_has_blocked_edge(g), 0);
  ASSERT_EQ(graph_set_blocked(g, 0, 1), STATUS_OK);
  ASSERT_EQ(eval_graph_has_blocked_edge(g), 1);

  SimStats st;
  ASSERT_EQ(eval_run_single(net_hub5, EVAL_SCEN_NORMAL, EVAL_POL_FIXED_EQUAL,
                            1000ULL, NULL, NULL, NULL, 4, 30.0, 0.02, 60, 1,
                            &st),
            STATUS_ERR_INVALID);
  ASSERT_EQ(graph_set_blocked(g, 0, 0), STATUS_OK);
  ASSERT_EQ(eval_graph_has_blocked_edge(g), 0);

  /* Analytic anomaly expected flags agrees with manual loop over 63 windows */
  double analytic = eval_anomaly_expected_flags(net_hub5, 360.0, 1320.0, 15.0,
                                                DAY_WEEKDAY, 0, 3.0, 1.0, 5);
  double manual = 0.0;
  for (int w = 0; w < 63; w++) {
    int hour = (360 + w * 15) / 60;
    for (int s = 0; s < 8; s++) {
      double lam =
          demand_expected(stops[s].type, stops[s].scale, hour, DAY_WEEKDAY, 0, 0) *
          15.0 / 60.0;
      int k = 5;
      while (anomaly_zscore(k, lam, 1.0) <= 3.0) k++;
      manual += eval_poisson_tail_ge(lam, k);
    }
  }
  ASSERT_APPROX(analytic, manual, 1e-12);
  ASSERT_TRUE(isnan(eval_anomaly_expected_flags(NULL, 360.0, 1320.0, 15.0,
                                                DAY_WEEKDAY, 0, 3.0, 1.0, 5)));

  /* Error cases */
  ASSERT_EQ(eval_run_single(NULL, EVAL_SCEN_NORMAL, EVAL_POL_FIXED_EQUAL,
                            1000ULL, NULL, NULL, NULL, 4, 30.0, 0.02, 60, 1,
                            &st),
            STATUS_ERR_NULL);
  ASSERT_EQ(eval_run_single(net_hub5, EVAL_SCEN_NORMAL, EVAL_POL_FIXED_EQUAL,
                            1000ULL, NULL, NULL, NULL, 4, 30.0, 0.02, 60, 1,
                            NULL),
            STATUS_ERR_NULL);
  ASSERT_EQ(eval_run_single(net_hub5, EVAL_SCEN_NORMAL,
                            EVAL_POL_GREEDY_MARGINAL_RF, 1000ULL, NULL, NULL,
                            NULL, 4, 30.0, 0.02, 60, 1, &st),
            STATUS_ERR_NULL);

  network_destroy(net_hub5);
  graph_destroy(g);
  TEST_PASS();
}

/* 5. Determinism, reversed policy order, and paired arrivals identity */
static void test_eval_determinism_and_paired_design(void) {
  TEST_START("test_eval_determinism_and_paired_design");

  DemandStop stops[8];
  Graph *g = build_line_graph_fixture(stops);
  ASSERT_NOT_NULL(g);
  stops[0].type = STOP_HUB;
  stops[0].scale = 1.6;
  NetworkModel *net = build_line_network(g, stops, 2);
  ASSERT_NOT_NULL(net);

  /* Train small MA predictor on synthetic rows */
  DemandRow *rows = NULL;
  size_t n_rows = 0;
  ASSERT_EQ(demand_generate(stops, 8, 42ULL, 5, &rows, &n_rows), STATUS_OK);
  DemandPredictor *dp_ma = dp_train_moving_avg(rows, n_rows);
  free(rows);
  ASSERT_NOT_NULL(dp_ma);

  double pw[2] = {0.0, 0.0};
  ASSERT_EQ(eval_compute_prop_weights(net, dp_ma, pw), STATUS_OK);

  const EvalPolicyId pols[3] = {EVAL_POL_FIXED_EQUAL,
                                EVAL_POL_GREEDY_MARGINAL_MA,
                                EVAL_POL_GREEDY_MARGINAL_ORACLE};
  SimStats run1[3][3];
  SimStats run2[3][3];
  SimStats rev[3][3];

  for (int i = 0; i < 3; i++) {
    uint64_t seed = (uint64_t)(1000 + i);
    for (int p = 0; p < 3; p++) {
      ASSERT_EQ(eval_run_single(net, EVAL_SCEN_NORMAL, pols[p], seed, dp_ma,
                                NULL, pw, 4, 30.0, 0.02, 60, 1, &run1[i][p]),
                STATUS_OK);
    }
  }
  for (int i = 0; i < 3; i++) {
    uint64_t seed = (uint64_t)(1000 + i);
    for (int p = 0; p < 3; p++) {
      ASSERT_EQ(eval_run_single(net, EVAL_SCEN_NORMAL, pols[p], seed, dp_ma,
                                NULL, pw, 4, 30.0, 0.02, 60, 1, &run2[i][p]),
                STATUS_OK);
      ASSERT_EQ(memcmp(&run1[i][p], &run2[i][p], sizeof(SimStats)), 0);
    }
  }
  /* Reversed policy order */
  for (int i = 0; i < 3; i++) {
    uint64_t seed = (uint64_t)(1000 + i);
    for (int p = 2; p >= 0; p--) {
      ASSERT_EQ(eval_run_single(net, EVAL_SCEN_NORMAL, pols[p], seed, dp_ma,
                                NULL, pw, 4, 30.0, 0.02, 60, 1, &rev[i][p]),
                STATUS_OK);
      ASSERT_EQ(memcmp(&run1[i][p], &rev[i][p], sizeof(SimStats)), 0);
    }
  }
  /* Paired design: same seed yields identical arrived passenger total across policies */
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(run1[i][0].arrived, run1[i][1].arrived);
    ASSERT_EQ(run1[i][0].arrived, run1[i][2].arrived);
  }

  dp_destroy(dp_ma);
  network_destroy(net);
  graph_destroy(g);
  TEST_PASS();
}

int main(void) {
  RUN_TEST(test_eval_t_crit_and_mean_sd);
  RUN_TEST(test_eval_paired_stats_hand_values);
  RUN_TEST(test_eval_poisson_tail_and_median);
  RUN_TEST(test_eval_scenario_helpers);
  RUN_TEST(test_eval_determinism_and_paired_design);

  return test_summary();
}
