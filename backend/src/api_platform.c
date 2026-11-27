#define _POSIX_C_SOURCE 200809L

#include "api_platform.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

struct ApiMutex {
    pthread_mutex_t mu;
    int initialized;
};

struct ApiThread {
    pthread_t tid;
};

/* Async-signal-safe termination flag set by SIGINT/SIGTERM handlers */
static volatile sig_atomic_t g_api_signal_stop = 0;

static void api_signal_handler(int sig) {
    (void)sig;
    g_api_signal_stop = 1;
}

double api_monotonic_s(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void api_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
        if (g_api_signal_stop != 0) {
            break;
        }
    }
}

ApiMutex *api_mutex_create(void) {
    ApiMutex *m = (ApiMutex *)calloc(1, sizeof(ApiMutex));
    if (!m) return NULL;
    if (pthread_mutex_init(&m->mu, NULL) != 0) {
        free(m);
        return NULL;
    }
    m->initialized = 1;
    return m;
}

void api_mutex_destroy(ApiMutex *m) {
    if (!m) return;
    if (m->initialized) {
        pthread_mutex_destroy(&m->mu);
        m->initialized = 0;
    }
    free(m);
}

Status api_mutex_lock(ApiMutex *m) {
    if (!m) return STATUS_ERR_NULL;
    if (pthread_mutex_lock(&m->mu) != 0) return STATUS_ERR_INVALID;
    return STATUS_OK;
}

Status api_mutex_unlock(ApiMutex *m) {
    if (!m) return STATUS_ERR_NULL;
    if (pthread_mutex_unlock(&m->mu) != 0) return STATUS_ERR_INVALID;
    return STATUS_OK;
}

Status api_thread_create(ApiThreadFn fn, void *arg, ApiThread **out) {
    if (!fn || !out) return STATUS_ERR_NULL;
    *out = NULL;
    ApiThread *t = (ApiThread *)calloc(1, sizeof(ApiThread));
    if (!t) return STATUS_ERR_NOMEM;
    if (pthread_create(&t->tid, NULL, fn, arg) != 0) {
        free(t);
        return STATUS_ERR_INVALID;
    }
    *out = t;
    return STATUS_OK;
}

Status api_thread_join(ApiThread *t) {
    if (!t) return STATUS_OK;
    int rc = pthread_join(t->tid, NULL);
    free(t);
    return (rc == 0) ? STATUS_OK : STATUS_ERR_INVALID;
}

Status api_signals_install(void) {
    g_api_signal_stop = 0;
    struct sigaction sa;
    sa.sa_handler = api_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) != 0) return STATUS_ERR_INVALID;
    if (sigaction(SIGTERM, &sa, NULL) != 0) return STATUS_ERR_INVALID;
    return STATUS_OK;
}

int api_signal_stop_requested(void) {
    return (g_api_signal_stop != 0) ? 1 : 0;
}

void api_signal_clear(void) {
    g_api_signal_stop = 0;
}

int api_dir_exists(const char *path) {
    if (!path || path[0] == '\0') return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

