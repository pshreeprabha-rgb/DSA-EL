#ifndef TRANSPORT_API_PLATFORM_H
#define TRANSPORT_API_PLATFORM_H

/**
 * @file api_platform.h
 * @brief Platform abstraction for monotonic timing, sleep, mutexes, threads, and signals.
 *
 * POSIX headers and syscalls are isolated to src/api_platform.c.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ApiMutex ApiMutex;
typedef struct ApiThread ApiThread;
typedef void *(*ApiThreadFn)(void *arg);

/**
 * @brief Returns monotonic clock time in seconds.
 * @return Elapsed monotonic seconds (>= 0.0).
 * @note Complexity: O(1).
 */
double api_monotonic_s(void);

/**
 * @brief Sleeps for approximately ms milliseconds (clamped at >= 0).
 * @param ms Duration in milliseconds.
 * @note Complexity: O(1).
 */
void api_sleep_ms(int ms);

/**
 * @brief Creates an initialized mutex.
 * @return Allocated ApiMutex pointer, or NULL on failure.
 * @note Ownership: Caller must free with api_mutex_destroy(). Complexity: O(1).
 */
ApiMutex *api_mutex_create(void);

/**
 * @brief Destroys an ApiMutex (NULL-safe).
 * @param m Mutex handle.
 * @note Complexity: O(1).
 */
void api_mutex_destroy(ApiMutex *m);

/**
 * @brief Acquires the mutex.
 * @param m Mutex handle (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL if m is NULL, STATUS_ERR_INVALID on OS failure.
 * @note Complexity: O(1).
 */
Status api_mutex_lock(ApiMutex *m);

/**
 * @brief Releases the mutex.
 * @param m Mutex handle (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL if m is NULL, STATUS_ERR_INVALID on OS failure.
 * @note Complexity: O(1).
 */
Status api_mutex_unlock(ApiMutex *m);

/**
 * @brief Spawns a joinable worker thread running fn(arg).
 * @param fn Entry function (non-NULL).
 * @param arg User argument passed to fn.
 * @param out Output pointer receiving allocated ApiThread handle (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL/NOMEM/INVALID on failure.
 * @note Ownership: Join and free handle with api_thread_join(). Complexity: O(1).
 */
Status api_thread_create(ApiThreadFn fn, void *arg, ApiThread **out);

/**
 * @brief Joins the thread and frees the ApiThread handle (NULL-safe).
 * @param t Thread handle.
 * @return STATUS_OK on success, STATUS_ERR_INVALID if pthread_join fails.
 * @note Complexity: O(1) wait for thread exit.
 */
Status api_thread_join(ApiThread *t);

/**
 * @brief Installs SIGINT and SIGTERM handlers setting an internal shutdown flag.
 * @return STATUS_OK on success, STATUS_ERR_INVALID if handler registration fails.
 * @note Complexity: O(1).
 */
Status api_signals_install(void);

/**
 * @brief Queries whether SIGINT or SIGTERM has been received since installation/clear.
 * @return 1 if termination signal received, 0 otherwise.
 * @note Complexity: O(1).
 */
int api_signal_stop_requested(void);

/**
 * @brief Resets the internal signal shutdown flag.
 * @note Complexity: O(1).
 */
void api_signal_clear(void);

/**
 * @brief Checks whether path exists and is a directory.
 * @param path Filesystem path.
 * @return 1 if directory exists, 0 otherwise.
 * @note Complexity: O(1).
 */
int api_dir_exists(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_API_PLATFORM_H */

