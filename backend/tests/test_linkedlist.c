#include <stdio.h>
#include <stdlib.h>
#include "test_util.h"
#include "linkedlist.h"

static int g_free_counter = 0;

static void custom_free(void *item) {
    if (item) {
        g_free_counter++;
        free(item);
    }
}

static int is_even(const void *item, const void *ctx) {
    (void)ctx;
    if (!item) return 0;
    int val = *(const int *)item;
    return (val % 2 == 0);
}

static int is_match_val(const void *item, const void *ctx) {
    if (!item || !ctx) return 0;
    return (*(const int *)item == *(const int *)ctx);
}

typedef struct ForwardCtx {
    int arr[16];
    size_t count;
} ForwardCtx;

static void collect_forward(void *item, void *ctx) {
    ForwardCtx *c = (ForwardCtx *)ctx;
    if (c->count < 16) {
        c->arr[c->count++] = *(int *)item;
    }
}

static void collect_reverse(void *item, void *ctx) {
    ForwardCtx *c = (ForwardCtx *)ctx;
    if (c->count < 16) {
        c->arr[c->count++] = *(int *)item;
    }
}

static void test_null_args(void) {
    ll_destroy(NULL);
    ll_clear(NULL);
    TEST_ASSERT_EQ_INT(ll_append(NULL, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_EQ_INT(ll_prepend(NULL, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_EQ_INT(ll_insert_at(NULL, 0, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_EQ_INT(ll_remove_at(NULL, 0), STATUS_ERR_NULL);
    TEST_ASSERT_EQ_INT(ll_remove_if(NULL, is_even, NULL), 0);
    TEST_ASSERT_NULL(ll_get(NULL, 0));
    TEST_ASSERT_NULL(ll_find(NULL, is_even, NULL));
    ll_foreach(NULL, collect_forward, NULL);
    ll_foreach_reverse(NULL, collect_reverse, NULL);
    TEST_ASSERT_EQ_INT(ll_size(NULL), 0);
    TEST_ASSERT_EQ_INT(ll_is_empty(NULL), 1);
}

static void test_empty_list(void) {
    LinkedList *l = ll_create(NULL);
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_EQ_INT(ll_size(l), 0);
    TEST_ASSERT_EQ_INT(ll_is_empty(l), 1);
    TEST_ASSERT_NULL(ll_get(l, 0));
    TEST_ASSERT_NULL(ll_find(l, is_even, NULL));
    TEST_ASSERT_EQ_INT(ll_remove_at(l, 0), STATUS_ERR_EMPTY);
    TEST_ASSERT_EQ_INT(ll_insert_at(l, 1, NULL), STATUS_ERR_INVALID);
    ll_clear(l);
    TEST_ASSERT_EQ_INT(ll_size(l), 0);
    ll_destroy(l);
}

static void test_single_element_and_removals(void) {
    int v1 = 10;
    LinkedList *l = ll_create(NULL);
    TEST_ASSERT_NOT_NULL(l);

    /* Append single element */
    TEST_ASSERT_EQ_INT(ll_append(l, &v1), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(l), 1);
    TEST_ASSERT_EQ_INT(ll_is_empty(l), 0);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 0), 10);

    /* Remove only element */
    TEST_ASSERT_EQ_INT(ll_remove_at(l, 0), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(l), 0);
    TEST_ASSERT_EQ_INT(ll_is_empty(l), 1);
    TEST_ASSERT_NULL(ll_get(l, 0));

    /* Prepend single element */
    TEST_ASSERT_EQ_INT(ll_prepend(l, &v1), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(l), 1);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 0), 10);
    TEST_ASSERT_EQ_INT(ll_remove_at(l, 0), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(l), 0);

    ll_destroy(l);
}

static void test_remove_first_last_middle(void) {
    int v[5] = {1, 2, 3, 4, 5};
    LinkedList *l = ll_create(NULL);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ_INT(ll_append(l, &v[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(ll_size(l), 5);

    /* Remove middle element (index 2: val 3) -> [1, 2, 4, 5] */
    TEST_ASSERT_EQ_INT(ll_remove_at(l, 2), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(l), 4);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 0), 1);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 1), 2);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 2), 4);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 3), 5);

    /* Remove first element (index 0: val 1) -> [2, 4, 5] */
    TEST_ASSERT_EQ_INT(ll_remove_at(l, 0), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(l), 3);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 0), 2);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 1), 4);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 2), 5);

    /* Remove last element (index 2: val 5) -> [2, 4] */
    TEST_ASSERT_EQ_INT(ll_remove_at(l, 2), STATUS_OK);
    TEST_ASSERT_EQ_INT(ll_size(l), 2);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 0), 2);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 1), 4);

    /* Check boundary removal */
    TEST_ASSERT_EQ_INT(ll_remove_at(l, 5), STATUS_ERR_INVALID);

    ll_destroy(l);
}

static void test_insert_at(void) {
    int v[4] = {10, 20, 30, 40};
    LinkedList *l = ll_create(NULL);

    /* Insert at 0 on empty list */
    TEST_ASSERT_EQ_INT(ll_insert_at(l, 0, &v[1]), STATUS_OK); /* [20] */
    /* Insert at 0 (prepend) */
    TEST_ASSERT_EQ_INT(ll_insert_at(l, 0, &v[0]), STATUS_OK); /* [10, 20] */
    /* Insert at size (append) */
    TEST_ASSERT_EQ_INT(ll_insert_at(l, 2, &v[3]), STATUS_OK); /* [10, 20, 40] */
    /* Insert in middle */
    TEST_ASSERT_EQ_INT(ll_insert_at(l, 2, &v[2]), STATUS_OK); /* [10, 20, 30, 40] */

    TEST_ASSERT_EQ_INT(ll_size(l), 4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQ_INT(*(int *)ll_get(l, (size_t)i), v[i]);
    }

    ll_destroy(l);
}

static void test_remove_if(void) {
    int v[6] = {1, 2, 4, 5, 6, 7};
    LinkedList *l = ll_create(NULL);
    for (int i = 0; i < 6; i++) {
        ll_append(l, &v[i]);
    }
    /* Remove even numbers: 2, 4, 6 */
    size_t removed = ll_remove_if(l, is_even, NULL);
    TEST_ASSERT_EQ_INT(removed, 3);
    TEST_ASSERT_EQ_INT(ll_size(l), 3);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 0), 1);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 1), 5);
    TEST_ASSERT_EQ_INT(*(int *)ll_get(l, 2), 7);

    ll_destroy(l);
}

static void test_traversals(void) {
    int v[4] = {10, 20, 30, 40};
    LinkedList *l = ll_create(NULL);
    for (int i = 0; i < 4; i++) {
        ll_append(l, &v[i]);
    }

    ForwardCtx fwd = { {0}, 0 };
    ForwardCtx rev = { {0}, 0 };
    ll_foreach(l, collect_forward, &fwd);
    ll_foreach_reverse(l, collect_reverse, &rev);

    TEST_ASSERT_EQ_INT(fwd.count, 4);
    TEST_ASSERT_EQ_INT(rev.count, 4);
    for (size_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQ_INT(fwd.arr[i], v[i]);
        TEST_ASSERT_EQ_INT(rev.arr[i], v[3 - i]);
    }

    int target = 30;
    int *found = (int *)ll_find(l, is_match_val, &target);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQ_INT(*found, 30);

    int missing = 99;
    TEST_ASSERT_NULL(ll_find(l, is_match_val, &missing));

    ll_destroy(l);
}

static void test_ownership(void) {
    g_free_counter = 0;
    LinkedList *l = ll_create(custom_free);
    TEST_ASSERT_NOT_NULL(l);

    for (int i = 0; i < 5; i++) {
        int *val = (int *)malloc(sizeof(int));
        *val = i * 10;
        ll_append(l, val);
    }
    TEST_ASSERT_EQ_INT(ll_size(l), 5);
    TEST_ASSERT_EQ_INT(g_free_counter, 0);

    /* Remove index 1: frees item */
    TEST_ASSERT_EQ_INT(ll_remove_at(l, 1), STATUS_OK);
    TEST_ASSERT_EQ_INT(g_free_counter, 1);
    TEST_ASSERT_EQ_INT(ll_size(l), 4);

    /* ll_clear frees remaining 4 */
    ll_clear(l);
    TEST_ASSERT_EQ_INT(g_free_counter, 5);
    TEST_ASSERT_EQ_INT(ll_size(l), 0);

    /* Add another item and test ll_destroy frees it */
    int *val2 = (int *)malloc(sizeof(int));
    *val2 = 100;
    ll_append(l, val2);
    ll_destroy(l);
    TEST_ASSERT_EQ_INT(g_free_counter, 6);
}

int main(void) {
    RUN_TEST(test_null_args);
    RUN_TEST(test_empty_list);
    RUN_TEST(test_single_element_and_removals);
    RUN_TEST(test_remove_first_last_middle);
    RUN_TEST(test_insert_at);
    RUN_TEST(test_remove_if);
    RUN_TEST(test_traversals);
    RUN_TEST(test_ownership);
    return test_summary();
}

