#include "test_util.h"
#include "ai_demand.h"
#include "demand_model.h"
#include "ai_inference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define EPSILON 1e-5

static void test_moving_avg(void) {
    /* 10 hand-made rows:
       Row 0: stop 1, RESIDENTIAL (3), scale 1.0, hour 8, WEEKDAY (0), passengers 10
       Row 1: stop 1, RESIDENTIAL (3), scale 1.0, hour 8, WEEKDAY (0), passengers 20
         -> stop 1, hour 8, weekday mean = 15.0
       Row 2: stop 2, RESIDENTIAL (3), scale 1.0, hour 8, WEEKDAY (0), passengers 30
         -> stop 2, hour 8, weekday mean = 30.0
         -> RESIDENTIAL, hour 8, weekday mean = (10 + 20 + 30) / 3 = 20.0
       Row 3: stop 3, OFFICE (1), scale 1.0, hour 8, WEEKDAY (0), passengers 40
         -> OFFICE, hour 8, weekday mean = 40.0
         -> overall hour 8 mean = (10 + 20 + 30 + 40) / 4 = 25.0
       Row 4: stop 1, RESIDENTIAL (3), scale 1.0, hour 9, WEEKDAY (0), passengers 50
       Row 5: stop 1, RESIDENTIAL (3), scale 1.0, hour 8, WEEKEND (1), passengers 60
       Row 6: stop 4, HUB (0), scale 1.0, hour 12, WEEKDAY (0), passengers 70
       Row 7: stop 4, HUB (0), scale 1.0, hour 12, WEEKDAY (0), passengers 80
       Row 8: stop 5, MARKET (2), scale 1.0, hour 18, WEEKDAY (0), passengers 90
       Row 9: stop 5, MARKET (2), scale 1.0, hour 18, WEEKDAY (0), passengers 100
    */
    DemandRow rows[10];
    memset(rows, 0, sizeof(rows));

    rows[0] = (DemandRow){.day = 0, .stop_node = 1, .stop_type = STOP_RESIDENTIAL, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 10, .lambda = 10.0};
    rows[1] = (DemandRow){.day = 0, .stop_node = 1, .stop_type = STOP_RESIDENTIAL, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 20, .lambda = 20.0};
    rows[2] = (DemandRow){.day = 0, .stop_node = 2, .stop_type = STOP_RESIDENTIAL, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 30, .lambda = 30.0};
    rows[3] = (DemandRow){.day = 0, .stop_node = 3, .stop_type = STOP_OFFICE, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 40, .lambda = 40.0};
    rows[4] = (DemandRow){.day = 0, .stop_node = 1, .stop_type = STOP_RESIDENTIAL, .stop_scale = 1.0, .hour = 9, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 50, .lambda = 50.0};
    rows[5] = (DemandRow){.day = 1, .stop_node = 1, .stop_type = STOP_RESIDENTIAL, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKEND, .rain = 0, .event = 0, .passengers = 60, .lambda = 60.0};
    rows[6] = (DemandRow){.day = 0, .stop_node = 4, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 12, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 70, .lambda = 70.0};
    rows[7] = (DemandRow){.day = 0, .stop_node = 4, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 12, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 80, .lambda = 80.0};
    rows[8] = (DemandRow){.day = 0, .stop_node = 5, .stop_type = STOP_MARKET, .stop_scale = 1.0, .hour = 18, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 90, .lambda = 90.0};
    rows[9] = (DemandRow){.day = 0, .stop_node = 5, .stop_type = STOP_MARKET, .stop_scale = 1.0, .hour = 18, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 100, .lambda = 100.0};

    DemandPredictor *dp = dp_train_moving_avg(rows, 10);
    TEST_ASSERT_NOT_NULL(dp);
    TEST_ASSERT_EQ_INT(dp_kind(dp), DEMAND_MODEL_MOVING_AVG);

    /* 1. Exact match for stop 1, hour 8, weekday: (10 + 20) / 2 = 15.0 */
    double p1 = dp_predict(dp, 1, STOP_RESIDENTIAL, 1.0, 8, DAY_WEEKDAY, 0, 0);
    TEST_ASSERT(fabs(p1 - 15.0) < EPSILON);

    /* 2. Exact match for stop 2, hour 8, weekday: 30.0 */
    double p2 = dp_predict(dp, 2, STOP_RESIDENTIAL, 1.0, 8, DAY_WEEKDAY, 0, 0);
    TEST_ASSERT(fabs(p2 - 30.0) < EPSILON);

    /* 3. Fallback chain step 1: Unseen stop 99, but type RESIDENTIAL, hour 8, weekday exists:
          mean = (10 + 20 + 30) / 3 = 20.0 */
    double p_fallback_stop = dp_predict(dp, 99, STOP_RESIDENTIAL, 1.0, 8, DAY_WEEKDAY, 0, 0);
    TEST_ASSERT(fabs(p_fallback_stop - 20.0) < EPSILON);

    /* 4. Fallback chain step 2: Unseen stop 99 AND unseen type MARKET for hour 8, weekday:
          falls back to overall hour 8 mean: (10 + 20 + 30 + 40 + 60) / 5 = 160 / 5 = 32.0 */
    double p_fallback_type = dp_predict(dp, 99, STOP_MARKET, 1.0, 8, DAY_WEEKDAY, 0, 0);
    TEST_ASSERT(fabs(p_fallback_type - 32.0) < EPSILON);

    /* 5. Fallback chain step 3: Unseen hour 3 (never seen in training data):
          falls back to 0.0 */
    double p_fallback_hour = dp_predict(dp, 99, STOP_MARKET, 1.0, 3, DAY_WEEKDAY, 0, 0);
    TEST_ASSERT(fabs(p_fallback_hour - 0.0) < EPSILON);

    /* Invalid arguments return -1.0 */
    TEST_ASSERT(dp_predict(dp, 1, STOP_RESIDENTIAL, 1.0, -1, DAY_WEEKDAY, 0, 0) == -1.0);
    TEST_ASSERT(dp_predict(dp, 1, STOP_RESIDENTIAL, -0.5, 8, DAY_WEEKDAY, 0, 0) == -1.0);

    dp_destroy(dp);
}

static void test_ols_exact_recovery(void) {
    /* Generate 40 noise-free linearly independent rows per band from a known linear function */
    const size_t N = 160;
    DemandRow rows[160];
    memset(rows, 0, sizeof(rows));

    int base_hours[4] = {1, 6, 11, 16}; /* one base per band: night(0), morning(1), midday(2), evening(3) */

    for (int b = 0; b < 4; b++) {
        int base_h = base_hours[b];
        for (int i = 0; i < 40; i++) {
            size_t idx = (size_t)(b * 40 + i);
            int h = base_h + (i % 4); /* varied hours within each band */
            DayType dt = (DayType)(i % 3);
            int rain = (i % 4 == 1) ? 1 : 0;
            int event = (i % 5 == 2) ? 1 : 0;
            StopType st = (StopType)(i % 4);
            double scale = 0.75 + 0.05 * (double)(i % 9);

            /* Known linear function:
               y = 10.0 + 1.5*h + 2.0*(weekend) + 3.0*(holiday) + 4.0*rain + 5.0*event +
                   6.0*(hub) + 7.0*(office) + 8.0*(market) + 9.0*scale */
            double y = 10.0 + 1.5 * (double)h +
                       ((dt == DAY_WEEKEND) ? 2.0 : 0.0) +
                       ((dt == DAY_HOLIDAY) ? 3.0 : 0.0) +
                       ((rain) ? 4.0 : 0.0) +
                       ((event) ? 5.0 : 0.0) +
                       ((st == STOP_HUB) ? 6.0 : 0.0) +
                       ((st == STOP_OFFICE) ? 7.0 : 0.0) +
                       ((st == STOP_MARKET) ? 8.0 : 0.0) +
                       9.0 * scale;

            rows[idx].day = b;
            rows[idx].stop_node = (int)idx;
            rows[idx].stop_type = st;
            rows[idx].stop_scale = scale;
            rows[idx].hour = h;
            rows[idx].day_type = dt;
            rows[idx].rain = rain;
            rows[idx].event = event;
            rows[idx].passengers = y; /* exact noise-free target */
            rows[idx].lambda = y;
        }
    }

    DemandPredictor *dp = dp_train_ols_band(rows, N);
    TEST_ASSERT_NOT_NULL(dp);
    TEST_ASSERT_EQ_INT(dp_kind(dp), DEMAND_MODEL_OLS_BAND);

    /* Verify predictions match within 1e-4 of expected values across all bands */
    for (size_t i = 0; i < N; i++) {
        const DemandRow *r = &rows[i];
        double pred = dp_predict(dp, r->stop_node, r->stop_type, r->stop_scale, r->hour, r->day_type, r->rain, r->event);
        TEST_ASSERT(fabs(pred - r->lambda) < 1e-3);
    }

    dp_destroy(dp);
}

static void test_ols_unseen_event(void) {
    /* Dataset where event is ALWAYS 0 */
    DemandRow rows[40];
    memset(rows, 0, sizeof(rows));

    for (int i = 0; i < 40; i++) {
        rows[i].day = 0;
        rows[i].stop_node = i;
        rows[i].stop_type = (StopType)(i % 4);
        rows[i].stop_scale = 1.0;
        rows[i].hour = 8; /* band 1 (morning) */
        rows[i].day_type = (DayType)(i % 3);
        rows[i].rain = i % 2;
        rows[i].event = 0; /* completely zero */
        rows[i].passengers = 20.0 + (double)i;
        rows[i].lambda = 20.0 + (double)i;
    }

    DemandPredictor *dp = dp_train_ols_band(rows, 40);
    TEST_ASSERT_NOT_NULL(dp);

    /* Predict with event = 0 and event = 1 */
    double pred0 = dp_predict(dp, 1, STOP_OFFICE, 1.0, 8, DAY_WEEKDAY, 0, 0);
    double pred1 = dp_predict(dp, 1, STOP_OFFICE, 1.0, 8, DAY_WEEKDAY, 0, 1);

    TEST_ASSERT(!isnan(pred0));
    TEST_ASSERT(!isnan(pred1));
    TEST_ASSERT(pred0 >= 0.0);
    TEST_ASSERT(pred1 >= 0.0);

    dp_destroy(dp);
}

static void test_ols_band_fallback(void) {
    /* Dataset where morning band (hour 8) has only 5 rows (< 10) */
    DemandRow rows[5];
    memset(rows, 0, sizeof(rows));

    double sum = 0.0;
    for (int i = 0; i < 5; i++) {
        rows[i].day = 0;
        rows[i].stop_node = i;
        rows[i].stop_type = STOP_HUB;
        rows[i].stop_scale = 1.0;
        rows[i].hour = 8; /* band 1 */
        rows[i].day_type = DAY_WEEKDAY;
        rows[i].rain = 0;
        rows[i].event = 0;
        rows[i].passengers = 40.0 + (double)(i * 2); /* 40, 42, 44, 46, 48 -> mean = 44.0 */
        rows[i].lambda = rows[i].passengers;
        sum += rows[i].passengers;
    }
    double expected_mean = sum / 5.0;

    DemandPredictor *dp = dp_train_ols_band(rows, 5);
    TEST_ASSERT_NOT_NULL(dp);

    /* Any prediction in morning band should return the mean (44.0) regardless of features */
    double pred = dp_predict(dp, 99, STOP_OFFICE, 2.5, 9, DAY_WEEKEND, 1, 1);
    TEST_ASSERT(fabs(pred - expected_mean) < EPSILON);

    dp_destroy(dp);
}

static void test_predictions_never_negative(void) {
    /* Train a simple OLS predictor where extreme inputs could produce negative values */
    DemandRow rows[20];
    memset(rows, 0, sizeof(rows));

    for (int i = 0; i < 20; i++) {
        rows[i].day = 0;
        rows[i].stop_node = i;
        rows[i].stop_type = STOP_HUB;
        rows[i].stop_scale = 1.0;
        rows[i].hour = 8;
        rows[i].day_type = DAY_WEEKDAY;
        rows[i].rain = 0;
        rows[i].event = 0;
        rows[i].passengers = 1.0; /* very low demand */
        rows[i].lambda = 1.0;
    }

    DemandPredictor *dp = dp_train_ols_band(rows, 20);
    TEST_ASSERT_NOT_NULL(dp);

    double pred = dp_predict(dp, 1, STOP_RESIDENTIAL, 0.01, 8, DAY_WEEKDAY, 1, 0);
    TEST_ASSERT(pred >= 0.0);

    dp_destroy(dp);
}

/*
 * Hand-checked 4-row evaluation test:
 *
 * Row 0: actual = 10, pred = 12, lambda = 9.  category = CAT_WEEKDAY (0)
 *   error = 12 - 10 = 2, abs_err = 2, sq_err = 4, lambda_err = |12 - 9| = 3
 * Row 1: actual = 20, pred = 15, lambda = 22. category = CAT_WEEKDAY (0)
 *   error = 15 - 20 = -5, abs_err = 5, sq_err = 25, lambda_err = |15 - 22| = 7
 * Row 2: actual = 30, pred = 30, lambda = 31. category = CAT_WEEKEND (1)
 *   error = 30 - 30 = 0, abs_err = 0, sq_err = 0, lambda_err = |30 - 31| = 1
 * Row 3: actual = 40, pred = 50, lambda = 38. category = CAT_EVENT (4)
 *   error = 50 - 40 = 10, abs_err = 10, sq_err = 100, lambda_err = |50 - 38| = 12
 *
 * Overall (N = 4):
 *   sum_abs = 2 + 5 + 0 + 10 = 17
 *   MAE = 17 / 4 = 4.25
 *   sum_sq = 4 + 25 + 0 + 100 = 129
 *   RMSE = sqrt(129 / 4) = sqrt(32.25) = 5.678908345800274
 *   sum_actual = 10 + 20 + 30 + 40 = 100
 *   WAPE = 17 / 100 = 0.17
 *   sum_lambda_err = 3 + 7 + 1 + 12 = 23
 *   MAE_lambda = 23 / 4 = 5.75
 *
 * Category Breakdown:
 *   CAT_WEEKDAY (n = 2):
 *     MAE = 7 / 2 = 3.5
 *     RMSE = sqrt(29 / 2) = sqrt(14.5) = 3.8078865529319543
 *     WAPE = 7 / 30 = 0.23333333333333334
 *     MAE_lambda = 10 / 2 = 5.0
 *   CAT_WEEKEND (n = 1):
 *     MAE = 0.0, RMSE = 0.0, WAPE = 0.0, MAE_lambda = 1.0
 *   CAT_RAIN (n = 0):
 *     n = 0, MAE = 0.0, RMSE = 0.0, WAPE = 0.0, MAE_lambda = 0.0 (no div by zero)
 *   CAT_EVENT (n = 1):
 *     MAE = 10.0, RMSE = 10.0, WAPE = 10 / 40 = 0.25, MAE_lambda = 12.0
 */
static void test_dp_evaluate(void) {
    DemandRow rows[4];
    memset(rows, 0, sizeof(rows));

    rows[0] = (DemandRow){.day = 0, .stop_node = 1, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 10.0, .lambda = 9.0};
    rows[1] = (DemandRow){.day = 0, .stop_node = 2, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 20.0, .lambda = 22.0};
    rows[2] = (DemandRow){.day = 1, .stop_node = 3, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKEND, .rain = 0, .event = 0, .passengers = 30.0, .lambda = 31.0};
    rows[3] = (DemandRow){.day = 0, .stop_node = 4, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 1, .passengers = 40.0, .lambda = 38.0};

    DemandRow train[4];
    memset(train, 0, sizeof(train));
    train[0] = (DemandRow){.day = 0, .stop_node = 1, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 12.0, .lambda = 12.0};
    train[1] = (DemandRow){.day = 0, .stop_node = 2, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 0, .passengers = 15.0, .lambda = 15.0};
    train[2] = (DemandRow){.day = 1, .stop_node = 3, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKEND, .rain = 0, .event = 0, .passengers = 30.0, .lambda = 30.0};
    train[3] = (DemandRow){.day = 0, .stop_node = 4, .stop_type = STOP_HUB, .stop_scale = 1.0, .hour = 8, .day_type = DAY_WEEKDAY, .rain = 0, .event = 1, .passengers = 50.0, .lambda = 50.0};

    DemandPredictor *dp = dp_train_moving_avg(train, 4);
    TEST_ASSERT_NOT_NULL(dp);

    DemandEval overall;
    DemandEval by_cat[DEMAND_NUM_CATEGORIES];
    Status st = dp_evaluate(dp, rows, 4, &overall, by_cat);
    TEST_ASSERT_EQ_INT(st, STATUS_OK);

    /* Verify overall */
    TEST_ASSERT_EQ_INT(overall.n, 4);
    TEST_ASSERT(fabs(overall.mae_noisy - 4.25) < EPSILON);
    TEST_ASSERT(fabs(overall.rmse_noisy - sqrt(32.25)) < EPSILON);
    TEST_ASSERT(fabs(overall.wape_noisy - 0.17) < EPSILON);
    TEST_ASSERT(fabs(overall.mae_lambda - 5.75) < EPSILON);

    /* CAT_WEEKDAY (0) */
    TEST_ASSERT_EQ_INT(by_cat[CAT_WEEKDAY].n, 2);
    TEST_ASSERT(fabs(by_cat[CAT_WEEKDAY].mae_noisy - 3.5) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_WEEKDAY].rmse_noisy - sqrt(14.5)) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_WEEKDAY].wape_noisy - 7.0 / 30.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_WEEKDAY].mae_lambda - 5.0) < EPSILON);

    /* CAT_WEEKEND (1) */
    TEST_ASSERT_EQ_INT(by_cat[CAT_WEEKEND].n, 1);
    TEST_ASSERT(fabs(by_cat[CAT_WEEKEND].mae_noisy - 0.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_WEEKEND].rmse_noisy - 0.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_WEEKEND].wape_noisy - 0.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_WEEKEND].mae_lambda - 1.0) < EPSILON);

    /* CAT_RAIN (3, empty) */
    TEST_ASSERT_EQ_INT(by_cat[CAT_RAIN].n, 0);
    TEST_ASSERT(fabs(by_cat[CAT_RAIN].mae_noisy - 0.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_RAIN].rmse_noisy - 0.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_RAIN].wape_noisy - 0.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_RAIN].mae_lambda - 0.0) < EPSILON);

    /* CAT_EVENT (4) */
    TEST_ASSERT_EQ_INT(by_cat[CAT_EVENT].n, 1);
    TEST_ASSERT(fabs(by_cat[CAT_EVENT].mae_noisy - 10.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_EVENT].rmse_noisy - 10.0) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_EVENT].wape_noisy - 0.25) < EPSILON);
    TEST_ASSERT(fabs(by_cat[CAT_EVENT].mae_lambda - 12.0) < EPSILON);

    dp_destroy(dp);
}

static void test_ownership(void) {
    /* Test that dp_from_forest takes ownership of ForestModel and frees it on dp_destroy */
    ForestModel *fm = NULL;
    Status st = forest_load(&fm, "tests/fixtures/mini_rf.txt");
    if (st != STATUS_OK) {
        st = forest_load(&fm, "backend/tests/fixtures/mini_rf.txt");
    }
    TEST_ASSERT_EQ_INT(st, STATUS_OK);
    TEST_ASSERT_NOT_NULL(fm);

    DemandPredictor *dp = dp_from_forest(fm);
    TEST_ASSERT_NOT_NULL(dp);
    TEST_ASSERT_EQ_INT(dp_kind(dp), DEMAND_MODEL_FOREST);
    TEST_ASSERT(strcmp(dp_name(dp), "random_forest") == 0);

    /* Predict on a sample row */
    double pred = dp_predict(dp, 10, STOP_HUB, 1.0, 9, DAY_WEEKDAY, 0, 0);
    TEST_ASSERT(pred > 0.0);

    /* Freeing predictor frees fm */
    dp_destroy(dp);
}

int main(void) {
    RUN_TEST(test_moving_avg);
    RUN_TEST(test_ols_exact_recovery);
    RUN_TEST(test_ols_unseen_event);
    RUN_TEST(test_ols_band_fallback);
    RUN_TEST(test_predictions_never_negative);
    RUN_TEST(test_dp_evaluate);
    RUN_TEST(test_ownership);

    return test_summary();
}
