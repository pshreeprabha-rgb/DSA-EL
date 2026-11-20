#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef enum Status {
    STATUS_OK = 0,
    STATUS_ERR_NULL,
    STATUS_ERR_NOMEM,
    STATUS_ERR_NOT_FOUND,
    STATUS_ERR_DUPLICATE,
    STATUS_ERR_EMPTY,
    STATUS_ERR_INVALID
} Status;

/**
 * Returns a static, human-readable string representation of a Status code.
 *
 * @param s Status code to convert.
 * @return String literal corresponding to status s.
 * Ownership: Returns pointer to static constant string. Caller does not own memory.
 * Complexity: O(1)
 */
const char *status_to_string(Status s);

typedef void (*FreeFn)(void *item);
typedef int  (*CmpFn)(const void *a, const void *b);
typedef void (*VisitFn)(void *item, void *ctx);
typedef int  (*PredFn)(const void *item, const void *ctx);

#endif /* COMMON_H */

