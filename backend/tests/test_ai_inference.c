#include "test_util.h"
#include "ai_inference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ASSERT_TRUE(c) TEST_ASSERT(c)
#define ASSERT_EQ(a,b) TEST_ASSERT_EQ_INT(a,b)
#define ASSERT_NULL(p) TEST_ASSERT_NULL(p)
#define ASSERT_NOT_NULL(p) TEST_ASSERT_NOT_NULL(p)
#define ASSERT_DOUBLE_EQ(a,b,tol) TEST_ASSERT(fabs((a)-(b))<=(tol))

/* 1. Golden test on mini_rf.txt and mini_golden.csv */
static void test_inference_mini_fixture(void) {
    ForestModel *fm = NULL;
    Status st = forest_load(&fm, "tests/fixtures/mini_rf.txt");
    if (st != STATUS_OK) {
        /* If running from different directory, check fixtures path */
        st = forest_load(&fm, "backend/tests/fixtures/mini_rf.txt");
    }
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(fm);
    ASSERT_EQ(forest_tree_count(fm), 3);
    ASSERT_TRUE(forest_node_count(fm) > 0);

    FILE *f = fopen("tests/fixtures/mini_golden.csv", "rb");
    if (!f) {
        f = fopen("backend/tests/fixtures/mini_golden.csv", "rb");
    }
    ASSERT_NOT_NULL(f);

    char line[512];
    /* Header */
    char *h = fgets(line, sizeof(line), f);
    ASSERT_NOT_NULL(h);

    size_t golden_rows = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        char *cursor = line;
        double feats[DEMAND_NUM_FEATURES];
        for (int i = 0; i < DEMAND_NUM_FEATURES; i++) {
            char *p = strchr(cursor, ',');
            ASSERT_NOT_NULL(p);
            *p = '\0';
            feats[i] = atof(cursor);
            cursor = p + 1;
        }
        double expected = atof(cursor);

        double pred = forest_predict(fm, feats);
        ASSERT_TRUE(fabs(pred - expected) < 1e-9);
        golden_rows++;
    }

    fclose(f);
    ASSERT_TRUE(golden_rows >= 224);

    forest_destroy(fm);
}

/* 2. Hand-written 2-tree model file with threshold equality and float32-cast edge cases */
static void test_inference_hand_written_and_edge_cases(void) {
    const char *path = "tmp_hand_rf.txt";
    FILE *f = fopen(path, "wb");
    ASSERT_NOT_NULL(f);

    /*
     * Tree 0:
     * Node 0: split on feature 0 (hour) <= 12.0 -> left node 1, right node 2
     * Node 1: leaf value 10.0
     * Node 2: leaf value 20.0
     *
     * Tree 1:
     * Tests float32 cast:
     * Feature 9 (stop_scale): input value 0.1 (double: 0.10000000000000000555...)
     * Cast to float32: (float)0.1 = 0.10000000149011611938...
     * Node 0: split on feature 9 <= (double)(float)0.1 -> left node 1, right node 2
     * Node 1: leaf value 100.0
     * Node 2: leaf value 200.0
     */
    float f32_val = 0.1f;
    double thresh_exact = (double)f32_val;

    fprintf(f, "RF_MODEL v1\n");
    fprintf(f, "FEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\n");
    fprintf(f, "TREES 2\n");
    fprintf(f, "TREE 0 NODES 3\n");
    fprintf(f, "1\t2\t0\t12.0\t15.0\n");
    fprintf(f, "-1\t-1\t-1\t0\t10.0\n");
    fprintf(f, "-1\t-1\t-1\t0\t20.0\n");
    fprintf(f, "END\n");
    fprintf(f, "TREE 1 NODES 3\n");
    fprintf(f, "1\t2\t9\t%.17g\t150.0\n", thresh_exact);
    fprintf(f, "-1\t-1\t-1\t0\t100.0\n");
    fprintf(f, "-1\t-1\t-1\t0\t200.0\n");
    fprintf(f, "END\n");
    fclose(f);

    ForestModel *fm = NULL;
    Status st = forest_load(&fm, path);
    ASSERT_EQ(st, STATUS_OK);
    ASSERT_NOT_NULL(fm);
    ASSERT_EQ(forest_tree_count(fm), 2);
    ASSERT_EQ(forest_node_count(fm), 6);

    double feats[DEMAND_NUM_FEATURES];
    memset(feats, 0, sizeof(feats));

    /* Case 1: hour = 12.0 (exact equality with threshold 12.0 -> goes left: val 10.0)
     * stop_scale = 0.1 (float cast equals thresh_exact -> goes left: val 100.0)
     * Average = (10 + 100) / 2 = 55.0
     */
    feats[0] = 12.0;
    feats[9] = 0.1;
    double p1 = forest_predict(fm, feats);
    ASSERT_DOUBLE_EQ(p1, 55.0, 1e-9);

    /* Case 2: hour = 12.0001 (strictly greater than 12.0 -> goes right: val 20.0)
     * Average = (20 + 100) / 2 = 60.0
     */
    feats[0] = 12.0001;
    double p2 = forest_predict(fm, feats);
    ASSERT_DOUBLE_EQ(p2, 60.0, 1e-9);

    /* Case 3: float32 cast edge test:
     * If feature 9 is slightly greater such that (float)feats[9] > (float)0.1 -> goes right: val 200.0
     * Average = (20 + 200) / 2 = 110.0
     */
    feats[9] = 0.1001;
    double p3 = forest_predict(fm, feats);
    ASSERT_DOUBLE_EQ(p3, 110.0, 1e-9);

    /* Non-finite and NULL features return NAN */
    ASSERT_TRUE(isnan(forest_predict(NULL, feats)));
    ASSERT_TRUE(isnan(forest_predict(fm, NULL)));
    feats[0] = NAN;
    ASSERT_TRUE(isnan(forest_predict(fm, feats)));
    feats[0] = INFINITY;
    ASSERT_TRUE(isnan(forest_predict(fm, feats)));

    forest_destroy(fm);
    remove(path);
}

/* 3. Strict malformed file validation tests */
static void test_inference_malformed_files(void) {
    ForestModel *fm = NULL;

    /* Missing file */
    ASSERT_EQ(forest_load(&fm, "missing_rf_file_xyz.txt"), STATUS_ERR_NOT_FOUND);
    ASSERT_NULL(fm);

    /* Empty file */
    FILE *f = fopen("tmp_m_empty.txt", "wb");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_empty.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_empty.txt");

    /* Wrong first line */
    f = fopen("tmp_m_line1.txt", "wb");
    fprintf(f, "INVALID_MODEL v1\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_line1.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_line1.txt");

    /* Wrong FEATURES line: wrong count */
    f = fopen("tmp_m_feat_cnt.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 9 hour weekend holiday rain event type_hub type_office type_market type_residential\nTREES 1\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_feat_cnt.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_feat_cnt.txt");

    /* Wrong FEATURES line: wrong order */
    f = fopen("tmp_m_feat_order.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 weekend hour holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_feat_order.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_feat_order.txt");

    /* T = 0 */
    f = fopen("tmp_m_t0.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 0\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_t0.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_t0.txt");

    /* T = 501 */
    f = fopen("tmp_m_t501.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 501\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_t501.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_t501.txt");

    /* Node count 0 */
    f = fopen("tmp_m_n0.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 0\nEND\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_n0.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_n0.txt");

    /* Child index <= parent index (left child 0 <= 0) */
    f = fopen("tmp_m_child_le.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 3\n0\t2\t0\t10.0\t1.0\n-1\t-1\t-1\t0\t1.0\n-1\t-1\t-1\t0\t1.0\nEND\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_child_le.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_child_le.txt");

    /* Child index out of range (right child 5 >= node count 3) */
    f = fopen("tmp_m_child_oor.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 3\n1\t5\t0\t10.0\t1.0\n-1\t-1\t-1\t0\t1.0\n-1\t-1\t-1\t0\t1.0\nEND\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_child_oor.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_child_oor.txt");

    /* Node with exactly one child */
    f = fopen("tmp_m_one_child.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 2\n1\t-1\t0\t10.0\t1.0\n-1\t-1\t-1\t0\t1.0\nEND\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_one_child.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_one_child.txt");

    /* Leaf with a child */
    f = fopen("tmp_m_leaf_child.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 1\n-1\t-1\t2\t0\t1.0\nEND\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_leaf_child.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_leaf_child.txt");

    /* NaN threshold */
    f = fopen("tmp_m_nan.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 3\n1\t2\t0\tnan\t1.0\n-1\t-1\t-1\t0\t1.0\n-1\t-1\t-1\t0\t1.0\nEND\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_nan.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_nan.txt");

    /* Missing END */
    f = fopen("tmp_m_no_end.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 1\n-1\t-1\t-1\t0\t1.0\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_no_end.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_no_end.txt");

    /* Trailing garbage */
    f = fopen("tmp_m_trailing.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 1\n-1\t-1\t-1\t0\t1.0\nEND\nEXTRA_GARBAGE\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_trailing.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_trailing.txt");

    /* Truncated file */
    f = fopen("tmp_m_trunc.txt", "wb");
    fprintf(f, "RF_MODEL v1\nFEATURES 10 hour weekend holiday rain event type_hub type_office type_market type_residential stop_scale\nTREES 1\nTREE 0 NODES 3\n1\t2\t0\t10.0\t1.0\n");
    fclose(f);
    ASSERT_EQ(forest_load(&fm, "tmp_m_trunc.txt"), STATUS_ERR_INVALID);
    ASSERT_NULL(fm);
    remove("tmp_m_trunc.txt");
}

int main(void) {
    RUN_TEST(test_inference_mini_fixture);
    RUN_TEST(test_inference_hand_written_and_edge_cases);
    RUN_TEST(test_inference_malformed_files);

    return test_summary();
}
