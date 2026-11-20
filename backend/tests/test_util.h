#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;
static int g_assertions_run = 0;
static int g_assertions_failed = 0;

#define TEST_ASSERT(cond) do { \
    g_assertions_run++; \
    if (!(cond)) { \
        g_assertions_failed++; \
        printf("  FAIL: %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define TEST_ASSERT_EQ_INT(a, b) do { \
    g_assertions_run++; \
    long long _tu_val_a = (long long)(a); \
    long long _tu_val_b = (long long)(b); \
    if (_tu_val_a != _tu_val_b) { \
        g_assertions_failed++; \
        printf("  FAIL: %s:%d: %s == %s (expected %lld, got %lld)\n", \
               __FILE__, __LINE__, #a, #b, _tu_val_b, _tu_val_a); \
    } \
} while (0)

#define TEST_ASSERT_NULL(p) do { \
    g_assertions_run++; \
    const void *_tu_p = (const void *)(p); \
    if (_tu_p != NULL) { \
        g_assertions_failed++; \
        printf("  FAIL: %s:%d: %s is not NULL (got %p)\n", __FILE__, __LINE__, #p, _tu_p); \
    } \
} while (0)

#define TEST_ASSERT_NOT_NULL(p) do { \
    g_assertions_run++; \
    const void *_tu_p = (const void *)(p); \
    if (_tu_p == NULL) { \
        g_assertions_failed++; \
        printf("  FAIL: %s:%d: %s is NULL\n", __FILE__, __LINE__, #p); \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    printf("[RUN ] %s\n", #fn); \
    int _tu_prev_failed = g_assertions_failed; \
    g_tests_run++; \
    fn(); \
    if (g_assertions_failed > _tu_prev_failed) { \
        g_tests_failed++; \
        printf("[FAIL] %s\n", #fn); \
    } else { \
        printf("[PASS] %s\n", #fn); \
    } \
} while (0)

static inline int test_summary(void) {
    printf("----------------------------------------\n");
    printf("Tests run: %d | Passed: %d | Failed: %d\n",
           g_tests_run, g_tests_run - g_tests_failed, g_tests_failed);
    printf("Assertions run: %d | Failed: %d\n",
           g_assertions_run, g_assertions_failed);
    printf("----------------------------------------\n");
    return (g_tests_failed > 0 || g_assertions_failed > 0) ? 1 : 0;
}

#endif /* TEST_UTIL_H */

