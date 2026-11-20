#include <stdio.h>
#include <stdlib.h>
#include "test_util.h"
#include "heap.h"

static int g_heap_free_count = 0;

static void heap_item_free(void *item) {
    if (item) {
        g_heap_free_count++;
        free(item);
    }
}

static int cmp_int_asc(const void *a, const void *b) {
    int val_a = *(const int *)a;
    int val_b = *(const int *)b;
    return (val_a < val_b) ? -1 : (val_a > val_b) ? 1 : 0;
}

static int cmp_int_desc(const void *a, const void *b) {
    int val_a = *(const int *)a;
    int val_b = *(const int *)b;
    return (val_a > val_b) ? -1 : (val_a < val_b) ? 1 : 0;
}

typedef struct TrackedItem {
    int id;
    int priority;
    size_t heap_idx;
} TrackedItem;

static void track_index_cb(void *item, size_t index) {
    if (item) {
        TrackedItem *t = (TrackedItem *)item;
        t->heap_idx = index;
    }
}

static int cmp_tracked_asc(const void *a, const void *b) {
    const TrackedItem *ta = (const TrackedItem *)a;
    const TrackedItem *tb = (const TrackedItem *)b;
    return (ta->priority < tb->priority) ? -1 : (ta->priority > tb->priority) ? 1 : 0;
}

static void test_heap_null_args(void) {
    heap_destroy(NULL);
    heap_clear(NULL);
    TEST_ASSERT_NULL(heap_create(NULL, NULL, NULL));
    TEST_ASSERT_EQ_INT(heap_push(NULL, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_NULL(heap_pop(NULL));
    TEST_ASSERT_NULL(heap_peek(NULL));
    TEST_ASSERT_EQ_INT(heap_update(NULL, 0), STATUS_ERR_NULL);
    TEST_ASSERT_NULL(heap_remove_at(NULL, 0));
    TEST_ASSERT_EQ_INT(heap_size(NULL), 0);
    TEST_ASSERT_EQ_INT(heap_is_empty(NULL), 1);
    TEST_ASSERT_EQ_INT(heap_is_valid(NULL), 0);
}

static void test_heap_empty(void) {
    Heap *h = heap_create(cmp_int_asc, NULL, NULL);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQ_INT(heap_size(h), 0);
    TEST_ASSERT_EQ_INT(heap_is_empty(h), 1);
    TEST_ASSERT_NULL(heap_pop(h));
    TEST_ASSERT_NULL(heap_peek(h));
    TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);
    TEST_ASSERT_EQ_INT(heap_update(h, 0), STATUS_ERR_INVALID);
    TEST_ASSERT_NULL(heap_remove_at(h, 0));
    heap_clear(h);
    heap_destroy(h);
}

static void test_heap_validity_small(void) {
    Heap *h = heap_create(cmp_int_asc, NULL, NULL);
    TEST_ASSERT_NOT_NULL(h);

    int vals[15] = {50, 30, 20, 15, 8, 12, 10, 45, 60, 25, 18, 5, 2, 70, 1};
    for (int i = 0; i < 15; i++) {
        TEST_ASSERT_EQ_INT(heap_push(h, &vals[i]), STATUS_OK);
        TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);
    }
    TEST_ASSERT_EQ_INT(heap_size(h), 15);

    while (!heap_is_empty(h)) {
        void *p = heap_pop(h);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);
    }

    heap_destroy(h);
}

static void test_heap_1000_min(void) {
    srand(42);
    Heap *h = heap_create(cmp_int_asc, NULL, NULL);
    TEST_ASSERT_NOT_NULL(h);

    int vals[1000];
    for (int i = 0; i < 1000; i++) {
        vals[i] = rand() % 100000;
        TEST_ASSERT_EQ_INT(heap_push(h, &vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(heap_size(h), 1000);
    TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);

    int prev = -1;
    for (int i = 0; i < 1000; i++) {
        int *val = (int *)heap_pop(h);
        TEST_ASSERT_NOT_NULL(val);
        if (i > 0) {
            TEST_ASSERT(*val >= prev);
        }
        prev = *val;
    }
    TEST_ASSERT_EQ_INT(heap_size(h), 0);
    TEST_ASSERT_EQ_INT(heap_is_empty(h), 1);

    heap_destroy(h);
}

static void test_heap_1000_max(void) {
    srand(42);
    Heap *h = heap_create(cmp_int_desc, NULL, NULL);
    TEST_ASSERT_NOT_NULL(h);

    int vals[1000];
    for (int i = 0; i < 1000; i++) {
        vals[i] = rand() % 100000;
        TEST_ASSERT_EQ_INT(heap_push(h, &vals[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(heap_size(h), 1000);
    TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);

    int prev = 200000;
    for (int i = 0; i < 1000; i++) {
        int *val = (int *)heap_pop(h);
        TEST_ASSERT_NOT_NULL(val);
        if (i > 0) {
            TEST_ASSERT(*val <= prev);
        }
        prev = *val;
    }
    TEST_ASSERT_EQ_INT(heap_size(h), 0);
    TEST_ASSERT_EQ_INT(heap_is_empty(h), 1);

    heap_destroy(h);
}

static void test_heap_update_and_remove_at(void) {
    Heap *h = heap_create(cmp_tracked_asc, NULL, track_index_cb);
    TEST_ASSERT_NOT_NULL(h);

    TrackedItem items[8];
    for (int i = 0; i < 8; i++) {
        items[i].id = i;
        items[i].priority = (i + 1) * 10;
        items[i].heap_idx = 0;
        TEST_ASSERT_EQ_INT(heap_push(h, &items[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);

    /* Verify items correctly track their positions */
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT(items[i].heap_idx < 8);
    }

    /* Decrease key: items[6] originally priority 70 -> change to 5 (highest priority!) */
    items[6].priority = 5;
    TEST_ASSERT_EQ_INT(heap_update(h, items[6].heap_idx), STATUS_OK);
    TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);
    TEST_ASSERT_EQ_INT(items[6].heap_idx, 0);
    TEST_ASSERT_EQ_INT(((TrackedItem *)heap_peek(h))->id, 6);

    /* Increase key: items[6] change to 100 (lowest priority!) */
    items[6].priority = 100;
    TEST_ASSERT_EQ_INT(heap_update(h, items[6].heap_idx), STATUS_OK);
    TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);
    TEST_ASSERT(((TrackedItem *)heap_peek(h))->id != 6);

    /* heap_remove_at: remove items[2] */
    size_t target_idx = items[2].heap_idx;
    TrackedItem *rem = (TrackedItem *)heap_remove_at(h, target_idx);
    TEST_ASSERT_NOT_NULL(rem);
    TEST_ASSERT_EQ_INT(rem->id, 2);
    TEST_ASSERT_EQ_INT(heap_size(h), 7);
    TEST_ASSERT_EQ_INT(heap_is_valid(h), 1);

    heap_destroy(h);
}

static void test_heap_ownership(void) {
    g_heap_free_count = 0;
    Heap *h = heap_create(cmp_int_asc, heap_item_free, NULL);
    TEST_ASSERT_NOT_NULL(h);

    for (int i = 0; i < 6; i++) {
        int *p = (int *)malloc(sizeof(int));
        *p = (6 - i) * 10;
        heap_push(h, p);
    }
    TEST_ASSERT_EQ_INT(heap_size(h), 6);
    TEST_ASSERT_EQ_INT(g_heap_free_count, 0);

    /* Pop transfers ownership to caller; free_fn not called */
    int *top = (int *)heap_pop(h);
    TEST_ASSERT_NOT_NULL(top);
    TEST_ASSERT_EQ_INT(*top, 10);
    TEST_ASSERT_EQ_INT(g_heap_free_count, 0);
    free(top);

    /* remove_at transfers ownership to caller; free_fn not called */
    int *rem = (int *)heap_remove_at(h, 2);
    TEST_ASSERT_NOT_NULL(rem);
    TEST_ASSERT_EQ_INT(g_heap_free_count, 0);
    free(rem);

    /* heap_clear frees remaining 4 items */
    heap_clear(h);
    TEST_ASSERT_EQ_INT(g_heap_free_count, 4);
    TEST_ASSERT_EQ_INT(heap_size(h), 0);

    /* heap_destroy frees any newly added item */
    int *extra = (int *)malloc(sizeof(int));
    *extra = 99;
    heap_push(h, extra);
    heap_destroy(h);
    TEST_ASSERT_EQ_INT(g_heap_free_count, 5);
}

int main(void) {
    RUN_TEST(test_heap_null_args);
    RUN_TEST(test_heap_empty);
    RUN_TEST(test_heap_validity_small);
    RUN_TEST(test_heap_1000_min);
    RUN_TEST(test_heap_1000_max);
    RUN_TEST(test_heap_update_and_remove_at);
    RUN_TEST(test_heap_ownership);
    return test_summary();
}

