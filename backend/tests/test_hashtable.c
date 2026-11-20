#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_util.h"
#include "hashtable.h"

static int g_ht_free_count = 0;

static void ht_value_free(void *item) {
    if (item) {
        g_ht_free_count++;
        free(item);
    }
}

typedef struct HtVisitCtx {
    int sum;
    size_t count;
} HtVisitCtx;

static void sum_values(void *item, void *ctx) {
    if (item && ctx) {
        HtVisitCtx *c = (HtVisitCtx *)ctx;
        c->sum += *(int *)item;
        c->count++;
    }
}

static void test_ht_null_and_safety(void) {
    ht_destroy(NULL);
    ht_clear(NULL);
    TEST_ASSERT_EQ_INT(ht_put_int(NULL, 1, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_NULL(ht_get_int(NULL, 1));
    TEST_ASSERT_EQ_INT(ht_contains_int(NULL, 1), 0);
    TEST_ASSERT_EQ_INT(ht_remove_int(NULL, 1), STATUS_ERR_NULL);

    TEST_ASSERT_EQ_INT(ht_put_str(NULL, "a", NULL), STATUS_ERR_NULL);
    TEST_ASSERT_NULL(ht_get_str(NULL, "a"));
    TEST_ASSERT_EQ_INT(ht_contains_str(NULL, "a"), 0);
    TEST_ASSERT_EQ_INT(ht_remove_str(NULL, "a"), STATUS_ERR_NULL);

    TEST_ASSERT_EQ_INT(ht_size(NULL), 0);
    TEST_ASSERT_EQ_INT(ht_bucket_count(NULL), 0);
    ht_foreach(NULL, sum_values, NULL);
}

static void test_ht_wrong_key_kind(void) {
    HashTable *hi = ht_create(HT_KEY_INT, NULL);
    HashTable *hs = ht_create(HT_KEY_STR, NULL);

    /* Calling string methods on integer table */
    TEST_ASSERT_EQ_INT(ht_put_str(hi, "key", NULL), STATUS_ERR_INVALID);
    TEST_ASSERT_NULL(ht_get_str(hi, "key"));
    TEST_ASSERT_EQ_INT(ht_contains_str(hi, "key"), 0);
    TEST_ASSERT_EQ_INT(ht_remove_str(hi, "key"), STATUS_ERR_INVALID);

    /* Calling int methods on string table */
    TEST_ASSERT_EQ_INT(ht_put_int(hs, 123, NULL), STATUS_ERR_INVALID);
    TEST_ASSERT_NULL(ht_get_int(hs, 123));
    TEST_ASSERT_EQ_INT(ht_contains_int(hs, 123), 0);
    TEST_ASSERT_EQ_INT(ht_remove_int(hs, 123), STATUS_ERR_INVALID);

    ht_destroy(hi);
    ht_destroy(hs);
}

static void test_ht_set_usage_null_values(void) {
    HashTable *h = ht_create(HT_KEY_INT, NULL);
    TEST_ASSERT_NOT_NULL(h);

    /* Insert with NULL value (using table as a set) */
    TEST_ASSERT_EQ_INT(ht_put_int(h, 42, NULL), STATUS_OK);
    TEST_ASSERT_EQ_INT(ht_contains_int(h, 42), 1);
    TEST_ASSERT_NULL(ht_get_int(h, 42)); /* Ambiguous with absent, contains is authoritative */

    TEST_ASSERT_EQ_INT(ht_contains_int(h, 99), 0);
    TEST_ASSERT_NULL(ht_get_int(h, 99));

    TEST_ASSERT_EQ_INT(ht_remove_int(h, 42), STATUS_OK);
    TEST_ASSERT_EQ_INT(ht_contains_int(h, 42), 0);

    ht_destroy(h);
}

static void test_ht_10000_ints_and_removal(void) {
    HashTable *h = ht_create(HT_KEY_INT, NULL);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQ_INT(ht_bucket_count(h), 16);

    int *vals = (int *)malloc(10000 * sizeof(int));
    TEST_ASSERT_NOT_NULL(vals);

    /* Insert 10,000 int keys (forces multiple resizes from 16 to > 16384) */
    for (int i = 0; i < 10000; i++) {
        vals[i] = i * 7;
        TEST_ASSERT_EQ_INT(ht_put_int(h, (int64_t)i, &vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(ht_size(h), 10000);
    TEST_ASSERT(ht_bucket_count(h) >= 16384);

    /* Verify all 10,000 keys exist and match values */
    for (int i = 0; i < 10000; i++) {
        TEST_ASSERT_EQ_INT(ht_contains_int(h, (int64_t)i), 1);
        int *v = (int *)ht_get_int(h, (int64_t)i);
        TEST_ASSERT_NOT_NULL(v);
        TEST_ASSERT_EQ_INT(*v, i * 7);
    }

    /* Replace existing key */
    int replacement = 999999;
    TEST_ASSERT_EQ_INT(ht_put_int(h, 500, &replacement), STATUS_OK);
    TEST_ASSERT_EQ_INT(ht_size(h), 10000);
    TEST_ASSERT_EQ_INT(*(int *)ht_get_int(h, 500), 999999);

    /* Remove half (even indices) and verify the others remain */
    for (int i = 0; i < 10000; i += 2) {
        TEST_ASSERT_EQ_INT(ht_remove_int(h, (int64_t)i), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(ht_size(h), 5000);

    for (int i = 0; i < 10000; i++) {
        if (i % 2 == 0) {
            TEST_ASSERT_EQ_INT(ht_contains_int(h, (int64_t)i), 0);
            TEST_ASSERT_NULL(ht_get_int(h, (int64_t)i));
        } else {
            TEST_ASSERT_EQ_INT(ht_contains_int(h, (int64_t)i), 1);
            TEST_ASSERT_NOT_NULL(ht_get_int(h, (int64_t)i));
        }
    }

    free(vals);
    ht_destroy(h);
}

static void test_ht_string_keys_and_ownership(void) {
    g_ht_free_count = 0;
    HashTable *h = ht_create(HT_KEY_STR, ht_value_free);
    TEST_ASSERT_NOT_NULL(h);

    char key_buf[32];
    for (int i = 0; i < 20; i++) {
        snprintf(key_buf, sizeof(key_buf), "route_%d", i);
        int *val = (int *)malloc(sizeof(int));
        *val = i * 10;
        TEST_ASSERT_EQ_INT(ht_put_str(h, key_buf, val), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(ht_size(h), 20);
    TEST_ASSERT_EQ_INT(g_ht_free_count, 0);

    /* Replace value for route_5 */
    int *rep_val = (int *)malloc(sizeof(int));
    *rep_val = 5555;
    TEST_ASSERT_EQ_INT(ht_put_str(h, "route_5", rep_val), STATUS_OK);
    /* Old value replaced and freed */
    TEST_ASSERT_EQ_INT(g_ht_free_count, 1);
    TEST_ASSERT_EQ_INT(ht_size(h), 20);
    TEST_ASSERT_EQ_INT(*(int *)ht_get_str(h, "route_5"), 5555);

    /* Remove route_5 */
    TEST_ASSERT_EQ_INT(ht_remove_str(h, "route_5"), STATUS_OK);
    TEST_ASSERT_EQ_INT(g_ht_free_count, 2);
    TEST_ASSERT_EQ_INT(ht_size(h), 19);
    TEST_ASSERT_EQ_INT(ht_contains_str(h, "route_5"), 0);

    /* Foreach traversal */
    HtVisitCtx ctx = { 0, 0 };
    ht_foreach(h, sum_values, &ctx);
    TEST_ASSERT_EQ_INT(ctx.count, 19);

    /* ht_destroy frees all remaining 19 items and their string keys */
    ht_destroy(h);
    TEST_ASSERT_EQ_INT(g_ht_free_count, 21);
}

static void test_ht_colliding_keys(void) {
    /* Test chaining with multiple keys */
    HashTable *h = ht_create(HT_KEY_INT, NULL);
    int vals[5] = {1, 2, 3, 4, 5};
    /* Intentionally add multiple keys */
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ_INT(ht_put_int(h, (int64_t)(i * 16), &vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(ht_size(h), 5);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ_INT(ht_contains_int(h, (int64_t)(i * 16)), 1);
        TEST_ASSERT_EQ_INT(*(int *)ht_get_int(h, (int64_t)(i * 16)), vals[i]);
    }
    /* Remove from middle of chain */
    TEST_ASSERT_EQ_INT(ht_remove_int(h, 32), STATUS_OK);
    TEST_ASSERT_EQ_INT(ht_contains_int(h, 32), 0);
    TEST_ASSERT_EQ_INT(ht_contains_int(h, 0), 1);
    TEST_ASSERT_EQ_INT(ht_contains_int(h, 16), 1);
    TEST_ASSERT_EQ_INT(ht_contains_int(h, 48), 1);
    TEST_ASSERT_EQ_INT(ht_contains_int(h, 64), 1);

    ht_destroy(h);
}

int main(void) {
    RUN_TEST(test_ht_null_and_safety);
    RUN_TEST(test_ht_wrong_key_kind);
    RUN_TEST(test_ht_set_usage_null_values);
    RUN_TEST(test_ht_10000_ints_and_removal);
    RUN_TEST(test_ht_string_keys_and_ownership);
    RUN_TEST(test_ht_colliding_keys);
    return test_summary();
}

