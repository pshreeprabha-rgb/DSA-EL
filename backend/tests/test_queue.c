#include <stdio.h>
#include <stdlib.h>
#include "test_util.h"
#include "queue.h"

static int g_queue_free_count = 0;

static void queue_item_free(void *item) {
    if (item) {
        g_queue_free_count++;
        free(item);
    }
}

typedef struct QueueForeachCtx {
    int arr[64];
    size_t count;
} QueueForeachCtx;

static void queue_collect(void *item, void *ctx) {
    QueueForeachCtx *c = (QueueForeachCtx *)ctx;
    if (c->count < 64) {
        c->arr[c->count++] = *(int *)item;
    }
}

static void test_queue_null_args(void) {
    queue_destroy(NULL);
    queue_clear(NULL);
    TEST_ASSERT_EQ_INT(queue_enqueue(NULL, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_NULL(queue_dequeue(NULL));
    TEST_ASSERT_NULL(queue_peek(NULL));
    TEST_ASSERT_EQ_INT(queue_size(NULL), 0);
    TEST_ASSERT_EQ_INT(queue_is_empty(NULL), 1);
    queue_foreach(NULL, queue_collect, NULL);
}

static void test_queue_empty(void) {
    Queue *q = queue_create(NULL);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQ_INT(queue_size(q), 0);
    TEST_ASSERT_EQ_INT(queue_is_empty(q), 1);
    TEST_ASSERT_NULL(queue_dequeue(q));
    TEST_ASSERT_NULL(queue_peek(q));
    queue_clear(q);
    queue_destroy(q);
}

static void test_queue_fifo(void) {
    int v[5] = {10, 20, 30, 40, 50};
    Queue *q = queue_create(NULL);
    TEST_ASSERT_NOT_NULL(q);

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ_INT(queue_enqueue(q, &v[i]), STATUS_OK);
        TEST_ASSERT_EQ_INT(queue_size(q), (size_t)(i + 1));
    }
    TEST_ASSERT_EQ_INT(*(int *)queue_peek(q), 10);

    for (int i = 0; i < 5; i++) {
        int *val = (int *)queue_dequeue(q);
        TEST_ASSERT_NOT_NULL(val);
        TEST_ASSERT_EQ_INT(*val, v[i]);
    }
    TEST_ASSERT_EQ_INT(queue_size(q), 0);
    TEST_ASSERT_EQ_INT(queue_is_empty(q), 1);
    TEST_ASSERT_NULL(queue_dequeue(q));

    queue_destroy(q);
}

static void test_queue_wrap_around_growth(void) {
    /* Required test: enqueue 6, dequeue 4, enqueue 20, then verify the order of all remaining items */
    Queue *q = queue_create(NULL);
    TEST_ASSERT_NOT_NULL(q);

    int data[30];
    for (int i = 0; i < 30; i++) {
        data[i] = i;
    }

    /* Enqueue 6 items: [0, 1, 2, 3, 4, 5] */
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQ_INT(queue_enqueue(q, &data[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(queue_size(q), 6);

    /* Dequeue 4 items: 0, 1, 2, 3. Remaining: 4, 5. Head is now index 4 */
    for (int i = 0; i < 4; i++) {
        int *val = (int *)queue_dequeue(q);
        TEST_ASSERT_NOT_NULL(val);
        TEST_ASSERT_EQ_INT(*val, data[i]);
    }
    TEST_ASSERT_EQ_INT(queue_size(q), 2);
    TEST_ASSERT_EQ_INT(*(int *)queue_peek(q), 4);

    /* Enqueue 20 more items (data[6] to data[25]): this wraps around initial capacity 8 and triggers resize! */
    for (int i = 6; i < 26; i++) {
        TEST_ASSERT_EQ_INT(queue_enqueue(q, &data[i]), STATUS_OK);
    }
    /* Total remaining should be 2 + 20 = 22 items */
    TEST_ASSERT_EQ_INT(queue_size(q), 22);

    /* Verify order of remaining items: 4, 5, 6, ..., 25 */
    for (int i = 4; i < 26; i++) {
        int *val = (int *)queue_dequeue(q);
        TEST_ASSERT_NOT_NULL(val);
        TEST_ASSERT_EQ_INT(*val, data[i]);
    }
    TEST_ASSERT_EQ_INT(queue_size(q), 0);
    TEST_ASSERT_EQ_INT(queue_is_empty(q), 1);

    queue_destroy(q);
}

static void test_queue_foreach(void) {
    int v[4] = {100, 200, 300, 400};
    Queue *q = queue_create(NULL);
    for (int i = 0; i < 4; i++) {
        queue_enqueue(q, &v[i]);
    }

    QueueForeachCtx ctx = { {0}, 0 };
    queue_foreach(q, queue_collect, &ctx);

    TEST_ASSERT_EQ_INT(ctx.count, 4);
    for (size_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQ_INT(ctx.arr[i], v[i]);
    }

    queue_destroy(q);
}

static void test_queue_ownership(void) {
    g_queue_free_count = 0;
    Queue *q = queue_create(queue_item_free);
    TEST_ASSERT_NOT_NULL(q);

    for (int i = 0; i < 5; i++) {
        int *p = (int *)malloc(sizeof(int));
        *p = i;
        queue_enqueue(q, p);
    }
    TEST_ASSERT_EQ_INT(queue_size(q), 5);
    TEST_ASSERT_EQ_INT(g_queue_free_count, 0);

    /* Dequeue transfers ownership; must not free */
    int *val = (int *)queue_dequeue(q);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQ_INT(*val, 0);
    TEST_ASSERT_EQ_INT(g_queue_free_count, 0);
    free(val);

    /* queue_clear frees remaining 4 items */
    queue_clear(q);
    TEST_ASSERT_EQ_INT(g_queue_free_count, 4);
    TEST_ASSERT_EQ_INT(queue_size(q), 0);

    /* queue_destroy frees remaining items */
    int *extra = (int *)malloc(sizeof(int));
    *extra = 42;
    queue_enqueue(q, extra);
    queue_destroy(q);
    TEST_ASSERT_EQ_INT(g_queue_free_count, 5);
}

int main(void) {
    RUN_TEST(test_queue_null_args);
    RUN_TEST(test_queue_empty);
    RUN_TEST(test_queue_fifo);
    RUN_TEST(test_queue_wrap_around_growth);
    RUN_TEST(test_queue_foreach);
    RUN_TEST(test_queue_ownership);
    return test_summary();
}

