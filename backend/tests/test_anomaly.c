#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "test_util.h"
#include "anomaly.h"
#include "demand_model.h"

#define ASSERT_TRUE(c) TEST_ASSERT(c)
#define ASSERT_EQ(a,b) TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p) TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_APPROX(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))
#define TEST_START(name) (void)(name)
#define TEST_PASS() do {} while(0)

/* Helper to compute Poisson CDF tail P(X >= k) */
static double poisson_tail_prob(double lambda, int k_min) {
    double term = exp(-lambda);
    double cdf = 0.0;
    for (int k = 0; k < k_min; k++) {
        if (k > 0) {
            term *= lambda / (double)k;
        }
        cdf += term;
    }
    return 1.0 - cdf;
}

static void test_anomaly_zscore_by_hand(void) {
    TEST_START("Anomaly Z-Score Hand Calculation");

    /* obs = 10, exp = 4.0, var_floor = 1.0 -> (10 - 4)/sqrt(4) = 6/2 = 3.0 */
    double z1 = anomaly_zscore(10, 4.0, 1.0);
    ASSERT_APPROX(z1, 3.0, 1e-9);

    /* obs = 5, exp = 0.5, var_floor = 1.0 -> max(0.5, 1.0)=1.0 -> (5 - 0.5)/1 = 4.5 */
    double z2 = anomaly_zscore(5, 0.5, 1.0);
    ASSERT_APPROX(z2, 4.5, 1e-9);

    /* obs = 2, exp = 2.0 -> 0.0 */
    double z3 = anomaly_zscore(2, 2.0, 1.0);
    ASSERT_APPROX(z3, 0.0, 1e-9);

    /* Invalid parameters */
    ASSERT_TRUE(isnan(anomaly_zscore(5, -1.0, 1.0)));
    ASSERT_TRUE(isnan(anomaly_zscore(5, 2.0, -0.5)));
    ASSERT_TRUE(isnan(anomaly_zscore(5, 2.0, 0.0)));

    TEST_PASS();
}

static void test_anomaly_gating_by_hand(void) {
    TEST_START("Anomaly Gating By Hand (min_observed)");

    AnomalyConfig cfg = anomaly_config_default();
    cfg.min_observed = 5;
    cfg.z_threshold = 3.0;

    AnomalyDetector *d = anomaly_create(2, &cfg);
    ASSERT_NOT_NULL(d);

    /* Stop 0: obs = 4, exp = 0.01 -> z = (4 - 0.01)/1 = 3.99 > 3.0, but obs=4 < 5 (NOT flagged) */
    for (int i = 0; i < 4; i++) {
        anomaly_record_arrival(d, 0);
    }
    /* Stop 1: obs = 5, exp = 0.01 -> z = 4.99 > 3.0, obs=5 >= 5 (FLAGGED) */
    for (int i = 0; i < 5; i++) {
        anomaly_record_arrival(d, 1);
    }

    double expected[2] = {0.01, 0.01};
    AnomalyFlag flags[4];
    size_t n_flags = 0;

    Status st = anomaly_close_window(d, 10.0, expected, flags, 4, &n_flags);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_flags, 1);
    ASSERT_EQ(flags[0].stop, 1);
    ASSERT_EQ(flags[0].observed, 5);
    ASSERT_APPROX(flags[0].expected, 0.01, 1e-9);

    anomaly_destroy(d);
    TEST_PASS();
}

static void test_anomaly_cooldown(void) {
    TEST_START("Anomaly Cooldown Period (60 min)");

    AnomalyConfig cfg = anomaly_config_default();
    cfg.cooldown_min = 60.0;
    cfg.z_threshold = 3.0;
    cfg.min_observed = 5;

    AnomalyDetector *d = anomaly_create(1, &cfg);
    ASSERT_NOT_NULL(d);

    double expected[1] = {1.0};
    AnomalyFlag flags[2];
    size_t n_flags = 0;

    /* Window 1 at t = 10.0 min: 10 arrivals -> flagged */
    for (int i = 0; i < 10; i++) anomaly_record_arrival(d, 0);
    Status st = anomaly_close_window(d, 10.0, expected, flags, 2, &n_flags);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_flags, 1);
    ASSERT_EQ(flags[0].stop, 0);

    /* Window 2 at t = 30.0 min: 10 arrivals -> (30 - 10) = 20 < 60 -> suppressed by cooldown */
    for (int i = 0; i < 10; i++) anomaly_record_arrival(d, 0);
    st = anomaly_close_window(d, 30.0, expected, flags, 2, &n_flags);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_flags, 0);

    /* Window 3 at t = 75.0 min: 10 arrivals -> (75 - 10) = 65 >= 60 -> flagged again */
    for (int i = 0; i < 10; i++) anomaly_record_arrival(d, 0);
    st = anomaly_close_window(d, 75.0, expected, flags, 2, &n_flags);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_flags, 1);
    ASSERT_EQ(flags[0].stop, 0);

    anomaly_destroy(d);
    TEST_PASS();
}

static void test_anomaly_window_resets(void) {
    TEST_START("Anomaly Window Semantics and Resets");

    AnomalyDetector *d = anomaly_create(3, NULL);
    ASSERT_NOT_NULL(d);

    anomaly_record_arrival(d, 0);
    anomaly_record_arrival(d, 0);
    anomaly_record_arrival(d, 2);

    ASSERT_EQ(anomaly_observed(d, 0), 2);
    ASSERT_EQ(anomaly_observed(d, 1), 0);
    ASSERT_EQ(anomaly_observed(d, 2), 1);

    double expected[3] = {10.0, 10.0, 10.0};
    AnomalyFlag flags[4];
    size_t n_flags = 0;

    Status st = anomaly_close_window(d, 10.0, expected, flags, 4, &n_flags);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(n_flags, 0);

    /* Counts must be reset to 0 */
    ASSERT_EQ(anomaly_observed(d, 0), 0);
    ASSERT_EQ(anomaly_observed(d, 1), 0);
    ASSERT_EQ(anomaly_observed(d, 2), 0);

    anomaly_destroy(d);
    TEST_PASS();
}

static void test_anomaly_statistical_false_alarm(void) {
    TEST_START("Anomaly Statistical False Alarm Rate (20000 windows)");

    /*
     * Null hypothesis: Poisson(lambda = 5.0). Expected = 5.0, var_floor = 1.0.
     * Flag condition: z = (k - 5) / sqrt(5) > 3.0 <=> k - 5 > 3*sqrt(5) ~ 6.708 <=> k >= 12.
     * With cooldown set to 0.0, every window is an independent trial.
     */
    AnomalyConfig cfg = anomaly_config_default();
    cfg.cooldown_min = 0.0;
    cfg.z_threshold = 3.0;
    cfg.min_observed = 5;

    AnomalyDetector *d = anomaly_create(1, &cfg);
    ASSERT_NOT_NULL(d);

    double expected[1] = {5.0};
    int total_flags = 0;
    int n_trials = 20000;
    uint64_t seed = 12345;

    for (int t = 0; t < n_trials; t++) {
        uint64_t hash = demand_hash(seed, (uint64_t)t, 0x1234ULL, 0ULL);
        int obs = (int)demand_poisson_sample(5.0, hash);
        for (int i = 0; i < obs; i++) {
            anomaly_record_arrival(d, 0);
        }
        AnomalyFlag fl[2];
        size_t nf = 0;
        anomaly_close_window(d, (double)t, expected, fl, 2, &nf);
        total_flags += (int)nf;
    }

    double p_theoretical = poisson_tail_prob(5.0, 12);
    double expected_flags = (double)n_trials * p_theoretical;
    double se = sqrt((double)n_trials * p_theoretical * (1.0 - p_theoretical));

    /* Check within 4 * SE */
    double diff = fabs((double)total_flags - expected_flags);
    ASSERT_TRUE(diff <= 4.0 * se);

    anomaly_destroy(d);
    TEST_PASS();
}

static void test_anomaly_statistical_power(void) {
    TEST_START("Anomaly Statistical Power (10000 windows under Alternative)");

    /*
     * Alternative hypothesis: true lambda = 12.5, but expected is 5.0.
     * Flag condition remains k >= 12.
     * Power is P(X >= 12 | lambda = 12.5).
     */
    AnomalyConfig cfg = anomaly_config_default();
    cfg.cooldown_min = 0.0;
    cfg.z_threshold = 3.0;
    cfg.min_observed = 5;

    AnomalyDetector *d = anomaly_create(1, &cfg);
    ASSERT_NOT_NULL(d);

    double expected[1] = {5.0};
    int total_detections = 0;
    int n_trials = 10000;
    uint64_t seed = 54321;

    for (int t = 0; t < n_trials; t++) {
        uint64_t hash = demand_hash(seed, (uint64_t)t, 0x5678ULL, 0ULL);
        int obs = (int)demand_poisson_sample(12.5, hash);
        for (int i = 0; i < obs; i++) {
            anomaly_record_arrival(d, 0);
        }
        AnomalyFlag fl[2];
        size_t nf = 0;
        anomaly_close_window(d, (double)t, expected, fl, 2, &nf);
        total_detections += (int)nf;
    }

    double p_theoretical = poisson_tail_prob(12.5, 12);
    double expected_detections = (double)n_trials * p_theoretical;
    double se = sqrt((double)n_trials * p_theoretical * (1.0 - p_theoretical));

    double diff = fabs((double)total_detections - expected_detections);
    ASSERT_TRUE(diff <= 4.0 * se);

    anomaly_destroy(d);
    TEST_PASS();
}

static void test_anomaly_null_and_bounds(void) {
    TEST_START("Anomaly NULL and Bounds Checks");

    ASSERT_NULL(anomaly_create(0, NULL));

    AnomalyDetector *d = anomaly_create(2, NULL);
    ASSERT_NOT_NULL(d);

    ASSERT_EQ(anomaly_record_arrival(NULL, 0), STATUS_ERR_NULL);
    ASSERT_EQ(anomaly_record_arrival(d, 5), STATUS_ERR_INVALID);
    ASSERT_EQ(anomaly_observed(NULL, 0), -1);
    ASSERT_EQ(anomaly_observed(d, 5), -1);

    double expected[2] = {1.0, 1.0};
    AnomalyFlag fl[1];
    size_t nf = 0;

    ASSERT_EQ(anomaly_close_window(NULL, 0.0, expected, fl, 1, &nf), STATUS_ERR_NULL);
    ASSERT_EQ(anomaly_close_window(d, 0.0, NULL, fl, 1, &nf), STATUS_ERR_NULL);
    ASSERT_EQ(anomaly_close_window(d, 0.0, expected, fl, 1, NULL), STATUS_ERR_NULL);

    /* Test max_out exceeded returns STATUS_ERR_NOMEM */
    for (int i = 0; i < 10; i++) {
        anomaly_record_arrival(d, 0);
        anomaly_record_arrival(d, 1);
    }
    Status st = anomaly_close_window(d, 0.0, expected, fl, 1, &nf);
    ASSERT_EQ(st, STATUS_ERR_NOMEM);
    ASSERT_EQ(nf, 2);

    anomaly_destroy(d);
    TEST_PASS();
}

int main(void) {
    RUN_TEST(test_anomaly_zscore_by_hand);
    RUN_TEST(test_anomaly_gating_by_hand);
    RUN_TEST(test_anomaly_cooldown);
    RUN_TEST(test_anomaly_window_resets);
    RUN_TEST(test_anomaly_statistical_false_alarm);
    RUN_TEST(test_anomaly_statistical_power);
    RUN_TEST(test_anomaly_null_and_bounds);

    return test_summary();
}
