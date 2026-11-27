#ifndef TRANSPORT_API_JSON_H
#define TRANSPORT_API_JSON_H

/**
 * @file api_json.h
 * @brief Pure JSON response builders, request parsers, strict input validators, and HTTP error mapping.
 *
 * Caller must hold app_lock(ctx) while invoking builders that inspect AppCtx state.
 * All returned heap strings (*out) are NUL-terminated JSON owned by the caller (free() to release).
 */

#include <stddef.h>
#include "common.h"
#include "api_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define API_MAX_SCHEDULE_TRIPS 500
#define API_MAX_LOG_LINES      100

/**
 * @brief Parses a base-10 integer strictly within [min_val, max_val] (no whitespace or trailing chars).
 * @param s Input string (non-NULL).
 * @param min_val Inclusive lower bound.
 * @param max_val Inclusive upper bound.
 * @param out Destination integer pointer (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg, STATUS_ERR_INVALID on bad format/range.
 * @note Complexity: O(strlen(s)).
 */
Status api_parse_int_strict(const char *s, int min_val, int max_val, int *out);

/**
 * @brief Parses a finite double strictly within [min_val, max_val] (no whitespace or trailing chars).
 * @param s Input string (non-NULL).
 * @param min_val Inclusive lower bound.
 * @param max_val Inclusive upper bound.
 * @param out Destination double pointer (non-NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL on NULL arg, STATUS_ERR_INVALID on bad format/range.
 * @note Complexity: O(strlen(s)).
 */
Status api_parse_double_strict(const char *s, double min_val, double max_val, double *out);

/**
 * @brief Maps a Status code to an HTTP status code and machine-readable error code string.
 * @param st Status code.
 * @param code_out Optional output receiving static error code string (e.g. "not_found").
 * @return HTTP status code (400, 404, 409, or 500).
 * @note Ownership: code_out receives a static string literal. Complexity: O(1).
 */
int api_http_status_from_error(Status st, const char **code_out);

/**
 * @brief Builds a JSON error envelope {"error":{"code":"...","message":"..."}}.
 * @param code Machine-readable error code (non-NULL).
 * @param message Human-readable error message (non-NULL).
 * @return Allocated JSON string (caller frees), or NULL on allocation failure.
 * @note Complexity: O(strlen(code) + strlen(message)).
 */
char *api_json_error(const char *code, const char *message);

Status api_json_health(const AppCtx *ctx, char **out);
Status api_json_config(const AppCtx *ctx, char **out);
Status api_json_graph(const AppCtx *ctx, char **out);
Status api_json_stops(const AppCtx *ctx, char **out);
Status api_json_routes(const AppCtx *ctx, char **out);
Status api_json_geometry(const AppCtx *ctx, char **out);
Status api_json_state(AppCtx *ctx, char **out);
Status api_json_bus(const AppCtx *ctx, int bus_id, char **out);
Status api_json_schedule(const AppCtx *ctx, int has_from, int from_min,
                         int has_to, int to_min, int has_route, int route_id, char **out);
Status api_json_tree(const AppCtx *ctx, char **out);
Status api_json_forecast(const AppCtx *ctx, int stop_idx, char **out);
Status api_json_metrics(AppCtx *ctx, char **out);
Status api_json_alerts(const AppCtx *ctx, char **out);
Status api_json_history(const AppCtx *ctx, char **out);

/**
 * @brief Strictly parses and validates a POST /api/sim/control JSON body.
 *
 * Accepts {"action":"start|pause|resume|speed","speed":number} where speed is in [1, 3600].
 *
 * @param body Request body string (non-NULL).
 * @param action_buf Destination buffer for action string (non-NULL).
 * @param action_cap Size of action_buf.
 * @param has_action Receives 1 if "action" present, 0 otherwise (non-NULL).
 * @param has_speed Receives 1 if "speed" present, 0 otherwise (non-NULL).
 * @param speed_out Receives parsed speed if *has_speed (non-NULL).
 * @return STATUS_OK on valid payload, STATUS_ERR_NULL/INVALID on error.
 * @note Complexity: O(strlen(body)).
 */
Status api_json_sim_control_parse(const char *body, char *action_buf, size_t action_cap,
                                  int *has_action, int *has_speed, double *speed_out);

Status api_json_sim_control_ok(const AppCtx *ctx, char **out);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_API_JSON_H */

