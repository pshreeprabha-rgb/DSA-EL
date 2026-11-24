#include "test_util.h"
#include "demand_model.h"
#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ASSERT_TRUE(c) TEST_ASSERT(c)
#define ASSERT_EQ(a,b) TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p) TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_DOUBLE_EQ(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))
#define ASSERT_STR_EQ(a,b) TEST_ASSERT(strcmp((a),(b))==0)

/*
 * 1. Hand-computed expected demand test:
 * HUB, scale 1.0, hour 8, weekday, no rain, no event.
 * Derivation:
 *   t = 8 + 0.5 = 8.5
 *   flr = 0.15
 *   g1 = exp(-0.5 * ((8.5 - 8.5) / 1.4)^2) = exp(0) = 1.0
 *   a1 * g1 = 0.85 * 1.0 = 0.85
 *   g2 = exp(-0.5 * ((8.5 - 18.0) / 1.6)^2) = exp(-0.5 * (-5.9375)^2) = exp(-17.626953125) = 2.211516766e-8
 *   a2 * g2 = 0.90 * 2.211516766e-8 = 1.99036509e-8
 *   g3 = exp(-0.5 * ((8.5 - 13.0) / 3.0)^2) = exp(-0.5 * (-1.5)^2) = exp(-1.125) = 0.324652467358
 *   a3 * g3 = 0.35 * 0.324652467358 = 0.113628363575
 *   profile = 0.15 + 0.85 + 1.99036509e-8 + 0.113628363575 = 1.113628383479
 *   lambda = 60.0 * 1.0 * profile * 1.0 * 1.0 * 1.0 = 66.81770300874
 */
static void test_demand_expected_hand_check(void) {
    double lambda = demand_expected(STOP_HUB, 1.0, 8, DAY_WEEKDAY, 0, 0);
    double expected = 66.81770300874;
    ASSERT_DOUBLE_EQ(lambda, expected, 1e-6);

    /* Night (hour 3) value far below morning peak for every type */
    for (int t = 0; t < 4; t++) {
        double val_night = demand_expected((StopType)t, 1.0, 3, DAY_WEEKDAY, 0, 0);
        double val_morning = demand_expected((StopType)t, 1.0, 8, DAY_WEEKDAY, 0, 0);
        ASSERT_TRUE(val_night < val_morning * 0.4);
    }

    /* HUB weekday 08:00 > HUB weekend 08:00 (weekend multiplier 0.80) */
    double hub_wd = demand_expected(STOP_HUB, 1.0, 8, DAY_WEEKDAY, 0, 0);
    double hub_we = demand_expected(STOP_HUB, 1.0, 8, DAY_WEEKEND, 0, 0);
    ASSERT_TRUE(hub_wd > hub_we);
    ASSERT_DOUBLE_EQ(hub_we / hub_wd, 0.80, 1e-12);

    /* OFFICE weekend << OFFICE weekday (weekend multiplier 0.30) */
    double off_wd = demand_expected(STOP_OFFICE, 1.0, 9, DAY_WEEKDAY, 0, 0);
    double off_we = demand_expected(STOP_OFFICE, 1.0, 9, DAY_WEEKEND, 0, 0);
    ASSERT_TRUE(off_we < off_wd * 0.35);
    ASSERT_DOUBLE_EQ(off_we / off_wd, 0.30, 1e-12);

    /* MARKET weekend > MARKET weekday at 12:00 (weekend multiplier 1.25) */
    double mkt_wd = demand_expected(STOP_MARKET, 1.0, 12, DAY_WEEKDAY, 0, 0);
    double mkt_we = demand_expected(STOP_MARKET, 1.0, 12, DAY_WEEKEND, 0, 0);
    ASSERT_TRUE(mkt_we > mkt_wd);
    ASSERT_DOUBLE_EQ(mkt_we / mkt_wd, 1.25, 1e-12);

    /* Rain multiplies by exactly 1.20 and event by exactly 2.5 (ratio checks to 1e-12) */
    double base_val = demand_expected(STOP_HUB, 1.2, 10, DAY_WEEKDAY, 0, 0);
    double rain_val = demand_expected(STOP_HUB, 1.2, 10, DAY_WEEKDAY, 1, 0);
    double event_val = demand_expected(STOP_HUB, 1.2, 10, DAY_WEEKDAY, 0, 1);
    double both_val = demand_expected(STOP_HUB, 1.2, 10, DAY_WEEKDAY, 1, 1);

    ASSERT_DOUBLE_EQ(rain_val / base_val, 1.20, 1e-12);
    ASSERT_DOUBLE_EQ(event_val / base_val, 2.50, 1e-12);
    ASSERT_DOUBLE_EQ(both_val / base_val, 1.20 * 2.50, 1e-12);

    /* Invalid arguments return -1.0 */
    ASSERT_DOUBLE_EQ(demand_expected((StopType)-1, 1.0, 8, DAY_WEEKDAY, 0, 0), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(demand_expected((StopType)4, 1.0, 8, DAY_WEEKDAY, 0, 0), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(demand_expected(STOP_HUB, 0.0, 8, DAY_WEEKDAY, 0, 0), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(demand_expected(STOP_HUB, -0.5, 8, DAY_WEEKDAY, 0, 0), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(demand_expected(STOP_HUB, 1.0, -1, DAY_WEEKDAY, 0, 0), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(demand_expected(STOP_HUB, 1.0, 24, DAY_WEEKDAY, 0, 0), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(demand_expected(STOP_HUB, 1.0, 8, (DayType)-1, 0, 0), -1.0, 1e-9);
    ASSERT_DOUBLE_EQ(demand_expected(STOP_HUB, 1.0, 8, (DayType)3, 0, 0), -1.0, 1e-9);
}

/* 2. Feature layout verification */
static void test_demand_features(void) {
    double feats[DEMAND_NUM_FEATURES];
    demand_features(STOP_OFFICE, 1.25, 14, DAY_WEEKEND, 1, 0, feats);

    ASSERT_DOUBLE_EQ(feats[0], 14.0, 1e-9); /* hour */
    ASSERT_DOUBLE_EQ(feats[1], 1.0, 1e-9);  /* weekend */
    ASSERT_DOUBLE_EQ(feats[2], 0.0, 1e-9);  /* holiday */
    ASSERT_DOUBLE_EQ(feats[3], 1.0, 1e-9);  /* rain */
    ASSERT_DOUBLE_EQ(feats[4], 0.0, 1e-9);  /* event */
    ASSERT_DOUBLE_EQ(feats[5], 0.0, 1e-9);  /* type_hub */
    ASSERT_DOUBLE_EQ(feats[6], 1.0, 1e-9);  /* type_office */
    ASSERT_DOUBLE_EQ(feats[7], 0.0, 1e-9);  /* type_market */
    ASSERT_DOUBLE_EQ(feats[8], 0.0, 1e-9);  /* type_residential */
    ASSERT_DOUBLE_EQ(feats[9], 1.25, 1e-9); /* stop_scale */

    /* Holiday mutually exclusive with weekend */
    demand_features(STOP_RESIDENTIAL, 0.8, 20, DAY_HOLIDAY, 0, 1, feats);
    ASSERT_DOUBLE_EQ(feats[1], 0.0, 1e-9);
    ASSERT_DOUBLE_EQ(feats[2], 1.0, 1e-9);
    ASSERT_DOUBLE_EQ(feats[5] + feats[6] + feats[7] + feats[8], 1.0, 1e-9);
    ASSERT_DOUBLE_EQ(feats[8], 1.0, 1e-9);
}

/* 3. Calendar generation and statistical sanity checks */
static void test_demand_calendar(void) {
    uint64_t seed = 42;
    DayType dt0 = DAY_WEEKDAY;
    int rain0 = 0, event0 = 0;
    demand_calendar(seed, 0, &dt0, &rain0, &event0);
    ASSERT_TRUE(dt0 == DAY_WEEKDAY || dt0 == DAY_HOLIDAY);

    /* Day 5 (Saturday) and Day 6 (Sunday) are weekends */
    DayType dt5, dt6;
    demand_calendar(seed, 5, &dt5, NULL, NULL);
    demand_calendar(seed, 6, &dt6, NULL, NULL);
    ASSERT_EQ(dt5, DAY_WEEKEND);
    ASSERT_EQ(dt6, DAY_WEEKEND);

    /* Calendar determinism */
    DayType dt_check;
    int r_check, e_check;
    demand_calendar(seed, 0, &dt_check, &r_check, &e_check);
    ASSERT_EQ(dt0, dt_check);
    ASSERT_EQ(rain0, r_check);
    ASSERT_EQ(event0, e_check);

    /* Test over 2000 days for rain ~0.20, event ~0.08, holiday among weekdays ~0.05 within 3 SE */
    const int N_DAYS = 2000;
    int rain_count = 0;
    int event_count = 0;
    int weekday_count = 0;
    int holiday_count = 0;

    for (int d = 0; d < N_DAYS; d++) {
        DayType dt;
        int r = 0, ev = 0;
        demand_calendar(seed, d, &dt, &r, &ev);
        if (r) rain_count++;
        if (ev) event_count++;
        if (d % 7 != 5 && d % 7 != 6) {
            weekday_count++;
            if (dt == DAY_HOLIDAY) holiday_count++;
        }
    }

    /* rain: p = 0.20, SE = sqrt(0.20 * 0.80 / 2000) = 0.00894 */
    double rain_frac = (double)rain_count / (double)N_DAYS;
    double rain_se = sqrt(0.20 * 0.80 / (double)N_DAYS);
    ASSERT_TRUE(fabs(rain_frac - 0.20) < 3.0 * rain_se);

    /* event: p = 0.08, SE = sqrt(0.08 * 0.92 / 2000) = 0.00607 */
    double ev_frac = (double)event_count / (double)N_DAYS;
    double ev_se = sqrt(0.08 * 0.92 / (double)N_DAYS);
    ASSERT_TRUE(fabs(ev_frac - 0.08) < 3.0 * ev_se);

    /* holiday among weekdays: p = 0.05 */
    double hol_frac = (double)holiday_count / (double)weekday_count;
    double hol_se = sqrt(0.05 * 0.95 / (double)weekday_count);
    ASSERT_TRUE(fabs(hol_frac - 0.05) < 3.0 * hol_se);
}

/* 4. Event stops selection */
static void test_demand_event_stops(void) {
    uint64_t seed = 123;
    int stops[3];
    size_t cnt = demand_event_stops(seed, 10, 50, stops);
    ASSERT_EQ(cnt, 3);
    ASSERT_TRUE(stops[0] >= 0 && stops[0] < 50);
    ASSERT_TRUE(stops[1] >= 0 && stops[1] < 50);
    ASSERT_TRUE(stops[2] >= 0 && stops[2] < 50);
    ASSERT_TRUE(stops[0] != stops[1]);
    ASSERT_TRUE(stops[1] != stops[2]);
    ASSERT_TRUE(stops[0] != stops[2]);

    /* Determinism check */
    int stops_check[3];
    demand_event_stops(seed, 10, 50, stops_check);
    ASSERT_EQ(stops[0], stops_check[0]);
    ASSERT_EQ(stops[1], stops_check[1]);
    ASSERT_EQ(stops[2], stops_check[2]);

    /* When n_stops < 3 */
    int s2[3];
    ASSERT_EQ(demand_event_stops(seed, 10, 2, s2), 2);
    ASSERT_EQ(s2[0], 0);
    ASSERT_EQ(s2[1], 1);
}

/* 5. Stop scale quantisation and range */
static void test_demand_stop_scale(void) {
    uint64_t seed = 999;
    for (int64_t osm = 1; osm <= 100; osm++) {
        double s = demand_stop_scale(seed, osm);
        /* Formula: round((0.6 + 1.2*u)*64)/64 where u in [0,1) -> k in [38, 115], 38/64=0.59375 */
        ASSERT_TRUE(s >= 38.0 / 64.0 - 1e-9);
        ASSERT_TRUE(s <= 115.0 / 64.0 + 1e-9);
        /* Quantised to 1/64: s * 64 must be integer */
        double intpart;
        double frac = modf(s * 64.0, &intpart);
        ASSERT_TRUE(fabs(frac) < 1e-9 || fabs(frac - 1.0) < 1e-9);
    }
}

/* 6. Extract stops from graph */
static void test_demand_stops_from_graph(void) {
    Graph *g = graph_create();
    /* Largest SCC: nodes 0, 1, 2 (cycle) */
    graph_add_node(g, 1001, 12.9767, 77.5713, "Majestic Hub", 1);    /* at Majestic -> HUB */
    graph_add_node(g, 1002, 12.9756, 77.6068, "MG Road Office", 1);   /* at MG Road -> OFFICE */
    graph_add_node(g, 1003, 12.5000, 77.5000, "Remote Stop", 1);      /* outside centres -> MARKET or RESIDENTIAL */
    graph_add_edge(g, 0, 1, 100.0, 10.0, "e01", ROAD_PRIMARY);
    graph_add_edge(g, 1, 2, 100.0, 10.0, "e12", ROAD_PRIMARY);
    graph_add_edge(g, 2, 0, 100.0, 10.0, "e20", ROAD_PRIMARY);

    /* Island node outside largest SCC (not a stop, or is a stop but isolated) */
    graph_add_node(g, 1004, 12.0000, 77.0000, "Island Stop", 1);

    DemandStop *stops = NULL;
    size_t count = 0;
    Status st = demand_stops_from_graph(g, 2, 42, &stops, &count);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(stops);
    ASSERT_EQ(count, 2); /* stride selection of 2 stops from 3 candidates in largest SCC */

    /* Node 3 (island stop) must NEVER be chosen */
    ASSERT_TRUE(stops[0].node != 3);
    ASSERT_TRUE(stops[1].node != 3);

    /* Verify classification */
    for (size_t i = 0; i < count; i++) {
        if (stops[i].node == 0) {
            ASSERT_EQ(stops[i].type, STOP_HUB);
        } else if (stops[i].node == 1) {
            ASSERT_EQ(stops[i].type, STOP_OFFICE);
        }
    }

    free(stops);

    /* Error cases */
    ASSERT_EQ(demand_stops_from_graph(g, 0, 42, &stops, &count), STATUS_ERR_INVALID);
    ASSERT_EQ(demand_stops_from_graph(NULL, 10, 42, &stops, &count), STATUS_ERR_NULL);

    graph_destroy(g);
}

/* 7. Poisson sampler tests */
static void test_demand_poisson_sampler(void) {
    ASSERT_DOUBLE_EQ(demand_poisson_sample(0.0, 123), 0.0, 1e-9);
    ASSERT_DOUBLE_EQ(demand_poisson_sample(-5.0, 123), 0.0, 1e-9);

    const int N_DRAWS = 20000;
    double lambdas[3] = { 3.0, 20.0, 200.0 };

    for (int li = 0; li < 3; li++) {
        double lam = lambdas[li];
        double sum = 0.0;
        double sum_sq = 0.0;

        for (int i = 0; i < N_DRAWS; i++) {
            uint64_t h = demand_hash(42, (uint64_t)li, (uint64_t)i, 777);
            double k = demand_poisson_sample(lam, h);
            sum += k;
            sum_sq += k * k;
        }

        double mean = sum / (double)N_DRAWS;
        double variance = (sum_sq - (sum * sum) / (double)N_DRAWS) / (double)(N_DRAWS - 1);
        double se = sqrt(lam / (double)N_DRAWS);

        /* Sample mean within 4 standard errors */
        ASSERT_TRUE(fabs(mean - lam) < 4.0 * se);
        /* Sample variance within 10% of lambda */
        ASSERT_TRUE(fabs(variance - lam) < 0.10 * lam);
    }
}

/* 8. Generation determinism and CSV round trip */
static void test_demand_generation_and_csv(void) {
    DemandStop stops[2];
    memset(stops, 0, sizeof(stops));
    stops[0].node = 10;
    stops[0].osm_id = 1010;
    stops[0].type = STOP_HUB;
    stops[0].scale = 1.0;
    stops[1].node = 20;
    stops[1].osm_id = 2020;
    stops[1].type = STOP_OFFICE;
    stops[1].scale = 1.125;

    DemandRow *r30 = NULL, *r60 = NULL;
    size_t n30 = 0, n60 = 0;

    Status st = demand_generate(stops, 2, 42, 30, &r30, &n30);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(r30);
    ASSERT_EQ(n30, 30 * 2 * 24);

    st = demand_generate(stops, 2, 42, 60, &r60, &n60);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(r60);
    ASSERT_EQ(n60, 60 * 2 * 24);

    /* First 30 days of 60-day run equal the 30-day run */
    for (size_t i = 0; i < n30; i++) {
        ASSERT_EQ(r30[i].day, r60[i].day);
        ASSERT_EQ(r30[i].stop_node, r60[i].stop_node);
        ASSERT_EQ(r30[i].hour, r60[i].hour);
        ASSERT_EQ(r30[i].event, r60[i].event);
        ASSERT_DOUBLE_EQ(r30[i].passengers, r60[i].passengers, 1e-9);
        ASSERT_DOUBLE_EQ(r30[i].lambda, r60[i].lambda, 1e-9);
    }

    /* Event rows exist only in hours 17..20 */
    for (size_t i = 0; i < n30; i++) {
        if (r30[i].event) {
            ASSERT_TRUE(r30[i].hour >= 17 && r30[i].hour <= 20);
        }
    }

    /* CSV Round trip */
    const char *path = "tmp_demand_test.csv";
    st = demand_rows_write_csv(r30, n30, path);
    ASSERT_EQ(st, STATUS_OK);

    DemandRow *loaded = NULL;
    size_t n_loaded = 0;
    st = demand_rows_load_csv(path, &loaded, &n_loaded);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(n_loaded, n30);

    for (size_t i = 0; i < n30; i++) {
        ASSERT_EQ(loaded[i].day, r30[i].day);
        ASSERT_EQ(loaded[i].stop_node, r30[i].stop_node);
        ASSERT_EQ(loaded[i].stop_type, r30[i].stop_type);
        ASSERT_DOUBLE_EQ(loaded[i].stop_scale, r30[i].stop_scale, 1e-6);
        ASSERT_EQ(loaded[i].hour, r30[i].hour);
        ASSERT_EQ(loaded[i].day_type, r30[i].day_type);
        ASSERT_EQ(loaded[i].rain, r30[i].rain);
        ASSERT_EQ(loaded[i].event, r30[i].event);
        ASSERT_DOUBLE_EQ(loaded[i].passengers, r30[i].passengers, 1e-6);
        ASSERT_DOUBLE_EQ(loaded[i].lambda, r30[i].lambda, 1e-6);
    }

    free(loaded);
    remove(path);
    free(r30);
    free(r60);
}

/* 9. Strict CSV error parsing and Category priority */
static void test_demand_csv_errors_and_categories(void) {
    DemandRow *rows = NULL;
    size_t n = 0;

    /* Missing file */
    ASSERT_EQ(demand_rows_load_csv("non_existent_demand_file.csv", &rows, &n), STATUS_ERR_NOT_FOUND);

    /* Empty file */
    FILE *f = fopen("tmp_empty.csv", "wb");
    fclose(f);
    ASSERT_EQ(demand_rows_load_csv("tmp_empty.csv", &rows, &n), STATUS_ERR_INVALID);
    remove("tmp_empty.csv");

    /* Wrong header */
    f = fopen("tmp_bad_hdr.csv", "wb");
    fprintf(f, "a,b,c\n1,2,3\n");
    fclose(f);
    ASSERT_EQ(demand_rows_load_csv("tmp_bad_hdr.csv", &rows, &n), STATUS_ERR_INVALID);
    remove("tmp_bad_hdr.csv");

    /* Missing column */
    f = fopen("tmp_miss_col.csv", "wb");
    fprintf(f, "day,stop_node,stop_type,stop_scale,hour,day_type,rain,event,passengers,lambda\n0,1,0,1.0,0,0,0,0,5\n");
    fclose(f);
    ASSERT_EQ(demand_rows_load_csv("tmp_miss_col.csv", &rows, &n), STATUS_ERR_INVALID);
    remove("tmp_miss_col.csv");

    /* Garbage number */
    f = fopen("tmp_garbage.csv", "wb");
    fprintf(f, "day,stop_node,stop_type,stop_scale,hour,day_type,rain,event,passengers,lambda\n0,1,0,1.0,abc,0,0,0,5,5.0\n");
    fclose(f);
    ASSERT_EQ(demand_rows_load_csv("tmp_garbage.csv", &rows, &n), STATUS_ERR_INVALID);
    remove("tmp_garbage.csv");

    /* Enum out of range */
    f = fopen("tmp_bad_enum.csv", "wb");
    fprintf(f, "day,stop_node,stop_type,stop_scale,hour,day_type,rain,event,passengers,lambda\n0,1,99,1.0,8,0,0,0,5,5.0\n");
    fclose(f);
    ASSERT_EQ(demand_rows_load_csv("tmp_bad_enum.csv", &rows, &n), STATUS_ERR_INVALID);
    remove("tmp_bad_enum.csv");

    /* Hour 24 */
    f = fopen("tmp_bad_hour.csv", "wb");
    fprintf(f, "day,stop_node,stop_type,stop_scale,hour,day_type,rain,event,passengers,lambda\n0,1,0,1.0,24,0,0,0,5,5.0\n");
    fclose(f);
    ASSERT_EQ(demand_rows_load_csv("tmp_bad_hour.csv", &rows, &n), STATUS_ERR_INVALID);
    remove("tmp_bad_hour.csv");

    /* Negative passengers */
    f = fopen("tmp_neg_pass.csv", "wb");
    fprintf(f, "day,stop_node,stop_type,stop_scale,hour,day_type,rain,event,passengers,lambda\n0,1,0,1.0,8,0,0,0,-5,5.0\n");
    fclose(f);
    ASSERT_EQ(demand_rows_load_csv("tmp_neg_pass.csv", &rows, &n), STATUS_ERR_INVALID);
    remove("tmp_neg_pass.csv");

    /* Priority test for categories: event > rain > holiday > weekend > weekday */
    DemandRow r;
    memset(&r, 0, sizeof(r));
    r.day_type = DAY_WEEKDAY;
    ASSERT_EQ(demand_row_category(&r), CAT_WEEKDAY);

    r.day_type = DAY_WEEKEND;
    ASSERT_EQ(demand_row_category(&r), CAT_WEEKEND);

    r.day_type = DAY_HOLIDAY;
    ASSERT_EQ(demand_row_category(&r), CAT_HOLIDAY);

    r.rain = 1;
    ASSERT_EQ(demand_row_category(&r), CAT_RAIN);

    r.event = 1;
    ASSERT_EQ(demand_row_category(&r), CAT_EVENT);

    ASSERT_STR_EQ(demand_category_name(CAT_WEEKDAY), "weekday");
    ASSERT_STR_EQ(demand_category_name(CAT_WEEKEND), "weekend");
    ASSERT_STR_EQ(demand_category_name(CAT_HOLIDAY), "holiday");
    ASSERT_STR_EQ(demand_category_name(CAT_RAIN), "rain");
    ASSERT_STR_EQ(demand_category_name(CAT_EVENT), "event");
}

static void test_demand_days_and_stops_dimension_check(void) {
    DemandStop stops[5];
    for (int i = 0; i < 5; i++) {
        stops[i].node = 100 + i;
        stops[i].osm_id = 1000 + i;
        stops[i].type = (StopType)(i % 4);
        stops[i].scale = 1.0;
        snprintf(stops[i].name, sizeof(stops[i].name), "Stop_%d", 100 + i);
    }

    DemandRow *rows = NULL;
    size_t n_rows = 0;
    int days = 3;
    size_t n_stops = 5;
    uint64_t seed = 42;

    Status st = demand_generate(stops, n_stops, seed, days, &rows, &n_rows);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(rows);
    ASSERT_EQ(n_rows, (size_t)(days * (int)n_stops * 24)); /* 3 * 5 * 24 = 360 */

    int max_day = -1;
    int min_day = 9999;
    int stop_counts[5] = {0};

    for (size_t i = 0; i < n_rows; i++) {
        if (rows[i].day > max_day) max_day = rows[i].day;
        if (rows[i].day < min_day) min_day = rows[i].day;

        int found = 0;
        for (int s = 0; s < 5; s++) {
            if (rows[i].stop_node == stops[s].node) {
                stop_counts[s]++;
                found = 1;
                break;
            }
        }
        ASSERT_EQ(found, 1);
    }

    ASSERT_EQ(min_day, 0);
    ASSERT_EQ(max_day, 2); /* 3 days: max day is 2 */

    for (int s = 0; s < 5; s++) {
        ASSERT_EQ(stop_counts[s], days * 24); /* each stop appears 3 * 24 = 72 times */
    }

    free(rows);
}

/*
 * 11. Hand-built graph test for demand_stops_from_graph_anchored.
 *
 * Hand derivation:
 * 10 nodes (0..9) in a single directed cycle (0->1->2->...->9->0), all is_stop=1.
 * Placed at known offsets from centres:
 *   Majestic (12.9767, 77.5713), radius 700m, max 3:
 *     Node 0: +0.001 deg lat (~111m) -> within 700m, 1st nearest
 *     Node 1: +0.002 deg lat (~222m) -> within 700m, 2nd nearest
 *     Node 2: +0.003 deg lat (~334m) -> within 700m, 3rd nearest
 *     Node 3: +0.004 deg lat (~445m) -> within 700m, 4th nearest (exceeds limit 3 -> not chosen as anchor)
 *     Node 4: +0.010 deg lat (~1112m)-> outside 700m radius -> not an anchor
 *   Shivajinagar (12.9857, 77.6057), radius 700m, max 3:
 *     Node 5: +0.001 deg lat (~111m) -> within 700m, 1st nearest (~1130m to MG Road)
 *     Node 6: +0.002 deg lat (~222m) -> within 700m, 2nd nearest (~1240m to MG Road)
 *   MG Road (12.9756, 77.6068), radius 1500m, max 2:
 *     Node 5, 6: within 1500m of MG Road, but already chosen as Shivajinagar anchors!
 *                Deduplication ensures they are NOT taken again.
 *     Node 7: +0.002 deg lat (~222m from MG Road) -> within 1500m -> chosen anchor
 *   Indiranagar (12.9784, 77.6408), radius 1500m, max 2:
 *     Node 8: +0.002 deg lat (~222m from Indiranagar) -> within 1500m -> chosen anchor
 *   Koramangala (12.9352, 77.6245), radius 1500m, max 2:
 *     No candidates within 1500m (Node 9 is >10km away at 12.5, 77.5).
 *
 * Ordered Anchors: [Node 0, Node 1, Node 2, Node 5, Node 6, Node 7, Node 8] (7 anchors total).
 *
 * Test Case 1: max_stops = 3 (< 7 anchors)
 *   Truncated in priority order: first 3 anchors [0, 1, 2].
 *   Final list sorted ascending: {0, 1, 2}.
 *
 * Test Case 2: max_stops = 9 (> 7 anchors)
 *   Anchors kept: {0, 1, 2, 5, 6, 7, 8} (7 anchors).
 *   Remaining candidates (anchors excluded): [3, 4, 9] (rem_count = 3).
 *   Remaining slots = 9 - 7 = 2 slots.
 *   Stride over [3, 4, 9]:
 *     k = 0: (0 * 3) / 2 = 0 -> Node 3
 *     k = 1: (1 * 3) / 2 = 1 -> Node 4
 *   Final selected nodes: {0, 1, 2, 5, 6, 7, 8} + {3, 4} = {0, 1, 2, 3, 4, 5, 6, 7, 8}.
 *   Final list sorted ascending: {0, 1, 2, 3, 4, 5, 6, 7, 8}.
 */
static void test_demand_stops_anchored_selection(void) {
    Graph *g = graph_create();
    ASSERT_NOT_NULL(g);

    /* Majestic: 12.9767, 77.5713 */
    graph_add_node(g, 100, 12.9767 + 0.001, 77.5713, "M1", 1); /* Node 0 (~111m) */
    graph_add_node(g, 101, 12.9767 + 0.002, 77.5713, "M2", 1); /* Node 1 (~222m) */
    graph_add_node(g, 102, 12.9767 + 0.003, 77.5713, "M3", 1); /* Node 2 (~334m) */
    graph_add_node(g, 103, 12.9767 + 0.004, 77.5713, "M4", 1); /* Node 3 (~445m) */
    graph_add_node(g, 104, 12.9767 + 0.010, 77.5713, "M_out", 1); /* Node 4 (~1112m, outside radius) */

    /* Shivajinagar: 12.9857, 77.6057 */
    graph_add_node(g, 105, 12.9857 + 0.001, 77.6057, "S1", 1); /* Node 5 (~111m) */
    graph_add_node(g, 106, 12.9857 + 0.002, 77.6057, "S2", 1); /* Node 6 (~222m) */

    /* MG Road: 12.9756, 77.6068 */
    graph_add_node(g, 107, 12.9756 + 0.002, 77.6068, "MG1", 1); /* Node 7 (~222m) */

    /* Indiranagar: 12.9784, 77.6408 */
    graph_add_node(g, 108, 12.9784 + 0.002, 77.6408, "IND1", 1); /* Node 8 (~222m) */

    /* Far remote stop: 12.5000, 77.5000 */
    graph_add_node(g, 109, 12.5000, 77.5000, "REMOTE", 1); /* Node 9 (>10km away) */

    /* Single directed cycle 0->1->2->...->9->0 so all 10 nodes are in largest SCC */
    for (int i = 0; i < 10; i++) {
        graph_add_edge(g, i, (i + 1) % 10, 100.0, 10.0, "cyc", ROAD_PRIMARY);
    }

    /* Sub-test 1: max_stops = 3 (< 7 anchors) */
    DemandStop *s3 = NULL;
    size_t c3 = 0;
    Status st = demand_stops_from_graph_anchored(g, 3, 42, &s3, &c3);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(s3);
    ASSERT_EQ(c3, 3);
    /* Truncated to first 3 anchors in priority order (Majestic anchors: Node 0, 1, 2) */
    ASSERT_EQ(s3[0].node, 0);
    ASSERT_EQ(s3[1].node, 1);
    ASSERT_EQ(s3[2].node, 2);
    /* Rule order: anchors within 700m of Majestic become STOP_HUB */
    ASSERT_EQ(s3[0].type, STOP_HUB);
    ASSERT_EQ(s3[1].type, STOP_HUB);
    ASSERT_EQ(s3[2].type, STOP_HUB);
    free(s3);

    /* Sub-test 2: max_stops = 9 (> 7 anchors) */
    DemandStop *s9 = NULL;
    size_t c9 = 0;
    st = demand_stops_from_graph_anchored(g, 9, 42, &s9, &c9);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(s9);
    ASSERT_EQ(c9, 9);
    /* Expected nodes: {0, 1, 2, 3, 4, 5, 6, 7, 8} */
    for (size_t i = 0; i < 9; i++) {
        ASSERT_EQ(s9[i].node, (int)i);
    }
    /* Verify output is sorted strictly ascending by node index */
    for (size_t i = 1; i < 9; i++) {
        ASSERT_TRUE(s9[i - 1].node < s9[i].node);
    }
    /* Verify types:
     * Node 0, 1, 2, 3: < 700m of Majestic -> STOP_HUB
     * Node 5, 6: < 700m of Shivajinagar -> STOP_HUB
     * Node 7: < 1500m of MG Road -> STOP_OFFICE
     * Node 8: < 1500m of Indiranagar -> STOP_OFFICE */
    ASSERT_EQ(s9[0].type, STOP_HUB);
    ASSERT_EQ(s9[1].type, STOP_HUB);
    ASSERT_EQ(s9[2].type, STOP_HUB);
    ASSERT_EQ(s9[3].type, STOP_HUB);
    ASSERT_EQ(s9[5].type, STOP_HUB);
    ASSERT_EQ(s9[6].type, STOP_HUB);
    ASSERT_EQ(s9[7].type, STOP_OFFICE);
    ASSERT_EQ(s9[8].type, STOP_OFFICE);

    /* Sub-test 3: Determinism check */
    DemandStop *s9_check = NULL;
    size_t c9_check = 0;
    st = demand_stops_from_graph_anchored(g, 9, 42, &s9_check, &c9_check);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_EQ(c9_check, c9);
    for (size_t i = 0; i < 9; i++) {
        ASSERT_EQ(s9[i].node, s9_check[i].node);
        ASSERT_EQ(s9[i].type, s9_check[i].type);
        ASSERT_DOUBLE_EQ(s9[i].scale, s9_check[i].scale, 1e-9);
        ASSERT_STR_EQ(s9[i].name, s9_check[i].name);
    }
    free(s9_check);
    free(s9);

    graph_destroy(g);
}

/*
 * 12. Error cases for demand_stops_from_graph_anchored.
 */
static void test_demand_stops_anchored_errors(void) {
    Graph *g = graph_create();
    ASSERT_NOT_NULL(g);
    graph_add_node(g, 1, 12.0, 77.0, "N0", 0); /* not a stop */
    graph_add_node(g, 2, 12.1, 77.1, "N1", 0); /* not a stop */
    graph_add_edge(g, 0, 1, 100.0, 10.0, "e01", ROAD_PRIMARY);
    graph_add_edge(g, 1, 0, 100.0, 10.0, "e10", ROAD_PRIMARY);

    DemandStop *stops = NULL;
    size_t count = 0;

    /* NULL checks */
    ASSERT_EQ(demand_stops_from_graph_anchored(NULL, 5, 42, &stops, &count), STATUS_ERR_NULL);
    ASSERT_EQ(demand_stops_from_graph_anchored(g, 5, 42, NULL, &count), STATUS_ERR_NULL);
    ASSERT_EQ(demand_stops_from_graph_anchored(g, 5, 42, &stops, NULL), STATUS_ERR_NULL);

    /* max_stops == 0 -> STATUS_ERR_INVALID */
    ASSERT_EQ(demand_stops_from_graph_anchored(g, 0, 42, &stops, &count), STATUS_ERR_INVALID);

    /* Graph has 0 stops in SCC -> STATUS_ERR_INVALID */
    ASSERT_EQ(demand_stops_from_graph_anchored(g, 5, 42, &stops, &count), STATUS_ERR_INVALID);
    ASSERT_NULL(stops);
    ASSERT_EQ(count, 0);

    graph_destroy(g);
}

int main(void) {
    RUN_TEST(test_demand_expected_hand_check);
    RUN_TEST(test_demand_features);
    RUN_TEST(test_demand_calendar);
    RUN_TEST(test_demand_event_stops);
    RUN_TEST(test_demand_stop_scale);
    RUN_TEST(test_demand_stops_from_graph);
    RUN_TEST(test_demand_poisson_sampler);
    RUN_TEST(test_demand_generation_and_csv);
    RUN_TEST(test_demand_csv_errors_and_categories);
    RUN_TEST(test_demand_days_and_stops_dimension_check);
    RUN_TEST(test_demand_stops_anchored_selection);
    RUN_TEST(test_demand_stops_anchored_errors);

    return test_summary();
}
