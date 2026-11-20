#include "common.h"

const char *status_to_string(Status s) {
    switch (s) {
        case STATUS_OK:
            return "STATUS_OK";
        case STATUS_ERR_NULL:
            return "STATUS_ERR_NULL";
        case STATUS_ERR_NOMEM:
            return "STATUS_ERR_NOMEM";
        case STATUS_ERR_NOT_FOUND:
            return "STATUS_ERR_NOT_FOUND";
        case STATUS_ERR_DUPLICATE:
            return "STATUS_ERR_DUPLICATE";
        case STATUS_ERR_EMPTY:
            return "STATUS_ERR_EMPTY";
        case STATUS_ERR_INVALID:
            return "STATUS_ERR_INVALID";
        default:
            return "STATUS_UNKNOWN";
    }
}

