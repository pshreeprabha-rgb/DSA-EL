#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "test_util.h"
#include "avl.h"

static int g_avl_free_count = 0;

static void avl_item_free(void *item) {
    if (item) {
        g_avl_free_count++;
        free(item);
    }
}

static int cmp_int(const void *a, const void *b) {
    int va = *(const int *)a;
    int vb = *(const int *)b;
    return (va < vb) ? -1 : (va > vb) ? 1 : 0;
}

typedef struct InorderCollect {
    int arr[1200];
    size_t count;
} InorderCollect;

static void collect_inorder(void *item, void *ctx) {
    InorderCollect *c = (InorderCollect *)ctx;
    if (c->count < 1200) {
        c->arr[c->count++] = *(int *)item;
    }
}

static void test_avl_null_args(void) {
    avl_destroy(NULL);
    TEST_ASSERT_NULL(avl_create(NULL, NULL));
    TEST_ASSERT_EQ_INT(avl_insert(NULL, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_EQ_INT(avl_delete(NULL, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_NULL(avl_search(NULL, NULL));
    TEST_ASSERT_NULL(avl_min(NULL));
    TEST_ASSERT_NULL(avl_max(NULL));
    TEST_ASSERT_NULL(avl_lower_bound(NULL, NULL));
    avl_inorder(NULL, collect_inorder, NULL);
    TEST_ASSERT_EQ_INT(avl_size(NULL), 0);
    TEST_ASSERT_EQ_INT(avl_height(NULL), 0);
    TEST_ASSERT_EQ_INT(avl_is_balanced(NULL), 0);
}

static void test_avl_rotations(void) {
    /* 1. LL case: 30, 20, 10 */
    int ll_vals[3] = {30, 20, 10};
    Avl *t_ll = avl_create(cmp_int, NULL);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQ_INT(avl_insert(t_ll, &ll_vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(avl_is_balanced(t_ll), 1);
    TEST_ASSERT_EQ_INT(avl_height(t_ll), 2);
    TEST_ASSERT_EQ_INT(*(int *)avl_min(t_ll), 10);
    TEST_ASSERT_EQ_INT(*(int *)avl_max(t_ll), 30);
    avl_destroy(t_ll);

    /* 2. RR case: 10, 20, 30 */
    int rr_vals[3] = {10, 20, 30};
    Avl *t_rr = avl_create(cmp_int, NULL);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQ_INT(avl_insert(t_rr, &rr_vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(avl_is_balanced(t_rr), 1);
    TEST_ASSERT_EQ_INT(avl_height(t_rr), 2);
    avl_destroy(t_rr);

    /* 3. LR case: 30, 10, 20 */
    int lr_vals[3] = {30, 10, 20};
    Avl *t_lr = avl_create(cmp_int, NULL);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQ_INT(avl_insert(t_lr, &lr_vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(avl_is_balanced(t_lr), 1);
    TEST_ASSERT_EQ_INT(avl_height(t_lr), 2);
    avl_destroy(t_lr);

    /* 4. RL case: 10, 30, 20 */
    int rl_vals[3] = {10, 30, 20};
    Avl *t_rl = avl_create(cmp_int, NULL);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQ_INT(avl_insert(t_rl, &rl_vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(avl_is_balanced(t_rl), 1);
    TEST_ASSERT_EQ_INT(avl_height(t_rl), 2);
    avl_destroy(t_rl);
}

static void test_avl_duplicates_and_missing(void) {
    int v[3] = {10, 20, 30};
    Avl *t = avl_create(cmp_int, NULL);
    for (int i = 0; i < 3; i++) {
        avl_insert(t, &v[i]);
    }
    /* Duplicate insertion */
    int dup = 20;
    TEST_ASSERT_EQ_INT(avl_insert(t, &dup), STATUS_ERR_DUPLICATE);
    TEST_ASSERT_EQ_INT(avl_size(t), 3);

    /* Search existing and missing */
    int probe = 20;
    TEST_ASSERT_NOT_NULL(avl_search(t, &probe));
    int missing = 99;
    TEST_ASSERT_NULL(avl_search(t, &missing));

    /* Delete missing key */
    TEST_ASSERT_EQ_INT(avl_delete(t, &missing), STATUS_ERR_NOT_FOUND);
    TEST_ASSERT_EQ_INT(avl_size(t), 3);

    avl_destroy(t);
}

static void test_avl_lower_bound(void) {
    int vals[5] = {10, 20, 30, 40, 50};
    Avl *t = avl_create(cmp_int, NULL);
    for (int i = 0; i < 5; i++) {
        avl_insert(t, &vals[i]);
    }

    int p1 = 25;
    int *res1 = (int *)avl_lower_bound(t, &p1);
    TEST_ASSERT_NOT_NULL(res1);
    TEST_ASSERT_EQ_INT(*res1, 30);

    int p2 = 30;
    int *res2 = (int *)avl_lower_bound(t, &p2);
    TEST_ASSERT_NOT_NULL(res2);
    TEST_ASSERT_EQ_INT(*res2, 30);

    int p3 = 5;
    int *res3 = (int *)avl_lower_bound(t, &p3);
    TEST_ASSERT_NOT_NULL(res3);
    TEST_ASSERT_EQ_INT(*res3, 10);

    int p4 = 55;
    int *res4 = (int *)avl_lower_bound(t, &p4);
    TEST_ASSERT_NULL(res4);

    avl_destroy(t);
}

static void test_avl_1000_ascending(void) {
    Avl *t = avl_create(cmp_int, NULL);
    TEST_ASSERT_NOT_NULL(t);

    int vals[1000];
    for (int i = 0; i < 1000; i++) {
        vals[i] = i;
        TEST_ASSERT_EQ_INT(avl_insert(t, &vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(avl_size(t), 1000);
    TEST_ASSERT_EQ_INT(avl_is_balanced(t), 1);

    /* Height constraint: avl_height <= 1.45 * log2(n + 2) */
    double max_allowed = 1.45 * (log(1002.0) / log(2.0));
    int h = avl_height(t);
    TEST_ASSERT((double)h <= max_allowed);

    /* Check in-order output is strictly ascending */
    InorderCollect coll = { {0}, 0 };
    avl_inorder(t, collect_inorder, &coll);
    TEST_ASSERT_EQ_INT(coll.count, 1000);
    for (size_t i = 0; i < 1000; i++) {
        TEST_ASSERT_EQ_INT(coll.arr[i], (int)i);
    }

    avl_destroy(t);
}

static void test_avl_1000_descending_and_random(void) {
    /* Descending */
    Avl *t_desc = avl_create(cmp_int, NULL);
    int vals_desc[1000];
    for (int i = 0; i < 1000; i++) {
        vals_desc[i] = 1000 - i;
        TEST_ASSERT_EQ_INT(avl_insert(t_desc, &vals_desc[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(avl_size(t_desc), 1000);
    TEST_ASSERT_EQ_INT(avl_is_balanced(t_desc), 1);
    avl_destroy(t_desc);

    /* Random */
    srand(42);
    Avl *t_rand = avl_create(cmp_int, NULL);
    int vals_rand[1000];
    for (int i = 0; i < 1000; i++) {
        vals_rand[i] = rand() % 500000;
        /* Status might be DUPLICATE if random repeats, which is fine */
        avl_insert(t_rand, &vals_rand[i]);
    }
    TEST_ASSERT_EQ_INT(avl_is_balanced(t_rand), 1);
    avl_destroy(t_rand);
}

static void test_avl_delete_every_other(void) {
    /* Insert 1000 items in ascending order */
    Avl *t = avl_create(cmp_int, NULL);
    int vals[1000];
    for (int i = 0; i < 1000; i++) {
        vals[i] = i * 2;
        avl_insert(t, &vals[i]);
    }
    TEST_ASSERT_EQ_INT(avl_size(t), 1000);
    TEST_ASSERT_EQ_INT(avl_is_balanced(t), 1);

    /* Delete every other item: 0, 4, 8, ... (500 items)
     * and check avl_is_balanced after EVERY deletion */
    for (int i = 0; i < 1000; i += 2) {
        int probe = i * 2;
        TEST_ASSERT_EQ_INT(avl_delete(t, &probe), STATUS_OK);
        TEST_ASSERT_EQ_INT(avl_is_balanced(t), 1);
    }
    TEST_ASSERT_EQ_INT(avl_size(t), 500);

    /* Verify the remaining items are the odd indices (i=1, 3, 5, ...) */
    for (int i = 1; i < 1000; i += 2) {
        int probe = i * 2;
        TEST_ASSERT_NOT_NULL(avl_search(t, &probe));
    }

    avl_destroy(t);
}

static void test_avl_ownership(void) {
    g_avl_free_count = 0;
    Avl *t = avl_create(cmp_int, avl_item_free);
    TEST_ASSERT_NOT_NULL(t);

    for (int i = 0; i < 10; i++) {
        int *p = (int *)malloc(sizeof(int));
        *p = i * 10;
        avl_insert(t, p);
    }
    TEST_ASSERT_EQ_INT(avl_size(t), 10);
    TEST_ASSERT_EQ_INT(g_avl_free_count, 0);

    /* Delete single node: frees stored item */
    int probe = 30;
    TEST_ASSERT_EQ_INT(avl_delete(t, &probe), STATUS_OK);
    TEST_ASSERT_EQ_INT(g_avl_free_count, 1);
    TEST_ASSERT_EQ_INT(avl_size(t), 9);

    /* Destroy tree: frees remaining 9 items */
    avl_destroy(t);
    TEST_ASSERT_EQ_INT(g_avl_free_count, 10);
}

int main(void) {
    RUN_TEST(test_avl_null_args);
    RUN_TEST(test_avl_rotations);
    RUN_TEST(test_avl_duplicates_and_missing);
    RUN_TEST(test_avl_lower_bound);
    RUN_TEST(test_avl_1000_ascending);
    RUN_TEST(test_avl_1000_descending_and_random);
    RUN_TEST(test_avl_delete_every_other);
    RUN_TEST(test_avl_ownership);
    return test_summary();
}

