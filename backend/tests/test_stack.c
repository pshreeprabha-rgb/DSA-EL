#include <stdio.h>
#include <stdlib.h>
#include "test_util.h"
#include "stack.h"

static int g_stack_free_count = 0;

static void stack_item_free(void *item) {
    if (item) {
        g_stack_free_count++;
        free(item);
    }
}

static void test_stack_null_args(void) {
    stack_destroy(NULL);
    stack_clear(NULL);
    TEST_ASSERT_EQ_INT(stack_push(NULL, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_NULL(stack_pop(NULL));
    TEST_ASSERT_NULL(stack_peek(NULL));
    TEST_ASSERT_EQ_INT(stack_size(NULL), 0);
    TEST_ASSERT_EQ_INT(stack_is_empty(NULL), 1);
    TEST_ASSERT_NULL(stack_get(NULL, 0));
}

static void test_stack_empty(void) {
    Stack *s = stack_create(NULL);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQ_INT(stack_size(s), 0);
    TEST_ASSERT_EQ_INT(stack_is_empty(s), 1);
    TEST_ASSERT_NULL(stack_pop(s));
    TEST_ASSERT_NULL(stack_peek(s));
    TEST_ASSERT_NULL(stack_get(s, 0));
    stack_clear(s);
    stack_destroy(s);
}

static void test_stack_basic_ops(void) {
    int v[3] = {10, 20, 30};
    Stack *s = stack_create(NULL);
    TEST_ASSERT_NOT_NULL(s);

    TEST_ASSERT_EQ_INT(stack_push(s, &v[0]), STATUS_OK);
    TEST_ASSERT_EQ_INT(stack_size(s), 1);
    TEST_ASSERT_EQ_INT(stack_is_empty(s), 0);
    TEST_ASSERT_EQ_INT(*(int *)stack_peek(s), 10);
    TEST_ASSERT_EQ_INT(*(int *)stack_get(s, 0), 10);
    TEST_ASSERT_NULL(stack_get(s, 1));

    TEST_ASSERT_EQ_INT(stack_push(s, &v[1]), STATUS_OK);
    TEST_ASSERT_EQ_INT(stack_push(s, &v[2]), STATUS_OK);
    TEST_ASSERT_EQ_INT(stack_size(s), 3);

    /* stack_get: 0 is top (30), 1 is 20, 2 is 10 */
    TEST_ASSERT_EQ_INT(*(int *)stack_get(s, 0), 30);
    TEST_ASSERT_EQ_INT(*(int *)stack_get(s, 1), 20);
    TEST_ASSERT_EQ_INT(*(int *)stack_get(s, 2), 10);
    TEST_ASSERT_NULL(stack_get(s, 3));

    int *p1 = (int *)stack_pop(s);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_EQ_INT(*p1, 30);
    TEST_ASSERT_EQ_INT(stack_size(s), 2);

    int *p2 = (int *)stack_pop(s);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_EQ_INT(*p2, 20);

    int *p3 = (int *)stack_pop(s);
    TEST_ASSERT_NOT_NULL(p3);
    TEST_ASSERT_EQ_INT(*p3, 10);

    TEST_ASSERT_EQ_INT(stack_size(s), 0);
    TEST_ASSERT_EQ_INT(stack_is_empty(s), 1);
    TEST_ASSERT_NULL(stack_pop(s));

    stack_destroy(s);
}

static void test_stack_1000_items(void) {
    Stack *s = stack_create(NULL);
    TEST_ASSERT_NOT_NULL(s);

    int items[1000];
    for (int i = 0; i < 1000; i++) {
        items[i] = i * 3 + 1;
        TEST_ASSERT_EQ_INT(stack_push(s, &items[i]), STATUS_OK);
    }
    TEST_ASSERT_EQ_INT(stack_size(s), 1000);
    TEST_ASSERT_EQ_INT(*(int *)stack_peek(s), items[999]);
    TEST_ASSERT_EQ_INT(*(int *)stack_get(s, 0), items[999]);
    TEST_ASSERT_EQ_INT(*(int *)stack_get(s, 999), items[0]);

    for (int i = 999; i >= 0; i--) {
        int *val = (int *)stack_pop(s);
        TEST_ASSERT_NOT_NULL(val);
        TEST_ASSERT_EQ_INT(*val, items[i]);
    }
    TEST_ASSERT_EQ_INT(stack_size(s), 0);
    TEST_ASSERT_EQ_INT(stack_is_empty(s), 1);

    stack_destroy(s);
}

static void test_stack_ownership(void) {
    g_stack_free_count = 0;
    Stack *s = stack_create(stack_item_free);
    TEST_ASSERT_NOT_NULL(s);

    for (int i = 0; i < 5; i++) {
        int *p = (int *)malloc(sizeof(int));
        *p = i;
        stack_push(s, p);
    }
    TEST_ASSERT_EQ_INT(stack_size(s), 5);
    TEST_ASSERT_EQ_INT(g_stack_free_count, 0);

    /* Popping transfers ownership to caller; FreeFn must NOT be called */
    int *popped = (int *)stack_pop(s);
    TEST_ASSERT_NOT_NULL(popped);
    TEST_ASSERT_EQ_INT(*popped, 4);
    TEST_ASSERT_EQ_INT(g_stack_free_count, 0);
    free(popped);

    /* stack_clear frees remaining 4 items */
    stack_clear(s);
    TEST_ASSERT_EQ_INT(g_stack_free_count, 4);
    TEST_ASSERT_EQ_INT(stack_size(s), 0);

    /* stack_destroy frees remaining items */
    int *p_extra = (int *)malloc(sizeof(int));
    *p_extra = 999;
    stack_push(s, p_extra);
    stack_destroy(s);
    TEST_ASSERT_EQ_INT(g_stack_free_count, 5);
}

int main(void) {
    RUN_TEST(test_stack_null_args);
    RUN_TEST(test_stack_empty);
    RUN_TEST(test_stack_basic_ops);
    RUN_TEST(test_stack_1000_items);
    RUN_TEST(test_stack_ownership);
    return test_summary();
}

