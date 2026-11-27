#ifndef TRANSPORT_API_HTTP_H
#define TRANSPORT_API_HTTP_H

/**
 * @file api_http.h
 * @brief CivetWeb HTTP adapter, routing under /api/, CORS headers, and error envelopes.
 */

#include "common.h"
#include "api_app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ApiHttpServer ApiHttpServer;

/**
 * @brief Starts the CivetWeb HTTP server bound to opt->bind_addr:opt->port and serving /api/.
 *
 * @param app Initialized AppCtx (borrowed; must outlive ApiHttpServer).
 * @param out Receives newly allocated ApiHttpServer handle (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL/INVALID/NOMEM on error.
 * @note Ownership: Stop and free with api_http_stop(). Complexity: O(1).
 */
Status api_http_start(AppCtx *app, ApiHttpServer **out);

/**
 * @brief Stops CivetWeb cleanly, waits for worker threads, and frees ApiHttpServer (NULL-safe).
 * @param srv Server handle.
 * @note Complexity: O(1) after active requests finish.
 */
void api_http_stop(ApiHttpServer *srv);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_API_HTTP_H */

